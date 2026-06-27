#include "ble_keyboard.h"
#include "hardware_config.h"

#if defined(BOARD_HAS_BLE_KEYBOARD) && BOARD_HAS_BLE_KEYBOARD

#include "os_core.h"
#include "bbq20_keyboard.h"

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ble_keyboard";

// HID Service UUID (0x1812) in little-endian for BLE advertising comparison.
static const uint8_t HID_SERVICE_UUID[] = {0x12, 0x18};

#define MAX_SCAN_RESULTS 16
#define MAX_NAME_LEN 32

typedef struct {
    esp_bd_addr_t addr;
    uint8_t addr_type;
    char name[MAX_NAME_LEN];
    int rssi;
    bool is_hid;
} scan_result_t;

static scan_result_t scan_results[MAX_SCAN_RESULTS];
static int scan_count = 0;
static bool ble_initialized = false;
static bool ble_scanning = false;
static int scan_duration = 10;

static esp_hidh_dev_t *connected_dev = NULL;
static char connected_name[MAX_NAME_LEN] = "";

// Semaphore to signal scan completion (for synchronous scan API).
static SemaphoreHandle_t scan_done_sem = NULL;

// Previous keyboard report keys for detecting press/release transitions.
// HID boot keyboard report: up to 6 simultaneous key codes.
static uint8_t prev_keys[6] = {0};

// --- HID usage → keycode mapping ---

static uint8_t hid_usage_to_keycode(uint8_t usage) {
    if (usage >= 0x04 && usage <= 0x1D) return 'a' + (usage - 0x04);
    if (usage >= 0x1E && usage <= 0x26) return '1' + (usage - 0x1E);
    if (usage == 0x27) return '0';
    if (usage == 0x28) return '\n';
    if (usage == 0x29) return BT2I2C_KEY_ESC;
    if (usage == 0x2A) return 0x08;
    if (usage == 0x2B) return '\t';
    if (usage == 0x2C) return ' ';
    if (usage == 0x2D) return '-';
    if (usage == 0x2E) return '=';
    if (usage == 0x2F) return '[';
    if (usage == 0x30) return ']';
    if (usage == 0x31) return '\\';
    if (usage == 0x33) return ';';
    if (usage == 0x34) return '\'';
    if (usage == 0x35) return '`';
    if (usage == 0x36) return ',';
    if (usage == 0x37) return '.';
    if (usage == 0x38) return '/';
    if (usage >= 0x3A && usage <= 0x45) return BT2I2C_KEY_F1 + (usage - 0x3A);
    if (usage == 0x46) return BT2I2C_KEY_PRTSCR;
    if (usage == 0x47) return BT2I2C_KEY_SCRLK;
    if (usage == 0x48) return BT2I2C_KEY_PAUSE;
    if (usage == 0x49) return BT2I2C_KEY_INSERT;
    if (usage == 0x4A) return BT2I2C_KEY_HOME;
    if (usage == 0x4B) return BT2I2C_KEY_PGUP;
    if (usage == 0x4C) return BT2I2C_KEY_DELETE;
    if (usage == 0x4D) return BT2I2C_KEY_END;
    if (usage == 0x4E) return BT2I2C_KEY_PGDN;
    if (usage == 0x4F) return BT2I2C_KEY_RIGHT;
    if (usage == 0x50) return BT2I2C_KEY_LEFT;
    if (usage == 0x51) return BT2I2C_KEY_DOWN;
    if (usage == 0x52) return BT2I2C_KEY_UP;
    return 0;
}

static uint8_t hid_modifiers_to_os(uint8_t hid_mods) {
    uint8_t mods = 0;
    if (hid_mods & 0x01) mods |= MODIFIER_CTRL;
    if (hid_mods & 0x02) mods |= MODIFIER_SHIFT;
    if (hid_mods & 0x04) mods |= MODIFIER_ALT;
    if (hid_mods & 0x08) mods |= MODIFIER_FN;
    if (hid_mods & 0x10) mods |= MODIFIER_CTRL;
    if (hid_mods & 0x20) mods |= MODIFIER_SHIFT;
    if (hid_mods & 0x40) mods |= MODIFIER_ALT;
    if (hid_mods & 0x80) mods |= MODIFIER_FN;
    return mods;
}

static void send_key_event(uint8_t hid_usage, bool pressed, uint8_t modifiers) {
    uint8_t key_code = hid_usage_to_keycode(hid_usage);
    if (key_code == 0) return;

    event_t event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_KEYBOARD;
    event.keyboard.key = (char)key_code;
    event.keyboard.pressed = pressed;
    event.keyboard.modifiers = modifiers;
    event.keyboard.raw_key_code = hid_usage;
    os_post_event(&event);
}

// Parse a standard HID boot keyboard report and emit press/release events.
static void parse_keyboard_report(const uint8_t *data, uint16_t len) {
    if (len < 3) return;

    uint8_t modifiers = hid_modifiers_to_os(data[0]);
    const uint8_t *curr_keys = &data[2];
    int curr_count = (int)len - 2;
    if (curr_count > 6) curr_count = 6;

    // Newly pressed keys
    for (int i = 0; i < curr_count; i++) {
        uint8_t key = curr_keys[i];
        if (key == 0) continue;
        bool was_pressed = false;
        for (int j = 0; j < 6; j++) {
            if (prev_keys[j] == key) { was_pressed = true; break; }
        }
        if (!was_pressed) {
            send_key_event(key, true, modifiers);
        }
    }

    // Released keys
    for (int i = 0; i < 6; i++) {
        uint8_t key = prev_keys[i];
        if (key == 0) continue;
        bool still_pressed = false;
        for (int j = 0; j < curr_count; j++) {
            if (curr_keys[j] == key) { still_pressed = true; break; }
        }
        if (!still_pressed) {
            send_key_event(key, false, modifiers);
        }
    }

    // Update previous state
    memset(prev_keys, 0, sizeof(prev_keys));
    for (int i = 0; i < curr_count && i < 6; i++) {
        prev_keys[i] = curr_keys[i];
    }
}

// --- HID Host event handler ---

static void hidh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)data;

    switch (event) {
        case ESP_HIDH_OPEN_EVENT: {
            if (param->open.status == ESP_OK) {
                const char *name = esp_hidh_dev_name_get(param->open.dev);
                if (name) {
                    strncpy(connected_name, name, MAX_NAME_LEN - 1);
                    connected_name[MAX_NAME_LEN - 1] = '\0';
                } else {
                    strcpy(connected_name, "BLE Keyboard");
                }
                connected_dev = param->open.dev;
                ESP_LOGI(TAG, "Connected: %s", connected_name);
            } else {
                ESP_LOGE(TAG, "Open failed: %s", esp_err_to_name(param->open.status));
                connected_dev = NULL;
                connected_name[0] = '\0';
            }
            break;
        }
        case ESP_HIDH_INPUT_EVENT: {
            if (param->input.usage == ESP_HID_USAGE_KEYBOARD ||
                param->input.usage == ESP_HID_USAGE_GENERIC) {
                parse_keyboard_report(param->input.data, param->input.length);
            }
            break;
        }
        case ESP_HIDH_CLOSE_EVENT: {
            ESP_LOGI(TAG, "Disconnected");
            if (connected_dev) {
                esp_hidh_dev_free(connected_dev);
                connected_dev = NULL;
            }
            memset(prev_keys, 0, sizeof(prev_keys));
            connected_name[0] = '\0';
            break;
        }
        default:
            break;
    }
}

// --- GAP scan event handler ---

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
            if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                esp_ble_gap_start_scanning(scan_duration);
            }
            break;
        }
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT: {
            if (param->scan_start_cmpl.status == ESP_OK) {
                ble_scanning = true;
                ESP_LOGI(TAG, "Scan started");
            } else {
                ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(param->scan_start_cmpl.status));
            }
            break;
        }
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
            if (scan_count >= MAX_SCAN_RESULTS) break;

            esp_bd_addr_t bda;
            memcpy(bda, param->scan_rst.bda, sizeof(esp_bd_addr_t));

            // Check if already in results
            for (int i = 0; i < scan_count; i++) {
                if (memcmp(scan_results[i].addr, bda, sizeof(esp_bd_addr_t)) == 0) {
                    // Update RSSI
                    scan_results[i].rssi = param->scan_rst.rssi;
                    return;
                }
            }

            // Check if device advertises HID service (UUID 0x1812)
            bool is_hid = false;
            uint8_t *adv_data = param->scan_rst.ble_adv;
            uint8_t adv_len = param->scan_rst.adv_data_len;

            for (int i = 0; i + 1 < adv_len;) {
                uint8_t field_len = adv_data[i];
                uint8_t field_type = adv_data[i + 1];
                if (field_len == 0) break;
                if (i + 1 + field_len > adv_len) break;

                // 0x02 = Incomplete list of 16-bit service UUIDs
                // 0x03 = Complete list of 16-bit service UUIDs
                if (field_type == 0x02 || field_type == 0x03) {
                    for (int j = i + 2; j + 1 < i + 1 + field_len; j += 2) {
                        if (adv_data[j] == HID_SERVICE_UUID[0] &&
                            adv_data[j + 1] == HID_SERVICE_UUID[1]) {
                            is_hid = true;
                        }
                    }
                }
                // 0x07 = Complete list of 128-bit service UUIDs (HID-over-GATT)
                // We only check 16-bit UUIDs for simplicity.

                i += field_len + 1;
            }

            // Accept all devices if we can't parse (some keyboards don't
            // advertise the service in every packet), but prefer HID ones.
            scan_result_t *r = &scan_results[scan_count];
            memcpy(r->addr, bda, sizeof(esp_bd_addr_t));
            r->addr_type = param->scan_rst.ble_addr_type;
            r->rssi = param->scan_rst.rssi;
            r->is_hid = is_hid;

            // Try to get name from advertising data
            r->name[0] = '\0';
            for (int i = 0; i + 1 < adv_len;) {
                uint8_t field_len = adv_data[i];
                uint8_t field_type = adv_data[i + 1];
                if (field_len == 0) break;
                if (i + 1 + field_len > adv_len) break;

                // 0x08 = Shortened local name, 0x09 = Complete local name
                if (field_type == 0x08 || field_type == 0x09) {
                    int name_len = field_len - 1;
                    if (name_len >= MAX_NAME_LEN) name_len = MAX_NAME_LEN - 1;
                    memcpy(r->name, &adv_data[i + 2], name_len);
                    r->name[name_len] = '\0';
                    break;
                }
                i += field_len + 1;
            }

            if (r->name[0] == '\0') {
                snprintf(r->name, MAX_NAME_LEN, "%02X:%02X:%02X:%02X:%02X:%02X",
                         bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            }

            ESP_LOGI(TAG, "Found: %s (RSSI %d, HID=%d)", r->name, r->rssi, is_hid);
            scan_count++;
            break;
        }
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT: {
            ble_scanning = false;
            ESP_LOGI(TAG, "Scan stopped (%d devices found)", scan_count);
            if (scan_done_sem) {
                xSemaphoreGive(scan_done_sem);
            }
            break;
        }
        default:
            break;
    }
}

// --- Public API ---

bool ble_keyboard_init(void) {
    if (ble_initialized) return true;

    ESP_LOGI(TAG, "Initializing BLE HID Host...");
    ESP_LOGI(TAG, "Internal heap: %u bytes (largest: %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    esp_err_t ret;

    // Release Classic BT memory (not used — ESP32-S3 is BLE only).
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Controller init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Controller enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Register GAP callback and set scan parameters.
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize HID Host.
    esp_hidh_config_t hidh_config = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ret = esp_hidh_init(&hidh_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HIDH init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ble_initialized = true;
    ESP_LOGI(TAG, "BLE HID Host ready");
    return true;
}

bool ble_keyboard_is_available(void) {
    return ble_initialized;
}

int ble_keyboard_start_scan(int duration_seconds) {
    if (!ble_initialized) return 0;

    // Clear previous results
    scan_count = 0;
    scan_duration = duration_seconds > 0 ? duration_seconds : 10;

    // Create semaphore if not already created
    if (!scan_done_sem) {
        scan_done_sem = xSemaphoreCreateBinary();
    }
    if (scan_done_sem) {
        xSemaphoreTake(scan_done_sem, 0); // Clear any pending signal
    }

    static esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x0050,
        .scan_window = 0x0030,
        .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE,
    };

    esp_err_t ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set scan params failed: %s", esp_err_to_name(ret));
        return 0;
    }

    // Wait for scan to complete (the scan duration is set in the GAP callback).
    if (scan_done_sem) {
        xSemaphoreTake(scan_done_sem, pdMS_TO_TICKS((duration_seconds + 2) * 1000));
    }

    return scan_count;
}

int ble_keyboard_get_scan_count(void) {
    return scan_count;
}

const char *ble_keyboard_get_scan_name(int index) {
    if (index < 0 || index >= scan_count) return "";
    return scan_results[index].name;
}

int ble_keyboard_get_scan_rssi(int index) {
    if (index < 0 || index >= scan_count) return 0;
    return scan_results[index].rssi;
}

const char *ble_keyboard_get_scan_addr(int index) {
    if (index < 0 || index >= scan_count) return "";
    static char addr_str[18];
    uint8_t *a = scan_results[index].addr;
    snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             a[0], a[1], a[2], a[3], a[4], a[5]);
    return addr_str;
}

bool ble_keyboard_connect(int scan_index) {
    if (!ble_initialized || scan_index < 0 || scan_index >= scan_count) {
        return false;
    }
    if (connected_dev) {
        ESP_LOGW(TAG, "Already connected, disconnecting first...");
        esp_hidh_dev_close(connected_dev);
        connected_dev = NULL;
    }

    scan_result_t *r = &scan_results[scan_index];
    ESP_LOGI(TAG, "Connecting to %s...", r->name);

    esp_hidh_dev_t *dev = esp_hidh_dev_open(r->addr, ESP_HID_TRANSPORT_BLE, r->addr_type);
    if (dev == NULL) {
        ESP_LOGE(TAG, "Failed to open HID device");
        return false;
    }
    // The OPEN_EVENT callback will set connected_dev and connected_name.
    return true;
}

void ble_keyboard_disconnect(void) {
    if (connected_dev) {
        esp_hidh_dev_close(connected_dev);
        connected_dev = NULL;
        connected_name[0] = '\0';
        memset(prev_keys, 0, sizeof(prev_keys));
    }
}

bool ble_keyboard_is_connected(void) {
    return connected_dev != NULL;
}

const char *ble_keyboard_get_connected_name(void) {
    return connected_name;
}

static void ble_init_task(void *arg) {
    ble_keyboard_init();
    vTaskDelete(NULL);
}

void ble_keyboard_init_async(void) {
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
    xTaskCreatePinnedToCore(ble_init_task, "ble_init", 8192, NULL, 1, NULL, 1);
#else
    xTaskCreatePinnedToCore(ble_init_task, "ble_init", 8192, NULL, 1, NULL, 0);
#endif
    ESP_LOGI(TAG, "BLE init started in background task");
}

#else

// Stubs for boards without BLE.
bool ble_keyboard_init(void) { return false; }
void ble_keyboard_init_async(void) {}
bool ble_keyboard_is_available(void) { return false; }
int ble_keyboard_start_scan(int duration_seconds) { (void)duration_seconds; return 0; }
int ble_keyboard_get_scan_count(void) { return 0; }
const char *ble_keyboard_get_scan_name(int index) { (void)index; return ""; }
int ble_keyboard_get_scan_rssi(int index) { (void)index; return 0; }
const char *ble_keyboard_get_scan_addr(int index) { (void)index; return ""; }
bool ble_keyboard_connect(int scan_index) { (void)scan_index; return false; }
void ble_keyboard_disconnect(void) {}
bool ble_keyboard_is_connected(void) { return false; }
const char *ble_keyboard_get_connected_name(void) { return ""; }

#endif // BOARD_HAS_BLE_KEYBOARD

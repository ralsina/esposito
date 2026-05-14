#include "bbq20_keyboard.h"
#include "hardware_config.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "bbq20_kbd";

// BBQ20 I2C configuration
#define BBQ20_I2C_ADDR     0x1F
#define BBQ20_I2C_FREQ_HZ  100000

// BBQ20 special key codes (from terminado project)
#define BBQ20_KEY_ESCAPE     5   // Escape key
#define BBQ20_KEY_LEFT       6   // Left arrow
#define BBQ20_KEY_DOWN       17  // Down arrow
#define BBQ20_KEY_CTRL       18  // SYMBOL key (Ctrl modifier)
#define BBQ20_KEY_FN         7   // BACK key (Fn modifier)
#define BBQ20_KEY_ALT        19  // CALL key (Alt modifier)
#define BBQ20_KEY_FN2        20  // BlackBerry key (Fn2 modifier)

// Modifier key states (based on terminado implementation)
static bool fn_pressed = false;     // BACK key (Fn)
static bool fn2_pressed = false;    // BlackBerry key (Fn2)
static bool ctrl_pressed = false;   // SYMBOL key (Ctrl)
static bool alt_pressed = false;    // CALL key (Alt)

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t i2c_device = NULL;

// BBQ20 I2C Registers
#define BBQ20_REG_FW_VERSION   0x01
#define BBQ20_REG_STATUS      0x04
#define BBQ20_REG_FIFO        0x09

// BBQ10 key states from Arduino library
#define BBQ10_STATE_IDLE     0x00
#define BBQ10_STATE_PRESS    0x01
#define BBQ10_STATE_LONG_PRESS 0x02
#define BBQ10_STATE_RELEASE  0x03

// BBQ10 key state flags (from Arduino library)
#define BBQ10_STATE_MASK     0x0F  // Lower 4 bits are the state
#define BBQ10_CAPSLOCK       (1 << 5)  // 0x20
#define BBQ10_NUMLOCK        (1 << 6)  // 0x40

static bool bbq20_initialized = false;

// Update modifier key state based on key code and state
static void update_modifier_state(uint8_t key_code, uint8_t state) {
    uint8_t actual_state = state & BBQ10_STATE_MASK;
    bool pressed = (actual_state == BBQ10_STATE_PRESS || actual_state == BBQ10_STATE_LONG_PRESS);

    switch (key_code) {
        case BBQ20_KEY_FN:
            fn_pressed = pressed;
            ESP_LOGI(TAG, "🔧 Fn key %s", pressed ? "pressed" : "released");
            break;
        case BBQ20_KEY_CTRL:
            ctrl_pressed = pressed;
            ESP_LOGI(TAG, "🔧 Ctrl key %s", pressed ? "pressed" : "released");
            break;
        case BBQ20_KEY_ALT:
            alt_pressed = pressed;
            ESP_LOGI(TAG, "🔧 Alt key %s", pressed ? "pressed" : "released");
            break;
        case BBQ20_KEY_FN2:
            fn2_pressed = pressed;
            ESP_LOGI(TAG, "🔧 Fn2 key %s", pressed ? "pressed" : "released");
            break;
    }
}

// Get current modifier state as bit flags
uint8_t bbq20_get_modifiers(void) {
    uint8_t modifiers = 0;
    if (fn_pressed) modifiers |= 0x08;     // MODIFIER_FN
    if (ctrl_pressed) modifiers |= 0x02;   // MODIFIER_CTRL
    if (alt_pressed) modifiers |= 0x04;    // MODIFIER_ALT
    if (fn2_pressed) modifiers |= 0x10;    // MODIFIER_FN2
    return modifiers;
}

// Read from BBQ20 register using new I2C master driver
static esp_err_t bbq20_read_reg(uint8_t reg, uint8_t *data, size_t len) {
    if (!i2c_device) {
        ESP_LOGW(TAG, "I2C device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Write register address, then read data
    esp_err_t ret = i2c_master_transmit_receive(i2c_device, &reg, 1, data, len, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C read failed: reg=0x%02X, error=%s", reg, esp_err_to_name(ret));
    }
    return ret;
}

void bbq20_keyboard_deinit(void) {
    if (i2c_device) {
        i2c_master_bus_rm_device(i2c_device);
        i2c_device = NULL;
    }
    if (i2c_bus) {
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
    }
    fn_pressed = false;
    fn2_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    bbq20_initialized = false;
}

bool bbq20_keyboard_init(void) {
    // Already initialized, return success
    if (bbq20_initialized) {
        ESP_LOGI(TAG, "BBQ20 keyboard already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing BBQ20 keyboard via I2C (new driver)");
    ESP_LOGI(TAG, "I2C pins: SDA=GPIO%d, SCL=GPIO%d", I2C_SDA, I2C_SCL);

    // Configure I2C bus using new driver
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus creation failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "✅ I2C bus created successfully");

    // Configure I2C device for BBQ20 keyboard
    i2c_device_config_t i2c_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BBQ20_I2C_ADDR,
        .scl_speed_hz = BBQ20_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(i2c_bus, &i2c_dev_config, &i2c_device);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        // Clean up bus
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
        return false;
    }
    ESP_LOGI(TAG, "✅ I2C device added (address 0x%02X)", BBQ20_I2C_ADDR);

    // Test BBQ20 keyboard presence
    ESP_LOGI(TAG, "🔍 Scanning I2C bus for devices...");

    // Try common BBQ20 keyboard addresses
    uint8_t test_addresses[] = {0x1F, 0x3F, 0x1E, 0x2E};
    bool device_found = false;

    for (int i = 0; i < 4; i++) {
        uint8_t test_addr = test_addresses[i];

        // Create temporary device handle for scanning
        i2c_device_config_t test_dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = test_addr,
            .scl_speed_hz = BBQ20_I2C_FREQ_HZ,
        };

        i2c_master_dev_handle_t test_device;
        esp_err_t scan_ret = i2c_master_bus_add_device(i2c_bus, &test_dev_config, &test_device);

        if (scan_ret == ESP_OK) {
            // Try to read firmware version register
            uint8_t fw_version;
            uint8_t fw_reg = BBQ20_REG_FW_VERSION;
            esp_err_t read_ret = i2c_master_transmit_receive(test_device, &fw_reg, 1, &fw_version, 1, pdMS_TO_TICKS(100));

            if (read_ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ Found I2C device at 0x%02X! FW version: 0x%02X", test_addr, fw_version);

                if (test_addr == BBQ20_I2C_ADDR) {
                    i2c_master_bus_rm_device(test_device);
                    device_found = true;
                    bbq20_initialized = true;
                    break;
                }
            }

            // Clean up test device
            i2c_master_bus_rm_device(test_device);
        }
    }

    if (device_found) {
        return true;
    }

    ESP_LOGW(TAG, "⚠️  No BBQ20 keyboard found on I2C bus");
    ESP_LOGW(TAG, "⚠️  Checked addresses: 0x1F, 0x3F, 0x1E, 0x2E");
    ESP_LOGW(TAG, "⚠️  Check connections: SDA=GPIO%d, SCL=GPIO%d", I2C_SDA, I2C_SCL);
    bbq20_initialized = false;
    return false;
}

char bbq20_key_to_ascii(uint8_t key_code, uint8_t state) {
    // Extract actual state from lower 4 bits
    uint8_t actual_state = state & BBQ10_STATE_MASK;

    // Only process key press and long press events
    if (actual_state != BBQ10_STATE_PRESS && actual_state != BBQ10_STATE_LONG_PRESS) {
        return 0;
    }

    // Driver layer policy:
    // - never synthesize terminal semantics (Fn/Ctrl combos)
    // - pass through raw character bytes + separate modifier bitmask
    // - consumers (terminal_mode/apps) decide meaning of modifiers

    // Filter out modifier keys - they should not generate characters
    if (key_code == BBQ20_KEY_FN || key_code == BBQ20_KEY_CTRL ||
        key_code == BBQ20_KEY_ALT || key_code == BBQ20_KEY_FN2) {
        return 0;  // Don't generate ASCII for modifier keys
    }

    // Special keys (must be checked before printable range checks)
    if (key_code == 10) return '\n';  // Enter key (0x0A)
    if (key_code == BBQ20_KEY_ESCAPE) return 27;  // Escape key
    if (key_code == 8) return '\b';   // Backspace key (0x08)
    if (key_code == 40) return ' ';
    if (key_code == 41) return '\t';
    if (key_code == 42) return '\n';
    if (key_code == 43) return '\b';

    // BBQ20 keyboard sends ASCII directly (0x61 = 'a', 0x41 = 'A', etc.)
    // Check if it's a printable ASCII character
    if (key_code >= 32 && key_code <= 126) {
        return key_code;  // Return the ASCII character directly
    }

    // No other special keys recognized for BBQ20 (BBQ10 fallback disabled)
    return 0;
}

bool bbq20_read_key_event(bbq20_key_event_t *event) {
    if (!event) return false;

    // Try real BBQ20 keyboard
    if (bbq20_initialized) {
        // Read status register
        uint8_t status;
        esp_err_t ret = bbq20_read_reg(BBQ20_REG_STATUS, &status, 1);

        if (ret == ESP_OK) {
            uint8_t key_count = status & 0x1F; // Lower 5 bits

            if (key_count > 0) {
                ESP_LOGD(TAG, "🎹 BBQ20: %d keys in FIFO, status=0x%02X", key_count, status);

                // Read FIFO (16-bit values: key_code | state << 8)
                uint8_t fifo_data[2];
                ret = bbq20_read_reg(BBQ20_REG_FIFO, fifo_data, 2);

                if (ret == ESP_OK) {
                    // FIFO data is 16-bit: [key_code (high byte) | key_state (low byte)]
                    uint8_t key_code = fifo_data[1];  // High byte is key code
                    uint8_t key_state = fifo_data[0]; // Low byte is key state

                    ESP_LOGD(TAG, "🎹 BBQ20: Raw key_code=0x%02X, key_state=0x%02X", key_code, key_state);

                    // Update modifier key states first (before processing)
                    update_modifier_state(key_code, key_state);

                    // Extract actual state and modifiers
                    uint8_t actual_state = key_state & BBQ10_STATE_MASK;

                    event->raw_key_code = key_code;
                    event->key_code = key_code;
                    event->modifiers = bbq20_get_modifiers();  // Use current modifier state
                    event->pressed = (actual_state == BBQ10_STATE_PRESS || actual_state == BBQ10_STATE_LONG_PRESS);

                    char ascii = bbq20_key_to_ascii(key_code, key_state);
                    if (ascii) {
                        // For control characters (0x00-0x1F), show them as '^X' format in logs
                        if (ascii < 0x20) {
                            ESP_LOGD(TAG, "🎹 BBQ20 REAL key: ^%c (code:0x%02X state:0x%02X mods:0x%02X)",
                                    ascii + 0x40, key_code, key_state, event->modifiers);
                        } else {
                            ESP_LOGD(TAG, "🎹 BBQ20 REAL key: %c (code:0x%02X state:0x%02X mods:0x%02X)",
                                    ascii, key_code, key_state, event->modifiers);
                        }
                        event->key_code = (uint8_t)ascii;
                        return true;
                    } else {
                        ESP_LOGD(TAG, "🎹 BBQ20: Key mapped to NULL ASCII");
                    }
                } else {
                    ESP_LOGW(TAG, "🎹 BBQ20: FIFO read failed: %s", esp_err_to_name(ret));
                }
            }
        } else {
            ESP_LOGW(TAG, "🎹 BBQ20: Status read failed: %s", esp_err_to_name(ret));
        }
    }

    // No keys available
    return false;
}
#include "touchscreen.h"
#include "hardware_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "touchscreen";

// Calibration factors (these need to be adjusted for your specific screen)
// These are starting values - you may need to tweak them
static float xfac = 1.0f;
static float yfac = 1.0f;
static int xoff = 0;
static int yoff = 0;

// Touchscreen configuration - uses GPIO bit-banging like witnessmenow
#define TOUCH_MOSI        32
#define TOUCH_MISO        39
#define TOUCH_CLK         25
#define TOUCH_CS          33
#define TOUCH_IRQ         36

// XPT2046 commands
#define XPT2046_CMD_X     0x90
#define XPT2046_CMD_Y     0xD0

static bool touchscreen_initialized = false;

// GPIO bit-banging SPI (from witnessmenow XPT2046 driver)
static void xpt2046_gpio_write_byte(uint8_t num) {
    for (int count = 0; count < 8; count++) {
        gpio_set_level(TOUCH_MOSI, (num & 0x80) ? 1 : 0);
        num <<= 1;
        gpio_set_level(TOUCH_CLK, 0);
        gpio_set_level(TOUCH_CLK, 1);
    }
}

static uint16_t xpt2046_gpio_spi_read(uint8_t reg) {
    gpio_set_level(TOUCH_CLK, 0);
    gpio_set_level(TOUCH_MOSI, 0);
    gpio_set_level(TOUCH_CS, 0);  // Select touchscreen

    xpt2046_gpio_write_byte(reg);  // Send command

    esp_rom_delay_us(6);  // Conversion time (from witnessmenow)

    gpio_set_level(TOUCH_CLK, 0);
    esp_rom_delay_us(1);
    gpio_set_level(TOUCH_CLK, 1);  // Clear BUSY
    gpio_set_level(TOUCH_CLK, 0);

    uint16_t ad_value = 0;
    for (int count = 0; count < 16; count++) {  // Read 16 bits, only high 12 valid
        ad_value <<= 1;
        gpio_set_level(TOUCH_CLK, 0);
        gpio_set_level(TOUCH_CLK, 1);
        if (gpio_get_level(TOUCH_MISO)) {
            ad_value++;
        }
    }

    ad_value >>= 4;  // Only high 12 bits valid
    gpio_set_level(TOUCH_CS, 1);  // Release CS

    return ad_value;
}

// Read multiple samples and average (like witnessmenow does)
static uint16_t xpt2046_read_avg(uint8_t cmd) {
    #define READ_COUNT 30
    #define LOST_VAL 1

    uint16_t buf[READ_COUNT];

    // Read multiple samples
    for (int i = 0; i < READ_COUNT; i++) {
        buf[i] = xpt2046_gpio_spi_read(cmd);
    }

    // Sort the array
    for (int i = 0; i < READ_COUNT - 1; i++) {
        for (int j = i + 1; j < READ_COUNT; j++) {
            if (buf[i] > buf[j]) {
                uint16_t temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }

    // Average the middle values (discard highest and lowest)
    uint32_t sum = 0;
    for (int i = LOST_VAL; i < READ_COUNT - LOST_VAL; i++) {
        sum += buf[i];
    }

    return sum / (READ_COUNT - 2 * LOST_VAL);
}

bool touchscreen_init(void) {
    ESP_LOGI(TAG, "Initializing XPT2046 touchscreen with GPIO bit-banging");

    // Configure IRQ pin as input (GPIO 36 is input-only, no pullup)
    gpio_config_t irq_conf = {
        .pin_bit_mask = (1ULL << TOUCH_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&irq_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure IRQ pin: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure MISO as input
    gpio_config_t miso_conf = {
        .pin_bit_mask = (1ULL << TOUCH_MISO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&miso_conf);

    // Configure MOSI, CLK, CS as outputs
    gpio_set_direction(TOUCH_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(TOUCH_CLK, GPIO_MODE_OUTPUT);
    gpio_set_direction(TOUCH_CS, GPIO_MODE_OUTPUT);

    // Set initial levels
    gpio_set_level(TOUCH_CS, 1);  // Deselect
    gpio_set_level(TOUCH_CLK, 0);
    gpio_set_level(TOUCH_MOSI, 0);

    // Check initial IRQ state
    int irq_level = gpio_get_level(TOUCH_IRQ);
    ESP_LOGI(TAG, "Touchscreen IRQ level: %d (1=not touched, 0=touched)", irq_level);

    touchscreen_initialized = true;
    ESP_LOGI(TAG, "✅ XPT2046 touchscreen driver ready (GPIO bit-bang)");

    // Test read to verify communication
    uint16_t test_x = xpt2046_read_avg(XPT2046_CMD_X);
    uint16_t test_y = xpt2046_read_avg(XPT2046_CMD_Y);
    ESP_LOGI(TAG, "Test readings - X: %d, Y: %d", test_x, test_y);

    return true;
}

bool touchscreen_is_available(void) {
    return touchscreen_initialized;
}

bool touchscreen_get_position(uint16_t *x, uint16_t *y, bool *pressed) {
    static int poll_count = 0;

    if (!touchscreen_initialized) {
        ESP_LOGW(TAG, "Touchscreen not initialized!");
        return false;
    }

    // Remove unused variable warning
    (void)poll_count;

    // Check if screen is being touched (IRQ is active low: 0=touched, 1=not touched)
    int irq_level = gpio_get_level(TOUCH_IRQ);
    *pressed = (irq_level == 0);

    if (!*pressed) {
        return false;  // No touch detected by IRQ
    }

    // Read X and Y coordinates (using averaging method from witnessmenow)
    uint16_t raw_x = xpt2046_read_avg(XPT2046_CMD_X);
    uint16_t raw_y = xpt2046_read_avg(XPT2046_CMD_Y);

    ESP_LOGI(TAG, "Touch: raw_x=%d, raw_y=%d", raw_x, raw_y);

    // Filter out invalid readings (but allow 0 for Y near top)
    if (raw_x < 200 || raw_x > 3900 || raw_y > 3900) {
        return false;
    }

    // Map raw values to screen coordinates
    // XPT2046 on CYD2USB: X: 200-3900 -> 0-320, Y: 200-3900 -> 0-240
    *x = (raw_x - 200) * 320 / 3700;
    *y = (raw_y - 200) * 240 / 3700;

    // Clamp to screen bounds
    if (*x > 320) *x = 320;
    if (*y > 240) *y = 240;

    ESP_LOGI(TAG, "Touch: raw_x=%d, raw_y=%d, screen_x=%d, screen_y=%d",
             raw_x, raw_y, *x, *y);

    return true;
}
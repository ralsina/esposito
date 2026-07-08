#include "battery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "adc_cali_schemes.h"

static const char *TAG = "battery";

// GPIO5 on ESP32-S3 is ADC1_CH4
#define BATTERY_ADC_PIN       5
#define BATTERY_ADC_UNIT      ADC_UNIT_1
#define BATTERY_ADC_CHANNEL   ADC_CHANNEL_4
#define BATTERY_ADC_ATTEN     ADC_ATTEN_DB_12
#define BATTERY_ADC_BITWIDTH  ADC_BITWIDTH_DEFAULT

// Voltage divider: battery -- 33K -- GPIO5 -- 100K -- GND.
// Ratio = (R_top + R_bottom) / R_bottom = (33 + 100) / 100 = 1.33.
#define BATTERY_DIVIDER_RATIO 1.33f

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static bool battery_initialized = false;

bool battery_init(void) {
    if (battery_initialized) return true;

    gpio_reset_pin((gpio_num_t)BATTERY_ADC_PIN);
    ESP_LOGI(TAG, "GPIO%d reset for ADC mode", BATTERY_ADC_PIN);

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(ret));
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return false;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle);
#else
    cali_handle = NULL;
    ret = ESP_ERR_NOT_SUPPORTED;
#endif
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw values: %s", esp_err_to_name(ret));
        cali_handle = NULL;
    }

    battery_initialized = true;
    ESP_LOGI(TAG, "Battery ADC ready (GPIO%d)", BATTERY_ADC_PIN);
    return true;
}

bool battery_is_available(void) {
    return battery_initialized;
}

int battery_read_millivolts(void) {
    if (!battery_initialized) return -1;

    int64_t sum_raw = 0;
    int samples = 0;
    for (int i = 0; i < 32; i++) {
        int raw;
        esp_err_t ret = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw);
        if (ret == ESP_OK) {
            sum_raw += raw;
            samples++;
        }
        esp_rom_delay_us(200);
    }

    if (samples == 0) {
        ESP_LOGE(TAG, "All ADC read attempts failed");
        return -1;
    }

    int avg_raw = (int)(sum_raw / samples);

    int adc_mv;
    esp_err_t ret = adc_cali_raw_to_voltage(cali_handle, avg_raw, &adc_mv);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Calibration failed: %s", esp_err_to_name(ret));
        adc_mv = avg_raw;
    }

    int battery_mv = (int)(adc_mv * BATTERY_DIVIDER_RATIO);
    return battery_mv;
}

void battery_deinit(void) {
    if (cali_handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(cali_handle);
#endif
        cali_handle = NULL;
    }
    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }
    battery_initialized = false;
    ESP_LOGI(TAG, "Battery ADC deinitialized");
}

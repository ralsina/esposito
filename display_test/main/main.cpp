// Self-contained LovyanGFX panel validation -- baseline red squares.

#include <LovyanGFX.hpp>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "guition_bus.hpp"

static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_RED   = 0xF800;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_BLUE  = 0x001F;

class TestGFX : public lgfx::LGFX_Device
{
public:
    guition::GuitionQSPIBus       _bus;
    guition::Panel_NV3041A_Guition panel;
    lgfx::Light_PWM               light;

    TestGFX(void)
    {
        panel.setBus(&_bus);
        auto cfg = panel.config();
        cfg.pin_cs   = -1;
        cfg.pin_rst  = -1;
        cfg.pin_busy = -1;
        cfg.memory_width  = 480;
        cfg.memory_height = 272;
        cfg.panel_width   = 480;
        cfg.panel_height  = 272;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        cfg.offset_rotation = 0;
        cfg.readable   = false;
        cfg.invert     = true;
        cfg.rgb_order  = true;
        cfg.dlen_16bit = false;
        cfg.bus_shared = false;
        panel.config(cfg);

        auto lc = light.config();
        lc.pin_bl = 1;
        lc.invert = false;
        lc.freq = 44100;
        lc.pwm_channel = 7;
        light.config(lc);
        panel.setLight(&light);
        setPanel(&panel);
    }
};

TestGFX tft;

extern "C" void app_main(void)
{
    ESP_LOGI("test", "init");
    tft.init();
    tft.setBrightness(255);

    ESP_LOGI("test", "fill white");
    tft.fillScreen(C_WHITE);

    const int sq = 56, y0 = 36;

    ESP_LOGI("test", "drawPixel square (red) -- using 1x1 fillRect");
    for (int y = y0; y < y0 + sq; y++)
        for (int x = 10; x < 10 + sq; x++)
            tft.fillRect(x, y, 1, 1, C_RED);

    ESP_LOGI("test", "fillRect square (red)");
    tft.fillRect(170, y0, sq, sq, C_RED);

    ESP_LOGI("test", "size strip (red)");
    int sx = 250;
    for (int s = 1; s <= 16; s <<= 1) {
        tft.fillRect(sx, y0, s, s, C_RED);
        sx += s + 6;
    }

    ESP_LOGI("test", "single red dot");
    tft.drawPixel(430, y0, C_RED);

    // Anti-aliased text on coloured backgrounds — the failing scenario.
    // Left: black text on green (transparent mode). Right: white text on blue.
    tft.fillRect(10, 110, 200, 50, C_GREEN);
    tft.fillRect(250, 110, 200, 50, C_BLUE);

    tft.setTextColor(0x0000);  // black, no bg = transparent
    tft.setCursor(20, 120);
    tft.setTextSize(2);
    tft.print("Black on green");

    tft.setTextColor(C_WHITE);  // white, no bg = transparent
    tft.setCursor(260, 120);
    tft.setTextSize(2);
    tft.print("White on blue");

    // Also test: black text with explicit green bg (non-transparent)
    tft.fillRect(10, 180, 200, 50, 0x0000);  // black rect
    tft.setTextColor(0x0000, C_GREEN);  // black fg, green bg
    tft.setCursor(20, 190);
    tft.setTextSize(2);
    tft.print("fg/bg mode");

    ESP_LOGI("test", "done");
    while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}

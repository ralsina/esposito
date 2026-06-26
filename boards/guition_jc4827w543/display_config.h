#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include "board.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "guition_bus.hpp"
#include <lgfx/v1/touch/Touch_GT911.hpp>

#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xC618
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFD20
#define TFT_GREENYELLOW 0xAFE5
#define TFT_PINK        0xF81F

// Guition JC4827W543: 4.3" 480x272 TFT, NV3041A over QSPI.
// Uses a self-contained LovyanGFX panel (Panel_NV3041A_Guition) that bypasses
// LovyanGFX's Bus_SPI/init orchestration and drives the panel directly via the
// ESP-IDF spi_device driver (Arduino_GFX framing). LovyanGFX is kept for
// high-level features (fonts, sprites, JPEG).
class LGFX : public lgfx::LGFX_Device
{
public:
    guition::GuitionQSPIBus          _bus_instance;
    guition::Panel_NV3041A_Guition _panel_instance;
    lgfx::Light_PWM               _light_instance;
    lgfx::Touch_GT911             _touch_instance;

    LGFX(void)
    {
        _panel_instance.setBus(&_bus_instance);

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = -1;        // the panel owns CS (toggles per transaction)
            cfg.pin_rst  = -1;        // RST not wired
            cfg.pin_busy = -1;
            cfg.memory_width  = BOARD_SCREEN_WIDTH;
            cfg.memory_height = BOARD_SCREEN_HEIGHT;
            cfg.panel_width   = BOARD_SCREEN_WIDTH;
            cfg.panel_height  = BOARD_SCREEN_HEIGHT;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            cfg.offset_rotation = 0;
            cfg.readable   = false;
            cfg.invert     = true;   // INVON is sent in the panel init
            cfg.rgb_order  = true;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = BOARD_LCD_BL;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {   // GT911 capacitive touch over I2C (independent of the display bus).
            auto cfg = _touch_instance.config();
            cfg.x_min = 0;
            cfg.x_max = BOARD_SCREEN_WIDTH - 1;
            cfg.y_min = 0;
            cfg.y_max = BOARD_SCREEN_HEIGHT - 1;
            cfg.offset_rotation = BOARD_TOUCH_OFFSET_ROTATION;
            cfg.bus_shared = false;
            cfg.i2c_port = BOARD_TOUCH_I2C_PORT;
            cfg.pin_sda  = BOARD_TOUCH_I2C_SDA;
            cfg.pin_scl  = BOARD_TOUCH_I2C_SCL;
            cfg.pin_int  = BOARD_TOUCH_INT;
            cfg.pin_rst  = BOARD_TOUCH_RST;
            cfg.freq     = BOARD_TOUCH_I2C_FREQ;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};

#endif // DISPLAY_CONFIG_H

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// Hardware Configuration for Cheap Yellow Display (2USB version)
// Based on terminado project

// Display Configuration
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define DISPLAY_TYPE_ST7789  // ST7789 for 2-port CYD version

// SPI Display Pins
#define PIN_LCD_SCLK 14
#define PIN_LCD_MOSI 13
#define PIN_LCD_MISO 12
#define PIN_LCD_DC 2
#define PIN_LCD_CS 15
#define PIN_LCD_BL 21

// I2C Configuration for BBQ20 Keyboard
#define I2C_SDA 22
#define I2C_SCL 27
#define I2C_PORT I2C_NUM_0
#define I2C_FREQ 100000  // 100kHz for BBQ20 compatibility

// Touch Configuration (if available)
#define TOUCH_I2C_ADDR 0x14
#define TOUCH_I2C_PORT I2C_NUM_1

// Display Colors
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_YELLOW 0xFFE0

#endif // HARDWARE_CONFIG_H

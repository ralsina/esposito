#ifndef BOARD_GUITION_JC4827W543_H
#define BOARD_GUITION_JC4827W543_H

#define BOARD_NAME "Guition JC4827W543"

// Display: 4.3" TFT LCD, 480x272, NV3041A driver over QSPI.
#define BOARD_SCREEN_WIDTH 480
#define BOARD_SCREEN_HEIGHT 272
#define BOARD_DEFAULT_DISPLAY_ROTATION 0
#define BOARD_DISPLAY_TYPE_NV3041A

// Display QSPI bus pins (NV3041A max write clock is 32 MHz).
#define BOARD_LCD_SPI_HOST SPI3_HOST
#define BOARD_LCD_SPI_MODE 1
#define BOARD_LCD_FREQ_WRITE 32000000U
#define BOARD_LCD_FREQ_READ 16000000U
#define BOARD_LCD_SCLK 47
#define BOARD_LCD_QSPI_D0 21
#define BOARD_LCD_QSPI_D1 48
#define BOARD_LCD_QSPI_D2 40
#define BOARD_LCD_QSPI_D3 39
#define BOARD_LCD_CS 45
#define BOARD_LCD_BL 1
// Panel_NV3041A's LovyanGFX init ignores the `invert` config, so display_init()
// forces inversion on after begin() (matches the Arduino_GFX ips=true setting
// that produces correct colors on this panel).
#define BOARD_LCD_INVERT 1

// Touchscreen: GT911 capacitive touch over I2C (driven via LovyanGFX).
#define BOARD_HAS_TOUCHSCREEN 1
#define BOARD_TOUCH_GT911 1
#define BOARD_TOUCH_I2C_PORT I2C_NUM_0
#define BOARD_TOUCH_I2C_SDA 8
#define BOARD_TOUCH_I2C_SCL 4
#define BOARD_TOUCH_INT 3
#define BOARD_TOUCH_RST 38
#define BOARD_TOUCH_I2C_FREQ 400000
// Orientation correction (LovyanGFX touch offset_rotation, 0-7). Default 0
// matches the panel's native landscape orientation; if touches come through
// mirrored or axis-swapped, try other values (the upstream board example used 6).
#define BOARD_TOUCH_OFFSET_ROTATION 0

// BBQ20 keyboard on I2C_NUM_1 (separate from touch I2C on GPIO8/4).
// Connector: IO17(SDA)/IO18(SCL)/GND/3.3V (JST 1.25mm 4-pin)
#define BOARD_HAS_BBQ20_KEYBOARD 1
#define BOARD_HAS_KEYBOARD_BACKLIGHT 1
#define BOARD_BBQ20_SDA 17
#define BOARD_BBQ20_SCL 18
#define BOARD_BBQ20_PORT I2C_NUM_1
#define BOARD_BBQ20_FREQ 100000

// SD Card: microSD slot on SPI (pins per the JC4827W543 "Dev Device Pins").
// Routed to SPI2_HOST so it does not share the display's QSPI bus (SPI3_HOST).
#define BOARD_HAS_SD_CARD 1
#define BOARD_SD_SPI_HOST SPI2_HOST
#define BOARD_SD_MISO_PIN 13
#define BOARD_SD_MOSI_PIN 11
#define BOARD_SD_CLK_PIN 12
#define BOARD_SD_CS_PIN 10

// Boot button (GPIO0 on ESP32-S3 dev modules).
#define BOARD_BOOT_BUTTON_GPIO GPIO_NUM_0

// BLE HID Host: ESP32-S3 supports BLE — use for wireless keyboards.
#define BOARD_HAS_BLE_KEYBOARD 1

// Audio: built-in I2S DAC + amplifier, SH 1.0mm 2-pin speaker connector.
// SPH0645 digital microphone on DIN (GPIO 15, assignable).
#define BOARD_HAS_AUDIO 1
#define BOARD_I2S_NUM     0
#define BOARD_I2S_BCLK    42
#define BOARD_I2S_LRCK    2
#define BOARD_I2S_DOUT    41
#define BOARD_I2S_DIN     15

#endif // BOARD_GUITION_JC4827W543_H

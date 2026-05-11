# CYD2USB Hardware Documentation

Based on analysis of https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

## Board Overview
- **Board**: ESP32 Cheap Yellow Display (CYD) with 2 USB ports
- **Microcontroller**: ESP32-WROOM-32
- **Display**: ST7789 320x240 TFT LCD
- **Touch**: Resistive touchscreen (XPT2046 controller)
- **Keyboard**: BBQ20/KBD keyboard via I2C
- **Storage**: SD card slot (microSD)
- **Audio**: PWM buzzer
- **LED**: RGB LED

## Pin Configuration

### Display (ST7789)
- **MISO**: GPIO 12
- **MOSI**: GPIO 13
- **SCLK**: GPIO 14
- **CS**: GPIO 15
- **DC**: GPIO 2
- **RST**: -1 (connected to ESP32 reset)
- **BL**: GPIO 21 (backlight)

### Keyboard (BBQ20/KBD)
- **I2C SDA**: GPIO 22
- **I2C SCL**: GPIO 27
- **I2C Address**: 0x1F
- **I2C Frequency**: 100kHz

### SD Card
- **Interface**: SPI (shares VSPI bus with display)
- **MISO**: GPIO 12 (shared with display)
- **MOSI**: GPIO 13 (shared with display)
- **SCLK**: GPIO 14 (shared with display)
- **CS**: GPIO 5 (dedicated SD card CS pin)

**Important**: The SD card shares the VSPI (SPI3) bus with the display. This requires careful SPI bus management.

### Touchscreen (XPT2046)
- **Interface**: SPI (separate bus or shared)
- **Note**: Touchscreen implementation is complex and may not work on all CYD2USB boards

### Other Hardware
- **Buzzer**: PWM controlled
- **RGB LED**: PWM controlled (3 channels)
- **Buttons**: GPIO buttons on board

## Special Keys on BBQ20 Keyboard
The BBQ20 keyboard has 5 special modifier keys at the top:
- **BACK** → Functions as Fn modifier
- **SYMBOL** → Functions as Ctrl modifier
- **CALL** → Functions as Alt modifier
- **BlackBerry** → Functions as Fn2 modifier

### Keyboard Key Codes
- **ESC**: ASCII 5
- **Arrow keys**: Special codes (left=6, down=17, etc.)
- **Function keys**: Triggered by Fn + letter combinations

## Display Configuration
- **Driver**: ST7789
- **Resolution**: 320x240
- **SPI Frequency**: High speed (up to 80MHz for writes, 16MHz for reads)
- **Color Format**: RGB565
- **Rotation**: 1 (landscape mode for CYD2USB)

## SPI Bus Usage
The ESP32 has multiple SPI buses:
- **VSPI (SPI3)**: Used by display and SD card (shared bus)
- **HSPI (SPI2)**: Available for other uses

**Critical**: Display and SD card both use VSPI, requiring proper bus arbitration.

## Power Requirements
- **Input Voltage**: 5V via USB
- **Current**: ~200-400mA depending on usage

## Development Notes

### For ESP-IDF Development (v6.0+)
- Use LovyanGFX library for display
- Arduino SD library examples don't directly translate to ESP-IDF
- SPI bus sharing requires manual transaction management
- CS pin arbitration between display and SD card

### Key Differences from Arduino
- Arduino handles SPI bus sharing automatically
- ESP-IDF requires manual SPI transaction management
- API differences between Arduino SD library and ESP-IDF vfs_fat

### Successful Implementations
- **Display**: Working with LovyanGFX in ESP-IDF v6.0
- **Keyboard**: Working with BBQ20 driver in ESP-IDF v6.0
- **SD Card**: Working in Arduino, requires SPI bus sharing solution in ESP-IDF

## Known Issues
1. **Touchscreen**: XPT2046 driver may not work reliably on all CYD2USB boards
2. **SD Card**: Requires advanced SPI bus management in ESP-IDF due to shared VSPI bus
3. **SPI Conflicts**: Display and SD card cannot operate simultaneously without proper arbitration

## References
- Original repository: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
- Display config: DisplayConfig/CYD2USB/User_Setup.h
- SD card example: Examples/Basics/3-SDCardTest/3-SDCardTest.ino
- Arduino vs ESP-IDF API differences are significant
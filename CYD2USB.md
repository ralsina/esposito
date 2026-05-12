# CYD2USB Hardware Documentation

ESP32 Cheap Yellow Display (CYD) with 2 USB ports - Complete hardware integration guide for Esposito OS.

## Board Overview
- **Board**: ESP32-2432S028R (ESP32 CYD2USB)
- **Microcontroller**: ESP32-WROOM-32
- **Display**: ST7789 320x240 TFT LCD  
- **Touch**: Resistive touchscreen (XPT2046 controller)
- **Keyboard**: BBQ20/KBD keyboard via I2C
- **Storage**: SD card slot (microSD)
- **Audio**: PWM buzzer
- **LED**: RGB LED (NeoPixel)

## Component-by-Component Wiring and Implementation

### 🖥️ Display (ST7789 320x240 TFT LCD)

**Physical Wiring:**
- **MOSI**: GPIO 13
- **MISO**: GPIO 12  
- **SCLK**: GPIO 14
- **CS**: GPIO 15
- **DC**: GPIO 2
- **RST**: -1 (connected to ESP32 reset)
- **BL**: GPIO 21 (backlight control)

**Implementation:**
- **Library**: LovyanGFX (external managed component)
- **Config**: `main/hardware_config.h` - Display pin definitions
- **SPI Bus**: SPI2_HOST (HSPI) - dedicated bus for display
- **Driver**: `main/hardware.cpp` - `LGFX tft` instance
- **Frequency**: 40MHz SPI clock
- **Rotation**: 1 (landscape mode: 240x320)

**Access Pattern:**
```cpp
// Initialize display
tft.begin();
tft.setRotation(1);  // Landscape

// Clear screen
tft.fillScreen(color);

// Draw text
tft.setCursor(x, y);
tft.setTextColor(color);
tft.print("text");
```

**Status**: ✅ Working perfectly with LovyanGFX

---

### ⌨️ Keyboard (BBQ20/KBD I2C Keyboard)

**Physical Wiring:**
- **I2C SDA**: GPIO 22
- **I2C SCL**: GPIO 27
- **I2C Address**: 0x1F
- **Interrupt**: GPIO 36 (shared with touchscreen IRQ - see note)

**Implementation:**
- **Library**: Custom driver (`main/bbq20_keyboard.c/h`)
- **Config**: `main/hardware_config.h` - I2C pin definitions
- **I2C Bus**: I2C_NUM_0 (ESP32 primary I2C)
- **Frequency**: 100kHz (BBQ20 requirement)
- **Special Keys**: 5 modifier keys (BACK=Fn, SYMBOL=Ctrl, CALL=Alt, BlackBerry=Fn2)

**Access Pattern:**
```cpp
// Initialize keyboard
bbq20_keyboard_init();

// Read keyboard events
event_t event;
if (keyboard_read_event(&event)) {
    char key = event.keyboard.key;
    bool pressed = event.keyboard.pressed;
    uint8_t modifiers = event.keyboard.modifiers;
}
```

**Status**: ✅ Working perfectly with custom driver

**Important Note**: GPIO 36 is used by both keyboard interrupt and touchscreen IRQ. Keyboard polling is used instead of interrupts to avoid conflicts.

---

### 💾 SD Card (microSD Slot)

**Physical Wiring:**
- **Interface**: SDSPI (SPI mode SD card)
- **MOSI**: GPIO 23 (VSPI/HSPI standard pin)
- **MISO**: GPIO 19 (VSPI/HSPI standard pin)  
- **SCLK**: GPIO 18 (VSPI/HSPI standard pin)
- **CS**: GPIO 5 (SD card dedicated CS pin)

**Implementation:**
- **Library**: ESP-IDF built-in (`esp_vfs_fat`, `sdspi_host`)
- **Config**: `main/hardware_config.h` - SD card pin definitions
- **SPI Bus**: SPI3_HOST (VSPI) - dedicated bus for SD card
- **Driver**: `main/sd_card.c/h` - ESP-IDF SDSPI implementation
- **Mount Point**: `/sdcard`
- **Filesystem**: FAT32 (pre-formatted on SD card)

**Access Pattern:**
```cpp
// Initialize SD card
sd_card_init();

// File operations
FILE *f = fopen("/sdcard/test.txt", "w");
fprintf(f, "Hello SD card!\n");
fclose(f);

// List files
DIR *dir = opendir("/sdcard");
struct dirent *entry;
while ((entry = readdir(dir)) != NULL) {
    printf("File: %s\n", entry->d_name);
}
closedir(dir);
```

**Status**: ✅ Working with ESP-IDF SDSPI driver

**Key Difference**: Unlike Arduino examples that share SPI with display, this implementation uses dedicated SPI3 bus to avoid conflicts.

---

### 👆 Touchscreen (XPT2046 Resistive)

**Physical Wiring:**
- **Interface**: GPIO bit-banging SPI (NOT hardware SPI)
- **MOSI**: GPIO 32
- **MISO**: GPIO 39
- **SCLK**: GPIO 25  
- **CS**: GPIO 33
- **IRQ**: GPIO 36 (active low, 0=touched, 1=not touched)

**Implementation:**
- **Library**: Custom driver (`main/touchscreen.c/h`)
- **Config**: `main/hardware_config.h` - Touchscreen pin definitions
- **Method**: GPIO bit-banging SPI (from witnessmenow repo)
- **Algorithm**: 30-sample averaging with outlier rejection
- **Coordinates**: Raw ADC values mapped to 320x240 screen

**Why GPIO Bit-Banging?**
Hardware SPI was unreliable (returned 0xFFF consistently). GPIO bit-banging provides precise timing control that the XPT2046 requires.

**Access Pattern:**
```cpp
// Initialize touchscreen
touchscreen_init();

// Read touch position in event loop
uint16_t x, y;
bool pressed;
if (touchscreen_get_position(&x, &y, &pressed)) {
    printf("Touch: x=%d, y=%d, pressed=%d\n", x, y, pressed);
}
```

**Status**: ✅ Working with GPIO bit-banging method

**Calibration**: Current calibration works well. For fine-tuning, the witnessmenow repo includes a calibration routine that calculates `xfac`, `yfac`, `xoff`, `yoff` factors.

---

## SPI Bus Architecture

The ESP32 CYD2USB uses **separate SPI buses** for each component to avoid conflicts:

```
SPI2_HOST (HSPI): Display (MOSI=13, MISO=12, CLK=14, CS=15)
     ↓
 LovyanGFX Library

SPI3_HOST (VSPI): SD Card (MOSI=23, MISO=19, CLK=18, CS=5)
     ↓
ESP-IDF SDSPI Driver

GPIO Bit-Banging: Touchscreen (MOSI=32, MISO=39, CLK=25, CS=33)
     ↓
Custom XPT2046 Driver
```

This separation eliminates the complex SPI arbitration that Arduino examples struggle with.

---

## I2C Bus Architecture

```
I2C_NUM_0: Keyboard (SDA=22, SCL=27, Addr=0x1F)
          ↓
     BBQ20 Keyboard Driver
```

Simple single-device I2C bus with no conflicts.

---

## Pin Summary Table

| GPIO | Component | Direction | Purpose |
|------|-----------|-----------|---------|
| 2 | Display | Output | DC (Data/Command) |
| 5 | SD Card | Output | CS (Chip Select) |
| 12 | Display | Input | MISO |
| 13 | Display | Output | MOSI |
| 14 | Display | Output | SCLK |
| 15 | Display | Output | CS |
| 18 | SD Card | Output | SCLK |
| 19 | SD Card | Input | MISO |
| 21 | Display | Output | Backlight |
| 22 | Keyboard | I2C | SDA |
| 23 | SD Card | Output | MOSI |
| 25 | Touchscreen | Output | SCLK (GPIO) |
| 27 | Keyboard | I2C | SCL |
| 32 | Touchscreen | Output | MOSI (GPIO) |
| 33 | Touchscreen | Output | CS |
| 36 | Touchscreen/Keyboard | Input | IRQ (shared) |
| 39 | Touchscreen | Input | MISO (GPIO) |

---

## Configuration Files

**`main/hardware_config.h`**: Contains all hardware pin definitions and constants
- Display pins and LCD configuration
- I2C pins for keyboard  
- SD card SPI pins
- Touchscreen GPIO pins
- Color definitions
- Touch configuration ranges

---

## Status Summary

| Component | Status | Method | Notes |
|-----------|--------|---------|-------|
| Display | ✅ Working | LovyanGFX + SPI2 | Hardware SPI, 40MHz |
| Keyboard | ✅ Working | Custom driver + I2C | Polling mode to avoid IRQ conflicts |
| SD Card | ✅ Working | ESP-IDF SDSPI + SPI3 | Dedicated SPI3 bus |
| Touchscreen | ✅ Working | GPIO bit-banging | 30-sample averaging, precise timing |

---

## Development Notes

**Key Learnings:**
1. **Separate SPI buses** eliminate arbitration complexity
2. **GPIO bit-banging** was essential for reliable touchscreen operation
3. **IRQ sharing** (GPIO 36) required keyboard polling instead of interrupts
4. **LovyanGFX** provides excellent display performance with minimal code
5. **witnessmenow repo** was invaluable for touchscreen implementation reference

**Power Consumption:**
- **Normal operation**: ~200-400mA
- **Input**: 5V via USB (micro USB or USB-C)

**For Development:**
- ESP-IDF v6.0.1
- LovyanGFX managed component
- Custom drivers for keyboard and touchscreen
- ESP-IDF built-in SD card support

---

## References
- https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
- Display config: `/tmp/esp32-cyd/DisplayConfig/CYD2USB/User_Setup.h`
- XPT2046 driver: `/tmp/esp32-cyd/Examples/ESP-IDF/LCD_Touch/components/XPT2046/xpt2046.c`
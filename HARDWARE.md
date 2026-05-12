# Hardware Integration Status

## ✅ Working

### Display Driver
- LovyanGFX on ESP-IDF via SPI @ 80MHz
- ST7789 panel with 320x240 resolution (landscape)
- Font rendering using spleen-5x8
- Text mode subsystem (64x30 grid, 16 colors, attributes)

### Keyboard Driver (BBQ20)
- I2C driver using ESP-IDF new I2C master API
- Keyboard detection and scanning at 0x1F
- Full key mapping with modifier keys (Fn, Ctrl, Alt, Fn2)
- Arrow key combinations via Fn+WASD
- FIFO-based key event reading

### Touch Driver (XPT2046)
- GPIO bit-banging SPI on separate pins (GPIO 32/39/25/33)
- IRQ-based press detection (active low on GPIO 36)
- Multi-sample averaging (30 samples, discard outliers)
- Raw-to-screen coordinate mapping (200-3900 → 0-320/0-240)
- Works alongside BBQ20 keyboard (no I2C conflict)

### SD Card
- SDSPI mode on SPI3/VSPI bus (separate from display)
- FAT32 filesystem via ESP-VFS
- Hot-plug detection and mount at `/sdcard`
- File read/write/list operations

## 📋 Hardware Configuration

### Display: ST7789
```
Resolution: 320x240 (rotated to 240x320)
Interface: SPI @ 80MHz
Pins:
  - SCLK: GPIO 14
  - MOSI: GPIO 13
  - MISO: GPIO 12
  - DC:   GPIO 2
  - CS:   GPIO 15
  - BL:   GPIO 21
```

### Keyboard: BBQ20
```
Interface: I2C @ 100kHz
Pins:
  - SDA: GPIO 22
  - SCL: GPIO 27
Address: 0x1F
FIFO Registers: 0x01 (FW), 0x04 (Status), 0x09 (Key Data)
```

### Touch: XPT2046
```
Interface: GPIO bit-banging SPI
Pins:
  - MOSI: GPIO 32
  - MISO: GPIO 39
  - CLK:  GPIO 25
  - CS:   GPIO 33
  - IRQ:  GPIO 36
Commands: 0x90 (X), 0xD0 (Y)
```

### SD Card
```
Interface: SDSPI on SPI3/VSPI bus
Pins:
  - MISO: GPIO 19
  - MOSI: GPIO 23
  - CLK:  GPIO 18
  - CS:   GPIO 5
Mount point: /sdcard
```

## 🔗 Key References from Terminado

### Display Implementation
- **File**: `terminado/gfx_conf.h`
- **Library**: LovyanGFX (Arduino)
- **Status**: Ported to ESP-IDF via LovyanGFX managed component

### Keyboard Implementation
- **File**: `terminado/terminado.ino`
- **Library**: BBQ10Keyboard (Arduino)
- **Status**: Reimplemented with ESP-IDF I2C master driver
- **Key Features**:
  - Modifier keys (Fn, Ctrl, Alt)
  - Arrow key combinations
  - Special key mappings

### Font System
- **Files**: `font5x7.h`, `spleen-5x8.h`
- **Status**: Copied to esposito project
- **Usage**: Can be used directly for text rendering

## 🚀 Next Steps

1. **Dynamic App Loading**: Implement dlopen/dlsym for .so app libraries
2. **App Switcher**: Key combo detection and UI
3. **Checkpoint Serialization**: JSON or binary format for SPIFFS state storage
4. **Create More Apps**: Demonstrate app switching capabilities

## 📝 Implementation Notes

### Why Direct SPI/I2C Instead of Libraries?
- Arduino libraries not compatible with ESP-IDF
- Need hardware-level control for event system
- Better performance and memory management

### Terminado Differences
- **Terminado**: Arduino-based VT100 emulator
- **Esposito**: ESP-IDF OS with dynamic app loading
- **Shared**: Same hardware, similar display/keyboard needs
- **Different**: Esposito needs event-driven architecture vs terminal loop

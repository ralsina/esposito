# Hardware Integration Status

## ✅ Completed

### Project Structure
- ESP-IDF compatible project structure
- Hardware configuration based on terminado
- Font files copied from terminado (font5x7.h, spleen-5x8.h)

### I2C Configuration
- BBQ20 Keyboard I2C pins configured (GPIO 22/27)
- I2C driver initialization
- Keyboard detection logic

### SPI Configuration
- ST7789 display SPI pins configured
- SPI bus initialization
- Basic display framework

## 🔨 In Progress

### Display Driver
- **Status**: Framework in place, needs ST7789 initialization commands
- **Reference**: `terminado/gfx_conf.h` for LovyanGFX library usage
- **TODO**:
  - Port LovyanGFX library to ESP-IDF or use SPI directly
  - Implement ST7789 initialization sequence
  - Add basic drawing primitives (pixels, text, rectangles)

### Keyboard Driver
- **Status**: I2C configured, needs BBQ20 protocol implementation
- **Reference**: `terminado/terminado.ino` (key handling logic)
- **TODO**:
  - Implement BBQ20 keyboard I2C protocol
  - Port key mapping logic from terminado
  - Add modifier key handling (Fn, Ctrl, Alt)

### Touch Driver
- **Status**: Not implemented (disabled in terminado due to I2C conflict)
- **Reference**: `terminado/gfx_conf.h` (commented out touch code)
- **TODO**:
  - Implement if touch is available on hardware
  - Otherwise focus on keyboard input

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
```

### Touch: (Optional)
```
Interface: I2C
Address: 0x14
Note: Conflicts with BBQ20 keyboard on same I2C bus
```

## 🔗 Key References from Terminado

### Display Implementation
- **File**: `terminado/gfx_conf.h`
- **Library**: LovyanGFX (Arduino)
- **Challenge**: Need ESP-IDF equivalent or direct SPI implementation

### Keyboard Implementation
- **File**: `terminado/terminado.ino`
- **Library**: BBQ10Keyboard (Arduino)
- **Challenge**: Need to implement I2C protocol directly
- **Key Features**:
  - Modifier keys (Fn, Ctrl, Alt)
  - Autorepeat functionality
  - Special key combinations

### Font System
- **Files**: `font5x7.h`, `spleen-5x8.h`
- **Status**: Copied to esposito project
- **Usage**: Can be used directly for text rendering

## 🚀 Next Steps

1. **Display Driver**: Choose approach
   - Option A: Port LovyanGFX to ESP-IDF (complex but feature-rich)
   - Option B: Implement ST7789 SPI directly (simpler, basic features)

2. **Keyboard Driver**: Implement BBQ20 I2C protocol
   - Study BBQ10Keyboard Arduino library
   - Implement I2C read/write functions
   - Port key mapping logic

3. **Test Hardware Integration**
   - Verify display output
   - Verify keyboard input
   - Test app event system with real hardware

## 📝 Implementation Notes

### Why Direct SPI/I2C Instead of Libraries?
- Arduino libraries not compatible with ESP-IDF
- Need hardware-level control for event system
- Better performance and memory management
- Learn hardware-specific optimizations

### Terminado Differences
- **Terminado**: Arduino-based VT100 emulator
- **Esposito**: ESP-IDF OS with dynamic app loading
- **Shared**: Same hardware, similar display/keyboard needs
- **Different**: Esposito needs event-driven architecture vs terminal loop

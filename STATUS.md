# Esposito OS Development Status

## ✅ Completed

### Project Structure
- ESP-IDF project initialized with CMake build system
- Main component with all core OS modules
- Example app structure created
- SDK configuration defaults set up

### Core OS Modules
- **Event System**: Central event queue with subscription support
- **App Lifecycle**: Load/unload/switch between apps
- **Filesystem**: SPIFFS integration for state storage
- **Checkpoint System**: Basic framework for app state persistence
- **Hardware Abstraction**: Display (LovyanGFX/ST7789), touch (XPT2046 GPIO bitbang), keyboard (BBQ20 I2C), SD card (SDSPI), timer, serial

### Example App
- Hello World app demonstrating the interface
- Touch event handling
- Timer events
- State checkpoint functionality

## 🔨 Current Status

The project has all core hardware drivers implemented and working on real hardware.

### What Works
- Project structure and build system
- Event queue architecture
- App lifecycle management
- Basic checkpoint API
- Touch driver (XPT2046 GPIO bit-banging with IRQ)
- SD card (SDSPI on VSPI bus, FAT32)
- BBQ20 keyboard (I2C with modifier keys)
- Display (LovyanGFX/ST7789, text mode subsystem)
- Text mode (64x30 grid, 16 colors, attributes)

### What Needs Implementation
- **Dynamic Loading**: dlopen/dlsym for app libraries (.so files)
- **App Switcher**: Key combo detection and UI
- **Checkpoint Serialization**: JSON or binary format for state storage

## 🚀 Next Steps

1. **Implement Dynamic Loading**: Add dlopen support for loading .so app libraries
2. **Create More Apps**: Demonstrate app switching capabilities
3. **App Switcher**: Key combo detection and UI
4. **Build & Test**: Compile and flash to actual hardware

## 📋 Technical Notes

### App Interface
Apps must export these functions (C naming convention):
```c
void app_init(app_context_t *ctx);
void app_checkpoint(app_context_t *ctx);
void app_close(app_context_t *ctx);
void app_event(app_context_t *ctx, event_t *event);
```

### Event Types
- `EVENT_TIMER`: Periodic timer events
- `EVENT_TOUCH`: Touchscreen input (x, y, pressed)
- `EVENT_KEYBOARD`: BBQ20 keyboard input
- `EVENT_KEY_COMBO`: Special key combinations for OS functions
- `EVENT_SERIAL`: Serial data input

### Build Requirements
- ESP-IDF framework
- ESP32 toolchain
- CYD (Cheap Yellow Display) hardware
- BBQ20 keyboard

## 📝 Architecture Decisions

- **Single-tasking**: Only one app active at a time
- **Palm-style lifecycle**: Apps save state on exit, restore on init
- **Event subscriptions**: Apps only receive events they care about
- **SD card storage**: Apps loaded from `/apps` directory
- **No compositor**: Full screen per app, no window management

## 🔗 References

- `terminado` project: Keyboard and display implementation for BBQ20 + CYD
- ESP-IDF documentation: https://docs.espressif.com/projects/esp-idf/

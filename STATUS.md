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
- **Hardware Abstraction**: Stubs for display, touch, keyboard, timer, serial

### Example App
- Hello World app demonstrating the interface
- Touch event handling
- Timer events
- State checkpoint functionality

## 🔨 Current Status

The project is in a **proof-of-concept** stage. All core structures are in place but most implementations are stubs.

### What Works
- Project structure and build system
- Event queue architecture
- App lifecycle management
- Basic checkpoint API

### What Needs Implementation
- **Display Driver**: Implement based on `terminado` project
- **Touch Driver**: Raw coordinate reading
- **Keyboard Driver**: BBQ20 integration from `terminado`
- **Dynamic Loading**: dlopen/dlsym for app libraries (.so files)
- **SD Card Support**: Filesystem operations and app scanning
- **App Switcher**: Key combo detection and UI
- **Checkpoint Serialization**: JSON or binary format for state storage

## 🚀 Next Steps

1. **Integrate Hardware Drivers**: Copy/refer to `terminado` project for display and keyboard
2. **Implement Dynamic Loading**: Add dlopen support for loading .so app libraries
3. **SD Card Filesystem**: Enable SPIFFS and implement app scanning
4. **Build & Test**: Compile and flash to actual hardware
5. **Create More Apps**: Demonstrate app switching capabilities

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

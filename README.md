# Esposito OS

A simple operating system for ESP32 Cheap Yellow Display (CYD) with dynamic app loading.

## Features

- Dynamic app loading from SD card
- Event-driven architecture
- Palm-style app lifecycle (checkpoint/save state)
- Single-tasking, single-app model
- Touchscreen and keyboard support (BBQ20)

## Architecture

- **Framework**: ESP-IDF
- **App Storage**: SD card filesystem
- **Event System**: Central event queue with app subscriptions
- **Display**: Full screen control per app
- **Licensing**: Apps are dynamically loaded libraries

## App Interface

Each app library (.so) must export these functions:

```c
void app_init(AppContext* ctx);           // Restore state or fresh start
void app_checkpoint(AppContext* ctx);     // Save state to SD
void app_close(AppContext* ctx);          // Cleanup
void app_event(AppContext* ctx, Event* e); // Handle subscribed events
```

## Building

```bash
idf.py build
```

## Flashing

```bash
idf.py flash
```

## Hardware

- ESP32 Cheap Yellow Display (2USB version)
- BBQ20 Keyboard
- Touchscreen display
- SD card

## Project Structure

```
esposito/
├── main/           # Core OS implementation
├── apps/           # Application libraries
└── README.md
```

## References

This project references the `terminado` project for keyboard and display implementation.

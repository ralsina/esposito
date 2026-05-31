# Esposito OS

A simple operating system for ESP32 Cheap Yellow Display (CYD) with dynamic app loading.

**Website: https://esposito.ralsina.me**

![Esposito in action](https://esposito.ralsina.me/reader2.png)

## Features

- Dynamic app loading from SD card
- Event-driven architecture
- Palm-style app lifecycle (checkpoint/save state)
- Single-tasking, single-app model
- Touchscreen and keyboard support (BBQ20)
- Multiple color palettes (CGA, CGA Light, Solarized Dark, Solarized Light)
- Arduboy game compatibility layer
- Game Boy (DMG) emulation via Peanut-GB (40-60 FPS)
- Switchable fonts (Hack, IBM Plex Mono, Inconsolata, Ioskeley, Kode Mono, Nova)
- WiFi connectivity with HTTP/HTTPS support

## Architecture

- **Framework**: ESP-IDF
- **Display**: LovyanGFX on ILI9341 (320x240), text mode and graphics mode
- **App Storage**: SD card filesystem
- **Event System**: Central event queue with app subscriptions
- **App Loading**: ELF binaries loaded dynamically from SD card
- **Input**: BBQ20 keyboard (I2C) + resistive touchscreen

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

```text
esposito/
├── main/           # Core OS implementation
├── apps/           # Application libraries
└── README.md
```

## App READMEs

- [apps/breakout/README.md](apps/breakout/README.md)
- [apps/calc/README.md](apps/calc/README.md)
- [apps/clock/README.md](apps/clock/README.md)
- [apps/file_manager/README.md](apps/file_manager/README.md)
- [apps/file_picker/README.md](apps/file_picker/README.md)
- [apps/kilo/README.md](apps/kilo/README.md)
- [apps/lali/README.md](apps/lali/README.md)
- [apps/paint/README.md](apps/paint/README.md)
- [apps/peanut_gb/README.md](apps/peanut_gb/README.md)
- [apps/reader/README.md](apps/reader/README.md)
- [apps/settings/README.md](apps/settings/README.md)
- [apps/snake/README.md](apps/snake/README.md)

## References

This project references the `terminado` project for keyboard and display implementation.

## Emulator

- [EMULATOR.md](EMULATOR.md)
- [wokwi/README.md](wokwi/README.md)

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
- Game Boy (DMG) emulation via gameboy app (40-60 FPS)
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

## Firmware Updates (OTA and SD Card)

Esposito supports two firmware update paths.

### 1) OTA update from the device UI

Use the Settings app:

1. Open Settings.
2. Go to System.
3. Use Check Update to compare current and latest versions.
4. Use Update Firmware to download and apply the update.

Notes:

- OTA download is resumable and can continue across interrupted HTTP transfers.
- The updater validates the downloaded image before applying it.

### 2) Manual update via SD card

You can also update firmware by placing a firmware image on the SD card.

1. Copy the firmware binary to:

	/sdcard/system/firmware.bin

2. Reboot the device.

At boot, Esposito checks for that file. If present, it launches the update stub,
applies the image, and then returns to normal boot.

Tips:

- Ensure the file is a valid ESP32 app image (starts with ESP image magic 0xE9).
- Keep only the firmware you want to apply in that location.

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

### Built-in Apps (firmware)
- [apps/launcher/README.md](apps/launcher/README.md) - App launcher and file browser
- [apps/settings/README.md](apps/settings/README.md) - System settings and configuration

### Text Apps
- [apps/hecto/README.md](apps/hecto/README.md) - Paged text editor with large file support
- [apps/kilo/README.md](apps/kilo/README.md) - Lightweight text editor
- [apps/shell/README.md](apps/shell/README.md) - Command-line interface

### Reader & File Apps
- [apps/reader/README.md](apps/reader/README.md) - E-book reader with page navigation
- [apps/file_manager/README.md](apps/file_manager/README.md) - File browser and manager
- [apps/file_picker/README.md](apps/file_picker/README.md) - File selection dialog

### Utility Apps
- [apps/calc/README.md](apps/calc/README.md) - Scientific calculator
- [apps/clock/README.md](apps/clock/README.md) - Clock with NTP sync
- [apps/numbers/README.md](apps/numbers/README.md) - Number games

### Creative Apps
- [apps/paint/README.md](apps/paint/README.md) - Drawing application
- [apps/image_viewer/README.md](apps/image_viewer/README.md) - Image viewer
- [apps/lali/README.md](apps/lali/README.md) - Drawing app

### Games
- [apps/breakout/README.md](apps/breakout/README.md) - Breakout game clone
- [apps/snake/README.md](apps/snake/README.md) - Snake game
- [apps/gameboy/README.md](apps/gameboy/README.md) - Game Boy emulator

### Development Tools
- [apps/hello_world/README.md](apps/hello_world/README.md) - Example "Hello World" app

## References

This project references the `terminado` project for keyboard and display implementation.

## Emulator

- [EMULATOR.md](EMULATOR.md)
- [wokwi/README.md](wokwi/README.md)

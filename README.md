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
- **Display**: LovyanGFX on ST7789 (320x240), text mode and graphics mode
- **App Storage**: SD card filesystem
- **Event System**: Central event queue with app subscriptions
- **App Loading**: ELF binaries loaded dynamically from SD card
- **Input**: BBQ20 keyboard (I2C) + resistive touchscreen

## Trust Model

Esposito is a personal device. Applications are fully trusted (no sandboxing), physical access is root, and the network is untrusted. The full model — including what is and is not protected, what is implemented today versus planned, and how to recover from a misbehaving app — is in [docs/trust-model.md](docs/trust-model.md).

## App Interface

Each app is compiled to a relocatable ELF binary (`program.elf`) and must export these functions:

```c
void app_init(app_context_t *ctx);            // Restore state or fresh start
void app_checkpoint(app_context_t *ctx);      // Save state to SD
void app_close(app_context_t *ctx);           // Cleanup
void app_event(app_context_t *ctx, event_t *e); // Handle subscribed events
```

## Building

```bash
idf.py build
```

## Linux Emulator

Esposito includes a desktop emulation environment for testing apps without real hardware. It uses SDL2 to simulate the display and keyboard, and reimplements the OS API surface in C for Linux.

### Prerequisites

```bash
sudo apt install libsdl2-dev gcc make
```

### Usage

```bash
cd linux
./run.sh                    # Default: hello_world
./run.sh reader             # Run the reader app
./run.sh file_picker        # Run file picker
```

### Controls

| Key | Action |
|-----|--------|
| `W` / `S` | Previous / next page (reader) |
| `↑` / `↓` / `PgUp` / `PgDn` | Page navigation |
| `Esc` | Back / Exit current mode |
| `Ctrl+Q` / `Ctrl+Esc` | Quit emulator |
| Mouse click | Touch simulation |
| `A`-`Z`, `0`-`9` | Keyboard input |

### Emulated SD Card

The emulator creates `/sdcard/` as a staging directory. You can point `ESP_SD_CARD` at an existing mount:

```bash
ESP_SD_CARD=/mnt/sdcard ./run.sh reader
```

### Architecture

The emulator lives in `linux/` and reimplements the OS API surface in plain C + SDL2:

| File | What it provides |
|------|-----------------|
| `src/text_mode.c` | VLW bitmap font rendering, text grid, UTF-8 support, attribute rendering (borders, underline, bold, inverse) |
| `src/graphics_mode.c` | 4bpp indexed framebuffer with 16-color RGB565 palette |
| `src/sprite.c` | Sprite creation and scaled rendering (used by the gameboy app) |
| `src/os_core.c` | `os_task_create` (pthread), `os_semaphore_*` (SDL semaphores), `esp_timer_get_time` |
| `src/app_config.c` | `config_get/set_*` (JSON file on disk) |
| `src/hardware.c` | Display stubs, keyboard, serial UART stubs |
| `src/flash_rom.c` | ROM file loading for the gameboy emulator |
| `src/wifi.c` | WiFi / HTTP stubs |
| `src/main.c` | Event loop, SDL key → app event translation |

### Limitations

- No real WiFi or HTTP — `os_http_get()` returns errors
- Touch simulated via mouse clicks
- No serial port — `serial_init` / `serial_write` are no-ops
- No sound
- App switching (`os_load_app`) not fully implemented — exits current app
- 320×240 display at native pixel resolution

### Debugging

The emulator runs as a normal Linux process. All code is compiled with `-g`, so you can run under GDB or use `printf` for debug output.

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
├── main/           # Core OS implementation (event loop, ELF loader, HAL, drivers)
├── apps/           # Application libraries (compiled to ELF binaries)
├── libs/           # Shared libraries for apps (ui2, json, arduboy, lua, serial_rx)
├── fonts/          # Font definitions for display
├── boards/         # Per-board hardware configuration (board.h, display_config.h)
├── scripts/        # Build helpers (build_app.sh, build_test.sh, font generators)
├── linux/          # Desktop emulator (SDL2) for host-side development
├── docs/           # Architecture and design documents
├── stub/           # OTA update stub partition
├── site/           # Static site sources (website)
├── CMakeLists.txt  # ESP-IDF project root
├── partitions.csv  # Flash partition layout
├── sdkconfig       # ESP-IDF configuration
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



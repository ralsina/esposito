---
title: ESP-Osito News for June 16, 2026
date: 2026-06-16 12:00:00 -03:00
---

## The Linux Desktop Emulator

Developing for a 320×240 display with a BBQ20 keyboard over I2C is fun, but there's something to be said for being able to test an app without flashing firmware every 30 seconds. So here's the **Linux emulator** — a desktop build of the Esposito OS API that runs any app as a native SDL2 application.

```bash
# Prerequisites
sudo apt install libsdl2-dev gcc make

# Clone the repo
git clone https://github.com/ralsina/esposito
cd esposito/linux

# Run any app
./run.sh hello_world
./run.sh reader
./run.sh file_picker
```

It Just Works™. Well, mostly. Let's talk about what's under the hood.

### How it works

The emulator in `linux/` reimplements the entire OS API surface that apps link against — but instead of ESP32 hardware, it uses standard Linux C + SDL2:

- **`text_mode.c`** loads the **exact same VLW bitmap fonts** the hardware uses and renders them via SDL2 textures. Full UTF-8 support, border attributes, bold, inverse, underline — just like on the real display.
- **`graphics_mode.c`** provides the 4bpp indexed framebuffer with the 16-color RGB565 palette for pixel-level drawing.
- **`sprite.c`** handles the sprite API used by the Game Boy emulator (more on that below).
- **`os_core.c`** maps ESP32 concepts — tasks, semaphores, timers — to pthreads and SDL semaphores.
- **`app_config.c`** stores config as JSON files on disk rather than in NVS flash.
- The event loop in **`main.c`** translates SDL keypresses and mouse clicks into the same event types apps expect on the device.

The build process is also neat: app source files pass through `sed` to strip ESP32-specific inline assembly (`rsr ccount` and friends) before compilation. No manual source changes needed.

### What you can do with it

The emulator is genuinely useful for rapid iteration:

- **Layout and UI testing** — tweak widget positions, check border rendering, verify icon alignment, all without touching the device
- **Game Boy development** — the gameboy emulator app runs at **60 FPS** in the desktop emulator, with sprite rendering via SDL2 textures. The dual-buffer pipeline, the display task running on a background thread, the semaphore synchronization — it all works.
- **Debugging** — run under GDB, add `printf` calls, inspect memory. No serial monitor needed.
- **The reader app** opens markdown files from `/sdcard/books/`, renders pages, handles navigation, TOC, search — exactly as it does on hardware.

### Emulated SD card

Apps expect an SD card at `/sdcard/`. The emulator creates it automatically, but you can point it at a real card:

```bash
ESP_SD_CARD=/mnt/sdcard ./run.sh reader
```

### What doesn't work (yet)

It's an emulator, not a miracle worker:

- **WiFi / HTTP** — `os_http_get()` returns errors. The clock app won't sync NTP.
- **Serial** — `serial_init` and `serial_write` are no-ops. The reader's "receive over serial" feature won't work.
- **Sound** — not implemented.
- **App switching** — `os_load_app()` exits the current app rather than loading another.
- **Touch** — simulated via mouse clicks, no multi-touch.

### What's next

The emulator started as a quick hack to debug the gameboy sprite pipeline and grew into something much more useful. I'd love to add:

- Real HTTP via libcurl so network-dependent apps can actually be tested
- A debug overlay showing framebuffer contents, frame timing, memory usage
- Support for the `ESP_FONT_SIZE` env var to test different font sizes

In the meantime, it's already a massive quality-of-life improvement for development. Give it a try — `./run.sh reader` with a markdown file in `/sdcard/books/` is a good place to start.

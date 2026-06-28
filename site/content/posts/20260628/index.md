---
title: ESP-Osito News for June 28, 2026
date: 2026-06-28 18:00:00 -03:00
---

## New Board: Guition JC4827W543

Esposito OS now runs on the **Guition JC4827W543**, a 4.3" ESP32-S3 display
panel with 480×272 resolution, 8 MB octal PSRAM, capacitive touch, and an
SD card slot. I bought it for **$19** — still very much in the spirit of
the Cheap Yellow Display family, just with a bigger screen and a faster
chip.

The port required a new display driver (NV3041A over QSPI), GT911
capacitive touch support, and separate I2C buses for the touchscreen and
BBQ20 keyboard. The Guition also has a JST connector on the board for a
BBQ20 keyboard, so the physical keyboard experience carries over
seamlessly.

Both boards are now first-class citizens: the firmware auto-detects the
target, the [install page](/install/) lets you pick your board, and the
release pipeline builds firmware for both. The App Store even serves
per-architecture app binaries (Xtensa LX6 for the CYD, LX7 for the
Guition) so apps downloaded from the device always match.

## Bluetooth Keyboards

The Guition's ESP32-S3 has Bluetooth, and since the app heap lives in
PSRAM, there was plenty of free internal RAM to bring up a BLE HID Host.
**You can now pair a wireless Bluetooth keyboard** and use it alongside
(or instead of) the BBQ20.

How it works:

- **Settings app** has a new "BT" tab: scan for devices, tap to pair,
  disconnect when done. The pairing is saved, so the keyboard reconnects
  automatically on boot and when you toggle it off and back on.
- **Keyboard events flow through the same pipeline** as the BBQ20 — the
  BLE driver parses HID usage codes, maps them to the existing keycode
  space (including extended keys like F1-F12, arrows, Insert/Home/etc.),
  and pushes them via the standard event loop. Apps don't need any
  changes.
- **Apps detect the keyboard automatically**: `keyboard_is_available()`
  returns true when either the BBQ20 or a BLE keyboard is connected, so
  apps that hide their on-screen keyboard when a physical one is present
  work correctly with Bluetooth too.

BLE is enabled only on the ESP32-S3 build (it needs Bluedroid, which
takes ~50 KB of internal RAM). The CYD build compiles stubs and is
completely unaffected.

## Performance: Fixing the Backslide

Adding all these features — BLE, PSRAM sprite buffering, per-architecture
app builds — came with a cost. App switching had crept up to around
**2 seconds** on the Guition. That's not great UX when you're trying to
flip between the reader and the launcher.

So we spent some time with the profiler. The results were instructive:

**Flash erase (780 ms → 35 ms):** The ELF loader was erasing the
*entire* 1.3 MB app partition on every single app load, even though it
only writes ~13 KB of code. Fixed by erasing only the sectors that will
actually be written.

**SD card I/O (994 ms → 69 ms):** The ELF loader was reading relocation
entries one at a time — 255 individual 8-byte reads from the SD card,
each with its own `fseek`+`fread` overhead. Fixed by batch-reading the
entire relocation section into memory. On boards with PSRAM, the entire
ELF file is now buffered in one `fread`, eliminating all SD card access
during parsing.

**App list scanning (900 ms → 0 ms):** The launcher was reading 23
`manifest.cfg` files from the SD card on every launch — both to build
the app list and to get display names. Fixed by caching the scan results
(including display names) in the OS layer. The cache is invalidated when
apps are installed or removed via the App Store.

**Config directory creation (60 ms → 0 ms):** Every call to
`config_bind_app()` was making three `mkdir()` syscalls on the SD card
"just in case" the directories didn't exist. Fixed by caching which app's
directories have been verified.

### Where we are now

App switch time went from **~2000 ms to ~500 ms** — a 4x improvement.
The remaining time is mostly the single `fread` that pulls the ELF from
the SD card into PSRAM (~70 ms) and the launcher's UI rendering (~130 ms).
That's fast enough to feel snappy.

---
title: ESP-Osito News for June 13, 2026
date: 2026-06-13 12:00:00 -03:00
---

## Esposito SDK v0.2

Until now, building an app for Esposito meant cloning the full firmware repo, setting up ESP-IDF, and building everything together. The **Esposito SDK** changes that — it's a standalone 80KB tarball that contains everything you need to compile apps: headers, library sources, linker scripts, and the pre-generated symbol table. No firmware checkout required.

### What's in the box

| Item | Purpose |
|------|---------|
| `build_app.sh` | Build script with `-l ui2 -l json` library flags |
| `include/` | All public API headers: `os_core.h`, `hardware.h`, `text_mode.h`, `wifi.h`, etc. |
| `include/ui2/` | Full ui2 widget toolkit (layout, buttons, lists, tab views, on-screen keyboard) |
| `include/json/` | Core JSON parser |
| `lib/` | Source files for ui2 and json, compiled alongside your app |
| `ld/app.ld` | Linker script for app memory layout |
| `ld/os_symbols.ld` | 348 exported OS symbols with their firmware addresses |

### Quick start

```bash
# Source ESP-IDF for the xtensa-esp32-elf toolchain
. /opt/esp-idf/export.sh

# Download and unpack
curl -LO https://esposito.ralsina.me/sdk/esposito-sdk-0.2.tar.gz
tar xzf esposito-sdk-0.2.tar.gz

# Build your app
cd esposito-sdk-0.2
./build_app.sh -l ui2 my_app/app.c
```

The output `.elf` goes to `/sdcard/apps/<appname>/program.elf` on your SD card.

### The developer experience

Apps export four lifecycle functions (`app_init`, `app_event`, `app_checkpoint`, `app_close`) and link against whatever OS services they need — display, text mode, WiFi, HTTP, filesystem, config, timers, and more. The ui2 library provides a widget hierarchy with layouts, buttons, lists, tab views, text input, progress bars, and an on-screen keyboard.

The SDK is versioned alongside the firmware. Apps built with SDK v0.2 work with firmware v0.2, and vice versa.

### Full documentation

See the **[SDK docs](https://github.com/ralsina/esposito/blob/main/docs/sdk.md)** for detailed usage, the complete API reference, app structure, and manifest format.

### Release v0.2

This release bundles everything from the past three days of work: the **App Store** with catalog browsing and install/update/uninstall, the **capability system** for hardware requirements, and the **SDK** for third-party development. Tagged as [`v0.2`](https://github.com/ralsina/esposito/releases/tag/v0.2) on GitHub.

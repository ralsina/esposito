# Esposito SDK

The SDK lets you build Esposito apps without cloning the full firmware repository. It provides the headers, library sources, linker scripts, and symbol table needed to compile an app ELF.

## Quick Start

```bash
# 1. Install ESP-IDF (for the xtensa-esp32-elf toolchain only)
#    https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

# 2. Download the SDK
curl -LO https://esposito.ralsina.me/sdk/esposito-sdk-latest.tar.gz
tar xzf esposito-sdk-latest.tar.gz
cd esposito-sdk-*/

# 3. Source ESP-IDF to get the toolchain on PATH
. /opt/esp-idf/export.sh

# 4. Build your app
./build_app.sh -l ui2 path/to/app.c
```

The output `.elf` goes to your SD card as `/sdcard/apps/<appname>/program.elf`.

## Prerequisites

- **`xtensa-esp32-elf-*` cross-compiler toolchain** — from [ESP-IDF](https://docs.espressif.com/projects/esp-idf/). Only the toolchain is needed, not the full IDF framework. Source `export.sh` to add it to your PATH.
- **`xxd`** — for embedding `manifest.cfg` into the app ELF (usually included with `vim-common`).

## SDK Contents

```
esposito-sdk/
├── build_app.sh              # Standalone build script
├── version.txt               # Firmware version this SDK targets
├── ld/
│   ├── app.ld                # Linker script (app memory layout)
│   └── os_symbols.ld         # Exported OS symbols and their addresses
├── include/
│   ├── os_core.h             # Event types, app context, OS API
│   ├── hardware.h            # Display, touch, LED functions
│   ├── text_mode.h           # Text grid display (64x30 character cells)
│   ├── app_config.h          # App config/settings API
│   ├── app_manifest.h        # Manifest reading/writing
│   ├── terminal_mode.h       # VT100 terminal emulator
│   ├── graphics_mode.h       # Graphics framebuffer mode
│   ├── wifi.h                # WiFi connectivity
│   ├── fonts.h               # Font table and metadata
│   ├── ui2/                  # Widget library (layout, buttons, lists, etc.)
│   │   ├── ui2.h
│   │   ├── ui2_widget.h
│   │   ├── ui2_button.h
│   │   ├── ui2_list.h
│   │   ├── ui2_tabview.h
│   │   ├── ui2_text.h
│   │   ├── ui2_text_input.h
│   │   ├── ui2_screen.h
│   │   ├── ui2_layout.h
│   │   ├── ui2_progressbar.h
│   │   ├── ui2_osk.h
│   │   ├── ui2_buffer.h
│   │   └── lucide_icons.h
│   ├── json/
│   │   └── core_json.h       # C JSON parser
│   └── serial_rx/
│       └── serial_rx.h       # Serial port reading
└── lib/
    ├── ui2/                  # ui2 widget sources (compiled with your app)
    ├── json/
    │   └── core_json.c
    └── serial_rx/
        └── serial_rx.c
```

## Usage

### Basic app (no libraries)

```bash
./build_app.sh my_app/app.c
```

Simple apps that only use text mode or display functions don't need library flags.

### App with ui2 widgets

```bash
./build_app.sh -l ui2 my_app/app.c
```

### App with ui2 and JSON parsing

```bash
./build_app.sh -l ui2 -l json my_app/app.c
```

### Custom output directory

```bash
./build_app.sh -l ui2 my_app/app.c /path/to/output/
```

### Using from another directory

Set `ESPOSITO_SDK_DIR` to point at the SDK root:

```bash
ESPOSITO_SDK_DIR=/opt/esposito-sdk /opt/esposito-sdk/build_app.sh -l ui2 my_app/app.c
```

## App Structure

Apps must export exactly four C functions:

```c
#include "os_core.h"
#include "hardware.h"
#include "text_mode.h"
#include <string.h>

static app_context_t *g_ctx;

void app_init(app_context_t *ctx) {
    g_ctx = ctx;
    ctx->subscriptions = EVENT_TIMER;  // or EVENT_TOUCH, etc.
    ctx->timer_interval_ms = 1000;
    // Initialize display, register callbacks, etc.
}

void app_event(app_context_t *ctx, event_t *event) {
    // Handle events the app subscribed to
    if (event->type == EVENT_TIMER) {
        // Do periodic work
    }
}

void app_checkpoint(app_context_t *ctx) {
    // Save state before app switching
}

void app_close(app_context_t *ctx) {
    // Cleanup
}
```

### Optional: manifest.cfg

```ini
name=My App
launcher=yes
version=1.0
short_description=Does something useful
long_description=A longer description shown in the app store detail view
homepage=https://github.com/me/my-app
requires=keyboard,wifi
```

If `manifest.cfg` exists next to your source files, the build script embeds it automatically.

## Capabilities

Apps can declare hardware requirements in `manifest.cfg`:

| Capability | Meaning |
|------------|---------|
| `keyboard` | BBQ20 physical keyboard (I2C) |
| `touch`    | Resistive touchscreen |
| `wifi`     | WiFi connectivity |
| `psram`    | PSRAM (extra heap memory) |

The firmware checks these at app load time and refuses to start an app whose requirements aren't met. The App Store warns about missing capabilities but allows installation.

## Available OS API

The SDK's `os_symbols.ld` provides ~350 symbols including:

- **Display**: `display_clear`, `display_draw_text`, `display_draw_jpg_fit`, `display_draw_scaled_text_bg`, etc.
- **Input**: `keyboard_read_event`, `keyboard_is_available`
- **Text mode**: `text_mode_init`, `text_mode_print_at`, `text_mode_set_attr`, VT100 line drawing
- **Terminal**: `terminal_mode_init`, `terminal_mode_process_bytes`, `terminal_mode_handle_key`
- **Graphics**: `graphics_mode_init`, `graphics_draw_line`, `graphics_blit_scaled`
- **Files**: `fopen`, `fread`, `fwrite`, `opendir`, `readdir`, `stat`, `mkdir`
- **Config**: `config_get_int`, `config_set_string`, `config_bind_app`
- **Network**: `wifi_init`, `wifi_connect`, `os_http_get`, `os_http_download`, `os_download_via_os`
- **System**: `os_load_app`, `os_exit`, `os_has_capability`, `malloc`, `free`, `printf`, `snprintf`
- **Timers**: `esp_timer_get_time`, `os_task_create`, `os_semaphore_create`
- **Math**: `sinf`, `cosf`, `sqrtf`, `powf`, `rand`, etc.
- **Lua**: Full Lua 5.x C API (for Lua scripting apps)

## Building Apps Without the SDK

If you already have the full Esposito checkout, use `scripts/build_app.sh` instead:

```bash
# From the project root, after building firmware:
scripts/build_app.sh -l ui2 -l json apps/clock/app.c build/apps/
```

This requires a built firmware ELF (`build/esposito.elf`) to generate the symbol table.

## Version Compatibility

Apps built with SDK version X.Y work with firmware version X.Y. The `os_symbols.ld` file is generated from a specific firmware build, so mismatched versions may cause runtime crashes or missing symbols. Check `version.txt` in the SDK root.

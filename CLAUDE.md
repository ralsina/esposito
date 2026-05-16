# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Quick Start - Full Build Pipeline
The complete build process for development:

```bash
# Build everything and deploy (requires SD card mounted)
bash scripts/build_test.sh
```

This script:
1. Builds the firmware
2. Generates the OS symbol table
3. Builds all app ELFs from apps/*/app.c
4. Copies apps to SD card at `/run/media/ralsina/ESPRESSIF` (override with `SD_MOUNT=`)
5. Flashes firmware to device

### Build Firmware Only
```bash
# Source ESP-IDF environment first
. /opt/esp-idf/export.sh  # or your ESP-IDF path

# Build firmware
idf.py build
```

### Flash to Device
```bash
# Flash and monitor serial output
idf.py flash monitor

# Flash only
idf.py flash
```

### Monitor Serial Output
```bash
idf.py monitor
# Or use the custom monitor script:
./monitor.sh
```

### Build Individual Apps
```bash
# Source ESP-IDF environment first
. /opt/esp-idf/export.sh

# Build a single app
bash scripts/build_app.sh apps/<app_name>/app.c
# Output: build/apps/<app_name>.elf

# Build app with shared libraries
bash scripts/build_app.sh -l ui apps/my_app/app.c
```

**Important**: Always source the ESP-IDF environment before building apps. The build script needs ESP-IDF tools in the PATH.

## Project Architecture

Esposito is a dynamic app-loading OS for ESP32 CYD (Cheap Yellow Display). The firmware provides a runtime environment where apps are loaded as ELF binaries from the SD card.

### Core Components

**main/** - Firmware implementation
- `os_core.c` - Core OS services, event loop, app lifecycle management
- `elf_loader.c` - Dynamic ELF loader for app binaries  
- `app_loader.c` - App discovery and loading from SD card
- `app_launcher.c` - Launcher UI for running apps
- `app_manifest.c` - App metadata system (manifest.cfg files)
- `checkpoint.c` - State persistence for app switching
- `hardware.cpp` - Hardware abstraction layer
- `bbq20_keyboard.c` - BBQ20 keyboard driver
- `touchscreen.c` - Touch input handling
- `text_mode.cpp` - Text grid display system with VT100 line drawing
- `terminal_mode.c` - VT100 terminal emulator for CLI apps
- `wifi.cpp` - WiFi connectivity and networking
- `os_symtab.c` - Symbol table exports for app API

**apps/** - Application libraries
- Each app is a standalone C program compiled to a position-independent ELF
- Apps must export: `app_init()`, `app_event()`, `app_checkpoint()`, `app_close()`
- Apps communicate with OS through exported symbols in `os_symtab.c`
- `manifest.cfg` files control app visibility and file associations
- `deps` files in app directories list library dependencies (one lib name per line)
- `app.ld` linker scripts define memory layout (can use template from `apps/app_template/`)

**libs/ui/** - Shared UI library for apps

**fonts/** - Font definitions for display

## App API Surface

Apps link against the firmware through exported symbols. Key API groups:

### App Lifecycle
- `app_init(app_context_t *ctx)` - Initialize app, set event subscriptions
- `app_event(app_context_t *ctx, event_t *event)` - Handle events
- `app_checkpoint(app_context_t *ctx)` - Save state before switching
- `app_close(app_context_t *ctx)` - Cleanup and release display

### App Switching
- `os_load_app(name)` - Launch another app
- `os_open_app_with_file(name, path)` - Open file with app
- `os_consume_startup_file(out, size)` - Read startup file

### Display (Pixel and Text)
- `display_clear(color)`, `display_draw_pixel()`, `display_fill_rect()`
- `display_draw_text()`, `display_draw_text_bg()`, `display_draw_char_at()`
- `display_draw_scaled_text_bg()` - Large scaled text for UI
- `display_draw_jpg_fit()` - JPEG rendering with scaling
- `text_mode_init()`, `text_mode_print_at*()` - Text grid system
- `text_mode_set_attr(TEXT_ATTR_LINE_DRAWING)` - Enable VT100 line drawing

### Input
- `keyboard_read_event(&key, &pressed)` - Read BBQ20 keyboard events

### Time & NTP
- `os_get_time_status(&status)`, `os_time_is_synchronized()`

### Config & Settings
- `checkpoint_save*()`, `checkpoint_load*()` - App state persistence
- `config_bind_app(name)`, `config_get*()`, `config_set*()` - App settings
- `os_settings_get*()`, `os_settings_set*()` - Global system settings

### Networking
- `wifi_init()`, `wifi_connect()`, `wifi_scan()` - WiFi management
- `os_http_get(url, out, size, timeout)` - HTTP requests

### Files
- Standard C file I/O: `fopen`, `fread`, `fwrite`, `fclose`, `fseek`
- Directory operations: `opendir`, `readdir`, `closedir`, `mkdir`, `stat`

### Terminal Mode (VT100)
- `terminal_mode_init()`, `terminal_mode_process_bytes()`
- `terminal_mode_handle_key()`, `terminal_mode_render()`

### Memory
- `malloc`, `calloc`, `realloc`, `free` use app-local heap

## Hardware

- ESP32 Cheap Yellow Display (2USB version)
- 320×240 touchscreen display
- BBQ20 keyboard (I2C)
- SD card for app storage
- WiFi connectivity

## Development Workflow

### Quick Development Cycle
For rapid iteration, use the build test script:
```bash
bash scripts/build_test.sh
```
This builds firmware + all apps, copies apps to SD card, and flashes firmware.

### Manual Development Steps
1. **Source ESP-IDF environment**: `. /opt/esp-idf/export.sh`
2. **Build firmware**: `idf.py build`
3. **Build app**: `bash scripts/build_app.sh apps/<app>/app.c`
4. **Copy to SD**: Copy resulting ELF to `/sdcard/apps/<app>/program.elf`
5. **Test**: Flash firmware, insert SD card, run app
6. **Debug**: Use `idf.py monitor` or `./monitor.sh` for serial output

### App Dependencies
If an app uses shared libraries from `libs/`, create a `deps` file:
```
# apps/my_app/deps
ui
json
```

The build system will automatically link these libraries.

## Key Patterns

- **Event-driven**: Apps subscribe to events in `app_init()`, handle in `app_event()`
- **Checkpoint/save state**: Apps save state in `app_checkpoint()` for fast switching
- **Single-tasking**: Only one app runs at a time, OS manages switching
- **Dynamic loading**: Apps are loaded ELF binaries, not statically linked
- **Palm-style lifecycle**: Similar to Palm OS — checkpoint, switch, restore

## HTTPS/SSL Configuration

HTTPS is supported and enabled by default. The system includes:
- Full Mozilla certificate bundle (200 root certificates)
- TLS 1.2 support via mbedTLS
- `esp_crt_bundle_attach` for certificate verification

### Using HTTPS in Apps
Simply use HTTPS URLs with `os_http_get()`:
```c
// HTTPS works automatically
char response[256];
int result = os_http_get("https://api.example.com/data", response, sizeof(response), 5000);
if (result > 0) {
    // Success - response contains data
} else {
    // Failed - result is negative status code or error
}
```

### HTTPS Debugging
The clock app (`apps/clock/app.c`) includes detailed HTTPS logging. Check the serial output for:
- Connection attempts and results
- Certificate verification status  
- Response data and HTTP status codes

### HTTPS Configuration in sdkconfig
Key settings (already configured):
- `CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y`
- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y`
- `CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y`

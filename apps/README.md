# Esposito Apps

Dynamic apps that run on Esposito OS. Each app is compiled to a self-contained ELF and loaded from the SD card at runtime.

## App API

Apps are linked against a small OS API surface that is exported by the firmware.
The exact symbol list lives in `main/os_symtab.c`, but the main groups are:

### App lifecycle

- `app_init(app_context_t *ctx)` sets subscriptions and initializes the app.
- `app_event(app_context_t *ctx, event_t *event)` receives timer, keyboard, touch, and serial events.
- `app_checkpoint(app_context_t *ctx)` saves state before the app switches away.
- `app_close(app_context_t *ctx)` releases resources and clears the display.

### App switching and startup files

- `os_load_app(name)` launches another app.
- `os_open_app_with_file(name, path)` stores a startup file for the target app and launches it.
- `os_consume_startup_file(out, size)` reads and clears that startup file during app startup.

### Time and NTP

- `os_get_time_status(&status)` returns the current UTC time and whether the clock is trusted.
- `os_time_is_synchronized()` returns whether NTP has synced during this boot.
- `os_time_last_sync()` returns the Unix timestamp of the last successful sync.

### Display and text mode

- `text_mode_init()` / `text_mode_init_ex(font)` select the text grid.
- `text_mode_print_at*()` and `text_mode_printf_at*()` draw text.
- `text_mode_clear()` clears the screen.
- `text_mode_flush()` commits buffered text updates.

### Checkpoint and config

- `checkpoint_save_*()` / `checkpoint_load_*()` store app state in the app checkpoint.
- `config_bind_app(name)` / `config_unbind_app()` select an app config namespace.
- `config_get_*()` and `config_set_*()` read and write app settings.

### Files and system helpers

- Standard C I/O helpers such as `fopen`, `fread`, `fwrite`, `fclose`, `fseek`, and `ftell` are available.
- Directory helpers such as `opendir`, `readdir`, `closedir`, `stat`, and `mkdir` are available.
- `os_log(tag, fmt, ...)` writes to the system log.

### Memory

- `malloc`, `calloc`, `realloc`, and `free` are mapped to the app heap, not the global firmware heap.

If you are writing a new app, start from [app_template/](app_template/) and keep the event loop small: most apps just set up state in `app_init`, react to events in `app_event`, and save state in `app_checkpoint`.

## Apps

### [kilo](kilo/)

A minimal text editor, ported from Salvatore Sanfilippo's classic [kilo](https://github.com/antirez/kilo). Open, edit, and save text files on the SD card. Supports Ctrl+O file picker open, Ctrl+S save, Ctrl+W save as.

![kilo](kilo/kilo.png)

---

### [paint](paint/)

A touch-first pixel paint app. 16-color canvas with pencil, eraser, line, and rectangle tools. Live drag preview, single-step undo, and save/load to SD card.

---

### [reader](reader/)

A markdown ebook reader. Reads `.md` files from `/sdcard/books/`, with paragraph reflow, page navigation, search, and checkpoint resume.

![reader](reader/reader.png)

---

### [snake](snake/)

The classic Snake game. Grow your snake by eating food, don't run into yourself. A good example of a minimal Esposito app.

![snake](snake/snake.png)

---

### [settings](settings/)

System settings: WiFi SSID/password configuration, serial log toggle, and WiFi connection status.

---

### [file_manager](file_manager/)

Two-pane filesystem browser for SD card content (mc-style). Navigate each pane independently and keep pane paths between launches.

Current controls: `W/S` move, `A/D` or `Tab` switch pane, `Enter` open directory, `ESC` up/exit, `R` reload, `K` mkdir, `C` copy to other pane, `O` open-with app.

---

### [file_picker](file_picker/)

Single-pane reusable file picker app. Constrains navigation to a configured root path and filters visible files by a configured glob.

---

### [terminado](terminado/)

A serial terminal emulator. Connects to a host over USB serial and provides a full VT100 terminal. The foundation for the terminal mode subsystem.

---

### [hello_world](hello_world/)

Minimal example app. Prints a message and exits. Good starting point for new apps.

---

### [font_test](font_test/)

Developer utility that renders all available fonts to the display for visual inspection.

---

### [clock](clock/)

Small UTC clock app showing current date/time, Unix timestamp, and whether the OS time has been trusted by NTP during this boot.

---

### [sd_test](sd_test/)

Developer utility that exercises SD card read/write and reports results to the display.

---

## Building Apps

```sh
bash scripts/build_app.sh apps/<name>/app.c
```

The resulting ELF goes to `build/apps/<name>.elf`. Copy it to `/sdcard/apps/<name>/program.elf` on the device.

## Creating a New App

Start from [app_template/](app_template/) — copy the folder, implement `app_init`, `app_event`, and `app_close` in `app.c`.

# Esposito Apps

Dynamic apps that run on Esposito OS. Each app is compiled to a self-contained ELF and loaded from the SD card at runtime.

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

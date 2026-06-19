# Hello World

Minimal example app for Esposito OS, and the recommended starting point for new apps.

## What It Demonstrates

- Text mode initialization and colored printing
- Subscribing to keyboard events
- Persisting state across launches via the config API

Each keypress increments a counter, which is saved on checkpoint and restored on the next launch.

## Controls

- **Any key** increments the counter and shows the key code
- **Ctrl+Esc** returns to the launcher

## Build

```sh
bash scripts/build_app.sh apps/hello_world/app.c
```

Then copy to SD card:

```text
/sdcard/apps/hello_world/program.elf
```

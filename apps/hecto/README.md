# Hecto

A paged text editor for Esposito OS, designed for large files. Navigate by pages, search, and edit text and Markdown.

## Features

- Paged editing with large file support
- Markdown syntax highlighting (headings, bold, emphasis)
- Search, save, and save-as
- File associations: `txt`, `c`, `h`, `md`, `cfg`

## Controls

- **W** / **S**: scroll up / down
- **PgUp** / **PgDn**: page up / down
- **Enter**: edit mode / confirm
- **Esc**: back / exit current mode
- **Ctrl+Esc**: return to the launcher

## Build

```sh
bash scripts/build_app.sh apps/hecto/app.c
```

Then copy to SD card:

```text
/sdcard/apps/hecto/program.elf
```

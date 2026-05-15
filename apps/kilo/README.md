# Kilo

A minimal text editor for Esposito OS, ported from the classic
[kilo](https://github.com/antirez/kilo) editor by Salvatore Sanfilippo.

![Kilo screenshot](kilo.png)

## Features

- Open, edit, and save text files on the SD card
- Syntax highlighting for C/C++, JSON, and Markdown files (`.c`, `.h`, `.cpp`, `.json`, `.md`, `.markdown`)
- Open-file picker and save-as prompt
- Scrolling for files wider or longer than the screen
- Modified-file indicator in the status bar

## Controls

| Key | Action |
| --- | --- |
| Fn + W/A/S/D | Move cursor up/left/down/right |
| Backspace | Delete character |
| Enter | Insert newline |
| Fn + Q | Insert tab (4 spaces) |
| ^S (Ctrl + S) | Save |
| ^N (Ctrl + N) | New file |
| ^O (Ctrl + O) | Open file (prompt for path) |
| ^W (Ctrl + W) | Save As prompt |

Save As still uses a typed path prompt.

## Limitations

- **File size limit: 32 KiB** — Kilo loads entire files into memory. Attempting to open files larger than 32KB will be rejected with an error message. This protects against memory exhaustion on the app heap.
- **No line wrapping** — Long lines scroll horizontally but do not wrap to the next display line.

## Default File

If no file is configured, new content is saved to `/sdcard/apps/kilo/untitled.txt`.
The last opened file is remembered across app restarts via the OS checkpoint system.

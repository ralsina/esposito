# Kilo

A minimal text editor for Esposito OS, ported from the classic
[kilo](https://github.com/antirez/kilo) editor by Salvatore Sanfilippo.

![Kilo screenshot](kilo.png)

## Features

- Open, edit, and save text files on the SD card
- Syntax highlighting for C/C++, JSON, and Markdown files (`.c`, `.h`, `.cpp`, `.json`, `.md`, `.markdown`)
- Open-file and save-as prompts
- Scrolling for files wider or longer than the screen
- Modified-file indicator in the status bar

## Controls

| Key | Action |
|-----|--------|
| Fn + W/A/S/D | Move cursor up/left/down/right |
| Backspace | Delete character |
| Enter | Insert newline |
| Fn + Q | Insert tab (4 spaces) |
| ^S (Ctrl + S) | Save |
| ^N (Ctrl + N) | New file |
| ^O (Ctrl + O) | Open file prompt |
| ^W (Ctrl + W) | Save As prompt |

In the Open / Save As prompt, type a full SD card path (e.g. `/sdcard/notes.txt`) and press Enter to confirm, or Esc to cancel.

## Default File

If no file is configured, new content is saved to `/sdcard/apps/kilo/untitled.txt`.
The last opened file is remembered across app restarts via the OS checkpoint system.

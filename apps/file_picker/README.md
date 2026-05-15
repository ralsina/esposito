# File Picker

Single-pane file picker for Esposito apps.

## Purpose

This app is intended to be launched by other apps (for example `reader` and `kilo`) to pick a file path, then return to the caller app.

## Config Contract

Set these keys in app config scope `file_picker` before launching:

- `root_path`: top directory the picker cannot leave
- `start_path`: initial directory (or file path; file parent is used)
- `glob`: file filter (supports `*`, `?`, and multiple patterns split by `|`, `,`, `;`)
- `title`: window title
- `return_app`: app to launch after select/cancel
- `target_app`: app whose config will receive selected path
- `result_key`: key in `target_app` config where selected path is written
- `cancel_to_launcher`: `1` means cancel at root exits to launcher, `0` returns to `return_app`

Directories are always shown so navigation remains possible; glob filtering applies to files.

## Controls

| Key | Action |
| --- | --- |
| W / S | Move selection |
| Enter | Enter directory or select file |
| ESC | Go to parent directory, or cancel at root |
| R | Reload current directory |

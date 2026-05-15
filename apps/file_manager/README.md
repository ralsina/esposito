# File Manager

A directory browser for `/sdcard`.

## Current Features

- Two-pane (`mc`-style) browser under `/sdcard`
- Independent left/right pane paths
- Enter directory with `Enter` in active pane
- Go to parent directory with `ESC` in active pane
- Reload active pane with `R`
- Quick create directory in active pane with `K` (`newdir`, `newdir_2`, ...)
- Copy selected file to other pane with `C` (auto-unique destination name)
- Open selected file with app association using `O`
- Handles ambiguous extensions (for example `.md`) via one-key choice (`1`/`2`)
- Persist pane paths and active pane between launches

## Controls

| Key | Action |
| --- | --- |
| W / S | Move selection |
| A / D or Tab | Switch active pane |
| Enter | Open selected directory in active pane |
| ESC | Go to parent directory in active pane, or exit at `/sdcard` |
| R | Reload active pane |
| K | Create a new directory in active pane |
| C | Copy selected file to other pane |
| O | Open selected file with associated app |
| 1 / 2 | Resolve app choice when extension has multiple handlers |

## Notes

This implementation currently supports directory creation and file copy. Directory copy/move/delete and picker mode are planned next.

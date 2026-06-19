# Shell

A command-line shell for Esposito OS. Navigate the filesystem, copy and move files, view file contents, and open files in other apps.

## Commands

| Command | Action |
|---------|--------|
| `ls [path]` | List directory contents |
| `cd [path]` | Change directory (`cd` goes to `/sdcard`, `cd ..` goes up) |
| `cp <src> <dst>` | Copy a file |
| `mv <src> <dst>` | Move/rename a file |
| `view <file>` | View a file's contents |
| `<app> <file>` | Open a file in another app (e.g. `reader book.md`) |

## Controls

- Type a command and press **Enter** to run it
- **Ctrl+Esc**: return to the launcher

## Build

```sh
bash scripts/build_app.sh -l ui2 apps/shell/app.c
```

Then copy to SD card:

```text
/sdcard/apps/shell/program.elf
```

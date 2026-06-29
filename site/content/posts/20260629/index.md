---
title: ESP-Osito News for June 29, 2026
date: 2026-06-29 18:00:00 -03:00
---

## Say Hello to BreezyBox: A Real Shell

The old shell app was a proof of concept — it could run a few commands and
that was about it. It's been replaced with **BreezyBox**, a Unix-like shell
that actually *feels* like a shell.

What it does now:

- **20 built-in commands**: `cat`, `cd`, `clear`, `cp`, `date`, `du`,
  `echo`, `free`, `help`, `ls`, `mkdir`, `mv`, `pwd`, `reboot`, `rm`,
  `rmdir`, `sh`, `touch`, `uname`, `wc`
- **I/O redirection and pipes**: `cat foo.txt | wc -l > count.txt` —
  pipes work through a temp file, but the syntax is the real thing
- **Command history**: Fn+W/S to scroll through the last 20 commands
- **Line editing**: arrows, backspace, Ctrl+U (clear line), Ctrl+L (clear screen)
- **`sh` scripts**: write a text file and run it with `sh myscript.sh`
- **Auto-detecting SD card at `/sdcard`** — no mount steps needed

It runs as a pure ELF app with VT100 terminal output, so it works on both
the CYD and Guition. The display name in the launcher is just **"Shell"**,
one tap away.

## BLE Keyboard: Shift Actually Works

When we added BLE keyboard support a few days ago, the HID usage codes
were mapped to lowercase letters — always. If you pressed Shift+A,
BreezyBox saw `a` and ignored the modifier. The root cause was that the
BBQ20 keyboard sends pre-shifted ASCII (`0x41` for `A`), so the rest of
the stack never needed to apply the shift bit.

The fix is in the BLE driver: `send_key_event()` now calls
`apply_shift()`, which maps HID usage + shift modifier to the correct
character — uppercase letters, symbols on the number row (`1` → `!`,
`,` → `<`, etc.), and everything in between. The driver converts BLE
keyboard reports to the same ASCII-keycode events the BBQ20 produces,
so all apps benefit.

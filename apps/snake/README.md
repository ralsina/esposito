# Esposito Snake

A simple Snake game for Esposito OS, built as a single-file dynamic app for ESP32/CYD.

![Snake screenshot](snake.png)

## Gameplay

- Eat food (`*`) to grow and increase score.
- Avoid hitting walls or your own body.
- Keep improving your high score (saved with app config on SD card).

## Controls

- Keyboard:
  - `W/A/S/D` or arrows to move
  - `Enter` or `Space` to restart after game over
- Touch:
  - On-screen directional buttons at the bottom of the screen:
    - `[UP]`
    - `[LEFT] [DOWN] [RIGHT]`

## Difficulty / Speed Ramp

- Starts at an easier base speed.
- Every few foods, movement speed increases.
- Speed is capped to a minimum interval to keep the game playable.
- Current speed is shown in the HUD.

## Exit

Use the normal launcher shortcut (`Ctrl+Esc`) to return to the app launcher.

## Build

From the repository root:

```bash
bash scripts/build_app.sh apps/snake/app.c
```

Then copy the generated ELF to:

```text
/sdcard/apps/snake/program.elf
```

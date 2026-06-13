---
title: ESP-Osito News for May 31, 2026
date: 2026-05-31 12:00:00 -03:00
---

## Peanut-GB: Game Boy Emulation on ESP32

Yes, really. The ESP32 CYD is now a Game Boy. Peanut-GB is a DMG emulator running
as an Esposito app, achieving 40-60 FPS at 240MHz with no PSRAM.

ROMs are loaded from the SD card into flash memory and memory-mapped for fast
access. The 160x144 Game Boy display is centered on the 320x240 screen with black
borders, just like the real thing.

{{% figure src="/posts/20260531/peanut_gb.png" caption="Game Boy emulation on a $5 microcontroller" link="https://github.com/ralsina/esposito/tree/main/apps/peanut_gb" %}}

### Controls

| Action       | Key |
|-------------|-----|
| D-Pad       | W/A/S/D |
| A Button    | L   |
| B Button    | M   |
| Select      | O   |
| Start       | P   |
| Save State  | K   |
| Load State  | J   |
| Exit        | ESC |

ROMs go in `/sdcard/roms/` on the SD card. For legal homebrew Game Boy games, check out [Homebrew Hub](https://hh.gbdev.io/).

## Sprite API and Flash ROM Loading

To make the emulator possible, the firmware gained two new systems:

- **Sprite API**: Paletted sprites for efficient pixel-level rendering. The Game Boy
  display uses a 2bpp sprite (5.7KB on system heap instead of a 23KB RGB565 buffer),
  with one `sprite_write_row` call per scanline.

- **Flash ROM loading**: ROMs are copied from SD card into the app_code flash partition
  and memory-mapped. This gives zero-overhead pointer dereference instead of slow SD
  card I/O during emulation.

## Improved Event Handling

The OS event loop now drains all pending events instead of processing one per
iteration. This eliminates the input lag that was especially noticeable in
fast-paced apps like the emulator. The event queue was also doubled from 32 to
64 slots.

## CPU at 240MHz

The CPU frequency was bumped from 160MHz to 240MHz, which — combined with
compiler optimizations and bank 0 RAM caching — is what makes Game Boy emulation
viable on this hardware.

## Credits

The emulator core is [Peanut-GB](https://github.com/deltabeard/Peanut-GB) by
Mahyar Koshkouei, a remarkably fast single-header C99 Game Boy DMG emulator
library. Licensed under the MIT License.

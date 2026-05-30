---
title: ESP-Osito News for May 30, 2026
date: 2026-05-30 12:00:00 -03:00
---

## Color Palettes!

Settings now has a Palette option that lets you cycle through four color themes:
CGA (classic), CGA Light, Solarized Dark, and Solarized Light. The choice is
saved and applied at boot, so your preferred look persists across restarts.

Also, all button labels are now bold bright white for better readability.

## Portrait Mode Fixes

The paint app and the Arduboy compatibility layer (used by the Numbers game)
were both broken in portrait orientation. The graphics buffer was using the
physical screen dimensions (320x240) instead of the rotated dimensions
(240x320), causing garbled drawing, invisible palettes, and off-center
rendering. All fixed now — everything works correctly in both landscape and
portrait.

## Screenshots of Graphics Apps

Pressing Fn+ESC to take a screenshot now works for graphics-mode apps (paint,
Arduboy games). Previously only text-mode screenshots were captured, and
graphics apps would produce all-black images.

{{% figure src="/paint.png" caption="Paint app screenshot — yes, that's a real screenshot from the device!" link="https://github.com/ralsina/esposito/tree/main/apps/paint" %}}

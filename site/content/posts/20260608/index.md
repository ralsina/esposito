---
title: ESP-Osito News for June 8, 2026
date: 2026-06-08 12:00:00 -03:00
---

## Lua 5.4 Integration

Esposito now embeds Lua 5.4.7 as a scripting engine. The Lua runtime lives in
flash (zero RAM at rest) and uses the app heap via a custom allocator when
activated.

The new **Lua** app provides:

- **Interactive REPL** — type expressions or statements at the `>` prompt,
  see results immediately. Supports the full standard library plus Esposito
  bindings.
- **Script runner** — open `.lua` files from the file manager to execute
  them, then drop into the REPL.
- **`esposito` module** — 25 bindings exposing the text display, keyboard
  input, WiFi, HTTP, system settings, and app launcher to Lua scripts.

![Lua REPL screenshot](../../apps/lua/lua.png)

## Game Boy Emulator Performance

The Game Boy emulator was upgraded from Peanut-GB to **Walnut-CGB**, bringing
full Game Boy Color support and massive performance improvements:

- **Dual-core rendering** — Core 0 handles emulation, Core 1 pushes frames to
  the display in parallel
- **Hardware-accelerated scaling** — 1x, 1.5x, and 2x zoom via sprite
  rotation/scaling hardware
- **Frame skipping** — maintains 60 FPS game speed at ~30-37 FPS rendering
- **Double-buffered sprite system** — smooth, tear-free visuals
- **240 MHz CPU boost** — the emulator automatically overclocks to 240 MHz
  for full-speed play, and the screensaver restores the lower idle frequency

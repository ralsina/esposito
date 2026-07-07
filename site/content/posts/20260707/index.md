---
title: ESP-Osito News for July 7, 2026
date: 2026-07-07 18:00:00 -03:00
---

## Audio Player: MP3 Support and Proper Playback Controls

The WAV player has been renamed to **Audio Player** and gained a few things it
should have had from the start:

**MP3 playback** — The original app only played 16-bit 44100 Hz WAV files. Now
it also handles MP3s via the dr_mp3 decoder. Non-44100 Hz files are resampled
with linear interpolation, and mono sources are upmixed to stereo. The file
browser now shows both `.wav` and `.mp3` files under "Audio Files".

**Pause/resume** — Pressing the play/pause button on the toolbar (or hitting
`P` / space on the keyboard) pauses the current track. Instead of stopping the
audio stream (which would cause a click and lose the DMA buffer), the playback
task writes silence to the I2S output while keeping the file position unchanged.
Resuming picks up exactly where you left off.

**Playback toolbar** — A dedicated toolbar now appears during playback with
volume down, play/pause, volume up, stop, and exit buttons. The icons use
proper Lucide media symbols (▶ and ⏸).

**Volume control** — The +/- keys adjust volume in 10% steps, and the current
level is shown in the info line.

## Semi-Graphical Mode

Until now, all UI2 widgets rendered exclusively in text mode — every button,
list, and progress bar was drawn character by character using the terminal grid.
That worked, but it meant round edges were impossible, colors were limited to
the 16-entry text palette, and everything sat on a fixed character grid.

Widgets can now render at the **pixel level** when the device has PSRAM. The
mode is auto-detected (the Guition with its 8 MB octal PSRAM enables it, the
CYD without PSRAM sticks to text mode) and can be overridden with
`ui2_set_graphical()`.

Here is what changes:

- **Buttons** draw as rounded rectangles with a lighter border, and text is
  centered using `CC_DATUM` for accurate pixel positioning regardless of font
  metrics
- **Lists** render each row with individual background fills, a frame border,
  and a scrollbar with a thumb indicator
- **Progress bars** draw filled/unfilled sections with `display_fill_rect` and
  overlay the percentage text
- **Tab views** render a pixel-level tab strip with dividers and a bottom
  border line
- **Text inputs** and **labels** draw directly at pixel coordinates instead of
  character cells

The pixel drawing layer lives in a new shared library, `ui2_graphical`, and
uses a new `display_draw_text_centered()` function routed through the sprite
buffer so graphical elements are not overwritten during flush. Color conversion
between the text palette and RGB565 is handled by `text_mode_get_palette_color()`.

The whole thing is backwards compatible — every widget keeps its text-mode draw
path, and apps do not need changes to benefit from the graphical rendering on
PSRAM-equipped hardware.

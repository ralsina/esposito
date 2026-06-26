# display_test — isolated LovyanGFX test for the Guition JC4827W543

Strips away the esposito OS, ELF loader, text engine, etc., so we can see
exactly what LovyanGFX + `Panel_NV3041A` (raw, unpatched) does on this board.

Uses the pinout from `GUITION_DISPLAY_CONFIG.md` and the same panel config the
OS uses, then draws on a white background:

- a **green square pixel-by-pixel** via `drawPixel()` (the suspected broken path)
- a **green square** via `fillRect()` (known-good path) for comparison
- a single green + single blue pixel, plus green horizontal/vertical lines
- a red text label

## Build & flash

```bash
. /opt/esp-idf/export.sh
cd display_test
idf.py set-target esp32s3      # only needed once
idf.py -p PORT flash monitor
```

It reuses the LovyanGFX vendored at `../managed_components/LovyanGFX` (populated
by building the main project once), so no extra download.

## How to read it

- `drawPixel` square **black/missing**, `fillRect` square **green**:
  fault is in LovyanGFX's single-quad-write path (independent of the OS).
- Both squares green: the display config is fine; the problem is in how the OS
  draws.
- Whole screen colours inverted / wrong: panel config (`invert`, `rgb_order`).

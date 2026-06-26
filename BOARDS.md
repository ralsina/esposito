# Supported Boards

Esposito selects its board at build time. The board directory (under
`boards/<name>/`) provides `board.h` (pin/peripheral map) and `display_config.h`
(the LovyanGFX panel setup). All board differences are compile-time conditional,
so adding a board never changes another board's binary.

## Board selection

The board is chosen in `main/CMakeLists.txt`, in this order of precedence:

1. **Explicit override** — pass `-DBOARD=<name>` to the configure step.
2. **Inferred from `IDF_TARGET`** (the default):
   - `esp32c3` → `elecrow_round`
   - `esp32s3` → `guition_jc4827w543`
   - `esp32` (and anything else) → `cyd_2usb`

The selected board is printed during configuration, e.g.
`Esposito: target=esp32 board=cyd_2usb`.

## Supported boards

| Board | Target | Directory | Display |
|---|---|---|---|
| CYD 2USB (Cheap Yellow Display) | esp32 | `boards/cyd_2usb` | 320×240 ST7789 (SPI) |
| Guition JC4827W543 | esp32s3 | `boards/guition_jc4827w543` | 480×272 NV3041A (QSPI) |

## Building for the CYD (default)

```bash
. /opt/esp-idf/export.sh
idf.py set-target esp32
idf.py build
```

## Building for the Guition JC4827W543

```bash
. /opt/esp-idf/export.sh
idf.py set-target esp32s3     # picks up sdkconfig.defaults.esp32s3 (OPI PSRAM)
idf.py build
```

To force a specific board regardless of target, use the override:

```bash
idf.py -DBOARD=guition_jc4827w543 build
```

The app builder and the OTA stub follow the target automatically:
- `scripts/build_app.sh` reads `CONFIG_IDF_TARGET` from `sdkconfig` and picks the
  matching toolchain (`xtensa-esp32-elf` / `xtensa-esp32s3-elf` / `riscv32-esp-elf`),
  chip include paths, and board directory. Override with `IDF_TARGET=` / `BOARD=`.
- `make stub` (via `stub/Makefile`) syncs the stub's IDF target to the main
  project's target and recovers from a stale `stub/build/` cache. The stub's SD
  pins come from the board's `board.h`, so it mounts the right SD card per board.

## Guition JC4827W543 notes

- **MCU**: ESP32-S3-WROOM-1-N4R8 (4 MB flash, 8 MB OPI PSRAM).
- **Display**: 4.3" 480×272 TFT, NV3041A driver over QSPI. Pins:
  CS=45, SCK=47, D0=21, D1=48, D2=40, D3=39. Backlight on GPIO 1.
  The LovyanGFX `Bus_SPI` enters Quad-SPI mode automatically when all four
  `pin_io0..3` are configured.
- **SD card**: microSD slot in SPI mode, on SPI2_HOST (kept separate from the
  display's SPI3 QSPI bus). Pins: MOSI=11, MISO=13, CLK=12, CS=10.
- **Touch**: GT911 capacitive touch over I2C (SDA=8, SCL=4, INT=3, RST=38),
  driven via LovyanGFX's `Touch_GT911`. `touchscreen.c` polls it through a
  `display_get_touch()` wrapper in `hardware.cpp`. Orientation is set by
  `BOARD_TOUCH_OFFSET_ROTATION` in `board.h` (0 by default; adjust 0–7 if
  touches land mirrored or axis-swapped).
- **Not used**: BBQ20 keyboard, RGB LED.

Pinout source: the `PINS_JC4827W543.h` from moononournation's "Dev Device Pins"
library (cross-checked against `thelastoutpostworkshop/JC4827W543_avi_player`).

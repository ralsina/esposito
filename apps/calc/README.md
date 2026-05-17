# Calculator App

A simple 4-function calculator with floating point support for the Esposito OS.

## Features

- **Basic Operations**: Addition, subtraction, multiplication, division
- **Floating Point**: Uses hardware-accelerated float operations via ESP32 FPU
- **Touch Interface**: Large, touch-friendly buttons optimized for the ESP32 CYD touchscreen
- **Keyboard Support**: Full keyboard input including BBQ20 keyboard support
- **Display**: Large, bright green display using scaled text rendering
- **Memory Functions**: Clear, sign toggle (+/-), and percentage operations

## Calculator Layout

```
┌─────────────────────────────────────┐
│ Calculator                          │
│                                     │
│         [ Large Display Area ]       │
│                                     │
│  ┌───┐┌───┐┌───┐┌───┐              │
│  │ C ││+/-││ % ││ / │              │
│  └───┘└───┘└───┘└───┘              │
│  ┌───┐┌───┐┌───┐┌───┐              │
│  │ 7 ││ 8 ││ 9 ││ * │              │
│  └───┘└───┘└───┘└───┘              │
│  ┌───┐┌───┐┌───┐┌───┐              │
│  │ 4 ││ 5 ││ 6 ││ - │              │
│  └───┘└───┘└───┘└───┘              │
│  ┌───┐┌───┐┌───┐┌───┐              │
│  │ 1 ││ 2 ││ 3 ││ + │              │
│  └───┘└───┘└───┘└───┘              │
│  ┌───┐┌───┐┌───────────┐           │
│  │ 0 ││ . ││     =     │           │
│  └───┘└───┘└───────────┘           │
└─────────────────────────────────────┘
```

## Technical Details

- **Font Size**: 3x scaled text for the display
- **Color**: Bright green (RGB565: 0x07E0) on black background
- **Precision**: Single-precision float (32-bit) for hardware FPU acceleration
- **Display Format**: %.8g format for optimal precision/readability balance
- **Button Width**: Automatically calculated based on screen size, centered layout
- **Touch Coordinates**: Properly converts pixel coordinates to character coordinates using font metrics

## Keyboard Shortcuts

- **Digits 0-9**: Number entry
- **Operators**: `+`, `-`, `*`, `/`
- **Decimal Point**: `.`
- **Equals**: `=` or `Enter`/`Return`
- **Clear**: `C`, `c`, or `ESC`
- **Sign Toggle**: Works through button only
- **Percentage**: Works through button only

## Dependencies

- `ui` - UI library for button widgets

## Floating Point Implementation

This calculator demonstrates Esposito OS's floating point support:

1. **Hardware FPU**: Uses ESP32 single-precision FPU for fast float operations
2. **OS Symbol Table**: Math functions exported from firmware to apps
3. **No Double Math**: Avoids slow software double-precision emulation
4. **Direct Display**: Uses lovyanGFX scaled text rendering for large numbers

## Files

- `app.c` - Main calculator application
- `manifest.cfg` - App metadata and configuration
- `deps` - UI library dependency
- `README.md` - This file

## Building

```bash
# Build individual app
bash scripts/build_app.sh -l ui apps/calc/app.c

# Or build all apps
make test
```

## Usage Notes

- The calculator uses standard order of operations (left-to-right for same precedence)
- Division by zero displays "Error"
- Large numbers are displayed in scientific notation when needed
- The display area is automatically cleared when starting new number entry
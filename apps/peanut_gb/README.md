# GB Emulator for Esposito

A Game Boy (DMG) and Game Boy Color (GBC) emulator for the ESP32 Cheap Yellow Display, running as an Esposito OS app.

![GB Emulator running on ESP32 CYD](peanut_gb.png)

## About

This app uses [Walnut-CGB](https://github.com/Koutoku/Walnut-CGB) — a high-performance fork of Peanut-GB with full Game Boy Color support. It runs at ~30-37 FPS with frame skipping enabled to maintain correct game speed.

**Key features:**
- Full Game Boy and Game Boy Color support
- Dual-core rendering (Core 0: emulation, Core 1: display)
- Hardware-accelerated scaling with LovyanGFX
- Frame skipping for correct game timing
- ROMs loaded from SD card and memory-mapped
- Double-buffered rendering for smooth visuals

## Performance

- **Rendering**: 30-37 FPS (hardware limited)
- **Game speed**: 60 FPS with frame skipping enabled
- **Scaling**: Automatic 1.5x hardware scaling for 320×240 displays
- **Memory**: ~11KB for double-buffered sprites (fits in 96KB app heap)

## Controls

| Action       | Key |
|-------------|-----|
| D-Pad Up    | W   |
| D-Pad Down  | S   |
| D-Pad Left  | A   |
| D-Pad Right | D   |
| A Button    | L   |
| B Button    | M   |
| Select      | O   |
| Start       | P   |
| Save State  | K   |
| Load State  | J   |
| Exit        | ESC |

## ROMs

Place `.gb` and `.gbc` files in `/sdcard/roms/` on the SD card. ROMs can also be opened via the file manager.

**Supports:**
- Game Boy (DMG) monochrome games
- Game Boy Color (GBC) games with full color support
- Both file extensions: `.gb` and `.gbc`

For legal, homebrew Game Boy ROMs, visit the [Homebrew Hub](https://hh.gbdev.io/).

## Saves

- **State**: Save/load with K/J keys, stored as `<rom>.state` alongside the ROM
- **SRAM**: Currently disabled (will be implemented with SD card storage)

## Credits

- Emulator core: [Walnut-CGB](https://github.com/Koutoku/Walnut-CGB) (based on Peanut-GB by Mahyar Koshkouei)
- Some code from [SameBoy](https://github.com/LIJI32/SameBoy/) by Lior Halphon
- Esposito OS app integration by Roberto Alsina

## License

MIT License — see [LICENSE](LICENSE).

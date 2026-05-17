# Esposito OS Wokwi Emulator

This directory contains the Wokwi configuration used to run Esposito OS in an emulator.

## Current Baseline

- **Base Board**: ESP32-2432S028R (Cheap Yellow Display with 2USB variant)
- **Firmware Input**: `../build/esposito.elf` from `.wokwi.build.toml`
- **Input**: Touchscreen only in this baseline
- **Keyboard Emulation**: Not enabled yet (custom BBQ20 part not included)

## Files

- `diagram.json`: Wokwi board layout
- `.wokwi.build.toml`: firmware path and project metadata
- `wokwi-demo.sh`: helper script to build firmware before emulator use

## Running in Wokwi

### Option 1: Using Wokwi CLI

```bash
npm install -g wokwi-cli
cd wokwi
wokwi-server
```

Then open the local URL printed by the server.

### Option 2: Using Wokwi Web IDE

1. Open [wokwi.com/projects/new](https://wokwi.com/projects/new)
1. Copy `diagram.json` contents to the editor
1. Build firmware locally (`idf.py build`)
1. Upload `build/esposito.elf`

## Build Firmware

From repository root:

```bash
idf.py build
```

Expected output artifact:

```text
build/esposito.elf
```

## Validation Checklist

Use this checklist each time emulator support is touched.

1. Boot test: launcher appears within 10 seconds.
2. Navigation test: touch can open an app from launcher.
3. Settings test: open Settings and switch default font.
4. Relayout test: after font switch, Settings remains usable.
5. Networking smoke test: open Clock and trigger weather refresh.
6. Stability test: launch and exit 5 apps without crash/restart.

## Known Limitations

- ✅ ESP32 microcontroller
- ✅ 320×240 touchscreen display
- ✅ Basic UI and app navigation via touch
- ⚠️ SD card simulation (limited)
- ✅ WiFi networking simulation
- ❌ BBQ20 keyboard emulation (custom part not yet implemented in repo)

## Next Steps

1. Add custom Wokwi part files for BBQ20 keyboard emulation.
2. Reconnect keyboard I2C lines in `diagram.json`.
3. Add keyboard-specific emulator tests (text input and app hotkeys).

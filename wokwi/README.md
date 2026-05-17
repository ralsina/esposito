# Esposito OS Wokwi Emulator

This directory contains the Wokwi emulator configuration for Esposito OS on the ESP32 Cheap Yellow Display (CYD) hardware.

## Hardware Configuration

- **Base Board**: ESP32-2432S028R (Cheap Yellow Display with 2USB variant)
- **Display**: 320×240 touchscreen TFT LCD
- **Keyboard**: BBQ20 Keyboard (I2C address 0x20)
- **Storage**: SD card for app storage
- **Connectivity**: WiFi (ESP32 built-in)

## Wokwi Parts

This configuration uses:
- `wokwi-esp32-2432s028r` - Built-in ESP32 CYD board support
- `wokwi-bbq20keyboard` - Custom BBQ20 keyboard part

## Running in Wokwi

### Option 1: Using Wokwi CLI
```bash
npm install -g wokwi-cli
cd wokwi
wokwi-server
```

### Option 2: Using Wokwi Web IDE
1. Open https://wokwi.com/projects/new
2. Copy `diagram.json` contents to the editor
3. Upload Esposito firmware ELF

## Firmware Loading

The Wokwi emulator can run the Esposito firmware:
1. Build the firmware: `idf.py build`
2. Locate the ELF: `build/esposito.elf`
3. Load in Wokwi via the firmware upload feature

## Differences from Real Hardware

The Wokwi emulator closely matches the real Esposito hardware:
- ✅ ESP32 microcontroller
- ✅ 320×240 touchscreen display
- ✅ I2C keyboard (BBQ20)
- ⚠️ SD card simulation (limited)
- ✅ WiFi networking simulation
- ✅ Touch input simulation

## Keyboard Layout

The BBQ20 keyboard uses I2C communication:
- **SDA**: GPIO 18
- **SCL**: GPIO 19
- **Address**: 0x20

## Demo Mode

For demonstration purposes, the emulator can run without physical hardware:
1. Load the Esposito firmware
2. The app launcher will start automatically
3. Use keyboard or touch to interact with apps
4. Try different fonts via Settings app to see responsive layouts

## Custom Parts

The BBQ20 keyboard requires a custom Wokwi part definition if not available in the standard library. The connection is simulated via I2C protocol.

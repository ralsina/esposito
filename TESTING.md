# Esposito OS - Testing Guide

## ✅ Current State: All Hardware + Dynamic Loading Working

All core hardware drivers (display, keyboard, touch, SD card) are implemented and working.
Apps are dynamically loaded from SD card as standalone ELF binaries.

## 🚀 Quick Start Commands

```bash
# Set up ESP-IDF environment (run once per terminal session)
. ~/esp/esp-idf/export.sh  # Adjust path to your ESP-IDF installation

# Build the project
idf.py build

# Flash to device
idf.py flash

# Monitor serial output
idf.py monitor

# Flash and monitor in one command
idf.py flash monitor
```

## 📋 Expected Boot Sequence

When you flash the device, you should see this serial output:

```
I (xxx) esposito: Esposito OS v0.1.0-alpha
I (xxx) esposito: Starting boot sequence...

I (xxx) boot: ╔═══════════════════════════════════════╗
I (xxx) boot: ║     Esposito OS Boot Sequence        ║
I (xxx) boot: ║     ESP32 CYD (2USB version)         ║
I (xxx) boot: ╚═══════════════════════════════════════╝

I (xxx) boot: ✓ Display Init
I (xxx) boot:   Display ready
I (xxx) boot: ✓ Hardware Init
I (xxx) boot:   Hardware ready
I (xxx) boot: ✓ Filesystem Init
I (xxx) boot:   Filesystem ready
I (xxx) boot: ✓ Keyboard Init
I (xxx) boot:   Keyboard subsystem ready
I (xxx) boot: ✓ App Loader Init
I (xxx) boot:   App loader ready
I (xxx) boot: ✓ Load Default App

I (xxx) boot: ╔═══════════════════════════════════════╗
I (xxx) boot: ║     BOOT SEQUENCE COMPLETE            ║
I (xxx) boot: ╚═══════════════════════════════════════╝

I (xxx) boot: ✓ System ready
I (xxx) boot: Available apps: 2  (or however many are on SD)
I (xxx) app_loader: Found 2 app(s) on SD card
I (xxx) esposito: Starting main event loop...
```

## 🧪 Testing Tools

### 1. Structure Test
```bash
./test_structure.sh
```
Verifies all required files are present.

### 2. Boot Log Analysis
```bash
./analyze_boot_log.sh
```
Shows expected boot output and common issues.

### 3. Serial Monitor
```bash
idf.py monitor
```
Watch real-time boot sequence and events.

## 🐛 Troubleshooting

### Build Errors
- **ESP-IDF not found**: Make sure you sourced the export.sh script
- **Missing components**: Run `idf.py fullclean` then `idf.py build`

### App ELF Not on SD Card
- If the launcher shows "No apps available!", the SD card may not have apps copied yet
- Run `make test` or manually copy `build/apps/*/*.elf` to the SD card

### Boot Failures
- **Display not available**: Check LovyanGFX configuration and SPI pins
- **Keyboard not detected**: Normal if BBQ20 not connected - non-fatal
- **Filesystem failed**: Check SPIFFS partition in sdkconfig
- **SD card not detected**: Check SD card insertion and formatting (FAT32)

### Known Limitations
- Checkpoint serialization still stubbed
- App launcher doesn't refresh if SD card is hot-swapped

## ✅ What Should Work Now

1. **Build successfully** with ESP-IDF
2. **Boot sequence** with detailed logging
3. **Full hardware support** (display, keyboard, touch, SD card, filesystem)
4. **Touch input** with coordinate mapping
5. **Keyboard input** with modifier keys (Fn, Ctrl, Alt)
6. **SD card** mounted at `/sdcard` with file operations
7. **Text mode** display subsystem with colors and attributes
8. **Dynamic app loading**: ELF apps loaded from SD card
9. **App launcher** (Ctrl+ESC) with keyboard navigation
10. **Graceful degradation** (continues without keyboard/display)
11. **Serial monitoring** for debugging

## 🔧 Next Development Steps

1. Create more apps
2. Implement checkpoint serialization

## 📝 Boot Sequence Stages

| Stage | Description | Current Status |
|-------|-------------|----------------|
| Power On | System start | ✅ Working |
| Display Init | ST7789 via LovyanGFX SPI | ✅ Working |
| Hardware Init | GPIO/I2C/SPI drivers | ✅ Working |
| Filesystem Init | SPIFFS mount | ✅ Working |
| Keyboard Init | BBQ20 I2C detection | ✅ Working |
| SD Card Init | SDSPI on VSPI bus | ✅ Working |
| Touchscreen Init | XPT2046 GPIO bitbang | ✅ Working |
| App Loader Init | SD card app scanning | ✅ Working |
| Load Default App | key_echo from SD card | ✅ Working |
| Complete | Enter event loop | ✅ Working |

## 🎯 Success Criteria

Current test passes if:
- [x] Project builds without errors
- [x] Device boots and shows serial output
- [x] Boot sequence completes with ✓ marks
- [x] System enters main event loop
- [x] Display shows boot screen
- [x] Keyboard generates events
- [x] Touch generates events
- [x] SD card mounts and reads/writes files
- [x] Dynamic app loading from SD card

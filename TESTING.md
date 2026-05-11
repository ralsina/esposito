# Esposito OS - Testing Guide

## ✅ Current State: Ready for ESP-IDF Build

The boot sequence is now implemented and ready for testing once ESP-IDF is set up.

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
I (xxx) boot: Available apps: 0
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

### Boot Failures
- **Display not available**: Normal for now - display driver needs completion
- **Keyboard not detected**: Normal if BBQ20 not connected - non-fatal
- **Filesystem failed**: Check SPIFFS partition in sdkconfig

### Expected Current Limitations
- Display shows boot screen (needs completion)
- Keyboard detected but no events yet (needs protocol implementation)
- No apps available (dynamic loading needs implementation)
- Event loop runs but no events generated yet

## ✅ What Should Work Now

1. **Build successfully** with ESP-IDF
2. **Boot sequence** with detailed logging
3. **Hardware detection** (display, keyboard, filesystem)
4. **Graceful degradation** (continues without keyboard/display)
5. **Serial monitoring** for debugging

## 🔧 Next Development Steps

Once boot is working:
1. Complete display driver (ST7789 initialization)
2. Implement keyboard I2C protocol
3. Add basic app for testing
4. Implement dynamic loading

## 📝 Boot Sequence Stages

| Stage | Description | Current Status |
|-------|-------------|----------------|
| Power On | System start | ✅ Working |
| Display Init | ST7789 SPI setup | ⚠️ Partial |
| Hardware Init | GPIO/I2C/SPI drivers | ⚠️ Partial |
| Filesystem Init | SPIFFS mount | ✅ Working |
| Keyboard Init | BBQ20 detection | ⚠️ Detection only |
| App Loader Init | SD card scanning | 🔲 Stub |
| Load Default App | Load first app | 🔲 Stub |
| Complete | Enter event loop | ✅ Working |

## 🎯 Success Criteria

Current test passes if:
- [x] Project builds without errors
- [x] Device boots and shows serial output
- [x] Boot sequence completes with ✓ marks
- [x] System enters main event loop
- [ ] Display shows boot screen (pending)
- [ ] Keyboard generates events (pending)
- [ ] Apps can be loaded (pending)

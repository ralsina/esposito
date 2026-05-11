# 🚀 Flashing Esposito OS to CYD Device

## Prerequisites

1. **Connect the ESP32 CYD device** via USB
2. **Verify connection**: `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`
3. **Install permissions** (if needed): User needs dialout group access

## Flash Commands

### Automatic Port Detection
```bash
idf.py flash monitor
```

### Manual Port Specification
```bash
# For USB serial (common)
idf.py -p /dev/ttyUSB0 flash monitor

# For ACM serial (some devices)
idf.py -p /dev/ttyACM0 flash monitor
```

### Flash Only (Without Monitor)
```bash
idf.py flash
```

### Monitor Only (Device Already Flashed)
```bash
idf.py monitor
```

## What to Expect

### Successful Flash Output
```
Connecting........_
Chip type:         ESP32
Chip revision:     v1.0
Features:          WiFi, BLE, 32MB PSRAM
Uploading stub...
Running stub...
Configuring flash size...
Flash will be erased from 0x00001000 to 0x00007fff...
Flash will be erased from 0x00008000 to 0x00008fff...
Flash will be erased from 0x0000c000 to 0x0003ffff...
Flash will be erased from 0x00040000 to 0x0007ffff...
Writing at 0x00001000... (100%)
Writing at 0x00008000... (100%)
Writing at 0x0000c000... (100%)
Writing at 0x00040000... (100%)
```

### Expected Boot Sequence
```
I (xxx) esposito: Esposito OS v0.1.0-alpha
I (xxx) esposito: Starting boot sequence...

I (xxx) boot: ╔═══════════════════════════════════════╗
I (xxx) boot: ║     Esposito OS Boot Sequence        ║
I (xxx) boot: ║     ESP32 CYD (2USB version)         ║
I (xxx) boot: ╚═══════════════════════════════════════╝

I (xxx) boot: ✓ Display Init
I (xxx) boot:   Display ready (or warnings about incomplete driver)
I (xxx) boot: ✓ Hardware Init
I (xxx) boot:   Hardware ready
I (xxx) boot: ✓ Filesystem Init
I (xxx) boot:   Filesystem ready
I (xxx) boot: ✓ Keyboard Init
I (xxx) boot:   Keyboard subsystem ready
I (xxx) boot: ✓ App Loader Init
I (xxx) boot:   App loader ready
I (xxx) boot: ✓ Load Default App
I (xxx) boot: Available apps: 0

I (xxx) boot: ╔═══════════════════════════════════════╗
I (xxx) boot: ║     BOOT SEQUENCE COMPLETE            ║
I (xxx) boot: ╚═══════════════════════════════════════╝
I (xxx) boot: ✓ System ready
I (xxx) esposito: Starting main event loop...
```

## Troubleshooting

### "No serial port found"
- **Check connection**: USB cable plugged in?
- **Check permissions**: `sudo usermod -a -G dialout $USER`
- **Try manual port**: `idf.py -p /dev/ttyUSB0 flash`

### "Permission denied"
- **Add user to dialout group**: `sudo usermod -a -G dialout $USER`
- **Log out and back in** for group change to take effect

### "Failed to connect"
- **Press BOOT button**: Some CYD devices need manual boot mode
- **Try different USB cable**: Some cables are power-only
- **Check device manager** (Windows) or **lsusb** (Linux)

### "Connection timeout"
- **Wrong port**: Verify correct serial port
- **Device in use**: Close other serial monitor applications
- **Driver issues**: Install CP210x or CH340 drivers

## Monitor Controls

Once in monitor mode:
- **Ctrl+]**: Exit monitor
- **Ctrl+T, Ctrl+H**: Help menu
- **Reset device**: ESP32 restart and shows boot sequence again

## First Boot Checklist

When you first flash Esposito OS, you should see:

✅ **Clear boot sequence** with stage-by-stage progress
✅ **Hardware detection** messages
✅ **Filesystem mount** success
✅ **Entry to main event loop**
✅ **No crashes or Guru Meditation errors**

⚠️ **Expected warnings** (normal for now):
- Display initialization warnings (driver incomplete)
- Keyboard not detected (if BBQ20 not connected)
- No apps available (dynamic loading not implemented)

## Next Steps After Successful Boot

1. **Verify hardware**: What components are detected?
2. **Test display**: Any output on screen (even if just garbage)?
3. **Test keyboard**: Press keys - any serial output?
4. **Plan improvements**: What to implement first?

## Success Criteria

Boot is successful if:
- [x] Device flashes without errors
- [x] Boot sequence appears in serial monitor
- [x] System reaches "Starting main event loop"
- [x] No watchdog resets or crashes
- [x] Responds to reset button

Ready to flash when you are! 🔥

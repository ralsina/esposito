# Installation Guide

This document explains how to get Esposito OS running on an ESP32 CYD (Cheap Yellow Display) with apps.

## Prerequisites

### Hardware

- **ESP32 CYD (2USB version)** — specifically the ESP32-2432S028R board
- **MicroSD card** (formatted as FAT32)
- **USB cable** (data-capable) for flashing
- Optional: BBQ20 keyboard (I2C)

### Software

- **ESP-IDF v6.0.1** — the Espressif IoT Development Framework
- Build tools: Python 3, Git, CMake, Ninja

## Step 1: Install ESP-IDF

Follow the [official ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32/get-started/) for your OS.

On Linux:

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
```

After installation, source the environment in every terminal session:

```bash
. ~/esp/esp-idf/export.sh
```

Or adjust the path to wherever you installed ESP-IDF (e.g., `/opt/esp-idf/export.sh`).

## Step 2: Clone and Build the Firmware

```bash
git clone <repo-url> esposito
cd esposito
. /path/to/esp-idf/export.sh
idf.py build
```

The output firmware binary will be at `build/esposito.bin`.

## Step 3: Flash the Firmware

Connect the CYD via USB and flash.

### With OTA update support (recommended)

Use the Makefile to build and flash the firmware + OTA update stub together:

```bash
make flash
```

This flashes the bootloader, partition table, firmware, OTA data, and update stub. The serial port is hardcoded to `/dev/ttyUSB0` in the Makefile — edit it or change the device path if needed.

### Without OTA support

If you don't need OTA updates, skip the stub:

```bash
idf.py -p /dev/ttyUSB0 flash
```

The serial port may be `/dev/ttyUSB0`, `/dev/ttyACM0`, or similar depending on your system. Run `ls /dev/ttyUSB* /dev/ttyACM*` to find it.

To build and flash just the OTA update stub later (without re-flashing the whole firmware):

```bash
make flash-stub
```

If you get a permission error, add your user to the `dialout` group:

```bash
sudo usermod -a -G dialout $USER
# Log out and back in for the change to take effect
```

## Step 4: Prepare the SD Card

The SD card must be formatted as FAT32. Create the following directory structure:

```
/sdcard/apps/<app_name>/program.elf
/sdcard/apps/<app_name>/manifest.cfg   (optional)
```

### Building All Apps

From the project root, after sourcing ESP-IDF:

```bash
# Generate OS symbol table
scripts/gen_symtab.sh build/esposito.elf build/os_symbols.ld

# Build each app
for app_dir in apps/*/; do
    app_name=$(basename "$app_dir")
    if [ -f "${app_dir}app.c" ]; then
        scripts/build_app.sh "${app_dir}app.c" build/apps
    fi
done
```

This produces `.elf` files in `build/apps/`.

### Copying Apps to the SD Card

```bash
SD_MOUNT=/path/to/sd_card_mount

for app_elf in build/apps/*.elf; do
    app_name=$(basename "$app_elf" .elf)
    mkdir -p "$SD_MOUNT/apps/${app_name}"
    cp "$app_elf" "$SD_MOUNT/apps/${app_name}/program.elf"
    manifest="apps/${app_name}/manifest.cfg"
    if [ -f "$manifest" ]; then
        cp "$manifest" "$SD_MOUNT/apps/${app_name}/manifest.cfg"
    fi
done
```

### Copying Font Packs

```bash
mkdir -p "$SD_MOUNT/fonts/fpack"
cp fonts/*.fpack "$SD_MOUNT/fonts/fpack/"
```

## Step 5: One-Command Build + Flash + SD Card Setup

The project includes a script that does everything at once:

```bash
SD_MOUNT=/path/to/sd_card bash scripts/build_test.sh
```

This builds the firmware, generates the symbol table, builds all apps, copies them to the SD card, prompts you to insert the SD card into the device, then flashes the firmware.

The default SD card mount point is `/run/media/ralsina/ESPRESSIF`; set `SD_MOUNT` to your actual mount point.

## Step 6: First Boot

1. Insert the SD card into the CYD.
2. Connect the device via USB (or power it from any USB power source).
3. The launcher should appear on the display, showing all installed apps.
4. Tap an app icon to launch it.

If the display stays blank, connect a serial monitor to check boot messages:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Press `Ctrl+]` to exit the monitor.

## Troubleshooting

### No apps appear on the launcher

- Check that the SD card is properly inserted.
- Verify the SD card is formatted as FAT32.
- Confirm apps are in `/sdcard/apps/<name>/program.elf`.
- Check serial boot logs for "App loader" messages.

### "Permission denied" when flashing

Add your user to the `dialout` group and re-login:

```bash
sudo usermod -a -G dialout $USER
```

### "Failed to connect" when flashing

Some CYD boards need to be put into download mode manually. Press and hold the **BOOT** button (GPIO0), tap **EN** (reset), then release BOOT.

### Keyboard not detected

The BBQ20 keyboard is optional. The system works fine without it, but some apps may require keyboard input.

### Display shows garbage or is blank

Ensure the board is the **2USB version** of the CYD (ESP32-2432S028R). Earlier single-USB versions use different pins for the display.

## Updating

### OTA Updates

Use the Settings app's System menu to check for and apply firmware updates over WiFi.

### SD Card Updates

Place a firmware binary at `/sdcard/system/firmware.bin` and reboot. The bootloader will apply it automatically.

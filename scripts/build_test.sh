#!/bin/bash
# Build firmware + test app, copy to SD card, flash
# Usage: build_test.sh [app_name] [app_source]

set -e

APP_NAME="${1:-example_app}"
APP_SRC="${2:-apps/$APP_NAME/app.c}"
SD_MOUNT="${SD_MOUNT:-/run/media/ralsina/ESPRESSIF}"
BOOT_CPP="main/boot.cpp"
FIRMWARE_ELF="build/esposito.elf"
BOOT_CPP_BAK="build/boot.cpp.bak"

if [ ! -d "$SD_MOUNT" ]; then
    echo "SD card mount point $SD_MOUNT not found."
    echo "Insert SD card and check the mount point."
    echo "Override with: SD_MOUNT=/path ./scripts/build_test.sh"
    exit 1
fi

if [ ! -f "$APP_SRC" ]; then
    echo "Source not found: $APP_SRC"
    echo "Usage: $0 [app_name] [app_source]"
    echo "  app_name  - directory under apps/ (default: example_app)"
    echo "  app_source - path to app.c (default: apps/<app_name>/app.c)"
    exit 1
fi

# --- Step 1: Back up and patch boot.cpp ---
echo "=== Patching boot.cpp to load '$APP_NAME' ==="
cp "$BOOT_CPP" "$BOOT_CPP_BAK"
sed -i "s/os_load_app(\"[^\"]*\")/os_load_app(\"$APP_NAME\")/" "$BOOT_CPP"

# --- Step 2: Build firmware ---
echo "=== Building firmware ==="
idf.py build

# --- Step 3: Generate OS symbol table ---
echo "=== Generating OS symbol table ==="
scripts/gen_symtab.sh "$FIRMWARE_ELF" build/os_symbols.ld

# --- Step 4: Build app ELF (output named after directory) ---
echo "=== Building app ELF ==="
scripts/build_app.sh "$APP_SRC"

# --- Step 5: Copy to SD card ---
echo "=== Copying to SD card ==="
APP_ELF="build/apps/${APP_NAME}.elf"
mkdir -p "$SD_MOUNT/apps/${APP_NAME}"
cp "$APP_ELF" "$SD_MOUNT/apps/${APP_NAME}/program.elf"
sync
echo "Copied $APP_ELF -> $SD_MOUNT/apps/${APP_NAME}/program.elf"

# --- Step 6: Restore boot.cpp ---
echo "=== Restoring boot.cpp ==="
cp "$BOOT_CPP_BAK" "$BOOT_CPP"
rm -f "$BOOT_CPP_BAK"

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║  SD card ready at: $SD_MOUNT  ║"
echo "║                                                  ║"
echo "║  Remove the SD card, insert it into the device.  ║"
echo "║  Press ENTER when ready to flash the firmware.   ║"
echo "╚══════════════════════════════════════════════════╝"
read -r

# --- Step 7: Flash ---
echo "=== Flashing firmware ==="
idf.py flash

echo ""
echo "Done! The device will boot and load '$APP_NAME' from SD card."

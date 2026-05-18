#!/bin/bash
# Build firmware + all app ELFs, copy to SD card, flash
# Usage: build_test.sh

set -e

SD_MOUNT="${SD_MOUNT:-/run/media/ralsina/ESPRESSIF}"

if [ ! -d "$SD_MOUNT" ]; then
    echo "SD card mount point $SD_MOUNT not found."
    echo "Insert SD card and check the mount point."
    echo "Override with: SD_MOUNT=/path ./scripts/build_test.sh"
    exit 1
fi

# Step 1: Build firmware
echo "=== Building firmware ==="
. /opt/esp-idf/export.sh
idf.py build

# Step 2: Generate OS symbol table
echo "=== Generating OS symbol table ==="
scripts/gen_symtab.sh build/esposito.elf build/os_symbols.ld

# Step 3: Build all app ELFs
echo "=== Building app ELFs ==="
mkdir -p build/apps
for app_dir in apps/*/; do
    app_name=$(basename "$app_dir")
    # Look for either app.c or app.cpp
    app_src="${app_dir}app.c"
    if [ ! -f "$app_src" ]; then
        app_src="${app_dir}app.cpp"
    fi
    if [ -f "$app_src" ]; then
        DEPS=""
        if [ -f "${app_dir}deps" ]; then
            DEPS=$(while read -r lib; do echo -n "-l $lib "; done < "${app_dir}deps")
        fi
        echo "  Building $app_name... $DEPS"
        # shellcheck disable=SC2086
        scripts/build_app.sh $DEPS "$app_src" build/apps
    fi
done

# Step 4: Copy all apps to SD card
echo "=== Copying apps to SD card ==="
for app_elf in build/apps/*.elf; do
    app_name=$(basename "$app_elf" .elf)
    mkdir -p "$SD_MOUNT/apps/${app_name}"
    cp "$app_elf" "$SD_MOUNT/apps/${app_name}/program.elf"
    manifest="apps/${app_name}/manifest.cfg"
    if [ -f "$manifest" ]; then
        cp "$manifest" "$SD_MOUNT/apps/${app_name}/manifest.cfg"
        echo "  Copied $app_name (with manifest)"
    else
        echo "  Copied $app_name"
    fi
done
sync

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║  Apps copied to SD card at: $SD_MOUNT  ║"
echo "║                                                  ║"
echo "║  Remove the SD card, insert it into the device.  ║"
echo "║  Press ENTER when ready to flash the firmware.   ║"
echo "╚══════════════════════════════════════════════════╝"
read -r

# Step 5: Flash
echo "=== Flashing firmware ==="
idf.py flash

echo ""
echo "Done! All apps on SD card will be available in the launcher."

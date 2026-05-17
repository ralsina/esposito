#!/bin/bash
# Build Esposito for Wokwi emulator

set -e

echo "Building Esposito OS for Wokwi Emulator..."

# Source ESP-IDF environment
if [ -f "/opt/esp-idf/export.sh" ]; then
    . /opt/esp-idf/export.sh
else
    echo "Error: ESP-IDF not found at /opt/esp-idf"
    echo "Please update the path in this script"
    exit 1
fi

# Build firmware
echo "Building firmware..."
idf.py build

# Check if build succeeded
if [ ! -f "build/esposito.elf" ]; then
    echo "Error: Firmware build failed"
    exit 1
fi

echo "Build complete!"
echo "Firmware: build/esposito.elf"
echo ""
echo "To run in Wokwi:"
echo "1. Install Wokwi CLI: npm install -g wokwi-cli"
echo "2. Run: cd wokwi && wokwi-server"
echo "3. Open http://localhost:8080 in your browser"
echo "4. Upload build/esposito.elf to the emulator"

#!/bin/bash

# Esposito OS - Serial Monitor
# Use this to watch the boot sequence and serial output

echo "Esposito OS - Serial Monitor"
echo "============================"
echo ""
echo "Press Ctrl+C to exit"
echo "Press RESET button on device to see boot sequence"
echo ""

# Configure serial port
stty -F /dev/ttyUSB0 115200 raw -echo -echoe -echok

# Read serial output
cat /dev/ttyUSB0

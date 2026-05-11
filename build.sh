#!/bin/bash

# Esposito OS Build Script

echo "Esposito OS Build Script"
echo "========================"

# Check if ESP-IDF environment is set up
if [ -z "$IDF_PATH" ]; then
    echo "Error: ESP-IDF environment not set up"
    echo "Please source export.sh from ESP-IDF first"
    exit 1
fi

echo "ESP-IDF Path: $IDF_PATH"

# Try to build
echo "Building project..."
idf.py build

if [ $? -eq 0 ]; then
    echo "Build successful!"
else
    echo "Build failed with errors"
    exit 1
fi

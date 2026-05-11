#!/bin/bash

# Esposito OS - Structure Test
# This script verifies the project structure without requiring ESP-IDF

echo "Esposito OS - Project Structure Test"
echo "===================================="
echo ""

# Check project structure
echo "Checking project structure..."

required_files=(
    "CMakeLists.txt"
    "sdkconfig.defaults"
    "main/CMakeLists.txt"
    "main/main.c"
    "main/boot.h"
    "main/boot.c"
    "main/os_core.h"
    "main/os_core.c"
    "main/hardware.h"
    "main/hardware.c"
    "main/hardware_config.h"
    "main/app_loader.h"
    "main/app_loader.c"
    "main/checkpoint.h"
    "main/checkpoint.c"
    "apps/hello_world/app.c"
    "apps/hello_world/CMakeLists.txt"
)

all_good=true
for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        echo "✓ $file"
    else
        echo "✗ $file (MISSING)"
        all_good=false
    fi
done

echo ""
echo "Header file dependencies..."
grep -h "^#include" main/*.c | sort -u | sed 's/^/  /'

echo ""
echo "Function definitions (by module)..."
for module in main/*.c; do
    echo "  $(basename $module):"
    grep -a "^[a-zA-Z_].*(" "$module" | grep -v "^//" | grep -v "if\|for\|while\|switch" | head -5 | sed 's/^/    /'
done

echo ""
if [ "$all_good" = true ]; then
    echo "✓ All required files present"
    echo "✓ Project structure is valid"
    echo ""
    echo "Next steps:"
    echo "1. Set up ESP-IDF environment"
    echo "2. Run: idf.py build"
    echo "3. Run: idf.py flash"
else
    echo "✗ Some files are missing"
    exit 1
fi

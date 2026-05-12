#!/bin/bash
# Build a standalone app ELF for dynamic loading
# Usage: build_app.sh <app.c> [output_dir]

set -e

APP_SRC="${1}"
if [ -z "$APP_SRC" ]; then
    echo "Usage: $0 <app.c> [output_dir]"
    echo "Builds an app .c file into a relocatable ELF for SD card loading"
    echo ""
    echo "  The output ELF is named after the parent directory of the source."
    echo "  Example: $0 apps/my_app/app.c  ->  build/apps/my_app.elf"
    exit 1
fi

# Name the output after the app's directory, not the .c filename.
# This way apps/example_app/app.c produces example_app.elf.
APP_DIR="$(cd "$(dirname "$APP_SRC")" && pwd)"
APP_NAME="$(basename "$APP_DIR")"
OUTPUT_DIR="${2:-build/apps}"
TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-xtensa-esp32-elf}"

FIRMWARE_ELF="build/esposito.elf"
# Look for app.ld in the app's directory first, fall back to template
APP_LD="${APP_DIR}/app.ld"
if [ ! -f "$APP_LD" ]; then
    APP_LD="apps/app_template/app.ld"
fi
OS_SYMBOLS_LD="build/os_symbols.ld"

# Check prerequisites
if [ ! -f "$FIRMWARE_ELF" ]; then
    echo "Error: firmware ELF not found. Build the project first."
    exit 1
fi

# Generate OS symbol table if needed
if [ ! -f "$OS_SYMBOLS_LD" ]; then
    echo "Generating OS symbol table..."
    mkdir -p "$(dirname "$OS_SYMBOLS_LD")"
    scripts/gen_symtab.sh "$FIRMWARE_ELF" "$OS_SYMBOLS_LD"
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Find ESP-IDF include paths
IDF_PATH="${IDF_PATH:-$IDF_TOOLS_PATH/esp-idf}"
if [ -z "$IDF_PATH" ]; then
    # Try common locations
    if [ -d "$HOME/esp/esp-idf" ]; then
        IDF_PATH="$HOME/esp/esp-idf"
    elif [ -d "/opt/esp-idf" ]; then
        IDF_PATH="/opt/esp-idf"
    fi
fi

# Build include flags
IDF_PATH="${IDF_PATH:-/opt/esp-idf}"
INCLUDE_FLAGS="-I main -I . -I build/config"
if [ -d "$IDF_PATH" ]; then
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_common/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_system/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_timer/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/FreeRTOS-Kernel/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/heap/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/log/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/newlib/platform_include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/soc/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/hal/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_hw_support/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_rom/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/hal/esp32/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/xtensa/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/xtensa/esp32/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_app_format/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/soc/esp32/include"
fi

echo "Building app: $APP_NAME"
echo "Source: $APP_SRC"
echo "Output: $OUTPUT_DIR/${APP_NAME}.elf"

# Compile to object file
echo "  Compiling..."
"${TOOLCHAIN_PREFIX}-gcc" \
    -nostdlib -nostartfiles \
    -ffreestanding \
    -mlongcalls \
    -Wl,-q \
    -Wl,--emit-relocs \
    -T "$APP_LD" \
    -T "$OS_SYMBOLS_LD" \
    $INCLUDE_FLAGS \
    -o "$OUTPUT_DIR/${APP_NAME}.elf" \
    "$APP_SRC"

echo "  Done: $OUTPUT_DIR/${APP_NAME}.elf"
echo ""
echo "To use, copy to SD card:"
echo "  mkdir -p /sdcard/apps/${APP_NAME}"
echo "  cp $OUTPUT_DIR/${APP_NAME}.elf /sdcard/apps/${APP_NAME}/program.elf"

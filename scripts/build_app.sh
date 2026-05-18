#!/bin/bash
# Build a standalone app ELF for dynamic loading
# Usage: build_app.sh [-l <lib>...] <app.c|app_dir> [output_dir]

set -e

LIBS=()

while getopts "l:" opt; do
    case $opt in
        l) LIBS+=("$OPTARG") ;;
        ?) exit 1 ;;
    esac
done
shift $((OPTIND-1))

APP_SRC="${1}"
if [ -z "$APP_SRC" ]; then
    echo "Usage: $0 [-l <lib>...] <app.c|app_dir> [output_dir]"
    echo "Builds an app .c file(s) into a relocatable ELF for SD card loading"
    echo ""
    echo "  -l <lib>    Link against a library from libs/<lib>/ (repeatable)"
    echo "  If a .c file is given, the app directory is its parent."
    echo "  All .c files in the app directory are compiled together."
    echo "  Example: $0 -l ui apps/my_app/app.c  ->  build/apps/my_app.elf"
    exit 1
fi

# Determine app directory: if argument is a .c or .cpp file, use its parent; otherwise use as-is
if [ -f "$APP_SRC" ] && ([[ "$APP_SRC" == *.c ]] || [[ "$APP_SRC" == *.cpp ]]); then
    APP_DIR="$(cd "$(dirname "$APP_SRC")" && pwd)"
else
    APP_DIR="$(cd "$APP_SRC" && pwd)"
fi
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

# Generate OS symbol table
echo "Generating OS symbol table..."
mkdir -p "$(dirname "$OS_SYMBOLS_LD")"
scripts/gen_symtab.sh "$FIRMWARE_ELF" "$OS_SYMBOLS_LD"

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

# Get the project root directory (where we started)
PROJECT_ROOT="$(pwd)"

# Build include flags
IDF_PATH="${IDF_PATH:-/opt/esp-idf}"
INCLUDE_FLAGS="-I main -I . -I fonts -I build/config"
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

# Collect all .c and .cpp source files in the app directory
APP_SOURCES=()
HAS_CPP=false
while IFS= read -r -d '' f; do
    APP_SOURCES+=("$f")
done < <(find "$APP_DIR" -maxdepth 1 -name '*.c' -print0)
while IFS= read -r -d '' f; do
    APP_SOURCES+=("$f")
    HAS_CPP=true
done < <(find "$APP_DIR" -maxdepth 1 -name '*.cpp' -print0)

# Collect library sources and include paths
LIB_SOURCES=()
for lib in "${LIBS[@]}"; do
    LIB_DIR="libs/$lib"
    if [ ! -d "$LIB_DIR" ]; then
        echo "Error: library not found: $LIB_DIR"
        exit 1
    fi
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $LIB_DIR"
    # Collect both C and C++ files
    while IFS= read -r -d '' f; do
        LIB_SOURCES+=("$f")
    done < <(find "$LIB_DIR" -maxdepth 1 -name '*.c' -print0)
    while IFS= read -r -d '' f; do
        LIB_SOURCES+=("$f")
        HAS_CPP=true
    done < <(find "$LIB_DIR" -maxdepth 1 -name '*.cpp' -print0)
done

echo "Building app: $APP_NAME"
echo "Sources: ${APP_SOURCES[*]}"
for lib in "${LIBS[@]}"; do
    echo "  Library: $lib"
done
echo "Output: $OUTPUT_DIR/${APP_NAME}.elf"

# Compile and link all source files together
echo "  Compiling..."

# Use g++ if we have any C++ files, otherwise use gcc
if [ "$HAS_CPP" = true ]; then
    COMPILER="${TOOLCHAIN_PREFIX}-g++"
else
    COMPILER="${TOOLCHAIN_PREFIX}-gcc"
fi

$COMPILER \
    -nostdlib -nostartfiles \
    -ffreestanding \
    -mlongcalls \
    -Wl,-q \
    -Wl,--emit-relocs \
    -T "$APP_LD" \
    -T "$OS_SYMBOLS_LD" \
    $INCLUDE_FLAGS \
    -o "$OUTPUT_DIR/${APP_NAME}.elf" \
    "${APP_SOURCES[@]}" "${LIB_SOURCES[@]}"

echo "  Done: $OUTPUT_DIR/${APP_NAME}.elf"
echo ""
echo "To use, copy to SD card:"
echo "  mkdir -p /sdcard/apps/${APP_NAME}"
echo "  cp $OUTPUT_DIR/${APP_NAME}.elf /sdcard/apps/${APP_NAME}/program.elf"
if [ -f "${APP_DIR}/manifest.cfg" ]; then
    echo "  cp ${APP_DIR}/manifest.cfg /sdcard/apps/${APP_NAME}/manifest.cfg"
fi

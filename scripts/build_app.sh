#!/bin/bash
# Build a standalone app ELF for dynamic loading
# Usage: build_app.sh [-l <lib>...] <app.c|app_dir> [output_dir]

set -e

LIBS=()
OPT_LEVEL="-Os"
EXTRA_CFLAGS=""

while getopts "l:O:" opt; do
    case $opt in
        l) LIBS+=("$OPTARG") ;;
        O) OPT_LEVEL="-O$OPTARG" ;;
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

# Add performance optimizations for gameboy
if [[ "$APP_NAME" == "gameboy" ]]; then
    EXTRA_CFLAGS="-fjump-tables -ftree-switch-conversion -fno-strict-aliasing"
    echo "  Enabling jump table optimizations for gameboy"
fi

# --- Target detection -------------------------------------------------------
# Derive toolchain / chip include paths / board dir from the main firmware's
# configured target (sdkconfig). Override with IDF_TARGET=... / BOARD=... /
# TOOLCHAIN_PREFIX=... on the command line.
IDF_TARGET="${IDF_TARGET:-$(grep -oE '^CONFIG_IDF_TARGET="[^"]+"' sdkconfig 2>/dev/null | sed -E 's/^CONFIG_IDF_TARGET="([^"]+)".*/\1/')}"
IDF_TARGET="${IDF_TARGET:-esp32}"
case "$IDF_TARGET" in
    esp32s3) CHIP=esp32s3; ARCH=xtensa; DEFAULT_BOARD=guition_jc4827w543 ;;
    esp32c3) CHIP=esp32c3; ARCH=riscv;  DEFAULT_BOARD=elecrow_round ;;
    *)       CHIP=esp32;   ARCH=xtensa; DEFAULT_BOARD=cyd_2usb ;;
esac
BOARD_DIR="${BOARD:-$DEFAULT_BOARD}"
if [ -z "$TOOLCHAIN_PREFIX" ]; then
    if [ "$ARCH" = "riscv" ]; then
        TOOLCHAIN_PREFIX="riscv32-esp-elf"
    else
        TOOLCHAIN_PREFIX="xtensa-${CHIP}-elf"
    fi
fi
echo "  Target: $IDF_TARGET ($ARCH), board: $BOARD_DIR, toolchain: $TOOLCHAIN_PREFIX"

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
INCLUDE_FLAGS="-I main -I . -I fonts -I boards/${BOARD_DIR} -I build/config -I libs/lua"
if [ -d "$IDF_PATH" ]; then
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_common/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_system/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_timer/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/FreeRTOS-Kernel/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/FreeRTOS-Kernel/include/freertos"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/FreeRTOS-Kernel/portable/${ARCH}/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/FreeRTOS-Kernel/portable/${ARCH}/include/freertos"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/config/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/config/include/freertos"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/config/xtensa/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/esp_additions"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/freertos/esp_additions/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/heap/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/log/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/newlib/platform_include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/soc/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/hal/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_hw_support/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_rom/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/hal/${CHIP}/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_app_format/include"
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/soc/${CHIP}/include"
    if [ "$ARCH" = "xtensa" ]; then
        INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/xtensa/include"
        INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/xtensa/${CHIP}/include"
    fi
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I $IDF_PATH/components/esp_hw_support/etm/include"
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

# Use g++ if we have any C++ files, otherwise use gcc
if [ "$HAS_CPP" = true ]; then
    COMPILER="${TOOLCHAIN_PREFIX}-g++"
else
    COMPILER="${TOOLCHAIN_PREFIX}-gcc"
fi

# Generate manifest section from manifest.cfg
MANIFEST_GEN_O=""
MANIFEST_CFG="${APP_DIR}/manifest.cfg"
if [ -f "$MANIFEST_CFG" ]; then
    echo "  Generating manifest section..."
    MANIFEST_GEN_C="${OUTPUT_DIR}/${APP_NAME}_manifest_gen.c"
    MANIFEST_GEN_O="${OUTPUT_DIR}/${APP_NAME}_manifest_gen.o"
    {
        printf '#include <stdint.h>\n'
        printf '__attribute__((section(".manifest"), used))\n'
        printf 'const uint8_t app_manifest[] = {\n'
        xxd -i < "$MANIFEST_CFG"
        printf '};\n'
    } > "$MANIFEST_GEN_C"
    $COMPILER -c "$MANIFEST_GEN_C" -o "$MANIFEST_GEN_O" $INCLUDE_FLAGS
fi

# Collect extra object files (e.g. manifest)
EXTRA_OBJS=()
if [ -n "$MANIFEST_GEN_O" ]; then
    EXTRA_OBJS+=("$MANIFEST_GEN_O")
fi

echo "  Compiling..."

$COMPILER \
    -nostdlib -nostartfiles \
    -ffreestanding \
    $OPT_LEVEL \
    $EXTRA_CFLAGS \
    -mlongcalls \
    -fsingle-precision-constant \
    -Wno-double-promotion \
    -Wl,-q \
    -Wl,--emit-relocs \
    -T "$APP_LD" \
    -T "$OS_SYMBOLS_LD" \
    $INCLUDE_FLAGS \
    -o "$OUTPUT_DIR/${APP_NAME}.elf" \
    "${APP_SOURCES[@]}" "${LIB_SOURCES[@]}" "${EXTRA_OBJS[@]}" \
    -lgcc

echo "  Done: $OUTPUT_DIR/${APP_NAME}.elf"
echo ""
echo "To use, copy to SD card:"
echo "  mkdir -p /sdcard/apps/${APP_NAME}"
echo "  cp $OUTPUT_DIR/${APP_NAME}.elf /sdcard/apps/${APP_NAME}/program.elf"
if [ -f "${APP_DIR}/manifest.cfg" ]; then
    echo "  cp ${APP_DIR}/manifest.cfg /sdcard/apps/${APP_NAME}/manifest.cfg"
fi

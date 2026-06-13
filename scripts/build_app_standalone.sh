#!/bin/bash
# Standalone app build script — part of the Esposito SDK
# Builds an app ELF for dynamic loading without needing the full firmware checkout.
# Usage: build_app.sh [-l <lib>...] <app.c|app_dir> [output_dir]
#
# Prerequisites:
#   - xtensa-esp32-elf-* toolchain on PATH (from ESP-IDF)
#   - This script lives inside the SDK directory, OR
#     ESPOSITO_SDK_DIR points to the SDK root

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
    echo "  -l <lib>    Link against a library from the SDK's lib/ directory (repeatable)"
    echo "  If a .c file is given, the app directory is its parent."
    echo "  All .c files in the app directory are compiled together."
    echo "  Example: $0 -l ui2 apps/my_app/app.c  ->  my_app.elf"
    exit 1
fi

# Determine SDK root: script's location or env var
SDK_DIR="${ESPOSITO_SDK_DIR:-$(cd "$(dirname "$0")" && pwd)}"

TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-xtensa-esp32-elf}"

# Determine app directory
if [ -f "$APP_SRC" ] && ([[ "$APP_SRC" == *.c ]] || [[ "$APP_SRC" == *.cpp ]]); then
    APP_DIR="$(cd "$(dirname "$APP_SRC")" && pwd)"
else
    APP_DIR="$(cd "$APP_SRC" && pwd)"
fi
APP_NAME="$(basename "$APP_DIR")"
OUTPUT_DIR="${2:-.}"

# Add performance optimizations for gameboy
if [[ "$APP_NAME" == "gameboy" ]]; then
    EXTRA_CFLAGS="-fjump-tables -ftree-switch-conversion -fno-strict-aliasing"
    echo "  Enabling jump table optimizations for gameboy"
fi

# Locate linker scripts
APP_LD="${APP_DIR}/app.ld"
if [ ! -f "$APP_LD" ]; then
    APP_LD="${SDK_DIR}/ld/app.ld"
fi
OS_SYMBOLS_LD="${SDK_DIR}/ld/os_symbols.ld"

if [ ! -f "$OS_SYMBOLS_LD" ]; then
    echo "Error: OS symbol table not found at $OS_SYMBOLS_LD"
    echo "Is ESPOSITO_SDK_DIR set correctly?"
    exit 1
fi

# Build include flags (SDK headers only, no ESP-IDF needed)
INCLUDE_FLAGS="-I ${SDK_DIR}/include"

mkdir -p "$OUTPUT_DIR"

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

# Collect library sources and include paths from SDK lib/
LIB_SOURCES=()
for lib in "${LIBS[@]}"; do
    LIB_DIR="${SDK_DIR}/lib/${lib}"
    if [ ! -d "$LIB_DIR" ]; then
        echo "Error: library not found: $LIB_DIR"
        exit 1
    fi
    INCLUDE_FLAGS="$INCLUDE_FLAGS -I ${SDK_DIR}/include/${lib}"
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

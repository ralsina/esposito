#!/bin/bash
# Build the SD card app bundle for a release.
#
# This script assumes the firmware has already been built (idf.py build)
# and the ESP-IDF environment is sourced. It:
#   1. Generates the OS symbol table (os_symbols.ld)
#   2. Builds a curated set of app ELFs
#   3. Assembles the SD card directory tree (apps/, fonts/, books/)
#   4. Produces a zip ready to extract onto a FAT32 SD card
#
# Usage: build_release_bundle.sh <output_zip> [version]
# Example: build_release_bundle.sh artifacts/sdcard-bundle.zip 0.5.1

set -e

OUTPUT_ZIP="${1:-artifacts/sdcard-bundle.zip}"
VERSION="${2:-unknown}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

# ---------------------------------------------------------------------------
# Apps to include in the bundle.
# Add or remove entries here. Each name must match a directory under apps/.
# ---------------------------------------------------------------------------
APPS=(
    launcher
    settings
    reader
    clock
    calc
    snake
    breakout
    file_manager
)

echo "=== Building release SD card bundle (v${VERSION}) ==="

# --- 1. Generate symbol table -------------------------------------------------
echo ">>> Generating OS symbol table..."
scripts/gen_symtab.sh build/esposito.elf build/os_symbols.ld

# --- 2. Build app ELFs --------------------------------------------------------
echo ">>> Building ${#APPS[@]} apps..."
mkdir -p build/apps

for app_name in "${APPS[@]}"; do
    app_dir="apps/${app_name}"
    app_src="${app_dir}/app.c"
    if [ ! -f "$app_src" ]; then
        app_src="${app_dir}/app.cpp"
    fi
    if [ ! -f "$app_src" ]; then
        echo "  WARNING: ${app_name} has no app.c/app.cpp, skipping"
        continue
    fi

    DEPS=""
    if [ -f "${app_dir}/deps" ]; then
        DEPS=$(while read -r lib; do echo -n "-l $lib "; done < "${app_dir}/deps")
    fi
    echo "  Building ${app_name}..."
    scripts/build_app.sh $DEPS "$app_src" build/apps
done

# --- 3. Assemble SD card directory -------------------------------------------
echo ">>> Assembling SD card directory..."
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

# Apps
for app_name in "${APPS[@]}"; do
    elf="build/apps/${app_name}.elf"
    [ -f "$elf" ] || { echo "  WARNING: ${app_name}.elf not found, skipping"; continue; }
    mkdir -p "$STAGING/apps/${app_name}"
    cp "$elf" "$STAGING/apps/${app_name}/program.elf"
    manifest="apps/${app_name}/manifest.cfg"
    if [ -f "$manifest" ]; then
        cp "$manifest" "$STAGING/apps/${app_name}/manifest.cfg"
    fi
    echo "  + apps/${app_name}/"
done

# Fonts
echo ">>> Copying fonts..."
mkdir -p "$STAGING/fonts/fpack"
cp fonts/*.fpack "$STAGING/fonts/fpack/"
echo "  + fonts/fpack/ ($(ls "$STAGING/fonts/fpack/" | wc -l) font packs)"

# Books
echo ">>> Copying books..."
if [ -d books ]; then
    mkdir -p "$STAGING/books"
    cp books/*.md "$STAGING/books/" 2>/dev/null || cp books/*.txt "$STAGING/books/" 2>/dev/null || true
    echo "  + books/ ($(ls "$STAGING/books/" 2>/dev/null | wc -l) files)"
fi

# Version stamp
echo "v${VERSION}" > "$STAGING/ESPOSITO_VERSION.txt"

# --- 4. Zip -------------------------------------------------------------------
echo ">>> Creating archive..."
mkdir -p "$(dirname "$OUTPUT_ZIP")"
( cd "$STAGING" && zip -r -q "$ROOT_DIR/$OUTPUT_ZIP" . )

echo "=== Done: ${OUTPUT_ZIP} ==="
ls -lh "$ROOT_DIR/$OUTPUT_ZIP"
echo ""
echo "Users extract this zip to the root of a FAT32 SD card."
echo "Directory structure:"
( cd "$STAGING" && find . -maxdepth 2 -type d | sort )

#!/bin/bash
# Package the Esposito SDK — a standalone tarball for building apps
# without the full firmware checkout.
#
# Usage: package_sdk.sh [output_dir]
#
# Prerequisites:
#   - Firmware must be built: build/esposito.elf must exist
#   - xtensa-esp32-elf toolchain on PATH (for gen_symtab.sh)
#   - scripts/gen_symtab.sh in the project

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$(realpath "${1:-${PROJECT_ROOT}/build}")"
SDK_VERSION="${SDK_VERSION:-$(git -C "$PROJECT_ROOT" describe --tags --always 2>/dev/null | sed 's/^v//' || echo "0.0.0")}"
SDK_NAME="esposito-sdk-${SDK_VERSION}"

# Temp working directory
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

SDK_DIR="${WORK_DIR}/${SDK_NAME}"

echo "Packaging Esposito SDK v${SDK_VERSION}..."
echo "Output: ${OUTPUT_DIR}/${SDK_NAME}.tar.gz"

# Verify prerequisites
FIRMWARE_ELF="${PROJECT_ROOT}/build/esposito.elf"
if [ ! -f "$FIRMWARE_ELF" ]; then
    echo "Error: firmware ELF not found at ${FIRMWARE_ELF}"
    echo "Build the project first with: idf.py build"
    exit 1
fi

GEN_SYMTAB="${PROJECT_ROOT}/scripts/gen_symtab.sh"
if [ ! -f "$GEN_SYMTAB" ]; then
    echo "Error: gen_symtab.sh not found at ${GEN_SYMTAB}"
    exit 1
fi

# Ensure toolchain is available
TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-xtensa-esp32-elf}"
if ! command -v "${TOOLCHAIN_PREFIX}-nm" &>/dev/null; then
    echo "Error: ${TOOLCHAIN_PREFIX}-nm not found on PATH"
    echo "Source ESP-IDF environment first or add the toolchain to PATH."
    echo "  Example: . /opt/esp-idf/export.sh"
    exit 1
fi

mkdir -p "${SDK_DIR}/include"
mkdir -p "${SDK_DIR}/lib"
mkdir -p "${SDK_DIR}/ld"

# --- Copy headers ---
# Top-level API headers from main/
echo "  Copying headers..."
for header in os_core.h app_config.h app_manifest.h text_mode.h hardware.h \
               hardware_config.h graphics_mode.h terminal_mode.h wifi.h; do
    cp "${PROJECT_ROOT}/main/${header}" "${SDK_DIR}/include/"
done

# Fonts header
cp "${PROJECT_ROOT}/fonts/fonts.h" "${SDK_DIR}/include/"

# ui2 library headers
mkdir -p "${SDK_DIR}/include/ui2"
cp "${PROJECT_ROOT}/libs/ui2/"*.h "${SDK_DIR}/include/ui2/"

# json library headers
mkdir -p "${SDK_DIR}/include/json"
cp "${PROJECT_ROOT}/libs/json/core_json.h" "${SDK_DIR}/include/json/"

# serial_rx library headers
mkdir -p "${SDK_DIR}/include/serial_rx"
cp "${PROJECT_ROOT}/libs/serial_rx/serial_rx.h" "${SDK_DIR}/include/serial_rx/"

# Lua library headers (optional)
mkdir -p "${SDK_DIR}/include/lua"
cp "${PROJECT_ROOT}/libs/lua/"*.h "${SDK_DIR}/include/lua/"

# --- Copy library source files ---
echo "  Copying library sources..."
mkdir -p "${SDK_DIR}/lib/ui2"
cp "${PROJECT_ROOT}/libs/ui2/"*.c "${SDK_DIR}/lib/ui2/"

mkdir -p "${SDK_DIR}/lib/json"
cp "${PROJECT_ROOT}/libs/json/core_json.c" "${SDK_DIR}/lib/json/"

mkdir -p "${SDK_DIR}/lib/serial_rx"
cp "${PROJECT_ROOT}/libs/serial_rx/serial_rx.c" "${SDK_DIR}/lib/serial_rx/"

# --- Copy linker scripts ---
echo "  Generating linker scripts..."
mkdir -p "${SDK_DIR}/ld"
cp "${PROJECT_ROOT}/apps/app_template/app.ld" "${SDK_DIR}/ld/"

# Generate OS symbol table from firmware ELF
bash "$GEN_SYMTAB" "$FIRMWARE_ELF" "${SDK_DIR}/ld/os_symbols.ld"

# --- Copy build script ---
echo "  Copying build script..."
cp "${PROJECT_ROOT}/scripts/build_app_standalone.sh" "${SDK_DIR}/build_app.sh"
chmod +x "${SDK_DIR}/build_app.sh"

# --- Write version file ---
echo "${SDK_VERSION}" > "${SDK_DIR}/version.txt"

# --- Pack tarball ---
mkdir -p "$OUTPUT_DIR"
cd "$WORK_DIR"
tar czf "${OUTPUT_DIR}/${SDK_NAME}.tar.gz" "$SDK_NAME"

echo ""
echo "Done: ${OUTPUT_DIR}/${SDK_NAME}.tar.gz"
echo "  Size: $(du -h "${OUTPUT_DIR}/${SDK_NAME}.tar.gz" | cut -f1)"
echo ""
echo "To use:"
echo "  tar xzf ${SDK_NAME}.tar.gz"
echo "  cd ${SDK_NAME}"
echo "  ./build_app.sh -l ui2 path/to/app.c"

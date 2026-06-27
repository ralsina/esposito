#!/bin/bash
# Build ALL apps into build/apps/ for the App Store.
# Assumes firmware is already built and ESP-IDF is sourced.
#
# Usage: build_all_apps.sh [target]
#   target: esp32 (default) or esp32s3
set -e

TARGET="${1:-${IDF_TARGET:-esp32}}"
BUILD_DIR="build/apps-${TARGET}"

cd "$(cd "$(dirname "$0")/.." && pwd)"

echo "=== Building all apps for ${TARGET} ==="
mkdir -p "${BUILD_DIR}"

for app_dir in apps/*/; do
    app_name=$(basename "$app_dir")
    app_src="${app_dir}app.c"
    if [ ! -f "$app_src" ]; then
        app_src="${app_dir}app.cpp"
    fi
    if [ ! -f "$app_src" ]; then
        continue
    fi

    # Skip template and non-launcher apps
    if [ -f "${app_dir}manifest.cfg" ]; then
        launcher=$(grep -oP 'launcher\s*=\s*\K\w+' "${app_dir}manifest.cfg" || echo "")
        if [ "$launcher" != "yes" ]; then
            echo "  Skip ${app_name} (not in launcher)"
            continue
        fi
    fi

    DEPS=""
    if [ -f "${app_dir}deps" ]; then
        DEPS=$(while read -r lib; do echo -n "-l $lib "; done < "${app_dir}deps")
    fi
    echo "  Building ${app_name}..."
    scripts/build_app.sh -t "$TARGET" $DEPS "$app_src" "${BUILD_DIR}"
done

echo "=== Done: $(ls "${BUILD_DIR}"/*.elf 2>/dev/null | wc -l) apps built for ${TARGET} ==="

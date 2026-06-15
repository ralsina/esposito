#!/bin/bash
# Build and run an app in the SDL2 emulator
# Usage: ./run.sh [app_name]
#   app_name: name of an app in apps/ (default: hello_world)
#             or path to an app.c file
#
# Environment:
#   ESP_SD_CARD  - Path to use as the emulated SD card (default: /sdcard)
#                  The emulator will symlink /sdcard -> $ESP_SD_CARD if set.

set -e

APP="${1:-hello_world}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# If it's a simple name (no path separators, no .c), resolve to apps/<name>/app.c
if [[ "$APP" != */* && "$APP" != *.c ]]; then
    APP="../apps/$APP/app.c"
fi

echo "==> Building $APP for emulation..."
make -s -C "$SCRIPT_DIR" clean 2>/dev/null
make -s -C "$SCRIPT_DIR" APP="$APP"

BIN="$SCRIPT_DIR/build/$(basename "${APP%%/app.c}")"
echo "==> Running $BIN"
exec "$BIN"

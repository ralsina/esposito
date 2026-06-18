#!/bin/bash
# Run the ELF loader end-to-end host test (Option B).
#
# This is more complex than the other test suites because elf_loader.c pulls
# in real ESP-IDF headers (esp_partition.h, esp_log.h, freertos/...). We solve
# this by:
#   1. -include elf_host_stubs.h  — forces the stub API before any real header
#   2. Creating fake header dirs (esp_partition/, freertos/, esp_log.h) that
#      are empty — since the stub header already defines everything via
#      -include, the #include directives find empty files and do nothing.
#
# The flash mock (elf_host_stubs.c) backs esp_partition_* with a RAM buffer
# allocated with mmap(MAP_32BIT) so addresses fit in uint32_t (the loader
# casts pointers to uint32_t, as it would on a 32-bit MCU).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"   # tests/host
TESTS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"      # tests
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"        # repo root
HOST_DIR="$SCRIPT_DIR"
FAKE_DIR="$SCRIPT_DIR/fake_includes"

CC=${CC:-gcc}
CFLAGS="-Wall -Wextra -std=c11 -g -O0 -w -D_POSIX_C_SOURCE=200809L"

# Create fake include directories so elf_loader.c's #include <esp_partition.h>
# etc. find an empty file. The real definitions come from elf_host_stubs.h
# which is force-included via -include.
mkdir -p "$FAKE_DIR/esp_partition" "$FAKE_DIR/freertos"
: > "$FAKE_DIR/esp_log.h"
: > "$FAKE_DIR/esp_partition.h"
: > "$FAKE_DIR/esp_cache.h"
: > "$FAKE_DIR/freertos/FreeRTOS.h"
: > "$FAKE_DIR/freertos/task.h"
: > "$FAKE_DIR/freertos/queue.h"
: > "$FAKE_DIR/freertos/semphr.h"

# Build the test binary.
SOURCES=(
    "$HOST_DIR/test_elf_loader.c"
    "$ROOT/main/elf_loader.c"
    "$ROOT/main/elf_validate.c"
    "$HOST_DIR/elf_host_stubs.c"
    "$HOST_DIR/elf_fixture.c"
)

INCLUDES=(
    -I"$TESTS_DIR"
    -I"$ROOT/main"
    -I"$ROOT/tests"
    -I"$HOST_DIR"
    -I"$FAKE_DIR"
    -isystem "$FAKE_DIR"
)

echo "--- test_elf_loader ---"
$CC $CFLAGS "${INCLUDES[@]}" -include "$HOST_DIR/elf_host_stubs.h" \
    "${SOURCES[@]}" -o /tmp/test_elf_loader
/tmp/test_elf_loader -v

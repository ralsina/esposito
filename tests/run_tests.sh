#!/bin/bash
# Run host-side unit tests (requires gcc).
#
# Tests use the greatest framework (tests/greatest.h, vendored, ISC-style).
# Each tests/test_*.c is compiled against the host-compilable pure-C sources
# under main/ and executed. A test "passes" only if it exits 0, so assertion
# failures (which greatest turns into a non-zero exit) count as failures.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"
echo "Running host unit tests..."
echo ""

FAILED=0
PASSED=0

# Host-compilable unit sources linked into every test binary. Add new
# host-clean translation units here as they are extracted from the firmware.
HOST_SOURCES="main/semver.c main/elf_validate.c"

for test_src in "$SCRIPT_DIR"/test_*.c; do
    [ -e "$test_src" ] || continue
    test_name="$(basename "$test_src" .c)"
    echo "--- $test_name ---"
    if gcc -o "/tmp/${test_name}" "$test_src" $HOST_SOURCES \
         -I"$SCRIPT_DIR" -Imain -w 2>&1; then
        if "/tmp/${test_name}"; then
            PASSED=$((PASSED + 1))
        else
            echo "FAIL: test exited with error"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "FAIL: compilation error"
        FAILED=$((FAILED + 1))
    fi
    echo ""
done

echo "=== Results: $PASSED passed, $FAILED failed ==="
exit $FAILED

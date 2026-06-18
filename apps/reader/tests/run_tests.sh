#!/bin/bash
# Run reader tokenizer/renderer unit tests on host (requires gcc).
#
# Tests use the greatest framework (tests/greatest.h, vendored at the repo
# root). Each tests/test_*.c is compiled against the reader's host-compilable
# sources (reader_token.c + reader_renderer.c) and executed. A test "passes"
# only if it exits 0, so assertion failures (which greatest turns into a
# non-zero exit) count as failures.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

cd "$PROJECT_ROOT"
echo "Running reader tests..."
echo ""

FAILED=0
PASSED=0

# Host-compilable reader sources linked into every test binary.
READER_SOURCES="apps/reader/reader_token.c apps/reader/reader_renderer.c"

for test_src in "$SCRIPT_DIR"/test_*.c; do
    [ -e "$test_src" ] || continue
    test_name="$(basename "$test_src" .c)"
    echo "--- $test_name ---"
    if gcc -o "/tmp/${test_name}" "$test_src" $READER_SOURCES \
         -I"$SCRIPT_DIR" -Iapps/reader -I"$PROJECT_ROOT/tests" \
         -isystem "$SCRIPT_DIR" -w 2>&1; then
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

#!/bin/bash
# Run reader tokenizer tests on host (requires gcc)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

cd "$PROJECT_ROOT"
echo "Running reader tests..."
echo ""

FAILED=0
PASSED=0

for test_src in "$SCRIPT_DIR"/test_*.c; do
    test_name="$(basename "$test_src" .c)"
    echo "--- $test_name ---"
    if gcc -o "/tmp/${test_name}" "$test_src" \
         apps/reader/reader_token.c \
         apps/reader/reader_renderer.c \
         -Iapps/reader -isystem "$SCRIPT_DIR" -w 2>&1; then
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

#!/usr/bin/env bash
set -uo pipefail
REPEAT="${1:-10}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-tsan-sweep"
SUPPRESSIONS="$ROOT_DIR/tsan_suppressions.txt"
if [ ! -f "$SUPPRESSIONS" ]; then
    echo "FATAL: suppressions file not found at $SUPPRESSIONS"
    exit 2
fi
echo "=== Configuring TSan build ($BUILD_DIR) ==="
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSCHEDULER_ENABLE_TSAN=ON \
    -DSCHEDULER_BUILD_TESTS=ON \
    -DSCHEDULER_BUILD_EXAMPLES=ON \
    > "$BUILD_DIR.configure.log" 2>&1
if [ $? -ne 0 ]; then
    echo "FATAL: cmake configure failed -- see $BUILD_DIR.configure.log"
    exit 2
fi
echo "=== Building ==="
NPROC="$(nproc 2>/dev/null || echo 2)"
cmake --build "$BUILD_DIR" -j"$NPROC" > "$BUILD_DIR.build.log" 2>&1
if [ $? -ne 0 ]; then
    echo "FATAL: build failed -- see $BUILD_DIR.build.log"
    exit 2
fi
echo "=== Running $REPEAT full-suite sweep(s) ==="
OVERALL_FAIL=0
for i in $(seq 1 "$REPEAT"); do
    OUTPUT="$(cd "$BUILD_DIR" && timeout 120 env TSAN_OPTIONS="halt_on_error=0 suppressions=$SUPPRESSIONS" ctest --output-on-failure 2>&1)"
    TIMED_OUT=$?
    if [ "$TIMED_OUT" -eq 124 ]; then
        echo "[run $i/$REPEAT] FAIL: sweep timed out after 120s -- likely undefined behavior causing a hang, not a clean crash"
        OVERALL_FAIL=1
        continue
    fi
    if [ "$TIMED_OUT" -ne 0 ]; then
        echo "[run $i/$REPEAT] FAIL: ctest failed with exit code $TIMED_OUT"
        printf '%s\n' "$OUTPUT" | tail -40
        OVERALL_FAIL=1
        continue
    fi
    WARNING_COUNT="$(printf '%s\n' "$OUTPUT" | grep -c 'WARNING: ThreadSanitizer' || true)"
    TESTS_PASSED="$(printf '%s\n' "$OUTPUT" | grep -c '100% tests passed' || true)"
    if [ "$WARNING_COUNT" -ne 0 ]; then
        echo "[run $i/$REPEAT] FAIL: $WARNING_COUNT unsuppressed ThreadSanitizer warning(s)"
        printf '%s\n' "$OUTPUT" | grep -A 25 'WARNING: ThreadSanitizer'
        OVERALL_FAIL=1
    elif [ "$TESTS_PASSED" -eq 0 ]; then
        echo "[run $i/$REPEAT] FAIL: a test failed (or crashed) -- 0 TSan warnings, but not a clean pass"
        printf '%s\n' "$OUTPUT" | tail -20
        OVERALL_FAIL=1
    else
        echo "[run $i/$REPEAT] OK: 0 unsuppressed warnings, all tests passed"
    fi
done
echo
if [ "$OVERALL_FAIL" -ne 0 ]; then
    echo "=== TSAN SWEEP FAILED -- see above for the offending run(s) ==="
    exit 1
fi
echo "=== TSAN SWEEP PASSED: $REPEAT full-suite runs, 0 unsuppressed warnings ==="
exit 0
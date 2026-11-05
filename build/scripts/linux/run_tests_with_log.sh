#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

BIN_DIR="$(realpath ../../../bin/clang/release)"
BIN_LIB_DIR="$BIN_DIR/lib"
RESULTS_FILE="$BIN_DIR/test_results.txt"

mkdir -p "$BIN_DIR"
: > "$RESULTS_FILE"

cd "$BIN_DIR"

# Ensure local runtime libs are discoverable (main bin + deployed lib folder).
export LD_LIBRARY_PATH="$BIN_DIR:$BIN_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

for f in Test_*; do
    [[ -f "$f" && -x "$f" ]] || continue
    echo "Running $f" | tee -a "$RESULTS_FILE"
    echo "----------------------------------------" | tee -a "$RESULTS_FILE"

    "./$f" 2>&1 | tee -a "$RESULTS_FILE"
    status=${PIPESTATUS[0]}

    echo "----------------------------------------" | tee -a "$RESULTS_FILE"

    if (( status != 0 )); then
        echo "Test $f failed" | tee -a "$RESULTS_FILE"
        echo "One or more tests failed." | tee -a "$RESULTS_FILE"
        exit 1
    fi
done

echo "All tests passed." | tee -a "$RESULTS_FILE"

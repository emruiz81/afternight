#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

ROOT_DIR="$(realpath ../../..)"
BIN_DIR="$ROOT_DIR/bin/clang/release"
BIN_LIB_DIR="$BIN_DIR/lib"
ROOT_LIB_DIR="$ROOT_DIR/lib/Release"

echo "****************09-measure_extension_host_handshake_release****************"

./08a-build_afternight_tests_release.sh

echo "Environment"
printf 'CPU_MODEL=' && lscpu | awk -F: '/Model name/ {gsub(/^ +/, "", $2); print $2; exit}'
printf 'CPU_CORES=' && nproc
printf 'MEM_TOTAL=' && free -h | awk '/Mem:/ {print $2}'
printf 'KERNEL=' && uname -sr

cd "$ROOT_DIR"
export LD_LIBRARY_PATH="$BIN_DIR:$ROOT_LIB_DIR:$BIN_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM=offscreen

echo "Benchmarks"
"$BIN_DIR/Test_ProcessFramework" \
    --gtest_filter=ExtensionHostManagerTest.PreliminarySpawnToHandshakeBaseline:ExtensionHostManagerTest.PreliminaryEndToEndColdStartBaseline

echo "Done!"
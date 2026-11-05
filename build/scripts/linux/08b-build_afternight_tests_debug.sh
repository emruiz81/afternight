#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************08b-build_afternight_tests_debug****************
pushd ../../make-linux/tests
make -j$(nproc) config=debug_x64
popd
pushd ../../make-linux
make -j$(nproc) config=debug_x64 AfterNightExtensionHost
make -j$(nproc) config=debug_x64 AfterNightExtensionRuntimeBackend
popd

echo Done!

#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************08a-build_afternight_tests_release****************
pushd ../../make-linux/tests
make -j$(nproc) config=release_x64
popd
pushd ../../make-linux
make -j$(nproc) config=release_x64 AfterNightExtensionHost
make -j$(nproc) config=release_x64 AfterNightExtensionRuntimeBackend
popd

echo Done!

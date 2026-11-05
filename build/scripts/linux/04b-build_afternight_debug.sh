#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************04b-build_afternight_debug****************
pushd ../../make-linux
# Default 'all' target already includes AfterNightExtensionHost and AfterNightExtensionRuntimeBackend.
make -j$(nproc) config=debug_x64
popd

echo Done!

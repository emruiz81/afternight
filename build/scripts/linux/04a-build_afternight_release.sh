#!/bin/bash
set -e

cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************04a-build_afternight_release****************
pushd ../../make-linux
# Default 'all' target already includes AfterNightExtensionHost and AfterNightExtensionRuntimeBackend.
make -j$(nproc) config=release_x64
popd

echo Done!

#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************03b-build_3rdparty_libs_debug****************
pushd ../../make-linux/3rdparty
make -j$(nproc) config=debug_x64
popd

# The engine links these libraries, and the linker resolves what a linked library needs from the binaries' own
# directory, so a consumer of the engine links only once they are there. 05b copies them again with the rest.
mkdir -p ../../../bin/clang/debug
cp -f ../../../3rdparty/bin/clang/debug/*.so ../../../bin/clang/debug/

echo Done!

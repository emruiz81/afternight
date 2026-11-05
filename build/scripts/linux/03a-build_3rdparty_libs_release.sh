#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************03a-build_3rdparty_libs_release****************
pushd ../../make-linux/3rdparty
make -j$(nproc) config=release_x64
popd

# The engine links these libraries, and the linker resolves what a linked library needs from the binaries' own
# directory, so a consumer of the engine links only once they are there. 05a copies them again with the rest.
mkdir -p ../../../bin/clang/release
cp -f ../../../3rdparty/bin/clang/release/*.so ../../../bin/clang/release/

echo Done!

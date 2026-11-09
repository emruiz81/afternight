#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# Usage: _build_openfits.sh [release]
#
# Builds OpenFITS, the FITS codec's library, from its published release as a shared library and installs it into
# openfits/build-clang-install: the library under lib and the headers under include. The release's tag is cloned into
# openfits/src and refused unless it is the commit pinned below, and the library is built by its own CMake project
# against the vcpkg port of the x64-linux triplet. That is a position-independent static archive, so its zlib is
# carried inside it, and the linker keeps every one of zlib's symbols out of its dynamic symbol table: the library
# exports its own API and nothing else. It is built without its optional oneTBB: the engine runs parallel work only
# through its own scheduler, under each operation's grant, so the library decompresses and compresses tiles on the
# thread that calls it. Both configurations are built; `release` builds the Release one alone, as the hosted
# workflows do. 02-install_linux_deps.sh runs this, and the engine's build copies the library beside the engine.
#
# This is how the build acquires OpenFITS until the vcpkg registry carries it. The Windows script,
# build/scripts/msvc/_build_openfits.bat, pins the same release, and check_dependency_provenance.py holds both to the
# record in build/scripts/tools/dependencies.txt.
OPENFITS_REPOSITORY=https://github.com/astroformats/openfits.git
OPENFITS_TAG=v1.0.0
OPENFITS_COMMIT=8f22a0befbd4ccac5369cc1f2ed875ff8670b90a

CONFIGURATIONS=(Release Debug)
if [ $# -gt 0 ]; then
    if [ $# -ne 1 ] || [ "$1" != release ]; then
        echo "Usage: $0 [release]"
        exit 1
    fi
    CONFIGURATIONS=(Release)
fi

ROOT="$(cd ../../.. && pwd)"
VCPKG_DIR="$ROOT/vcpkg"
TRIPLET=x64-linux
SRC_DIR="$ROOT/openfits/src"
BUILD_DIR="$ROOT/openfits/build-clang"
INSTALL_DIR="$ROOT/openfits/build-clang-install"

if [ ! -f "$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" ]; then
    echo "ERROR: the vcpkg submodule is not checked out at $VCPKG_DIR."
    exit 1
fi
if [ ! -d "$VCPKG_DIR/installed/$TRIPLET/share/zlib" ]; then
    echo "ERROR: the vcpkg port OpenFITS is built against is not installed for $TRIPLET."
    echo "Run 02-install_linux_deps.sh first."
    exit 1
fi

# The source is the release's tag, verified to be the pinned commit. A checkout already at that commit and unmodified
# is kept; anything else is cloned afresh.
fetched=""
if [ -d "$SRC_DIR/.git" ] && [ -z "$(git -C "$SRC_DIR" status --porcelain 2>/dev/null)" ]; then
    fetched="$(git -C "$SRC_DIR" rev-parse HEAD 2>/dev/null || true)"
fi
if [ "$fetched" != "$OPENFITS_COMMIT" ]; then
    rm -rf "$SRC_DIR"
    echo "Fetching OpenFITS $OPENFITS_TAG..."
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$OPENFITS_TAG" "$OPENFITS_REPOSITORY" \
        "$SRC_DIR"
    fetched="$(git -C "$SRC_DIR" rev-parse HEAD)"
    if [ "$fetched" != "$OPENFITS_COMMIT" ]; then
        echo "ERROR: $OPENFITS_TAG of $OPENFITS_REPOSITORY is $fetched, not the pinned $OPENFITS_COMMIT."
        exit 1
    fi
fi

rm -rf "$BUILD_DIR" "$INSTALL_DIR"

# The library's tests, samples, benchmarks, fuzz targets and documentation are its own repository's business; vcpkg's
# classic mode supplies the port, and the repository's own manifest is not read. One single-configuration tree per
# configuration, so the toolchain hands each the archive of that configuration.
for configuration in "${CONFIGURATIONS[@]}"; do
    build="$BUILD_DIR/${configuration,,}"
    echo "Configuring OpenFITS ($configuration)..."
    cmake -G Ninja -B "$build" -S "$SRC_DIR" \
        -DCMAKE_BUILD_TYPE="$configuration" \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
        -DVCPKG_MANIFEST_MODE=OFF \
        -DBUILD_SHARED_LIBS=ON \
        -DOPENFITS_BUILD_TESTS=OFF \
        -DOPENFITS_BUILD_SAMPLES=OFF \
        -DOPENFITS_BUILD_BENCHMARKS=OFF \
        -DOPENFITS_BUILD_FUZZERS=OFF \
        -DOPENFITS_BUILD_DOCS=OFF \
        -DOPENFITS_WITH_TBB=OFF \
        -DOPENFITS_INSTALL=ON \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--exclude-libs,ALL"
    echo "Building and installing OpenFITS ($configuration)..."
    cmake --build "$build" -j"$(nproc)"
    cmake --install "$build"

    suffix=""
    [ "$configuration" = Debug ] && suffix=d
    library="$INSTALL_DIR/lib/libopenfits$suffix.so.1.0"
    if [ ! -f "$library" ]; then
        echo "ERROR: the $configuration build installed no $(basename "$library")."
        exit 1
    fi
    # The archive inside stays inside: a symbol of its in the dynamic table would bind another copy of zlib in the
    # process to this one.
    exported="$(nm -DC --defined-only "$library" | grep -E ' (deflate|inflate|crc32|adler32|compress|uncompress|gz|zlib|zError|get_crc_table)' \
        || true)"
    if [ -n "$exported" ]; then
        echo "ERROR: $(basename "$library") exports symbols of the library it carries:"
        echo "$exported" | head -20
        exit 1
    fi
done

echo
echo "==== OpenFITS $OPENFITS_TAG is installed in $INSTALL_DIR ===="

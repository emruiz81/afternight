#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# Usage: _build_openxisf.sh [release]
#
# Builds OpenXISF, the XISF codec's library, from its published release as a shared library and installs it into
# openxisf/build-clang-install: the library under lib and the headers under include. The release's tag is cloned into
# openxisf/src and refused unless it is the commit pinned below, and the library is built by its own CMake project
# against the vcpkg ports of the x64-linux triplet. Those are position-independent static archives, so its pugixml,
# LZ4, Zstandard and zlib are carried inside it, and the linker keeps every one of their symbols out of its dynamic
# symbol table: the library exports its own API and nothing else. Both configurations are built; `release` builds
# the Release one alone, as the hosted workflows do. 02-install_linux_deps.sh runs this, and the engine's build
# copies the library beside the engine.
#
# This is how the build acquires OpenXISF until the vcpkg registry carries it. The Windows script,
# build/scripts/msvc/_build_openxisf.bat, pins the same release, and check_dependency_provenance.py holds both to the
# record in build/scripts/tools/dependencies.txt.
OPENXISF_REPOSITORY=https://github.com/astroformats/openxisf.git
OPENXISF_TAG=v1.1.0
OPENXISF_COMMIT=ceca3adcfc06b66face78dd22a1422cc3ccd85cd

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
SRC_DIR="$ROOT/openxisf/src"
BUILD_DIR="$ROOT/openxisf/build-clang"
INSTALL_DIR="$ROOT/openxisf/build-clang-install"

if [ ! -f "$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" ]; then
    echo "ERROR: the vcpkg submodule is not checked out at $VCPKG_DIR."
    exit 1
fi
if [ ! -d "$VCPKG_DIR/installed/$TRIPLET/share/pugixml" ]; then
    echo "ERROR: the vcpkg ports OpenXISF is built against are not installed for $TRIPLET."
    echo "Run 02-install_linux_deps.sh first."
    exit 1
fi

# A tree that installed OpenXISF as a vcpkg port before this script took its place removes the port: its headers and
# its archive sit in directories every project searches, where they could be found ahead of these.
if [ -d "$VCPKG_DIR/installed/$TRIPLET/share/openxisf" ]; then
    echo "Removing the vcpkg port openxisf, which this build replaces..."
    "$VCPKG_DIR/vcpkg" remove openxisf --triplet "$TRIPLET" --vcpkg-root "$VCPKG_DIR"
fi

# The source is the release's tag, verified to be the pinned commit. A checkout already at that commit and unmodified
# is kept; anything else is cloned afresh.
fetched=""
if [ -d "$SRC_DIR/.git" ] && [ -z "$(git -C "$SRC_DIR" status --porcelain 2>/dev/null)" ]; then
    fetched="$(git -C "$SRC_DIR" rev-parse HEAD 2>/dev/null || true)"
fi
if [ "$fetched" != "$OPENXISF_COMMIT" ]; then
    rm -rf "$SRC_DIR"
    echo "Fetching OpenXISF $OPENXISF_TAG..."
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$OPENXISF_TAG" "$OPENXISF_REPOSITORY" \
        "$SRC_DIR"
    fetched="$(git -C "$SRC_DIR" rev-parse HEAD)"
    if [ "$fetched" != "$OPENXISF_COMMIT" ]; then
        echo "ERROR: $OPENXISF_TAG of $OPENXISF_REPOSITORY is $fetched, not the pinned $OPENXISF_COMMIT."
        exit 1
    fi
fi

rm -rf "$BUILD_DIR" "$INSTALL_DIR"

# The library's tests, samples, benchmarks, fuzz targets and documentation are its own repository's business; vcpkg's
# classic mode supplies the ports, and the repository's own manifest is not read. One single-configuration tree per
# configuration, so the toolchain hands each the archives of that configuration.
for configuration in "${CONFIGURATIONS[@]}"; do
    build="$BUILD_DIR/${configuration,,}"
    echo "Configuring OpenXISF ($configuration)..."
    cmake -G Ninja -B "$build" -S "$SRC_DIR" \
        -DCMAKE_BUILD_TYPE="$configuration" \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
        -DVCPKG_MANIFEST_MODE=OFF \
        -DBUILD_SHARED_LIBS=ON \
        -DOPENXISF_BUILD_TESTS=OFF \
        -DOPENXISF_BUILD_SAMPLES=OFF \
        -DOPENXISF_BUILD_BENCHMARKS=OFF \
        -DOPENXISF_BUILD_FUZZERS=OFF \
        -DOPENXISF_BUILD_DOCS=OFF \
        -DOPENXISF_INSTALL=ON \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--exclude-libs,ALL"
    echo "Building and installing OpenXISF ($configuration)..."
    cmake --build "$build" -j"$(nproc)"
    cmake --install "$build"

    suffix=""
    [ "$configuration" = Debug ] && suffix=d
    library="$INSTALL_DIR/lib/libopenxisf$suffix.so.1.1"
    if [ ! -f "$library" ]; then
        echo "ERROR: the $configuration build installed no $(basename "$library")."
        exit 1
    fi
    # The archives inside stay inside: a symbol of theirs in the dynamic table would bind another copy of the same
    # library in the process to this one.
    exported="$(nm -DC --defined-only "$library" | grep -E ' (pugi::|ZSTD_|ZDICT_|LZ4|deflate|inflate|crc32|adler32)' \
        || true)"
    if [ -n "$exported" ]; then
        echo "ERROR: $(basename "$library") exports symbols of the libraries it carries:"
        echo "$exported" | head -20
        exit 1
    fi
done

echo
echo "==== OpenXISF $OPENXISF_TAG is installed in $INSTALL_DIR ===="

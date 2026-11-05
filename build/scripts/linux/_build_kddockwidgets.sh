#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# Usage: _build_kddockwidgets.sh [release]
# Builds and installs both configurations, as 02-install_linux_deps.sh does for a development machine; `release` builds
# only the one a release-only build links, as the hosted workflows do.
CONFIGURATIONS=(Release Debug)
if [ $# -gt 0 ]; then
    if [ $# -ne 1 ] || [ "$1" != release ]; then
        echo "Usage: $0 [release]"
        exit 1
    fi
    CONFIGURATIONS=(Release)
fi

# === CONFIGURATION ===
QT_DIR="$(pwd)/../../../Qt6/6.10.3/gcc_64"
QT_DIR_CMAKE="$QT_DIR/lib/cmake/Qt6"
INSTALL_DIR="$(pwd)/../../../kddockwidgets/build-clang-install"
BUILD_DIR="$(pwd)/../../../kddockwidgets/build-clang"
SRC_DIR="$(pwd)/../../../kddockwidgets"

# === Verify Qt installation exists ===
if [ ! -d "$QT_DIR" ]; then
    echo "ERROR: Qt directory does not exist: $QT_DIR"
    exit 1
fi

if [ ! -d "$QT_DIR_CMAKE" ]; then
    echo "ERROR: Qt CMake directory does not exist: $QT_DIR_CMAKE"
    exit 1
fi

# === Clean old dirs if they exist ===
rm -rf "$BUILD_DIR" "$INSTALL_DIR"

# === CONFIGURE WITH CMAKE ===
echo "Configuring with CMake..."

cmake -G "Ninja Multi-Config" -B "$BUILD_DIR" -S "$SRC_DIR" \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DQt6_DIR="$QT_DIR_CMAKE" \
    -DKDDockWidgets_QT6=ON \
    -DKDDockWidgets_QTQUICK=OFF \
    -DKDDockWidgets_FLUTTER=OFF \
    -DKDDockWidgets_EXAMPLES=OFF \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DKDDockWidgets_FRONTENDS="qtwidgets"

# === BUILD & INSTALL EACH CONFIGURATION ===
for configuration in "${CONFIGURATIONS[@]}"; do
    echo "Building $configuration..."
    cmake --build "$BUILD_DIR" --config "$configuration" -j"$(nproc)"
    echo "Installing $configuration..."
    cmake --install "$BUILD_DIR" --config "$configuration"
done

# KDAB's CMake installs each library with the directories it was linked against - this host's Qt - in its run path,
# after $ORIGIN/../lib. A deployment carries Qt beside the library, so the run path keeps that entry alone and names no
# directory of the machine that built it.
echo "Setting the run path of the installed libraries..."
for library in "$INSTALL_DIR"/lib/libkddockwidgets*.so*; do
    [ -L "$library" ] && continue
    cmake -DFILE="$library" '-DRUN_PATH=$ORIGIN/../lib' -P "$(pwd)/set_run_path.cmake"
done

echo
echo "==== Build Finished Successfully ===="
echo "Libraries are installed in: $INSTALL_DIR"
echo

# List what was installed
if [ -d "$INSTALL_DIR" ]; then
    echo "Installed files:"
    find "$INSTALL_DIR"
fi

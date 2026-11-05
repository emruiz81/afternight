#!/bin/bash
set -e

PYTHON_VERSION=3.14.7
BUILD_DATE=20260814
PYTHON_SHA256=490953e4a7e97a6ed64d0f0bd04f2d4d9a7bb45368890d3fa32ba05d0ff5c48f
PYSIDE6_VERSION=6.10.3
PYTHON_TARBALL="cpython-${PYTHON_VERSION}+${BUILD_DATE}-x86_64-unknown-linux-gnu-install_only.tar.gz"
PYTHON_URL="https://github.com/astral-sh/python-build-standalone/releases/download/${BUILD_DATE}/${PYTHON_TARBALL}"

INSTALL_DIR="$(pwd)/../../../python/python/tools_linux"

rm -fr "$INSTALL_DIR"

mkdir -p "$INSTALL_DIR"
pushd "$INSTALL_DIR"

if [ ! -d "python" ]; then
    wget "$PYTHON_URL"
    printf '%s  %s\n' "$PYTHON_SHA256" "$PYTHON_TARBALL" | sha256sum --check --status
    tar -xzf "$PYTHON_TARBALL"
    mv python/* .
    rm -rf python "$PYTHON_TARBALL"
fi

# Create python symlink for packages that expect 'python' in shebang
ln -sf python3 bin/python

# Add Python bin to PATH for this session
export PATH="$(pwd)/bin:$PATH"

# Use Clang as the default C/C++ compiler for building Python packages
export CC=clang
export CXX=clang++

./bin/python3 -m ensurepip --upgrade
./bin/python3 -m pip install --no-user --ignore-installed --no-warn-script-location -U pip setuptools wheel
./bin/python3 -m pip install --no-user --ignore-installed --no-warn-script-location -U numpy scipy scikit-image appdirs virtualenv
./bin/python3 -m pip install --no-user --ignore-installed --no-warn-script-location --only-binary=:all: "PySide6==${PYSIDE6_VERSION}"

./bin/python3 -m compileall -x '.*\.tmpl\.py$' .
popd

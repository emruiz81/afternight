#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************05a-deploy_all_deps_release****************

./_ensure_afternight_not_running.sh

echo ****************Deploying Clang 3rdparty deps****************

cp -f ../../../3rdparty/bin/clang/release/*.so ../../../bin/clang/release/


# Ensure the destination directory exists and copy python*.so files
cp -f ../../../python/python/tools_linux/lib/*.so ../../../bin/clang/release/
cp -f ../../../python/python/tools_linux/lib/*.so.* ../../../bin/clang/release/

# Copy the bundled Python interpreter so extension environment provisioning
# can run against the deployed app layout without a dev-tree fallback. The
# bundled runtime is replaced whole: copied over an earlier deployment, it kept
# every package, distribution record and library its source has since dropped,
# and the AppImage ships what this directory holds.
rm -rf ../../../bin/clang/release/PythonLib
mkdir -p ../../../bin/clang/release/PythonLib/bin
for exe in python3.14 python3 python; do
    SRC=../../../python/python/tools_linux/bin/$exe
    if [ -f "$SRC" ]; then
        cp -f "$SRC" ../../../bin/clang/release/
        cp -f "$SRC" ../../../bin/clang/release/PythonLib/bin/
        chmod +x ../../../bin/clang/release/$exe || true
        chmod +x ../../../bin/clang/release/PythonLib/bin/$exe || true
    fi
done

# Ensure site-packages destination exists and copy everything
mkdir -p ../../../bin/clang/release/PythonLib/lib
cp -rf ../../../python/python/tools_linux/lib/* ../../../bin/clang/release/PythonLib/lib
PYTHON_SITE_PACKAGES=../../../bin/clang/release/PythonLib/lib/python3.14/site-packages
mkdir -p "$PYTHON_SITE_PACKAGES"
if [ -d ../../../python/modules ] && compgen -G "../../../python/modules/*" > /dev/null; then
    cp -rf ../../../python/modules/* "$PYTHON_SITE_PACKAGES/"
fi

for package in PySide6 shiboken6; do
    if [ ! -d "$PYTHON_SITE_PACKAGES/$package" ]; then
        echo "Error: deployed Python runtime is missing $package at $PYTHON_SITE_PACKAGES/$package"
        echo "Run build/scripts/python/setup_python_linux.sh, then rerun this deploy script."
        exit 1
    fi
done

echo ****************Deploying crashpad_handler****************
CRASHPAD_HANDLER=../../../vcpkg/installed/x64-linux/tools/crashpad/crashpad_handler
if [ -f "$CRASHPAD_HANDLER" ]; then
    cp -f "$CRASHPAD_HANDLER" ../../../bin/clang/release/
    chmod +x ../../../bin/clang/release/crashpad_handler || true
else
    echo "Warning: crashpad_handler not found at $CRASHPAD_HANDLER"
fi


echo ****************Deploying Clang Scripts****************
cp -f -r ../../../scripts ../../../bin/clang/release/

echo ****************Deploying Linux desktop integration files****************
# Copy multi-resolution app icons so run_AfterNight.sh can install them into
# the user's XDG icon theme for Wayland compositor icon lookup.
ICONS_DIR=../../../icons
BIN_DIR=../../../bin/clang/release

mkdir -p "$BIN_DIR/icons"
for size in 128 256 512; do
    SRC="${ICONS_DIR}/afternight_icon_${size}x${size}_blue.png"
    if [ -f "$SRC" ]; then
        cp -f "$SRC" "$BIN_DIR/icons/"
    fi
done
./deploy_extensions.sh release

#echo ****************Creating Clang dirs****************
#mkdir -p ../../../bin/clang/release/accessiblebridge/


echo ****************Deploying Clang binary deps****************

./_deploy_binaries.sh release


echo Done!

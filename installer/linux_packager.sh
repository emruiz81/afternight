#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

APP_NAME="AfterNight"
APP_VERSION="1.0.1"
DEPLOY_DIR="$SCRIPT_DIR/../bin/clang/release"
APPDIR="$SCRIPT_DIR/AppDir"
APPIMAGE_TOOL="$SCRIPT_DIR/appimagetool-x86_64.AppImage"
APPIMAGE_OUTPUT="$SCRIPT_DIR/$APP_NAME-$APP_VERSION-x86_64.AppImage"

# Clean old builds
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/128x128/apps"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/icons/hicolor/512x512/apps"

echo "[1/8] Copying application binary..."
# The application goes beside the libraries, as it is deployed: it looks beside itself for the bundled Python, both
# extension hosts, the bundled extensions and the crash handler, which the sweep below puts in usr/lib, so an application
# in usr/bin could provision no extension environment, start no host and report no crash.
# build/scripts/tools/check_installer_payload.py holds this copy and AppRun to the sweep's directory. usr/bin keeps a
# link to it for tools that look there.
cp "$DEPLOY_DIR/$APP_NAME" "$APPDIR/usr/lib/"
ln -s "../lib/$APP_NAME" "$APPDIR/usr/bin/$APP_NAME"

echo "[2/8] Copying libraries..."
# Copy everything except the main binary, debug symbol sidecars, the engine's build record, test and fuzz
# executables, the RAW qualification tools, the benchmark runners, the measurement driver, the optional Python
# adapter and the opt-in GPU backend into usr/lib.
# That keeps the engine and its G'MIC adapter, libAfterNightPlatform.so (the logger and the crash handler every
# binary loads), the consumer runtime, the libraries, both extension hosts, the command-line tool, the runtime backend,
# the style plugin and the crash handler process; build/scripts/tools/check_installer_payload.py holds the exclusions
# to both. A core dump is excluded at the top level only: rsync matches an unanchored pattern at every depth, and
# core.* would take the bundled Python's core.py modules, the extension SDK's among them. Qt Test is excluded at
# lib/ alone for the same reason: the bundled Python's PySide6 carries a copy of its own under PythonLib, which an
# unanchored pattern would take with it.
rsync -av --exclude="$APP_NAME" --exclude="*.debug" --exclude="*.log" --exclude="/core" --exclude="/core.*" --exclude="/vgcore.*" --exclude=".engine-instrumentation" --exclude="Test_*" --exclude="Fuzz_*" --exclude="/lib/libQt6Test*" --exclude="Raw*Bench*" --exclude="Raw*Oracle*" --exclude="RawRuntimeProbe*" --exclude="RawNativeTests*" --exclude="AfterNightEngineBench*" --exclude="AfterNightWorkflowDriver*" --exclude="libAfterNightEnginePythonAdapter*" --exclude="libAfterNightEngineOpenClBackend*" "$DEPLOY_DIR/" "$APPDIR/usr/lib/"

echo "[3/8] Creating .desktop entry..."
DESKTOP_FILE="$APPDIR/usr/share/applications/$APP_NAME.desktop"
mkdir -p "$(dirname "$DESKTOP_FILE")"

# Use the pre-built .desktop file from the installer directory
cp "$SCRIPT_DIR/$APP_NAME.desktop" "$DESKTOP_FILE"

# Copy .desktop also to top level (needed by AppImage spec)
cp "$DESKTOP_FILE" "$APPDIR/$APP_NAME.desktop"

echo "[4/8] Copying icons..."
ICON_DIR="$SCRIPT_DIR/../icons"

copy_icon() {
    local size="$1"
    local src="$ICON_DIR/afternight_icon_${size}x${size}_blue.png"
    local dst="$APPDIR/usr/share/icons/hicolor/${size}x${size}/apps/$APP_NAME.png"
    if [ -f "$src" ]; then
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
        echo "  Copied ${size}x${size} icon"
    else
        echo "  Warning: $src not found"
    fi
}

copy_icon 128
copy_icon 256
copy_icon 512

# Copy the best available icon to the AppDir root (required by AppImage spec)
if [ -f "$ICON_DIR/afternight_icon_512x512_blue.png" ]; then
    cp "$ICON_DIR/afternight_icon_512x512_blue.png" "$APPDIR/$APP_NAME.png"
elif [ -f "$ICON_DIR/afternight_icon_256x256_blue.png" ]; then
    cp "$ICON_DIR/afternight_icon_256x256_blue.png" "$APPDIR/$APP_NAME.png"
fi

echo "[5/8] Creating AppRun launcher..."
cat > "$APPDIR/AppRun" <<EOF
#!/bin/bash
HERE=\$(dirname "\$(readlink -f "\$0")")
export PATH="\$HERE/usr/bin:\$PATH"
export LD_LIBRARY_PATH="\$HERE/usr/lib:\$HERE/usr/lib/lib:\$LD_LIBRARY_PATH"
exec "\$HERE/usr/lib/$APP_NAME" "\$@"
EOF
chmod +x "$APPDIR/AppRun"

# The command-line tool has no AppRun of its own: its launcher has it load the package's libraries, as AppRun has the
# application load them. A run path reaches only a binary's own imports, so without the library path the third-party
# libraries in usr/lib/lib would take the host's copies of what they need.
cat > "$APPDIR/usr/bin/AfterNightCLI" <<EOF
#!/bin/bash
ROOT=\$(dirname "\$(readlink -f "\$0")")/../..
export LD_LIBRARY_PATH="\$ROOT/usr/lib:\$ROOT/usr/lib/lib:\$LD_LIBRARY_PATH"
exec "\$ROOT/usr/lib/AfterNightCLI" "\$@"
EOF
chmod +x "$APPDIR/usr/bin/AfterNightCLI"

# The deployment's qt.conf came with the sweep and names the plugins and Qt libraries beside the application.
echo "[6/8] Checking qt.conf beside the application..."
if [ ! -f "$APPDIR/usr/lib/qt.conf" ]; then
    echo "The deployment carries no qt.conf at $DEPLOY_DIR/qt.conf; run 05a-deploy_all_deps_release.sh first."
    exit 1
fi

echo "[7/8] Bundling offline documentation..."
DOCS_SITE="$SCRIPT_DIR/../docs/user-guide/site"
if [ -d "$DOCS_SITE" ]; then
    mkdir -p "$APPDIR/usr/share/doc/AfterNight"
    cp -r "$DOCS_SITE"/. "$APPDIR/usr/share/doc/AfterNight/"
elif [ "${REQUIRE_DOCS_SITE:-0}" = "1" ]; then
    echo "Documentation site is required for release packaging but was not found at $DOCS_SITE"
    exit 1
fi

echo "[8/8] Building AppImage..."
# Download appimagetool if missing
if [ ! -f "$APPIMAGE_TOOL" ]; then
    wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$APPIMAGE_TOOL"
fi

ARCH=x86_64 "$APPIMAGE_TOOL" "$APPDIR" "$APPIMAGE_OUTPUT"

echo "✅ Done! Generated: $APPIMAGE_OUTPUT"

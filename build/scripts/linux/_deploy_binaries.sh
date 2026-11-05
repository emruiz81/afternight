#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

APP_NAME="AfterNight"

# Check command line argument for build configuration
if [ $# -eq 0 ]; then
    echo "Usage: $0 [release|debug]"
    echo "Example: $0 release"
    echo "Example: $0 debug"
    exit 1
fi

BUILD_CONFIG="$1"

# Validate build configuration
if [ "$BUILD_CONFIG" != "release" ] && [ "$BUILD_CONFIG" != "debug" ]; then
    echo "Error: Build configuration must be 'release' or 'debug'"
    echo "Usage: $0 [release|debug]"
    exit 1
fi

BUILD_DIR="../../../bin/clang/$BUILD_CONFIG"

ENGINE_LIB_SOURCE="$BUILD_DIR/libAfterNightEngine.so"  # Update this path
PLATFORM_LIB_SOURCE="$BUILD_DIR/libAfterNightPlatform.so"
PROTOCOL_LIB_SOURCE="$BUILD_DIR/libAfterNightExtensionProtocol.so"

CRASHPAD_EXECUTABLE="$BUILD_DIR/crashpad_handler"  # Update this path

if [ "$BUILD_CONFIG" != "release" ]; then
    APP_NAME="AfterNight_d"
    ENGINE_LIB_SOURCE="$BUILD_DIR/libAfterNightEngine_d.so"  # Update this path
    PLATFORM_LIB_SOURCE="$BUILD_DIR/libAfterNightPlatform_d.so"
    PROTOCOL_LIB_SOURCE="$BUILD_DIR/libAfterNightExtensionProtocol_d.so"
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting Qt6 application deployment for $BUILD_CONFIG build...${NC}"

# Check if executable exists
if [ ! -f "$BUILD_DIR/$APP_NAME" ]; then
    echo -e "${RED}Error: Executable not found at $BUILD_DIR/$APP_NAME${NC}"
    echo "Please build the application first."
    exit 1
fi

# Ensure it's executable
chmod +x "$BUILD_DIR/$APP_NAME"

# Clean old deployment dirs
rm -rf "$BUILD_DIR/lib" "$BUILD_DIR/plugins"
mkdir -p "$BUILD_DIR/lib"

#######################################
# Custom Qt installation path
#######################################
### CHANGED: Set your Qt path here
QT_PATH="../../../Qt6/6.10.3/gcc_64"
QT_PLUGIN_PATH="$QT_PATH/plugins"
QT_LIB_PATH="$QT_PATH/lib"
# The build's Qt is what the dependency analysis resolves through, and what is deployed: without it the analysis would
# follow the distribution's Qt and bundle what that one needs, so the deployment stops rather than go on without it.
if ! QT_LIB_DIR="$(cd "$QT_LIB_PATH" 2>/dev/null && pwd)" || [ -z "$QT_LIB_DIR" ]; then
    echo "Error: the build's Qt libraries were not found at $QT_LIB_PATH; install Qt with 02-install_linux_deps.sh"
    exit 1
fi

#######################################
# Function to copy library dependencies
#######################################
copy_library_dependencies() {
    local binary="$1"
    local description="$2"
    
    echo -e "${YELLOW}Analyzing dependencies for $description...${NC}"
    
    # System libraries that should NOT be bundled
    local skip_libs=(
        "libc.so"
        "libpthread.so"
        "libdl.so"
        "librt.so"
        "libm.so"
        "libresolv.so"
        "libgcc_s.so"
        "libcrypt.so"
        #"libstdc++.so" Keep it if compiling from a modern enough linux, so we can ensure portability
    )
    
    # Get all dynamic libraries and filter system libs. The analysis resolves through the build's Qt, ahead of the
    # system's: a binary names lib/ beside it as its one run path, and lib/ is what is being filled, so without it the
    # analysis would follow the distribution's Qt and copy what that one needs - another ICU than the build's Qt needs.
    local libs=$(LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$binary" 2>/dev/null \
        | awk '{print $3}' | grep -E '^/' | sort -u)
    
    for lib in $libs; do
        local basename=$(basename "$lib")

        # Skip if in skip list
        for skip in "${skip_libs[@]}"; do
            if [[ "$basename" == "$skip"* ]]; then
                echo "  Skipped system lib: $basename"
                continue 2
            fi
        done

        # Prefer Qt libs from custom path
        if [[ "$basename" == *Qt6* ]] && [ -f "$QT_LIB_PATH/$basename" ]; then
            lib="$QT_LIB_PATH/$basename"
        fi

        if [ -f "$lib" ] && [ ! -f "$BUILD_DIR/lib/$basename" ]; then
            cp -u "$lib" "$BUILD_DIR/lib/"
            echo "  Copied: $basename"
        fi
    done
}

#######################################
# Copy ALL dynamic libraries for main executable
#######################################
echo -e "${YELLOW}Copying main executable libraries...${NC}"
copy_library_dependencies "$BUILD_DIR/$APP_NAME" "main executable"

#######################################
# Copy ALL dynamic libraries for engine library
#######################################
if [ -f "$ENGINE_LIB_SOURCE" ]; then
    #cp -u "$ENGINE_LIB_SOURCE" "$BUILD_DIR/lib/"
    copy_library_dependencies "$ENGINE_LIB_SOURCE" "AfterNightEngine library"
fi

#######################################
# Copy ALL dynamic libraries for the platform library, which holds the logger and the crash handler
#######################################
if [ -f "$PLATFORM_LIB_SOURCE" ]; then
    copy_library_dependencies "$PLATFORM_LIB_SOURCE" "AfterNightPlatform library"
fi

#######################################
# Copy protocol library and dependencies
#######################################
if [ -f "$PROTOCOL_LIB_SOURCE" ]; then
    cp -u "$PROTOCOL_LIB_SOURCE" "$BUILD_DIR/lib/"
    copy_library_dependencies "$PROTOCOL_LIB_SOURCE" "AfterNightExtensionProtocol library"
fi

#######################################
# Copy ALL dynamic libraries for crashpad
#######################################
if [ -f "$CRASHPAD_EXECUTABLE" ]; then
    #cp -u "$CRASHPAD_EXECUTABLE" "$BUILD_DIR/lib/"
    copy_library_dependencies "$CRASHPAD_EXECUTABLE" "Crashpad executable"
fi

#######################################
# Copy misc libraries sometimes not detected by the previous step
#######################################
echo -e "${YELLOW}Copying misc libraries...${NC}"
cp -u /usr/lib/x86_64-linux-gnu/libfftw3*.so* "$BUILD_DIR/lib/"

#######################################
# Copy kddockwidgets custom built library
#######################################
echo -e "${YELLOW}Copying KDDockWidgets library...${NC}"
cp -u ../../../kddockwidgets/build-clang-install/lib/*.so* "$BUILD_DIR/lib/"

#######################################
# Copy Qt plugins and their dependencies
#######################################
echo -e "${YELLOW}Copying Qt platform plugins...${NC}"
mkdir -p "$BUILD_DIR/plugins/platforms"
# Explicitly ensure libqoffscreen.so is copied (for headless tests/running)
if [ -f "$QT_PLUGIN_PATH/platforms/libqoffscreen.so" ]; then
    cp -u "$QT_PLUGIN_PATH/platforms/libqoffscreen.so" "$BUILD_DIR/plugins/platforms/"
    echo "  Copied: libqoffscreen.so"
fi
cp -u "$QT_PLUGIN_PATH/platforms/"* "$BUILD_DIR/plugins/platforms/" 2>/dev/null || true

# Copy dependencies for platform plugins
for plugin in "$BUILD_DIR/plugins/platforms/"*; do
    if [ -f "$plugin" ]; then
        copy_library_dependencies "$plugin" "platform plugin $(basename "$plugin")"
    fi
done

echo -e "${YELLOW}Copying Qt image/icon plugins...${NC}"
for plugin_dir in imageformats iconengines; do
    if [ -d "$QT_PLUGIN_PATH/$plugin_dir" ]; then
        mkdir -p "$BUILD_DIR/plugins/$plugin_dir"
        cp -u "$QT_PLUGIN_PATH/$plugin_dir/"* "$BUILD_DIR/plugins/$plugin_dir/"
        echo "Copied $plugin_dir plugins"
        
        for plugin in "$BUILD_DIR/plugins/$plugin_dir/"*; do
            if [ -f "$plugin" ]; then
                copy_library_dependencies "$plugin" "$plugin_dir plugin $(basename "$plugin")"
            fi
        done
    fi
done

echo -e "${YELLOW}Copying Main Qt deps...${NC}"

cp -u "$QT_LIB_PATH"/libQt6DBus*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Widgets*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Gui*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Network*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Egl*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6OpenGL*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Sql*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Svg*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Wayland*.so* "$BUILD_DIR/lib/"
#cp -u "$QT_LIB_PATH"/libQt6Shell*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6WlShell*.so* "$BUILD_DIR/lib/"
cp -u "$QT_LIB_PATH"/libQt6Xcb*.so* "$BUILD_DIR/lib/"

# Qt Test, which the suites beside the product load and no shipped binary does. A binary that links Qt finds it through
# one run path, lib/ beside it, so a suite would otherwise take the host distribution's build of another version; the
# packager leaves it out of the AppImage.
cp -u "$QT_LIB_PATH"/libQt6Test*.so* "$BUILD_DIR/lib/"

echo -e "${YELLOW}Copying Qt SQL drivers (SQLite)...${NC}"
mkdir -p "$BUILD_DIR/plugins/sqldrivers"
if cp -u "$QT_PLUGIN_PATH/sqldrivers/libqsqlite.so" "$BUILD_DIR/plugins/sqldrivers/" 2>/dev/null; then
    echo "Copied libqsqlite.so"
    copy_library_dependencies "$BUILD_DIR/plugins/sqldrivers/libqsqlite.so" "SQLite plugin"
else
    echo -e "${RED}Warning: Could not copy libqsqlite.so${NC}"
fi

# Copy any other SQL drivers
for sql_driver in "$QT_PLUGIN_PATH/sqldrivers/"*; do
    if [ -f "$sql_driver" ] && [ "$(basename "$sql_driver")" != "libqsqlite.so" ]; then
        cp -u "$sql_driver" "$BUILD_DIR/plugins/sqldrivers/" 2>/dev/null || true
        echo "Copied $(basename "$sql_driver")"
        copy_library_dependencies "$BUILD_DIR/plugins/sqldrivers/$(basename "$sql_driver")" "SQL driver $(basename "$sql_driver")"
    fi
done

#######################################
# Copy other common Qt plugin directories
#######################################
echo -e "${YELLOW}Checking for other Qt plugins...${NC}"
for plugin_dir in tls platformthemes wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client xcbglintegrations egldeviceintegrations; do
    if [ -d "$QT_PLUGIN_PATH/$plugin_dir" ]; then
        mkdir -p "$BUILD_DIR/plugins/$plugin_dir"
        cp -u "$QT_PLUGIN_PATH/$plugin_dir/"* "$BUILD_DIR/plugins/$plugin_dir/" 2>/dev/null || true
        echo "Copied $plugin_dir plugins"
        
        for plugin in "$BUILD_DIR/plugins/$plugin_dir/"*; do
            if [ -f "$plugin" ]; then
                copy_library_dependencies "$plugin" "$plugin_dir plugin $(basename "$plugin")"
            fi
        done
    fi
done

#######################################
# Final pass: resolve any remaining dependencies
#######################################
echo -e "${YELLOW}Final dependency resolution pass...${NC}"
for lib in "$BUILD_DIR/lib/"*; do
    if [ -f "$lib" ]; then
        copy_library_dependencies "$lib" "library $(basename "$lib")"
    fi
done

#######################################
# Create qt.conf
#######################################
echo -e "${YELLOW}Creating qt.conf...${NC}"
cat > "$BUILD_DIR/qt.conf" << EOF
[Paths]
Plugins = plugins
Libraries = lib
EOF

#######################################
# Create run script
#######################################
echo -e "${YELLOW}Creating launch script...${NC}"
cat > "$BUILD_DIR/run_$APP_NAME.sh" << 'LAUNCHER_EOF'
#!/bin/bash
# Launcher for AfterNight – registers the .desktop file and icon into the
# user's local XDG directories so that Wayland compositors can look up the
# correct icon via the XDG icon theme, then starts the application.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ensure_utf8_ctype() {
    local charmap normalized candidate candidate_normalized utf8_locale=""
    charmap="$(locale charmap 2>/dev/null || true)"
    normalized="${charmap//[-_]/}"
    if [[ "${normalized^^}" == "UTF8" ]]; then
        return
    fi

    while IFS= read -r candidate; do
        candidate_normalized="${candidate//[-_.]/}"
        if [[ "${candidate_normalized^^}" == *"UTF8"* ]]; then
            utf8_locale="$candidate"
            break
        fi
    done < <(locale -a 2>/dev/null || true)

    if [[ -z "$utf8_locale" ]]; then
        echo "No UTF-8 locale is installed; AfterNight requires a UTF-8 character environment." >&2
        exit 1
    fi

    unset LC_ALL
    export LC_CTYPE="$utf8_locale"
}

ensure_utf8_ctype

# Register desktop entry and icon sizes for Wayland app_id → icon resolution
XDG_DATA="${HOME}/.local/share"
DESKTOP_DIR="${XDG_DATA}/applications"
ICON_BASE="${XDG_DATA}/icons/hicolor"

mkdir -p "${DESKTOP_DIR}" \
         "${ICON_BASE}/128x128/apps" \
         "${ICON_BASE}/256x256/apps" \
         "${ICON_BASE}/512x512/apps"

# Install the desktop file (override Exec to use the absolute launcher path)
if [ -f "${SCRIPT_DIR}/AfterNight.desktop" ]; then
    sed "s|^Exec=.*|Exec=${SCRIPT_DIR}/run_AfterNight.sh|" \
        "${SCRIPT_DIR}/AfterNight.desktop" > "${DESKTOP_DIR}/AfterNight.desktop"
fi

# Install icon sizes
for size in 128 256 512; do
    src="${SCRIPT_DIR}/icons/afternight_icon_${size}x${size}_blue.png"
    if [ -f "${src}" ]; then
        cp -f "${src}" "${ICON_BASE}/${size}x${size}/apps/AfterNight.png"
    fi
done

# Refresh caches if tools are available
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t "${ICON_BASE}" 2>/dev/null || true
fi
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "${DESKTOP_DIR}" 2>/dev/null || true
fi

export LD_LIBRARY_PATH="${SCRIPT_DIR}:${SCRIPT_DIR}/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${SCRIPT_DIR}/plugins"
export QT_QPA_PLATFORMTHEME="${QT_QPA_PLATFORMTHEME:-xdgdesktopportal}"
exec "${SCRIPT_DIR}/__AFTERNIGHT_EXE__" "$@"
LAUNCHER_EOF

# Substitute the actual executable name for the placeholder
sed -i "s/__AFTERNIGHT_EXE__/$APP_NAME/" "$BUILD_DIR/run_$APP_NAME.sh"

chmod +x "$BUILD_DIR/run_$APP_NAME.sh"

#######################################
# Create .desktop entry
#######################################
# Use the installer's pre-built .desktop file if available; otherwise generate
# a minimal fallback so the AppImage / system install always works.
echo -e "${YELLOW}Creating desktop entry...${NC}"
INSTALLER_DESKTOP="$(dirname "${BASH_SOURCE[0]}")/../../../installer/AfterNight.desktop"
if [ -f "$INSTALLER_DESKTOP" ]; then
    cp -f "$INSTALLER_DESKTOP" "$BUILD_DIR/$APP_NAME.desktop"
else
    cat > "$BUILD_DIR/$APP_NAME.desktop" << EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
Comment=Astrophotography processing tool
Exec=./run_$APP_NAME.sh
Icon=$APP_NAME
Categories=Graphics;Photography;Science;
Terminal=false
StartupWMClass=$APP_NAME
StartupNotify=true
EOF
fi

#######################################
echo -e "${GREEN}Deployment complete!${NC}"
echo -e "Everything is inside: ${YELLOW}$BUILD_DIR${NC}"

# Summary
echo -e "\n${GREEN}Summary:${NC}"
echo "Libraries copied: $(ls "$BUILD_DIR/lib/" | wc -l)"
echo "Platform plugins: $(ls "$BUILD_DIR/plugins/platforms/" 2>/dev/null | wc -l)"
echo "Platform themes: $(ls "$BUILD_DIR/plugins/platformthemes/" 2>/dev/null | wc -l)"
echo "Image plugins: $(ls "$BUILD_DIR/plugins/imageformats/" 2>/dev/null | wc -l)"
echo "SQL drivers: $(ls "$BUILD_DIR/plugins/sqldrivers/" 2>/dev/null | wc -l)"

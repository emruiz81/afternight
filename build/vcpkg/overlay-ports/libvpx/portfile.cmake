# Keep the checkout port's platform integration while pinning the security-reviewed
# release used by the Linux FFmpeg build. The 1.17.0 tree is fetched here with the
# checkout port's patches applied, and the override makes the included port use it:
# the override drops the arguments of the port's own fetch, its patch list with them,
# so the list is repeated here. Without 0006 the Windows build stops on the first
# compiler flag the project generator does not know ("Unknown option -O3").
set(AFTERNIGHT_LIBVPX_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../vcpkg/ports/libvpx")
vcpkg_from_github(
    OUT_SOURCE_PATH AFTERNIGHT_LIBVPX_SOURCE_PATH
    REPO webmproject/libvpx
    REF "v${VERSION}"
    SHA512 be98d1b913575e538fefefe657c9944df85118fe2d0558361495a72c75ac6a2251048cb645fca49e60d6941c823a923f31d4ebc4de9b778008c267c42805b339
    HEAD_REF master
    PATCHES
        "${AFTERNIGHT_LIBVPX_PORT_DIR}/0003-add-uwp-v142-and-v143-support.patch"
        "${AFTERNIGHT_LIBVPX_PORT_DIR}/0004-remove-library-suffixes.patch"
        "${AFTERNIGHT_LIBVPX_PORT_DIR}/0005-dont-expect-gnu-diff.patch"
        "${AFTERNIGHT_LIBVPX_PORT_DIR}/0006-gen-vcxproj-ignore-unknown-flags.patch"
        "${AFTERNIGHT_LIBVPX_PORT_DIR}/0007-msvc-use-Fo-in-toolchain-probe.patch"
)

macro(vcpkg_from_github)
    set(SOURCE_PATH "${AFTERNIGHT_LIBVPX_SOURCE_PATH}")
endmacro()

set(AFTERNIGHT_LIBVPX_OVERLAY_DIR "${CURRENT_PORT_DIR}")
set(CURRENT_PORT_DIR "${AFTERNIGHT_LIBVPX_PORT_DIR}")
include("${CURRENT_PORT_DIR}/portfile.cmake")
set(CURRENT_PORT_DIR "${AFTERNIGHT_LIBVPX_OVERLAY_DIR}")

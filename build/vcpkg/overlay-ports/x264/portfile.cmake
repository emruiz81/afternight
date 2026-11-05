# Keep the checkout port's source, version and platform patches, and add one repair of the source it fetches: the
# AVX-512 macroblock-tree propagation reads past the row it is given (see README.md). The checkout port's call is
# `vcpkg_from_gitlab(... PATCHES <its patches>)` with PATCHES last, so an argument appended to it is one more patch;
# CURRENT_PORT_DIR points the checkout port's own patches and template at the checkout port, as the libvpx overlay
# does, so this overlay forks none of them.
set(AFTERNIGHT_X264_OVERLAY_DIR "${CURRENT_PORT_DIR}")

macro(vcpkg_from_gitlab)
    set(afternight_x264_arguments ${ARGN})
    list(GET afternight_x264_arguments -1 afternight_x264_last)
    if(NOT afternight_x264_last MATCHES "\\.patch$")
        message(FATAL_ERROR "the x264 overlay expects the checkout port to list its patches last")
    endif()
    _vcpkg_from_gitlab(${ARGN} "${AFTERNIGHT_X264_OVERLAY_DIR}/avx512-propagate-list-tail.patch")
endmacro()

set(CURRENT_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../vcpkg/ports/x264")
include("${CURRENT_PORT_DIR}/portfile.cmake")
set(CURRENT_PORT_DIR "${AFTERNIGHT_X264_OVERLAY_DIR}")

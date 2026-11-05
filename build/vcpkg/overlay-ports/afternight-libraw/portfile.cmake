vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO LibRaw/LibRaw
    REF 0.22.1
    SHA512 123050ea30366ada37b40e0aee84453f71f10a5e5e39261a1d16b96dc395f85a9ecdfd043d51b4c347a67546affdfa7ca84c10fa84d73b9b4070c074f1d301e8
    PATCHES 0001-private-bridge.patch 0002-zero-jpeg-difference.patch 0003-canon-sraw-scaling.patch
)
vcpkg_from_github(
    OUT_SOURCE_PATH CMAKE_SOURCE
    REPO LibRaw/LibRaw-cmake
    REF eb98e4325aef2ce85d2eb031c2ff18640ca616d3
    SHA512 63e68a4d30286ec3aa97168d46b7a1199268099ae27b61abcc92e93ec30e48d364086227983a1d724415e5f4da44d905422f30192453b95f31040e5f8469c3f9
    PATCHES dependencies.patch
)
file(COPY "${CMAKE_SOURCE}/CMakeLists.txt" "${CMAKE_SOURCE}/cmake" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/AnRawBackend.h" DESTINATION "${SOURCE_PATH}/libraw")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/AnRawBackend.cpp" DESTINATION "${SOURCE_PATH}/src/utils")
vcpkg_cmake_configure(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}" OPTIONS "-DRAW_SOURCE=${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME AfterNightLibRaw CONFIG_PATH share/afternight-libraw)
vcpkg_copy_pdbs()
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/afternight-libraw/libraw/libraw_types.h" "#ifdef LIBRAW_NODLL" "#if 1")
else()
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/afternight-libraw/libraw/libraw_types.h" "#ifdef LIBRAW_NODLL" "#if 0")
endif()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
file(INSTALL "${SOURCE_PATH}/" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/source")
foreach(RECIPE AnRawBackend.cpp AnRawBackend.h CMakeLists.txt README.md 0001-private-bridge.patch 0002-zero-jpeg-difference.patch 0003-canon-sraw-scaling.patch dependencies.patch)
    file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/${RECIPE}" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/recipe")
endforeach()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYRIGHT" "${SOURCE_PATH}/LICENSE.LGPL" "${SOURCE_PATH}/LICENSE.CDDL")

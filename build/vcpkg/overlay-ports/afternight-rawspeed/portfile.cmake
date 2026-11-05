vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO darktable-org/rawspeed
    REF de70ef5fbc62cde91009c8cff7a206272abe631e
    SHA512 0305bb219f7dc98b99f75039086495ddab1230b12f77e2759772531d95dbdd02ac36dc3f0e84ea1631a3ee13c1c9dbaed3bfad632ff383ebd62d36427da727bb
    PATCHES 0001-libraw-compatibility.patch 0002-cancellation.patch 0003-sony-byte-alignment.patch
)
vcpkg_from_github(
    OUT_SOURCE_PATH LIBRAW_SOURCE
    REPO LibRaw/LibRaw
    REF 0.22.1
    SHA512 123050ea30366ada37b40e0aee84453f71f10a5e5e39261a1d16b96dc395f85a9ecdfd043d51b4c347a67546affdfa7ca84c10fa84d73b9b4070c074f1d301e8
)
vcpkg_cmake_configure(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}" OPTIONS "-DRAW_SOURCE=${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME AfterNightRawSpeed CONFIG_PATH share/afternight-rawspeed)
vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
# Preserve the complete upstream licence inventory, camera database and source tree. The binary
# package is also a source-distribution artifact; no external experiment checkout is required.
file(INSTALL "${SOURCE_PATH}/" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/source")
foreach(RECIPE AnRawSpeed.cpp AnRawSpeed.h AnRawSpeedTests.cpp CMakeLists.txt README.md 0001-libraw-compatibility.patch 0002-cancellation.patch 0003-sony-byte-alignment.patch)
    file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/${RECIPE}" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/recipe")
endforeach()
file(INSTALL "${LIBRAW_SOURCE}/COPYRIGHT" "${LIBRAW_SOURCE}/LICENSE.LGPL" "${LIBRAW_SOURCE}/LICENSE.CDDL"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/libraw-wrapper-notices")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

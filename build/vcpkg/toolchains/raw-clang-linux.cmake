include("${CMAKE_CURRENT_LIST_DIR}/../../../vcpkg/scripts/toolchains/linux.cmake")
set(CMAKE_C_COMPILER /usr/bin/clang-21 CACHE FILEPATH "Nominated RAW C compiler")
set(CMAKE_CXX_COMPILER /usr/bin/clang++-21 CACHE FILEPATH "Nominated RAW C++ compiler")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

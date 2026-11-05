# Sets the run path of one ELF file through CMake's own editor, for a library a third party's CMake installed with the
# directories it was linked against in its run path.
#
# Usage: cmake -DFILE=<file> "-DRUN_PATH=<entries>" -P set_run_path.cmake
file(RPATH_SET FILE "${FILE}" NEW_RPATH "${RUN_PATH}")

# The OpenCL proof backend's own dependency tree, and nothing else's.
#
# The GPU backend is opt-in and off by default, so the headers and import library it compiles against
# must never appear in a directory a default project includes. A triplet of their own puts them under
# vcpkg/installed/x64-windows-opencl, which no default project names; install_opencl_backend_deps.bat
# is the only thing that installs into it, and 02-install_msvc_deps.bat never does.
#
# The settings are the stock x64-windows ones. Dynamic linkage matters: the backend imports OpenCL.dll
# and loads the operating system's installed ICD loader at run time, so the loader built here is used
# for its import library only and is never deployed.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

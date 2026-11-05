@echo off
setlocal

REM Installs what the opt-in OpenCL proof backend compiles against, and nothing else.
REM
REM This is deliberately not part of 02-install_msvc_deps.bat. The GPU backend is a separately
REM compiled, non-shipping module that is off by default, and a contributor who never runs this script
REM generates, builds, tests, deploys and packages everything else with every check passing.
REM
REM It installs the Khronos OpenCL headers and ICD loader from the repository's pinned vcpkg checkout
REM into their own triplet, x64-windows-opencl, so no default project's include or library directory
REM can reach them. The backend links the loader's import library and, at run time, loads the
REM OpenCL.dll the graphics driver installed in the system directory; the loader built here is never
REM deployed.
REM
REM Afterwards: set AFTERNIGHT_OPENCL=1, run 01-create_project_files.bat, then
REM build_afternightengine.bat release --with-tests.

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

pushd "%~dp0..\..\..\vcpkg"
vcpkg install opencl --vcpkg-root=. --triplet=x64-windows-opencl --overlay-triplets=..\build\vcpkg\triplets --recurse %*
set "AN_OPENCL_EXIT=%ERRORLEVEL%"
popd
exit /b %AN_OPENCL_EXIT%

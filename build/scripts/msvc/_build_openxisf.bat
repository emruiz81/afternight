@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM Usage: _build_openxisf.bat [release]
REM
REM Builds OpenXISF, the XISF codec's library, from its published release as a shared library and installs it into
REM openxisf\build-msvc-install: the DLL under bin, the import library under lib and the headers under include. The
REM release's tag is cloned into openxisf\src and refused unless it is the commit pinned below, and the library is
REM built by its own CMake project against the vcpkg ports AFTERNIGHT_VCPKG_TRIPLET names (x64-windows by default),
REM so its pugixml, LZ4, Zstandard and zlib are the ones the rest of the build deploys. Both configurations are built;
REM "release" builds the Release one alone, as continuous integration does on its release-only triplet.
REM 02-install_msvc_deps.bat runs this, and 04a and 04b deploy the DLL.
REM
REM This is how the build acquires OpenXISF until the vcpkg registry carries it. The Linux script,
REM build/scripts/linux/_build_openxisf.sh, pins the same release, and check_dependency_provenance.py holds both to
REM the record in build/scripts/tools/dependencies.txt.
set "OPENXISF_REPOSITORY=https://github.com/astroformats/openxisf.git"
set "OPENXISF_TAG=v1.1.0"
set "OPENXISF_COMMIT=ceca3adcfc06b66face78dd22a1422cc3ccd85cd"

set "CONFIGURATIONS=Release Debug"
if not "%~1"=="" (
    if /I not "%~1"=="release" (
        echo Usage: _build_openxisf.bat [release]
        exit /b 2
    )
    set "CONFIGURATIONS=Release"
)
if not defined AFTERNIGHT_VCPKG_TRIPLET set "AFTERNIGHT_VCPKG_TRIPLET=x64-windows"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "VCPKG_DIR=%ROOT%\vcpkg"
set "SRC_DIR=%ROOT%\openxisf\src"
set "BUILD_DIR=%ROOT%\openxisf\build-msvc"
set "INSTALL_DIR=%ROOT%\openxisf\build-msvc-install"

set "CMAKE_EXE="
where cmake >nul 2>nul
if not errorlevel 1 set "CMAKE_EXE=cmake"
if not defined CMAKE_EXE if defined VSINSTALLDIR if exist "%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
    echo ERROR: CMake was not found on PATH or in the Visual Studio CMake install location.
    exit /b 1
)

if not exist "%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: the vcpkg submodule is not checked out at %VCPKG_DIR%.
    exit /b 1
)
if not exist "%VCPKG_DIR%\installed\%AFTERNIGHT_VCPKG_TRIPLET%\share\pugixml\" (
    echo ERROR: the vcpkg ports OpenXISF is built against are not installed for %AFTERNIGHT_VCPKG_TRIPLET%.
    echo Run 02-install_msvc_deps.bat first.
    exit /b 1
)

REM A tree that installed OpenXISF as a vcpkg port before this script took its place removes the port: its headers
REM and its import library sit in directories every project searches, where they could be found ahead of these.
if exist "%VCPKG_DIR%\installed\%AFTERNIGHT_VCPKG_TRIPLET%\share\openxisf\" (
    echo Removing the vcpkg port openxisf, which this build replaces...
    "%VCPKG_DIR%\vcpkg.exe" remove openxisf --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root="%VCPKG_DIR%"
    if errorlevel 1 exit /b 1
)

REM The source is the release's tag, verified to be the pinned commit. A checkout already at that commit and
REM unmodified is kept; anything else is cloned afresh.
set "FETCHED="
set "MODIFIED="
if exist "%SRC_DIR%\.git" (
    for /f "usebackq delims=" %%C in (`git -C "%SRC_DIR%" rev-parse HEAD 2^>nul`) do set "FETCHED=%%C"
    for /f "usebackq delims=" %%M in (`git -C "%SRC_DIR%" status --porcelain 2^>nul`) do set "MODIFIED=1"
)
if /I "%FETCHED%"=="%OPENXISF_COMMIT%" if not defined MODIFIED goto configure
if exist "%SRC_DIR%" (
    attrib -r "%SRC_DIR%\*" /s /d >nul 2>nul
    rmdir /s /q "%SRC_DIR%"
)
echo Fetching OpenXISF %OPENXISF_TAG%...
git -c advice.detachedHead=false clone --quiet --depth 1 --branch %OPENXISF_TAG% %OPENXISF_REPOSITORY% "%SRC_DIR%"
if errorlevel 1 (
    echo ERROR: cloning %OPENXISF_TAG% of %OPENXISF_REPOSITORY% failed.
    exit /b 1
)
set "FETCHED="
for /f "usebackq delims=" %%C in (`git -C "%SRC_DIR%" rev-parse HEAD`) do set "FETCHED=%%C"
if /I not "%FETCHED%"=="%OPENXISF_COMMIT%" (
    echo ERROR: %OPENXISF_TAG% of %OPENXISF_REPOSITORY% is %FETCHED%, not the pinned %OPENXISF_COMMIT%.
    exit /b 1
)

:configure
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%INSTALL_DIR%" rmdir /s /q "%INSTALL_DIR%"

REM The library's tests, samples, benchmarks, fuzz targets and documentation are its own repository's business;
REM vcpkg's classic mode supplies the ports, and the repository's own manifest is not read.
echo Configuring OpenXISF with %CMAKE_EXE%...
"%CMAKE_EXE%" -B "%BUILD_DIR%" -S "%SRC_DIR%" -G "Visual Studio 18 2026" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%AFTERNIGHT_VCPKG_TRIPLET% ^
    -DVCPKG_MANIFEST_MODE=OFF ^
    -DVCPKG_APPLOCAL_DEPS=OFF ^
    -DBUILD_SHARED_LIBS=ON ^
    -DOPENXISF_BUILD_TESTS=OFF ^
    -DOPENXISF_BUILD_SAMPLES=OFF ^
    -DOPENXISF_BUILD_BENCHMARKS=OFF ^
    -DOPENXISF_BUILD_FUZZERS=OFF ^
    -DOPENXISF_BUILD_DOCS=OFF ^
    -DOPENXISF_INSTALL=ON ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DCMAKE_C_FLAGS_INIT="/MP" ^
    -DCMAKE_CXX_FLAGS_INIT="/MP"
if errorlevel 1 (
    echo ERROR: configuring OpenXISF failed.
    exit /b 1
)

for %%C in (%CONFIGURATIONS%) do (
    echo Building and installing %%C...
    "%CMAKE_EXE%" --build "%BUILD_DIR%" --config %%C -- /maxcpucount
    if errorlevel 1 exit /b 1
    "%CMAKE_EXE%" --install "%BUILD_DIR%" --config %%C
    if errorlevel 1 exit /b 1
)

if not exist "%INSTALL_DIR%\bin\openxisf-1.1.dll" (
    echo ERROR: the Release build installed no openxisf-1.1.dll.
    exit /b 1
)
echo %CONFIGURATIONS% | findstr /i "Debug" >nul
if not errorlevel 1 if not exist "%INSTALL_DIR%\bin\openxisf-1.1d.dll" (
    echo ERROR: the Debug build installed no openxisf-1.1d.dll.
    exit /b 1
)

echo.
echo ==== OpenXISF %OPENXISF_TAG% is installed in %INSTALL_DIR% ====
exit /b 0

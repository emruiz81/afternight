@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM Usage: _build_openfits.bat [release]
REM
REM Builds OpenFITS, the FITS codec's library, from its published release as a shared library and installs it into
REM openfits\build-msvc-install: the DLL under bin, the import library under lib and the headers under include. The
REM release's tag is cloned into openfits\src and refused unless it is the commit pinned below, and the library is
REM built by its own CMake project against the vcpkg ports AFTERNIGHT_VCPKG_TRIPLET names (x64-windows by default),
REM so its zlib is the one the rest of the build deploys. It is built without its optional oneTBB: the engine runs
REM parallel work only through its own scheduler, under each operation's grant, so the library decompresses and
REM compresses tiles on the thread that calls it. Both configurations are built; "release" builds the Release one
REM alone, as continuous integration does on its release-only triplet.
REM 02-install_msvc_deps.bat runs this, and 04a and 04b deploy the DLL.
REM
REM This is how the build acquires OpenFITS until the vcpkg registry carries it. The Linux script,
REM build/scripts/linux/_build_openfits.sh, pins the same release, and check_dependency_provenance.py holds both to
REM the record in build/scripts/tools/dependencies.txt.
set "OPENFITS_REPOSITORY=https://github.com/astroformats/openfits.git"
set "OPENFITS_TAG=v1.0.0"
set "OPENFITS_COMMIT=8f22a0befbd4ccac5369cc1f2ed875ff8670b90a"

set "CONFIGURATIONS=Release Debug"
if not "%~1"=="" (
    if /I not "%~1"=="release" (
        echo Usage: _build_openfits.bat [release]
        exit /b 2
    )
    set "CONFIGURATIONS=Release"
)
if not defined AFTERNIGHT_VCPKG_TRIPLET set "AFTERNIGHT_VCPKG_TRIPLET=x64-windows"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "VCPKG_DIR=%ROOT%\vcpkg"
set "SRC_DIR=%ROOT%\openfits\src"
set "BUILD_DIR=%ROOT%\openfits\build-msvc"
set "INSTALL_DIR=%ROOT%\openfits\build-msvc-install"

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
if not exist "%VCPKG_DIR%\installed\%AFTERNIGHT_VCPKG_TRIPLET%\share\zlib\" (
    echo ERROR: the vcpkg port OpenFITS is built against is not installed for %AFTERNIGHT_VCPKG_TRIPLET%.
    echo Run 02-install_msvc_deps.bat first.
    exit /b 1
)

REM The source is the release's tag, verified to be the pinned commit. A checkout already at that commit and
REM unmodified is kept; anything else is cloned afresh.
set "FETCHED="
set "MODIFIED="
if exist "%SRC_DIR%\.git" (
    for /f "usebackq delims=" %%C in (`git -C "%SRC_DIR%" rev-parse HEAD 2^>nul`) do set "FETCHED=%%C"
    for /f "usebackq delims=" %%M in (`git -C "%SRC_DIR%" status --porcelain 2^>nul`) do set "MODIFIED=1"
)
if /I "%FETCHED%"=="%OPENFITS_COMMIT%" if not defined MODIFIED goto configure
if exist "%SRC_DIR%" (
    attrib -r "%SRC_DIR%\*" /s /d >nul 2>nul
    rmdir /s /q "%SRC_DIR%"
)
echo Fetching OpenFITS %OPENFITS_TAG%...
git -c advice.detachedHead=false clone --quiet --depth 1 --branch %OPENFITS_TAG% %OPENFITS_REPOSITORY% "%SRC_DIR%"
if errorlevel 1 (
    echo ERROR: cloning %OPENFITS_TAG% of %OPENFITS_REPOSITORY% failed.
    exit /b 1
)
set "FETCHED="
for /f "usebackq delims=" %%C in (`git -C "%SRC_DIR%" rev-parse HEAD`) do set "FETCHED=%%C"
if /I not "%FETCHED%"=="%OPENFITS_COMMIT%" (
    echo ERROR: %OPENFITS_TAG% of %OPENFITS_REPOSITORY% is %FETCHED%, not the pinned %OPENFITS_COMMIT%.
    exit /b 1
)

:configure
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%INSTALL_DIR%" rmdir /s /q "%INSTALL_DIR%"

REM The library's tests, samples, benchmarks, fuzz targets and documentation are its own repository's business;
REM vcpkg's classic mode supplies the port, and the repository's own manifest is not read.
echo Configuring OpenFITS with %CMAKE_EXE%...
"%CMAKE_EXE%" -B "%BUILD_DIR%" -S "%SRC_DIR%" -G "Visual Studio 18 2026" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=%AFTERNIGHT_VCPKG_TRIPLET% ^
    -DVCPKG_MANIFEST_MODE=OFF ^
    -DVCPKG_APPLOCAL_DEPS=OFF ^
    -DBUILD_SHARED_LIBS=ON ^
    -DOPENFITS_BUILD_TESTS=OFF ^
    -DOPENFITS_BUILD_SAMPLES=OFF ^
    -DOPENFITS_BUILD_BENCHMARKS=OFF ^
    -DOPENFITS_BUILD_FUZZERS=OFF ^
    -DOPENFITS_BUILD_DOCS=OFF ^
    -DOPENFITS_WITH_TBB=OFF ^
    -DOPENFITS_INSTALL=ON ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DCMAKE_C_FLAGS_INIT="/MP" ^
    -DCMAKE_CXX_FLAGS_INIT="/MP"
if errorlevel 1 (
    echo ERROR: configuring OpenFITS failed.
    exit /b 1
)

for %%C in (%CONFIGURATIONS%) do (
    echo Building and installing %%C...
    "%CMAKE_EXE%" --build "%BUILD_DIR%" --config %%C -- /maxcpucount
    if errorlevel 1 exit /b 1
    "%CMAKE_EXE%" --install "%BUILD_DIR%" --config %%C
    if errorlevel 1 exit /b 1
)

if not exist "%INSTALL_DIR%\bin\openfits-1.0.dll" (
    echo ERROR: the Release build installed no openfits-1.0.dll.
    exit /b 1
)
echo %CONFIGURATIONS% | findstr /i "Debug" >nul
if not errorlevel 1 if not exist "%INSTALL_DIR%\bin\openfits-1.0d.dll" (
    echo ERROR: the Debug build installed no openfits-1.0d.dll.
    exit /b 1
)

echo.
echo ==== OpenFITS %OPENFITS_TAG% is installed in %INSTALL_DIR% ====
exit /b 0

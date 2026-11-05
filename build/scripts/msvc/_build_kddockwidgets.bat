@echo off
setlocal
cd /d "%~dp0"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

:: === CONFIGURATION ===
:: Qt 6.10.3 MSVC package is currently named msvc2022_64 (compatible with VS2026)
set QT_DIR=%cd%\..\..\..\Qt6\6.10.3\msvc2022_64
set QT_DIR_CMAKE=%QT_DIR%\lib\cmake\Qt6
set INSTALL_DIR=%cd%\..\..\..\kddockwidgets\build-msvc-install
set BUILD_DIR=%cd%\..\..\..\kddockwidgets\build-msvc
set SRC_DIR=%cd%\..\..\..\kddockwidgets

set "CMAKE_EXE="
where cmake >nul 2>nul
if not errorlevel 1 set "CMAKE_EXE=cmake"
if not defined CMAKE_EXE if defined VSINSTALLDIR if exist "%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles(x86)%\CMake\bin\cmake.exe"

if not defined CMAKE_EXE (
    echo ERROR: CMake was not found on PATH or in the Visual Studio CMake install location.
    echo Install the "C++ CMake tools for Windows" Visual Studio component or add CMake to PATH.
    pause
    exit /b 1
)

:: Verify Qt installation exists
if not exist "%QT_DIR%" (
    echo ERROR: Qt directory does not exist: %QT_DIR%
    pause
    exit /b 1
)

if not exist "%QT_DIR_CMAKE%" (
    echo ERROR: Qt CMake directory does not exist: %QT_DIR_CMAKE%
    pause
    exit /b 1
)

:: Clean old dirs if they exist
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%INSTALL_DIR%" rmdir /s /q "%INSTALL_DIR%"

:: === CONFIGURE WITH CMAKE ===
echo Configuring with CMake...
echo Using CMake: %CMAKE_EXE%

set CMAKE_PREFIX_PATH=%QT_DIR%
set Qt6_DIR=%QT_DIR_CMAKE%

"%CMAKE_EXE%" -B "%BUILD_DIR%" -S "%SRC_DIR%" ^
    -G "Visual Studio 18 2026" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DQt6_DIR="%QT_DIR_CMAKE%" ^
    -DKDDockWidgets_QT6=ON ^
    -DKDDockWidgets_QTQUICK=OFF ^
    -DKDDockWidgets_FLUTTER=OFF ^
    -DKDDockWidgets_EXAMPLES=OFF ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DKDDockWidgets_FRONTENDS="qtwidgets" ^
    -DCMAKE_C_FLAGS_INIT="/MP" ^
    -DCMAKE_CXX_FLAGS_INIT="/MP"

if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed
    pause
    exit /b 1
)

:: === BUILD & INSTALL BOTH CONFIGURATIONS ===
echo Building Release...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release -- /maxcpucount
if %ERRORLEVEL% neq 0 (
    echo ERROR: Release build failed
    pause
    exit /b 1
)

echo Installing Release...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --target install -- /maxcpucount
if %ERRORLEVEL% neq 0 (
    echo ERROR: Release install failed
    pause
    exit /b 1
)

echo Building Debug...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Debug -- /maxcpucount
if %ERRORLEVEL% neq 0 (
    echo ERROR: Debug build failed
    pause
    exit /b 1
)

echo Installing Debug...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Debug --target install -- /maxcpucount
if %ERRORLEVEL% neq 0 (
    echo ERROR: Debug install failed
    pause
    exit /b 1
)

echo.
echo ==== Build Finished Successfully ====
echo Libraries and DLLs are installed in: %INSTALL_DIR%
echo.

:: List what was installed
if exist "%INSTALL_DIR%" (
    echo Installed files:
    dir /s "%INSTALL_DIR%"
)

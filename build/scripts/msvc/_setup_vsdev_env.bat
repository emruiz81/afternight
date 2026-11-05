@echo off
cd /d "%~dp0"
REM ===========================================================================
REM _setup_vsdev_env.bat - Shared Visual Studio Developer Environment Setup
REM ===========================================================================
REM Called by other build scripts to ensure the VS developer environment is
REM loaded for the x64 host and target used by all AfterNight MSVC builds.
REM If an existing prompt targets another architecture, cleanly reinitialize
REM it as x64. Otherwise, locate and invoke VsDevCmd.bat automatically.
REM
REM Detection order:
REM   1. vswhere.exe (official VS locator - finds any installed edition)
REM   2. Hardcoded fallback paths: Professional > Enterprise > Community
REM ===========================================================================

REM Already inside the expected VS Developer Command Prompt - nothing to do.
if /I "%VSCMD_ARG_HOST_ARCH%"=="x64" if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 0

REM VsDevCmd invokes vswhere by name. Keep the official installer directory on PATH
REM so that invocation succeeds even when this helper starts from a plain shell.
set "AFTERNIGHT_VSWHERE_DIR=C:\Program Files (x86)\Microsoft Visual Studio\Installer"
if exist "%AFTERNIGHT_VSWHERE_DIR%\vswhere.exe" set "PATH=%AFTERNIGHT_VSWHERE_DIR%;%PATH%"

REM Reinitialize an existing x86/cross-target prompt before adding x64 paths.
if defined VSCMD_VER set "AFTERNIGHT_VSDEVCMD=%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat"
if defined VSCMD_VER (
    if not defined VSINSTALLDIR goto :missing_visual_studio
    if not exist "%AFTERNIGHT_VSDEVCMD%" goto :missing_visual_studio

    echo Reconfiguring Visual Studio Developer Environment for x64...
    call "%AFTERNIGHT_VSDEVCMD%" -clean_env -no_logo
    if errorlevel 1 exit /b 1
    call "%AFTERNIGHT_VSDEVCMD%" -arch=x64 -host_arch=x64 -no_logo
    if errorlevel 1 exit /b 1
    goto :verify_architecture
)

echo Setting up Visual Studio Developer Environment...

REM --- Strategy 1: Use vswhere (most reliable, edition-agnostic) ---
set "VSWHERE=%AFTERNIGHT_VSWHERE_DIR%\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\Common7\Tools\VsDevCmd.bat" (
            call "%%i\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
            if errorlevel 1 exit /b 1
            goto :verify_architecture
        )
    )
)

REM --- Strategy 2: Hardcoded fallback paths (Professional > Enterprise > Community) ---
REM Try both year-based (2026) and version-based (18) folder names for each edition.
for %%V in (2026 18) do (
    for %%E in (Professional Enterprise Community) do (
        if exist "C:\Program Files\Microsoft Visual Studio\%%V\%%E\Common7\Tools\VsDevCmd.bat" (
            call "C:\Program Files\Microsoft Visual Studio\%%V\%%E\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
            if errorlevel 1 exit /b 1
            goto :verify_architecture
        )
    )
)

:missing_visual_studio
set "AFTERNIGHT_VSWHERE_DIR="
set "VSWHERE="
echo.
echo ERROR: Could not find a Visual Studio installation.
echo Please run this script from a Visual Studio Developer Command Prompt,
echo or install Visual Studio 2026 (Professional, Enterprise, or Community).
exit /b 1

:verify_architecture
set "AFTERNIGHT_VSDEVCMD="
set "AFTERNIGHT_VSWHERE_DIR="
set "VSWHERE="
if /I not "%VSCMD_ARG_HOST_ARCH%"=="x64" goto :wrong_architecture
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" goto :wrong_architecture
exit /b 0

:wrong_architecture
echo.
echo ERROR: Visual Studio Developer Environment did not initialize for x64.
echo Host architecture: %VSCMD_ARG_HOST_ARCH%
echo Target architecture: %VSCMD_ARG_TGT_ARCH%
exit /b 1

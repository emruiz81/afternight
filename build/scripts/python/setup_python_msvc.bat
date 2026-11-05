@echo off
setlocal enabledelayedexpansion

REM ========================
REM Configuration
REM ========================
set PYTHON_VERSION=3.14.7
set BUILD_DATE=20260814
set PYTHON_SHA256=870e9199dff4aa993c852c0733f7d36c359d127a67947f08f0755765ecf4232c
set PYSIDE6_VERSION=6.10.3
set PYTHON_TARBALL=cpython-%PYTHON_VERSION%+%BUILD_DATE%-x86_64-pc-windows-msvc-install_only.tar.gz
set PYTHON_URL=https://github.com/astral-sh/python-build-standalone/releases/download/%BUILD_DATE%/%PYTHON_TARBALL%

REM Target install directory (relative to script location)
set INSTALL_DIR=%~dp0..\..\..\python\python\tools_msvc

REM ========================
REM Clean existing install
REM ========================
if exist "%INSTALL_DIR%" (
    rmdir /s /q "%INSTALL_DIR%"
)

mkdir "%INSTALL_DIR%"
pushd "%INSTALL_DIR%"

REM ========================
REM Download Python
REM ========================
if not exist "python" (
    echo Downloading Python from %PYTHON_URL% ...
    curl -L -o "%PYTHON_TARBALL%" %PYTHON_URL%
    if errorlevel 1 (
        echo ERROR: Failed to download the embedded Python archive.
        if exist "%PYTHON_TARBALL%" del "%PYTHON_TARBALL%"
        popd
        exit /b 1
    )

    set ACTUAL_PYTHON_SHA256=
    REM certutil's second output line is the digest. Keep the first non-empty line after its
    REM heading so the localized completion message cannot overwrite it.
    for /f "skip=1 tokens=*" %%H in ('certutil -hashfile "%PYTHON_TARBALL%" SHA256') do if not defined ACTUAL_PYTHON_SHA256 set ACTUAL_PYTHON_SHA256=%%H
    if /I not "!ACTUAL_PYTHON_SHA256!"=="%PYTHON_SHA256%" (
        echo ERROR: Embedded Python archive digest mismatch.
        echo Expected: %PYTHON_SHA256%
        echo Actual:   !ACTUAL_PYTHON_SHA256!
        del "%PYTHON_TARBALL%"
        popd
        exit /b 1
    )

    echo Extracting %PYTHON_TARBALL% ...
    REM Use tar if available
    tar -xzf "%PYTHON_TARBALL%" || powershell -Command "tar -xzf '%PYTHON_TARBALL%'"

    REM Move contents out of extracted 'python' subfolder
    if exist python (
        xcopy python\* . /E /I /H /Y >nul
        rmdir /s /q python
    )

    del "%PYTHON_TARBALL%"
)

REM ========================
REM Setup pip and packages
REM ========================
echo Setting up pip...
.\python.exe -m ensurepip --upgrade
.\python.exe -m pip install --no-user --ignore-installed --no-warn-script-location -U pip setuptools wheel
.\python.exe -m pip install --no-user --ignore-installed --no-warn-script-location -U numpy scipy scikit-image appdirs virtualenv
.\python.exe -m pip install --no-user --ignore-installed --no-warn-script-location --only-binary=:all: PySide6==%PYSIDE6_VERSION%
REM Pre-compile .py files
echo Precompiling Python modules...
.\python.exe -m compileall Lib

popd
endlocal



REM Use the global python interpreter for packaging the packed python
python.exe package_python_lib.py ..\..\..\python\python\tools_msvc\Lib\ ..\..\..\python\python314.zip --exclude site-packages

@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..\..") do set "REPO_ROOT=%%~fI"

set "DOCS_DIR=%REPO_ROOT%\docs\user-guide"
set "VENV_DIR=%DOCS_DIR%\.venv"
set "VENV_PYTHON=%VENV_DIR%\Scripts\python.exe"
set "REQUIREMENTS_FILE=%DOCS_DIR%\requirements.txt"
set "MKDOCS_CONFIG=%DOCS_DIR%\mkdocs.yml"

if not exist "%REQUIREMENTS_FILE%" (
    echo Error: Missing %REQUIREMENTS_FILE%.
    exit /b 1
)

if not exist "%MKDOCS_CONFIG%" (
    echo Error: Missing %MKDOCS_CONFIG%.
    exit /b 1
)

call :find_python
if errorlevel 1 exit /b 1

"%BASE_PYTHON_EXE%" %BASE_PYTHON_ARGS% -m venv --help >nul 2>&1
if errorlevel 1 (
    echo Error: Python venv support is unavailable. Install Python 3 with venv support, then retry.
    exit /b 1
)

if not exist "%VENV_PYTHON%" (
    echo Creating documentation virtual environment at %VENV_DIR%
    "%BASE_PYTHON_EXE%" %BASE_PYTHON_ARGS% -m venv "%VENV_DIR%"
    if errorlevel 1 (
        echo Error: Could not create %VENV_DIR%. Ensure Python venv support is installed and the docs directory is writable.
        exit /b 1
    )
)

if not exist "%VENV_PYTHON%" (
    echo Error: Could not find the virtual environment Python at %VENV_PYTHON%.
    exit /b 1
)

"%VENV_PYTHON%" -m pip --version >nul 2>&1
if errorlevel 1 (
    echo pip is missing in %VENV_DIR%; attempting to bootstrap it with ensurepip.
    "%VENV_PYTHON%" -m ensurepip --upgrade
    if errorlevel 1 (
        echo Error: pip is unavailable in %VENV_DIR%. Recreate the virtual environment with a Python install that includes pip.
        exit /b 1
    )
)

pushd "%REPO_ROOT%" >nul
if errorlevel 1 (
    echo Error: Could not enter repository root %REPO_ROOT%.
    exit /b 1
)

echo Installing user guide dependencies into docs\user-guide\.venv
set "PIP_REQUIRE_VIRTUALENV=true"
"%VENV_PYTHON%" -m pip install --require-hashes -r "docs\user-guide\requirements.txt"
if errorlevel 1 (
    echo Error: Dependency installation failed. Check Python version compatibility, network access, and the hashed requirements file.
    popd >nul
    exit /b 1
)

echo Building user guide with strict MkDocs validation
"%VENV_PYTHON%" -m mkdocs build -f "docs\user-guide\mkdocs.yml" --strict
if errorlevel 1 (
    echo Error: MkDocs strict build failed. Fix the documentation warnings or errors above and retry.
    popd >nul
    exit /b 1
)

popd >nul
echo User guide build completed successfully.
exit /b 0

:find_python
set "BASE_PYTHON_EXE="
set "BASE_PYTHON_ARGS="

if defined PYTHON (
    "%PYTHON%" --version >nul 2>&1
    if errorlevel 1 (
        echo Error: PYTHON is set to "%PYTHON%", but it is not executable. Set PYTHON to a working interpreter or install Python 3.
        exit /b 1
    )
    set "BASE_PYTHON_EXE=%PYTHON%"
    exit /b 0
)

where py >nul 2>&1
if not errorlevel 1 (
    py -3 --version >nul 2>&1
    if not errorlevel 1 (
        set "BASE_PYTHON_EXE=py"
        set "BASE_PYTHON_ARGS=-3"
        exit /b 0
    )
)

where python >nul 2>&1
if not errorlevel 1 (
    python --version >nul 2>&1
    if not errorlevel 1 (
        set "BASE_PYTHON_EXE=python"
        exit /b 0
    )
)

echo Error: Python was not found. Install Python 3, then retry.
exit /b 1

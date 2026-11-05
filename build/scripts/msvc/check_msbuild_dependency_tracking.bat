@echo off
cd /d "%~dp0"

REM Reports MSVC object files whose recorded header dependencies are incomplete, which is
REM how an interrupted build silently leaves objects compiled against outdated headers.
REM Pass --fix to delete them so the next build recompiles them.
REM See docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md

echo ****************check_msbuild_dependency_tracking****************

where python >NUL 2>&1
if errorlevel 1 (
    echo python was not found on PATH; skipping the dependency-tracking check.
    exit /b 0
)

python "%~dp0check_msbuild_dependency_tracking.py" %*
exit /b %ERRORLEVEL%

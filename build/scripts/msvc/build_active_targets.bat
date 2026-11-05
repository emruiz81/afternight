@echo off
setlocal
cd /d "%~dp0"

REM Builds the active target set the target registry names, build\scripts\tools\consumer_targets.txt, in
REM dependency order: the main solution, and with --tests the test solution. It holds the generated tree to the
REM registry first and builds nothing when they disagree, so a stale project or a target the registry does not
REM build cannot be built by accident. The third-party solution is built by 03a/03b.
REM
REM It builds what 05a/09a build, named by the registry rather than by whatever the generated tree holds. The
REM Linux counterpart is build/scripts/linux/build_active_targets.sh.
REM
REM Usage: build_active_targets.bat [release^|debug] [--tests] [--dry-run]

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

where python >NUL 2>&1
if errorlevel 1 (
    echo python was not found on PATH; the active set is read from the registry by a Python script.
    exit /b 1
)

REM Objects whose recorded header dependencies are incomplete would otherwise be linked in stale.
call "%~dp0check_msbuild_dependency_tracking.bat" --fix

echo ****************build_active_targets****************
python "%~dp0..\tools\active_targets.py" build %*
if errorlevel 1 exit /b 1

echo Done!

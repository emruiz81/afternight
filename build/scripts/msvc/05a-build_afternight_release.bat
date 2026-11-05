@echo off
cd /d "%~dp0"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

echo ****************05a-build_afternight_release****************

REM Objects whose recorded header dependencies are incomplete would otherwise be linked
REM in stale, mixing header revisions inside one binary. Deleting them here costs one
REM recompile; leaving them costs a crash nobody can attribute.
call "%~dp0check_msbuild_dependency_tracking.bat" --fix

REM PUSHD ..\..\vs2026-clang
PUSHD ..\..\vs2026-msvc
REM Solution build already includes AfterNightExtensionHost and AfterNightExtensionRuntimeBackend.
call msbuild AfterNight.slnx /t:Build /p:Configuration=Release;Platform=x64
set BUILD_EXIT_CODE=%ERRORLEVEL%
POPD

if not "%BUILD_EXIT_CODE%"=="0" exit /b %BUILD_EXIT_CODE%

echo Done!

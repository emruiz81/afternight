@echo off
cd /d "%~dp0"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

echo ****************03b-build_3rdparty_libs_debug****************
REM PUSHD ..\..\vs2026-clang\3rdparty
PUSHD ..\..\vs2026-msvc\3rdparty
call msbuild AfterNight-3rdparty.slnx /t:Build /p:Configuration=Debug;Platform=x64
set BUILD_EXIT_CODE=%ERRORLEVEL%
POPD

if not "%BUILD_EXIT_CODE%"=="0" exit /b %BUILD_EXIT_CODE%

echo Done!

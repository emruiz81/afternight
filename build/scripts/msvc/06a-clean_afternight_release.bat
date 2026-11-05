@echo off
cd /d "%~dp0"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

echo ****************06a-clean_afternight_release****************
PUSHD ..\..\vs2026-msvc
call msbuild AfterNight.slnx /t:Clean /p:Configuration=Release;Platform=x64
POPD

echo Done!

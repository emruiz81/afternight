@echo off
cd /d "%~dp0"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

echo ****************check_clang_format****************

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_clang_format_changed.ps1" -Mode Check %*
exit /b %ERRORLEVEL%

@echo off
setlocal
call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1
set "AN_RAW_CLANG_CL=%VSINSTALLDIR%VC\Tools\Llvm\x64\bin\clang-cl.exe"
python "%~dp0..\tools\rawspeed\check_cache.py" %*
exit /b %ERRORLEVEL%

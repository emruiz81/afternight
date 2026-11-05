@echo off
cd /d "%~dp0"

REM You need to run this from a Visual Studio Developer Command Prompt (for msbuild.exe)
call 01-create_project_files.bat
call 02-install_msvc_deps.bat
call 03b-build_3rdparty_libs_debug.bat
call 04b-deploy_deps_debug.bat
call 05b-build_afternight_debug.bat

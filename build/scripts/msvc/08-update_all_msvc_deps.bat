@echo off
cd /d "%~dp0"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

echo ****************08-update_all_msvc_deps****************
PUSHD ..\..\..\vcpkg
call bootstrap-vcpkg.bat -disableMetrics

vcpkg update --vcpkg-root=.\
vcpkg upgrade --vcpkg-root=.\ --no-dry-run

POPD

echo Done!

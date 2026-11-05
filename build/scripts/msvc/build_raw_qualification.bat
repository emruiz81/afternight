@echo off
setlocal
call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1
set "AN_RAW_CONFIG=%~1"
if "%AN_RAW_CONFIG%"=="" set "AN_RAW_CONFIG=Release"
set "AN_RAW_REPO=%~dp0..\..\.."
set "AN_RAW_NATIVE=OFF"
if "%AFTERNIGHT_RAWSPEED%"=="0" set "AN_RAW_NATIVE=ON"
cmake --fresh -S "%AN_RAW_REPO%\build\scripts\tools\rawspeed" -B "%AN_RAW_REPO%\obj\raw-qualification\%AN_RAW_CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%AN_RAW_CONFIG% -DAN_NATIVE_ONLY=%AN_RAW_NATIVE% "-DCMAKE_TOOLCHAIN_FILE=%AN_RAW_REPO%\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-raw "-DVCPKG_OVERLAY_TRIPLETS=%AN_RAW_REPO%\build\vcpkg\triplets" -DVCPKG_MANIFEST_MODE=OFF
if errorlevel 1 exit /b 1
cmake --build "%AN_RAW_REPO%\obj\raw-qualification\%AN_RAW_CONFIG%" --parallel 4
if errorlevel 1 exit /b 1
cmake --install "%AN_RAW_REPO%\obj\raw-qualification\%AN_RAW_CONFIG%" --prefix "%AN_RAW_REPO%\bin\msvc\%AN_RAW_CONFIG%"
exit /b %ERRORLEVEL%

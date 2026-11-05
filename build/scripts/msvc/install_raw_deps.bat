@echo off
setlocal
call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1
if "%AFTERNIGHT_RAWSPEED%"=="0" exit /b 0
set "AN_RAW_CLANG_CL=%VSINSTALLDIR%VC\Tools\Llvm\x64\bin\clang-cl.exe"
if not exist "%AN_RAW_CLANG_CL%" (
    echo Missing Visual Studio C++ Clang tools: %AN_RAW_CLANG_CL%
    exit /b 1
)
pushd "%~dp0..\..\..\vcpkg"
vcpkg install afternight-libraw --vcpkg-root=. --triplet=x64-windows-raw --overlay-ports=..\build\vcpkg\overlay-ports --overlay-triplets=..\build\vcpkg\triplets --recurse %*
set "AN_RAW_EXIT=%ERRORLEVEL%"
popd
exit /b %AN_RAW_EXIT%

@echo off
cd /d "%~dp0"

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

echo ****************09a-build_afternight_tests_release****************

REM The triplet 02-install_msvc_deps.bat installed the ports for: x64-windows unless AFTERNIGHT_VCPKG_TRIPLET
REM names another.
if not defined AFTERNIGHT_VCPKG_TRIPLET set "AFTERNIGHT_VCPKG_TRIPLET=x64-windows"

REM Objects whose recorded header dependencies are incomplete would otherwise be linked
REM in stale, mixing header revisions inside one binary. Deleting them here costs one
REM recompile; leaving them costs a crash nobody can attribute.
call "%~dp0check_msbuild_dependency_tracking.bat" --fix

REM PUSHD ..\..\vs2026-clang\tests
PUSHD ..\..\vs2026-msvc\tests
REM Build test projects in parallel, but skip per-project post-build deploys.
REM We replay the Qt runtime deployment serially afterward to avoid shared
REM output-folder races between multiple windeployqt invocations.
call msbuild AfterNightTest.slnx /t:Build /p:Configuration=Release;Platform=x64;PostBuildEventUseInBuild=false /m
if errorlevel 1 exit /b 1
POPD
call "%~dp0_deploy_msvc_test_qt_runtime.bat" release
if errorlevel 1 exit /b 1
PUSHD ..\..\vs2026-msvc
call msbuild AfterNight.slnx /t:AfterNightExtensionHostLite /p:Configuration=Release;Platform=x64
if errorlevel 1 exit /b 1
call msbuild AfterNight.slnx /t:AfterNightExtensionHost /p:Configuration=Release;Platform=x64
if errorlevel 1 exit /b 1
call msbuild AfterNight.slnx /t:AfterNightExtensionRuntimeBackend /p:Configuration=Release;Platform=x64
if errorlevel 1 exit /b 1
POPD

xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\gtest.dll ..\..\..\bin\msvc\release\
REM The library parity suite's reference implementations, which nothing that ships imports.
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\raw_r.dll ..\..\..\bin\msvc\release\
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\lcms2-2.dll ..\..\..\bin\msvc\release\
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\opencv_imgproc4.dll ..\..\..\bin\msvc\release\
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\opencv_core4.dll ..\..\..\bin\msvc\release\
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libsodium.dll ..\..\..\bin\msvc\release\

if not exist "..\..\..\bin\msvc\release\platforms" mkdir "..\..\..\bin\msvc\release\platforms"
REM Qt 6.10.3 package naming remains msvc2022_64 for MSVC toolchains
xcopy /Y ..\..\..\Qt6\6.10.3\msvc2022_64\plugins\platforms\qoffscreen.dll ..\..\..\bin\msvc\release\platforms\

echo Done!

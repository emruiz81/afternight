@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM Builds the AfterNightEngine project and its optional adapters from the generated
REM main workspace, to shorten the engine's edit/test loop. It is not a replacement for the
REM numbered build scripts: it builds a few targets out of the normal graph and skips nothing
REM else, so the full-product build is still the one that decides a change to the product.
REM
REM Usage: build_afternightengine.bat [release^|debug] [--with-tests] [--toolchain msvc^|clang]

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"
set "WITH_TESTS=0"
set "TOOLCHAIN=msvc"
shift
:parse_options
if "%~1"=="" goto options_parsed
if /I "%~1"=="--with-tests" (
    set "WITH_TESTS=1"
    shift
    goto parse_options
)
if /I "%~1"=="--toolchain" (
    if "%~2"=="" goto usage
    set "TOOLCHAIN=%~2"
    shift
    shift
    goto parse_options
)
goto usage
:options_parsed
if /I "%CONFIG%"=="release" (
    set "MSBUILD_CONFIG=Release"
) else if /I "%CONFIG%"=="debug" (
    set "MSBUILD_CONFIG=Debug"
) else (
    goto usage
)
if /I "%TOOLCHAIN%"=="msvc" (
    set "PROJECT_TREE=vs2026-msvc"
) else if /I "%TOOLCHAIN%"=="clang" (
    set "PROJECT_TREE=vs2026-clang"
) else (
    goto usage
)

echo ****************build_afternightengine_%TOOLCHAIN%_%CONFIG%****************

REM The generated graph is the only thing this script may build from, so a missing workspace is a
REM prerequisite error rather than something to work around by invoking MSBuild on a project file.
if not exist "..\..\%PROJECT_TREE%\AfterNight.slnx" (
    echo Projects are not generated. Run 01-create_project_files.bat first.
    exit /b 1
)

call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

REM Objects whose recorded header dependencies are incomplete would otherwise be linked in stale,
REM mixing header revisions inside one binary. A target-select script needs this exactly as much as
REM the numbered ones do, and a new project needs it from its first header change.
call "%~dp0check_msbuild_dependency_tracking.bat" --fix
if errorlevel 1 exit /b 1

REM The opt-in OpenCL proof backend and its suite exist only in a tree generated with
REM AFTERNIGHT_OPENCL=1, so they are built when the generated solutions name them rather than when a
REM variable set now says so: a tree generated without the opt-in has neither, and asking MSBuild for a
REM target its solution does not contain is an error.
set "MAIN_TARGETS=AfterNightEngine;AfterNightEnginePythonAdapter;AfterNightEngineGmicAdapter"
REM EngineBenchCommon is named rather than left to the fixture-bridge suite's project dependency,
REM which a target-selected solution build does not honour: the suite would link whatever copy of
REM the static library is in the lib directory, including one left by a tree generated with
REM AFTERNIGHT_SANITIZE, which the linker then refuses on the standard library's container annotations.
set "TEST_TARGETS=EngineBenchCommon;Test_EngineCore;Test_EngineRuntime;Test_EngineImage;Test_EngineImageOps;Test_EngineCalibration;Test_EngineStars;Test_EngineRegistration;Test_EngineAstrometry;Test_EngineColor;Test_EngineStacking;Test_EngineInterop;Test_EngineKernels;Test_EngineFaults;Test_EngineFixtureBridge;Test_EnginePublicHeaders;Test_EngineConsumerProbes;Test_EngineLibraryParity;Test_EnginePythonAdapter;Test_EngineGmicAdapter;Fuzz_EngineDescriptor;Fuzz_EngineConversion;Fuzz_EnginePixelMath;Fuzz_EngineMetadata;Fuzz_EngineCodec;Fuzz_EngineSequence"
findstr /C:"AfterNightEngineOpenClBackend.vcxproj" "..\..\%PROJECT_TREE%\AfterNight.slnx" >nul 2>&1 && set "MAIN_TARGETS=%MAIN_TARGETS%;AfterNightEngineOpenClBackend"
findstr /C:"Test_EngineOpenClBackend.vcxproj" "..\..\%PROJECT_TREE%\tests\AfterNightTest.slnx" >nul 2>&1 && set "TEST_TARGETS=%TEST_TARGETS%;Test_EngineOpenClBackend"

REM An instrumented generation and an ordinary one write their binaries to one directory and their
REM objects to different ones, so after a switch MSBuild finds every output newer than the objects it
REM would compile and builds nothing at all - leaving the previous instrumentation in bin behind a
REM "Build succeeded" and no other sign. What the tree was generated for is therefore recorded beside
REM the binaries, and a change of it rebuilds exactly these targets rather than trusting a timestamp.
set "BIN_DIR=msvc"
if /I "%TOOLCHAIN%"=="clang" set "BIN_DIR=clang-cl"
set "TREE_SANITIZER=none"
findstr /C:"/fsanitize=address" "..\..\%PROJECT_TREE%\AfterNightEngine.vcxproj" >nul 2>&1 && set "TREE_SANITIZER=address"
set "SANITIZER_MARKER=..\..\..\bin\%BIN_DIR%\%CONFIG%\.engine-instrumentation"
set "BUILT_SANITIZER=none"
if exist "%SANITIZER_MARKER%" set /p BUILT_SANITIZER=<"%SANITIZER_MARKER%"
if not "%TREE_SANITIZER%"=="%BUILT_SANITIZER%" (
    echo Instrumentation changed from "%BUILT_SANITIZER%" to "%TREE_SANITIZER%"; rebuilding every target this script builds.
    call :append_rebuild MAIN_TARGETS "!MAIN_TARGETS!"
    call :append_rebuild TEST_TARGETS "!TEST_TARGETS!"
)

PUSHD ..\..\%PROJECT_TREE%
call msbuild AfterNight.slnx /t:%MAIN_TARGETS% /p:Configuration=%MSBUILD_CONFIG%;Platform=x64
set BUILD_EXIT_CODE=%ERRORLEVEL%
POPD

if not "%BUILD_EXIT_CODE%"=="0" exit /b %BUILD_EXIT_CODE%

if "%WITH_TESTS%"=="1" (
    REM The engine's direct suites and fuzz targets, which link the engine and nothing else of
    REM AfterNight's. The rest of the test workspace is deliberately not built here.
    if not exist "..\..\%PROJECT_TREE%\tests\AfterNightTest.slnx" (
        echo Test projects are not generated. Run 01-create_project_files.bat first.
        exit /b 1
    )
    PUSHD ..\..\%PROJECT_TREE%\tests
    call msbuild AfterNightTest.slnx /t:!TEST_TARGETS! /p:Configuration=%MSBUILD_CONFIG%;Platform=x64
    set BUILD_EXIT_CODE=!ERRORLEVEL!
    POPD
    if not "!BUILD_EXIT_CODE!"=="0" exit /b !BUILD_EXIT_CODE!
)

>"%SANITIZER_MARKER%" echo %TREE_SANITIZER%

echo Done!
exit /b 0

REM Turns "A;B" into "A:Rebuild;B:Rebuild", for a solution build that must not trust a timestamp.
:append_rebuild
setlocal enabledelayedexpansion
set "REBUILT="
for %%T in (%~2) do set "REBUILT=!REBUILT!;%%T:Rebuild"
set "REBUILT=!REBUILT:~1!"
endlocal & set "%~1=%REBUILT%"
exit /b 0

:usage
echo Usage: build_afternightengine.bat [release^|debug] [--with-tests] [--toolchain msvc^|clang]
exit /b 2

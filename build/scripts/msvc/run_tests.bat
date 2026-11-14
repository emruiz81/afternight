@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"
if /I not "%CONFIG%"=="release" if /I not "%CONFIG%"=="debug" (
    echo Usage: run_tests.bat [release^|debug]
    exit /b 2
)
set "BIN_DIR=..\..\..\bin\msvc\%CONFIG%"
set "TEST_SUFFIX="
if /I "%CONFIG%"=="debug" set "TEST_SUFFIX=_d"
set "FOUND_TESTS=0"
:: Every suite runs even after one fails, so one run reports every failing suite.
set "FAILED_TESTS="
:: The full extension host is built with the product, so a case that starts it fails rather than skips when it
:: is not deployed beside the suites.
set "AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST=1"
set "TEST_NAMES=Test_AfterNightApp Test_AfterNightCLI Test_ConsumerFaults Test_ConsumerRuntime Test_ControlsAstrometry Test_ControlsDialogs Test_ControlsDocking Test_ControlsExtensionSeam Test_ControlsFiles Test_ControlsHistory Test_ControlsProcessFramework Test_ControlsSeams Test_ControlsSequences Test_ControlsServices Test_ControlsSession Test_ControlsStatistics Test_ControlsViewer Test_ExtensionHostNative Test_ExtensionRuntimeBackend Test_FixtureNeutral Test_FixtureEngine Test_ImageIntegration Test_ImageProcessControl Test_ImageRTPreviewControl Test_ImageViewControl Test_MainDashboard Test_MaskDrawing Test_Persistence Test_Platform Test_ProcessFramework Test_ProcessesChannels Test_ProcessesColorTools Test_ProcessesCombinations Test_ProcessesExternalTools Test_ProcessesGeometry Test_ProcessesMasks Test_ProcessesPhotometric Test_ProcessesPixelMath Test_ProcessesPointwise Test_ProcessesShared Test_ProcessesSpatial Test_ProcessesStarProfilers Test_ProcessesStretching Test_ProcessesWorkflowTools Test_ShellCore Test_SystemInformationPanel Test_ThemeSystem Test_Widgets"
:: Stale AfterNight test scratch stranded in %TEMP% by earlier runs that crashed,
:: were Ctrl-C'd, or were killed is reclaimed before this run starts.  Only
:: entries that have been untouched for at least this many hours are removed, so
:: a second test run happening right now is never deleted out from under itself.
set "STALE_SCRATCH_HOURS=6"
set "STALE_SCRATCH_GLOBS=AfterNight-Tests-*;AfterNight-Test_*;ANtmp-*;AfterNight_drizztmp;AfterNight_calibrated_stack_spill_*;AfterNightStackInput-*.andump"
set "STALE_SCRATCH_GLOBS=!STALE_SCRATCH_GLOBS!;afternight_test_executor;afternight_test_partial_success;afternight_calibration_tests;afternight-mock-tests"
set "STALE_SCRATCH_GLOBS=!STALE_SCRATCH_GLOBS!;afternight-process-framework-tests*;afternight-extension-host;afternight-extension-manager-page-tests"
set "STALE_SCRATCH_GLOBS=!STALE_SCRATCH_GLOBS!;an_extension_runtime_test_*;an_shared_buffer_test_*;afternight_gaia_catalog_*;afternight_codec_test_*"
set "STALE_SCRATCH_GLOBS=!STALE_SCRATCH_GLOBS!;afternight_test_reconfigured.log;afternight_extension_host_manager_tests.log"
:: Per-suite QTemporaryDir roots (Test_ImageIntegration-a1b2c3, ...).  Only the
:: known suite names are swept; a bare Test_* glob would be unsafe in a temp
:: directory shared with every other program on the machine.
for %%n in (%TEST_NAMES%) do set "STALE_SCRATCH_GLOBS=!STALE_SCRATCH_GLOBS!;%%n-*"
call :sweep_stale_scratch

pushd "%BIN_DIR%"

for %%e in (%TEST_NAMES%) do (
    if not exist "%%e%TEST_SUFFIX%.exe" (
        echo Missing expected test executable: %%e%TEST_SUFFIX%.exe
        popd
        goto :fail
    )
)

for %%f in ("Test_*.exe") do (
    set "FOUND_TESTS=1"
    REM A sweep may name executables to leave out, separated by spaces, in AFTERNIGHT_SKIP_TESTS.
    set "SKIP_TEST="
    if defined AFTERNIGHT_SKIP_TESTS for %%s in (!AFTERNIGHT_SKIP_TESTS!) do if /I "%%~s"=="%%~nf" set "SKIP_TEST=1"
    if defined SKIP_TEST (
        echo Skipping %%~nf%%~xf
    ) else (
        set "TEST_STORAGE_ROOT=%TEMP%\AfterNight-Tests-%%~nf-!RANDOM!-!RANDOM!"
        mkdir "!TEST_STORAGE_ROOT!"
        if errorlevel 1 (
            echo Failed to create test storage root: !TEST_STORAGE_ROOT!
            popd
            goto :fail
        )
        set "AFTERNIGHT_TEST_STORAGE_ROOT=!TEST_STORAGE_ROOT!"
        REM A sweep that names a results directory gets each run's gtest report there, named after the executable.
        set "RESULTS_FLAG="
        if defined AFTERNIGHT_TEST_RESULTS_DIR set "RESULTS_FLAG="--gtest_output=json:%AFTERNIGHT_TEST_RESULTS_DIR%\%%~nf.json""
        echo Running %%~nf%%~xf
        echo --------------------------------------------------

        REM Run test directly, showing full color output (stdout + stderr)
        ".\%%~f" !RESULTS_FLAG! 2>&1
        set "TEST_STATUS=!ERRORLEVEL!"
        set "AFTERNIGHT_TEST_STORAGE_ROOT="

        rmdir /s /q "!TEST_STORAGE_ROOT!"
        if exist "!TEST_STORAGE_ROOT!" (
            echo Failed to remove test storage root: !TEST_STORAGE_ROOT!
            set "TEST_STATUS=1"
        )

        if not "!TEST_STATUS!"=="0" (
            echo.
            echo [FAILED] %%~nf%%~xf
            echo --------------------------------------------------
            set "FAILED_TESTS=!FAILED_TESTS! %%~nf%%~xf"
        )

        echo.
    )
)

popd
if "%FOUND_TESTS%"=="0" (
    echo No test executables were found in %BIN_DIR%.
    echo Build tests first with the matching 09a/09b build script.
    goto :fail
)

if defined FAILED_TESTS (
    echo Failed:!FAILED_TESTS!
    goto :fail
)

echo All tests passed successfully.
call :cleanup
exit /b 0

:fail
call :cleanup
echo One or more tests failed.
exit /b 1

:cleanup
:: Every exit path this script controls routes through here.  NOTE: cmd.exe has
:: no equivalent of the shells' "trap": Ctrl-C is answered by the console with
:: "Terminate batch job (Y/N)?" and, if confirmed, the interpreter stops without
:: executing another line of this file.  taskkill, closing the console window and
:: a machine crash are equally uncatchable.  Such a run WILL strand the storage
:: root it was using; that residue is exactly what :sweep_stale_scratch reclaims
:: at the start of the next run.  This is not airtight, and cannot be made so in
:: batch alone.
if defined TEST_STORAGE_ROOT (
    if exist "!TEST_STORAGE_ROOT!" rmdir /s /q "!TEST_STORAGE_ROOT!"
)
set "AFTERNIGHT_TEST_STORAGE_ROOT="
exit /b 0

:sweep_stale_scratch
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='SilentlyContinue'; $root=$env:TEMP; $hours=[int]$env:STALE_SCRATCH_HOURS; $cut=(Get-Date).AddHours(-$hours); $globs=@($env:STALE_SCRATCH_GLOBS -split ';' | Where-Object { $_ -ne '' }); $n=0; $bytes=0; foreach ($e in @(Get-ChildItem -LiteralPath $root -Force)) { $hit=$false; foreach ($g in $globs) { if ($e.Name -like $g) { $hit=$true; break } }; if (-not $hit) { continue }; if ($e.LastWriteTime -gt $cut -or $e.CreationTime -gt $cut) { continue }; $size=0; if ($e.PSIsContainer) { $size=(Get-ChildItem -LiteralPath $e.FullName -Recurse -Force -File | Measure-Object -Property Length -Sum).Sum } else { $size=$e.Length }; Remove-Item -LiteralPath $e.FullName -Recurse -Force; if (Test-Path -LiteralPath $e.FullName) { continue }; $n=$n+1; $bytes=$bytes+[int64]$size }; if ($n -gt 0) { Write-Host ('Reclaimed {0} stale AfterNight test scratch entries ({1:N1} MiB) from {2}' -f $n,($bytes/1MB),$root) } else { Write-Host ('No AfterNight test scratch older than {0}h to reclaim in {1}' -f $hours,$root) }"
exit /b 0

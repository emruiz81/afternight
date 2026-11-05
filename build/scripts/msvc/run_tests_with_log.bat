@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set BIN_DIR=..\..\..\bin\msvc\release
set RESULTS_FILE=%BIN_DIR%\test_results.txt

echo. > "%RESULTS_FILE%"

pushd %BIN_DIR%
for %%f in ("Test_*.exe") do (
    echo Running %%~nf%%~xf
    >> "%RESULTS_FILE%" echo Running %%~nf%%~xf

    for /f "delims=" %%A in ('"%%f" 2^>^&1') do (
        echo %%A
        >> "%RESULTS_FILE%" echo %%A
    )

    if errorlevel 1 (
        echo Test %%~nf%%~xf failed
        >> "%RESULTS_FILE%" echo Test %%~nf%%~xf failed
        goto :fail
    )
)
popd

echo All tests passed
>> "%BIN_DIR%\%RESULTS_FILE%" echo All tests passed
goto :end

:fail
echo One or more tests failed.
>> "%BIN_DIR%\%RESULTS_FILE%" echo One or more tests failed.
:end

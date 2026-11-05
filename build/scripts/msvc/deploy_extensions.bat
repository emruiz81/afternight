@echo off
REM Deploy extension packages to bin\msvc\<config>\extensions\.
REM Usage: deploy_extensions.bat [release|debug]
REM When called without arguments, deploys to both release and debug.
setlocal
cd /d "%~dp0"

if "%~1"=="" (
    echo **************** deploy_extensions ****************
    call "%~f0" release
    if errorlevel 1 exit /b %ERRORLEVEL%
    call "%~f0" debug
    exit /b %ERRORLEVEL%
)

set "CONFIG=%~1"
echo **************** deploy_extensions (%CONFIG%) ****************

set "BIN_DIR=..\..\..\bin\msvc\%CONFIG%"
set "EXTENSIONS_SRC=..\..\..\extensions"

if exist "%BIN_DIR%" goto :bin_dir_ready
echo Skipping %CONFIG% ^(bin\msvc\%CONFIG%\ does not exist^)
exit /b 0

:bin_dir_ready

if exist "%EXTENSIONS_SRC%" goto :extensions_src_ready
echo Warning: extensions source directory not found
exit /b 0

:extensions_src_ready
if exist "%BIN_DIR%\extensions" rmdir /S /Q "%BIN_DIR%\extensions"
mkdir "%BIN_DIR%\extensions" 2>NUL
xcopy /Y /I /C /E "%EXTENSIONS_SRC%\*" "%BIN_DIR%\extensions\"
if exist "%BIN_DIR%\extensions\.gitkeep" del /Q "%BIN_DIR%\extensions\.gitkeep"
echo Deployed extensions to bin\msvc\%CONFIG%\extensions\
exit /b 0

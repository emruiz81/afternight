@echo off
cd /d "%~dp0"

echo ****************01 -create_project_files****************
PUSHD ..\..\premake
call .\configure_windows.bat
set "CONFIGURE_EXIT=%ERRORLEVEL%"
POPD
REM A generation Premake refused leaves the projects of the last one behind, so it must not end in "Done!".
if not "%CONFIGURE_EXIT%"=="0" (
    echo Project generation failed.
    exit /b %CONFIGURE_EXIT%
)

REM A target the registry no longer builds is in no generated tree, so nothing rebuilds or deletes what it built
REM before; left in place it would pass for current. Its staged outputs are removed here, where generation stops
REM producing it.
where python >NUL 2>&1
if errorlevel 1 (
    echo python was not found on PATH, so the outputs of targets the registry no longer builds were not removed.
) else (
    python "%~dp0..\tools\active_targets.py" invalidate
    if errorlevel 1 exit /b 1
)

echo Done!

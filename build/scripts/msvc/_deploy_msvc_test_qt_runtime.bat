@echo off
setlocal
cd /d "%~dp0"

if "%~1"=="" (
    echo ERROR: Missing build configuration.
    exit /b 1
)

set "CONFIG=%~1"
set "EXE_SUFFIX=%~2"
set "TARGET_DIR=%~dp0..\..\..\bin\msvc\%CONFIG%"
set "WDEPLOYQT=%~dp0..\..\..\Qt6\6.10.3\msvc2022_64\bin\windeployqt.exe"

if not exist "%WDEPLOYQT%" (
    echo ERROR: Could not find windeployqt at "%WDEPLOYQT%".
    exit /b 1
)

for %%F in (
    Test_AfterNightApp
    Test_ImageIntegration
    Test_ImageProcessControl
    Test_ImageRTPreviewControl
    Test_ImageViewControl
    Test_MainDashboard
    Test_MaskDrawing
    Test_ProcessFramework
    Test_ThemeSystem
    Test_Widgets
) do (
    if exist "%TARGET_DIR%\%%F%EXE_SUFFIX%.exe" (
        echo Deploying Qt runtime for %%F%EXE_SUFFIX%.exe
        call "%WDEPLOYQT%" "%TARGET_DIR%\%%F%EXE_SUFFIX%.exe" --no-translations --include-plugins qminimal,qoffscreen
        if errorlevel 1 exit /b 1
    )
)

exit /b 0
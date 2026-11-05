@echo off
cd /d "%~dp0"

echo ****************04b-deploy_deps_debug****************

python "%~dp0..\tools\deploy_raw_deps.py" --config debug
if errorlevel 1 exit /b 1
echo ****************Deploying MSVC 3rdparty deps****************

xcopy /Y /E ..\..\..\3rdparty\bin\msvc\debug\*.dll ..\..\..\bin\msvc\debug\
xcopy /Y /E ..\..\..\3rdparty\bin\msvc\debug\*.pdb ..\..\..\bin\msvc\debug\

echo ****************Deploying MSVC vcpkg deps****************
REM Every library the binaries import is required: a copy that finds no file - a port the triplet does not
REM hold - fails the step here rather than the first program that loads it.
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\cfitsio.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\pugixml.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\openxisf\build-msvc-install\bin\openxisf-1.1d.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\zstd.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libsodium.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\lz4d.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\tiffd.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\tbb12_debug.dll ..\..\..\bin\msvc\debug\ || exit /b 1
REM A rollback build without the private RAW packages reads camera files through the public
REM LibRaw; the default build imports neither library, and the library parity suite's copy
REM of them is deployed with the tests.
if "%AFTERNIGHT_RAWSPEED%"=="0" (
    xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\raw_rd.dll ..\..\..\bin\msvc\debug\ || exit /b 1
    xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\lcms2-2.dll ..\..\..\bin\msvc\debug\ || exit /b 1
)
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libwebp.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libsharpyuv.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\liblzma.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libexpatd.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\Lerc.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\jpeg8.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\intl-8.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\iconv-2.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\gslcblasd.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\gsld.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\fftw3.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\fftw3f.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\exiv2.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\deflate.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\brotlidec.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\brotlicommon.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\zd.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\bin\z.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libpng16d.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\avcodec-62.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\avutil-60.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\avformat-62.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\swscale-9.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libx265.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libx264-164.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\aom.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libwebpmux.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\swresample-6.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\pthreadVC3d.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\cmark-gfm.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\cmark-gfm-extensions.dll ..\..\..\bin\msvc\debug\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\debug\bin\libvpld.dll ..\..\..\bin\msvc\debug\ || exit /b 1

echo ****************Deploying crashpad_handler****************
xcopy /Y ..\..\..\vcpkg\installed\x64-windows\tools\crashpad\crashpad_handler.exe ..\..\..\bin\msvc\debug\ || exit /b 1

REM Qt deployment is handled by windeployqt post-build command in the MSVC solution

xcopy /Y ..\..\..\kddockwidgets\build-msvc-install\bin\kddockwidgets-qt6d.dll ..\..\..\bin\msvc\debug\ || exit /b 1

xcopy /Y ..\..\..\python\python\tools_msvc\python3*.dll ..\..\..\bin\msvc\debug\
if exist ..\..\..\python\python\tools_msvc\python.exe xcopy /Y ..\..\..\python\python\tools_msvc\python.exe ..\..\..\bin\msvc\debug\
if exist ..\..\..\python\python\tools_msvc\python3.exe xcopy /Y ..\..\..\python\python\tools_msvc\python3.exe ..\..\..\bin\msvc\debug\
if exist ..\..\..\python\python\tools_msvc\python3.14.exe xcopy /Y ..\..\..\python\python\tools_msvc\python3.14.exe ..\..\..\bin\msvc\debug\
if exist ..\..\..\python\python\tools_msvc\bin\python.exe xcopy /Y ..\..\..\python\python\tools_msvc\bin\python.exe ..\..\..\bin\msvc\debug\
if exist ..\..\..\python\python\tools_msvc\bin\python3.exe xcopy /Y ..\..\..\python\python\tools_msvc\bin\python3.exe ..\..\..\bin\msvc\debug\
if exist ..\..\..\python\python\tools_msvc\bin\python3.14.exe xcopy /Y ..\..\..\python\python\tools_msvc\bin\python3.14.exe ..\..\..\bin\msvc\debug\
xcopy /Y ..\..\..\python\*.zip ..\..\..\bin\msvc\debug\
REM The bundled runtime is replaced whole: copied over an earlier deployment, it kept every package, distribution record
REM and library its source has since dropped, and the installer ships what this directory holds.
if exist ..\..\..\bin\msvc\debug\PythonLib rmdir /S /Q ..\..\..\bin\msvc\debug\PythonLib
xcopy /Y /E ..\..\..\python\python\tools_msvc\DLLs\*.* ..\..\..\bin\msvc\debug\PythonLib\DLLs\
xcopy /Y /E ..\..\..\python\python\tools_msvc\Lib\site-packages\*.* ..\..\..\bin\msvc\debug\PythonLib\Lib\site-packages\
xcopy /Y /E ..\..\..\python\python\tools_msvc\Scripts\*.* ..\..\..\bin\msvc\debug\PythonLib\Scripts\
if exist ..\..\..\python\modules xcopy /Y /I /C /E ..\..\..\python\modules ..\..\..\bin\msvc\debug\PythonLib\Lib\site-packages\
if exist ..\..\..\bin\msvc\debug\PythonLib\Lib\site-packages\.gitkeep del /Q ..\..\..\bin\msvc\debug\PythonLib\Lib\site-packages\.gitkeep


echo ****************Deploying the Visual C++ runtime****************
REM The debug binaries built here import the debug Visual C++ runtime, which the toolset installs with Visual Studio and
REM which may not be redistributed. The release runtime deployed beside them is what the embedded Python and the
REM release-built libraries it loads import: Windows provides the universal CRT and not that runtime, so it is copied
REM from the toolset's own redistributable.
call "%~dp0_setup_vsdev_env.bat" || exit /b 1
cd /d "%~dp0"
set "AFTERNIGHT_VC_CRT="
for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do set "AFTERNIGHT_VC_CRT=%%D"
if not defined AFTERNIGHT_VC_CRT (
    echo The toolset's redistributable Visual C++ runtime was not found under "%VCToolsRedistDir%".
    exit /b 1
)
for %%F in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do (
    xcopy /Y "%AFTERNIGHT_VC_CRT%\%%F" ..\..\..\bin\msvc\debug\ || exit /b 1
)

echo ****************Deploying MSVC Scripts****************
xcopy /Y /I /C /E ..\..\..\scripts ..\..\..\bin\msvc\debug\scripts

echo ****************Creating MSVC dirs****************
mkdir ..\..\..\bin\msvc\debug\accessiblebridge 2>NUL
call deploy_extensions.bat debug

echo Done!
exit /b 0

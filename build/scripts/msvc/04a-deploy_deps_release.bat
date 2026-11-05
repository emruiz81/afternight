@echo off
cd /d "%~dp0"

echo ****************04a-deploy_deps_release****************

REM The triplet 02-install_msvc_deps.bat installed the ports for: x64-windows unless AFTERNIGHT_VCPKG_TRIPLET
REM names another.
if not defined AFTERNIGHT_VCPKG_TRIPLET set "AFTERNIGHT_VCPKG_TRIPLET=x64-windows"

python "%~dp0..\tools\deploy_raw_deps.py" --config release
if errorlevel 1 exit /b 1
echo ****************Deploying MSVC 3rdparty deps****************

xcopy /Y /E ..\..\..\3rdparty\bin\msvc\release\*.dll ..\..\..\bin\msvc\release\

echo ****************Deploying MSVC vcpkg deps****************
REM Every library the binaries import is required: a copy that finds no file - a port the triplet does not
REM hold - fails the step here rather than the first program that loads it.
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\cfitsio.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\pugixml.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\openxisf\build-msvc-install\bin\openxisf-1.1.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\zstd.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libsodium.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\lz4.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\tiff.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\tbb12.dll ..\..\..\bin\msvc\release\ || exit /b 1
REM A rollback build without the private RAW packages reads camera files through the public
REM LibRaw; the default build imports neither library, and the library parity suite's copy
REM of them is deployed with the tests.
if "%AFTERNIGHT_RAWSPEED%"=="0" (
    xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\raw_r.dll ..\..\..\bin\msvc\release\ || exit /b 1
    xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\lcms2-2.dll ..\..\..\bin\msvc\release\ || exit /b 1
)
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libwebp.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libsharpyuv.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\liblzma.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libexpat.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\Lerc.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\jpeg8.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\intl-8.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\iconv-2.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\gslcblas.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\gsl.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\fftw3.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\fftw3f.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\exiv2.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\deflate.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\brotlidec.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\brotlicommon.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\z.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libpng16.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\avcodec-62.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\avutil-60.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\avformat-62.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\swscale-9.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libx265.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libx264-164.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\aom.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libwebpmux.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\swresample-6.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\pthreadVC3.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\cmark-gfm.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\cmark-gfm-extensions.dll ..\..\..\bin\msvc\release\ || exit /b 1
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\bin\libvpl.dll ..\..\..\bin\msvc\release\ || exit /b 1

echo ****************Deploying crashpad_handler****************
xcopy /Y ..\..\..\vcpkg\installed\%AFTERNIGHT_VCPKG_TRIPLET%\tools\crashpad\crashpad_handler.exe ..\..\..\bin\msvc\release\ || exit /b 1

REM Qt deployment is handled by windeployqt post-build command in the MSVC solution

xcopy /Y ..\..\..\kddockwidgets\build-msvc-install\bin\kddockwidgets-qt6.dll ..\..\..\bin\msvc\release\ || exit /b 1

xcopy /Y ..\..\..\python\python\tools_msvc\python3*.dll ..\..\..\bin\msvc\release\
if exist ..\..\..\python\python\tools_msvc\python.exe xcopy /Y ..\..\..\python\python\tools_msvc\python.exe ..\..\..\bin\msvc\release\
if exist ..\..\..\python\python\tools_msvc\python3.exe xcopy /Y ..\..\..\python\python\tools_msvc\python3.exe ..\..\..\bin\msvc\release\
if exist ..\..\..\python\python\tools_msvc\python3.14.exe xcopy /Y ..\..\..\python\python\tools_msvc\python3.14.exe ..\..\..\bin\msvc\release\
if exist ..\..\..\python\python\tools_msvc\bin\python.exe xcopy /Y ..\..\..\python\python\tools_msvc\bin\python.exe ..\..\..\bin\msvc\release\
if exist ..\..\..\python\python\tools_msvc\bin\python3.exe xcopy /Y ..\..\..\python\python\tools_msvc\bin\python3.exe ..\..\..\bin\msvc\release\
if exist ..\..\..\python\python\tools_msvc\bin\python3.14.exe xcopy /Y ..\..\..\python\python\tools_msvc\bin\python3.14.exe ..\..\..\bin\msvc\release\
xcopy /Y ..\..\..\python\*.zip ..\..\..\bin\msvc\release\
REM The bundled runtime is replaced whole: copied over an earlier deployment, it kept every package, distribution record
REM and library its source has since dropped, and the installer ships what this directory holds.
if exist ..\..\..\bin\msvc\release\PythonLib rmdir /S /Q ..\..\..\bin\msvc\release\PythonLib
xcopy /Y /E ..\..\..\python\python\tools_msvc\DLLs\*.* ..\..\..\bin\msvc\release\PythonLib\DLLs\
xcopy /Y /E ..\..\..\python\python\tools_msvc\Lib\site-packages\*.* ..\..\..\bin\msvc\release\PythonLib\Lib\site-packages\
xcopy /Y /E ..\..\..\python\python\tools_msvc\Scripts\*.* ..\..\..\bin\msvc\release\PythonLib\Scripts\
if exist ..\..\..\python\modules xcopy /Y /I /C /E ..\..\..\python\modules ..\..\..\bin\msvc\release\PythonLib\Lib\site-packages\
if exist ..\..\..\bin\msvc\release\PythonLib\Lib\site-packages\.gitkeep del /Q ..\..\..\bin\msvc\release\PythonLib\Lib\site-packages\.gitkeep

echo ****************Deploying the Visual C++ runtime****************
REM Windows provides the universal CRT and not the Visual C++ runtime the binaries here import, so the runtime is deployed
REM beside them from the toolset's own redistributable and ships with the package: the product then starts on a machine
REM where no redistributable was installed. check_deployed_runtimes.py holds this list to what the binaries import.
call "%~dp0_setup_vsdev_env.bat" || exit /b 1
cd /d "%~dp0"
set "AFTERNIGHT_VC_CRT="
for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do set "AFTERNIGHT_VC_CRT=%%D"
if not defined AFTERNIGHT_VC_CRT (
    echo The toolset's redistributable Visual C++ runtime was not found under "%VCToolsRedistDir%".
    exit /b 1
)
for %%F in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll msvcp140_atomic_wait.dll) do (
    xcopy /Y "%AFTERNIGHT_VC_CRT%\%%F" ..\..\..\bin\msvc\release\ || exit /b 1
)

echo ****************Deploying MSVC Scripts****************
xcopy /Y /I /C /E ..\..\..\scripts ..\..\..\bin\msvc\release\scripts

echo ****************Creating MSVC dirs****************
mkdir ..\..\..\bin\msvc\release\accessiblebridge 2>NUL
call deploy_extensions.bat release

echo Done!
exit /b 0

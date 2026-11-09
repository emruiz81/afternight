@echo off
cd /d "%~dp0"

REM Usage: 02-install_msvc_deps.bat [vcpkg]
REM
REM With no argument every dependency is installed. "vcpkg" installs the vcpkg ports and the private RAW
REM packages and stops, for continuous integration, which provisions OpenFITS, OpenXISF, Python, Qt and KDDockWidgets
REM in steps of its own. AFTERNIGHT_VCPKG_TRIPLET names the triplet the ports are installed for: x64-windows by
REM default, the full triplet a development machine builds both configurations from, or
REM x64-windows-release, which continuous integration installs to halve the dependency build. 04a and 09a
REM deploy from the same triplet.
if not "%~1"=="" if /I not "%~1"=="vcpkg" (
    echo Usage: 02-install_msvc_deps.bat [vcpkg]
    exit /b 2
)
if not defined AFTERNIGHT_VCPKG_TRIPLET set "AFTERNIGHT_VCPKG_TRIPLET=x64-windows"
call "%~dp0_setup_vsdev_env.bat"
if errorlevel 1 exit /b 1

echo ****************02-install_msvc_deps****************
PUSHD ..\..\..\vcpkg
call bootstrap-vcpkg.bat -disableMetrics

vcpkg install libpng --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install tbb[hwloc] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
REM FFmpeg and Expat come from overlay ports that pin a newer upstream release than this vcpkg checkout
REM carries: the pinned ones are inside published advisories for code the engine reaches from a file.
REM --recurse rebuilds what is already installed against them. See each overlay's README.
vcpkg install ffmpeg[aom,avcodec,avdevice,avfilter,avformat,swresample,swscale,webp,x264,x265,vpx,nvcodec,qsv,amf] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ --overlay-ports=..\build\vcpkg\overlay-ports --recurse || exit /b 1
vcpkg install libjpeg-turbo[jpeg8] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install liblzma --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install lz4 --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install zstd --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install libsodium --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install libdeflate[compression,decompression,gzip,zlib] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install tiff[jpeg,lerc,libdeflate,lzma,webp,zip,zstd] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install gsl --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
REM The portable vector abstraction the engine writes its CPU kernels against. The
REM contrib feature carries the vectorized transcendentals a later kernel family needs; it is
REM installed with the port so the two never disagree about a version.
vcpkg install highway[contrib] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install fftw3[avx,avx2,sse,sse2,threads] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install exiv2[bmff,nls,png,xmp] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ --overlay-ports=..\build\vcpkg\overlay-ports --recurse || exit /b 1
vcpkg install libraw[dng-lossy,x3f] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ --overlay-ports=..\build\vcpkg\overlay-ports --recurse || exit /b 1
vcpkg install opencv4[dnn,eigen,ipp,jpeg,nonfree,openexr,png,tbb,tiff,webp] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install sqlite3 --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install crashpad --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install gtest --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install cmark-gfm --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install eigen3 --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install fmt --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install nlohmann-json --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
REM pugixml, zlib, LZ4 and Zstandard are what OpenXISF, the XISF codec's library, is built against, and zlib
REM is what OpenFITS, the FITS codec's library, is; _build_openxisf.bat and _build_openfits.bat build them
REM from their published releases below.
vcpkg install pugixml --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install zlib --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install pybind11 --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ --overlay-ports=..\build\vcpkg\overlay-ports || exit /b 1
vcpkg install readerwriterqueue --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1
vcpkg install spdlog[fmt,wchar] --triplet=%AFTERNIGHT_VCPKG_TRIPLET% --vcpkg-root=.\ || exit /b 1

POPD

call "%~dp0install_raw_deps.bat"
if errorlevel 1 exit /b 1
if /I "%~1"=="vcpkg" (
    echo Done!
    exit /b 0
)

PUSHD ..\python
call setup_python_msvc.bat
POPD 

PUSHD ..\..\..\python\python\tools_msvc
.\python.exe -m venv venvs\qt6
call venvs\qt6\Scripts\activate.bat
venvs\qt6\Scripts\python.exe -m pip install aqtinstall
REM Qt 6.10.3 Windows package is currently named win64_msvc2022_64 (used with VS2026)
venvs\qt6\Scripts\python.exe -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O ..\..\..\Qt6
call venvs\qt6\Scripts\deactivate.bat
POPD 

call _build_openxisf.bat
if errorlevel 1 exit /b 1

call _build_openfits.bat
if errorlevel 1 exit /b 1

call _build_kddockwidgets.bat
if errorlevel 1 exit /b 1

echo Done!

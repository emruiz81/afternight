#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1

# Usage: 02-install_linux_deps.sh [system|vcpkg]
#
# With no argument every dependency is installed. "system" installs the distribution packages and stops, and
# "vcpkg" installs the vcpkg ports and the private RAW packages and stops: continuous integration runs the two
# on their own, beside the OpenFITS, OpenXISF, Qt, Python and KDDockWidgets steps it caches separately.
stage="${1:-all}"
case "$stage" in
    all|system|vcpkg) ;;
    *) echo "Usage: $0 [system|vcpkg]" >&2; exit 2 ;;
esac

echo ****************02-install_linux_deps****************

if [[ "$stage" != vcpkg ]]; then
    echo ** Installing deps **

    sudo apt-get update -y || exit 1
    sudo apt-get install -y \
        build-essential \
        gdb \
        valgrind \
        lldb \
        libpng-dev \
        libjpeg-dev \
        libtiff-dev \
        libfftw3-dev \
        libxcb1-dev \
        libopencv-dev \
        libexiv2-dev \
        libicu-dev \
        libgsl-dev \
        libtbb-dev \
        libgtest-dev \
        ffmpeg \
        libavcodec-dev \
        libavformat-dev \
        libavfilter-dev \
        libswscale-dev \
        libavutil-dev \
        libvpl-dev \
        liblz4-dev \
        libzstd-dev \
        libsodium-dev \
        zstd \
        mesa-common-dev \
        libgl1-mesa-dev \
        python3-pip \
        python3-venv \
        libsqlite3-dev \
        pkg-config \
        curl \
        cmake \
        ninja-build \
        nasm \
        autoconf \
        autoconf-archive \
        automake \
        libtool \
        libtool-bin \
        libgl1-mesa-dev libx11-dev libx11-xcb-dev \
        libxext-dev libxfixes-dev libxi-dev libxrender-dev \
        libxcb1-dev libxcb-glx0-dev libxcb-keysyms1-dev \
        libxcb-image0-dev libxcb-shm0-dev libxcb-icccm4-dev \
        libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev \
        libxcb-randr0-dev libxcb-render-util0-dev \
        libxcb-xinerama0-dev libxcb-xkb-dev \
        libxkbcommon-dev libxkbcommon-x11-dev \
        libglib2.0-0 libdbus-1-3 \
        libnss3 libsm6 libice6 libfontconfig1 \
        libfreetype6 libxrandr2 libxcomposite1 libxcursor1 \
        libxdamage1 libxinerama1 zlib1g \
        libwayland-dev libwayland-client0 libwayland-cursor0 libwayland-egl1 \
        libx11-xcb1 libxcb1 libxcb-util1 \
        libxcb-icccm4 libxcb-randr0 libxcb-render0 \
        libxcb-render-util0 libxcb-shape0 libxcb-sync1 \
        libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 \
        libxkbcommon0 libxkbcommon-x11-0 \
        libfuse2t64 \
        clang lld \
        clang-21 \
        zip unzip tar \
        libxcb-cursor0 || exit 1
fi
if [[ "$stage" == system ]]; then
    echo Done!
    exit 0
fi

echo "** Installing vcpkg dependencies (x64-linux) **"
pushd ../../../vcpkg || exit 1
if [ ! -x "./vcpkg" ]; then
    ./bootstrap-vcpkg.sh -disableMetrics || exit 1
fi
./vcpkg install crashpad --triplet x64-linux --overlay-ports=../build/scripts/linux/vcpkg/overlay-ports || exit 1
./vcpkg install cmark-gfm --triplet x64-linux || exit 1
# The portable vector abstraction the engine writes its CPU kernels against. It comes
# from vcpkg on both platforms rather than from a distribution package, so both compile the same
# port version; the contrib feature carries the vectorized transcendentals a later kernel family
# needs and is installed with the port so the two never disagree about a version.
./vcpkg install 'highway[contrib]' --triplet x64-linux || exit 1
./vcpkg install fmt --triplet x64-linux || exit 1
# The engine takes Exiv2 and its XML stack from pinned position-independent static archives, not from
# the distribution's copy above, whose Expat release can lag fixes for hostile XMP input and must not
# enter the engine's runtime closure.
./vcpkg install expat --triplet x64-linux --overlay-ports=../build/vcpkg/overlay-ports --recurse || exit 1
./vcpkg install 'exiv2[bmff,png,xmp]' --triplet x64-linux --overlay-ports=../build/vcpkg/overlay-ports --recurse || exit 1
# The engine links the FFmpeg release the overlay pins (build/vcpkg/overlay-ports/ffmpeg) as
# position-independent static archives, so its shared library exposes none of FFmpeg's symbols and the
# distribution's FFmpeg above stays out of its runtime closure.
./vcpkg install 'ffmpeg[core,avcodec,avformat,swresample,swscale,vpx,x264,x265]' --triplet x64-linux --overlay-ports=../build/vcpkg/overlay-ports --recurse || exit 1
./vcpkg install 'libraw[dng-lossy,x3f]' --triplet x64-linux --overlay-ports=../build/vcpkg/overlay-ports --recurse || exit 1
./vcpkg install libsodium --triplet x64-linux || exit 1
./vcpkg install nlohmann-json --triplet x64-linux || exit 1
# pugixml, zlib, LZ4 and Zstandard are what OpenXISF, the XISF codec's library, is built against, and zlib is what
# OpenFITS, the FITS codec's library, is: position-independent static archives, which each shared library carries.
# _build_openxisf.sh and _build_openfits.sh build them from their published releases below.
./vcpkg install pugixml --triplet x64-linux || exit 1
./vcpkg install zlib --triplet x64-linux || exit 1
./vcpkg install lz4 --triplet x64-linux || exit 1
./vcpkg install zstd --triplet x64-linux || exit 1
./vcpkg install pybind11 --triplet x64-linux || exit 1
./vcpkg install readerwriterqueue --triplet x64-linux || exit 1
./vcpkg install 'spdlog[fmt]' --triplet x64-linux || exit 1
popd || exit 1

./install_raw_deps.sh || exit 1
if [[ "$stage" == vcpkg ]]; then
    echo Done!
    exit 0
fi

echo ** Installing Qt **

python3 -m venv ~/venvs/qt
source ~/venvs/qt/bin/activate
pip install aqtinstall
# Qt online packages for Linux desktop are distributed as linux_gcc_64.
aqt install-qt linux desktop 6.10.3 linux_gcc_64 -O ../../../Qt6
deactivate

# Install Python packages
pushd ../python
./setup_python_linux.sh
popd 

# Build OpenXISF and OpenFITS from their published releases
./_build_openxisf.sh || exit 1
./_build_openfits.sh || exit 1

# Build KDDockWidgets
./_build_kddockwidgets.sh

echo Done!

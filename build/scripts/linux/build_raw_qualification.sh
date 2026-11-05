#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
config="${1:-Release}"
case "${config,,}" in release) config=Release;; debug) config=Debug;; *) exit 2;; esac
native=OFF
if [[ "${AFTERNIGHT_RAWSPEED:-1}" == 0 ]]; then native=ON; fi
cmake --fresh -S "$repo_root/build/scripts/tools/rawspeed" -B "$repo_root/obj/raw-qualification/$config" -G Ninja \
    -DCMAKE_BUILD_TYPE="$config" -DAN_NATIVE_ONLY="$native" -DCMAKE_C_COMPILER=/usr/bin/clang-21 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-21 \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/vcpkg/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-linux-raw -DVCPKG_OVERLAY_TRIPLETS="$repo_root/build/vcpkg/triplets" \
    -DVCPKG_MANIFEST_MODE=OFF
cmake --build "$repo_root/obj/raw-qualification/$config" --parallel 4
cmake --install "$repo_root/obj/raw-qualification/$config" --prefix "$repo_root/bin/clang/${config,,}"

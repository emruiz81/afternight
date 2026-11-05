#!/usr/bin/env bash
# Instrument the installed, patched sources without altering packages or their buildtrees.
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
work="$repo_root/obj/raw-sanitizers"
raw_prefix="$repo_root/vcpkg/installed/x64-linux-raw"
installed="$work/installed"
# Keep Debug assertions with enough optimization for full-size decoder/allocation sweeps.
flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=vptr'
common=(-G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/bin/clang-21
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++-21 "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags"
    "-DCMAKE_EXE_LINKER_FLAGS=$flags" -DBUILD_SHARED_LIBS=OFF
    "-DCMAKE_PREFIX_PATH=$installed;$raw_prefix"
    "-DCMAKE_TOOLCHAIN_FILE=$repo_root/vcpkg/scripts/buildsystems/vcpkg.cmake"
    -DVCPKG_TARGET_TRIPLET=x64-linux-raw "-DVCPKG_OVERLAY_TRIPLETS=$repo_root/build/vcpkg/triplets"
    -DVCPKG_MANIFEST_MODE=OFF)
for port in afternight-rawspeed afternight-libraw; do
    source="$raw_prefix/share/$port/source"
    test -d "$source"
    private_packages=()
    if [[ "$port" == afternight-libraw ]]; then
        # vcpkg prepends its installed tree to CMAKE_PREFIX_PATH. Pin the instrumented
        # dependency explicitly so this build cannot silently link the ordinary archive.
        private_packages=("-DAfterNightRawSpeed_DIR=$installed/share/afternight-rawspeed")
    fi
    cmake --fresh -S "$raw_prefix/share/$port/recipe" -B "$work/$port" "${common[@]}" \
        "${private_packages[@]}" "-DRAW_SOURCE=$source"
    cmake --build "$work/$port" --parallel 4
    cmake --install "$work/$port" --prefix "$installed"
done
cmake --fresh -S "$repo_root/build/scripts/tools/rawspeed" -B "$work/boundary" "${common[@]}" \
    -DAN_ENGINE_DRIVERS=OFF "-DAN_PRIVATE_PREFIX=$installed"
cmake --build "$work/boundary" --parallel 4
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:print_stacktrace=1:allocator_may_return_null=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
"$work/boundary/RawNativeTests"
python3 "$repo_root/build/scripts/tools/rawspeed/boundary.py" --bin "$work/boundary" \
    --wrapper "$installed/tools/afternight-rawspeed/AnRawSpeedTests" --output "$work/evidence.json" "$@"

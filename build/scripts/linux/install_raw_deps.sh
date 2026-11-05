#!/usr/bin/env bash
set -euo pipefail
if [[ "${AFTERNIGHT_RAWSPEED:-1}" == 0 ]]; then exit 0; fi
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
# The RAW stack is built by the compiler build/vcpkg/toolchains/raw-clang-linux.cmake nominates, which the system stage
# of 02-install_linux_deps.sh installs by its versioned package, whichever clang the distribution's metapackage names.
for compiler in /usr/bin/clang-21 /usr/bin/clang++-21; do
    if [[ ! -x "$compiler" ]]; then
        echo "Error: $compiler, the compiler the RAW stack is built by, is not installed; run 02-install_linux_deps.sh" >&2
        exit 1
    fi
done
"$repo_root/vcpkg/vcpkg" install afternight-libraw --triplet=x64-linux-raw \
    --overlay-ports="$repo_root/build/vcpkg/overlay-ports" \
    --overlay-triplets="$repo_root/build/vcpkg/triplets" --recurse "$@"

#!/usr/bin/env bash
#
# Builds an oneTBB shared library instrumented with ThreadSanitizer, for the sanitizer run to load
# in place of the distribution's.
#
# ThreadSanitizer proves a happens-before edge exists by watching the code that establishes it. The
# distribution's liboneTBB is not instrumented, so every edge the oneTBB scheduler establishes -
# spawning a task, stealing it, joining the region - is invisible, and the instrument concludes that
# everything the region hands from one thread to another is racy. That is not a subtle effect: the
# first parallel region this engine ran under the instrument produced 156 reports, most of them
# naming this engine's own files, and every one of them was the same missing edge. A loop counter
# read by a worker and written by the thread that started the region is reported; so is the range
# object oneTBB splits inside its own header.
#
# The alternative was a suppression file scoped to oneTBB's frames, and it was rejected after being
# measured rather than on taste. ThreadSanitizer matches a suppression against *every* frame of a
# report, not against the frame the access is in, and an engine race inside a parallel loop body
# always has oneTBB frames below it - so a suppression wide enough to silence the noise is also wide
# enough to silence the defects the local run exists to find. Instrumenting the library instead removes
# the reports by making the synchronisation visible, which is the difference between an instrument
# that agrees with the suite and one that has been told not to look.
#
# The library is built from the version the installed headers declare, so it is a drop-in for the
# one they were compiled against: only the implementation is replaced, and the header code that gets
# inlined into the engine is the same code production compiles. The source archive is digest-pinned
# per version, which is why a version nobody has pinned yet fails loudly here instead of quietly
# building something nobody can reproduce.
#
# oneTBB supports this directly - TBB_SANITIZE is upstream's own option - so nothing here patches or
# works around the library.
#
# Usage: build_tsan_onetbb.sh [output-directory]
#   ONETBB_INCLUDE_ROOT  where to read the installed oneapi/tbb/version.h (default /usr/include)
#   ONETBB_SOURCE_SHA256 the archive digest, for a version this script does not pin yet
#
# Prints the directory holding the built libtbb.so.12 as its last line, which is what belongs at the
# front of LD_LIBRARY_PATH for an instrumented run.

set -euo pipefail

INCLUDE_ROOT="${ONETBB_INCLUDE_ROOT:-/usr/include}"
OUTPUT_DIRECTORY="${1:-}"

VERSION_HEADER="${INCLUDE_ROOT}/oneapi/tbb/version.h"
if [[ ! -f "${VERSION_HEADER}" ]]; then
    echo "No oneTBB headers at ${VERSION_HEADER}. Install libtbb-dev, or name the include root with"
    echo "ONETBB_INCLUDE_ROOT." >&2
    exit 2
fi

# What the headers say they are. The library has to match them rather than the other way round: the
# headers are what the engine was compiled against, and they are not being replaced.
version_field() {
    awk -v field="TBB_VERSION_${1}" '$1 == "#define" && $2 == field {print $3; exit}' "${VERSION_HEADER}"
}

MAJOR="$(version_field MAJOR)"
MINOR="$(version_field MINOR)"
PATCH="$(version_field PATCH)"
if [[ -z "${MAJOR}" || -z "${MINOR}" || -z "${PATCH}" ]]; then
    echo "${VERSION_HEADER} does not declare TBB_VERSION_MAJOR/MINOR/PATCH, so this script cannot"
    echo "tell which oneTBB to build." >&2
    exit 2
fi
VERSION="${MAJOR}.${MINOR}.${PATCH}"

# One digest per version this repository has actually seen. Ubuntu 24.04 ships 2021.11.0; Ubuntu
# 26.04 ships 2022.3.0. A validation host that moves to a version not listed here stops this run
# with the line below rather than building an archive nobody verified.
declare -A PINNED_SHA256=(
    ["2021.11.0"]="782ce0cab62df9ea125cdea253a50534862b563f1d85d4cda7ad4e77550ac363"
    ["2022.0.0"]="e8e89c9c345415b17b30a2db3095ba9d47647611662073f7fbf54ad48b7f3c2a"
    ["2022.1.0"]="ed067603ece0dc832d2881ba5c516625ac2522c665d95f767ef6304e34f961b5"
    ["2022.2.0"]="f0f78001c8c8edb4bddc3d4c5ee7428d56ae313254158ad1eec49eced57f6a5b"
    ["2022.3.0"]="01598a46c1162c27253a0de0236f520fd8ee8166e9ebb84a4243574f88e6e50a"
)

EXPECTED_SHA256="${ONETBB_SOURCE_SHA256:-${PINNED_SHA256[${VERSION}]:-}}"
ARCHIVE_URL="https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v${VERSION}.tar.gz"
if [[ -z "${EXPECTED_SHA256}" ]]; then
    echo "The installed oneTBB headers are ${VERSION}, and no digest is pinned for it." >&2
    echo "Verify the release, then add its digest to this script:" >&2
    echo "  curl -fsSL ${ARCHIVE_URL} | sha256sum" >&2
    exit 1
fi

echo "Building oneTBB ${VERSION} with ThreadSanitizer, to match the headers in ${INCLUDE_ROOT}."

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

curl -fsSL -o "${WORK}/onetbb.tar.gz" "${ARCHIVE_URL}"
if ! echo "${EXPECTED_SHA256}  ${WORK}/onetbb.tar.gz" | sha256sum --check --status; then
    echo "The downloaded archive does not have the expected SHA-256, so it is not the release this" >&2
    echo "script means to build. Nothing was built." >&2
    exit 1
fi
tar -xzf "${WORK}/onetbb.tar.gz" -C "${WORK}"
SOURCE="${WORK}/oneTBB-${VERSION}"

# TBB_SANITIZE is upstream's option and takes the same spelling the compiler does. Tests and
# examples are off because only the library is wanted, and TBB_STRICT is off because a warning in
# somebody else's source should not stop this local validation run.
cmake -S "${SOURCE}" -B "${WORK}/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTBB_TEST=OFF \
    -DTBB_EXAMPLES=OFF \
    -DTBB_STRICT=OFF \
    -DTBB_SANITIZE=thread \
    -DCMAKE_C_COMPILER="${CC:-clang}" \
    -DCMAKE_CXX_COMPILER="${CXX:-clang++}" >/dev/null
cmake --build "${WORK}/build" -j"$(nproc)" --target tbb >/dev/null

BUILT="$(find "${WORK}/build" -name 'libtbb.so.12*' -type f -print -quit)"
if [[ -z "${BUILT}" ]]; then
    echo "The build produced no libtbb.so.12." >&2
    exit 1
fi

# The point of the exercise, checked rather than assumed: an uninstrumented library would leave the
# run reporting the same 156 races and looking like it had used an instrumented scheduler.
#
# The symbols are counted rather than matched with `grep -q`, which exits on the first hit and leaves
# nm writing to a closed pipe - under `pipefail` that reads as a failed pipeline, so the check would
# fail on exactly the library it is meant to accept.
INSTRUMENTED_SYMBOLS="$(nm -D "${BUILT}" 2>/dev/null | grep -c '__tsan' || true)"
if [[ "${INSTRUMENTED_SYMBOLS}" -eq 0 ]]; then
    echo "${BUILT} contains no ThreadSanitizer symbols, so it would not make oneTBB's" >&2
    echo "synchronisation visible. Check that ${CXX:-clang++} supports -fsanitize=thread." >&2
    exit 1
fi

DESTINATION="${OUTPUT_DIRECTORY:-$(dirname "${BUILT}")}"
if [[ -n "${OUTPUT_DIRECTORY}" ]]; then
    mkdir -p "${DESTINATION}"
    # The soname link as well as the file: the engine records libtbb.so.12, and a directory holding
    # only libtbb.so.12.17 would be skipped by the loader without a word.
    cp -a "$(dirname "${BUILT}")"/libtbb.so* "${DESTINATION}/"
fi

echo "Built oneTBB ${VERSION} with ThreadSanitizer in ${DESTINATION}."
echo "${DESTINATION}"

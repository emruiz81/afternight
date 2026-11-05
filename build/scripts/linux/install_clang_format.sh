#!/usr/bin/env bash
#
# Installs the clang-format version pinned for AfterNight formatting checks.
#
# Ubuntu ships a clang-format that trails the version bundled with Visual Studio, and different
# clang-format majors disagree on line breaking, so the pinned major is installed from apt.llvm.org
# to keep local Windows checks and Linux CI on the same formatter.

set -euo pipefail

CLANG_FORMAT_MAJOR="${CLANG_FORMAT_MAJOR:-22}"

if command -v "clang-format-${CLANG_FORMAT_MAJOR}" >/dev/null 2>&1; then
    echo "clang-format-${CLANG_FORMAT_MAJOR} is already installed."
    exit 0
fi

codename="$(. /etc/os-release && echo "${VERSION_CODENAME}")"
if [[ -z "${codename}" ]]; then
    echo "Could not determine the Ubuntu/Debian codename required by apt.llvm.org."
    exit 1
fi

sudo apt-get update -y
sudo apt-get install -y ca-certificates curl gnupg

curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key |
    sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null
echo "deb http://apt.llvm.org/${codename}/ llvm-toolchain-${codename}-${CLANG_FORMAT_MAJOR} main" |
    sudo tee "/etc/apt/sources.list.d/llvm-toolchain-${CLANG_FORMAT_MAJOR}.list" >/dev/null

sudo apt-get update -y
sudo apt-get install -y "clang-format-${CLANG_FORMAT_MAJOR}"

"clang-format-${CLANG_FORMAT_MAJOR}" --version

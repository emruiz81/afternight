#!/usr/bin/env bash
#
# Installs the clang-tidy version pinned for the AfterNightEngine naming check.
#
# Same reasoning as install_clang_format.sh, and the same pinned major for the same reason: Ubuntu
# ships a clang-tidy several releases behind the one Visual Studio bundles, and the naming check's
# options are not identical across majors - an option a newer release renamed is silently ignored by
# an older one, which turns part of the check off without failing. Installing the pinned major from
# apt.llvm.org keeps the local Windows run and Linux CI checking the same rules.

set -euo pipefail

CLANG_TIDY_MAJOR="${CLANG_TIDY_MAJOR:-22}"

if command -v "clang-tidy-${CLANG_TIDY_MAJOR}" >/dev/null 2>&1; then
    echo "clang-tidy-${CLANG_TIDY_MAJOR} is already installed."
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
echo "deb http://apt.llvm.org/${codename}/ llvm-toolchain-${codename}-${CLANG_TIDY_MAJOR} main" |
    sudo tee "/etc/apt/sources.list.d/llvm-toolchain-${CLANG_TIDY_MAJOR}.list" >/dev/null

sudo apt-get update -y
sudo apt-get install -y "clang-tidy-${CLANG_TIDY_MAJOR}"

"clang-tidy-${CLANG_TIDY_MAJOR}" --version

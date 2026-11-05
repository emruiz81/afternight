#!/usr/bin/env bash
#
# Installs a doxygen new enough to run the AfterNightEngine documentation check.
#
# Unlike the formatter and clang-tidy, doxygen is deliberately not pinned to one version: the
# checker prints doxygen's diagnostics verbatim, so a newer release that reports more reads as a
# message rather than as a silent gap. There is a floor, though, and the first Linux run of that
# check is what found it.
#
# Doxygen does not refuse a tag it does not recognise. It prints one warning and carries on with
# that rule switched off. HIDE_UNDOC_NAMESPACES is understood from 1.10 and ignored by the 1.9.8
# that Ubuntu 24.04 ships, and it is the tag that makes an undocumented namespace a failure - so
# below the floor the check runs with a rule missing and nothing says so afterwards. That is the
# shape of green tick a check exists to prevent, which is why the floor is enforced here and by the
# checker rather than left to whatever the distribution happens to ship.
#
# The distribution package is used whenever it satisfies the floor, so nothing is downloaded on a
# machine that is already current. Only when it does not is a release binary fetched, and that one
# is digest-pinned: an unpinned binary download is a dependency nobody can reproduce.
#
# Usage: install_doxygen.sh
#   DOXYGEN_MINIMUM_VERSION  the floor to satisfy (default below)
#   DOXYGEN_INSTALL_PREFIX   where a downloaded doxygen is installed (default /usr/local)

set -euo pipefail

MINIMUM_VERSION="${DOXYGEN_MINIMUM_VERSION:-1.10.0}"
INSTALL_PREFIX="${DOXYGEN_INSTALL_PREFIX:-/usr/local}"

# The release fetched when the distribution's is too old, and the digest it must have.
PINNED_VERSION="1.14.0"
PINNED_SHA256="e5d6ae24d0bf3f0cdc4d8f146726b89ca323922f19441af99b1872d503665ad6"
PINNED_RELEASE="https://github.com/doxygen/doxygen/releases/download/Release_1_14_0"
PINNED_URL="${PINNED_RELEASE}/doxygen-${PINNED_VERSION}.linux.bin.tar.gz"

# The numeric version a doxygen reports, which it prints as "1.14.0 (<commit>)".
version_of() {
    local executable="${1:-}"
    [[ -n "${executable}" ]] || return 0
    "${executable}" --version 2>/dev/null | head -n1 | awk '{print $1}'
}

# Whether the first version is at least the second. Empty never satisfies anything.
at_least() {
    [[ -n "${1}" ]] || return 1
    [[ "$(printf '%s\n%s\n' "${1}" "${2}" | sort -V | head -n1)" == "${2}" ]]
}

can_sudo() {
    [[ "${EUID}" -eq 0 ]] && return 0
    command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1
}

as_root() {
    if [[ "${EUID}" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

# What apt would install, asked before installing it: on a distribution whose doxygen is below the
# floor there is no point fetching it first and replacing it a step later.
apt_candidate_version() {
    command -v apt-cache >/dev/null 2>&1 || return 0
    apt-cache policy doxygen 2>/dev/null |
        awk '/Candidate:/ {print $2}' |
        sed -e 's/^[0-9]*://' -e 's/[-+~].*$//'
}

installed="$(version_of "$(command -v doxygen || true)")"
if at_least "${installed}" "${MINIMUM_VERSION}"; then
    echo "doxygen ${installed} is already installed and satisfies the ${MINIMUM_VERSION} floor."
    exit 0
fi

if [[ -n "${installed}" ]]; then
    echo "doxygen ${installed} is installed, which is below the ${MINIMUM_VERSION} floor."
fi

candidate="$(apt_candidate_version)"
if at_least "${candidate}" "${MINIMUM_VERSION}"; then
    if can_sudo; then
        echo "Installing doxygen ${candidate} from the distribution."
        as_root apt-get update -y
        as_root apt-get install -y doxygen
        installed="$(version_of "$(command -v doxygen || true)")"
        if at_least "${installed}" "${MINIMUM_VERSION}"; then
            doxygen --version
            exit 0
        fi
        echo "The distribution package reports ${installed:-no version}, which is below the floor."
    else
        echo "The distribution offers doxygen ${candidate}, but installing it needs root."
    fi
elif [[ -n "${candidate}" ]]; then
    echo "The distribution offers doxygen ${candidate}, below the ${MINIMUM_VERSION} floor."
fi

echo "Fetching doxygen ${PINNED_VERSION} from ${PINNED_URL}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

curl -fsSL -o "${work}/doxygen.tar.gz" "${PINNED_URL}"
if ! echo "${PINNED_SHA256}  ${work}/doxygen.tar.gz" | sha256sum --check --status; then
    echo "The downloaded archive does not have the pinned SHA-256, so it is not the release this"
    echo "script means to install. Nothing was installed."
    exit 1
fi

tar -xzf "${work}/doxygen.tar.gz" -C "${work}"

# Only the generator itself: doxyindexer, doxysearch and doxywizard belong to a search-enabled or
# interactive workflow that neither the check nor the reference build uses.
binary="${work}/doxygen-${PINNED_VERSION}/bin/doxygen"
target="${INSTALL_PREFIX}/bin"
if mkdir -p "${target}" 2>/dev/null && [[ -w "${target}" ]]; then
    install -m 0755 "${binary}" "${target}/doxygen"
elif can_sudo; then
    as_root install -d -m 0755 "${target}"
    as_root install -m 0755 "${binary}" "${target}/doxygen"
else
    echo "${target} is not writable and sudo is not available without a password."
    echo "Name a writable directory with DOXYGEN_INSTALL_PREFIX, or run this with sudo."
    exit 1
fi

installed="$(version_of "${target}/doxygen")"
if ! at_least "${installed}" "${MINIMUM_VERSION}"; then
    echo "${target}/doxygen reports ${installed:-no version}, which does not satisfy the floor."
    exit 1
fi

echo "Installed doxygen ${installed} in ${target}."
resolved="$(command -v doxygen || true)"
if [[ "${resolved}" != "${target}/doxygen" ]]; then
    # Said plainly rather than left to be discovered by a check that runs the older one anyway.
    echo "Note: doxygen on PATH still resolves to ${resolved:-nothing}. Put ${target} ahead of it,"
    echo "or name the new one with DOXYGEN_BIN."
fi

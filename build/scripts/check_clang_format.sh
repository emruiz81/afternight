#!/usr/bin/env bash

set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

find_command() {
    for candidate in "$@"; do
        if [[ -n "$candidate" ]] && command -v "$candidate" >/dev/null 2>&1; then
            command -v "$candidate"
            return 0
        fi
    done
    return 1
}

clang_format="$(find_command "${CLANG_FORMAT_BIN:-}" clang-format-22 clang-format || true)"

if [[ -z "$clang_format" ]]; then
    echo "clang-format tooling is missing."
    echo "Install clang-format-22, then retry. On Ubuntu: build/scripts/linux/install_clang_format.sh"
    exit 2
fi

base_ref="${1:-${CLANG_FORMAT_BASE:-}}"
if [[ -z "$base_ref" ]]; then
    if git rev-parse --verify origin/main >/dev/null 2>&1; then
        base_ref="origin/main"
    else
        base_ref="HEAD~1"
    fi
fi

if ! git rev-parse --verify "$base_ref" >/dev/null 2>&1; then
    echo "Could not resolve clang-format base ref: $base_ref"
    echo "Pass a base ref explicitly, for example: build/scripts/check_clang_format.sh origin/main"
    exit 2
fi

declare -A seen=()
declare -a changed_files=()

add_candidate() {
    local path="$1"
    case "$path" in
        *.c|*.h|*.cc|*.cpp|*.cxx|*.hpp|*.hxx|*.hh) ;;
        *) return ;;
    esac
    [[ -f "$path" ]] || return
    [[ -n "${seen[$path]:-}" ]] && return
    seen["$path"]=1
    changed_files+=("$path")
}

while IFS= read -r -d '' path; do
    add_candidate "$path"
done < <(git diff --name-only --diff-filter=ACMR -z "$base_ref" -- src)

while IFS= read -r -d '' path; do
    add_candidate "$path"
done < <(git ls-files --others --exclude-standard -z -- src)

if (( ${#changed_files[@]} == 0 )); then
    echo "No changed C/C++ files under src against $base_ref."
    exit 0
fi

"$clang_format" --dry-run --Werror --style=file "${changed_files[@]}"
echo "clang-format check passed for ${#changed_files[@]} complete changed C/C++ files against $base_ref."

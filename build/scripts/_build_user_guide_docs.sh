#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "$script_dir/../.." && pwd -P)"
docs_dir="$repo_root/docs/user-guide"
venv_dir="$docs_dir/.venv"
requirements_file="$docs_dir/requirements.txt"
# With the argument "theme" the guide is built in its own theme, from mkdocs.theme.yml into site-theme/.
config_name="mkdocs.yml"
case "${1:-}" in
    "") ;;
    theme) config_name="mkdocs.theme.yml" ;;
    *) echo "Error: Unknown argument '$1'. Pass nothing for the guide, or 'theme' for the guide in its own theme." >&2
       exit 1 ;;
esac
mkdocs_config="$docs_dir/$config_name"

fail() {
    echo "Error: $*" >&2
    exit 1
}

find_python() {
    if [[ -n "${PYTHON:-}" ]]; then
        if command -v "$PYTHON" >/dev/null 2>&1 || [[ -x "$PYTHON" ]]; then
            printf '%s\n' "$PYTHON"
            return 0
        fi
        fail "PYTHON is set to '$PYTHON', but it is not executable. Set PYTHON to a working interpreter or install python3."
    fi

    for candidate in python3 python; do
        if command -v "$candidate" >/dev/null 2>&1; then
            command -v "$candidate"
            return 0
        fi
    done

    fail "Python was not found. Install Python 3 with venv support, then retry."
}

find_venv_python() {
    for candidate in "$venv_dir/bin/python" "$venv_dir/Scripts/python.exe" "$venv_dir/Scripts/python"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

[[ -f "$requirements_file" ]] || fail "Missing $requirements_file."
[[ -f "$mkdocs_config" ]] || fail "Missing $mkdocs_config."

base_python="$(find_python)"

if ! "$base_python" -m venv --help >/dev/null 2>&1; then
    fail "Python venv support is unavailable. Install the python3-venv package or a Python distribution with venv."
fi

venv_python="$(find_venv_python || true)"
if [[ -z "$venv_python" ]]; then
    echo "Creating documentation virtual environment at $venv_dir"
    if ! "$base_python" -m venv "$venv_dir"; then
        fail "Could not create $venv_dir. Install Python venv support and ensure the docs directory is writable."
    fi
    venv_python="$(find_venv_python || true)"
fi

[[ -n "$venv_python" ]] || fail "Could not find the virtual environment Python under $venv_dir."

if ! "$venv_python" -m pip --version >/dev/null 2>&1; then
    echo "pip is missing in $venv_dir; attempting to bootstrap it with ensurepip."
    if ! "$venv_python" -m ensurepip --upgrade; then
        fail "pip is unavailable in $venv_dir. Recreate the virtual environment with a Python install that includes pip."
    fi
fi

cd "$repo_root"

echo "Installing user guide dependencies into docs/user-guide/.venv"
export PIP_REQUIRE_VIRTUALENV=true
if ! "$venv_python" -m pip install --require-hashes -r "docs/user-guide/requirements.txt"; then
    fail "Dependency installation failed. Check Python version compatibility, network access, and the hashed requirements file."
fi

echo "Building user guide with strict MkDocs validation"
if ! "$venv_python" -m mkdocs build -f "docs/user-guide/$config_name" --strict; then
    fail "MkDocs strict build failed. Fix the documentation warnings or errors above and retry."
fi

echo "User guide build completed successfully."

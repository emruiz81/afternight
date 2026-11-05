#!/bin/bash
# Deploys a package profile's targets, or the named ones, from bin/clang/<config> into a clean directory with the
# libraries their imports reach there, and records every file's hash and imports in deploy-manifest.json. The
# deployment directory is never the build output directory, so a stale library in bin cannot satisfy a deployed
# binary; the deployed binaries carry no run path, so run them with the deployment directory and its lib/ on
# LD_LIBRARY_PATH. The Windows counterpart is build/scripts/msvc/deploy_active_targets.bat.
#
# Usage: deploy_active_targets.sh [release|debug] (--profile <name> | --targets <a,b>) [--destination <dir>] [--replace]
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

CONFIG="release"
if [ "$1" = "release" ] || [ "$1" = "debug" ]; then
    CONFIG="$1"
    shift
fi

echo "****************deploy_active_targets_${CONFIG}****************"
python3 ../tools/active_targets.py deploy "$CONFIG" "$@"

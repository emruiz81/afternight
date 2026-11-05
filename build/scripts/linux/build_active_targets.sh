#!/bin/bash
# Builds the active target set the target registry names, build/scripts/tools/consumer_targets.txt, in
# dependency order: the main workspace, and with --tests the test workspace. It holds the generated tree to the
# registry first and builds nothing when they disagree, so a stale project or a target the registry does not
# build cannot be built by accident. The third-party workspace is built by 03a/03b.
#
# It builds what 04a/08a build, named by the registry rather than by whatever the generated tree holds. The
# Windows counterpart is build/scripts/msvc/build_active_targets.bat.
#
# Usage: build_active_targets.sh [release|debug] [--tests] [--dry-run]
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

CONFIG="release"
EXTRA=()
for argument in "$@"; do
    case "$argument" in
        release|debug) CONFIG="$argument" ;;
        --tests|--dry-run) EXTRA+=("$argument") ;;
        *)
            echo "Usage: build_active_targets.sh [release|debug] [--tests] [--dry-run]"
            exit 2
            ;;
    esac
done

echo "****************build_active_targets_${CONFIG}****************"
python3 ../tools/active_targets.py build "$CONFIG" "${EXTRA[@]}"
echo Done!

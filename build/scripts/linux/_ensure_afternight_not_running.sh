#!/bin/bash
set -euo pipefail

process_names=("$@")
if [ ${#process_names[@]} -eq 0 ]; then
    process_names=(AfterNight AfterNightExtensionHost AfterNightExtensionHostLite)
fi

if command -v pgrep >/dev/null 2>&1; then
    finder=pgrep
elif command -v pidof >/dev/null 2>&1; then
    finder=pidof
else
    echo "Warning: cannot check for running AfterNight processes; pgrep/pidof not found." >&2
    exit 0
fi

running=()
for process_name in "${process_names[@]}"; do
    if [ "$finder" = "pgrep" ]; then
        if [ ${#process_name} -le 15 ]; then
            pids="$(pgrep -x "$process_name" 2>/dev/null || true)"
        else
            pids="$(pgrep -f "(^|/)${process_name}([[:space:]]|$)" 2>/dev/null || true)"
        fi
    else
        pids="$(pidof "$process_name" 2>/dev/null || true)"
    fi
    confirmed=()
    for pid in $pids; do
        # Long names need pgrep -f, which also matches compiler include/output paths.
        # Verify the executable before treating an argument match as a live runtime.
        if executable="$(readlink "/proc/$pid/exe" 2>/dev/null)"; then
            executable="${executable% (deleted)}"
            if [ "${executable##*/}" != "$process_name" ]; then
                continue
            fi
        elif [ ! -d "/proc/$pid" ]; then
            continue # It exited between discovery and inspection.
        fi
        # A live process whose executable cannot be inspected still blocks deployment.
        confirmed+=("$pid")
    done
    if [ ${#confirmed[@]} -gt 0 ]; then
        running+=("$process_name: ${confirmed[*]}")
    fi
done

if [ ${#running[@]} -gt 0 ]; then
    echo "Error: refusing to deploy while AfterNight runtime processes are running:" >&2
    for entry in "${running[@]}"; do
        echo "  $entry" >&2
    done
    echo "Close those processes before running this deploy script so shared libraries are not replaced under a live process." >&2
    exit 1
fi

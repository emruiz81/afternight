#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

ensure_utf8_ctype() {
    local charmap normalized candidate candidate_normalized utf8_locale=""
    charmap="$(locale charmap 2>/dev/null || true)"
    normalized="${charmap//[-_]/}"
    if [[ "${normalized^^}" == "UTF8" ]]; then
        return
    fi

    while IFS= read -r candidate; do
        candidate_normalized="${candidate//[-_.]/}"
        if [[ "${candidate_normalized^^}" == *"UTF8"* ]]; then
            utf8_locale="$candidate"
            break
        fi
    done < <(locale -a 2>/dev/null || true)

    if [[ -z "$utf8_locale" ]]; then
        echo "No UTF-8 locale is installed; AfterNight tests require a UTF-8 character environment." >&2
        exit 1
    fi

    unset LC_ALL
    export LC_CTYPE="$utf8_locale"
}

ensure_utf8_ctype

config="${1:-release}"
case "$config" in
    release|debug) ;;
    *) echo "Usage: $0 [release|debug]" >&2; exit 2 ;;
esac
BIN_DIR="$(realpath "../../../bin/clang/$config")"
test_suffix=""
[[ "$config" == "debug" ]] && test_suffix="_d"
BIN_LIB_DIR="$BIN_DIR/lib"
test_storage_base="$(realpath -m "${TMPDIR:-/tmp}")"
# The full extension host is built with the product, so a case that starts it fails rather than skips when it is
# not deployed beside the suites.
export AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST=1
test_storage_root=""

cleanup_test_storage() {
    if [[ -z "$test_storage_root" ]]; then
        return
    fi
    case "$test_storage_root" in
        "$test_storage_base"/AfterNight-*) rm -rf -- "$test_storage_root" ;;
        *) echo "Refusing to remove unexpected test storage path: $test_storage_root" >&2 ;;
    esac
}
trap cleanup_test_storage EXIT

# Stale AfterNight test scratch from runs that crashed, were interrupted, or were
# killed is reclaimed before this run starts.  Only entries that have been
# untouched for at least this many hours are removed, so a second test run in
# progress right now is never deleted out from under itself.
stale_scratch_hours=6

sweep_stale_test_scratch() {
    local cutoff="-${stale_scratch_hours} hours"
    local globs=(
        'AfterNight-Tests-*'
        'AfterNight-Test_*'
        'ANtmp-*'
        'AfterNight_drizztmp'
        'AfterNight_calibrated_stack_spill_*'
        'AfterNightStackInput-*.andump'
        'afternight_test_executor'
        'afternight_test_partial_success'
        'afternight_calibration_tests'
        'afternight-mock-tests'
        'afternight-process-framework-tests*'
        'afternight-extension-host'
        'afternight-extension-manager-page-tests'
        'an_extension_runtime_test_*'
        'an_shared_buffer_test_*'
        'afternight_gaia_catalog_*'
        'afternight_codec_test_*'
        'afternight_test_reconfigured.log'
        'afternight_extension_host_manager_tests.log'
    )
    # Per-suite QTemporaryDir roots (Test_ImageIntegration-a1b2c3, ...).  Only the
    # known suite names are swept; a bare Test_* glob would be unsafe in a temp
    # directory shared with every other program on the machine.
    local name
    for name in "${expected_tests[@]}"; do
        globs+=("$name-*")
    done

    local predicate=() glob
    for glob in "${globs[@]}"; do
        if (( ${#predicate[@]} > 0 )); then
            predicate+=(-o)
        fi
        predicate+=(-name "$glob")
    done

    local entry size removed=0 bytes=0
    while IFS= read -r -d '' entry; do
        size="$(du -sb -- "$entry" 2>/dev/null | cut -f1 || true)"
        [[ "$size" =~ ^[0-9]+$ ]] || size=0
        rm -rf -- "$entry" 2>/dev/null || true
        if [[ -e "$entry" ]]; then
            continue
        fi
        removed=$((removed + 1))
        bytes=$((bytes + size))
    done < <(find "$test_storage_base" -mindepth 1 -maxdepth 1 \
        \( "${predicate[@]}" \) \
        -not -newermt "$cutoff" -not -newerct "$cutoff" \
        -print0 2>/dev/null)

    if (( removed > 0 )); then
        printf 'Reclaimed %d stale AfterNight test scratch entries (%d MiB) from %s\n' \
            "$removed" "$((bytes / 1048576))" "$test_storage_base"
    else
        printf 'No AfterNight test scratch older than %dh to reclaim in %s\n' \
            "$stale_scratch_hours" "$test_storage_base"
    fi
}

cd "$BIN_DIR"

expected_tests=(Test_AfterNightApp Test_AfterNightCLI Test_ConsumerFaults Test_ConsumerRuntime Test_ControlsAstrometry
    Test_ControlsDialogs Test_ControlsDocking Test_ControlsExtensionSeam Test_ControlsFiles Test_ControlsHistory
    Test_ControlsProcessFramework Test_ControlsSeams Test_ControlsSequences Test_ControlsServices Test_ControlsSession
    Test_ControlsStatistics Test_ControlsViewer Test_ExtensionHostNative Test_ExtensionRuntimeBackend
    Test_FixtureNeutral Test_FixtureEngine Test_ImageIntegration Test_ImageProcessControl Test_ImageRTPreviewControl
    Test_ImageViewControl Test_MainDashboard Test_MaskDrawing Test_Persistence Test_Platform Test_ProcessFramework
    Test_ProcessesChannels Test_ProcessesColorTools Test_ProcessesCombinations Test_ProcessesExternalTools
    Test_ProcessesGeometry Test_ProcessesMasks Test_ProcessesPhotometric Test_ProcessesPixelMath
    Test_ProcessesPointwise Test_ProcessesShared Test_ProcessesSpatial Test_ProcessesStarProfilers
    Test_ProcessesStretching Test_ProcessesWorkflowTools Test_ShellCore Test_SystemInformationPanel Test_ThemeSystem
    Test_Widgets)
for expected in "${expected_tests[@]}"; do
    if [[ ! -x "$expected$test_suffix" ]]; then
        echo "Missing expected test executable: $expected$test_suffix" >&2
        exit 1
    fi
done

sweep_stale_test_scratch

# Ensure local runtime libs are discoverable (main bin + deployed lib folder).
export LD_LIBRARY_PATH="$BIN_DIR:$BIN_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

found_tests=0
# Every suite runs even after one fails, so one run reports every failing suite.
failed_tests=()
for f in Test_*; do
    [[ -f "$f" && -x "$f" ]] || continue
    found_tests=1
    # A sweep may name executables to leave out, separated by spaces, in AFTERNIGHT_SKIP_TESTS.
    if [[ " ${AFTERNIGHT_SKIP_TESTS:-} " == *" $f "* ]]; then
        echo "Skipping $f"
        continue
    fi
    test_storage_root="$(mktemp -d "$test_storage_base/AfterNight-${f}.XXXXXX")"
    export AFTERNIGHT_TEST_STORAGE_ROOT="$test_storage_root"
    echo "Running $f"
    echo "----------------------------------------"

    # A sweep that names a results directory gets each run's gtest report there, named after the executable.
    results_flag=()
    if [[ -n "${AFTERNIGHT_TEST_RESULTS_DIR:-}" ]]; then
        results_flag=("--gtest_output=json:$AFTERNIGHT_TEST_RESULTS_DIR/$f.json")
    fi
    test_command=("./$f" "${results_flag[@]}")
    if "${test_command[@]}"; then
        status=0
    else
        status=$?
    fi

    unset AFTERNIGHT_TEST_STORAGE_ROOT
    if ! rm -rf -- "$test_storage_root" || [[ -e "$test_storage_root" ]]; then
        echo "Failed to remove test storage root: $test_storage_root" >&2
        status=1
    fi
    test_storage_root=""

    echo "----------------------------------------"

    if (( status != 0 )); then
        echo "Test $f failed"
        failed_tests+=("$f")
    fi
done

if (( found_tests == 0 )); then
    echo "No executable Test_* binaries were found in $BIN_DIR." >&2
    exit 1
fi

if (( ${#failed_tests[@]} > 0 )); then
    echo "One or more tests failed: ${failed_tests[*]}"
    exit 1
fi

echo "All tests passed."

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The engine-neutral benchmark controller: an interleaved measurement of two builds of the engine.

A comparison of a candidate build with a baseline build - a change against the commit before it, or
a repaired engine against a frozen runtime - is published only from runs whose order was controlled.
This controller launches the two runners in a seeded randomized order, supplies identical
scenario/fixture options to both, and retains the raw per-trial records. Recording a random-interleaving
flag without actually controlling execution order would be an invalid run.

That is the reason this file exists. `RunOptions::randomInterleaving` is a claim about how a
measurement was taken, and a standalone runner cannot honestly make it: a runner owns its own
repetitions and has no idea what ran before or after it. So the runner always writes `false`, and this
controller is the only thing permitted to write `true`.

How it works: instead of asking each runner for N repetitions in one go - which would run every
baseline trial, then every candidate trial, and let any machine drift during the run land entirely on
one side - the controller invokes each runner N times with one repetition each, in a seeded order that
alternates between the two, then concatenates the per-round samples into one manifest per side. The
two distributions are therefore built from trials that were spread across the same stretch of
wall-clock time on the same machine.

Engine-neutral in the literal sense: this file knows the shape of a run manifest and nothing about the
engine. It is Python rather than another C++ binary because it links nothing, ships nothing, and would
otherwise need its own packaging exclusion.

Two further duties bound what one invocation may cost. `--baseline-scenarios` and `--candidate-scenarios`
give each runner its own catalogue, so each side's timed batch can be sized to its own per-call time
instead of a common batch sized for the faster build. `--deadline-epoch` is a wall-clock deadline:
a pair that cannot finish before it is not started, a round still running at it is stopped with its
whole process tree, and only complete pairs are kept - in `*-incomplete.json`, never under the names
a finished run uses - with exit code 4. Progress is reported at least once a minute.
"""

import argparse
import hashlib
import json
import math
import os
import random
import subprocess
import sys
import time

import bounded_process


BASELINE = "Baseline"
CANDIDATE = "Candidate"

#: Exit code of a run the deadline cut short. Its complete pairs are retained; it is not a result.
INCOMPLETE = 4

#: Options that describe *what* is measured rather than *which* build measures it. Every one is
#: forwarded to both runners unchanged, because a difference in any of them makes the two result
#: sets describe different work.
SHARED_VALUE_OPTIONS = (
    ("scenarios", "--scenarios"),
    ("corpus_root", "--corpus-root"),
    ("corpus_manifest", "--corpus-manifest"),
    ("host_id", "--host-id"),
    ("commit", "--commit"),
    ("dependency_lock_digest", "--dependency-lock-digest"),
    ("timestamp", "--timestamp"),
    ("warmup", "--warmup"),
    ("threads", "--threads"),
    ("power_profile", "--power-profile"),
    ("notes", "--notes"),
)


def build_schedule(rounds, seed):
    """Return the launch order for `rounds` trials of each side.

    Built as one randomly-ordered pair per round rather than as a shuffle of the whole sequence.
    A shuffle of equal counts is balanced on average but says nothing about local ordering: it can
    put a long block of one side at either end, and a block is precisely the arrangement that lets
    thermal or cache drift during the run land on one build and not the other.

    Emitting a pair per round gives three properties by construction, for every seed: exactly
    `rounds` trials of each side, both sides present in every consecutive pair, and a maximum run
    length of two - which occurs only where one pair ends with the side the next pair begins with.
    There is no tail case, because the counts cannot go out of balance.
    """
    rng = random.Random(seed)
    schedule = []
    for _ in range(rounds):
        pair = [BASELINE, CANDIDATE]
        rng.shuffle(pair)
        schedule.extend(pair)
    return schedule


def runner_command(executable, out_path, args, python, side):
    """The command line for one runner invocation.

    `python` is set only by the tests, which substitute a script for a compiled runner. A real run
    executes the runner binary directly.
    """
    # Always launch through an absolute, OS-native path. A relative path spelled with forward
    # slashes - which is what a shell on Windows hands over - reaches CreateProcess as something it
    # parses as switches rather than as a path, and the launch fails with "file not found" even
    # though the file is plainly there.
    executable = os.path.normpath(os.path.abspath(executable))
    command = [python, executable] if python and executable.endswith(".py") else [executable]
    command += ["--out", out_path, "--repetitions", "1"]
    for attribute, flag in SHARED_VALUE_OPTIONS:
        value = getattr(args, attribute)
        if value not in (None, ""):
            command += [flag, str(value)]
    # A side's own catalogue, when the two sides' timed batches were sized independently.
    own_catalogue = args.baseline_scenarios if side == BASELINE else args.candidate_scenarios
    if own_catalogue:
        command += ["--scenarios", own_catalogue]
    command += ["--dirty"] if args.dirty else ["--clean"]
    engine_commit = args.baseline_engine_commit if side == BASELINE else args.candidate_engine_commit
    if engine_commit:
        command += ["--engine-commit", engine_commit]
    engine_dirty = args.baseline_engine_dirty if side == BASELINE else args.candidate_engine_dirty
    if engine_dirty is not None:
        command += ["--engine-dirty"] if engine_dirty else ["--engine-clean"]
    engine_lto = args.baseline_engine_lto if side == BASELINE else args.candidate_engine_lto
    if engine_lto is not None:
        command += ["--engine-lto"] if engine_lto else ["--engine-no-lto"]
    if args.otherwise_idle:
        command.append("--otherwise-idle")
    if args.dry_run:
        command.append("--dry-run")
    return command


# Correctness classes whose contract is a byte-identical result. For these a digest that changes
# between two trials of one build is a defect, and merging the trials would average two behaviours.
# Every other class states a tolerance instead, which is the same as saying the last bits are not
# promised - so a changed digest there is a property to record, not a reason to throw the sweep away.
EXACT_CLASSES = ("BitExact", "ExactAfterMapping", "MetadataExact")
TOLERANCE_CLASSES = ("MaxUlp", "AbsoluteRelative", "LsbDistribution", "RmsPsnr",
                     "GeometricResidual", "EstimatorTolerance")


def _frozen(value):
    if isinstance(value, dict):
        return tuple((key, _frozen(item)) for key, item in sorted(value.items()))
    if isinstance(value, list):
        return tuple(_frozen(item) for item in value)
    return value


def _stable_run_signature(manifest):
    stable = {key: value for key, value in manifest.items()
              if key not in ("timestamp", "environment", "results")}
    environment = manifest.get("environment", {})
    stable["environment"] = {key: value for key, value in environment.items()
                             if key not in ("cpuFrequencyMhz", "backgroundLoad")}
    return _frozen(stable)


def _metric_note(record):
    """What is wrong with a tolerance-class record's correctness metric, or None when nothing is.

    This is reported and not enforced. Correctness is decided per scenario, on the merged record,
    which refuses one whose metric is missing or outside its tolerance - and since merging keeps the
    largest metric of every round, a round that exceeded its tolerance reaches that decision rather
    than being averaged away. Failing here would discard every other scenario's measured trials in
    order to report a defect belonging to one of them, and a scenario with no oracle would stop a
    full catalogue from publishing on any host.

    What only this function can see is which round it happened in, so that is what it says.
    """
    if record.get("correctnessClass") not in TOLERANCE_CLASSES:
        return None
    metric = record.get("correctnessMetric")
    tolerance = record.get("tolerance")
    if isinstance(metric, bool) or not isinstance(metric, (int, float)) or not math.isfinite(metric):
        return "reported no correctness metric"
    if (isinstance(tolerance, bool) or not isinstance(tolerance, (int, float)) or
            not math.isfinite(tolerance) or tolerance <= 0):
        return "declares no usable tolerance"
    if not 0 <= metric <= tolerance:
        return "reported a correctness metric of %r against a tolerance of %r" % (metric, tolerance)
    return None


def _merge_memory(target, incoming):
    target_memory = target.get("memory", {})
    incoming_memory = incoming.get("memory", {})
    if not isinstance(target_memory, dict) or not isinstance(incoming_memory, dict):
        return False
    for field in ("allocationCount", "allocatedBytes", "scratchHighWaterBytes",
                  "peakResidentBytes", "retainedCacheBytes"):
        before = target_memory.get(field)
        after = incoming_memory.get(field)
        if before is None and after is None:
            continue
        if (isinstance(before, bool) or not isinstance(before, (int, float)) or before < 0 or
                isinstance(after, bool) or not isinstance(after, (int, float)) or after < 0):
            return False
        target_memory[field] = max(before, after)
    return True


def _merge_stretch_evidence(target, incoming):
    """Retain every stretch oracle failure; omissions and changing execution counts refuse the merge."""
    left, right = target.get("counters", {}), incoming.get("counters", {})
    families = ("histogram", "asinh", "ghs", "mtf", "statistical", "stretch")
    keys = {key for key in left if key.startswith(families)}
    if keys != {key for key in right if key.startswith(families)}:
        return False
    stable = {"asinhOracleVersion", "asinhOracleSamples", "ghsOracleVersion", "ghsOracleSamples", "histogramOracleVersion",
              "histogramOracleSamples", "mtfOracleVersion", "mtfOracleSamples", "statisticalOracleVersion",
              "statisticalOracleSamples", "stretchTimedBatch", "stretchWarmupCalls"}
    for key in keys:
        a, b = left[key], right[key]
        if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0
               for v in (a, b)):
            return False
        if key in stable and a != b:
            return False
        left[key] = a + b if key == "stretchTimedCalls" else max(a, b)
    return True


def merge_round(target, manifest, round_index, stream, divergences=None, side="run"):
    """Fold one round's manifest into the accumulating manifest for that side.

    Returns False on the two disagreements that must not be averaged away: a scenario that promises
    a byte-identical result and did not produce one twice, and one that skipped in some rounds and
    ran in others. Both mean the trials describe different behaviour, and a merged distribution
    would silently describe neither.

    A tolerance-bearing scenario whose digest changed is appended to `divergences` and still merged.
    `side` names the side in what is reported.
    """
    if divergences is None:
        divergences = []
    if target is None:
        return manifest, True

    if _stable_run_signature(target) != _stable_run_signature(manifest):
        stream.write("round %d: %s changed its build, host, or run protocol\n" % (round_index, side))
        return target, False

    target_results = target.get("results", [])
    incoming_results = manifest.get("results", [])
    target_by_id = {record.get("scenarioId"): record for record in target_results}
    by_id = {record.get("scenarioId"): record for record in incoming_results}
    if (len(target_by_id) != len(target_results) or len(by_id) != len(incoming_results) or
            set(target_by_id) != set(by_id)):
        stream.write("round %d: %s changed its scenario set\n" % (round_index, side))
        return target, False

    _merge_host_activity(target, manifest)
    ok = True
    stable_record_fields = ("scenarioId", "operationVersion", "skipped", "skipReason",
                            "requestedBackend", "actualBackend", "threadCount", "fixtureDigest",
                            "scenarioDigest", "correctnessClass", "tolerance",
                            "acceptedDifferenceReason", "bytesProcessed", "elementsProcessed")
    for record in target_results:
        scenario = record["scenarioId"]
        incoming = by_id.get(scenario)
        changed_fields = [field for field in stable_record_fields
                          if _frozen(record.get(field)) != _frozen(incoming.get(field))]
        if changed_fields:
            stream.write("round %d: %s changed %s for %s\n" %
                         (round_index, side, ", ".join(changed_fields), scenario))
            ok = False
            continue
        if record["skipped"]:
            continue
        for note in (_metric_note(record), _metric_note(incoming)):
            if note is not None:
                stream.write("round %d: %s %s for %s; this scenario's correctness is decided on the "
                             "merged record, so the round still merges\n" % (round_index, side, note, scenario))
        if incoming.get("outputDigest") != record.get("outputDigest"):
            correctness = record.get("correctnessClass", "BitExact")
            if correctness in EXACT_CLASSES:
                stream.write(
                    "round %d: %s produced a different output digest for %s (%s then %s); "
                    "the engine is not deterministic and these trials must not be merged\n"
                    % (round_index, side, scenario, record.get("outputDigest"), incoming.get("outputDigest")))
                ok = False
                continue
            stream.write(
                "round %d: %s produced a different output digest for %s (%s then %s); its "
                "correctness class is %s, which promises a tolerance and not the last bits, so "
                "the trials merge and the divergence is recorded\n"
                % (round_index, side, scenario, record.get("outputDigest"), incoming.get("outputDigest"),
                   correctness))
            _record_divergence(divergences, side, scenario, correctness,
                               [record.get("outputDigest"), incoming.get("outputDigest")])
        metrics = [value for value in (record.get("correctnessMetric"), incoming.get("correctnessMetric"))
                   if not isinstance(value, bool) and isinstance(value, (int, float))]
        record["correctnessMetric"] = max(metrics) if len(metrics) == 2 else None
        if not _merge_stretch_evidence(record, incoming):
            stream.write("round %d: %s changed or omitted independent stretch evidence for %s\n" %
                         (round_index, side, scenario))
            ok = False
            continue
        if not _merge_memory(record, incoming):
            stream.write("round %d: %s changed memory-counter availability for %s\n" %
                         (round_index, side, scenario))
            ok = False
            continue
        record["wallMs"].extend(incoming.get("wallMs", []))
        record["cpuMs"].extend(incoming.get("cpuMs", []))
    return target, ok


def _record_divergence(divergences, side, scenario, correctness, digests):
    """Keep one entry per side and scenario, holding every distinct digest it produced."""
    for entry in divergences:
        if entry["side"] == side and entry["scenarioId"] == scenario:
            for digest in digests:
                if digest not in entry["digests"]:
                    entry["digests"].append(digest)
            return
    divergences.append({"side": side, "scenarioId": scenario,
                        "correctnessClass": correctness, "digests": list(dict.fromkeys(digests))})


def _merge_host_activity(target, incoming):
    """Merge the sampled host activity instead of retaining only the first child process.

    CPU-frequency means and background load are weighted by the activity sampler's observation
    count. Its interval is fixed by the runner, so this covers the whole interleaved run without
    pretending that each differently-sized child process represents the same duration.
    """
    target_environment = target.get("environment", {})
    incoming_environment = incoming.get("environment", {})
    target_frequency = target_environment.get("cpuFrequencyMhz", {})
    incoming_frequency = incoming_environment.get("cpuFrequencyMhz", {})

    def sample_count(frequency):
        value = frequency.get("samples") if isinstance(frequency, dict) else None
        return value if not isinstance(value, bool) and isinstance(value, int) and value > 0 else 0

    target_samples = sample_count(target_frequency)
    incoming_samples = sample_count(incoming_frequency)
    if incoming_samples == 0:
        return
    if target_samples == 0:
        target_environment["cpuFrequencyMhz"] = dict(incoming_frequency)
        target_environment["backgroundLoad"] = incoming_environment.get("backgroundLoad", 0.0)
        return

    frequency_values = [target_frequency.get(field) for field in ("minimum", "mean", "maximum")]
    incoming_values = [incoming_frequency.get(field) for field in ("minimum", "mean", "maximum")]
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value)
           for value in frequency_values + incoming_values):
        return
    total_samples = target_samples + incoming_samples
    target_frequency["minimum"] = min(frequency_values[0], incoming_values[0])
    target_frequency["mean"] = ((frequency_values[1] * target_samples) +
                                (incoming_values[1] * incoming_samples)) / total_samples
    target_frequency["maximum"] = max(frequency_values[2], incoming_values[2])
    target_frequency["samples"] = total_samples

    target_load = target_environment.get("backgroundLoad")
    incoming_load = incoming_environment.get("backgroundLoad")
    loads = (target_load, incoming_load)
    if all(not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value)
           for value in loads):
        target_environment["backgroundLoad"] = ((target_load * target_samples) +
                                                 (incoming_load * incoming_samples)) / total_samples


def parse_args(argv):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--out-dir", required=True, dest="out_dir")
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--threads", type=int, default=None)
    parser.add_argument("--scenarios", default="")
    parser.add_argument("--baseline-scenarios", dest="baseline_scenarios", default="")
    parser.add_argument("--candidate-scenarios", dest="candidate_scenarios", default="")
    parser.add_argument("--deadline-epoch", dest="deadline_epoch", type=float, default=None)
    parser.add_argument("--pair-estimate-seconds", dest="pair_estimate_seconds", type=float, default=None)
    parser.add_argument("--progress-seconds", dest="progress_seconds", type=float, default=30.0)
    parser.add_argument("--corpus-root", dest="corpus_root", default="")
    parser.add_argument("--corpus-manifest", dest="corpus_manifest", default="")
    parser.add_argument("--host-id", dest="host_id", default="")
    parser.add_argument("--commit", default="")
    parser.add_argument("--baseline-engine-commit", dest="baseline_engine_commit", default="")
    parser.add_argument("--candidate-engine-commit", dest="candidate_engine_commit", default="")
    parser.add_argument("--dependency-lock-digest", dest="dependency_lock_digest", default="")
    parser.add_argument("--timestamp", default="")
    parser.add_argument("--power-profile", dest="power_profile", default="")
    parser.add_argument("--notes", default="")
    parser.add_argument("--otherwise-idle", dest="otherwise_idle", action="store_true")
    parser.add_argument("--dirty", action="store_true")
    parser.add_argument("--clean", dest="dirty", action="store_false")
    parser.add_argument("--baseline-engine-dirty", dest="baseline_engine_dirty", action="store_true")
    parser.add_argument("--baseline-engine-clean", dest="baseline_engine_dirty", action="store_false")
    parser.add_argument("--candidate-engine-dirty", dest="candidate_engine_dirty", action="store_true")
    parser.add_argument("--candidate-engine-clean", dest="candidate_engine_dirty", action="store_false")
    parser.add_argument("--baseline-engine-lto", dest="baseline_engine_lto", action="store_true")
    parser.add_argument("--baseline-engine-no-lto", dest="baseline_engine_lto", action="store_false")
    parser.add_argument("--candidate-engine-lto", dest="candidate_engine_lto", action="store_true")
    parser.add_argument("--candidate-engine-no-lto", dest="candidate_engine_lto", action="store_false")
    parser.add_argument("--dry-run", dest="dry_run", action="store_true")
    parser.add_argument("--python", default="")
    parser.set_defaults(baseline_engine_dirty=None, candidate_engine_dirty=None,
                        baseline_engine_lto=None, candidate_engine_lto=None)
    return parser.parse_args(argv)


def _catalogue_identity(path):
    if not path:
        return None
    with open(path, "rb") as handle:
        return {"path": os.path.abspath(path), "sha256": hashlib.sha256(handle.read()).hexdigest()}


def _write_json_atomically(path, value, patience_seconds=5.0):
    with open(path + ".tmp", "w") as handle:
        json.dump(value, handle, indent=1)
    # Windows refuses to replace a file another process has open, and a caller may read the
    # progress file while pairs run. A reader holds it for milliseconds, so the replacement waits
    # for it rather than ending the run; a file still held after the patience is a real failure.
    deadline = time.monotonic() + patience_seconds
    while True:
        try:
            os.replace(path + ".tmp", path)
            return
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.02)


def main(argv=None, stream=None, env=None):
    stream = stream or sys.stderr
    try:
        args = parse_args(sys.argv[1:] if argv is None else argv)
    except SystemExit:
        return 3

    if args.rounds < 1:
        stream.write("--rounds must be at least 1\n")
        return 3
    if args.threads is not None and args.threads < 1:
        stream.write("--threads must be at least 1 when it is stated\n")
        return 3
    if not 0 < args.progress_seconds <= bounded_process.MAXIMUM_TICK_SECONDS:
        stream.write("--progress-seconds must report at least once a minute\n")
        return 3
    independent = bool(args.baseline_scenarios or args.candidate_scenarios)
    if independent and (args.scenarios or not (args.baseline_scenarios and args.candidate_scenarios)):
        stream.write("give one shared --scenarios, or both --baseline-scenarios and --candidate-scenarios\n")
        return 3
    for label, path in ((BASELINE, args.baseline), (CANDIDATE, args.candidate)):
        if not os.path.isfile(path):
            stream.write("%s runner not found: %s\n" % (label, path))
            return 3
    for label, path in ((BASELINE, args.baseline_scenarios), (CANDIDATE, args.candidate_scenarios)):
        if path and not os.path.isfile(path):
            stream.write("%s catalogue not found: %s\n" % (label, path))
            return 3

    os.makedirs(args.out_dir, exist_ok=True)
    scratch = os.path.join(args.out_dir, "rounds")
    os.makedirs(scratch, exist_ok=True)

    budget = (bounded_process.Budget.until_epoch(args.deadline_epoch)
              if args.deadline_epoch is not None else None)
    deadline = budget.deadline_monotonic() if budget is not None else math.inf
    executables = {BASELINE: args.baseline, CANDIDATE: args.candidate}
    catalogues = ({BASELINE: _catalogue_identity(args.baseline_scenarios),
                   CANDIDATE: _catalogue_identity(args.candidate_scenarios)} if independent else
                  {BASELINE: _catalogue_identity(args.scenarios), CANDIDATE: _catalogue_identity(args.scenarios)})
    merged = {BASELINE: None, CANDIDATE: None}
    divergences = []
    schedule = build_schedule(args.rounds, args.seed)
    started = time.monotonic()
    progress = {"rounds": args.rounds, "completedRounds": 0, "completedPairs": 0, "running": None}
    pair_seconds = []
    pending = []
    stop_reason = None

    def report():
        remaining = "unbounded" if budget is None else "%.0f s" % budget.remaining()
        running = progress["running"]
        stream.write("controller: elapsed %.0f s, remaining %s, pairs %d/%d complete%s\n" %
                     (time.monotonic() - started, remaining, progress["completedPairs"], args.rounds,
                      "" if running is None else ", round %d (%s) running" % running))
        stream.flush()

    def publish_progress():
        progress["elapsedSeconds"] = time.monotonic() - started
        progress["remainingSeconds"] = None if budget is None else budget.remaining()
        _write_json_atomically(os.path.join(args.out_dir, "progress.json"), progress)

    for index, side in enumerate(schedule):
        if index % 2 == 0 and budget is not None:
            # A pair is the unit of evidence, so one that cannot finish is not begun.
            estimate = max(pair_seconds) if pair_seconds else args.pair_estimate_seconds
            if budget.expired():
                stop_reason = "deadline reached before pair %d" % (index // 2)
                break
            if estimate is not None and estimate > budget.remaining():
                stop_reason = ("pair %d, estimated at %.1f s, would not finish in the remaining %.1f s" %
                               (index // 2, estimate, budget.remaining()))
                break
        if index % 2 == 0:
            pair_started = time.monotonic()
        round_path = os.path.join(scratch, "round-%03d-%s.json" % (index, side.lower()))
        command = runner_command(executables[side], round_path, args, args.python, side)
        child_env = dict(env or os.environ)
        child_env["STUB_SIDE"] = side
        child_env["STUB_COUNTER"] = os.path.join(scratch, "counter-%s.txt" % side.lower())
        progress["running"] = (index, side)
        publish_progress()
        completed = bounded_process.run_bounded(command, deadline, env=child_env, stdout=subprocess.PIPE,
                                                stderr=subprocess.PIPE, tick_seconds=args.progress_seconds,
                                                on_tick=report)
        progress["running"] = None
        if completed.timed_out:
            stop_reason = "deadline reached during round %d (%s); that runner was stopped" % (index, side)
            break
        if completed.returncode != 0:
            stream.write("%s runner failed in round %d (exit %d)\n%s\n"
                         % (side, index, completed.returncode, (completed.stderr or "").strip()))
            return 1
        try:
            with open(round_path) as handle:
                manifest = json.load(handle)
        except (OSError, ValueError) as error:
            stream.write("round %d: could not read %s: %s\n" % (index, round_path, error))
            return 1
        progress["completedRounds"] = index + 1
        pending.append((index, side, manifest))
        if index % 2 == 1:
            # Each schedule pair contains exactly one launch of each side, and a pair is merged only
            # once both have finished. Appending each side in encounter order therefore preserves pair
            # identity: timing sample i in the two merged manifests came from the same pair. An exact
            # paired sign test over the two relies on that invariant when randomInterleaving is true,
            # so a half pair is never merged.
            for round_index, round_side, round_manifest in pending:
                merged[round_side], ok = merge_round(merged[round_side], round_manifest, round_index, stream,
                                                     divergences, round_side)
                if not ok:
                    return 1
            pending = []
            pair_seconds.append(time.monotonic() - pair_started)
            progress["completedPairs"] += 1
            report()
        publish_progress()

    completed_pairs = progress["completedPairs"]
    complete = stop_reason is None
    progress["complete"] = complete
    publish_progress()
    # The interleaving claim is earned, not configured. One pair is a seeded order but nothing is
    # interleaved, and a dry run measures nothing at all.
    interleaved = completed_pairs >= 2 and not args.dry_run

    if completed_pairs:
        suffix = "" if complete else "-incomplete"
        for side, name in ((BASELINE, "baseline"), (CANDIDATE, "candidate")):
            manifest = merged[side]
            manifest["randomInterleaving"] = interleaved
            manifest["repetitions"] = completed_pairs
            with open(os.path.join(args.out_dir, name + suffix + ".json"), "w") as handle:
                json.dump(manifest, handle, indent=1)

    with open(os.path.join(args.out_dir, "schedule.json"), "w") as handle:
        json.dump({"seed": args.seed, "rounds": args.rounds, "order": schedule,
                   "interleaved": interleaved, "dryRun": args.dry_run,
                   "digestDivergences": divergences, "complete": complete,
                   "completedPairs": completed_pairs, "stopReason": stop_reason,
                   "deadlineEpoch": args.deadline_epoch, "pairSeconds": pair_seconds,
                   "timedBatches": "independent" if independent else "shared",
                   "catalogues": catalogues}, handle, indent=1)

    if not complete:
        stream.write("incomplete: %s; %d of %d pairs retained\n" % (stop_reason, completed_pairs, args.rounds))
        return INCOMPLETE
    return 0


if __name__ == "__main__":
    sys.exit(main())

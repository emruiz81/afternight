# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for bench_controller.py.

The controller is the only component permitted to record `randomInterleaving: true`, so most of
what is worth testing here is the difference between actually controlling execution order and
merely claiming to have done so.

The tests never invoke a real benchmark runner. A stub runner stands in for one, which keeps them
hermetic and fast and lets a test manufacture the failure modes - a non-deterministic digest across
rounds, a runner that exits non-zero - that a real runner produces only by accident.
"""

import io
import copy
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bench_controller as bc
import bounded_process
import check_interleaving_claim as cic


class IndependentEvidenceMergeTest(unittest.TestCase):
    def test_later_histogram_failures_and_missing_evidence_are_never_hidden(self):
        record = {"scenarioId": "stretch.test", "skipped": False, "outputDigest": "same",
                  "correctnessClass": "IntentionallyChanged", "wallMs": [1], "cpuMs": [1],
                  "memory": {}, "counters": {"histogramOracleVersion": 1,
                  "histogramOracleSamples": 257, "histogramRangeViolations": 0,
                  "stretchTimedCalls": 5, "stretchTimedBatch": 5, "stretchWarmupCalls": 2}}
        run = {"results": [record]}
        later = copy.deepcopy(run)
        later["results"][0]["counters"]["histogramRangeViolations"] = 1
        merged, ok = bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())
        self.assertTrue(ok)
        self.assertEqual(merged["results"][0]["counters"]["histogramRangeViolations"], 1)
        self.assertEqual(merged["results"][0]["counters"]["stretchTimedCalls"], 10)
        del later["results"][0]["counters"]["histogramOracleSamples"]
        self.assertFalse(bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())[1])

    def test_later_asinh_failures_and_missing_evidence_are_never_hidden(self):
        record = {"scenarioId": "stretch.test", "skipped": False, "outputDigest": "same",
                  "correctnessClass": "IntentionallyChanged", "wallMs": [1], "cpuMs": [1],
                  "memory": {}, "counters": {"asinhOracleVersion": 1,
                  "asinhOracleSamples": 257, "asinhRangeViolations": 0,
                  "stretchTimedCalls": 5, "stretchTimedBatch": 5, "stretchWarmupCalls": 2}}
        run = {"results": [record]}
        later = copy.deepcopy(run)
        later["results"][0]["counters"]["asinhRangeViolations"] = 1
        merged, ok = bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())
        self.assertTrue(ok)
        self.assertEqual(merged["results"][0]["counters"]["asinhRangeViolations"], 1)
        self.assertEqual(merged["results"][0]["counters"]["stretchTimedCalls"], 10)
        del later["results"][0]["counters"]["asinhOracleSamples"]
        self.assertFalse(bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())[1])

    def test_later_ghs_failures_and_missing_evidence_are_never_hidden(self):
        record = {"scenarioId": "stretch.test", "skipped": False, "outputDigest": "same",
                  "correctnessClass": "IntentionallyChanged", "wallMs": [1], "cpuMs": [1],
                  "memory": {}, "counters": {"ghsOracleVersion": 1,
                  "ghsOracleSamples": 257, "ghsOracleMaxAbsoluteError": 1e-7,
                  "stretchTimedCalls": 5, "stretchTimedBatch": 5, "stretchWarmupCalls": 2}}
        run = {"results": [record]}
        later = copy.deepcopy(run)
        later["results"][0]["counters"]["ghsOracleMaxAbsoluteError"] = 0.25
        merged, ok = bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())
        self.assertTrue(ok)
        self.assertEqual(merged["results"][0]["counters"]["ghsOracleMaxAbsoluteError"], 0.25)
        self.assertEqual(merged["results"][0]["counters"]["ghsOracleSamples"], 257)
        del later["results"][0]["counters"]["ghsOracleSamples"]
        self.assertFalse(bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())[1])

    def test_later_mtf_failures_and_missing_evidence_are_never_hidden(self):
        record = {"scenarioId": "stretch.test", "skipped": False, "outputDigest": "same",
                  "correctnessClass": "EstimatorTolerance", "wallMs": [1], "cpuMs": [1],
                  "memory": {}, "counters": {"mtfOracleVersion": 1,
                  "mtfOracleSamples": 257, "mtfOracleMaxAbsoluteError": 1e-7,
                  "stretchTimedCalls": 5, "stretchTimedBatch": 5, "stretchWarmupCalls": 2}}
        run = {"results": [record]}
        later = copy.deepcopy(run)
        later["results"][0]["counters"]["mtfOracleMaxAbsoluteError"] = 0.25
        merged, ok = bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())
        self.assertTrue(ok)
        self.assertEqual(merged["results"][0]["counters"]["mtfOracleMaxAbsoluteError"], 0.25)
        later = copy.deepcopy(run)
        later["results"][0]["counters"]["mtfOracleSamples"] = 256
        self.assertFalse(bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())[1])
        del later["results"][0]["counters"]["mtfOracleSamples"]
        self.assertFalse(bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())[1])

    def test_later_statistical_failures_and_missing_evidence_are_never_hidden(self):
        record = {"scenarioId": "stretch.test", "skipped": False, "outputDigest": "same",
                  "correctnessClass": "EstimatorTolerance", "wallMs": [1], "cpuMs": [1],
                  "memory": {}, "counters": {"statisticalOracleVersion": 1,
                  "statisticalOracleSamples": 257, "statisticalOracleMaxAbsoluteError": 1e-7,
                  "statisticalCurveFromEngine": 0, "stretchTimedCalls": 5, "stretchTimedBatch": 5,
                  "stretchWarmupCalls": 2}}
        run = {"results": [record]}
        later = copy.deepcopy(run)
        later["results"][0]["counters"]["statisticalOracleMaxAbsoluteError"] = 0.25
        later["results"][0]["counters"]["statisticalCurveFromEngine"] = 1
        merged, ok = bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())
        self.assertTrue(ok)
        self.assertEqual(merged["results"][0]["counters"]["statisticalOracleMaxAbsoluteError"], 0.25)
        self.assertEqual(merged["results"][0]["counters"]["statisticalCurveFromEngine"], 1)
        later = copy.deepcopy(run)
        later["results"][0]["counters"]["statisticalOracleSamples"] = 256
        self.assertFalse(bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())[1])
        del later["results"][0]["counters"]["statisticalOracleSamples"]
        self.assertFalse(bc.merge_round(copy.deepcopy(run), later, 1, io.StringIO())[1])

    def test_a_missing_metric_in_one_trial_is_not_replaced_by_another_trial(self):
        record = {"scenarioId": "tolerant", "skipped": False, "outputDigest": "same",
                  "correctnessClass": "EstimatorTolerance", "tolerance": 1,
                  "correctnessMetric": None, "wallMs": [1], "cpuMs": [1], "memory": {}}
        run = {"results": [record]}
        later = copy.deepcopy(run)
        later["results"][0]["correctnessMetric"] = 0
        merged, ok = bc.merge_round(run, later, 1, io.StringIO())
        self.assertTrue(ok)
        self.assertIsNone(merged["results"][0]["correctnessMetric"])


STUB = '''
import json, os, sys
args = sys.argv[1:]


def value(name, default=None):
    return args[args.index(name) + 1] if name in args else default


out = value("--out")
reps = int(value("--repetitions", "1"))
mode = os.environ.get("STUB_MODE", "ok")
call = int(os.environ.get("STUB_CALL_FILE_COUNTER", "0"))

counter_path = os.environ["STUB_COUNTER"]
n = 0
if os.path.exists(counter_path):
    n = int(open(counter_path).read() or "0")
open(counter_path, "w").write(str(n + 1))

# Record the exact argv of every invocation so a test can assert what was forwarded and in
# what order the two sides were launched.
log = os.environ["STUB_LOG"]
earlier = 0
if os.path.exists(log):
    with open(log) as handle:
        earlier = sum(1 for line in handle if line.strip())
with open(log, "a") as handle:
    handle.write(json.dumps({"side": os.environ["STUB_SIDE"], "args": args,
                             "pid": os.getpid()}) + chr(10))

# From the given launch onwards the runner outlives any test deadline.
if earlier >= int(os.environ.get("STUB_SLOW_FROM", "1000000")):
    import time
    time.sleep(120)

if mode == "fail":
    sys.stderr.write("stub failure" + chr(10))
    sys.exit(1)

digest = "digest-a"
if mode in ("flaky", "flaky-estimator", "bad-estimator") and n >= 1:
    digest = "digest-b"

correctness = ("EstimatorTolerance" if mode in ("flaky-estimator", "bad-estimator", "no-estimator")
               else "BitExact")
tolerance = 1e-6 if correctness == "EstimatorTolerance" else 0.0
metric = (2e-6 if mode == "bad-estimator" and n >= 1 else 2e-7) if tolerance else None
if mode == "no-estimator":
    metric = None

skipped = mode == "skipped"

build = {"commit": value("--commit", ""), "dirty": "--dirty" in args, "compiler": "stub",
         "standardLibrary": "stub-stl", "linker": "stub-link", "configuration": "Release",
         "flags": "arch=stub cpp=202002 fastmath=off ndebug=on", "lto": False, "pgo": False,
         "fastMath": False, "dependencyVersions": {"EngineBenchCommon": "stub-3"}}
engine_build = dict(build)
engine_build["commit"] = value("--engine-commit", build["commit"])
engine_build["dirty"] = "--engine-dirty" in args or ("--engine-clean" not in args and build["dirty"])
engine_build["lto"] = "--engine-lto" in args or ("--engine-no-lto" not in args and build["lto"])
engine_build["dependencyVersions"] = {"engine": "stub"}
if mode == "build-drift" and n >= 1:
    engine_build["flags"] = "changed"

manifest = {
    "schemaVersion": 3,
    "runnerVersion": "stub-2",
    "timestamp": value("--timestamp", "2026-01-01T00:00:00Z"),
    "runnerBuild": build,
    "engineBuild": engine_build,
    "dependencyLockDigest": value("--dependency-lock-digest", "a" * 64),
    "environment": {"hostId": value("--host-id", "stub-host"), "cpuModel": "stub",
                    "microcodeVersion": "stub-microcode", "physicalCores": 1,
                    "logicalCores": 1, "numaNodes": 1, "memoryBytes": 1, "osName": "stub",
                    "gpuModel": "not-used", "driverVersion": "not-used", "isaCapabilities": [],
                    "powerProfile": value("--power-profile", ""), "acPower": True,
                    "otherwiseIdle": "--otherwise-idle" in args, "notes": value("--notes", ""),
                    "cpuFrequencyMhz": {"minimum": 3000.0 + n * 10.0,
                                        "mean": 3100.0 + n * 10.0,
                                        "maximum": 3200.0 + n * 10.0,
                                        "samples": n + 1},
                    "backgroundLoad": 0.01 * (n + 1)},
    "warmupIterations": int(value("--warmup", "0")),
    "repetitions": reps,
    "randomInterleaving": False,
    "corpusRoot": value("--corpus-root", ""),
    "results": [
        {
            "scenarioId": sid, "operationVersion": 1, "skipped": skipped,
            "skipReason": "stub skip" if skipped else "",
            "requestedBackend": "Unspecified", "actualBackend": "Unspecified",
            "threadCount": n + 1 if mode == "thread-drift" else 1,
            "wallMs": [] if skipped else [1.0 + n for _ in range(reps)],
            "cpuMs": [] if skipped else [0.5 + n for _ in range(reps)],
            "memory": {"allocationCount": 0, "allocatedBytes": 0, "scratchHighWaterBytes": 0,
                       "peakResidentBytes": 0, "retainedCacheBytes": 0},
            "outputDigest": "" if skipped else digest,
            "fixtureDigest": "fixture-b" if mode == "fixture-drift" and n >= 1 else "fixture",
            "scenarioDigest": "scenario",
            "correctnessClass": correctness, "tolerance": tolerance,
            "correctnessMetric": metric, "bytesProcessed": 16, "elementsProcessed": 4, "counters": {},
        }
        for sid in ["alpha", "beta"]
    ],
}
with open(out, "w") as handle:
    json.dump(manifest, handle)
sys.exit(0)
'''


class TempCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="benchctl")
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.stub = os.path.join(self.dir, "stub_runner.py")
        with open(self.stub, "w") as handle:
            handle.write(STUB)
        self.log = os.path.join(self.dir, "log.jsonl")
        self.out = os.path.join(self.dir, "out")

    def invoke(self, extra=None, mode="ok", stream=None, env_extra=None):
        env = dict(os.environ)
        env["STUB_MODE"] = mode
        env["STUB_LOG"] = self.log
        env.update(env_extra or {})
        argv = [
            "--baseline", self.stub, "--candidate", self.stub,
            "--out-dir", self.out, "--rounds", "4", "--seed", "7",
            "--host-id", "h1", "--commit", "abc123", "--clean",
            "--timestamp", "2026-08-24T10:00:00Z",
            "--python", sys.executable,
        ]
        argv += extra or []
        return bc.main(argv, stream=stream, env=env)

    def invocations(self):
        if not os.path.exists(self.log):
            return []
        with open(self.log) as handle:
            return [json.loads(line) for line in handle if line.strip()]

    def manifest(self, name):
        with open(os.path.join(self.out, name)) as handle:
            return json.load(handle)


class ScheduleTests(unittest.TestCase):
    def test_same_seed_gives_same_schedule(self):
        self.assertEqual(bc.build_schedule(6, 11), bc.build_schedule(6, 11))

    def test_different_seed_gives_different_schedule(self):
        seeds = {tuple(bc.build_schedule(8, s)) for s in range(12)}
        self.assertGreater(len(seeds), 1, "seed must actually vary the order")

    def test_schedule_is_balanced(self):
        schedule = bc.build_schedule(9, 3)
        self.assertEqual(schedule.count(bc.BASELINE), 9)
        self.assertEqual(schedule.count(bc.CANDIDATE), 9)

    def test_schedule_interleaves_rather_than_blocking(self):
        # A schedule that runs every baseline trial and then every candidate trial is not
        # interleaved, and would let machine drift land entirely on one side.
        for seed in range(20):
            schedule = bc.build_schedule(8, seed)
            first_half = set(schedule[: len(schedule) // 2])
            self.assertEqual(first_half, {bc.BASELINE, bc.CANDIDATE},
                             "seed %d produced a block-ordered schedule" % seed)

    def test_no_side_runs_more_than_twice_consecutively(self):
        for seed in range(20):
            schedule = bc.build_schedule(10, seed)
            run = 1
            for previous, current in zip(schedule, schedule[1:]):
                run = run + 1 if current == previous else 1
                self.assertLessEqual(run, 2, "seed %d produced a run of %d" % (seed, run))


class ControllerTests(TempCase):
    def test_runs_both_sides_and_writes_two_manifests(self):
        self.assertEqual(self.invoke(), 0)
        self.assertTrue(os.path.exists(os.path.join(self.out, "baseline.json")))
        self.assertTrue(os.path.exists(os.path.join(self.out, "candidate.json")))

    def test_merged_manifest_claims_interleaving(self):
        self.invoke()
        for name in ("baseline.json", "candidate.json"):
            self.assertTrue(self.manifest(name)["randomInterleaving"])

    def test_single_round_does_not_claim_interleaving(self):
        # One trial per side is a seeded order, but nothing is interleaved. Claiming it would be
        # exactly the invalid run the controller exists to prevent.
        self.assertEqual(self.invoke(["--rounds", "1"]), 0)
        for name in ("baseline.json", "candidate.json"):
            self.assertFalse(self.manifest(name)["randomInterleaving"])

    def test_dry_run_never_claims_interleaving(self):
        self.assertEqual(self.invoke(["--dry-run"]), 0)
        for name in ("baseline.json", "candidate.json"):
            self.assertFalse(self.manifest(name)["randomInterleaving"])

    def test_raw_trials_are_retained_one_per_round(self):
        self.invoke()
        manifest = self.manifest("baseline.json")
        self.assertEqual(manifest["repetitions"], 4)
        for record in manifest["results"]:
            self.assertEqual(len(record["wallMs"]), 4)
            self.assertEqual(len(record["cpuMs"]), 4)

    def test_samples_are_merged_in_round_order(self):
        self.invoke()
        record = self.manifest("baseline.json")["results"][0]
        # The stub returns 1.0 + invocation-index, so a correct merge is strictly increasing.
        self.assertEqual(record["wallMs"], sorted(record["wallMs"]))

    def test_host_activity_covers_every_merged_trial(self):
        self.invoke()
        for name in ("baseline.json", "candidate.json"):
            environment = self.manifest(name)["environment"]
            frequency = environment["cpuFrequencyMhz"]
            self.assertEqual(frequency["samples"], 10)
            self.assertEqual(frequency["minimum"], 3000.0)
            self.assertEqual(frequency["mean"], 3120.0)
            self.assertEqual(frequency["maximum"], 3230.0)
            self.assertAlmostEqual(environment["backgroundLoad"], 0.03)

    def test_both_sides_receive_identical_shared_options(self):
        self.invoke(["--corpus-root", "C:/corpus", "--warmup", "2", "--threads", "4"])
        shared = ("--corpus-root", "--warmup", "--threads", "--host-id", "--commit", "--timestamp")
        seen = {}
        for entry in self.invocations():
            args = entry["args"]
            picked = tuple((flag, args[args.index(flag) + 1]) for flag in shared if flag in args)
            seen.setdefault(entry["side"], set()).add(picked)
        self.assertEqual(len(seen), 2)
        values = list(seen.values())
        self.assertEqual(values[0], values[1], "the two runners were given different options")

    def test_runner_and_engine_provenance_are_forwarded_separately(self):
        digest = "c" * 64
        self.invoke(["--baseline-engine-commit", "baseline-pin", "--baseline-engine-clean",
                     "--candidate-engine-commit", "candidate-pin", "--candidate-engine-dirty",
                     "--dependency-lock-digest", digest])
        for entry in self.invocations():
            args = entry["args"]
            self.assertEqual(args[args.index("--commit") + 1], "abc123")
            self.assertEqual(args[args.index("--dependency-lock-digest") + 1], digest)
            expected = "baseline-pin" if entry["side"] == bc.BASELINE else "candidate-pin"
            self.assertEqual(args[args.index("--engine-commit") + 1], expected)
            self.assertIn("--engine-clean" if entry["side"] == bc.BASELINE
                          else "--engine-dirty", args)

    def test_engine_lto_is_forwarded_per_side_and_recorded_in_raw_trials(self):
        self.invoke(["--baseline-engine-lto", "--candidate-engine-no-lto"])
        for entry in self.invocations():
            expected = "--engine-lto" if entry["side"] == bc.BASELINE else "--engine-no-lto"
            self.assertIn(expected, entry["args"])
        self.assertTrue(self.manifest("baseline.json")["engineBuild"]["lto"])
        self.assertFalse(self.manifest("candidate.json")["engineBuild"]["lto"])

    def test_the_corpus_manifest_reaches_both_runners(self):
        # A corpus-backed scenario needs the manifest, not only the root: the root says where the
        # datasets live, the manifest says which of them this run is allowed to read and what they
        # must hash to. Only this controller may declare random interleaving, so a manifest it
        # cannot forward is a dataset no publishable run can measure.
        self.assertEqual(self.invoke(["--corpus-manifest", "C:/corpus/corpus.json"]), 0)
        self.assertTrue(self.invocations(), "no runner was launched")
        for entry in self.invocations():
            args = entry["args"]
            self.assertIn("--corpus-manifest", args, entry["side"])
            self.assertEqual(args[args.index("--corpus-manifest") + 1], "C:/corpus/corpus.json")

    def test_one_timestamp_is_stamped_for_the_whole_run(self):
        self.invoke()
        stamps = {self.manifest(n)["timestamp"] for n in ("baseline.json", "candidate.json")}
        self.assertEqual(stamps, {"2026-08-24T10:00:00Z"})

    def test_relative_runner_path_with_forward_slashes_still_launches(self):
        # What a shell hands over on Windows. CreateProcess parses a relative path spelled this way
        # as switches rather than as a path and fails with "file not found" while the file is
        # plainly there, so the controller resolves before it launches.
        nested = os.path.join(self.dir, "sub")
        os.makedirs(nested, exist_ok=True)
        relative_stub = os.path.join(nested, "stub_runner.py")
        shutil.copy(self.stub, relative_stub)
        previous = os.getcwd()
        os.chdir(self.dir)
        self.addCleanup(os.chdir, previous)
        env = dict(os.environ)
        env["STUB_MODE"] = "ok"
        env["STUB_LOG"] = self.log
        code = bc.main(["--baseline", "sub/stub_runner.py", "--candidate", "sub/stub_runner.py",
                        "--out-dir", self.out, "--rounds", "2", "--seed", "1",
                        "--python", sys.executable], env=env)
        self.assertEqual(code, 0)

    def test_schedule_is_written_alongside_the_results(self):
        self.invoke()
        with open(os.path.join(self.out, "schedule.json")) as handle:
            schedule = json.load(handle)
        self.assertEqual(schedule["seed"], 7)
        self.assertEqual(schedule["rounds"], 4)
        self.assertEqual(len(schedule["order"]), 8)


class FailureTests(TempCase):
    def test_nondeterministic_digest_across_rounds_is_a_hard_failure(self):
        # Two trials of one engine on one scenario that disagree about their own output is a
        # correctness problem. Merging them would average a number that describes two behaviours.
        self.assertEqual(self.invoke(mode="flaky"), 1)

    def test_an_estimator_whose_digest_changes_is_recorded_rather_than_failing_the_run(self):
        """A tolerance-bearing scenario cannot promise a byte-identical result run to run.

        The engine's channel estimators reduce in parallel over floating point, so the partition
        the scheduler chose decides the last bits of the sum. Failing the whole sweep on that would
        mean this host can never publish a number for any scenario; hiding it would be worse. It
        merges, and the divergence is written down beside the run.
        """
        self.assertEqual(self.invoke(mode="flaky-estimator"), 0)
        merged = self.manifest("baseline.json")
        for record in merged["results"]:
            self.assertEqual(len(record["wallMs"]), 4)
        with open(os.path.join(self.out, "schedule.json")) as handle:
            schedule = json.load(handle)
        divergences = schedule["digestDivergences"]
        self.assertTrue(divergences)
        first = divergences[0]
        self.assertIn(first["scenarioId"], ("alpha", "beta"))
        self.assertEqual(first["correctnessClass"], "EstimatorTolerance")
        self.assertEqual(sorted(first["digests"]), ["digest-a", "digest-b"])

    def test_a_deterministic_run_records_no_divergence(self):
        self.assertEqual(self.invoke(), 0)
        with open(os.path.join(self.out, "schedule.json")) as handle:
            self.assertEqual(json.load(handle)["digestDivergences"], [])

    def test_a_tolerant_trial_outside_its_oracle_limit_reaches_the_merged_record(self):
        # The controller no longer discards the whole catalogue over one scenario's metric. It says
        # which round exceeded, and the merged record keeps the worst metric of every round, so the
        # merged record - on which correctness is decided per scenario - refuses that one and nothing else.
        stream = io.StringIO()
        self.assertEqual(self.invoke(mode="bad-estimator", stream=stream), 0)
        said = stream.getvalue()
        self.assertIn("reported a correctness metric of", said)
        for name in ("baseline.json", "candidate.json"):
            for record in self.manifest(name)["results"]:
                self.assertEqual(record["correctnessClass"], "EstimatorTolerance")
                self.assertGreater(record["correctnessMetric"], record["tolerance"],
                                   "the exceeding round must survive the merge to be judged")

    def test_a_tolerant_scenario_reporting_no_metric_still_publishes_the_others(self):
        # A scenario with no oracle reports no metric in every round. That is a defect in the
        # scenario, and its merged record says so; it must not cost every other scenario its trials.
        stream = io.StringIO()
        self.assertEqual(self.invoke(mode="no-estimator", stream=stream), 0)
        self.assertIn("reported no correctness metric", stream.getvalue())
        for name in ("baseline.json", "candidate.json"):
            records = self.manifest(name)["results"]
            self.assertTrue(records)
            for record in records:
                self.assertIsNone(record["correctnessMetric"])
                self.assertEqual(len(record["wallMs"]), 4, "every round's trials must still merge")

    def test_scenario_identity_or_thread_drift_between_rounds_fails_the_run(self):
        self.assertEqual(self.invoke(mode="fixture-drift"), 1)
        shutil.rmtree(self.out, True)
        self.assertEqual(self.invoke(mode="thread-drift"), 1)

    def test_build_provenance_drift_between_rounds_fails_the_run(self):
        self.assertEqual(self.invoke(mode="build-drift"), 1)

    def test_runner_failure_fails_the_run(self):
        self.assertEqual(self.invoke(mode="fail"), 1)

    def test_missing_runner_is_an_argument_error(self):
        env = dict(os.environ)
        env["STUB_MODE"] = "ok"
        env["STUB_LOG"] = self.log
        code = bc.main(["--baseline", os.path.join(self.dir, "nope.py"), "--candidate", self.stub,
                        "--out-dir", self.out, "--python", sys.executable], env=env)
        self.assertEqual(code, 3)

    def test_zero_rounds_is_an_argument_error(self):
        self.assertEqual(self.invoke(["--rounds", "0"]), 3)

    def test_skipped_scenarios_merge_without_inventing_samples(self):
        self.assertEqual(self.invoke(mode="skipped"), 0)
        for record in self.manifest("baseline.json")["results"]:
            self.assertTrue(record["skipped"])
            self.assertEqual(record["wallMs"], [])


class IndependentCatalogueTests(TempCase):
    def write_catalogue(self, name, batch):
        path = os.path.join(self.dir, name)
        with open(path, "w") as handle:
            json.dump({"schemaVersion": 2, "scenarios": [{"id": "alpha", "parameters": {"timedBatch": batch}}]},
                      handle)
        return path

    def test_each_side_measures_its_own_catalogue_and_both_are_recorded(self):
        baseline = self.write_catalogue("baseline-catalogue.json", "4")
        candidate = self.write_catalogue("candidate-catalogue.json", "40")
        self.assertEqual(self.invoke(["--baseline-scenarios", baseline, "--candidate-scenarios", candidate]), 0)
        for entry in self.invocations():
            args = entry["args"]
            expected = baseline if entry["side"] == bc.BASELINE else candidate
            self.assertEqual(args[args.index("--scenarios") + 1], expected)
            self.assertEqual(args.count("--scenarios"), 1)
        with open(os.path.join(self.out, "schedule.json")) as handle:
            schedule = json.load(handle)
        self.assertEqual(schedule["timedBatches"], "independent")
        self.assertTrue(schedule["complete"])
        self.assertEqual(schedule["completedPairs"], 4)
        for side, path in ((bc.BASELINE, baseline), (bc.CANDIDATE, candidate)):
            with open(path, "rb") as handle:
                digest = hashlib.sha256(handle.read()).hexdigest()
            self.assertEqual(schedule["catalogues"][side], {"path": os.path.abspath(path), "sha256": digest})

    def test_a_shared_catalogue_is_recorded_as_shared(self):
        shared = self.write_catalogue("shared.json", "4")
        self.assertEqual(self.invoke(["--scenarios", shared]), 0)
        with open(os.path.join(self.out, "schedule.json")) as handle:
            schedule = json.load(handle)
        self.assertEqual(schedule["timedBatches"], "shared")
        self.assertEqual(schedule["catalogues"][bc.BASELINE], schedule["catalogues"][bc.CANDIDATE])

    def test_shared_and_per_side_catalogues_cannot_be_mixed_or_halved(self):
        baseline = self.write_catalogue("baseline-catalogue.json", "4")
        candidate = self.write_catalogue("candidate-catalogue.json", "40")
        for extra in (["--scenarios", baseline, "--baseline-scenarios", baseline, "--candidate-scenarios", candidate],
                      ["--baseline-scenarios", baseline],
                      ["--candidate-scenarios", candidate],
                      ["--baseline-scenarios", baseline, "--candidate-scenarios",
                       os.path.join(self.dir, "missing.json")]):
            self.assertEqual(self.invoke(extra), 3, extra)
        self.assertEqual(self.invocations(), [])


class DeadlineTests(TempCase):
    def test_the_deadline_stops_the_running_round_and_keeps_only_complete_pairs(self):
        stream = io.StringIO()
        started = time.monotonic()
        code = self.invoke(["--deadline-epoch", str(time.time() + 8), "--progress-seconds", "1"],
                           stream=stream, env_extra={"STUB_SLOW_FROM": "2"})
        self.assertEqual(code, bc.INCOMPLETE)
        self.assertLess(time.monotonic() - started, 60)
        # The third launch began the second pair and was stopped with its process.
        launches = self.invocations()
        self.assertEqual(len(launches), 3)
        self.assertTrue(bounded_process.wait_for_exit(launches[2]["pid"], 10))
        for name in ("baseline.json", "candidate.json"):
            self.assertFalse(os.path.exists(os.path.join(self.out, name)), name)
        for name in ("baseline-incomplete.json", "candidate-incomplete.json"):
            manifest = self.manifest(name)
            self.assertEqual(manifest["repetitions"], 1)
            self.assertFalse(manifest["randomInterleaving"])
            for record in manifest["results"]:
                self.assertEqual(len(record["wallMs"]), 1)
        with open(os.path.join(self.out, "schedule.json")) as handle:
            schedule = json.load(handle)
        self.assertFalse(schedule["complete"])
        self.assertEqual(schedule["completedPairs"], 1)
        self.assertIn("deadline", schedule["stopReason"])
        with open(os.path.join(self.out, "progress.json")) as handle:
            self.assertEqual(json.load(handle)["completedPairs"], 1)
        said = stream.getvalue()
        self.assertIn("remaining", said)
        self.assertIn("pairs 1/4 complete", said)

    def test_a_pair_that_cannot_finish_in_the_remaining_budget_is_not_started(self):
        code = self.invoke(["--deadline-epoch", str(time.time() + 30), "--pair-estimate-seconds", "45"])
        self.assertEqual(code, bc.INCOMPLETE)
        self.assertEqual(self.invocations(), [])
        with open(os.path.join(self.out, "schedule.json")) as handle:
            schedule = json.load(handle)
        self.assertEqual(schedule["completedPairs"], 0)
        self.assertIn("would not finish", schedule["stopReason"])
        self.assertFalse(os.path.exists(os.path.join(self.out, "baseline-incomplete.json")))

    def test_an_unreachable_deadline_changes_nothing_about_a_complete_run(self):
        self.assertEqual(self.invoke(["--deadline-epoch", str(time.time() + 600)]), 0)
        self.assertEqual(self.manifest("baseline.json")["repetitions"], 4)
        self.assertTrue(self.manifest("candidate.json")["randomInterleaving"])

    def test_progress_must_be_reported_at_least_once_a_minute(self):
        self.assertEqual(self.invoke(["--progress-seconds", "61"]), 3)
        self.assertEqual(self.invoke(["--progress-seconds", "0"]), 3)


class InterleavingClaimTests(TempCase):
    """check_interleaving_claim.py is the outside check on the controller's one behavioural claim."""

    def write_run(self, baseline_claim, candidate_claim):
        os.makedirs(self.out, exist_ok=True)
        for name, claim in (("baseline.json", baseline_claim), ("candidate.json", candidate_claim)):
            with open(os.path.join(self.out, name), "w") as handle:
                json.dump({"randomInterleaving": claim}, handle)

    def test_matching_claim_passes(self):
        self.write_run(False, False)
        self.assertEqual(cic.main(["--run-dir", self.out, "--expect", "false"], io.StringIO()), 0)

    def test_mismatched_claim_fails(self):
        self.write_run(True, True)
        self.assertEqual(cic.main(["--run-dir", self.out, "--expect", "false"], io.StringIO()), 1)

    def test_one_sided_claim_fails(self):
        # Both manifests come from one controller invocation, so they cannot honestly disagree.
        self.write_run(True, False)
        stream = io.StringIO()
        self.assertEqual(cic.main(["--run-dir", self.out, "--expect", "false"], stream), 1)
        self.assertIn("baseline.json", stream.getvalue())

    def test_missing_field_fails_rather_than_defaulting(self):
        os.makedirs(self.out, exist_ok=True)
        for name in ("baseline.json", "candidate.json"):
            with open(os.path.join(self.out, name), "w") as handle:
                json.dump({}, handle)
        self.assertEqual(cic.main(["--run-dir", self.out, "--expect", "false"], io.StringIO()), 1)

    def test_unreadable_run_fails(self):
        self.assertEqual(cic.main(["--run-dir", os.path.join(self.dir, "nope"),
                                   "--expect", "false"], io.StringIO()), 1)

    def test_controller_output_satisfies_the_check_end_to_end(self):
        self.assertEqual(self.invoke(["--dry-run"]), 0)
        self.assertEqual(cic.main(["--run-dir", self.out, "--expect", "false"], io.StringIO()), 0)
        self.assertEqual(cic.main(["--run-dir", self.out, "--expect", "true"], io.StringIO()), 1)



class ProgressFileTests(TempCase):
    """The qualification tool reads the progress file while the controller replaces it. On Windows a
    file another process has open cannot be replaced, and one refused replacement used to end a whole
    width's pairs."""

    def test_a_reader_holding_the_progress_file_delays_the_replacement_rather_than_failing_it(self):
        import threading
        os.makedirs(self.out, exist_ok=True)
        path = os.path.join(self.out, "progress.json")
        bc._write_json_atomically(path, {"completedPairs": 1})
        reader = open(path, "rb")
        releaser = threading.Timer(0.3, reader.close)
        releaser.start()
        try:
            bc._write_json_atomically(path, {"completedPairs": 2})
        finally:
            releaser.join()
            reader.close()
        with open(path) as handle:
            self.assertEqual(json.load(handle)["completedPairs"], 2)
        self.assertFalse(os.path.exists(path + ".tmp"))

if __name__ == "__main__":
    unittest.main(verbosity=1)

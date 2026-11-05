#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Measure how long each probed operation takes to return after a stop, and judge it by a budget.

The benchmark runner's `--cancel-at <fraction>` times and digests every scenario as usual and then runs
each probed operation once more under a stop token it requests that fraction of the way through the
operation's median time. This tool runs the runner once per fraction and reads what each probe
recorded: when the stop was actually requested, when the call returned, and what it returned.

A probe passes when the operation returned within the budget of the request and either stopped with
a failure whose cause is `Cancelled` or finished its work. It fails when the operation returned later
than the budget allows, or failed some other way. A probe whose operation returned before the request
was made measured nothing and is reported as such: that operation is shorter than the point the stop
was aimed at. A scenario the runner did not probe, or skipped, is refused evidence.

Exit codes: 0 every probe passed; 1 the runner failed or the evidence is incomplete, and nothing is
published; 2 the evidence is complete and some probe missed the budget or failed, and the report is
published; 3 bad arguments.
"""

import argparse
import json
import pathlib
import subprocess
import sys


REPORT_SCHEMA = 1
DEFAULT_FRACTIONS = (0.1, 0.5, 0.9)
DEFAULT_BUDGET_MS = 100.0


def probe_outcome(counters, budget_ms):
    """Classify one probe from the counters the runner recorded for it.

    Returns a dictionary carrying the outcome - `stopped`, `completed`, `returned-first`, `late` or
    `failed` - and whatever the counters said about it.
    """
    record = {"typicalMs": counters.get("cancelProbeTypicalMs"),
              "callMs": counters.get("cancelProbeCallMs")}
    if counters.get("cancelProbeDuringCall", 0.0) != 1.0:
        record["outcome"] = "returned-first"
        return record
    latency = counters.get("cancelProbeLatencyMs")
    record["requestedMs"] = counters.get("cancelProbeRequestedMs")
    record["latencyMs"] = latency
    failed = counters.get("cancelProbeFailed", 0.0) == 1.0
    cancelled = counters.get("cancelProbeCancelled", 0.0) == 1.0
    record["errorCode"] = counters.get("cancelProbeErrorCode", -1.0)
    if failed and not cancelled:
        record["outcome"] = "failed"
    elif latency is None or latency > budget_ms:
        record["outcome"] = "late"
    else:
        record["outcome"] = "stopped" if failed else "completed"
    return record


def analyze(manifests, budget_ms):
    """Judge every probed scenario across the runs, one run per fraction.

    `manifests` maps each fraction to the run manifest the runner wrote for it. Returns the report
    and a list of problems that make the evidence incomplete.
    """
    problems = []
    scenarios = {}
    for fraction, manifest in sorted(manifests.items()):
        for result in manifest.get("results", []):
            identity = result.get("scenarioId", "")
            entry = scenarios.setdefault(identity, {"scenarioId": identity, "probes": {}})
            if result.get("skipped"):
                problems.append(f"{identity} at {fraction}: skipped - {result.get('skipReason', '')}")
                continue
            counters = result.get("counters", {})
            if "cancelProbeFraction" not in counters:
                problems.append(f"{identity} at {fraction}: the runner does not probe this operation")
                continue
            if abs(counters["cancelProbeFraction"] - fraction) > 1e-12:
                problems.append(f"{identity} at {fraction}: the probe was made at {counters['cancelProbeFraction']}")
                continue
            entry["probes"][f"{fraction:g}"] = probe_outcome(counters, budget_ms)
    for identity, entry in sorted(scenarios.items()):
        missing = [f"{fraction:g}" for fraction in sorted(manifests) if f"{fraction:g}" not in entry["probes"]]
        if missing and not any(identity in problem for problem in problems):
            problems.append(f"{identity}: no probe at {', '.join(missing)}")
        outcomes = [probe["outcome"] for probe in entry["probes"].values()]
        latencies = [probe["latencyMs"] for probe in entry["probes"].values() if probe.get("latencyMs") is not None]
        entry["worstLatencyMs"] = max(latencies) if latencies else None
        entry["verdict"] = "fail" if any(outcome in ("late", "failed") for outcome in outcomes) else "pass"
    verdict = "pass" if all(entry["verdict"] == "pass" for entry in scenarios.values()) else "fail"
    report = {"schema": REPORT_SCHEMA,
              "budgetMs": budget_ms,
              "fractions": [f"{fraction:g}" for fraction in sorted(manifests)],
              "verdict": verdict,
              "failing": sorted(identity for identity, entry in scenarios.items() if entry["verdict"] == "fail"),
              "scenarios": [scenarios[identity] for identity in sorted(scenarios)]}
    return report, problems


def markdown(report, manifest, threads):
    """The report as a table a record or a bundle can carry."""
    environment = manifest.get("environment", {})
    lines = [f"# Cancellation latency - `{environment.get('hostId', 'unknown')}`",
             "",
             f"Budget {report['budgetMs']:g} ms from the stop request to the operation's return, at "
             f"{threads} threads; "
             f"stops at {', '.join(report['fractions'])} of each operation's median time. "
             f"Verdict: **{report['verdict']}**.",
             "",
             "| Scenario | Median ms | " + " | ".join(f"At {fraction}" for fraction in report["fractions"]) +
             " | Worst latency ms | Verdict |",
             "|---|---:|" + "---|" * len(report["fractions"]) + "---:|---|"]
    for entry in report["scenarios"]:
        cells = []
        typical = None
        for fraction in report["fractions"]:
            probe = entry["probes"].get(fraction)
            if probe is None:
                cells.append("missing")
                continue
            typical = probe.get("typicalMs", typical)
            if probe["outcome"] == "returned-first":
                cells.append(f"returned first, {probe['callMs']:.3f} ms")
            else:
                cells.append(f"{probe['outcome']} {probe['latencyMs']:.3f} ms after {probe['requestedMs']:.3f}")
        worst = entry["worstLatencyMs"]
        typical_text = "-" if typical is None else f"{typical:.3f}"
        lines.append(f"| `{entry['scenarioId']}` | {typical_text} | " + " | ".join(cells) +
                     f" | {'-' if worst is None else f'{worst:.3f}'} | {entry['verdict']} |")
    return "\n".join(lines) + "\n"


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--runner", required=True, help="the benchmark runner")
    parser.add_argument("--scenarios", required=True, help="an unbatched scenario file naming the probed rows")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--threads", type=int, required=True)
    parser.add_argument("--fractions", default=",".join(f"{value:g}" for value in DEFAULT_FRACTIONS))
    parser.add_argument("--budget-ms", type=float, default=DEFAULT_BUDGET_MS)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("runner_arguments", nargs=argparse.REMAINDER,
                        help="after --, arguments passed to the runner unchanged: provenance, corpus, power")
    args = parser.parse_args(argv)
    try:
        args.fraction_values = sorted({float(value) for value in args.fractions.split(",") if value.strip()})
    except ValueError:
        parser.error(f"--fractions must be numbers, not '{args.fractions}'")
    if not args.fraction_values or any(not 0.0 < value < 1.0 for value in args.fraction_values):
        parser.error("every fraction must lie strictly between 0 and 1")
    if args.threads < 1 or args.repetitions < 1 or args.warmup < 0 or args.budget_ms <= 0.0:
        parser.error("threads and repetitions must be positive, warm-ups non-negative, and the budget positive")
    if args.runner_arguments and args.runner_arguments[0] == "--":
        args.runner_arguments = args.runner_arguments[1:]
    return args


def main(argv=None, stream=None):
    stream = stream or sys.stdout
    try:
        args = parse_args(sys.argv[1:] if argv is None else argv)
    except SystemExit as exit_request:
        return 3 if exit_request.code else 0
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    repository = pathlib.Path(__file__).resolve().parents[3]
    # Absolute before the launch: the runner runs from the repository root, and a host may not look
    # for an executable in the current directory at all.
    runner = str(pathlib.Path(args.runner).resolve())
    scenarios = str(pathlib.Path(args.scenarios).resolve())
    manifests = {}
    for fraction in args.fraction_values:
        run_path = out_dir / f"probe-{fraction:g}.json"
        command = [runner, "--out", str(run_path.resolve()), "--scenarios", scenarios, "--threads", str(args.threads),
                   "--repetitions", str(args.repetitions), "--warmup", str(args.warmup), "--cancel-at", f"{fraction:g}",
                   *args.runner_arguments]
        print(" ".join(command), file=stream, flush=True)
        completed = subprocess.run(command, cwd=repository, capture_output=True, text=True)
        (out_dir / f"probe-{fraction:g}.log").write_text(completed.stdout + completed.stderr, encoding="utf-8")
        if completed.returncode != 0 or not run_path.is_file():
            print(f"the runner failed at {fraction:g} with exit {completed.returncode}", file=stream)
            return 1
        manifests[fraction] = json.loads(run_path.read_text(encoding="utf-8"))
    report, problems = analyze(manifests, args.budget_ms)
    if problems:
        for problem in problems:
            print(f"refused: {problem}", file=stream)
        return 1
    (out_dir / "CANCELLATION_LATENCY.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    first = manifests[args.fraction_values[0]]
    (out_dir / "CANCELLATION_LATENCY.md").write_text(markdown(report, first, args.threads), encoding="utf-8")
    print(f"verdict {report['verdict']}; failing: {', '.join(report['failing']) or 'none'}", file=stream)
    return 0 if report["verdict"] == "pass" else 2


if __name__ == "__main__":
    sys.exit(main())

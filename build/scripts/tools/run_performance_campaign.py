#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the performance campaign's registry, and judge its forced-backend runs.

`performance_matrix.txt` names the representative rows of every scenario group the engine is
measured on. A run of them on each concrete CPU backend, forced with the runner's `--backend`,
proves that every row reaches every backend it claims: a requested backend that was silently
substituted, a row the run did not cover, or runs from different hosts or builds are refused rather
than averaged into a result.

Usage:
    python build/scripts/tools/run_performance_campaign.py --check
    python build/scripts/tools/run_performance_campaign.py --forced Scalar=<run.json> ... --output <file>

`--check` reads the registry and runs nothing, which is what the common check list wants. A judged
run needs one `--forced BACKEND=MANIFEST` for every concrete CPU backend.

Tests: build/scripts/tools/run_performance_campaign_test.py
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass


class RegistryError(ValueError):
    """The checked-in performance registry is malformed."""


class EvidenceError(ValueError):
    """A forced-backend run cannot support a verdict."""


#: The group whose row runs over the reference dataset on a benchmark host only, and which a forced
#: run over the synthetic catalogue therefore does not reach.
CORPUS_GROUP = "pipelines"

EXPECTED_FORCED_BACKENDS = frozenset({
    "Scalar", "Sse2", "Sse42", "Avx", "Avx2", "Avx512", "Neon",
})


@dataclass(frozen=True)
class Registry:
    """The scenario groups and their representative rows, in file order."""

    groups: dict[str, list[str]]


def parse_registry(lines, source):
    """Parse the registry: one `group <name> <scenario> [scenario ...]` line per group."""
    groups = {}
    seen = set()
    for number, unstripped in enumerate(lines, 1):
        line = unstripped.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if fields[0] != "group" or len(fields) < 3:
            raise RegistryError(f"{source}:{number}: expected group <name> <scenario> [scenario ...]")
        name, scenarios = fields[1], fields[2:]
        if name in groups:
            raise RegistryError(f"{source}:{number}: duplicate group '{name}'")
        for scenario in scenarios:
            if scenario in seen:
                raise RegistryError(f"{source}:{number}: scenario '{scenario}' is registered twice")
            seen.add(scenario)
        groups[name] = scenarios
    if not groups:
        raise RegistryError(f"{source}: no groups")
    return Registry(groups)


def forced_scenarios(registry):
    """Every registered scenario a forced-backend run must cover."""
    return {scenario for group, scenarios in registry.groups.items() if group != CORPUS_GROUP
            for scenario in scenarios}


def _index(manifest, label):
    results = manifest.get("results")
    if not isinstance(results, list):
        raise EvidenceError(f"{label} has no results array")
    indexed = {}
    for row in results:
        scenario = row.get("scenarioId")
        if not isinstance(scenario, str) or not scenario or scenario in indexed:
            raise EvidenceError(f"{label} has a missing or duplicate scenario id")
        indexed[scenario] = row
    return indexed


def validate_forced_run(manifest, backend, expected_scenarios=None, expected_host=None,
                        expected_commit=None):
    """Fail closed when a requested concrete backend was silently substituted."""
    rows = _index(manifest, f"forced {backend}")
    if not rows:
        raise EvidenceError(f"forced {backend} is an empty run")
    if (expected_host is not None and
            manifest.get("environment", {}).get("hostId") != expected_host):
        raise EvidenceError(f"forced {backend} has a different host")
    if expected_commit is not None:
        build = manifest.get("engineBuild", {})
        if build.get("commit") != expected_commit or build.get("dirty") is not False:
            raise EvidenceError(f"forced {backend} has a different or dirty engine build")
    if expected_scenarios is not None and not set(expected_scenarios) <= set(rows):
        missing = sorted(set(expected_scenarios) - set(rows))
        raise EvidenceError(f"forced {backend} does not cover every registered scenario; missing={missing}")
    for scenario, row in rows.items():
        requested = row.get("requestedBackend")
        if requested != backend:
            raise EvidenceError(f"forced {backend} scenario '{scenario}' records requested backend '{requested}'")
        if not row.get("skipped") and row.get("actualBackend") != backend:
            raise EvidenceError(f"forced {backend} scenario '{scenario}' ran as '{row.get('actualBackend')}'")
    return {"rows": len(rows), "executed": sum(not row.get("skipped") for row in rows.values())}


def validate_forced_backend_set(backends):
    """Require one manifest for every concrete CPU request and no other backend."""
    if set(backends) != EXPECTED_FORCED_BACKENDS:
        missing = sorted(EXPECTED_FORCED_BACKENDS - set(backends))
        extra = sorted(set(backends) - EXPECTED_FORCED_BACKENDS)
        raise EvidenceError(f"forced backend set differs; missing={missing}, extra={extra}")


def _load(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError) as error:
        raise EvidenceError(f"could not read '{path}': {error}") from error


def judge(registry, forced_runs):
    """Every forced run held to the registry, one host and one clean engine build."""
    if not forced_runs:
        raise EvidenceError("judging needs a forced run for every concrete CPU backend")
    validate_forced_backend_set(forced_runs)
    first = forced_runs[sorted(forced_runs)[0]]
    host = first.get("environment", {}).get("hostId")
    commit = first.get("engineBuild", {}).get("commit")
    if not isinstance(host, str) or not host or not isinstance(commit, str) or not commit:
        raise EvidenceError("the forced runs name no host or engine build")
    expected = forced_scenarios(registry)
    report = {name: validate_forced_run(run, name, expected, host, commit) for name, run in forced_runs.items()}
    return {"schemaVersion": 1, "verdict": "pass", "host": host, "engineCommit": commit,
            "scenarios": sorted(expected), "forcedBackends": report}


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    here = os.path.dirname(os.path.abspath(__file__))
    parser.add_argument("--registry", default=os.path.join(here, "performance_matrix.txt"))
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--forced", action="append", default=[], metavar="BACKEND=MANIFEST")
    parser.add_argument("--output")
    return parser.parse_args(argv)


def main(argv=None, stream=None):
    stream = stream or sys.stderr
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        with open(args.registry, encoding="utf-8") as handle:
            registry = parse_registry(handle, args.registry)
        if args.check and not args.forced:
            stream.write(f"{len(registry.groups)} groups and "
                         f"{sum(len(scenarios) for scenarios in registry.groups.values())} scenarios\n")
            return 0
        if not args.forced or not args.output:
            raise EvidenceError("judging needs a --forced run for every concrete CPU backend and --output")
        forced = {}
        for value in args.forced:
            if "=" not in value:
                raise EvidenceError("--forced must be BACKEND=MANIFEST")
            backend, path = value.split("=", 1)
            if backend in forced:
                raise EvidenceError(f"duplicate forced backend '{backend}'")
            forced[backend] = _load(path)
        report = judge(registry, forced)
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(report, handle, indent=1)
            handle.write("\n")
        stream.write(f"performance campaign: {report['verdict']}\n")
        return 0
    except (RegistryError, EvidenceError, OSError) as error:
        stream.write(f"{error}\n")
        return 3


if __name__ == "__main__":
    sys.exit(main())

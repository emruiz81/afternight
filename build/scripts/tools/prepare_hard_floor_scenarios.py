# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Build a duration-qualified catalogue for an interleaved measurement of two builds of the engine.

The benchmark host records a minimum useful clocked duration. Smoke scenarios below that floor are
valuable correctness checks, but their timing cannot decide whether one build is slower than the
other. This tool reads one pilot of the baseline build, one of the candidate and any additional
pilots, keeps the scenarios every pilot executed, and writes the common ``timedBatch`` parameter
needed to put the fastest pilot above the floor. The runner divides the clocked batch by that count,
so pilot results remain per operation and an existing batch can be recalculated without first
stripping it.

``prepare_independent_catalogues`` instead writes one catalogue per side, each batched to its own
fastest call, for the controller's ``--baseline-scenarios``/``--candidate-scenarios``.
"""

import argparse
import json
import math
import os
import sys


TIMED_BATCH_PARAMETER = "timedBatch"
MAXIMUM_TIMED_BATCH = 1_000_000_000


def _read_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def _unique_by_id(records, label):
    by_id = {}
    for record in records:
        identity = record.get("scenarioId")
        if not isinstance(identity, str) or not identity:
            raise ValueError("%s contains a result with no scenarioId" % label)
        if identity in by_id:
            raise ValueError("%s repeats scenarioId '%s'" % (label, identity))
        by_id[identity] = record
    return by_id


def _positive_fastest(record, label):
    values = record.get("wallMs")
    if (not isinstance(values, list) or not values or
            any(isinstance(value, bool) or not isinstance(value, (int, float)) or
                not math.isfinite(value) or value <= 0 for value in values)):
        raise ValueError("%s has no positive finite wallMs samples" % label)
    return min(values)


def prepare_catalogue(catalogue, baseline_run, candidate_run, duration_floor_ms, safety_factor,
                      include_scenarios=None, additional_runs=None):
    """Return scenarios whose common timed region clears ``duration_floor_ms`` on every pilot."""
    if not math.isfinite(duration_floor_ms) or duration_floor_ms <= 0:
        raise ValueError("duration floor must be positive and finite")
    if not math.isfinite(safety_factor) or safety_factor < 1:
        raise ValueError("safety factor must be finite and at least one")

    schema_version = catalogue.get("schemaVersion")
    scenarios = catalogue.get("scenarios")
    pilots = [("baseline pilot", baseline_run), ("candidate pilot", candidate_run)]
    pilots.extend(("additional pilot %d" % (index + 1), run)
                  for index, run in enumerate(additional_runs or []))
    if isinstance(schema_version, bool) or not isinstance(schema_version, int) or schema_version <= 0:
        raise ValueError("scenario catalogue has no valid schemaVersion")
    if not isinstance(scenarios, list):
        raise ValueError("scenario catalogue has no scenarios array")
    results_by_pilot = []
    for label, run in pilots:
        results = run.get("results") if isinstance(run, dict) else None
        if not isinstance(results, list):
            raise ValueError("%s must contain a results array" % label)
        results_by_pilot.append((label, _unique_by_id(results, label)))
    selected = None if include_scenarios is None else set(include_scenarios)
    if selected is not None and len(selected) != len(include_scenarios):
        raise ValueError("included scenario ids must be unique")
    seen = set()
    prepared = []
    target_ms = duration_floor_ms * safety_factor

    for original in scenarios:
        identity = original.get("id")
        if not isinstance(identity, str) or not identity:
            raise ValueError("scenario catalogue contains an entry with no id")
        if identity in seen:
            raise ValueError("scenario catalogue repeats id '%s'" % identity)
        seen.add(identity)
        if selected is not None and identity not in selected:
            continue

        parameters = original.get("parameters")
        if not isinstance(parameters, dict):
            raise ValueError("scenario '%s' has no parameter object" % identity)
        records = [(label, by_id.get(identity)) for label, by_id in results_by_pilot]
        if any(record is None or record.get("skipped") for _, record in records):
            continue
        fastest = [_positive_fastest(record, "%s result '%s'" % (label, identity)) for label, record in records]
        batch = max(1, math.ceil(target_ms / min(fastest)))
        if batch > MAXIMUM_TIMED_BATCH:
            raise ValueError("scenario '%s' needs timedBatch %d, above the supported maximum" %
                             (identity, batch))

        scenario = dict(original)
        scenario["parameters"] = dict(parameters)
        scenario["parameters"][TIMED_BATCH_PARAMETER] = str(batch)
        prepared.append(scenario)

    if selected is not None:
        unknown = selected - seen
        if unknown:
            raise ValueError("included scenario id is not in the catalogue: %s" % sorted(unknown)[0])
    if not prepared:
        raise ValueError("the pilots have no scenario every pilot executed")
    return {"scenarios": prepared, "schemaVersion": schema_version}


def prepare_independent_catalogues(catalogue, baseline_run, candidate_run, duration_floor_ms, safety_factor,
                                   minimum_batches=None):
    """Return one catalogue per side, each batched to that side's own fastest call.

    A common batch sized for the faster build can repeat the slower one thousands of times per
    clocked region. Here each side's batch is ``ceil(floor * safety / fastest)`` of its own pilot,
    so a call that already exceeds the target runs once per region. The catalogues keep the source
    order and differ only in ``timedBatch``. Every row must have run on both sides: a row either
    pilot skipped or omitted is refused, never dropped. ``minimum_batches`` maps a side to batches
    from an earlier plan, which this plan may raise but never lower.
    """
    if not math.isfinite(duration_floor_ms) or duration_floor_ms <= 0:
        raise ValueError("duration floor must be positive and finite")
    if not math.isfinite(safety_factor) or safety_factor < 1:
        raise ValueError("safety factor must be finite and at least one")
    schema_version = catalogue.get("schemaVersion")
    if isinstance(schema_version, bool) or not isinstance(schema_version, int) or schema_version <= 0:
        raise ValueError("scenario catalogue has no valid schemaVersion")
    target_ms = duration_floor_ms * safety_factor
    prepared = {}
    for side, label, run in (("Baseline", "baseline pilot", baseline_run),
                             ("Candidate", "candidate pilot", candidate_run)):
        results = run.get("results") if isinstance(run, dict) else None
        if not isinstance(results, list):
            raise ValueError("%s must contain a results array" % label)
        by_id = _unique_by_id(results, label)
        minimum = (minimum_batches or {}).get(side, {})
        scenarios, seen = [], set()
        for original in catalogue.get("scenarios") or []:
            identity = original.get("id")
            if not isinstance(identity, str) or not identity or identity in seen:
                raise ValueError("scenario catalogue has a missing or repeated id")
            seen.add(identity)
            record = by_id.get(identity)
            if record is None or record.get("skipped"):
                raise ValueError("%s did not run scenario '%s'" % (label, identity))
            batch = max(1, math.ceil(target_ms / _positive_fastest(record, "%s result '%s'" % (label, identity))),
                        int(minimum.get(identity, 1)))
            if batch > MAXIMUM_TIMED_BATCH:
                raise ValueError("scenario '%s' needs timedBatch %d, above the supported maximum" % (identity, batch))
            scenario = dict(original)
            scenario["parameters"] = dict(original.get("parameters") or {})
            scenario["parameters"][TIMED_BATCH_PARAMETER] = str(batch)
            scenarios.append(scenario)
        if not scenarios:
            raise ValueError("the catalogue names no scenario")
        prepared[side] = {"scenarios": scenarios, "schemaVersion": schema_version}
    check_independent_catalogues(prepared["Baseline"], prepared["Candidate"])
    return prepared["Baseline"], prepared["Candidate"]


def check_independent_catalogues(baseline, candidate):
    """Refuse two catalogues unless they name the same rows, in order, differing only in their batch."""
    left, right = baseline.get("scenarios") or [], candidate.get("scenarios") or []
    if baseline.get("schemaVersion") != candidate.get("schemaVersion"):
        raise ValueError("independent catalogues have different schema versions")
    if [entry.get("id") for entry in left] != [entry.get("id") for entry in right]:
        raise ValueError("independent catalogues differ in membership or order")
    for one, other in zip(left, right):
        for entry in (one, other):
            value = (entry.get("parameters") or {}).get(TIMED_BATCH_PARAMETER)
            if not isinstance(value, str) or not value.isdigit() or not 1 <= int(value) <= MAXIMUM_TIMED_BATCH:
                raise ValueError("scenario '%s' does not declare a valid timedBatch" % entry.get("id"))

        def without_batch(entry):
            stripped = dict(entry)
            stripped["parameters"] = {key: value for key, value in entry["parameters"].items()
                                      if key != TIMED_BATCH_PARAMETER}
            return json.dumps(stripped, sort_keys=True)
        if without_batch(one) != without_batch(other):
            raise ValueError("scenario '%s' differs in more than its timedBatch" % one.get("id"))


def parse_args(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenarios", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--additional-run", action="append", default=[])
    parser.add_argument("--out", required=True)
    parser.add_argument("--duration-floor-ms", type=float, default=150.0)
    parser.add_argument("--safety-factor", type=float, default=1.2)
    parser.add_argument("--include-scenario", action="append", default=[])
    return parser.parse_args(argv)


def main(argv=None, stream=None):
    stream = stream or sys.stderr
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if os.path.exists(args.out):
        stream.write("output already exists: %s\n" % args.out)
        return 3
    try:
        prepared = prepare_catalogue(_read_json(args.scenarios), _read_json(args.baseline),
                                     _read_json(args.candidate), args.duration_floor_ms,
                                     args.safety_factor, args.include_scenario or None,
                                     [_read_json(path) for path in args.additional_run])
        with open(args.out, "x", encoding="utf-8") as handle:
            json.dump(prepared, handle, indent=2, sort_keys=True)
            handle.write("\n")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        stream.write("could not prepare hard-floor scenarios: %s\n" % error)
        return 1
    stream.write("prepared %d scenario(s) at %.3f ms with %.3fx safety\n" %
                 (len(prepared["scenarios"]), args.duration_floor_ms, args.safety_factor))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

import importlib.util
import pathlib
import unittest


HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("cancellation_latency", HERE / "cancellation_latency.py")
LATENCY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LATENCY)


def probed(fraction, during=True, latency=4.0, failed=True, cancelled=True, code=24.0, typical=40.0):
    counters = {"cancelProbeFraction": fraction,
                "cancelProbeTypicalMs": typical,
                "cancelProbeCallMs": typical * fraction + (latency if during else 0.0),
                "cancelProbeDuringCall": 1.0 if during else 0.0,
                "cancelProbeFailed": 1.0 if failed else 0.0,
                "cancelProbeCancelled": 1.0 if cancelled else 0.0,
                "cancelProbeErrorCode": code if failed else -1.0}
    if during:
        counters["cancelProbeRequestedMs"] = typical * fraction
        counters["cancelProbeLatencyMs"] = latency
    return counters


def manifest(results):
    return {"environment": {"hostId": "bench-host"}, "results": results}


def result(identity, counters=None, skipped=False):
    record = {"scenarioId": identity, "skipped": skipped, "counters": counters or {}}
    if skipped:
        record["skipReason"] = "no fixture"
    return record


class ProbeOutcomeTest(unittest.TestCase):
    def test_a_stop_answered_within_the_budget_passes_whether_it_stopped_or_finished(self):
        self.assertEqual(LATENCY.probe_outcome(probed(0.5), 100.0)["outcome"], "stopped")
        self.assertEqual(LATENCY.probe_outcome(probed(0.5, failed=False, cancelled=False), 100.0)["outcome"],
                         "completed")

    def test_a_stop_answered_after_the_budget_is_late(self):
        self.assertEqual(LATENCY.probe_outcome(probed(0.5, latency=100.5), 100.0)["outcome"], "late")

    def test_a_failure_nothing_stopped_is_a_failure_even_when_prompt(self):
        outcome = LATENCY.probe_outcome(probed(0.5, latency=1.0, cancelled=False, code=7.0), 100.0)
        self.assertEqual(outcome["outcome"], "failed")
        self.assertEqual(outcome["errorCode"], 7.0)

    def test_an_operation_that_returned_before_the_request_measured_nothing(self):
        outcome = LATENCY.probe_outcome(probed(0.9, during=False, failed=False, cancelled=False), 100.0)
        self.assertEqual(outcome["outcome"], "returned-first")
        self.assertNotIn("latencyMs", outcome)


class AnalyzeTest(unittest.TestCase):
    def test_every_scenario_is_judged_across_every_fraction(self):
        manifests = {0.1: manifest([result("a", probed(0.1)), result("b", probed(0.1))]),
                     0.9: manifest([result("a", probed(0.9, latency=250.0)), result("b", probed(0.9))])}
        report, problems = LATENCY.analyze(manifests, 100.0)
        self.assertEqual(problems, [])
        self.assertEqual(report["verdict"], "fail")
        self.assertEqual(report["failing"], ["a"])
        self.assertEqual(report["fractions"], ["0.1", "0.9"])
        byName = {entry["scenarioId"]: entry for entry in report["scenarios"]}
        self.assertEqual(byName["a"]["worstLatencyMs"], 250.0)
        self.assertEqual(byName["b"]["verdict"], "pass")

    def test_a_skipped_or_unprobed_scenario_is_refused_evidence(self):
        manifests = {0.5: manifest([result("skipped", skipped=True), result("plain", {"other": 1.0}),
                                    result("probed", probed(0.5))])}
        _, problems = LATENCY.analyze(manifests, 100.0)
        self.assertEqual(len(problems), 2)
        self.assertTrue(any("skipped" in problem for problem in problems))
        self.assertTrue(any("does not probe" in problem for problem in problems))

    def test_a_probe_made_at_another_fraction_is_refused(self):
        _, problems = LATENCY.analyze({0.5: manifest([result("a", probed(0.25))])}, 100.0)
        self.assertEqual(len(problems), 1)
        self.assertIn("0.25", problems[0])

    def test_a_scenario_missing_from_one_run_is_refused(self):
        manifests = {0.1: manifest([result("a", probed(0.1))]), 0.5: manifest([])}
        _, problems = LATENCY.analyze(manifests, 100.0)
        self.assertEqual(problems, ["a: no probe at 0.5"])

    def test_the_table_names_each_probe_and_the_verdict(self):
        manifests = {0.5: manifest([result("a", probed(0.5)), result("b", probed(0.5, during=False, failed=False))])}
        report, problems = LATENCY.analyze(manifests, 100.0)
        self.assertEqual(problems, [])
        table = LATENCY.markdown(report, manifests[0.5], 4)
        self.assertIn("`bench-host`", table)
        self.assertIn("stopped 4.000 ms after 20.000", table)
        self.assertIn("returned first", table)
        self.assertIn("**pass**", table)


class ArgumentsTest(unittest.TestCase):
    def test_fractions_must_lie_inside_the_operation(self):
        for fractions in ("0,0.5", "0.5,1", "half"):
            with self.assertRaises(SystemExit):
                LATENCY.parse_args(["--runner", "r", "--scenarios", "s", "--out-dir", "o", "--threads", "4",
                                    "--fractions", fractions])

    def test_arguments_after_the_separator_reach_the_runner(self):
        args = LATENCY.parse_args(["--runner", "r", "--scenarios", "s", "--out-dir", "o", "--threads", "4", "--",
                                   "--host-id", "bench-host", "--engine-lto"])
        self.assertEqual(args.runner_arguments, ["--host-id", "bench-host", "--engine-lto"])
        self.assertEqual(args.fraction_values, [0.1, 0.5, 0.9])


if __name__ == "__main__":
    unittest.main()

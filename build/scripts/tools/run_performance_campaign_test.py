# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for the performance campaign: its registry of scenario groups and its forced-backend runs."""

from __future__ import annotations

import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import run_performance_campaign as campaign  # noqa: E402


REGISTRY = """\
# a comment
group first anchor.one anchor.two
group second anchor.three
group pipelines pipeline.row
"""


def result(scenario, wall, requested="Unspecified", actual="Avx2", skipped=False):
    return {"scenarioId": scenario, "operationVersion": 1, "skipped": skipped,
            "skipReason": "unsupported" if skipped else "", "requestedBackend": requested,
            "actualBackend": actual, "threadCount": 4, "fixtureDigest": "f" * 64,
            "scenarioDigest": "s" * 64, "outputDigest": "o" * 64,
            "correctnessClass": "BitExact", "tolerance": 0.0,
            "wallMs": list(wall), "cpuMs": list(wall),
            "memory": {"peakResidentBytes": 100}}


def manifest(rows, host="bench", commit="candidate", dirty=False):
    return {"schemaVersion": 3, "repetitions": len(rows[0][1]),
            "warmupIterations": 2, "randomInterleaving": False,
            "engineBuild": {"commit": commit, "dirty": dirty},
            "environment": {"hostId": host, "otherwiseIdle": True},
            "results": [result(name, wall) for name, wall in rows]}


def forced(backend, scenarios=("anchor.one", "anchor.two", "anchor.three"), **identity):
    run = manifest([(name, [1.0] * 2) for name in scenarios], **identity)
    for row in run["results"]:
        row["requestedBackend"] = backend
        row["actualBackend"] = backend
    return run


class RegistryTest(unittest.TestCase):
    def test_groups_and_their_scenarios_are_parsed(self):
        registry = campaign.parse_registry(REGISTRY.splitlines(), "test")
        self.assertEqual(list(registry.groups), ["first", "second", "pipelines"])
        self.assertEqual(registry.groups["first"], ["anchor.one", "anchor.two"])

    def test_a_duplicate_group_or_scenario_is_refused(self):
        for text in ("group first one\ngroup first two\n", "group first one one\n",
                     "group first one\ngroup second one\n"):
            with self.assertRaises(campaign.RegistryError):
                campaign.parse_registry(text.splitlines(), "test")

    def test_a_group_needs_a_scenario_and_an_unknown_record_is_refused(self):
        for text in ("group empty\n", "target first 2.0 one\n", "confirmation one\n", ""):
            with self.assertRaises(campaign.RegistryError):
                campaign.parse_registry(text.splitlines(), "test")

    def test_the_forced_runs_cover_every_scenario_but_the_pipelines(self):
        registry = campaign.parse_registry(REGISTRY.splitlines(), "test")
        self.assertEqual(campaign.forced_scenarios(registry), {"anchor.one", "anchor.two", "anchor.three"})

    def test_the_repository_registry_reads(self):
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "performance_matrix.txt"),
                  encoding="utf-8") as handle:
            registry = campaign.parse_registry(handle, "performance_matrix.txt")
        self.assertGreaterEqual(len(registry.groups), 20)
        self.assertIn("pipelines", registry.groups)


class ForcedRunTest(unittest.TestCase):
    def test_a_forced_backend_substitution_is_refused(self):
        run = forced("Avx2", ["anchor.one"])
        run["results"][0]["actualBackend"] = "Scalar"
        with self.assertRaises(campaign.EvidenceError):
            campaign.validate_forced_run(run, "Avx2")

    def test_a_forced_backend_run_reports_how_many_rows_reached_it(self):
        run = forced("Scalar", ["anchor.one", "anchor.two"])
        run["results"][1]["skipped"] = True
        self.assertEqual(campaign.validate_forced_run(run, "Scalar"), {"rows": 2, "executed": 1})

    def test_a_forced_backend_run_must_cover_every_registered_scenario(self):
        with self.assertRaises(campaign.EvidenceError):
            campaign.validate_forced_run(forced("Scalar", ["anchor.one"]), "Scalar", {"anchor.one", "anchor.two"})

    def test_a_forced_backend_run_must_match_the_campaign_host_and_build(self):
        run = forced("Scalar", ["anchor.one"])
        with self.assertRaises(campaign.EvidenceError):
            campaign.validate_forced_run(run, "Scalar", {"anchor.one"}, "another-host", "candidate")
        with self.assertRaises(campaign.EvidenceError):
            campaign.validate_forced_run(run, "Scalar", {"anchor.one"}, "bench", "another-build")
        with self.assertRaises(campaign.EvidenceError):
            campaign.validate_forced_run(forced("Scalar", ["anchor.one"], dirty=True), "Scalar", {"anchor.one"},
                                         "bench", "candidate")

    def test_every_concrete_cpu_backend_is_required_once(self):
        with self.assertRaises(campaign.EvidenceError):
            campaign.validate_forced_backend_set({"Scalar", "Avx2"})
        with self.assertRaises(campaign.EvidenceError):
            campaign.validate_forced_backend_set(set(campaign.EXPECTED_FORCED_BACKENDS) | {"Gpu"})
        campaign.validate_forced_backend_set(set(campaign.EXPECTED_FORCED_BACKENDS))


class MainTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="performance-campaign")
        self.registry = os.path.join(self.directory, "matrix.txt")
        with open(self.registry, "w", encoding="utf-8") as handle:
            handle.write(REGISTRY)

    def tearDown(self):
        import shutil
        shutil.rmtree(self.directory, ignore_errors=True)

    def write(self, name, value):
        path = os.path.join(self.directory, name)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(value, handle)
        return path

    def test_the_check_reads_the_registry_and_runs_nothing(self):
        stream = io.StringIO()
        self.assertEqual(campaign.main(["--registry", self.registry, "--check"], stream), 0)
        self.assertIn("3 groups and 4 scenarios", stream.getvalue())

    def test_every_forced_run_is_judged_together(self):
        arguments = ["--registry", self.registry, "--output", os.path.join(self.directory, "PERFORMANCE.json")]
        for backend in sorted(campaign.EXPECTED_FORCED_BACKENDS):
            arguments += ["--forced", "%s=%s" % (backend, self.write(backend + ".json", forced(backend)))]
        stream = io.StringIO()
        self.assertEqual(campaign.main(arguments, stream), 0, stream.getvalue())
        with open(os.path.join(self.directory, "PERFORMANCE.json"), encoding="utf-8") as handle:
            report = json.load(handle)
        self.assertEqual(report["verdict"], "pass")
        self.assertEqual(report["forcedBackends"]["Scalar"], {"rows": 3, "executed": 3})

    def test_forced_runs_from_two_hosts_or_builds_are_refused(self):
        arguments = ["--registry", self.registry, "--output", os.path.join(self.directory, "PERFORMANCE.json")]
        for index, backend in enumerate(sorted(campaign.EXPECTED_FORCED_BACKENDS)):
            run = forced(backend, host="other" if index == 1 else "bench")
            arguments += ["--forced", "%s=%s" % (backend, self.write(backend + ".json", run))]
        self.assertEqual(campaign.main(arguments, io.StringIO()), 3)

    def test_a_run_needs_its_forced_runs_and_an_output(self):
        self.assertEqual(campaign.main(["--registry", self.registry], io.StringIO()), 3)


if __name__ == "__main__":
    unittest.main()

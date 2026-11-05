# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("prepare_hard_floor_scenarios.py")
SPEC = importlib.util.spec_from_file_location("prepare_hard_floor_scenarios", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def scenario(identity):
    return {"id": identity, "parameters": {"method": "same"}}


def result(identity, values, skipped=False):
    return {"scenarioId": identity, "wallMs": values, "skipped": skipped}


def catalogue(*identities):
    return {"schemaVersion": 2, "scenarios": [scenario(identity) for identity in identities]}


class PrepareHardFloorScenariosTest(unittest.TestCase):
    def test_batches_against_the_faster_pilot_and_preserves_the_input(self):
        source = catalogue("a")
        prepared = MODULE.prepare_catalogue(
            source,
            {"results": [result("a", [10.0, 12.0, 11.0])]},
            {"results": [result("a", [2.0, 3.0, 2.5])]},
            150.0,
            1.2)

        self.assertEqual(prepared["scenarios"][0]["parameters"]["timedBatch"], "90")
        self.assertEqual(prepared["schemaVersion"], 2)
        self.assertNotIn("timedBatch", source["scenarios"][0]["parameters"])

    def test_can_batch_a_zero_copy_anchor_above_one_million_iterations(self):
        prepared = MODULE.prepare_catalogue(
            catalogue("view"),
            {"results": [result("view", [0.000040])]},
            {"results": [result("view", [0.000003])]},
            150.0,
            1.0)

        self.assertEqual(prepared["scenarios"][0]["parameters"]["timedBatch"], "50000000")

    def test_additional_pilots_participate_in_the_fastest_side_and_coverage(self):
        prepared = MODULE.prepare_catalogue(
            catalogue("a", "b"),
            {"results": [result("a", [10.0]), result("b", [10.0])]},
            {"results": [result("a", [5.0]), result("b", [5.0])]},
            100.0,
            1.0,
            additional_runs=[{"results": [result("a", [2.0]), result("b", [2.0], True)]}])

        self.assertEqual([entry["id"] for entry in prepared["scenarios"]], ["a"])
        self.assertEqual(prepared["scenarios"][0]["parameters"]["timedBatch"], "50")

    def test_recalculates_a_catalogue_that_is_already_batched(self):
        source = catalogue("a")
        source["scenarios"][0]["parameters"]["timedBatch"] = "99"

        prepared = MODULE.prepare_catalogue(
            source,
            {"results": [result("a", [4.0])]},
            {"results": [result("a", [2.0])]},
            10.0,
            1.0)

        self.assertEqual(prepared["scenarios"][0]["parameters"]["timedBatch"], "5")
        self.assertEqual(source["scenarios"][0]["parameters"]["timedBatch"], "99")

    def test_keeps_only_scenarios_every_pilot_executed(self):
        source = catalogue("a", "b", "c")
        prepared = MODULE.prepare_catalogue(
            source,
            {"results": [result("a", [2.0]), result("b", [2.0], True)]},
            {"results": [result("a", [1.0]), result("b", [1.0]), result("c", [1.0])]},
            10.0,
            1.0)

        self.assertEqual([entry["id"] for entry in prepared["scenarios"]], ["a"])

    def test_can_prepare_a_named_inconclusive_subset(self):
        source = catalogue("a", "b", "c")
        prepared = MODULE.prepare_catalogue(
            source,
            {"results": [result(identity, [2.0]) for identity in ("a", "b", "c")]},
            {"results": [result(identity, [1.0]) for identity in ("a", "b", "c")]},
            10.0,
            1.0,
            ["b", "c"])

        self.assertEqual([entry["id"] for entry in prepared["scenarios"]], ["b", "c"])
        with self.assertRaisesRegex(ValueError, "not in the catalogue"):
            MODULE.prepare_catalogue(
                source,
                {"results": [result("a", [2.0])]},
                {"results": [result("a", [1.0])]},
                10.0,
                1.0,
                ["missing"])

    def test_refuses_invalid_timings(self):
        with self.assertRaisesRegex(ValueError, "positive finite"):
            MODULE.prepare_catalogue(
                catalogue("a"),
                {"results": [result("a", [0.0])]},
                {"results": [result("a", [1.0])]},
                10.0,
                1.0)

    def test_refuses_duplicate_results_and_empty_comparisons(self):
        with self.assertRaisesRegex(ValueError, "repeats scenarioId"):
            MODULE.prepare_catalogue(
                catalogue("a"),
                {"results": [result("a", [1.0]), result("a", [1.0])]},
                {"results": [result("a", [1.0])]},
                10.0,
                1.0)

        with self.assertRaisesRegex(ValueError, "no scenario"):
            MODULE.prepare_catalogue(
                catalogue("a"),
                {"results": [result("a", [1.0], True)]},
                {"results": [result("a", [1.0])]},
                10.0,
                1.0)

        with self.assertRaisesRegex(ValueError, "schemaVersion"):
            MODULE.prepare_catalogue(
                {"scenarios": [scenario("a")]},
                {"results": [result("a", [1.0])]},
                {"results": [result("a", [1.0])]},
                10.0,
                1.0)


def batches(prepared):
    return {entry["id"]: entry["parameters"]["timedBatch"] for entry in prepared["scenarios"]}


class IndependentCataloguesTest(unittest.TestCase):
    def test_each_side_is_batched_to_its_own_fastest_call(self):
        baseline, candidate = MODULE.prepare_independent_catalogues(
            catalogue("a", "b"),
            {"results": [result("a", [70.0, 60.0]), result("b", [1000.0])]},
            {"results": [result("a", [12.0]), result("b", [2.0])]},
            150.0,
            1.5)

        # A slow call already past the target runs once per region; nothing is sized for the other side.
        self.assertEqual(batches(baseline), {"a": "4", "b": "1"})
        self.assertEqual(batches(candidate), {"a": "19", "b": "113"})
        self.assertEqual([entry["id"] for entry in baseline["scenarios"]], ["a", "b"])
        self.assertEqual([entry["id"] for entry in candidate["scenarios"]], ["a", "b"])
        MODULE.check_independent_catalogues(baseline, candidate)

    def test_a_row_either_pilot_did_not_run_is_refused_rather_than_dropped(self):
        for baseline_results in ([result("a", [5.0])], [result("a", [5.0]), result("b", [5.0], True)]):
            with self.assertRaisesRegex(ValueError, "'b'"):
                MODULE.prepare_independent_catalogues(
                    catalogue("a", "b"),
                    {"results": baseline_results},
                    {"results": [result("a", [1.0]), result("b", [1.0])]},
                    150.0,
                    1.5)

    def test_a_previous_plan_only_raises_a_batch(self):
        baseline, candidate = MODULE.prepare_independent_catalogues(
            catalogue("a"),
            {"results": [result("a", [50.0])]},
            {"results": [result("a", [10.0])]},
            150.0,
            1.5,
            minimum_batches={"Baseline": {"a": 9}, "Candidate": {"a": 2}})

        self.assertEqual(batches(baseline), {"a": "9"})
        self.assertEqual(batches(candidate), {"a": "23"})

    def test_catalogues_that_differ_beyond_their_batch_are_refused(self):
        baseline, candidate = MODULE.prepare_independent_catalogues(
            catalogue("a", "b"),
            {"results": [result("a", [50.0]), result("b", [50.0])]},
            {"results": [result("a", [10.0]), result("b", [10.0])]},
            150.0,
            1.5)
        changed = {"schemaVersion": 2, "scenarios": [dict(entry) for entry in candidate["scenarios"]]}
        changed["scenarios"][0]["parameters"] = dict(changed["scenarios"][0]["parameters"], method="other")
        with self.assertRaisesRegex(ValueError, "more than"):
            MODULE.check_independent_catalogues(baseline, changed)
        reordered = {"schemaVersion": 2, "scenarios": list(reversed(candidate["scenarios"]))}
        with self.assertRaisesRegex(ValueError, "order"):
            MODULE.check_independent_catalogues(baseline, reordered)
        unbatched = {"schemaVersion": 2, "scenarios": [dict(entry, parameters={"method": "same"})
                                                       for entry in candidate["scenarios"]]}
        with self.assertRaisesRegex(ValueError, "timedBatch"):
            MODULE.check_independent_catalogues(baseline, unbatched)


if __name__ == "__main__":
    unittest.main()

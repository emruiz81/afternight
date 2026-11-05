# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for run_conformance_campaign.

Run with:
    python build/scripts/tools/run_conformance_campaign_test.py

Most cases build a throwaway registry and a throwaway generated tree, so they need nothing from the
real repository and nothing built. The last few read the repository's own registry and its own suite
sources, because a runner that only ever agrees with a synthetic campaign proves nothing about the
campaign that is run.
"""

from __future__ import annotations

import io
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import run_conformance_campaign as campaign  # noqa: E402


REGISTRY = ('# a comment\n'
            '\n'
            'suite Test_EngineCore   always\n'
            'suite Test_EngineFixtureBridge always\n'
            'suite Test_EngineProof  optional\n'
            'suite Test_EngineClient always program\n'
            'case  Test_EngineCore   ExtentTest.AnExtentRefusesZero\n')

PREMAKE = ('    addEngineTestProject { name = "Test_EngineCore", links_gtest = true }\n'
           '    addEngineTestProject { name = "Test_EngineClient", links_gtest = false }\n'
           '    if _OPTIONS["with-proof"] then\n'
           '        addEngineTestProject { name = "Test_EngineProof", links_gtest = true }\n'
           '    end\n'
           '    project "Test_EngineFixtureBridge"\n'
           # The benchmark layer's suite, which carries the engine suites' prefix and is not one of them.
           '    project "Test_EngineBenchCommon"\n')


def registry_from(text):
    """Parse registry text without writing a file.

    @param text the registry's contents.
    @return the parsed registry.
    """
    return campaign.parse_registry(text.splitlines(), 'test-registry')


class RegistryTest(unittest.TestCase):
    def test_a_registry_reads_its_two_record_types(self):
        registry = registry_from(REGISTRY)
        self.assertEqual([suite.name for suite in registry.suites],
                         ['Test_EngineCore', 'Test_EngineFixtureBridge', 'Test_EngineProof', 'Test_EngineClient'])
        self.assertEqual(registry.suites[0].presence, 'always')
        self.assertEqual(registry.suites[2].presence, 'optional')
        self.assertEqual(registry.suites[0].harness, 'gtest')
        self.assertEqual(registry.suites[3].harness, 'program')
        self.assertEqual(registry.suites[0].cases, ['ExtentTest.AnExtentRefusesZero'])

    def test_an_unknown_record_type_is_refused(self):
        for text in ('scenario alloc.stream allocation\n', 'difference demosaic.vng4.f32 record=9\n'):
            with self.assertRaises(campaign.RegistryError):
                registry_from(text)

    def test_a_repeated_suite_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            registry_from('suite Test_EngineCore always\nsuite Test_EngineCore always\n')

    def test_an_unknown_harness_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            registry_from('suite Test_EngineCore always shell\n')

    def test_a_case_in_a_program_suite_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            registry_from('suite Test_EngineClient always program\ncase Test_EngineClient A.B\n')

    def test_an_unknown_presence_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            registry_from('suite Test_EngineCore sometimes\n')

    def test_a_case_for_an_unregistered_suite_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            registry_from('case Test_EngineCore ExtentTest.AnExtentRefusesZero\n')


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='conformance-check-'))
        (self.root / 'build' / 'premake').mkdir(parents=True)
        (self.root / 'build' / 'premake' / 'afternight-test-pm5.lua').write_text(PREMAKE, encoding='utf-8')
        sources = self.root / 'src' / 'AfterNightTests' / 'Test_EngineCore'
        sources.mkdir(parents=True)
        (sources / 'ExtentTest.cpp').write_text('TEST(ExtentTest, AnExtentRefusesZero)\n{\n}\n', encoding='utf-8')

    def tearDown(self):
        import shutil
        shutil.rmtree(self.root, ignore_errors=True)

    def check(self, registry_text=REGISTRY):
        stream = io.StringIO()
        code = campaign.check(self.root, registry_from(registry_text), stream=stream)
        return code, stream.getvalue()

    def test_a_consistent_registry_passes(self):
        code, output = self.check()
        self.assertEqual(code, 0, output)

    def test_a_suite_the_generator_does_not_define_fails(self):
        code, output = self.check(REGISTRY + 'suite Test_EngineGhost always\n')
        self.assertEqual(code, 1)
        self.assertIn('Test_EngineGhost', output)

    def test_a_generated_suite_the_registry_does_not_name_fails(self):
        """The register is read in both directions: a new suite is not silently outside `all`."""
        text = REGISTRY.replace('suite Test_EngineCore   always\n', '')
        text = text.replace('case  Test_EngineCore   ExtentTest.AnExtentRefusesZero\n', '')
        code, output = self.check(text)
        self.assertEqual(code, 1)
        self.assertIn('Test_EngineCore', output)

    def test_a_suite_generated_behind_a_condition_need_not_be_registered(self):
        text = REGISTRY.replace('suite Test_EngineProof  optional\n', '')
        code, output = self.check(text)
        self.assertEqual(code, 0, output)

    def test_a_nominated_case_the_suite_does_not_define_fails(self):
        code, output = self.check(REGISTRY + 'case Test_EngineCore ExtentTest.AnExtentAllowsZero\n')
        self.assertEqual(code, 1)
        self.assertIn('AnExtentAllowsZero', output)


class SuiteTest(unittest.TestCase):
    PASSING = ('[==========] Running 12 tests from 3 test suites.\n'
               '[       OK ] ExtentTest.AnExtentRefusesZero (0 ms)\n'
               '[==========] 12 tests from 3 test suites ran. (31 ms total)\n'
               '[  PASSED  ] 11 tests.\n'
               '[  SKIPPED ] 1 test, listed below:\n')

    def test_a_passing_suite_reports_its_counts(self):
        report = campaign.parse_gtest(self.PASSING)
        self.assertEqual((report.ran, report.passed, report.skipped, report.failed), (12, 11, 1, 0))

    def test_a_failing_suite_reports_its_failures(self):
        text = self.PASSING.replace('[  PASSED  ] 11 tests.', '[  PASSED  ] 10 tests.\n[  FAILED  ] 1 test')
        report = campaign.parse_gtest(text)
        self.assertEqual(report.failed, 1)

    def test_a_suite_that_printed_no_summary_reports_nothing(self):
        self.assertIsNone(campaign.parse_gtest('the engine failed to start\n'))

    def test_the_skipped_cases_are_named(self):
        text = self.PASSING + '[  SKIPPED ] DemosaicConformanceTest.Avx512ExecutesWhenTheHostOffersIt' + chr(10)
        report = campaign.parse_gtest(text)
        self.assertEqual(report.skippedCases, ['DemosaicConformanceTest.Avx512ExecutesWhenTheHostOffersIt'])

    def test_the_skipped_count_line_is_not_a_case(self):
        self.assertEqual(campaign.parse_gtest(self.PASSING).skippedCases, [])

    def test_a_nominated_case_is_read_from_the_output(self):
        report = campaign.parse_gtest(self.PASSING)
        self.assertIn('ExtentTest.AnExtentRefusesZero', report.cases)

    def test_a_suite_that_did_not_run_its_nominated_case_fails(self):
        suite = campaign.Suite('Test_EngineCore', 'always')
        suite.cases.append('ExtentTest.SomethingElse')
        problems = campaign.judge_suite(suite, campaign.parse_gtest(self.PASSING), 0)
        self.assertEqual(len(problems), 1)
        self.assertIn('SomethingElse', problems[0])

    def test_a_suite_that_ran_nothing_fails(self):
        suite = campaign.Suite('Test_EngineCore', 'always')
        text = ('[==========] Running 0 tests from 0 test suites.\n'
                '[==========] 0 tests from 0 test suites ran. (0 ms total)\n'
                '[  PASSED  ] 0 tests.\n')
        problems = campaign.judge_suite(suite, campaign.parse_gtest(text), 0)
        self.assertEqual(len(problems), 1)

    def test_a_suite_that_exited_non_zero_fails(self):
        suite = campaign.Suite('Test_EngineCore', 'always')
        problems = campaign.judge_suite(suite, campaign.parse_gtest(self.PASSING), 1)
        self.assertEqual(len(problems), 1)

    def test_a_program_suite_is_judged_by_its_exit_code_and_its_output(self):
        suite = campaign.Suite('Test_EngineClient', 'always', 'program')
        self.assertEqual(campaign.judge_suite(suite, None, 0, 'the engine reported 0.94.0\n'), [])
        self.assertEqual(len(campaign.judge_suite(suite, None, 1, 'the engine reported 0.94.0\n')), 1)
        self.assertEqual(len(campaign.judge_suite(suite, None, 0, '   \n')), 1)


class GeneratedTreeTest(unittest.TestCase):
    """An optional suite is evidence when the tree generates it, never when a binary is lying there."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='conformance-generated-'))
        (self.root / 'build' / 'vs2026-msvc' / 'tests').mkdir(parents=True)
        (self.root / 'build' / 'vs2026-msvc' / 'tests' / 'Test_EngineCore.vcxproj').write_text('x',
                                                                                             encoding='utf-8')
        (self.root / 'build' / 'make-linux').mkdir(parents=True)
        (self.root / 'build' / 'make-linux' / 'Test_EngineImage.make').write_text('x', encoding='utf-8')

    def tearDown(self):
        import shutil
        shutil.rmtree(self.root, ignore_errors=True)

    def test_a_project_in_either_generated_tree_counts(self):
        self.assertTrue(campaign.is_generated(self.root, 'Test_EngineCore'))
        self.assertTrue(campaign.is_generated(self.root, 'Test_EngineImage'))
        self.assertFalse(campaign.is_generated(self.root, 'Test_EngineProof'))

    def test_an_ungenerated_optional_suite_is_not_run_even_when_a_binary_is_there(self):
        binaries = self.root / 'bin' / ('msvc' if os.name == 'nt' else 'clang') / 'release'
        binaries.mkdir(parents=True)
        campaign.binary_path(binaries, 'Test_EngineProof', 'release').write_text('x', encoding='utf-8')
        registry = registry_from('suite Test_EngineProof optional\n')
        output = self.root / 'out'
        output.mkdir()
        problems, summaries = campaign.run_suites(registry, self.root, binaries, 'release', output,
                                                  60, stream=io.StringIO())
        self.assertEqual(problems, [])
        self.assertEqual(summaries[0]['state'], 'not generated (stale binary ignored)')

    def test_an_ungenerated_always_present_suite_fails(self):
        registry = registry_from('suite Test_EngineProof always\n')
        output = self.root / 'out'
        output.mkdir()
        problems, _ = campaign.run_suites(registry, self.root, self.root / 'bin', 'release', output,
                                          60, stream=io.StringIO())
        self.assertEqual(len(problems), 1)
        self.assertIn('Test_EngineProof', problems[0])


class RepositoryTest(unittest.TestCase):
    """The registry this repository actually ships, read against this repository's own files."""

    def test_the_registry_reads(self):
        registry = campaign.read_registry()
        self.assertGreaterEqual(len(registry.suites), 19)

    def test_the_registry_and_the_repository_agree(self):
        self.assertEqual(campaign.check(campaign.REPO_ROOT, campaign.read_registry(), stream=io.StringIO()), 0)

    def test_every_registered_suite_is_an_engine_suite(self):
        for suite in campaign.read_registry().suites:
            self.assertTrue(suite.name.startswith('Test_Engine'), suite.name)

    def test_the_nominated_cases_are_named(self):
        """Cases the campaign requires to run and pass are named, not narrated."""
        nominated = {case for suite in campaign.read_registry().suites for case in suite.cases}
        for case in ('StackExecutorTest.RegionAccessKeepsTermsForOneTileWhenEachTileIsFinalOnItsOwn',
                     'StackPlannerTest.RegionAccessSizesTheTermsOfAnElementwiseStackWithTheTile',
                     'StarReductionExecutionTest.AContiguousRunReducesEverySampleToTheBitsOfTheStridedPath',
                     'RegistrationPlanTest.AnAffineModelMapsForwardToTheBitsOfTheSameMatrixDeclaredProjective',
                     'PhotometricColorCalibrationTest.ARowThatLeavesTheIntervalAtOneEndAloneIsCountedAndHeld'):
            self.assertIn(case, nominated)


if __name__ == '__main__':
    unittest.main(verbosity=2)

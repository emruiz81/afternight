# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for run_fault_campaign.

Run with:
    python build/scripts/tools/run_fault_campaign_test.py

Most cases write a throwaway registry and a throwaway suite source, so they need nothing from the
real repository. The last few read the repository's own registry and its own suite, because a runner
that only ever agrees with a synthetic campaign proves nothing about the campaign that is run.

The suite-running half is covered with a stand-in program that prints report lines, rather than with
the real suite: what this module decides is which scenarios must report, what a missing report means,
what a point count below the registered minimum means, and what a suite that never comes back means -
none of which needs a real fault sweep, and the last of which could not be produced by one on demand.
"""

from __future__ import annotations

import io
import os
import pathlib
import shutil
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import run_fault_campaign as campaign  # noqa: E402


REGISTRY = ('# a comment\n'
            '\n'
            'scenario alloc.stream  allocation   FaultCampaign.AnAllocationIsRefused minimum=12\n'
            'scenario io.write      io           FaultCampaign.TheEncoderFails       minimum=5\n')

SOURCE = ('TEST(FaultCampaign, AnAllocationIsRefused)\n'
          '{\n'
          '    Sweep sweep("alloc.stream", FaultKind::Allocation);\n'
          '}\n'
          'TEST(FaultCampaign, TheEncoderFails)\n'
          '{\n'
          '    Sweep sweep("io.write", an::faults::FaultKind::InputOutput);\n'
          '}\n')

STAND_IN_SUFFIX = '.bat' if os.name == 'nt' else '.sh'


def report(label, kind, points, runs=None, reached=None, refused=None, absorbed=0):
    """One scenario's report line, as the suite prints it.

    @param label the scenario.
    @param kind what it injects.
    @param points how many injection points it found.
    @param runs how many injected runs there were; the point count by default.
    @param reached how many fired; every run by default.
    @param refused how many answered with an accepted failure; every run that fired by default.
    @param absorbed how many fired and succeeded anyway.
    @return the line, without its newline.
    """
    runs = points if runs is None else runs
    reached = runs if reached is None else reached
    refused = reached if refused is None else refused
    return ('an-fault-scenario label=%s kind=%s points=%d runs=%d reached=%d refused=%d absorbed=%d seconds=0.10'
            % (label, kind, points, runs, reached, refused, absorbed))


class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fault-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)

    def write(self, text):
        path = self.root / 'fault_points.txt'
        path.write_text(text, encoding='utf-8')
        return path

    def test_reads_scenarios_in_order(self):
        scenarios = campaign.read_registry(self.write(REGISTRY))
        self.assertEqual([scenario.label for scenario in scenarios], ['alloc.stream', 'io.write'])
        self.assertEqual(scenarios[0].kind, 'allocation')
        self.assertEqual(scenarios[0].minimum, 12)
        self.assertEqual(scenarios[1].test, 'FaultCampaign.TheEncoderFails')

    def test_an_empty_registry_is_refused(self):
        """A campaign that runs nothing must not be able to report a pass."""
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('# nothing here\n'))

    def test_a_label_listed_twice_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write(REGISTRY + 'scenario alloc.stream allocation FaultCampaign.X minimum=1\n'))

    def test_an_unknown_kind_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('scenario a.b timeout FaultCampaign.X minimum=1\n'))

    def test_a_test_outside_the_campaign_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('scenario a.b io OtherSuite.X minimum=1\n'))

    def test_a_minimum_of_zero_is_refused(self):
        """A scenario that may find no injection point would pass by doing nothing."""
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('scenario a.b io FaultCampaign.X minimum=0\n'))

    def test_a_record_that_is_not_a_scenario_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('configuration a.b io FaultCampaign.X minimum=1\n'))

    def test_a_missing_field_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('scenario a.b io FaultCampaign.X\n'))


class DeclarationTests(unittest.TestCase):
    """What the suite's sources are read to say, which is half of what `--check` compares."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fault-decl-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.sources = self.root / campaign.SUITE_SOURCES
        self.sources.mkdir(parents=True)

    def write(self, text, name='Scenarios.cpp'):
        (self.sources / name).write_text(text, encoding='utf-8')

    def test_reads_the_label_kind_and_enclosing_case(self):
        self.write(SOURCE)
        declared, found = campaign.declared_scenarios(self.root)
        self.assertTrue(found)
        self.assertEqual(declared['alloc.stream'], ('FaultCampaign.AnAllocationIsRefused', 'allocation'))
        self.assertEqual(declared['io.write'], ('FaultCampaign.TheEncoderFails', 'io'))

    def test_a_tree_without_the_suite_is_reported_as_absent_rather_than_empty(self):
        """Nothing to compare against and a suite that declares nothing are different answers."""
        empty = pathlib.Path(tempfile.mkdtemp(prefix='an-fault-empty-'))
        self.addCleanup(shutil.rmtree, empty, True)
        declared, found = campaign.declared_scenarios(empty)
        self.assertEqual(declared, {})
        self.assertFalse(found)


class CheckTests(unittest.TestCase):
    """`--check`: the registry and the suite have to describe the same campaign."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fault-check-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.sources = self.root / campaign.SUITE_SOURCES
        self.sources.mkdir(parents=True)
        (self.sources / 'Scenarios.cpp').write_text(SOURCE, encoding='utf-8')
        self.registry = self.root / 'fault_points.txt'
        self.registry.write_text(REGISTRY, encoding='utf-8')

    def check(self):
        stream = io.StringIO()
        return campaign.check(self.root, self.registry, stream), stream.getvalue()

    def test_a_registry_that_matches_the_suite_passes(self):
        code, output = self.check()
        self.assertEqual(code, 0, output)

    def test_a_registered_scenario_the_suite_does_not_declare_fails(self):
        self.registry.write_text(REGISTRY + 'scenario cancel.x cancellation FaultCampaign.Y minimum=1\n',
                                 encoding='utf-8')
        code, output = self.check()
        self.assertEqual(code, 1)
        self.assertIn('cancel.x is registered but no scenario in the suite declares it', output)

    def test_a_declared_scenario_the_registry_does_not_run_fails(self):
        """A scenario nobody runs is worse than one that does not exist: it looks like coverage."""
        (self.sources / 'More.cpp').write_text('TEST(FaultCampaign, Extra)\n{\n'
                                               '    Sweep sweep("external.extra", FaultKind::External);\n}\n',
                                               encoding='utf-8')
        code, output = self.check()
        self.assertEqual(code, 1)
        self.assertIn('external.extra is declared by the suite but the registry does not run it', output)

    def test_a_kind_the_two_disagree_about_fails(self):
        self.registry.write_text(REGISTRY.replace('alloc.stream  allocation', 'alloc.stream  cancellation'),
                                 encoding='utf-8')
        code, output = self.check()
        self.assertEqual(code, 1)
        self.assertIn('registered as cancellation and declared as allocation', output)

    def test_a_test_name_the_two_disagree_about_fails(self):
        self.registry.write_text(REGISTRY.replace('FaultCampaign.AnAllocationIsRefused',
                                                  'FaultCampaign.SomethingElse'), encoding='utf-8')
        code, output = self.check()
        self.assertEqual(code, 1)
        self.assertIn('registered as FaultCampaign.SomethingElse', output)

    def test_an_unreadable_registry_fails_rather_than_passing_quietly(self):
        code, output = campaign.check(self.root, self.root / 'nothing.txt', io.StringIO()), ''
        self.assertEqual(code, 1)


class JudgeTests(unittest.TestCase):
    """What the reports have to say before a campaign is a pass."""

    def setUp(self):
        self.scenarios = campaign.read_registry(self.write_registry())

    def write_registry(self):
        root = pathlib.Path(tempfile.mkdtemp(prefix='an-fault-judge-'))
        self.addCleanup(shutil.rmtree, root, True)
        path = root / 'fault_points.txt'
        path.write_text(REGISTRY, encoding='utf-8')
        return path

    def judge(self, text, code=0):
        stream = io.StringIO()
        return campaign.judge(self.scenarios, campaign.parse_reports(text), code, stream), stream.getvalue()

    def test_every_scenario_reporting_its_minimum_passes(self):
        text = report('alloc.stream', 'allocation', 14) + '\n' + report('io.write', 'io', 5)
        code, output = self.judge(text)
        self.assertEqual(code, 0, output)

    def test_a_scenario_that_reported_nothing_fails(self):
        code, output = self.judge(report('alloc.stream', 'allocation', 14))
        self.assertEqual(code, 1)
        self.assertIn('io.write reported nothing; it did not run', output)

    def test_fewer_points_than_registered_fails(self):
        """The failure this registry exists for: a sweep that quietly stopped covering its subject."""
        text = report('alloc.stream', 'allocation', 3) + '\n' + report('io.write', 'io', 5)
        code, output = self.judge(text)
        self.assertEqual(code, 1)
        self.assertIn('alloc.stream found 3 injection point(s) and is registered for at least 12', output)

    def test_a_sweep_whose_injection_never_fired_fails(self):
        text = report('alloc.stream', 'allocation', 14, reached=0, refused=0) + '\n' + report('io.write', 'io', 5)
        code, output = self.judge(text)
        self.assertEqual(code, 1)
        self.assertIn('not one injection fired', output)

    def test_a_scenario_that_ran_as_another_kind_fails(self):
        text = report('alloc.stream', 'cancellation', 14) + '\n' + report('io.write', 'io', 5)
        code, output = self.judge(text)
        self.assertEqual(code, 1)
        self.assertIn('ran as cancellation and is registered as allocation', output)

    def test_a_reported_scenario_the_registry_does_not_name_fails(self):
        text = (report('alloc.stream', 'allocation', 14) + '\n' + report('io.write', 'io', 5) + '\n' +
                report('external.mystery', 'external', 2))
        code, output = self.judge(text)
        self.assertEqual(code, 1)
        self.assertIn('external.mystery reported a sweep and the registry does not name it', output)

    def test_a_failing_suite_fails_the_campaign_even_when_every_sweep_reported(self):
        text = report('alloc.stream', 'allocation', 14) + '\n' + report('io.write', 'io', 5)
        code, output = self.judge(text, code=1)
        self.assertEqual(code, 1)
        self.assertIn('the suite reported a failure: exit 1', output)

    def test_an_absorbed_injection_is_reported_and_is_not_a_failure(self):
        """An engine may legitimately recover from a refused allocation; the number says how often."""
        text = (report('alloc.stream', 'allocation', 14, reached=14, refused=13, absorbed=1) + '\n' +
                report('io.write', 'io', 5))
        code, output = self.judge(text)
        self.assertEqual(code, 0, output)
        self.assertIn('absorbed', output)


def write_stand_in(path, lines, exit_code=0, sleep_seconds=0):
    """A program that prints report lines and exits with a stated code.

    @param path where to write it.
    @param lines what to print.
    @param exit_code what it exits with.
    @param sleep_seconds how long to wait first, for the case about a campaign that never comes back.
    """
    if os.name == 'nt':
        body = ['@echo off']
        if sleep_seconds:
            body.append('ping -n {} 127.0.0.1 >nul'.format(sleep_seconds + 1))
        body.extend('echo ' + line for line in lines)
        body.append('exit /b {}'.format(exit_code))
        path.write_text('\r\n'.join(body) + '\r\n', encoding='utf-8')
    else:
        body = ['#!/bin/sh']
        if sleep_seconds:
            body.append('sleep {}'.format(sleep_seconds))
        body.extend('echo "{}"'.format(line) for line in lines)
        body.append('exit {}'.format(exit_code))
        path.write_text('\n'.join(body) + '\n', encoding='utf-8')
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class RunTests(unittest.TestCase):
    """What running the campaign does, with a stand-in in place of the suite."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fault-run-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.registry = self.root / 'fault_points.txt'
        self.registry.write_text(REGISTRY, encoding='utf-8')
        self.binaries = campaign.binary_directory(self.root, 'release')
        self.binaries.mkdir(parents=True)
        # Windows will not execute a file named `.exe` that is not one, and a test cannot produce a
        # real suite. What these cases are about is what the runner does with what the suite printed,
        # so the spelling of the path is stood aside here and checked on its own in BinaryPathTests.
        original = campaign.binary_path
        campaign.binary_path = lambda directory, config, suite=campaign.SUITE: directory / (suite + STAND_IN_SUFFIX)
        self.addCleanup(setattr, campaign, 'binary_path', original)

    def stand_in(self, lines, exit_code=0, sleep_seconds=0):
        write_stand_in(self.binaries / (campaign.SUITE + STAND_IN_SUFFIX), lines, exit_code, sleep_seconds)

    def run_campaign(self, *extra):
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--registry', str(self.registry),
                              '--storage-root', str(self.root / 'storage')] + list(extra), stream)
        return code, stream.getvalue()

    def test_a_campaign_whose_scenarios_all_reported_passes(self):
        self.stand_in([report('alloc.stream', 'allocation', 14), report('io.write', 'io', 5)])
        code, output = self.run_campaign()
        self.assertEqual(code, 0, output)
        self.assertIn('every registered scenario swept its points', output)

    def test_a_campaign_with_a_silent_scenario_fails(self):
        self.stand_in([report('alloc.stream', 'allocation', 14)])
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('io.write reported nothing', output)

    def test_a_campaign_that_never_comes_back_is_the_deadlock_it_looks_for(self):
        self.stand_in([report('alloc.stream', 'allocation', 14)], sleep_seconds=4)
        code, output = self.run_campaign('--timeout', '1')
        self.assertEqual(code, 1)
        self.assertIn('did not come back within 1 s', output)

    def test_a_subset_run_says_it_is_not_the_campaign(self):
        self.stand_in([report('alloc.stream', 'allocation', 14)])
        code, output = self.run_campaign('--scenario', 'alloc.stream')
        self.assertEqual(code, 0, output)
        self.assertIn('is not the campaign', output)
        self.assertIn('--gtest_filter=FaultCampaign.AnAllocationIsRefused', output)

    def test_an_unregistered_scenario_is_refused(self):
        code, output = self.run_campaign('--scenario', 'alloc.nothing')
        self.assertEqual(code, 2)
        self.assertIn('not registered', output)

    def test_a_suite_that_is_not_built_is_reported_rather_than_run(self):
        code, output = self.run_campaign()
        self.assertEqual(code, 2)
        self.assertIn('is not built', output)

    def test_the_storage_root_is_emptied_before_the_run(self):
        """A scenario that found an earlier run's file would be checking the wrong directory."""
        storage = self.root / 'storage'
        storage.mkdir(parents=True)
        (storage / 'left-behind.fits').write_text('stale', encoding='utf-8')
        self.stand_in([report('alloc.stream', 'allocation', 14), report('io.write', 'io', 5)])
        code, output = self.run_campaign()
        self.assertEqual(code, 0, output)
        self.assertFalse((storage / 'left-behind.fits').exists())


class SuiteTests(unittest.TestCase):
    """A scenario names the suite it is declared in, and the runner runs every suite the registry names."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fault-suites-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        for suite, sources in campaign.SUITES.items():
            (self.root / sources).mkdir(parents=True)
        (self.root / campaign.SUITE_SOURCES / 'Scenarios.cpp').write_text(SOURCE, encoding='utf-8')
        (self.root / campaign.SUITES['Test_ConsumerFaults'] / 'Consumer.cpp').write_text(
            'TEST(FaultCampaign, TheHistoryIsRefused)\n{\n    Sweep sweep("alloc.history", FaultKind::Allocation);\n}\n',
            encoding='utf-8')
        self.registry = self.root / 'fault_points.txt'
        self.registry.write_text(REGISTRY + 'scenario alloc.history allocation FaultCampaign.TheHistoryIsRefused '
                                 'minimum=3 suite=Test_ConsumerFaults\n', encoding='utf-8')
        self.binaries = campaign.binary_directory(self.root, 'release')
        self.binaries.mkdir(parents=True)
        original = campaign.binary_path
        campaign.binary_path = lambda directory, config, suite=campaign.SUITE: directory / (suite + STAND_IN_SUFFIX)
        self.addCleanup(setattr, campaign, 'binary_path', original)

    def test_a_scenario_reads_its_suite(self):
        scenarios = campaign.read_registry(self.registry)
        self.assertEqual([scenario.suite for scenario in scenarios],
                         [campaign.SUITE, campaign.SUITE, 'Test_ConsumerFaults'])

    def test_an_unknown_suite_is_refused(self):
        self.registry.write_text('scenario a.b io FaultCampaign.X minimum=1 suite=Test_Nothing\n', encoding='utf-8')
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.registry)

    def test_a_registry_that_agrees_with_both_suites_passes(self):
        stream = io.StringIO()
        self.assertEqual(campaign.check(self.root, self.registry, stream), 0, stream.getvalue())

    def test_a_scenario_registered_in_another_suite_than_it_is_declared_in_fails(self):
        self.registry.write_text(REGISTRY + 'scenario alloc.history allocation FaultCampaign.TheHistoryIsRefused '
                                 'minimum=3\n', encoding='utf-8')
        stream = io.StringIO()
        self.assertEqual(campaign.check(self.root, self.registry, stream), 1)
        self.assertIn('alloc.history is registered in Test_EngineFaults and declared in Test_ConsumerFaults',
                      stream.getvalue())

    def test_every_suite_the_registry_names_is_run(self):
        write_stand_in(self.binaries / (campaign.SUITE + STAND_IN_SUFFIX),
                       [report('alloc.stream', 'allocation', 12), report('io.write', 'io', 5)])
        write_stand_in(self.binaries / ('Test_ConsumerFaults' + STAND_IN_SUFFIX),
                       [report('alloc.history', 'allocation', 3)])
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--registry', str(self.registry),
                              '--storage-root', str(self.root / 'storage')], stream)
        self.assertEqual(code, 0, stream.getvalue())
        self.assertIn('in 2 suite(s)', stream.getvalue())

    def test_a_failing_second_suite_fails_the_campaign(self):
        write_stand_in(self.binaries / (campaign.SUITE + STAND_IN_SUFFIX),
                       [report('alloc.stream', 'allocation', 12), report('io.write', 'io', 5)])
        write_stand_in(self.binaries / ('Test_ConsumerFaults' + STAND_IN_SUFFIX),
                       [report('alloc.history', 'allocation', 3)], exit_code=1)
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--registry', str(self.registry),
                              '--storage-root', str(self.root / 'storage')], stream)
        self.assertEqual(code, 1)

    def test_a_suite_that_is_not_built_is_reported(self):
        write_stand_in(self.binaries / (campaign.SUITE + STAND_IN_SUFFIX),
                       [report('alloc.stream', 'allocation', 12), report('io.write', 'io', 5)])
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--registry', str(self.registry),
                              '--storage-root', str(self.root / 'storage')], stream)
        self.assertEqual(code, 2)
        self.assertIn('Test_ConsumerFaults is not built', stream.getvalue())


class BinaryPathTests(unittest.TestCase):
    """How the suite's executable is spelled, which the run cases stand aside to use a stand-in."""

    def test_a_release_suite_has_no_suffix(self):
        path = campaign.binary_path(pathlib.Path('bin'), 'release')
        self.assertEqual(path.stem, campaign.SUITE)
        self.assertEqual(path.suffix, '.exe' if os.name == 'nt' else '')

    def test_a_debug_suite_carries_the_debug_suffix(self):
        path = campaign.binary_path(pathlib.Path('bin'), 'debug')
        self.assertEqual(path.stem, campaign.SUITE + '_d')

    def test_the_directory_follows_the_toolchain(self):
        directory = campaign.binary_directory(pathlib.Path('repo'), 'release')
        self.assertEqual(directory.parts[-2:], ('msvc' if os.name == 'nt' else 'clang', 'release'))


class RepositoryTests(unittest.TestCase):
    """The repository's own campaign, because a runner that only passes on synthetic input proves nothing."""

    def test_the_registry_reads(self):
        scenarios = campaign.read_registry()
        self.assertGreaterEqual(len(scenarios), 13)

    def test_the_registry_and_the_suite_agree(self):
        self.assertEqual(campaign.check(stream=io.StringIO()), 0)

    def test_every_kind_is_covered(self):
        """Allocation, I/O, external and cancellation: a campaign missing one covers less."""
        kinds = {scenario.kind for scenario in campaign.read_registry()}
        self.assertEqual(kinds, {'allocation', 'cancellation', 'io', 'external'})

    def test_every_registered_scenario_names_a_distinct_case(self):
        tests = [scenario.test for scenario in campaign.read_registry()]
        self.assertEqual(len(tests), len(set(tests)))


if __name__ == '__main__':
    unittest.main(verbosity=2)

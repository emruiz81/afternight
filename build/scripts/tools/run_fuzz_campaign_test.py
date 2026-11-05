# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for run_fuzz_campaign.

Run with:
    python build/scripts/tools/run_fuzz_campaign_test.py

Most cases write a throwaway registry and a throwaway generated tree, so the suite needs nothing from
the real repository. The last few read the repository's own registry, because a runner that only ever
agrees with a synthetic registry proves nothing about the campaign that is actually run.

The target-running half is covered with a stand-in executable rather than a real fuzz target: what
this module decides is which targets run, with which budget, in which storage, and what a nonzero
exit means - none of which needs a real fuzzer, and all of which would be untestable behind one.
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

import run_fuzz_campaign as campaign  # noqa: E402


REGISTRY = ('# a comment\n'
            '\n'
            'Fuzz_EngineDescriptor   iterations=100  seconds=5\n'
            'Fuzz_EngineCodec        iterations=10   seconds=9\n')


class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fuzz-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)

    def write(self, text):
        path = self.root / 'fuzz_targets.txt'
        path.write_text(text, encoding='utf-8')
        return path

    def test_a_consumer_target_is_a_fuzz_target(self):
        targets = campaign.read_registry(self.write(REGISTRY + 'Fuzz_ConsumerProtocol iterations=10 seconds=5\n'))
        self.assertEqual(targets[-1].name, 'Fuzz_ConsumerProtocol')

    def test_a_name_of_neither_family_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('Fuzz_Other iterations=10 seconds=5\n'))

    def test_reads_names_and_budgets_in_order(self):
        targets = campaign.read_registry(self.write(REGISTRY))
        self.assertEqual([target.name for target in targets], ['Fuzz_EngineDescriptor', 'Fuzz_EngineCodec'])
        self.assertEqual(targets[0].iterations, 100)
        self.assertEqual(targets[1].seconds, 9)

    def test_an_empty_registry_is_refused(self):
        """A campaign that runs nothing must not be able to report a pass."""
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('# nothing here\n'))

    def test_a_name_that_is_not_a_fuzz_target_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('Test_EngineCore iterations=1 seconds=1\n'))

    def test_a_duplicate_name_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write(REGISTRY + 'Fuzz_EngineCodec iterations=1 seconds=1\n'))

    def test_a_missing_budget_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('Fuzz_EngineCodec iterations=10\n'))

    def test_a_budget_of_nothing_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('Fuzz_EngineCodec iterations=0 seconds=1\n'))

    def test_an_unknown_field_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('Fuzz_EngineCodec iterations=1 seconds=1 rounds=4\n'))

    def test_a_budget_that_is_not_a_number_is_refused(self):
        with self.assertRaises(campaign.RegistryError):
            campaign.read_registry(self.write('Fuzz_EngineCodec iterations=many seconds=1\n'))

    def test_scaling_never_empties_a_budget(self):
        target = campaign.Target('Fuzz_EngineCodec', 10, 10).scaled(0.0001)
        self.assertEqual((target.iterations, target.seconds), (1, 1))


class CheckTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fuzz-check-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.registry = self.root / 'fuzz_targets.txt'
        self.registry.write_text(REGISTRY, encoding='utf-8')
        self.tests = self.root / 'build' / 'vs2026-msvc' / 'tests'
        self.tests.mkdir(parents=True)

    def generate(self, *names):
        for name in names:
            (self.tests / (name + '.vcxproj')).write_text('<Project/>', encoding='utf-8')

    def run_check(self):
        stream = io.StringIO()
        code = campaign.check(self.root, self.registry, stream)
        return code, stream.getvalue()

    def test_a_tree_that_defines_exactly_the_registered_targets_passes(self):
        self.generate('Fuzz_EngineDescriptor', 'Fuzz_EngineCodec')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_a_registered_target_no_project_defines_fails(self):
        self.generate('Fuzz_EngineDescriptor')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Fuzz_EngineCodec is registered', output)

    def test_a_generated_target_the_registry_does_not_run_fails(self):
        """A target nobody runs looks like coverage, which is worse than a target that is missing."""
        self.generate('Fuzz_EngineDescriptor', 'Fuzz_EngineCodec', 'Fuzz_EngineMetadata')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Fuzz_EngineMetadata is generated', output)

    def test_the_linux_tree_is_read_where_its_generator_writes_test_projects(self):
        """The GNU Make generator writes the test projects one directory below the workspace root."""
        linux = self.root / 'build' / 'make-linux' / 'tests'
        linux.mkdir(parents=True)
        for name in ('Fuzz_EngineDescriptor', 'Fuzz_EngineCodec'):
            (linux / (name + '.make')).write_text('', encoding='utf-8')
        shutil.rmtree(self.root / 'build' / 'vs2026-msvc')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_other_projects_in_the_tree_are_not_fuzz_targets(self):
        self.generate('Fuzz_EngineDescriptor', 'Fuzz_EngineCodec')
        (self.tests / 'Test_EngineCore.vcxproj').write_text('<Project/>', encoding='utf-8')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_an_ungenerated_repository_checks_the_registry_alone(self):
        shutil.rmtree(self.root / 'build')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('no generated test workspace', output)

    def test_a_tree_with_no_fuzz_project_at_all_fails(self):
        """The opposite of an ungenerated repository, and it must not be mistaken for one."""
        (self.tests / 'Test_EngineCore.vcxproj').write_text('<Project/>', encoding='utf-8')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('is registered but no generated project defines it', output)

    def test_an_unusable_registry_fails(self):
        self.registry.write_text('Fuzz_EngineCodec\n', encoding='utf-8')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('not usable', output)


STAND_IN_SUFFIX = '.bat' if os.name == 'nt' else ''


def write_stand_in(path, exit_code):
    """A program that prints its arguments and exits with a stated code.

    @param path where to write it.
    @param exit_code what it exits with.
    """
    if os.name == 'nt':
        path.write_text('@echo off\r\necho stand-in %*\r\nexit /b {}\r\n'.format(exit_code), encoding='utf-8')
    else:
        path.write_text('#!/bin/sh\necho "stand-in $@"\nexit {}\n'.format(exit_code), encoding='utf-8')
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class RunTests(unittest.TestCase):
    """What running a campaign does, with stand-ins in place of the real targets."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-fuzz-run-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.registry = self.root / 'fuzz_targets.txt'
        self.registry.write_text(REGISTRY, encoding='utf-8')
        self.binaries = campaign.binary_directory(self.root, 'release')
        self.binaries.mkdir(parents=True)
        # Windows will not execute a file named `.exe` that is not one, and a test cannot produce a
        # real executable. What these cases are about is which targets run, with which budget and
        # what a nonzero exit means, so the spelling of the path is stood aside here and checked on
        # its own in BinaryPathTests.
        original = campaign.binary_path
        campaign.binary_path = lambda directory, name, config: directory / (name + STAND_IN_SUFFIX)
        self.addCleanup(setattr, campaign, 'binary_path', original)

    def stand_in(self, name, exit_code=0):
        write_stand_in(self.binaries / (name + STAND_IN_SUFFIX), exit_code)

    def run_campaign(self, *extra):
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--registry', str(self.registry),
                              '--storage-root', str(self.root / 'storage'),
                              '--artifacts', str(self.root / 'artifacts')] + list(extra), stream)
        return code, stream.getvalue()

    def test_every_target_running_passes(self):
        self.stand_in('Fuzz_EngineDescriptor')
        self.stand_in('Fuzz_EngineCodec')
        code, output = self.run_campaign()
        self.assertEqual(code, 0, output)
        self.assertIn('evaluated its whole budget', output)

    def test_a_target_that_did_not_come_back_fails_the_campaign(self):
        self.stand_in('Fuzz_EngineDescriptor')
        self.stand_in('Fuzz_EngineCodec', exit_code=3)
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('Fuzz_EngineCodec did not come back: exit 3', output)

    def test_the_budget_reaches_the_target(self):
        self.stand_in('Fuzz_EngineDescriptor')
        self.stand_in('Fuzz_EngineCodec')
        code, output = self.run_campaign('--seed', '7')
        self.assertEqual(code, 0, output)
        self.assertIn('--iterations 100', output)
        self.assertIn('--max-seconds 5', output)
        self.assertIn('--seed 7', output)

    def test_a_scaled_run_says_it_is_not_the_campaign(self):
        self.stand_in('Fuzz_EngineDescriptor')
        self.stand_in('Fuzz_EngineCodec')
        code, output = self.run_campaign('--scale', '0.01')
        self.assertEqual(code, 0, output)
        self.assertIn('is not the campaign', output)
        self.assertIn('--iterations 1', output)

    def test_a_missing_binary_fails_the_campaign(self):
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('MISSING', output)

    def test_an_unregistered_target_is_refused(self):
        code, output = self.run_campaign('--target', 'Fuzz_EngineNothing')
        self.assertEqual(code, 2)
        self.assertIn('not registered', output)


def write_libfuzzer_stand_in(path, lines, exit_code=0):
    """A program that says it was linked with libFuzzer, prints what a libFuzzer run prints and exits.

    @param path where to write it.
    @param lines what it prints, one line each.
    @param exit_code what it exits with.
    """
    marker = campaign.LIBFUZZER_ENTRY.decode('ascii')
    if os.name == 'nt':
        body = ''.join('echo {}\r\n'.format(line) for line in lines)
        path.write_text('@echo off\r\nrem {}\r\necho stand-in %*\r\n{}exit /b {}\r\n'.format(marker, body, exit_code),
                        encoding='utf-8')
    else:
        body = ''.join("echo '{}'\n".format(line) for line in lines)
        path.write_text('#!/bin/sh\n# {}\necho "stand-in $@"\n{}exit {}\n'.format(marker, body, exit_code),
                        encoding='utf-8')
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


SEEING_RUN = ('INFO: Loaded 3 modules   (5123 inline 8-bit counters): 5123 [0x1, 0x2), ',
              'stat::number_of_executed_units: 81234',
              'stat::new_units_added: 214')
BLIND_RUN = ('INFO: Loaded 1 modules   (39 inline 8-bit counters): 39 [0x1, 0x2), ',
             'WARNING: no interesting inputs were found so far. Is the code instrumented for coverage?',
             'stat::number_of_executed_units: 21107782',
             'stat::new_units_added: 0')


class LibFuzzerRunTests(unittest.TestCase):
    """The instrumented half: what the runner refuses before running and what it reads off each run.

    The defect these hold is a campaign that ran blind: the targets were linked with libFuzzer and the libraries they
    hand an input to carried no coverage counter, so every target kept a corpus of one byte and the run read clean.
    """

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-libfuzzer-run-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.registry = self.root / 'fuzz_targets.txt'
        self.registry.write_text(REGISTRY, encoding='utf-8')
        self.binaries = campaign.binary_directory(self.root, 'release')
        self.binaries.mkdir(parents=True)
        self.corpus = self.root / 'corpus'
        for name in ('Fuzz_EngineDescriptor', 'Fuzz_EngineCodec'):
            (self.corpus / name).mkdir(parents=True)
            (self.corpus / name / 'seed-0').write_bytes(b'seed')
        original = campaign.binary_path
        campaign.binary_path = lambda directory, name, config: directory / (name + STAND_IN_SUFFIX)
        self.addCleanup(setattr, campaign, 'binary_path', original)

    def library(self, name, instrumented=True):
        """One shared library of the product beside the targets, with or without the generation's markers."""
        spelled = (name + '.dll') if os.name == 'nt' else ('lib' + name + '.so')
        markers = b''.join(campaign.COVERAGE_MARKERS) if instrumented else b'__asan_init'
        (self.binaries / spelled).write_bytes(b'\x7fELF' + markers + b'\0')

    def target(self, name, lines=SEEING_RUN, exit_code=0):
        write_libfuzzer_stand_in(self.binaries / (name + STAND_IN_SUFFIX), lines, exit_code)

    def run_campaign(self, *extra):
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--registry', str(self.registry),
                              '--storage-root', str(self.root / 'storage'),
                              '--artifacts', str(self.root / 'artifacts'),
                              '--libfuzzer', '--corpus', str(self.corpus)] + list(extra), stream)
        return code, stream.getvalue()

    def test_targets_that_see_their_libraries_pass_and_report_what_they_found(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec')
        code, output = self.run_campaign()
        self.assertEqual(code, 0, output)
        self.assertIn('3 modules', output)
        self.assertIn('214 new', output)

    def test_a_library_without_coverage_counters_is_refused_before_anything_runs(self):
        self.library('AfterNightEngine', instrumented=False)
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec')
        code, output = self.run_campaign()
        self.assertEqual(code, 2, output)
        self.assertIn('carries no coverage instrumentation', output)
        self.assertNotIn('stand-in', output)

    def test_no_library_beside_the_targets_is_refused(self):
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec')
        code, output = self.run_campaign()
        self.assertEqual(code, 2, output)
        self.assertIn('no library of this product', output)

    def test_a_target_the_standalone_driver_was_built_into_is_refused(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        write_stand_in(self.binaries / ('Fuzz_EngineCodec' + STAND_IN_SUFFIX), 0)
        code, output = self.run_campaign()
        self.assertEqual(code, 2, output)
        self.assertIn('Fuzz_EngineCodec is not linked with libFuzzer', output)

    def test_a_run_that_says_it_is_blind_fails(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec', lines=BLIND_RUN)
        code, output = self.run_campaign()
        self.assertEqual(code, 1, output)
        self.assertIn('Fuzz_EngineCodec ran blind', output)

    def test_a_run_whose_libraries_reported_no_counters_fails(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec', lines=('INFO: Loaded 1 modules   (39 inline 8-bit counters): 39 [0x1, 0x2), ',
                                               'stat::new_units_added: 3'))
        code, output = self.run_campaign()
        self.assertEqual(code, 1, output)
        self.assertIn('Fuzz_EngineCodec ran blind', output)

    def test_a_crash_fails_the_campaign_and_names_where_its_input_is(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec', exit_code=77)
        code, output = self.run_campaign()
        self.assertEqual(code, 1, output)
        self.assertIn('Fuzz_EngineCodec did not come back: exit 77', output)

    def test_a_targets_own_output_goes_to_its_log_and_not_into_the_report(self):
        """A library that warns on every input fills gigabytes in a target's fifteen minutes. A runner that held a
        target's whole output to read its figures from would pass that peak to the next target it started, and
        libFuzzer's resident limit would stop that one before its first input."""
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        chatter = 'Warning: Directory Thumbnail, entry 0x0103 has unknown Exif (TIFF) type 99; setting type size 1.'
        self.target('Fuzz_EngineCodec', lines=(chatter,) * 3 + SEEING_RUN)
        code, output = self.run_campaign()
        self.assertEqual(code, 0, output)
        self.assertNotIn(chatter, output)
        self.assertIn('214 new', output)
        log = self.root / 'artifacts' / 'Fuzz_EngineCodec' / 'libfuzzer.log'
        self.assertEqual(log.read_text(encoding='utf-8').count(chatter), 3)
        self.assertIn(str(log), output)

    def test_a_crash_reports_the_end_of_what_the_target_printed(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec', lines=('==7==ERROR: AddressSanitizer: heap-use-after-free on address 0x1',
                                               'SUMMARY: AddressSanitizer: heap-use-after-free'), exit_code=1)
        code, output = self.run_campaign()
        self.assertEqual(code, 1, output)
        self.assertIn('heap-use-after-free on address 0x1', output)

    def test_the_budget_and_the_seeds_reach_libfuzzer(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec')
        code, output = self.run_campaign('--libfuzzer-seconds', '300', '--seed', '7')
        self.assertEqual(code, 0, output)
        self.assertIn('-max_total_time=300', output)
        self.assertIn('-seed=7', output)
        self.assertIn(str(self.corpus / 'Fuzz_EngineCodec'), output)

    def test_a_target_with_no_seeds_is_refused(self):
        self.library('AfterNightEngine')
        self.target('Fuzz_EngineDescriptor')
        self.target('Fuzz_EngineCodec')
        shutil.rmtree(self.corpus / 'Fuzz_EngineCodec')
        code, output = self.run_campaign()
        self.assertEqual(code, 2, output)
        self.assertIn('no seeds for Fuzz_EngineCodec', output)

    def test_seeds_are_required(self):
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--registry', str(self.registry), '--libfuzzer'], stream)
        self.assertEqual(code, 2)
        self.assertIn('--corpus', stream.getvalue())


class BinaryPathTests(unittest.TestCase):
    """How a target's executable is spelled, which the run cases stand aside to use a stand-in."""

    def test_a_release_target_has_no_suffix(self):
        path = campaign.binary_path(pathlib.Path('bin'), 'Fuzz_EngineCodec', 'release')
        self.assertEqual(path.stem, 'Fuzz_EngineCodec')
        self.assertEqual(path.suffix, '.exe' if os.name == 'nt' else '')

    def test_a_debug_target_carries_the_debug_suffix(self):
        path = campaign.binary_path(pathlib.Path('bin'), 'Fuzz_EngineCodec', 'debug')
        self.assertEqual(path.stem, 'Fuzz_EngineCodec_d')

    def test_the_directory_follows_the_toolchain(self):
        directory = campaign.binary_directory(pathlib.Path('repo'), 'release')
        self.assertEqual(directory.parts[-2:], ('msvc' if os.name == 'nt' else 'clang', 'release'))


class RepositoryTests(unittest.TestCase):
    """The repository's own registry, because a runner that only passes on synthetic input proves nothing."""

    def test_the_registry_reads(self):
        targets = campaign.read_registry()
        self.assertEqual(len(targets), 8)
        self.assertEqual({target.name for target in targets},
                         {'Fuzz_EngineDescriptor', 'Fuzz_EngineConversion', 'Fuzz_EnginePixelMath', 'Fuzz_EngineMetadata',
                          'Fuzz_EngineCodec', 'Fuzz_EngineSequence', 'Fuzz_ConsumerDocuments', 'Fuzz_ConsumerProtocol'})

    def test_every_registered_target_has_a_source_directory(self):
        for target in campaign.read_registry():
            directory = campaign.REPO_ROOT / 'src' / 'AfterNightTests' / target.name
            self.assertTrue(directory.is_dir(), target.name)
            self.assertTrue(any(directory.glob('*.cpp')), target.name)

    def test_the_registry_covers_the_six_subjects_the_campaign_is_scoped_to(self):
        """Descriptors, conversion, the expression front end, metadata, codecs and sequences."""
        names = {target.name for target in campaign.read_registry()}
        for subject in ('Descriptor', 'Conversion', 'PixelMath', 'Metadata', 'Codec', 'Sequence'):
            self.assertIn('Fuzz_Engine' + subject, names)


if __name__ == '__main__':
    unittest.main(verbosity=2)

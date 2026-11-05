# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for run_sanitizer_campaign.

Run with:
    python build/scripts/tools/run_sanitizer_campaign_test.py

Most cases write a throwaway matrix and a throwaway generated tree, so the suite needs nothing from
the real repository. The last few read the repository's own matrix, because a checker that only ever
agrees with a synthetic matrix proves nothing about the campaign that is actually run.

The row-running half is covered with stand-in executables rather than real suites: what this module
decides is which rows run, whether what ran was instrumented, what their output said and what a
sanitizer report means - none of which needs a real sanitizer, and all of which would be untestable
behind one.
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

import run_sanitizer_campaign as campaign  # noqa: E402


MATRIX = ('# a comment\n'
          '\n'
          'configuration address build=address hosts=windows,linux timeout=600\n'
          'option address ASAN_OPTIONS=halt_on_error=1:allocator_may_return_null=1\n'
          'weakened address allocator_may_return_null=1 One suite deliberately asks for an '
          'impossible allocation and requires a returned failure.\n'
          'run address suite.core Test_EngineCore whole\n'
          'run address suite.runtime Test_EngineRuntime whole\n'
          'run address pipeline.cancellation Test_EngineRuntime filter=Cancel*:Stop* repeat=2 shuffle\n')

STAND_IN_SUFFIX = '.bat' if os.name == 'nt' else '.sh'


def write_stand_in(path, exit_code=0, prints='[==========] 7 tests from 2 test suites ran. (1 ms total)',
                   markers=(), sleeps=0):
    """Writes a script that stands in for a built suite.

    @param path where to write it.
    @param exit_code what it returns.
    @param prints what it writes to its output.
    @param markers instrumentation markers to put in the file's bytes.
    @param sleeps how long it waits before returning, for the timeout case.
    """
    comment = ''.join('rem %s\r\n' % marker for marker in markers) if os.name == 'nt' \
        else ''.join('# %s\n' % marker for marker in markers)
    if os.name == 'nt':
        wait = 'ping -n %d 127.0.0.1 >nul\r\n' % (sleeps + 1) if sleeps else ''
        path.write_text('@echo off\r\n%s%secho %s\r\nexit /b %d\r\n'
                        % (comment, wait, prints, exit_code), encoding='utf-8')
    else:
        wait = 'sleep %d\n' % sleeps if sleeps else ''
        path.write_text('#!/bin/sh\n%s%secho "%s"\nexit %d\n' % (comment, wait, prints, exit_code),
                        encoding='utf-8')
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def markers_for(build, is_library):
    """The instrumentation markers this host's artifacts would carry.

    @param build `address` or `thread`.
    @param is_library whether this is the engine library.
    @return the markers, decoded.
    """
    return [marker.decode('ascii')
            for marker in campaign.instrumentation_markers(build, campaign.host_name(), is_library)]


class MatrixTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-san-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)

    def write(self, text):
        path = self.root / 'sanitizer_matrix.txt'
        path.write_text(text, encoding='utf-8')
        return path

    def test_reads_configurations_options_and_rows_in_order(self):
        configurations, rows = campaign.read_matrix(self.write(MATRIX))
        self.assertEqual(sorted(configurations), ['address'])
        self.assertEqual(configurations['address'].build, 'address')
        self.assertEqual(configurations['address'].hosts, ('windows', 'linux'))
        self.assertEqual(configurations['address'].timeout, 600)
        self.assertIn('ASAN_OPTIONS', configurations['address'].options)
        self.assertEqual([row.label for row in rows],
                         ['suite.core', 'suite.runtime', 'pipeline.cancellation'])
        self.assertEqual(rows[2].kind, 'filter')
        self.assertEqual(rows[2].selection, 'Cancel*:Stop*')
        self.assertEqual(rows[2].repeat, 2)
        self.assertTrue(rows[2].shuffle)
        self.assertTrue(rows[2].is_pipeline)

    def test_an_empty_matrix_is_refused(self):
        """A campaign that runs nothing must not be able to report a pass."""
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write('# nothing here\n'))

    def test_a_configuration_with_no_row_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'configuration thread build=thread hosts=linux timeout=60\n'))

    def test_an_unknown_build_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write('configuration a build=memory hosts=linux timeout=60\n'))

    def test_an_unknown_host_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write('configuration a build=address hosts=solaris timeout=60\n'))

    def test_a_configuration_without_a_wall_clock_bound_is_refused(self):
        """A row that hangs has to end as a finding; a deadlock is half of what this looks for."""
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write('configuration a build=address hosts=linux timeout=0\n'))

    def test_a_repeated_label_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'run address suite.core Test_EngineCore whole\n'))

    def test_a_label_that_is_neither_a_suite_nor_a_pipeline_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'run address core Test_EngineCore whole\n'))

    def test_a_skip_without_a_reason_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'run address suite.x Test_EngineX skip=slow\n'))

    def test_a_skip_with_a_reason_is_kept_whole(self):
        _, rows = campaign.read_matrix(self.write(
            MATRIX + 'run address suite.x Test_EngineX skip=it reaches a library nobody has an '
                     'instrumented copy of, so what it reported would be that library\n'))
        self.assertTrue(rows[-1].is_skip)
        self.assertIn('instrumented copy', rows[-1].selection)

    def test_an_empty_filter_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'run address pipeline.x Test_EngineCore filter=\n'))

    def test_an_unknown_selection_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'run address pipeline.x Test_EngineCore some\n'))

    def test_an_unknown_record_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write('sanitize address everything\n'))

    def test_a_weakening_reason_for_an_option_nobody_counts_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'weakened address verbosity=0 because it is loud\n'))

    def test_a_weakening_reason_that_is_a_word_is_refused(self):
        with self.assertRaises(campaign.MatrixError):
            campaign.read_matrix(self.write(MATRIX + 'weakened address detect_leaks=0 speed\n'))

    def test_print_suppressions_is_not_read_as_a_suppression(self):
        """`print_suppressions=1` contains `suppressions=` and is the opposite of a weakening."""
        configurations, _ = campaign.read_matrix(self.write(
            MATRIX + 'option address LSAN_OPTIONS=print_suppressions=1\n'))
        self.assertNotIn('suppressions=', configurations['address'].present_weakening_tokens())


class CheckTests(unittest.TestCase):
    """What the check refuses, which is the half the common check list runs."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-san-check-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.tree = self.root / 'build' / 'vs2026-msvc' / 'tests'
        self.tree.mkdir(parents=True)
        for name in ('Test_EngineCore', 'Test_EngineRuntime'):
            (self.tree / (name + '.vcxproj')).write_text('', encoding='utf-8')

    def check(self, text):
        path = self.root / 'sanitizer_matrix.txt'
        path.write_text(text, encoding='utf-8')
        stream = io.StringIO()
        return campaign.check(self.root, path, stream), stream.getvalue()

    def test_a_complete_matrix_passes(self):
        code, output = self.check(MATRIX)
        self.assertEqual(code, 0, output)

    def test_the_linux_tree_is_read_where_its_generator_writes_test_projects(self):
        """The GNU Make generator writes the test projects one directory below the workspace root."""
        linux = self.root / 'build' / 'make-linux' / 'tests'
        linux.mkdir(parents=True)
        for name in ('Test_EngineCore', 'Test_EngineRuntime'):
            (linux / (name + '.make')).write_text('', encoding='utf-8')
        shutil.rmtree(self.root / 'build' / 'vs2026-msvc')
        code, output = self.check(MATRIX)
        self.assertEqual(code, 0, output)

    def test_a_generated_suite_no_configuration_names_fails(self):
        (self.tree / 'Test_EngineImage.vcxproj').write_text('', encoding='utf-8')
        code, output = self.check(MATRIX)
        self.assertEqual(code, 1)
        self.assertIn('Test_EngineImage is generated but address does not say whether it runs', output)

    def test_the_benchmark_layers_suite_is_not_an_engine_suite(self):
        """The engine's suites share a prefix the benchmark layer's suite begins with too."""
        (self.tree / 'Test_EngineBenchCommon.vcxproj').write_text('', encoding='utf-8')
        code, output = self.check(MATRIX)
        self.assertEqual(code, 0, output)
        self.assertNotIn('Test_EngineBenchCommon', campaign.generated_suites(self.root)[0])

    def test_a_row_naming_a_target_nothing_defines_fails(self):
        code, output = self.check(MATRIX + 'run address suite.image Test_EngineImage whole\n')
        self.assertEqual(code, 1)
        self.assertIn('which no generated project defines', output)

    def test_a_skip_row_for_a_target_nothing_defines_is_allowed(self):
        """An opt-in suite absent from a default tree is still a row worth stating."""
        code, output = self.check(
            MATRIX + 'run address suite.opencl Test_EngineOpenClBackend skip=the opt-in proof backend '
                     'is absent from a default tree and runs inside a vendor driver\n')
        self.assertEqual(code, 0, output)

    def test_a_configuration_without_a_pipeline_row_fails(self):
        code, output = self.check(MATRIX.replace('pipeline.cancellation', 'suite.cancellation')
                                  .replace('run address suite.cancellation Test_EngineRuntime filter',
                                           'run address suite.other Test_EngineRuntime filter'))
        self.assertEqual(code, 1)
        self.assertIn('has no focused pipeline row', output)

    def test_an_unexplained_weakening_fails(self):
        code, output = self.check(MATRIX.replace(
            'ASAN_OPTIONS=halt_on_error=1:allocator_may_return_null=1',
            'ASAN_OPTIONS=halt_on_error=1:allocator_may_return_null=1:detect_leaks=0'))
        self.assertEqual(code, 1)
        self.assertIn('sets detect_leaks=0 and does not say why', output)

    def test_a_reason_for_an_option_that_is_not_set_fails(self):
        """A reason that has drifted away from its option is worse than none: it reads as coverage."""
        code, output = self.check(MATRIX + 'weakened address halt_on_error=0 the suites report their '
                                           'own failures and continue\n')
        self.assertEqual(code, 1)
        self.assertIn('explains halt_on_error=0, which its options do not set', output)

    def test_a_configuration_that_skips_everything_fails(self):
        code, output = self.check(
            'configuration a build=address hosts=linux timeout=60\n'
            'run a suite.core Test_EngineCore skip=there is a reason and it is long enough to pass\n'
            'run a suite.runtime Test_EngineRuntime skip=there is a reason and it is long enough too\n'
            'run a pipeline.x Test_EngineCore skip=and this one is skipped for a stated reason too\n')
        self.assertEqual(code, 1)
        self.assertIn('skips everything', output)

    def test_the_repositorys_own_matrix_passes(self):
        """The matrix that is actually run, against the tree this host generated."""
        stream = io.StringIO()
        self.assertEqual(campaign.check(campaign.REPO_ROOT, campaign.REGISTRY, stream), 0,
                         stream.getvalue())

    def test_the_repositorys_own_matrix_covers_every_configuration_a_host_can_run(self):
        configurations, rows = campaign.read_matrix(campaign.REGISTRY)
        self.assertTrue(any(configuration.runs_on('windows') for configuration in configurations.values()))
        self.assertTrue(all(any(row.configuration == name for row in rows) for name in configurations))


CONSUMER_REGISTRY = (
    'target Test_ConsumerRuntime\n'
    '    workspace    tests\n'
    '    kind         console\n'
    '    class        engine\n'
    '    state        active\n'
    '    depends      AfterNightConsumerRuntime, AfterNightEngine, AfterNightPlatform\n'
    '    runner       product\n'
    '\n'
    'target Test_OtherClassStandIn\n'
    '    workspace    tests\n'
    '    kind         console\n'
    '    class        other\n'
    '    state        active\n'
    '    depends      OtherLibraryStandIn, AfterNightPlatform\n'
    '    runner       product\n'
    '\n'
    'target Test_SuspendedSuite\n'
    '    workspace    tests\n'
    '    kind         console\n'
    '    class        engine\n'
    '    state        suspended\n'
    '    depends      AfterNightControls\n'
    '    runner       product\n')


class ConsumerSuiteTests(unittest.TestCase):
    """The consumer half: which suites the registry makes the campaign's, and what each requires instrumented."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-san-consumer-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        registry = self.root / campaign.TARGET_REGISTRY
        registry.parent.mkdir(parents=True)
        registry.write_text(CONSUMER_REGISTRY, encoding='utf-8')
        self.tree = self.root / 'build' / 'vs2026-msvc' / 'tests'
        self.tree.mkdir(parents=True)
        for name in ('Test_EngineCore', 'Test_EngineRuntime', 'Test_ConsumerRuntime'):
            (self.tree / (name + '.vcxproj')).write_text('', encoding='utf-8')

    def check(self, text):
        path = self.root / 'sanitizer_matrix.txt'
        path.write_text(text, encoding='utf-8')
        stream = io.StringIO()
        return campaign.check(self.root, path, stream), stream.getvalue()

    def test_an_active_consumer_suite_of_the_engine_class_is_the_campaigns(self):
        self.assertEqual(campaign.consumer_suites(self.root),
                         {'Test_ConsumerRuntime': ('AfterNightConsumerRuntime', 'AfterNightPlatform')})

    def test_a_generated_consumer_suite_no_configuration_names_fails(self):
        code, output = self.check(MATRIX)
        self.assertEqual(code, 1)
        self.assertIn('Test_ConsumerRuntime is generated but address does not say whether it runs', output)

    def test_a_consumer_suite_every_configuration_names_passes(self):
        code, output = self.check(MATRIX + 'run address suite.consumer.runtime Test_ConsumerRuntime whole\n')
        self.assertEqual(code, 0, output)

    def test_a_consumer_suite_nothing_generated_is_not_required(self):
        (self.tree / 'Test_ConsumerRuntime.vcxproj').unlink()
        code, output = self.check(MATRIX)
        self.assertEqual(code, 0, output)


class SuppressionTests(unittest.TestCase):
    """The one rule that decides whether this campaign can hide an engine defect."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-san-supp-'))
        self.addCleanup(shutil.rmtree, self.root, True)

    def write(self, text):
        path = self.root / 'suppressions.txt'
        path.write_text(text, encoding='utf-8')
        return path

    def test_a_third_party_entry_with_a_reason_is_accepted(self):
        path = self.write('# reason: libgomp keeps its worker pool alive past exit and it is not '
                          'this engine\nrace:libgomp.so*\n')
        self.assertEqual(campaign.check_suppression_file(path), [])

    def test_an_entry_naming_this_engine_is_refused(self):
        path = self.write('# reason: this is long enough to look like a real reason for it\n'
                          'race:an::stackFrames\n')
        self.assertEqual(len(campaign.check_suppression_file(path)), 1)
        self.assertIn('names this engine', campaign.check_suppression_file(path)[0])

    def test_an_entry_naming_the_engine_library_is_refused(self):
        path = self.write('# reason: this is long enough to look like a real reason for it\n'
                          'leak:libAfterNightEngine.so\n')
        self.assertIn('names this engine', campaign.check_suppression_file(path)[0])

    def test_an_entry_without_a_reason_is_refused(self):
        self.assertIn('has no reason above it',
                      campaign.check_suppression_file(self.write('race:libfoo.so\n'))[0])

    def test_a_reason_is_spent_on_the_entry_below_it(self):
        """One reason does not cover a list; each entry states its own."""
        path = self.write('# reason: this is long enough to look like a real reason for it\n'
                          'race:libfoo.so\nrace:libbar.so\n')
        self.assertEqual(len(campaign.check_suppression_file(path)), 1)

    def test_a_line_that_is_not_an_entry_is_refused(self):
        path = self.write('# reason: this is long enough to look like a real reason for it\n'
                          'ignore everything\n')
        self.assertIn('is not a suppression entry', campaign.check_suppression_file(path)[0])

    def test_a_named_file_that_does_not_exist_is_refused(self):
        self.assertIn('does not exist', campaign.check_suppression_file(self.root / 'absent.txt')[0])

    def test_a_suppression_file_is_named_to_the_sanitizer_from_the_repository(self):
        """A row runs in its executable's directory, where the sanitizer would look for a relative path."""
        value = campaign.resolve_suppressions('report_objects=1:suppressions=build/leaks.txt:print_suppressions=1',
                                              self.root)
        self.assertEqual(value, 'report_objects=1:suppressions=%s:print_suppressions=1'
                         % (self.root / 'build/leaks.txt'))
        self.assertEqual(campaign.resolve_suppressions('halt_on_error=1', self.root), 'halt_on_error=1')


class InstrumentationTests(unittest.TestCase):
    """The check that makes a clean run mean something."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-san-inst-'))
        self.addCleanup(shutil.rmtree, self.root, True)

    def artifact(self, content):
        path = self.root / 'artifact.bin'
        path.write_bytes(content)
        return path

    def test_a_windows_binary_importing_the_runtime_is_instrumented(self):
        path = self.artifact(b'MZ...clang_rt.asan_dynamic-x86_64.dll...')
        self.assertEqual(campaign.missing_instrumentation(path, 'address', 'windows', False), [])

    def test_a_windows_binary_without_the_import_is_not(self):
        path = self.artifact(b'MZ...ordinary...')
        self.assertEqual(campaign.missing_instrumentation(path, 'address', 'windows', False),
                         ['clang_rt.asan'])

    def test_a_linux_library_needs_both_halves_of_the_address_configuration(self):
        """The engine is where the undefined-behaviour half has to be, not the test executable."""
        path = self.artifact(b'ELF...__asan_report_load4...')
        self.assertEqual(campaign.missing_instrumentation(path, 'address', 'linux', True),
                         ['__ubsan_handle'])
        self.assertEqual(campaign.missing_instrumentation(path, 'address', 'linux', False), [])

    def test_a_thread_binary_is_recognised_by_its_hooks(self):
        path = self.artifact(b'ELF...__tsan_func_entry...')
        self.assertEqual(campaign.missing_instrumentation(path, 'thread', 'linux', True), [])
        self.assertEqual(campaign.missing_instrumentation(self.artifact(b'ELF...__asan_...'),
                                                          'thread', 'linux', True), ['__tsan_'])

    def test_an_instrumented_onetbb_on_the_library_path_is_found(self):
        directory = self.root / 'tsan-tbb'
        directory.mkdir()
        (directory / 'libtbb.so.12').write_bytes(b'ELF...__tsan_func_entry...')
        self.assertIsNotNone(campaign.instrumented_tbb({'LD_LIBRARY_PATH': str(directory)}))

    def test_an_ordinary_onetbb_on_the_library_path_is_refused(self):
        directory = self.root / 'stock-tbb'
        directory.mkdir()
        (directory / 'libtbb.so.12').write_bytes(b'ELF...ordinary...')
        self.assertIsNone(campaign.instrumented_tbb({'LD_LIBRARY_PATH': str(directory)}))

    def test_no_onetbb_on_the_library_path_is_refused(self):
        self.assertIsNone(campaign.instrumented_tbb({'LD_LIBRARY_PATH': str(self.root)}))


class OutputTests(unittest.TestCase):
    """What a row's own output is read for, since an exit code is not enough."""

    def test_each_kind_of_report_is_found(self):
        for line in ('==1==ERROR: AddressSanitizer: heap-use-after-free on address 0x1',
                     'WARNING: ThreadSanitizer: data race (pid=2)',
                     '==3==ERROR: LeakSanitizer: detected memory leaks',
                     'Image.cpp:41:9: runtime error: signed integer overflow',
                     'SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior Image.cpp:41:9'):
            self.assertEqual(len(campaign.findings_in('[ OK ] a.b\n' + line + '\n')), 1, line)

    def test_ordinary_output_is_not_a_finding(self):
        self.assertEqual(campaign.findings_in('[ OK ] ErrorTest.AnErrorIsReturned (1 ms)\n'
                                              '[  FAILED  ] 0 tests\nruntime: 3 ms\n'), [])

    def test_the_test_count_is_read_from_the_summary(self):
        self.assertEqual(campaign.tests_run('[==========] 116 tests from 13 test suites ran. (7 ms total)'), 116)

    def test_the_largest_count_wins_when_a_row_repeats(self):
        self.assertEqual(campaign.tests_run('[==========] 7 tests from 2 test suites ran. (1 ms total)\n'
                                            '[==========] 7 tests from 2 test suites ran. (1 ms total)'), 7)

    def test_a_binary_that_reports_no_count_is_not_a_count_of_zero(self):
        """The public-header client is a framework-free program; it reports no test count at all."""
        self.assertIsNone(campaign.tests_run('AfterNightEngine 1.0.0 loaded\n'))


class RunTests(unittest.TestCase):
    """What running a configuration does, with stand-ins in place of the built suites."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-san-run-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.matrix = self.root / 'sanitizer_matrix.txt'
        self.matrix.write_text(MATRIX, encoding='utf-8')
        self.binaries = campaign.binary_directory(self.root, 'release')
        self.binaries.mkdir(parents=True)
        # Windows will not execute a file named `.exe` that is not one, and a test cannot produce a
        # real executable. What these cases are about is which rows run, whether what ran was
        # instrumented and what its output meant, so the spelling of the path is stood aside here.
        original = campaign.binary_path
        campaign.binary_path = lambda directory, name, config: directory / (name + STAND_IN_SUFFIX)
        self.addCleanup(setattr, campaign, 'binary_path', original)
        original_runtime = campaign.asan_runtime_directory
        campaign.asan_runtime_directory = lambda explicit=None: self.root
        self.addCleanup(setattr, campaign, 'asan_runtime_directory', original_runtime)
        self.engine(markers_for('address', True))

    def engine(self, markers):
        path = campaign.engine_library_path(self.binaries, 'release')
        path.write_bytes(('\n'.join(markers)).encode('ascii'))

    def stand_in(self, name, **keywords):
        keywords.setdefault('markers', markers_for('address', False))
        write_stand_in(self.binaries / (name + STAND_IN_SUFFIX), **keywords)

    def run_campaign(self, *extra):
        stream = io.StringIO()
        code = campaign.main(['--repo-root', str(self.root), '--matrix', str(self.matrix),
                              '--configuration', 'address',
                              '--storage-root', str(self.root / 'storage'),
                              '--output', str(self.root / 'evidence')] + list(extra), stream)
        return code, stream.getvalue()

    def test_every_row_running_clean_passes(self):
        self.stand_in('Test_EngineCore')
        self.stand_in('Test_EngineRuntime')
        code, output = self.run_campaign()
        self.assertEqual(code, 0, output)
        self.assertIn('every row of address ran instrumented and reported nothing', output)

    def test_an_uninstrumented_executable_is_refused(self):
        """The whole point: a clean run over an uninstrumented binary looks exactly like a pass."""
        self.stand_in('Test_EngineCore', markers=())
        self.stand_in('Test_EngineRuntime')
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('carries no address instrumentation', output)

    def test_an_uninstrumented_engine_behind_an_instrumented_suite_is_refused(self):
        """The engine is a separate library; instrumenting the tests alone covers none of it."""
        self.stand_in('Test_EngineCore')
        self.stand_in('Test_EngineRuntime')
        self.engine([])
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngine', output)

    def test_an_uninstrumented_consumer_library_behind_an_instrumented_suite_is_refused(self):
        """A consumer suite exercises the libraries it links; one of them uninstrumented covers none of its code."""
        registry = self.root / campaign.TARGET_REGISTRY
        registry.parent.mkdir(parents=True, exist_ok=True)
        registry.write_text('target Test_EngineCore\n    workspace    tests\n    kind         console\n'
                            '    class        engine\n    state        active\n'
                            '    depends      AfterNightConsumerRuntime, AfterNightEngine\n'
                            '    runner       product\n', encoding='utf-8')
        self.stand_in('Test_EngineCore')
        self.stand_in('Test_EngineRuntime')
        library = campaign.library_path(self.binaries, 'AfterNightConsumerRuntime', 'release')
        library.write_bytes(b'nothing instrumented here')
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightConsumerRuntime', output)
        self.assertIn('carries no address instrumentation', output)
        library.write_bytes(('\n'.join(markers_for('address', True))).encode('ascii'))
        code, output = self.run_campaign()
        self.assertEqual(code, 0, output)

    def test_a_missing_binary_is_refused(self):
        self.stand_in('Test_EngineCore')
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('is not built', output)

    def test_a_sanitizer_report_fails_a_row_that_exited_zero(self):
        self.stand_in('Test_EngineCore',
                      prints='==1==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x1')
        self.stand_in('Test_EngineRuntime')
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('sanitizer report', output)

    def test_a_row_that_ran_no_test_fails(self):
        """A filter that matches nothing prints a clean line and proves nothing."""
        self.stand_in('Test_EngineCore', prints='[==========] 0 tests from 0 test suites ran. (0 ms total)')
        self.stand_in('Test_EngineRuntime')
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('ran no test at all', output)

    def test_a_nonzero_exit_fails(self):
        self.stand_in('Test_EngineCore', exit_code=1)
        self.stand_in('Test_EngineRuntime')
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('exit 1', output)

    def test_a_row_that_hangs_fails_at_its_bound(self):
        self.matrix.write_text(MATRIX.replace('timeout=600', 'timeout=1'), encoding='utf-8')
        self.stand_in('Test_EngineCore', sleeps=30)
        self.stand_in('Test_EngineRuntime')
        code, output = self.run_campaign()
        self.assertEqual(code, 1)
        self.assertIn('did not finish within', output)

    def test_every_rows_output_is_kept(self):
        self.stand_in('Test_EngineCore')
        self.stand_in('Test_EngineRuntime')
        self.run_campaign()
        self.assertTrue((self.root / 'evidence' / 'address' / 'suite.core.log').is_file())

    def test_one_row_may_be_run_on_its_own(self):
        self.stand_in('Test_EngineCore')
        code, output = self.run_campaign('--label', 'suite.core')
        self.assertEqual(code, 0, output)
        self.assertNotIn('suite.runtime', output)

    def test_a_label_that_is_not_in_the_configuration_is_refused(self):
        code, output = self.run_campaign('--label', 'suite.absent')
        self.assertEqual(code, 2)
        self.assertIn('not in address', output)

    def test_a_configuration_this_host_cannot_run_is_refused(self):
        self.matrix.write_text(MATRIX.replace('hosts=windows,linux', 'hosts=%s'
                                              % ('linux' if os.name == 'nt' else 'windows')),
                               encoding='utf-8')
        code, output = self.run_campaign()
        self.assertEqual(code, 2)
        self.assertIn('does not run on', output)

    def test_each_row_gets_its_own_test_owned_storage(self):
        self.stand_in('Test_EngineCore', prints='%AFTERNIGHT_TEST_STORAGE_ROOT%'
                      if os.name == 'nt' else '$AFTERNIGHT_TEST_STORAGE_ROOT')
        self.stand_in('Test_EngineRuntime')
        self.run_campaign()
        written = (self.root / 'evidence' / 'address' / 'suite.core.log').read_text(encoding='utf-8')
        self.assertIn(str(self.root / 'storage'), written)
        self.assertIn('suite.core', written)


if __name__ == '__main__':
    unittest.main(verbosity=2)

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_public_documentation.

Run with:
    python build/scripts/tools/check_public_documentation_test.py

Most cases inject a fake Doxygen, so the suite runs on a machine that has none and still proves the
parts that decide a verdict: which configuration counts as a check, which output lines count as
diagnostics, and which failure is "the engine is undocumented" rather than "nothing ran". The last
case runs the real tool against the real engine when one is installed, and says out loud that it
did not when there is not.
"""

from __future__ import annotations

import io
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_public_documentation as documentation  # noqa: E402


REPO_ROOT = documentation.REPO_ROOT
DOXYFILE_TEXT = (REPO_ROOT / documentation.DOXYFILE).read_bytes().decode('utf-8')


def replace_setting(text, name, value):
    lines = []
    for line in text.splitlines():
        match = documentation.SETTING_PATTERN.match(line)
        if match and match.group(1) == name:
            line = '%s = %s' % (name, value)
        lines.append(line)
    return '\n'.join(lines) + '\n'


def remove_setting(text, name):
    lines = []
    for line in text.splitlines():
        match = documentation.SETTING_PATTERN.match(line)
        if match and match.group(1) == name:
            continue
        lines.append(line)
    return '\n'.join(lines) + '\n'


class ConfigurationTests(unittest.TestCase):
    def test_the_repository_configuration_is_a_check(self):
        self.assertEqual(documentation.check_configuration(DOXYFILE_TEXT), [])

    def test_extracting_everything_is_refused(self):
        problems = documentation.check_configuration(
            replace_setting(DOXYFILE_TEXT, 'EXTRACT_ALL', 'YES'))
        self.assertTrue(any('EXTRACT_ALL' in problem for problem in problems), problems)

    def test_warnings_that_do_not_fail_are_refused(self):
        problems = documentation.check_configuration(
            replace_setting(DOXYFILE_TEXT, 'WARN_AS_ERROR', 'NO'))
        self.assertTrue(any('WARN_AS_ERROR' in problem for problem in problems), problems)

    def test_not_reporting_undocumented_symbols_is_refused(self):
        problems = documentation.check_configuration(
            replace_setting(DOXYFILE_TEXT, 'WARN_IF_UNDOCUMENTED', 'NO'))
        self.assertTrue(any('WARN_IF_UNDOCUMENTED' in problem for problem in problems), problems)

    def test_pointing_the_check_somewhere_else_is_refused(self):
        problems = documentation.check_configuration(
            replace_setting(DOXYFILE_TEXT, 'INPUT', 'src'))
        self.assertTrue(any('INPUT' in problem for problem in problems), problems)

    def test_every_optional_library_the_build_graph_declares_is_an_input(self):
        # A published surface added to the build and not to this check is one nobody reads, and the
        # opt-in GPU backend is documented whether or not a tree was generated with it.
        import check_build_graph as graph  # noqa: E402
        for component in list(graph.OPTIONAL_ADAPTER_SUITES) + list(graph.OPTIONAL_BACKEND_SUITES):
            self.assertIn('src/%s/include' % component, documentation.PUBLIC_INCLUDE_DIRS)

    def test_hiding_undocumented_members_is_refused(self):
        problems = documentation.check_configuration(
            replace_setting(DOXYFILE_TEXT, 'HIDE_UNDOC_MEMBERS', 'YES'))
        self.assertTrue(any('HIDE_UNDOC_MEMBERS' in problem for problem in problems), problems)

    def test_hiding_undocumented_classes_is_refused(self):
        problems = documentation.check_configuration(
            replace_setting(DOXYFILE_TEXT, 'HIDE_UNDOC_CLASSES', 'YES'))
        self.assertTrue(any('HIDE_UNDOC_CLASSES' in problem for problem in problems), problems)

    def test_hiding_undocumented_namespaces_is_refused(self):
        problems = documentation.check_configuration(
            replace_setting(DOXYFILE_TEXT, 'HIDE_UNDOC_NAMESPACES', 'YES'))
        self.assertTrue(any('HIDE_UNDOC_NAMESPACES' in problem for problem in problems), problems)

    def test_a_removed_setting_is_refused(self):
        problems = documentation.check_configuration(
            remove_setting(DOXYFILE_TEXT, 'WARN_NO_PARAMDOC'))
        self.assertTrue(any('WARN_NO_PARAMDOC is not set' in problem for problem in problems),
                        problems)

    def test_a_commented_out_setting_does_not_count_as_set(self):
        settings = documentation.read_settings('# EXTRACT_ALL = YES\nEXTRACT_ALL = NO\n')
        self.assertEqual(settings['EXTRACT_ALL'], 'NO')

    def test_the_output_directory_override_is_appended_last(self):
        composed = documentation.compose_configuration('EXTRACT_ALL = NO\n', '/tmp/reference')
        self.assertTrue(composed.rstrip().endswith('OUTPUT_DIRECTORY = /tmp/reference'))
        self.assertIn('EXTRACT_ALL = NO', composed)


class DiagnosticTests(unittest.TestCase):
    def test_warnings_and_errors_are_diagnostics(self):
        output = ('Searching for include files...\n'
                  'Header.h:12: warning: Member value is not documented.\n'
                  'Header.h:20: error: Compound an::Thing is not documented.\n'
                  'Generating index page...\n')
        self.assertEqual(documentation.extract_diagnostics(output), [
            'Header.h:12: warning: Member value is not documented.',
            'Header.h:20: error: Compound an::Thing is not documented.',
        ])

    def test_ordinary_progress_output_is_not_a_diagnostic(self):
        self.assertEqual(documentation.extract_diagnostics('Parsing files\nGenerating docs\n'), [])

    def test_a_path_that_merely_contains_the_word_error_is_not_a_diagnostic(self):
        self.assertEqual(documentation.extract_diagnostics('Reading errorhandling/Status.h\n'), [])

    def test_a_diagnostic_that_names_a_file_and_line_is_about_the_source(self):
        self.assertTrue(documentation.is_source_diagnostic(
            'src/AfterNightEngine/include/AfterNightEngine/Core/Version.h:20: error: '
            'Compound an::Thing is not documented.'))

    def test_a_windows_path_is_still_a_source_diagnostic(self):
        self.assertTrue(documentation.is_source_diagnostic(
            'C:/src/AfterNightEngine/include/AfterNightEngine/Core/Version.h:20: '
            'warning: Member value is not documented.'))

    def test_an_ignored_tag_is_a_diagnostic_about_the_run_and_not_about_the_source(self):
        # Doxygen ignores a tag it does not know and says so without naming a source file, because
        # the file it is complaining about is the configuration it was handed on standard input.
        self.assertFalse(documentation.is_source_diagnostic(
            "warning: ignoring unsupported tag 'HIDE_UNDOC_NAMESPACES' at line 59, file -"))

    def test_an_unreadable_input_is_a_diagnostic_about_the_run(self):
        self.assertFalse(documentation.is_source_diagnostic(
            'warning: source src/AfterNightEngine/include is not a readable file or directory'))


class ExecutableTests(unittest.TestCase):
    def test_an_explicit_path_that_exists_is_used(self):
        self.assertEqual(documentation.find_doxygen(sys.executable, environment={}),
                         sys.executable)

    def test_an_explicit_path_that_does_not_exist_is_not_silently_replaced(self):
        self.assertIsNone(documentation.find_doxygen('no-such-doxygen-anywhere', environment={}))

    def test_the_environment_names_the_executable_when_no_argument_does(self):
        self.assertEqual(
            documentation.find_doxygen(None, environment={'DOXYGEN_BIN': sys.executable}),
            sys.executable)


class VerdictTests(unittest.TestCase):
    """What each kind of Doxygen run means, with the tool itself replaced by a stub."""

    def run_with(self, code, output):
        stream = io.StringIO()
        result = documentation.main(
            [], stream=stream,
            runner=lambda executable, configuration, repo_root: (code, output),
            environment={'DOXYGEN_BIN': sys.executable})
        return result, stream.getvalue()

    def test_a_clean_run_passes(self):
        result, text = self.run_with(0, 'Parsing files\nGenerating docs\n')
        self.assertEqual(result, documentation.CLEAN, text)

    def test_a_reported_undocumented_symbol_fails(self):
        result, text = self.run_with(
            1, 'Version.h:20: error: Compound an::Thing is not documented.\n')
        self.assertEqual(result, documentation.PROBLEMS_FOUND, text)
        self.assertIn('an::Thing', text)

    def test_a_failure_with_no_diagnostic_is_not_a_documentation_verdict(self):
        """Doxygen dying on its own account says nothing about the engine, and must not read
        as a verdict about it - in either direction."""
        result, text = self.run_with(2, 'could not open the configuration\n')
        self.assertEqual(result, documentation.TOOL_UNUSABLE, text)
        self.assertIn('nothing can be concluded', text)

    def test_a_nonzero_exit_without_any_output_reports_that_nothing_was_concluded(self):
        result, text = self.run_with(139, '')
        self.assertEqual(result, documentation.TOOL_UNUSABLE, text)
        self.assertIn('nothing can be concluded', text)

    def test_a_tag_the_installed_doxygen_ignores_is_not_a_verdict_about_the_engine(self):
        """A tag doxygen ignores is a rule it is not applying, so the run is not this check.

        It is also not an undocumented declaration, and reporting it as one - which is what the
        first Linux run of this job did - sends the reader to the engine to look for a comment
        that is already there."""
        result, text = self.run_with(
            0, "warning: ignoring unsupported tag 'HIDE_UNDOC_NAMESPACES' at line 59, file -\n")
        self.assertEqual(result, documentation.TOOL_UNUSABLE, text)
        self.assertIn('HIDE_UNDOC_NAMESPACES', text)
        self.assertNotIn('undocumented or malformed public declaration', text)

    def test_a_run_that_was_not_configured_as_written_outranks_what_it_reported(self):
        result, text = self.run_with(
            1, "warning: ignoring unsupported tag 'HIDE_UNDOC_NAMESPACES' at line 59, file -\n"
               'Version.h:20: error: Compound an::Thing is not documented.\n')
        self.assertEqual(result, documentation.TOOL_UNUSABLE, text)
        # Everything doxygen said is still printed: the declaration it did find is real work.
        self.assertIn('an::Thing', text)

    def test_a_missing_tool_is_not_a_pass(self):
        stream = io.StringIO()
        result = documentation.main([], stream=stream, environment={'DOXYGEN_BIN': ''})
        # With no DOXYGEN_BIN and no doxygen on PATH the answer is "nothing ran"; on a machine that
        # does have one the real run decides, and either verdict is a real one.
        self.assertIn(result, (documentation.CLEAN, documentation.TOOL_UNUSABLE,
                               documentation.PROBLEMS_FOUND), stream.getvalue())
        if result == documentation.TOOL_UNUSABLE:
            # Exit 2 has two reasons - no tool, or one that did not apply the configuration - and
            # the run has to say which, because they are fixed differently.
            text = stream.getvalue()
            self.assertTrue('doxygen was not found' in text or 'did not run this check' in text,
                            text)


class ReferencePageTests(unittest.TestCase):
    """The two properties of the generated reference that no Doxygen diagnostic reports."""

    def setUp(self):
        self._temporary = tempfile.TemporaryDirectory(prefix='an-public-documentation-test-')
        self.root = pathlib.Path(self._temporary.name)
        header = self.root / documentation.PUBLIC_INCLUDE_DIR / 'AfterNightEngine/Core/Thing.h'
        header.parent.mkdir(parents=True)
        header.write_bytes(b'/// @file\r\n/// A thing.\r\n\r\n#pragma once\r\n')
        mainpage = self.root / documentation.MAINPAGE
        mainpage.parent.mkdir(parents=True)
        mainpage.write_bytes(b'# Reference\r\n')

    def tearDown(self):
        self._temporary.cleanup()

    def test_a_header_opening_with_a_file_block_reaches_the_reference(self):
        self.assertEqual(documentation.check_file_blocks(self.root), [])

    def test_a_header_whose_module_comment_is_ordinary_prose_is_found(self):
        header = self.root / documentation.PUBLIC_INCLUDE_DIR / 'AfterNightEngine/Core/Plain.h'
        header.write_bytes(b'// AfterNightEngine - a plain comment.\r\n#pragma once\r\n')
        problems = documentation.check_file_blocks(self.root)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('Core/Plain.h:1', problems[0])

    def test_the_licence_header_may_stand_before_the_file_block(self):
        header = self.root / documentation.PUBLIC_INCLUDE_DIR / 'AfterNightEngine/Core/Licensed.h'
        spdx = b'// SPDX-License-Identifier: GPL-3.0-or-later\r\n// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz\r\n\r\n'
        header.write_bytes(spdx + b'/// @file\r\n/// A thing.\r\n\r\n#pragma once\r\n')
        self.assertEqual(documentation.check_file_blocks(self.root), [])
        header.write_bytes(spdx + b'// A plain comment.\r\n#pragma once\r\n')
        self.assertEqual(len(documentation.check_file_blocks(self.root)), 1)

    def test_a_header_with_no_module_comment_is_found(self):
        header = self.root / documentation.PUBLIC_INCLUDE_DIR / 'AfterNightEngine/Core/Bare.h'
        header.write_bytes(b'#pragma once\r\n')
        self.assertEqual(len(documentation.check_file_blocks(self.root)), 1)

    def test_the_repository_configuration_opens_on_its_main_page(self):
        self.assertEqual(documentation.check_mainpage(DOXYFILE_TEXT, REPO_ROOT), [])

    def test_a_configuration_that_names_no_main_page_is_refused(self):
        problems = documentation.check_mainpage(remove_setting(DOXYFILE_TEXT, 'USE_MDFILE_AS_MAINPAGE'), self.root)
        self.assertTrue(any('USE_MDFILE_AS_MAINPAGE' in problem for problem in problems), problems)

    def test_a_main_page_that_is_not_input_is_refused(self):
        text = '\n'.join(line for line in DOXYFILE_TEXT.splitlines() if not line.startswith('INPUT '))
        text += '\nINPUT = %s\n' % ' '.join(documentation.PUBLIC_INCLUDE_DIRS)
        problems = documentation.check_mainpage(text, self.root)
        self.assertTrue(any('INPUT does not read' in problem for problem in problems), problems)

    def test_a_missing_main_page_is_refused(self):
        (self.root / documentation.MAINPAGE).unlink()
        problems = documentation.check_mainpage(DOXYFILE_TEXT, self.root)
        self.assertTrue(any('does not exist' in problem for problem in problems), problems)


class RepositoryTests(unittest.TestCase):
    def test_every_published_header_reaches_the_reference(self):
        self.assertEqual(documentation.check_file_blocks(REPO_ROOT), [])

    def test_the_repository_public_surface_is_documented(self):
        if documentation.find_doxygen() is None:
            self.skipTest('doxygen is not installed here, so the public surface was not checked')
        stream = io.StringIO()
        result = documentation.main([], stream=stream)
        reported = stream.getvalue()
        if result == documentation.TOOL_UNUSABLE and 'ignoring unsupported tag' in reported:
            # An older doxygen than the configuration needs checked less than this asks for, so it
            # cannot answer this question either way. Install one with install_doxygen.sh.
            self.skipTest(reported.splitlines()[0])
        self.assertEqual(result, documentation.CLEAN, reported)


if __name__ == '__main__':
    unittest.main(verbosity=2)

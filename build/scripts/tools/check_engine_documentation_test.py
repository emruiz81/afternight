# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_engine_documentation.

Run with:
    python build/scripts/tools/check_engine_documentation_test.py

Every rule is proved against a small tree built in a temporary directory, with the defect the rule
exists for planted in it and its absence shown to pass, so a rule that stopped reporting would fail
here rather than go quiet. The Doxygen half is proved with an injected runner; the last cases run the
real rules against the real tree, and the real tool when one is installed.
"""

from __future__ import annotations

import io
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_engine_documentation as documentation  # noqa: E402

REPO_ROOT = documentation.REPO_ROOT
INTERNAL_TEXT = (REPO_ROOT / documentation.INTERNAL_DOXYFILE).read_bytes().decode('utf-8')

# Work units' identifiers, as a comment or a message might cite them. They are assembled from parts so that this
# file, which has to show the rule what it refuses, cites none itself.
WORK_UNIT = 'R5' + '.2'
LETTERED_WORK_UNIT = 'R8' + '.16a'


def write(root, relative, text):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))
    return path


class TreeTest(unittest.TestCase):
    """A temporary repository holding one engine source, one guide index and the internal configuration."""

    def setUp(self):
        self._temporary = tempfile.TemporaryDirectory(prefix='an-engine-documentation-test-')
        self.root = pathlib.Path(self._temporary.name)
        write(self.root, documentation.INTERNAL_DOXYFILE, INTERNAL_TEXT)
        write(self.root, 'src/AfterNightEngine/src/core/Thing.cpp',
              '// AfterNightEngine - a thing.\n\n#include "core/Thing.h"\n\nnamespace an\n{\nint thing()\n'
              '{\n    return 1;\n}\n} // namespace an\n')
        write(self.root, 'src/AfterNightEngine/src/core/Thing.h',
              '// AfterNightEngine - a thing.\n#pragma once\n\nnamespace an\n{\n/// One.\nint thing();\n}\n')
        write(self.root, documentation.GUIDES_INDEX, '# Guides\n\n- [ARCHITECTURE.md](ARCHITECTURE.md)\n')
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\nThe thing is `thing()`, in `core/Thing.h`.\n')

    def tearDown(self):
        self._temporary.cleanup()

    def lexical(self):
        problems, _ = documentation.check_lexical(self.root)
        return [str(problem) for problem in problems]

    def rules(self, rule):
        return [problem for problem in self.lexical() if ': %s:' % rule in problem]


class ModuleCommentTests(unittest.TestCase):
    def test_a_comment_before_the_first_line_of_code_is_a_module_comment(self):
        self.assertTrue(documentation.has_module_comment('// What this is for.\n#include <x>\nint a;\n'))

    def test_a_comment_after_the_includes_still_comes_before_the_code(self):
        self.assertTrue(documentation.has_module_comment('#include <x>\n\n// What this is for.\nint a;\n'))

    def test_code_first_is_not_a_module_comment(self):
        self.assertFalse(documentation.has_module_comment('#pragma once\nnamespace an\n{\n// Later.\n}\n'))

    def test_a_comment_trailing_the_first_line_of_code_does_not_count(self):
        self.assertFalse(documentation.has_module_comment('#include <x>\nint a; // not a module comment\n'))

    def test_an_empty_comment_does_not_count(self):
        self.assertFalse(documentation.has_module_comment('//\n///\nint a;\n'))

    def test_a_macro_body_is_part_of_its_directive(self):
        text = '#define TWO \\\n    2\n// What this is for.\nint a = TWO;\n'
        self.assertTrue(documentation.has_module_comment(text))

    def test_a_block_comment_counts(self):
        self.assertTrue(documentation.has_module_comment('/* What this is for. */\nint a;\n'))

    def test_the_licence_header_is_not_a_module_comment(self):
        header = '// SPDX-License-Identifier: GPL-3.0-or-later\n// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz\n\n'
        self.assertFalse(documentation.has_module_comment(header + '#pragma once\nint a;\n'))
        self.assertTrue(documentation.has_module_comment(header + '/// @file\n/// What this is for.\n\nint a;\n'))
        self.assertTrue(documentation.has_module_comment('/// @file\n/// What this is for.\n\n' + header + 'int a;\n'))


class LexicalRuleTests(TreeTest):
    def test_the_planted_tree_is_clean(self):
        self.assertEqual(self.lexical(), [])

    def test_a_file_with_no_module_comment_is_found(self):
        write(self.root, 'src/AfterNightEngineGmicAdapter/src/Bare.cpp', 'namespace an\n{\n}\n')
        problems = self.rules('module-comment')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('src/AfterNightEngineGmicAdapter/src/Bare.cpp:1', problems[0])

    def test_a_work_unit_identifier_in_a_comment_is_a_planning_reference(self):
        write(self.root, 'src/AfterNightEngine/src/core/Ref.cpp',
              '// AfterNightEngine - refs.\n// The %s networks.\nint a;\n' % WORK_UNIT)
        problems = self.rules('planning-reference')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('Ref.cpp:2', problems[0])
        self.assertIn("'%s'" % WORK_UNIT, problems[0])

    def test_a_bare_work_series_in_a_comment_is_a_planning_reference(self):
        write(self.root, 'src/AfterNightEngine/src/core/Ref.cpp',
              '// AfterNightEngine - refs.\n// The seam R3 established.\nint a;\n')
        self.assertEqual(len(self.rules('planning-reference')), 1)

    def test_a_plan_coordinate_is_a_planning_reference(self):
        write(self.root, 'src/AfterNightEngine/src/core/Ref.cpp',
              '// AfterNightEngine - refs.\n// The proof section 6.9 asks for.\nint a;\n')
        self.assertEqual(len(self.rules('planning-reference')), 1)

    def test_a_camera_model_in_a_literal_is_not_a_planning_reference(self):
        write(self.root, 'src/AfterNightEngine/src/core/Ref.cpp',
              '// AfterNightEngine - refs.\nconst char* model = "Canon EOS R5";\n')
        self.assertEqual(self.rules('planning-reference'), [])

    def test_a_work_unit_identifier_in_a_test_message_is_a_planning_reference(self):
        write(self.root, 'src/AfterNightTests/Test_EngineThing/ThingTest.cpp',
              '// Tests.\nconst char* message = "the %s closure";\n' % LETTERED_WORK_UNIT)
        problems = self.rules('planning-reference')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('in a literal', problems[0])

    def test_narrating_a_functions_own_steps_and_naming_a_plan_object_are_not_references(self):
        write(self.root, 'src/AfterNightEngine/src/core/Ref.cpp',
              '// AfterNightEngine - refs.\n// Step 1: read the plan the planner returned.\n'
              '// A phase of the pipeline, and iteration 5 of the loop.\nint a;\n')
        self.assertEqual(self.rules('planning-reference'), [])

    def test_an_untracked_marker_in_a_component_is_found(self):
        write(self.root, 'src/AfterNightEngine/src/core/Mark.cpp',
              '// AfterNightEngine - marks.\n// TODO: handle the other case.\nint a;\n')
        problems = self.rules('untracked-marker')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('Mark.cpp:2', problems[0])

    def test_an_untracked_marker_in_a_suite_is_left_to_review(self):
        write(self.root, 'src/AfterNightTests/Test_EngineThing/ThingTest.cpp', '// Tests.\n// TODO: more.\nint a;\n')
        self.assertEqual(self.rules('untracked-marker'), [])

    def test_a_cited_path_that_does_not_exist_is_found(self):
        write(self.root, 'src/AfterNightEngine/src/core/Cite.cpp',
              '// AfterNightEngine - cites.\n// See docs/dev/guides/NOTHING_HERE.md.\nint a;\n')
        problems = self.rules('cited-path')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('docs/dev/guides/NOTHING_HERE.md', problems[0])

    def test_a_cited_file_that_is_gone_is_missing_even_under_the_engines_root(self):
        write(self.root, 'src/AfterNightEngine/src/core/Port.cpp',
              '// AfterNightEngine - derived from src/AfterNightEngine/src/core/Gone.cpp.\nint a;\n')
        problems = self.rules('cited-path')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('src/AfterNightEngine/src/core/Gone.cpp', problems[0])

    def test_a_cited_path_that_exists_passes(self):
        write(self.root, 'docs/dev/REAL.md', '# A note\n')
        write(self.root, 'src/AfterNightEngine/src/core/Cite.cpp',
              '// AfterNightEngine - cites.\n// See docs/dev/REAL.md.\nint a;\n')
        self.assertEqual(self.rules('cited-path'), [])


class GuideRuleTests(TreeTest):
    def test_a_guide_the_index_does_not_list_is_found(self):
        write(self.root, documentation.GUIDES_DIR + '/TESTING.md', '# Testing\n')
        problems = self.rules('guide-index')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('TESTING.md', problems[0])

    def test_a_link_that_does_not_resolve_is_found(self):
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\nSee [the notes](../NOTES.md) and [the web](https://example.com).\n')
        problems = self.rules('guide-link')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('../NOTES.md', problems[0])

    def test_a_path_that_does_not_exist_is_found(self):
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md', '# Architecture\n\nRead `core/Gone.h`.\n')
        problems = self.rules('guide-path')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('core/Gone.h', problems[0])

    def test_a_product_unit_named_by_its_path_inside_its_library_is_found(self):
        write(self.root, 'src/AfterNightControls/controls/Viewer.h', '// A view.\n')
        write(self.root, 'src/AfterNightProcesses/processes/ImageIntegration/workflow/Stage.h', '// A stage.\n')
        write(self.root, 'src/AfterNightConsumerRuntime/include/AfterNightConsumerRuntime/Session.h', '// A session.\n')
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\nThe view is `controls/Viewer.h`, the stage `workflow/Stage.h`, the session '
              '`<AfterNightConsumerRuntime/Session.h>`, and `controls/Gone.h` is not there.\n')
        problems = self.rules('guide-path')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('controls/Gone.h', problems[0])

    def test_a_shell_core_unit_named_by_its_path_inside_its_library_is_found(self):
        write(self.root, 'src/AfterNightShellCore/core/ShellRegistry.h', '// The shells.\n')
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\nThe shells are in `core/ShellRegistry.h`, and `core/Gone.h` is not there.\n')
        problems = self.rules('guide-path')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('core/Gone.h', problems[0])

    def test_a_name_spelled_nowhere_in_the_code_is_found(self):
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\nCall `an::renamedLongAgo()` and then `thing()`.\n')
        problems = self.rules('guide-name')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('renamedLongAgo', problems[0])

    def test_another_librarys_names_placeholders_and_suffixes_are_not_read_as_ours(self):
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\nA `std::span`, a `tbb::task_arena`, a shared `.inl`, and '
              '`<AfterNightEngine/<Area>/<Name>.h>`.\n')
        self.assertEqual(self.lexical(), [])

    def test_a_fenced_block_is_not_read(self):
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\n```text\n`an::notReal()` [x](gone.md)\n```\n')
        self.assertEqual(self.lexical(), [])

    def test_an_assignment_names_a_switch_the_scripts_must_read(self):
        write(self.root, 'build/premake/afternight-pm5.lua', 'local on = os.getenv("AFTERNIGHT_THING") == "1"\n')
        write(self.root, documentation.GUIDES_DIR + '/ARCHITECTURE.md',
              '# Architecture\n\n`AFTERNIGHT_THING=1` and `AFTERNIGHT_GONE=0`.\n')
        problems = self.rules('guide-name')
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('AFTERNIGHT_GONE', problems[0])

    def test_a_missing_index_is_found(self):
        (self.root / documentation.GUIDES_INDEX).unlink()
        self.assertEqual(len(self.rules('guide-index')), 1)


class InternalConfigurationTests(unittest.TestCase):
    def test_the_repository_configuration_is_a_check(self):
        self.assertEqual(documentation.check_internal_configuration(INTERNAL_TEXT, REPO_ROOT), [])

    def test_hiding_undocumented_members_is_refused(self):
        text = INTERNAL_TEXT.replace('HIDE_UNDOC_MEMBERS     = NO', 'HIDE_UNDOC_MEMBERS     = YES')
        problems = documentation.check_internal_configuration(text, REPO_ROOT)
        self.assertTrue(any('HIDE_UNDOC_MEMBERS' in problem for problem in problems), problems)

    def test_warnings_that_do_not_fail_are_refused(self):
        text = INTERNAL_TEXT.replace('WARN_AS_ERROR          = FAIL_ON_WARNINGS', 'WARN_AS_ERROR          = NO')
        problems = documentation.check_internal_configuration(text, REPO_ROOT)
        self.assertTrue(any('WARN_AS_ERROR' in problem for problem in problems), problems)

    def test_not_reading_static_functions_is_refused(self):
        text = INTERNAL_TEXT.replace('EXTRACT_STATIC         = YES', 'EXTRACT_STATIC         = NO')
        problems = documentation.check_internal_configuration(text, REPO_ROOT)
        self.assertTrue(any('EXTRACT_STATIC' in problem for problem in problems), problems)

    def test_a_component_whose_private_tree_is_not_read_is_refused(self):
        lines = INTERNAL_TEXT.replace('\r\n', '\n').split('\n')
        lines = [line.replace(' src/AfterNightEnginePythonAdapter/src', '') if line.startswith('INPUT ')
                 else line for line in lines]
        text = '\n'.join(lines)
        problems = documentation.check_internal_configuration(text, REPO_ROOT)
        self.assertTrue(any('src/AfterNightEnginePythonAdapter/src' in problem for problem in problems),
                        problems)

    def test_an_appended_input_counts_and_a_commented_one_does_not(self):
        text = 'INPUT = a\nINPUT += b c\n# INPUT += d\nINPUT_ENCODING += e\n'
        self.assertEqual(documentation.appended_values(text, 'INPUT'), ['b', 'c'])


class MainTests(TreeTest):
    def run_main(self, arguments, runner=None, environment=None):
        stream = io.StringIO()
        code = documentation.main(['--repo-root', str(self.root)] + arguments, stream=stream, runner=runner,
                                  environment=environment if environment is not None else {})
        return code, stream.getvalue()

    def test_a_clean_tree_and_a_clean_run_pass(self):
        code, output = self.run_main(['--doxygen', sys.executable], runner=lambda *_: (0, ''))
        self.assertEqual(code, documentation.CLEAN, output)

    def test_an_undocumented_private_declaration_fails(self):
        diagnostic = 'src/AfterNightEngine/src/core/Thing.h:7: warning: Member x is not documented.'
        code, output = self.run_main(['--doxygen', sys.executable], runner=lambda *_: (1, diagnostic))
        self.assertEqual(code, documentation.PROBLEMS_FOUND, output)
        self.assertIn('Member x is not documented', output)

    def test_a_run_that_was_not_configured_as_written_is_not_a_verdict(self):
        code, output = self.run_main(['--doxygen', sys.executable],
                                     runner=lambda *_: (0, 'warning: ignoring unsupported tag FOO'))
        self.assertEqual(code, documentation.TOOL_UNUSABLE, output)

    def test_a_missing_tool_is_not_a_pass(self):
        code, output = self.run_main(['--doxygen', str(self.root / 'no-such-doxygen')])
        self.assertEqual(code, documentation.TOOL_UNUSABLE, output)

    def test_skipping_the_tool_says_so_and_still_applies_the_other_rules(self):
        code, output = self.run_main(['--skip-doxygen'])
        self.assertEqual(code, documentation.CLEAN, output)
        self.assertIn('skipped on request', output)
        write(self.root, 'src/AfterNightEngine/src/core/Bare.cpp', 'int a;\n')
        code, output = self.run_main(['--skip-doxygen'])
        self.assertEqual(code, documentation.PROBLEMS_FOUND, output)

    def test_a_weakened_configuration_fails_before_anything_runs(self):
        write(self.root, documentation.INTERNAL_DOXYFILE,
              INTERNAL_TEXT.replace('EXTRACT_ALL            = NO', 'EXTRACT_ALL            = YES'))
        code, output = self.run_main(['--doxygen', sys.executable], runner=lambda *_: (0, ''))
        self.assertEqual(code, documentation.PROBLEMS_FOUND, output)
        self.assertIn('EXTRACT_ALL', output)


class RepositoryTests(unittest.TestCase):
    def test_the_repository_meets_every_rule_that_needs_no_tool(self):
        problems, files = documentation.check_lexical(REPO_ROOT)
        self.assertEqual([str(problem) for problem in problems], [])
        self.assertGreater(len(files), 700)

    def test_the_repository_private_declarations_are_documented(self):
        if public_doxygen() is None:
            self.skipTest('doxygen is not installed here, so the private declarations were not checked')
        stream = io.StringIO()
        code = documentation.main([], stream=stream)
        reported = stream.getvalue()
        if code == documentation.TOOL_UNUSABLE and 'ignoring unsupported tag' in reported:
            self.skipTest(reported.splitlines()[0])
        self.assertEqual(code, documentation.CLEAN, reported)


def public_doxygen():
    return documentation.public.find_doxygen()


if __name__ == '__main__':
    unittest.main(verbosity=2)

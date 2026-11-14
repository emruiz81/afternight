#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_shell_capabilities.py."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

import check_shell_capabilities as capabilities

CASES = '''TEST(FileOpenTest, OpensTheFile)
{
}

TEST_F(ViewMenuBuilder, EqualsTheRecordedTree)
{
}

TEST_P(ShellScenario, ViewMenuEntries)
{
}

TEST_P(CodecRoundTrip, KeepsTheSamples)
{
}

TYPED_TEST(TypedImageTest, CopiesTheImage)
{
}
'''

REGISTER = '''# The capability register.

capability view.context-menu
    title        The context menu of a view, entry for entry
    shared       ViewMenuBuilder.EqualsTheRecordedTree
    classic      ShellScenario.ViewMenuEntries/classic
    observatory  pending

capability files.open
    title        Open
    shared       FileOpenTest.OpensTheFile, CodecRoundTrip.KeepsTheSamples/Fits, TypedImageTest.CopiesTheImage
    classic
    observatory  ShellScenario.ViewMenuEntries/observatory
'''


def write(root: pathlib.Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))


class ShellCapabilitiesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        write(self.root, 'src/AfterNightTests/Test_Some/SomeTest.cpp', CASES)
        self.write_register(REGISTER)

    def tearDown(self) -> None:
        self.directory.cleanup()

    def write_register(self, text: str) -> None:
        write(self.root, capabilities.REGISTER, text)

    def problems(self, *required: str) -> list[str]:
        return capabilities.check(self.root, tuple(required))

    def test_a_register_whose_cases_exist_passes(self) -> None:
        self.assertEqual(self.problems(), [])
        self.assertIn('2 capabilities', capabilities.summary(self.root))
        self.assertIn('1 pending for the observatory shell', capabilities.summary(self.root))
        self.assertIn('1 with no classic case', capabilities.summary(self.root))

    def test_a_case_no_suite_defines_is_reported(self) -> None:
        self.write_register(REGISTER.replace('FileOpenTest.OpensTheFile', 'FileOpenTest.OpensNothing'))
        problems = self.problems()
        self.assertEqual(len(problems), 1)
        self.assertIn('FileOpenTest.OpensNothing', problems[0])
        self.assertIn('no suite', problems[0])

    def test_a_capability_listed_twice_is_reported(self) -> None:
        self.write_register(REGISTER + REGISTER.split('\n\n', 1)[1])
        self.assertTrue(any('is listed twice' in problem for problem in self.problems()))

    def test_every_capability_needs_an_observatory_line(self) -> None:
        self.write_register(REGISTER.replace('    observatory  pending\n', '    observatory\n'))
        self.assertTrue(any('names no case on its observatory line' in problem for problem in self.problems()))
        self.write_register(REGISTER.replace('    observatory  pending\n', ''))
        self.assertTrue(any('states no observatory line' in problem for problem in self.problems()))

    def test_pending_stands_alone_and_only_on_the_observatory_line(self) -> None:
        self.write_register(REGISTER.replace('observatory  pending',
                                             'observatory  pending, ShellScenario.ViewMenuEntries/observatory'))
        self.assertTrue(any('names cases there too' in problem for problem in self.problems()))
        self.write_register(REGISTER.replace('classic      ShellScenario.ViewMenuEntries/classic',
                                             'classic      pending'))
        self.assertTrue(any('only an observatory line may be' in problem for problem in self.problems()))

    def test_a_scenario_is_named_for_the_shell_of_its_line(self) -> None:
        self.write_register(REGISTER.replace('classic      ShellScenario.ViewMenuEntries/classic',
                                             'classic      ShellScenario.ViewMenuEntries/observatory'))
        self.assertTrue(any('run for the observatory shell, on its classic line' in problem
                            for problem in self.problems()))
        self.write_register(REGISTER.replace('classic      ShellScenario.ViewMenuEntries/classic',
                                             'classic      ShellScenario.ViewMenuEntries'))
        self.assertTrue(any('without the instance run for that shell' in problem for problem in self.problems()))
        # Named whole on the shared line, a parameterised case means every instance of it.
        self.write_register(REGISTER.replace('CodecRoundTrip.KeepsTheSamples/Fits', 'CodecRoundTrip.KeepsTheSamples'))
        self.assertEqual(self.problems(), [])
        self.write_register(REGISTER.replace('classic      ShellScenario.ViewMenuEntries/classic',
                                             'classic      FileOpenTest.OpensTheFile/classic'))
        self.assertTrue(any('not a value-parameterised case' in problem for problem in self.problems()))

    def test_the_lines_come_in_their_order_once_each(self) -> None:
        self.write_register(REGISTER.replace(
            '    shared       ViewMenuBuilder.EqualsTheRecordedTree\n    classic      ShellScenario.ViewMenuEntries/classic\n',
            '    classic      ShellScenario.ViewMenuEntries/classic\n    shared       ViewMenuBuilder.EqualsTheRecordedTree\n'))
        self.assertTrue(any('where its shared line belongs' in problem for problem in self.problems()))
        self.write_register(REGISTER.replace('    title        Open\n', '    title        Open\n    title        Again\n'))
        self.assertTrue(any('states its title line twice' in problem for problem in self.problems()))

    def test_a_malformed_name_or_line_is_reported(self) -> None:
        self.write_register(REGISTER.replace('capability files.open', 'capability Files_Open'))
        self.assertTrue(any('is not a capability name' in problem for problem in self.problems()))
        self.write_register(REGISTER.replace('    title        Open\n', '    title        Open\n    owner        me\n'))
        self.assertTrue(any("has a line 'owner'" in problem for problem in self.problems()))
        self.write_register(REGISTER.replace('FileOpenTest.OpensTheFile,', 'FileOpenTest,'))
        self.assertTrue(any('is not a case name' in problem for problem in self.problems()))
        self.write_register('stray line\n' + REGISTER)
        self.assertTrue(any('is neither a capability nor a line of one' in problem for problem in self.problems()))

    def test_a_required_shell_may_leave_nothing_pending(self) -> None:
        problems = self.problems('observatory')
        self.assertEqual(len(problems), 1)
        self.assertIn('view.context-menu is still pending for the observatory shell', problems[0])
        problems = self.problems('classic')
        self.assertEqual(len(problems), 1)
        self.assertIn('files.open names no case on its classic line', problems[0])

    def test_a_missing_register_is_reported(self) -> None:
        (self.root / capabilities.REGISTER).unlink()
        self.assertEqual(self.problems(), [f'{capabilities.REGISTER} does not exist'])

    def test_the_command_line_reports_and_fails(self) -> None:
        self.assertEqual(capabilities.main(['--repo-root', str(self.root)]), 0)
        self.assertEqual(capabilities.main(['--repo-root', str(self.root), '--require-shell', 'observatory']), 1)


if __name__ == '__main__':
    unittest.main()

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for panel_kinds_reference.py: the record read, the page written from it, and the repository's page held to
its record."""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import panel_kinds_reference as reference  # noqa: E402

RECORD = """\
# A preamble the reader passes over.

[limits]
nodes | 512 | How many nodes a description may hold.

[parameters]
float | parameter | - | double | A real number.
    id | text | - | 256 characters | The control's name.
    min | number | -1000000 | - | The least value.

[widgets]
column | group | holds children | - | Its children.
    visible | boolean | true | - | Whether it shows.
"""


class ReadTests(unittest.TestCase):
    def test_an_entry_is_read_with_its_depth_and_its_empty_fields(self):
        sections = reference.read_record(RECORD)
        self.assertEqual(sorted(sections), ['limits', 'parameters', 'widgets'])
        self.assertEqual(sections['parameters'][0], (0, ['float', 'parameter', '', 'double', 'A real number.']))
        self.assertEqual(sections['parameters'][1], (1, ['id', 'text', '', '256 characters', "The control's name."]))

    def test_escapes_are_undone(self):
        self.assertEqual(reference.unescape_fields('a \\| b | c\\\\d | -'), ['a | b', 'c\\d', ''])

    def test_an_entry_before_a_section_is_refused(self):
        with self.assertRaises(reference.RecordError):
            reference.read_record('float | parameter | - | - | x\n')


class RenderTests(unittest.TestCase):
    def test_each_kind_is_a_heading_with_its_aliases_and_a_table_of_its_fields(self):
        text = reference.render(reference.read_record(RECORD))
        self.assertIn('### `float` (also `double`)\r\n', text)
        self.assertIn('A real number. Role: parameter.', text)
        self.assertIn('| `min` | number | -1000000 | - | The least value. |', text)
        self.assertIn('### `column`\r\n', text)
        self.assertIn('Its children. Role: group; it holds other nodes.', text)
        self.assertIn('| nodes | 512 | How many nodes a description may hold. |', text)
        self.assertTrue(text.endswith('\r\n'))
        self.assertNotIn('\r\n\r\n\r\n', text)
        self.assertEqual(text.count('\n'), text.count('\r\n'), 'every line ends in CRLF')

    def test_the_texts_of_several_lines_end_their_lines_in_crlf_too(self):
        text = reference.render(reference.read_record(RECORD))
        self.assertIn(reference.INTRODUCTION.split('\n')[1] + '\r\n', text)

    def test_a_record_with_no_section_of_a_vocabulary_is_refused(self):
        with self.assertRaises(reference.RecordError):
            reference.render(reference.read_record('[limits]\nnodes | 512 | x\n'))

    def test_a_kind_of_the_wrong_shape_is_refused(self):
        with self.assertRaises(reference.RecordError):
            reference.render(reference.read_record('[limits]\n[parameters]\nfloat | parameter\n[widgets]\n'))

    def test_a_bar_in_a_cell_is_escaped(self):
        self.assertEqual(reference.cell('a|b'), 'a\\|b')
        self.assertEqual(reference.cell(''), '-')


class CommandTests(unittest.TestCase):
    def test_check_fails_until_the_page_is_written_and_passes_after(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / reference.RECORD).parent.mkdir(parents=True)
            (root / reference.RECORD).write_text(RECORD, encoding='utf-8')
            (root / reference.REFERENCE).parent.mkdir(parents=True)
            self.assertEqual(reference.main(['--check', '--repo-root', directory]), 1)
            self.assertEqual(reference.main(['--write', '--repo-root', directory]), 0)
            self.assertEqual(reference.main(['--check', '--repo-root', directory]), 0)


class RepositoryTests(unittest.TestCase):
    """The repository's own page, because a writer that only passes on a synthetic record proves nothing."""

    def test_the_page_is_what_the_record_generates(self):
        self.assertEqual(reference.main(['--check']), 0)


if __name__ == '__main__':
    unittest.main()

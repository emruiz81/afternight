# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for source_scan.py.

Run with:
    python build/scripts/tools/source_scan_test.py

The walk is what several checks read the tree's code through, so the shapes that defeat a regular expression are each
held here: a `//` inside a string, an escaped quote, a quote in a character literal, a raw string holding both, and a
block comment across lines - with the line count kept, because every check reports a line.
"""

from __future__ import annotations

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import source_scan  # noqa: E402


class SourceScanTests(unittest.TestCase):
    def test_comments_are_blanked_and_lines_kept(self):
        text = 'int a; // image_sptr\n/* one\n   two */ int b;\n'
        code = source_scan.strip_comments(text)
        self.assertNotIn('image_sptr', code)
        self.assertNotIn('one', code)
        self.assertEqual(code.count('\n'), text.count('\n'))
        self.assertIn('int b;', code)

    def test_a_comment_opener_inside_a_literal_is_code(self):
        code = source_scan.strip_comments('const char* u = "http://host"; int after;\n')
        self.assertIn('int after;', code)

    def test_every_literal_shape_is_one_token(self):
        text = 'a("x\\"y"); b(\'"\'); c(R"d(a"b//c)d"); /* e */ f;'
        kinds = [kind for _, _, kind in source_scan.tokens(text)]
        self.assertEqual(kinds, ['literal', 'literal', 'literal', 'block comment'])

    def test_literal_contents_are_blanked_and_returned_with_their_lines(self):
        code, literals = source_scan.blank_literals('x("one");\ny("two");\n')
        self.assertNotIn('one', code)
        self.assertEqual(literals, [(1, '"one"'), (2, '"two"')])

    def test_an_unterminated_quote_is_punctuation(self):
        self.assertEqual(list(source_scan.tokens("it's prose\nint x;\n")), [])


if __name__ == '__main__':
    unittest.main()

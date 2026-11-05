#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for consumer_source.py."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

import consumer_source as source


class ConsumerSourceTest(unittest.TestCase):
    def test_read_text_drops_the_byte_order_mark_and_carriage_returns(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'a.cpp'
            path.write_bytes(b'\xef\xbb\xbfnamespace an::ui {\r\n}\r\n')
            self.assertEqual(source.read_text(path), 'namespace an::ui {\n}\n')

    def test_code_only_blanks_comments_and_every_literal_kind(self) -> None:
        text = ('int a; // image_sptr\n'
                '/* pixel_type\n   spans */ int b;\n'
                'const char* s = "image_sptr";\n'
                'auto css = R"css(.a { color: red; }\nbody {})css";\n'
                "char c = ';';\n")
        code = source.code_only(text)
        self.assertEqual(code.count('\n'), text.count('\n'))
        for hidden in ('image_sptr', 'pixel_type', 'color', 'body', "';'"):
            self.assertNotIn(hidden, code)
        for kept in ('int a;', 'int b;', 'const char* s =', 'auto css =', 'char c ='):
            self.assertIn(kept, code)

    def test_without_comments_keeps_literals(self) -> None:
        text = 'm.def("load_image", f); // a comment\n'
        stripped = source.without_comments(text)
        self.assertIn('"load_image"', stripped)
        self.assertNotIn('a comment', stripped)


if __name__ == '__main__':
    unittest.main()

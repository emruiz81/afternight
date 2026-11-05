# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for binary_imports.py.

Run with:
    python build/scripts/tools/binary_imports_test.py

The reader's two shapes - a PE import table's `Name:` lines and an ELF file's `NeededLibraries` list - are read from
captured output, and an engine module is recognised in every platform's spelling of its name and no other.
"""

from __future__ import annotations

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import binary_imports  # noqa: E402


class BinaryImportsTests(unittest.TestCase):
    def test_a_pe_import_table_is_read(self):
        output = ('Import {\n  Name: KERNEL32.dll\n  ImportLookupTableRVA: 0x1\n}\n'
                  'Import {\n  Name: tiff.dll\n}\n')
        self.assertEqual(binary_imports.imported_names(output), ['KERNEL32.dll', 'tiff.dll'])

    def test_an_elf_needed_list_is_read(self):
        output = 'NeededLibraries [\n  libtiff.so.6\n  libc.so.6\n]\n'
        self.assertEqual(binary_imports.imported_names(output), ['libtiff.so.6', 'libc.so.6'])

    def test_an_engine_is_recognised_in_every_spelling_of_its_name(self):
        for name in ('Engine.dll', 'Engine_d.dll', 'libEngine.so', 'libEngine.so.1.2', 'Engine.lib', 'ENGINE.DLL'):
            self.assertTrue(binary_imports.is_engine_module(name, 'Engine'), name)
        for name in ('EngineBench.exe', 'libEngineAdapter.so', 'OtherEngine.dll'):
            self.assertFalse(binary_imports.is_engine_module(name, 'Engine'), name)


if __name__ == '__main__':
    unittest.main()

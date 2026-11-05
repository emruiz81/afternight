# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_license_headers.

Run with:
    python build/scripts/tools/check_license_headers_test.py

The cases plant sources in a temporary directory, which is not a repository, so the check walks it; the last case
reads the repository's own tree.
"""

from __future__ import annotations

import io
import os
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_license_headers as headers  # noqa: E402

GPL = '// SPDX-License-Identifier: GPL-3.0-or-later\n'
APACHE = '// SPDX-License-Identifier: Apache-2.0\n'
COPYRIGHT = '// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz\n'
PY_GPL = '# SPDX-License-Identifier: GPL-3.0-or-later\n'
PY_APACHE = '# SPDX-License-Identifier: Apache-2.0\n'
PY_COPYRIGHT = '# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz\n'


class TreeCase(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)

    def write(self, relative, text, encoding='utf-8'):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode(encoding) if isinstance(text, str) else text)
        return path

    def read(self, relative):
        return (self.root / relative).read_bytes().decode('utf-8')

    def problems(self, relative, text):
        return headers.check_text(relative, text)

    def assertClean(self, relative, text):
        self.assertEqual(self.problems(relative, text), [])

    def assertProblem(self, relative, text, words):
        found = self.problems(relative, text)
        self.assertEqual(len(found), 1, found)
        self.assertIn(relative, found[0])
        self.assertIn(words, found[0])


class CheckTests(TreeCase):
    def test_a_file_with_both_lines_before_its_code_is_clean(self):
        self.assertClean('src/A/A.cpp', GPL + COPYRIGHT + '\n#include "A.h"\nint a;\n')

    def test_a_file_with_no_licence_line_is_named(self):
        self.assertProblem('src/A/A.cpp', COPYRIGHT + '#include "A.h"\n', 'states no licence')

    def test_a_file_with_no_copyright_line_is_named(self):
        self.assertProblem('src/A/A.h', GPL + '#pragma once\n', 'states no copyright')

    def test_a_licence_stated_after_the_first_line_of_code_does_not_count(self):
        self.assertProblem('src/A/A.cpp', 'int a;\n' + GPL + COPYRIGHT, 'states no licence')

    def test_a_licence_stated_after_a_directive_still_counts(self):
        self.assertClean('src/A/A.h', '#pragma once\n\n' + GPL + COPYRIGHT + '\nint a;\n')

    def test_a_licence_inside_a_block_comment_is_not_the_header(self):
        self.assertProblem('src/A/A.cpp', '/*\n' + GPL + '*/\n' + COPYRIGHT + 'int a;\n', 'states no licence')

    def test_two_licence_lines_are_named(self):
        self.assertProblem('src/A/A.cpp', GPL + GPL + COPYRIGHT + 'int a;\n', 'two licence lines')

    def test_the_apache_boundary_states_apache(self):
        self.assertClean('src/AfterNightExtensionProtocol/P.h', '#pragma once\n' + APACHE + COPYRIGHT)
        self.assertProblem('src/AfterNightExtensionProtocol/P.h', GPL + COPYRIGHT, 'must state Apache-2.0')
        self.assertProblem('src/AfterNightExtensionHostLite/main.cpp', GPL + COPYRIGHT, 'must state Apache-2.0')

    def test_apache_outside_the_boundary_is_refused(self):
        self.assertProblem('src/AfterNightControls/C.cpp', APACHE + COPYRIGHT, 'must state GPL-3.0-or-later')

    def test_the_python_sdk_is_split_by_module(self):
        self.assertClean('python/modules/afternight/protocol.py', PY_APACHE + PY_COPYRIGHT + '"""Doc."""\n')
        self.assertClean('python/modules/afternight/core.py', PY_GPL + PY_COPYRIGHT + '"""Doc."""\n')
        self.assertProblem('python/modules/afternight/core.py', PY_APACHE + PY_COPYRIGHT, 'must state GPL-3.0-or-later')

    def test_a_derived_file_states_its_upstream_terms(self):
        relative = 'src/AfterNightControls/widgets/waitingspinnerwidget.h'
        self.assertIn(relative, headers.EXPRESSIONS)
        self.assertClean(relative, '/* notice */\n\n// SPDX-License-Identifier: MIT\n' + COPYRIGHT)
        self.assertProblem(relative, GPL + COPYRIGHT, 'must state MIT')

    def test_python_reads_the_comments_before_its_first_statement(self):
        self.assertClean('build/x.py', '#!/usr/bin/env python3\n' + PY_GPL + PY_COPYRIGHT + '"""Doc."""\n')
        self.assertProblem('build/x.py', '"""Doc."""\n' + PY_GPL + PY_COPYRIGHT, 'states no licence')

    def test_a_python_line_is_not_read_in_c_form(self):
        self.assertProblem('build/x.py', GPL.replace('//', '#') + '// SPDX-FileCopyrightText: x\n', 'states no copyright')


class ScopeTests(TreeCase):
    def test_owned_sources_are_read_and_others_are_not(self):
        self.assertTrue(headers.held('src/AfterNightEngine/src/core/Version.cpp'))
        self.assertTrue(headers.held('src/AfterNightControls/stdafx.hpp'))
        self.assertTrue(headers.held('build/scripts/tools/check_build_graph.py'))
        self.assertTrue(headers.held('src/AfterNightEngine/src/kernels/reference/StackMethodRow.inl'))
        self.assertFalse(headers.held('3rdparty/src/gmic/gmic.h'))
        self.assertFalse(headers.held('docs/dev/README.md'))
        self.assertFalse(headers.held('build/premake/afternight-pm5.lua'))

    def test_the_check_walks_a_tree_and_names_each_file(self):
        self.write('src/A/A.cpp', GPL + COPYRIGHT + 'int a;\n')
        self.write('src/A/B.cpp', 'int b;\n')
        self.write('3rdparty/src/x/x.c', 'int x;\n')
        self.write('docs/note.md', 'text\n')
        stream = io.StringIO()
        self.assertEqual(headers.run(self.root, expressions={}, stream=stream), 1)
        output = stream.getvalue()
        self.assertIn('src/A/B.cpp', output)
        self.assertNotIn('src/A/A.cpp', output)
        self.assertNotIn('3rdparty', output)

    def test_a_derived_entry_for_a_missing_file_is_stale(self):
        self.write('src/A/A.cpp', GPL + COPYRIGHT + 'int a;\n')
        stale = {'src/A/Gone.cpp': ('MIT', 'a file that left')}
        stream = io.StringIO()
        self.assertEqual(headers.run(self.root, expressions=stale, stream=stream), 1)
        self.assertIn('names a file that does not exist', stream.getvalue())
        self.assertEqual(headers.run(self.root, expressions={}, stream=io.StringIO()), 0)

    def test_main_reads_the_given_root(self):
        self.write('src/A/B.cpp', 'int b;\n')
        stream = io.StringIO()
        self.assertEqual(headers.main(['--root', str(self.root)], stream=stream), 1)
        self.assertIn('src/A/B.cpp: states no licence', stream.getvalue())


class InsertTests(TreeCase):
    def test_an_owned_file_gets_both_lines_and_a_blank_line_at_the_top(self):
        new = headers.insert_header('src/A/A.cpp', '#include "A.h"\r\nint a;\r\n')
        self.assertEqual(new, GPL.replace('\n', '\r\n') + COPYRIGHT.replace('\n', '\r\n') + '\r\n'
                         '#include "A.h"\r\nint a;\r\n')

    def test_the_line_ending_is_the_file_s(self):
        new = headers.insert_header('src/A/A.cpp', '#include "A.h"\nint a;\n')
        self.assertEqual(new, GPL + COPYRIGHT + '\n#include "A.h"\nint a;\n')

    def test_a_byte_order_mark_stays_first(self):
        new = headers.insert_header('src/A/A.cpp', '﻿#include "stdafx.hpp"\n')
        self.assertTrue(new.startswith('﻿' + GPL))

    def test_a_module_comment_follows_the_header(self):
        new = headers.insert_header('src/E/V.h', '/// @file\n/// The version.\n\n#pragma once\n')
        self.assertEqual(new, GPL + COPYRIGHT + '\n/// @file\n/// The version.\n\n#pragma once\n')

    def test_an_opening_notice_stays_first_and_the_header_follows_it(self):
        text = ('// Derived from X 1.0 by A. Person, Copyright (C) 2023 A. Person,\n'
                '// distributed under the GNU General Public License, version 3 or later.\n'
                '\n#pragma once\n')
        new = headers.insert_header('src/E/D.h', text)
        self.assertEqual(new, text.split('\n\n')[0] + '\n\n' + GPL + COPYRIGHT + '\n#pragma once\n')

    def test_a_description_that_says_derived_from_is_not_a_notice(self):
        text = '/// @file\n/// The widths derived from the fit, in pixels.\n\n#pragma once\n'
        self.assertTrue(headers.insert_header('src/E/W.h', text).startswith(GPL + COPYRIGHT + '\n/// @file'))

    def test_a_sentence_saying_where_the_file_came_from_is_a_notice(self):
        text = '/// @file\n/// The estimate.\n///\n/// **Derived from X 1.0 by A. Person.**\n\n#pragma once\n'
        new = headers.insert_header('src/E/X.h', text)
        self.assertTrue(new.startswith('/// @file\n'))
        self.assertIn('Person.**\n\n' + GPL + COPYRIGHT + '\n#pragma once\n', new)

    def test_a_block_notice_is_followed_too(self):
        text = '/* Original Work Copyright (c) 2012 Someone\n   MIT text\n*/\n\n#pragma once\n'
        new = headers.insert_header('src/AfterNightControls/widgets/waitingspinnerwidget.h', text)
        self.assertEqual(new, '/* Original Work Copyright (c) 2012 Someone\n   MIT text\n*/\n\n'
                         '// SPDX-License-Identifier: MIT\n' + COPYRIGHT + '\n#pragma once\n')

    def test_a_file_stating_its_licence_gets_only_the_copyright_line(self):
        text = '#pragma once\n\n' + APACHE + '\n#include "x.h"\n'
        new = headers.insert_header('src/AfterNightExtensionProtocol/P.h', text)
        self.assertEqual(new, '#pragma once\n\n' + APACHE + COPYRIGHT + '\n#include "x.h"\n')

    def test_a_clean_file_is_left_alone(self):
        text = GPL + COPYRIGHT + '\nint a;\n'
        self.assertEqual(headers.insert_header('src/A/A.cpp', text), text)

    def test_a_wrong_licence_is_refused_rather_than_rewritten(self):
        with self.assertRaises(headers.Refused):
            headers.insert_header('src/A/A.cpp', APACHE + 'int a;\n')

    def test_python_keeps_its_shebang_and_coding_lines_first(self):
        text = '#!/usr/bin/env python3\n# -*- coding: utf-8 -*-\n"""Doc."""\nimport os\n'
        new = headers.insert_header('build/x.py', text)
        self.assertEqual(new, '#!/usr/bin/env python3\n# -*- coding: utf-8 -*-\n' + PY_GPL + PY_COPYRIGHT
                         + '"""Doc."""\nimport os\n')

    def test_python_gets_the_header_without_a_blank_line(self):
        new = headers.insert_header('build/x.py', '"""Doc."""\n')
        self.assertEqual(new, PY_GPL + PY_COPYRIGHT + '"""Doc."""\n')

    def test_a_python_file_stating_its_licence_gets_only_the_copyright_line(self):
        new = headers.insert_header('python/modules/afternight/lite.py', PY_APACHE + '"""Doc."""\n')
        self.assertEqual(new, PY_APACHE + PY_COPYRIGHT + '"""Doc."""\n')

    def test_an_inserted_file_passes_the_check(self):
        for relative, text in (('src/A/A.cpp', '#include "A.h"\n'),
                               ('src/AfterNightExtensionProtocol/P.h', '#pragma once\n' + APACHE),
                               ('build/x.py', '#!/usr/bin/env python3\nimport os\n'),
                               ('src/E/D.h', '// Copyright (C) 2023 A. Person, GPL-3.0-or-later.\nint d;\n')):
            self.assertClean(relative, headers.insert_header(relative, text))


class ProofTests(unittest.TestCase):
    def test_an_insertion_of_header_lines_is_comment_only(self):
        headers.prove_comment_only('src/A/A.cpp', 'int a;\n', GPL + COPYRIGHT + '\nint a;\n')
        headers.prove_comment_only('build/x.py', 'import os\n', PY_GPL + PY_COPYRIGHT + 'import os\n')

    def test_a_changed_line_of_code_is_refused(self):
        with self.assertRaises(headers.Refused):
            headers.prove_comment_only('src/A/A.cpp', 'int a;\n', GPL + COPYRIGHT + 'int b;\n')

    def test_an_inserted_line_that_is_not_the_header_is_refused(self):
        with self.assertRaises(headers.Refused):
            headers.prove_comment_only('src/A/A.cpp', 'int a;\n', '// a note\nint a;\n')

    def test_a_header_line_landing_inside_a_literal_is_refused(self):
        old = 'auto s = R"(\nx\n)";\n'
        new = 'auto s = R"(\n' + GPL + COPYRIGHT + 'x\n)";\n'
        with self.assertRaises(headers.Refused):
            headers.prove_comment_only('src/A/A.cpp', old, new)

    def test_a_python_header_landing_inside_a_string_is_refused(self):
        old = 'X = """\nx\n"""\n'
        new = 'X = """\n' + PY_GPL + PY_COPYRIGHT + 'x\n"""\n'
        with self.assertRaises(headers.Refused):
            headers.prove_comment_only('build/x.py', old, new)


class ApplyTests(TreeCase):
    def test_apply_writes_every_missing_header_and_keeps_the_bytes_otherwise(self):
        self.write('src/A/A.cpp', b'#include "A.h"\r\nint a;\r\n')
        self.write('src/A/B.cpp', (GPL + COPYRIGHT + 'int b;\n').encode('utf-8'))
        self.write('tools/t.py', b'"""Doc."""\r\n')
        stream = io.StringIO()
        self.assertEqual(headers.run(self.root, apply=True, expressions={}, stream=stream), 0)
        self.assertEqual((self.root / 'src/A/A.cpp').read_bytes(),
                         (GPL + COPYRIGHT + '\n#include "A.h"\nint a;\n').replace('\n', '\r\n').encode('utf-8'))
        self.assertEqual(self.read('src/A/B.cpp'), GPL + COPYRIGHT + 'int b;\n')
        self.assertEqual(self.read('tools/t.py'), (PY_GPL + PY_COPYRIGHT + '"""Doc."""\n').replace('\n', '\r\n'))
        self.assertIn('2 file(s) given a header', stream.getvalue())
        self.assertEqual(headers.run(self.root, expressions={}, stream=io.StringIO()), 0)

    def test_apply_refuses_a_file_it_cannot_prove_and_leaves_it(self):
        self.write('src/A/A.cpp', APACHE + 'int a;\n')
        stream = io.StringIO()
        self.assertEqual(headers.run(self.root, apply=True, expressions={}, stream=stream), 1)
        self.assertEqual(self.read('src/A/A.cpp'), APACHE + 'int a;\n')
        self.assertIn('src/A/A.cpp', stream.getvalue())

    def test_a_non_utf8_byte_survives(self):
        self.write('src/A/A.cpp', b'// caf\xe9\nint a;\n')
        headers.run(self.root, apply=True, expressions={}, stream=io.StringIO())
        self.assertTrue((self.root / 'src/A/A.cpp').read_bytes().endswith(b'// caf\xe9\nint a;\n'))


class RepositoryCase(unittest.TestCase):
    """The register against the real tree: every derived entry names a file that exists and is held."""

    def test_every_register_entry_names_a_held_file(self):
        for relative in headers.EXPRESSIONS:
            self.assertTrue((headers.REPO_ROOT / relative).is_file(), relative)
            self.assertTrue(headers.held(relative), relative)
            self.assertFalse(headers.in_apache_boundary(relative), relative)


if __name__ == '__main__':
    unittest.main()

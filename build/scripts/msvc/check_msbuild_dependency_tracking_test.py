# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_msbuild_dependency_tracking.

Run with:
    python build/scripts/msvc/check_msbuild_dependency_tracking_test.py

Every case builds a throwaway source tree plus a hand-written MSBuild tracking log, so the
suite needs nothing from the real repository and writes nothing outside its temp dir.
"""

from __future__ import annotations

import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_msbuild_dependency_tracking as checker  # noqa: E402


class TrackingCheckTestCase(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='an-tracking-test-')
        self.addCleanup(shutil.rmtree, self.root, True)
        self.src = os.path.join(self.root, 'src')
        self.obj = os.path.join(self.root, 'obj')
        self.tlog_dir = os.path.join(self.obj, 'Project.tlog')
        os.makedirs(self.src)
        os.makedirs(self.tlog_dir)

    # -- fixture helpers ---------------------------------------------------------------

    def write_source(self, relative_path, text):
        path = os.path.join(self.src, relative_path.replace('/', os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(text)
        return path

    def write_read_tlog(self, groups):
        """groups: [(sources, reads)] - written exactly as MSBuild writes them."""
        lines = []
        for sources, reads in groups:
            lines.append('^' + '|'.join(source.upper() for source in sources))
            lines.extend(read.upper() for read in reads)
        payload = ('﻿' + '\r\n'.join(lines) + '\r\n').encode('utf-16-le')
        with open(os.path.join(self.tlog_dir, 'CL.read.1.tlog'), 'wb') as handle:
            handle.write(payload)

    def write_object(self, name):
        path = os.path.join(self.obj, name)
        with open(path, 'wb') as handle:
            handle.write(b'\0')
        return path

    def run_checker(self, fix=False):
        stream = io.StringIO()
        code = checker.run(self.obj, [self.src], fix, stream)
        return code, stream.getvalue()

    # -- log parsing -------------------------------------------------------------------

    def test_parses_utf16_log_and_shared_groups(self):
        self.write_read_tlog([(['a.cpp', 'b.cpp'], ['x.h'])])
        entries = checker.merge_read_tlogs(self.tlog_dir)
        self.assertEqual(sorted(entries), ['A.CPP', 'B.CPP'])
        self.assertEqual(entries['A.CPP'], {'X.H'})
        self.assertEqual(entries['B.CPP'], {'X.H'})

    # -- the healthy case --------------------------------------------------------------

    def test_complete_log_reports_nothing(self):
        header = self.write_source('Widget.h', '#pragma once\n')
        source = self.write_source('Widget.cpp', '#include "Widget.h"\n')
        self.write_read_tlog([([source], [header])])
        self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 0, output)
        self.assertIn('Dependency tracking OK', output)

    # -- the failure this whole tool exists for ----------------------------------------

    def test_truncated_log_is_reported_and_fixable(self):
        self.write_source('Widget.h', '#pragma once\n')
        source = self.write_source('Widget.cpp', '#include "Widget.h"\n')
        self.write_read_tlog([([source], [])])
        obj = self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 1, output)
        self.assertIn('Widget.h', output)
        self.assertTrue(os.path.exists(obj))

        code, output = self.run_checker(fix=True)
        self.assertEqual(code, 0, output)
        self.assertFalse(os.path.exists(obj))

    def test_transitive_header_must_also_be_recorded(self):
        self.write_source('Model.h', '#pragma once\n')
        inner = self.write_source('Widget.h', '#pragma once\n#include "Model.h"\n')
        source = self.write_source('Widget.cpp', '#include "Widget.h"\n')
        self.write_read_tlog([([source], [inner])])
        self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 1, output)
        self.assertIn('Model.h', output)

    def test_missing_object_is_not_a_finding(self):
        self.write_source('Widget.h', '#pragma once\n')
        source = self.write_source('Widget.cpp', '#include "Widget.h"\n')
        self.write_read_tlog([([source], [])])

        code, output = self.run_checker()
        self.assertEqual(code, 0, output)

    # -- cases that must never produce noise -------------------------------------------

    def test_conditional_include_is_not_required(self):
        self.write_source('WindowsOnly.h', '#pragma once\n')
        source = self.write_source('Widget.cpp',
                                   '#ifdef _WIN32\n#include "WindowsOnly.h"\n#endif\n')
        self.write_read_tlog([([source], [])])
        self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 0, output)

    def test_commented_out_include_is_not_required(self):
        self.write_source('Unused.h', '#pragma once\n')
        source = self.write_source('Widget.cpp',
                                   '// #include "Unused.h"\n/* #include "Unused.h" */\n')
        self.write_read_tlog([([source], [])])
        self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 0, output)

    def test_spelled_out_path_does_not_match_a_bare_basename_elsewhere(self):
        # <kddockwidgets/core/Group.h> must not resolve to 3rdparty/src/cfitsio/group.h.
        self.write_source('cfitsio/group.h', '#pragma once\n')
        source = self.write_source('Widget.cpp', '#include <kddockwidgets/core/Group.h>\n')
        self.write_read_tlog([([source], [])])
        self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 0, output)

    def test_read_log_picks_the_mock_over_the_shadowed_header(self):
        # The test suites put mocks/ ahead of the real tree on the include path; only the
        # mock is really opened, so only the mock's own includes may be required.
        self.write_source('controls/Widget.h', '#pragma once\n#include "HeavyDependency.h"\n')
        self.write_source('HeavyDependency.h', '#pragma once\n')
        mock = self.write_source('mocks/controls/Widget.h', '#pragma once\n')
        source = self.write_source('test_widget.cpp', '#include "controls/Widget.h"\n')
        self.write_read_tlog([([source], [mock])])
        self.write_object('test_widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 0, output)

    def test_precompiled_header_reads_count_for_consumers(self):
        engine = self.write_source('engine.h', '#pragma once\n')
        pch_header = self.write_source('stdafx.hpp', '#pragma once\n#include "engine.h"\n')
        pch_source = self.write_source('stdafx.cpp', '#include "stdafx.hpp"\n')
        source = self.write_source('Widget.cpp', '#include "engine.h"\n')
        self.write_read_tlog([
            ([pch_source], [pch_header, engine]),
            ([source], [os.path.join(self.obj, 'Project.pch'), pch_header]),
        ])
        self.write_object('stdafx.obj')
        self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 0, output)

    def test_precompiled_header_does_not_excuse_a_header_it_never_read(self):
        pch_header = self.write_source('stdafx.hpp', '#pragma once\n')
        pch_source = self.write_source('stdafx.cpp', '#include "stdafx.hpp"\n')
        self.write_source('Widget.h', '#pragma once\n')
        source = self.write_source('Widget.cpp', '#include "Widget.h"\n')
        self.write_read_tlog([
            ([pch_source], [pch_header]),
            ([source], [os.path.join(self.obj, 'Project.pch'), pch_header]),
        ])
        self.write_object('stdafx.obj')
        self.write_object('Widget.obj')

        code, output = self.run_checker()
        self.assertEqual(code, 1, output)
        self.assertIn('Widget.h', output)

    def test_missing_object_root_is_a_usage_error(self):
        stream = io.StringIO()
        code = checker.run(os.path.join(self.root, 'nope'), [self.src], False, stream)
        self.assertEqual(code, 2)


if __name__ == '__main__':
    unittest.main(verbosity=2)

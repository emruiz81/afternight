#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_consumer_layering.py.

Every case builds a throwaway tree holding the product sources the case is about, so the suite reads nothing of
the real repository.
"""

from __future__ import annotations

import io
import pathlib
import tempfile
import unittest
from contextlib import redirect_stdout

import check_consumer_layering as layering


CLEAN_PRECOMPILED_HEADER = '#pragma once\n#include <QtWidgets>\n#include <AfterNightPlatform/Logging.h>\n#include <string>\n'


def write(root: pathlib.Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))


class LayeringCase(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        write(self.root, 'src/AfterNightPlatform/include/AfterNightPlatform/Logging.h', '#pragma once\n')
        write(self.root, 'src/AfterNightControls/stdafx.hpp', CLEAN_PRECOMPILED_HEADER)
        write(self.root, 'src/AfterNightProcesses/stdafx.hpp', CLEAN_PRECOMPILED_HEADER)

    def run_check(self) -> tuple[int, str]:
        stream = io.StringIO()
        with redirect_stdout(stream):
            code = layering.main(['--repo-root', str(self.root)])
        return code, stream.getvalue()


class SourceOrderTests(LayeringCase):
    def test_sources_are_ordered_by_their_components_with_case_on_every_host(self) -> None:
        for name in ('controls/drawing/Brush.h', 'controls/alpha.h', 'controls/Zeta.h'):
            write(self.root, 'src/AfterNightControls/' + name, '#pragma once\n')
        ordered = [path.relative_to(self.root / 'src/AfterNightControls').as_posix()
                   for path in layering._sources(self.root, 'src/AfterNightControls')]
        self.assertEqual(ordered, ['controls/Zeta.h', 'controls/alpha.h', 'controls/drawing/Brush.h', 'stdafx.hpp'])


class CleanTreeTests(LayeringCase):
    def test_a_clean_tree_passes(self) -> None:
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('read 2 layered library sources', output)


class LayerTests(LayeringCase):
    def test_controls_including_a_processes_header_fails(self) -> None:
        write(self.root, 'src/AfterNightProcesses/processes/ProcessCommonUtils.h', '#pragma once\n')
        write(self.root, 'src/AfterNightControls/app/FileManager.cpp', '#include "processes/ProcessCommonUtils.h"\n')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('src/AfterNightControls/app/FileManager.cpp:1 includes processes/ProcessCommonUtils.h from '
                      'AfterNightProcesses', output)

    def test_controls_reaching_processes_by_a_relative_path_fails(self) -> None:
        write(self.root, 'src/AfterNightProcesses/processes/RTPreviewUiCommon.h', '#pragma once\n')
        # Three levels up, so that only the including file's own directory resolves it.
        write(self.root, 'src/AfterNightControls/controls/detail/Histogram.h',
              '#pragma once\n\n#include "../../../AfterNightProcesses/processes/RTPreviewUiCommon.h"\n')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('src/AfterNightControls/controls/detail/Histogram.h:3 includes '
                      '../../../AfterNightProcesses/processes/RTPreviewUiCommon.h from AfterNightProcesses', output)

    def test_a_path_both_libraries_hold_resolves_to_the_including_librarys_own(self) -> None:
        write(self.root, 'src/AfterNightProcesses/app/ProcessLauncher.h', '#pragma once\n')
        write(self.root, 'src/AfterNightControls/app/ProcessLauncher.h', '#pragma once\n')
        write(self.root, 'src/AfterNightControls/app/Registry.cpp', '#include "app/ProcessLauncher.h"\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_processes_including_a_controls_header_passes(self) -> None:
        write(self.root, 'src/AfterNightControls/controls/IAppContext.h', '#pragma once\n')
        write(self.root, 'src/AfterNightProcesses/processes/CurvesProcess.cpp', '#include "controls/IAppContext.h"\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_processes_including_an_application_header_fails(self) -> None:
        write(self.root, 'src/AfterNightApp/app/AfterNightApp.h', '#pragma once\n')
        write(self.root, 'src/AfterNightProcesses/processes/CurvesProcess.cpp', '#include "app/AfterNightApp.h"\n')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('includes app/AfterNightApp.h from AfterNightApp', output)

    def test_a_commented_include_is_not_read(self) -> None:
        write(self.root, 'src/AfterNightProcesses/processes/ProcessCommonUtils.h', '#pragma once\n')
        write(self.root, 'src/AfterNightControls/app/FileManager.cpp',
              '/*\n#include "processes/ProcessCommonUtils.h"\n*/\n// #include "processes/ProcessCommonUtils.h"\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)




if __name__ == '__main__':
    unittest.main()

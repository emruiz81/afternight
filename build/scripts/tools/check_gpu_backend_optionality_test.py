# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_gpu_backend_optionality.

Run with:
    python build/scripts/tools/check_gpu_backend_optionality_test.py

Most cases write a throwaway tree holding only the files the checker reads, so the suite needs nothing
from the real repository and writes nothing outside its own temporary directory. The last case reads
the repository itself, because a checker that only ever passes on synthetic input proves nothing about
the files it exists to guard.
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

import check_gpu_backend_optionality as optionality  # noqa: E402


MSVC_DEPENDENCIES = (
    '@echo off\n'
    'call "%~dp0_setup_vsdev_env.bat"\n'
    'vcpkg install ffmpeg[core,avcodec,avformat] --triplet=x64-windows\n'
    'call "%~dp0install_raw_deps.bat"\n'
)

LINUX_DEPENDENCIES = (
    '#!/bin/bash\n'
    'sudo apt-get install -y libtiff-dev libpng-dev\n'
    "./vcpkg install 'highway[contrib]' --triplet x64-linux\n"
)

RAW_DEPENDENCIES = '@echo off\nvcpkg install afternight-libraw --triplet=x64-windows-raw\n'

OPT_IN_INSTALLER = (
    '@echo off\n'
    'REM Installs what the opt-in OpenCL backend compiles against.\n'
    'vcpkg install opencl --vcpkg-root=. --triplet=x64-windows-opencl --overlay-triplets=..\\build\\vcpkg\\triplets\n'
)


class OptionalityCase(unittest.TestCase):
    """A tree whose dependency scripts install no GPU SDK and whose backend is opt-in."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-gpu-optionality-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.write(optionality.MSVC_DEPENDENCY_SCRIPT, MSVC_DEPENDENCIES)
        self.write(optionality.LINUX_DEPENDENCY_SCRIPT, LINUX_DEPENDENCIES)
        self.write('build/scripts/msvc/_setup_vsdev_env.bat', '@echo off\n')
        self.write('build/scripts/msvc/install_raw_deps.bat', RAW_DEPENDENCIES)
        self.write(optionality.OPT_IN_INSTALLER, OPT_IN_INSTALLER)
        for definition in optionality.PREMAKE_DEFINITIONS:
            self.write(definition, 'local common = require "afternight-common"\n%s\n' % optionality.OPT_IN_GATE)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def run_check(self):
        stream = io.StringIO()
        code = optionality.main(['--repo-root', str(self.root)], stream=stream)
        return code, stream.getvalue()

    def assertClean(self):
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def assertRejected(self, fragment):
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn(fragment, output)


class DependencyScriptTests(OptionalityCase):
    """Neither dependency script, nor a script either one calls, installs a GPU SDK."""

    def test_a_default_toolchain_with_no_gpu_sdk_passes(self):
        self.assertClean()

    def test_the_msvc_dependency_script_installing_opencl_fails(self):
        self.write(optionality.MSVC_DEPENDENCY_SCRIPT,
                   MSVC_DEPENDENCIES + 'vcpkg install opencl --triplet=x64-windows\n')
        self.assertRejected('02-install_msvc_deps.bat:5 names a GPU SDK (opencl)')

    def test_the_linux_dependency_script_installing_a_cuda_toolkit_fails(self):
        self.write(optionality.LINUX_DEPENDENCY_SCRIPT,
                   LINUX_DEPENDENCIES + 'sudo apt-get install -y nvidia-cuda-toolkit\n')
        self.assertRejected('02-install_linux_deps.sh:4 names a GPU SDK (cuda)')

    def test_a_distribution_opencl_loader_is_a_gpu_sdk_as_well(self):
        self.write(optionality.LINUX_DEPENDENCY_SCRIPT,
                   LINUX_DEPENDENCIES + 'sudo apt-get install -y ocl-icd-opencl-dev\n')
        self.assertRejected('02-install_linux_deps.sh:4 names a GPU SDK (ocl-icd, opencl)')

    def test_calling_the_opt_in_installer_from_the_dependency_script_fails(self):
        self.write(optionality.MSVC_DEPENDENCY_SCRIPT,
                   MSVC_DEPENDENCIES + 'call "%~dp0install_opencl_backend_deps.bat"\n')
        self.assertRejected('02-install_msvc_deps.bat:5 names a GPU SDK (opencl)')

    def test_a_script_the_dependency_script_calls_is_read_as_well(self):
        self.write('build/scripts/msvc/install_raw_deps.bat',
                   '@echo off\nvcpkg install cuda --triplet=x64-windows\n')
        self.assertRejected('build/scripts/msvc/install_raw_deps.bat:2 names a GPU SDK (cuda), and '
                            'build/scripts/msvc/02-install_msvc_deps.bat calls it')

    def test_a_comment_that_names_a_gpu_sdk_installs_nothing(self):
        self.write(optionality.MSVC_DEPENDENCY_SCRIPT,
                   MSVC_DEPENDENCIES + 'REM The OpenCL backend installs its own headers elsewhere.\n'
                   ':: nor CUDA\n')
        self.write(optionality.LINUX_DEPENDENCY_SCRIPT, LINUX_DEPENDENCIES + '# no CUDA toolkit here\n')
        self.assertClean()

    def test_a_word_that_merely_contains_a_token_is_not_a_finding(self):
        self.write(optionality.LINUX_DEPENDENCY_SCRIPT,
                   LINUX_DEPENDENCIES + 'sudo apt-get install -y barracuda-tools libsyclone-dev\n')
        self.assertClean()

    def test_a_missing_dependency_script_fails(self):
        (self.root / optionality.LINUX_DEPENDENCY_SCRIPT).unlink()
        self.assertRejected('02-install_linux_deps.sh: not found')


class OptInInstallerTests(OptionalityCase):
    """The opt-in installer puts the SDK where no default project looks."""

    def test_installing_into_the_default_triplet_fails(self):
        self.write(optionality.OPT_IN_INSTALLER, '@echo off\nvcpkg install opencl --triplet=x64-windows\n')
        self.assertRejected('install_opencl_backend_deps.bat:2 installs into x64-windows rather than '
                            'x64-windows-opencl')

    def test_an_installer_that_names_no_triplet_fails(self):
        self.write(optionality.OPT_IN_INSTALLER, '@echo off\nvcpkg install opencl\n')
        self.assertRejected('install_opencl_backend_deps.bat:2 names no triplet')

    def test_an_installer_that_installs_nothing_fails(self):
        self.write(optionality.OPT_IN_INSTALLER, '@echo off\n')
        self.assertRejected('install_opencl_backend_deps.bat: installs nothing')

    def test_a_missing_opt_in_installer_fails(self):
        (self.root / optionality.OPT_IN_INSTALLER).unlink()
        self.assertRejected('install_opencl_backend_deps.bat: not found')


class PremakeGateTests(OptionalityCase):
    """Both workspaces generate the backend only when it is asked for by name."""

    def test_a_backend_enabled_unless_switched_off_fails(self):
        self.write(optionality.PREMAKE_DEFINITIONS[0],
                   'local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") ~= "0"\n')
        self.assertRejected('afternight-pm5.lua does not gate the GPU backend on AFTERNIGHT_OPENCL=1')

    def test_the_test_workspace_needs_the_same_gate(self):
        self.write(optionality.PREMAKE_DEFINITIONS[1], 'local common = require "afternight-common"\n')
        self.assertRejected('afternight-test-pm5.lua does not gate the GPU backend on AFTERNIGHT_OPENCL=1')


class RepositoryTests(unittest.TestCase):
    def test_the_repository_passes(self):
        stream = io.StringIO()
        code = optionality.main([], stream=stream)
        self.assertEqual(code, 0, stream.getvalue())


if __name__ == '__main__':
    unittest.main(verbosity=2)

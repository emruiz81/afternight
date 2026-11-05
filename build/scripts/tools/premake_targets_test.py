#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for build/premake/afternight-targets.lua and the isolated test factory, run through the real generator.

Every case copies the Premake definitions, the registries and the scripts into a throwaway tree beside links to this
checkout's sources, edits the copy, and runs the repository's own Premake binary for GNU Make there - so what is
proved is what the generator does with the definitions this checkout holds, and nothing is written outside the
temporary tree. A host without the Premake binary skips the suite rather than passing it.
"""

from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import time
import unittest

import check_build_graph as build_graph
import consumer_targets as targets

REPO_ROOT = targets.REPO_ROOT
PREMAKE = REPO_ROOT / 'tools' / ('premake5.exe' if os.name == 'nt' else 'premake5')
ISOLATED_LIST = '''
sources {name}
    unit     src/AfterNightTests/Test_SystemInformationPanel/TestMain.cpp
    unit     src/AfterNightTests/Test_SystemInformationPanel/Test_SystemMemoryInfoParser_Linux.cpp only=linux
    unit     src/AfterNightTests/Test_SystemInformationPanel/Test_SystemMemoryInfoParser_Windows.cpp only=windows
    unit     src/AfterNightControls/controls/SystemMemoryInfoParser.cpp
    header   src/AfterNightControls/controls/SystemMemoryInfoParser.h
    header   src/AfterNightControls/AfterNightControlsExport.h
    include  src/AfterNightControls
'''
ISOLATED_TARGET = '''
target {name}
    workspace    tests
    kind         console
    class        {engine_class}
    state        active
    depends      {depends}
    sources      @{name}
    fixture      {engine_class}
'''


def link_directory(source: pathlib.Path, destination: pathlib.Path) -> None:
    """Make `destination` show `source`: a symbolic link, a Windows junction where links need a privilege, or a copy."""
    try:
        os.symlink(source, destination, target_is_directory=True)
        return
    except OSError:
        pass
    if os.name == 'nt':
        try:
            import _winapi
            _winapi.CreateJunction(str(source), str(destination))
            return
        except (ImportError, OSError):
            pass
    shutil.copytree(source, destination)


@unittest.skipUnless(PREMAKE.is_file(), 'the Premake binary is not in tools/ on this host')
class GeneratorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        (self.root / 'build').mkdir()
        (self.root / 'tools').mkdir()
        (self.root / '3rdparty').mkdir()
        shutil.copytree(REPO_ROOT / 'build/premake', self.root / 'build/premake')
        shutil.copytree(REPO_ROOT / 'build/scripts', self.root / 'build/scripts',
                        ignore=shutil.ignore_patterns('__pycache__'))
        shutil.copy(PREMAKE, self.root / 'tools' / PREMAKE.name)
        link_directory(REPO_ROOT / 'src', self.root / 'src')
        link_directory(REPO_ROOT / '3rdparty/src', self.root / '3rdparty/src')

    def read(self, relative: str) -> tuple[str, str]:
        """(the file's text with LF endings, the ending it is written with)."""
        text = (self.root / relative).read_bytes().decode('utf-8')
        return text.replace('\r\n', '\n'), '\r\n' if '\r\n' in text else '\n'

    def edit(self, relative: str, old: str, new: str) -> None:
        text, ending = self.read(relative)
        self.assertIn(old, text)
        (self.root / relative).write_bytes(text.replace(old, new, 1).replace('\n', ending).encode('utf-8'))

    def append(self, relative: str, addition: str) -> None:
        text, ending = self.read(relative)
        (self.root / relative).write_bytes((text + addition).replace('\n', ending).encode('utf-8'))

    def block(self, name: str) -> str:
        text, _ = self.read(targets.REGISTRY)
        start = text.index(f'target {name}\n')
        end = text.find('\n\n', start)
        return text[start:end + 1]

    def set_state(self, name: str, state: str) -> None:
        block = self.block(name)
        current = re.search(r'state +(\S+)', block).group(1)
        self.edit(targets.REGISTRY, block, block.replace(f'state        {current}', f'state        {state}'))

    def generate(self, environment=None) -> subprocess.CompletedProcess:
        run = subprocess.run([str(self.root / 'tools' / PREMAKE.name), '--cc=clang', '--os=linux', 'gmake'],
                             cwd=self.root / 'build/premake', capture_output=True, text=True,
                             env=dict(os.environ, **(environment or {})))
        if run.returncode == 0:
            stamp = self.root / 'build/make-linux/.premake-generated'
            stamp.write_text('')
            later = time.time() + 5
            os.utime(stamp, (later, later))
        return run

    def assertGenerated(self, run: subprocess.CompletedProcess) -> None:
        self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr[-2000:])

    def test_the_checked_in_definitions_generate_what_the_registry_holds(self) -> None:
        self.assertGenerated(self.generate())
        self.assertEqual(targets.check(self.root), [])

    def test_a_project_the_registry_does_not_declare_stops_the_generator(self) -> None:
        self.edit(targets.REGISTRY, self.block('Test_Widgets'), '')
        run = self.generate()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('Test_Widgets is not declared', run.stdout + run.stderr)

    def test_an_engine_flag_stops_the_generator(self) -> None:
        run = self.generate({'AFTERNIGHT_PRODUCT_ENGINE': 'engine'})
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('AFTERNIGHT_PRODUCT_ENGINE is set', run.stdout + run.stderr)

    def test_the_fixture_executables_link_their_class_the_platform_library_and_qt(self) -> None:
        self.assertGenerated(self.generate())
        tests = self.root / 'build/make-linux/tests'
        names = ['Test_FixtureNeutral', 'Test_FixtureEngine']
        projects = {name: build_graph.parse_make_project(str(tests / f'{name}.make')) for name in names}
        for name, project in projects.items():
            self.assertIn('-lAfterNightPlatform', project.links, name)
            self.assertEqual(project.links.count('-lAfterNightPlatform'), 1, name)
            self.assertTrue(any('Qt6Widgets' in entry or 'QtWidgets' in entry
                                for entry in project.links + project.include_dirs), name)
        self.assertNotIn('-lAfterNightEngine', projects['Test_FixtureNeutral'].links)
        self.assertIn('-lAfterNightEngine', projects['Test_FixtureEngine'].links)

    # GNU ld resolves a symbol only from a library named on its command line, so an executable that calls oneTBB
    # directly fails to link on Linux unless its list names it; MSVC links it through the header.
    def test_a_suite_that_calls_oneTBB_directly_links_it_on_linux(self) -> None:
        self.assertGenerated(self.generate())
        project = build_graph.parse_make_project(
            str(self.root / 'build/make-linux/tests/Test_ImageIntegration.make'))
        self.assertIn('-ltbb', project.links)

    def test_a_list_linking_a_library_the_factory_does_not_know_stops_the_generator(self) -> None:
        text, _ = self.read(targets.SOURCE_LISTS)
        start = text.index('sources Test_FixtureNeutral\n')
        block = text[start:text.index('\n\n', start)]
        self.edit(targets.SOURCE_LISTS, block,
                  block.replace('    link     AfterNightPlatform\n', '    link     AfterNightMissing\n'))
        run = self.generate()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('links AfterNightMissing, which the isolated factory does not know how to link',
                      run.stdout + run.stderr)

    def test_the_factory_links_the_engine_of_each_class_and_compiles_only_the_list(self) -> None:
        isolated = [('Test_IsolatedEngine', 'engine', 'AfterNightEngine')]
        for name, engine_class, depends in isolated:
            self.append(targets.SOURCE_LISTS, ISOLATED_LIST.format(name=name))
            self.append(targets.REGISTRY, ISOLATED_TARGET.format(name=name, engine_class=engine_class,
                                                                 depends=depends))
            self.edit('build/premake/afternight-test-pm5.lua',
                      'addIsolatedTestProject { name = "Test_SystemInformationPanel" }',
                      'addIsolatedTestProject { name = "Test_SystemInformationPanel" }\n'
                      f'    addIsolatedTestProject {{ name = "{name}" }}')
        self.assertGenerated(self.generate())
        tests = self.root / 'build/make-linux/tests'
        isolated_project = build_graph.parse_make_project(str(tests / 'Test_IsolatedEngine.make'))
        self.assertIn('-lAfterNightEngine', isolated_project.links)
        self.assertTrue(any(directory.endswith('src/AfterNightEngine/include')
                            for directory in isolated_project.include_dirs))
        projects = [isolated_project]
        for project in projects:
            self.assertEqual(sorted(pathlib.PurePosixPath(source).name for source in project.sources),
                             ['SystemMemoryInfoParser.cpp', 'TestMain.cpp', 'Test_SystemMemoryInfoParser_Linux.cpp'])
        self.assertEqual(targets.check(self.root), [])

    # A libFuzzer generation that instruments only the targets leaves every library they hand an input to blind: no
    # coverage counter and no sanitizer, so the fuzzer keeps a corpus of one byte and finds a defect in a library only
    # when it crashes outright.
    def test_a_libfuzzer_generation_instruments_every_library_a_target_reaches_for_coverage(self) -> None:
        self.assertGenerated(self.generate({'AFTERNIGHT_FUZZ': '1'}))
        main = self.root / 'build/make-linux'
        for name in ('AfterNightEngine', 'AfterNightPlatform', 'AfterNightConsumerRuntime', 'AfterNightControls',
                     'AfterNightProcesses', 'AfterNightExtensionProtocol'):
            text = (main / f'{name}.make').read_text(encoding='utf-8')
            self.assertIn('-fsanitize=fuzzer-no-link,address,undefined', text, name)
            self.assertIn('obj/sanitize-fuzzer/', text, name)
        for name in ('Fuzz_EngineCodec', 'Fuzz_ConsumerDocuments', 'Fuzz_ConsumerProtocol'):
            text = (main / 'tests' / f'{name}.make').read_text(encoding='utf-8')
            self.assertIn('-fsanitize=fuzzer,address,undefined', text, name)
            self.assertNotIn('FuzzDriver.cpp', text, name)
        self.assertEqual(targets.check(self.root), [])

    def test_a_libfuzzer_generation_asked_for_beside_a_sanitizer_generation_stops_the_generator(self) -> None:
        run = self.generate({'AFTERNIGHT_FUZZ': '1', 'AFTERNIGHT_SANITIZE': 'thread'})
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('AFTERNIGHT_FUZZ=1 and AFTERNIGHT_SANITIZE', run.stdout + run.stderr)


    # A Linux binary that links Qt finds it where the deployment puts it, beside itself in lib/, and names no directory of
    # the machine that built it: the build host's Qt directory in a run path is one no other machine has, and on the
    # build host it can satisfy an import the package does not carry.
    def test_a_linux_binary_linking_qt_finds_it_in_its_deployment_and_nowhere_else(self) -> None:
        self.assertGenerated(self.generate())
        linking_qt = 0
        for project in (self.root / 'build/make-linux').rglob('*.make'):
            text = project.read_text(encoding='utf-8')
            paths = re.findall(r'-Wl,-rpath,(\S+)', text)
            for found in paths:
                for entry in found.strip("'").replace("':'", ':').split(':'):
                    self.assertTrue(entry.strip("'").startswith('$$ORIGIN'), f'{project.name}: {found}')
            if '-lQt6Core' in text:
                linking_qt += 1
                self.assertTrue(any('$$ORIGIN/lib' in found for found in paths), project.name)
        self.assertGreater(linking_qt, 0)

    # GNU Make relinks a binary when something it links is newer, and Premake rewrites a project's makefile only when its
    # text changes, so a link that depends on its own makefile is done again exactly when the link line can have changed.
    # Without it a changed option alone - a run path - leaves the binary linked the old way, and a package made from that
    # tree ships it.
    def test_a_linux_binary_is_linked_again_when_its_makefile_changes(self) -> None:
        self.assertGenerated(self.generate())
        linked = 0
        for project in (self.root / 'build/make-linux').rglob('*.make'):
            text = project.read_text(encoding='utf-8')
            if '$(TARGET): $(GENERATED) $(OBJECTS) $(LDDEPS)' not in text:
                continue
            linked += 1
            # Premake states what every configuration shares once, before the first configuration's block.
            line = f'LDDEPS += {project.name}\n'
            shared = text.split('ifeq ($(config),', 1)[0]
            self.assertTrue(line in shared or text.count(line) == text.count('TARGETDIR = '), project.name)
        self.assertGreater(linked, 0)

if __name__ == '__main__':
    unittest.main()

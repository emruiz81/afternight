#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for active_targets.py.

The build cases plan against the planted repository of consumer_targets_test.py, whose registry and generated tree
agree, and read the commands rather than running them. The deployment and output cases plant a binary directory of
empty files and read each file's imports from a table instead of from a symbol reader, so every closure is exactly
what the case states.
"""

from __future__ import annotations

import io
import json
import os
import pathlib
import unittest
import unittest.mock
from contextlib import redirect_stdout

import active_targets as active
import consumer_targets_test as planted

IMPORTS = {
    'Test_Platform': ['libAfterNightPlatform.so', 'libc.so.6'],
    'libAfterNightPlatform.so': ['libstdc++.so.6'],
    'libbundled.so.1': [],
    'Test_EngineCore': ['libAfterNightEngine.so'],
    'libAfterNightEngine.so': ['libbundled.so.1'],
    'libAfterNightControls.so': ['libAfterNightEngine.so', 'libAfterNightPlatform.so'],
    'AfterNight': ['libAfterNightControls.so', 'libAfterNightEngine.so', 'libAfterNightPlatform.so'],
    'Test_Widgets': ['libAfterNightEngine.so', 'libAfterNightPlatform.so'],
    'Test_SystemInformationPanel': [],
}

# A planted target the registry no longer builds, and its outputs as a host that built it before it went still holds
# them.
RETIRED = 'Test_Gone'
RETIRED_OUTPUTS = ['bin/msvc/release/Test_Gone.exe', 'bin/msvc/debug/Test_Gone_d.pdb', 'bin/clang/release/Test_Gone',
                   'bin/clang/debug/Test_Gone_d.debug']


class ActiveCase(planted.PlantedCase):
    def setUp(self) -> None:
        super().setUp()
        self.imports = {name: list(names) for name, names in IMPORTS.items()}
        self.bin = self.root / 'bin/clang/release'

    def retire(self, *names: str) -> None:
        """Adds `names` to the targets the registry no longer builds, for this case alone."""
        patcher = unittest.mock.patch.object(active, 'RETIRED_TARGETS', active.RETIRED_TARGETS + names)
        patcher.start()
        self.addCleanup(patcher.stop)

    def reader(self, path: pathlib.Path):
        return self.imports.get(path.name)

    def plant_binaries(self, names=None) -> None:
        for name in names or self.imports:
            relative = ('lib/' + name) if name == 'libbundled.so.1' else name
            path = self.bin / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode('utf-8'))

    def plant_files(self, relatives, content: bytes = b'built') -> None:
        for relative in relatives:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)


class BuildTest(ActiveCase):
    def test_the_build_names_the_active_targets_of_each_workspace_in_order(self) -> None:
        self.plant()
        commands = active.build_commands(self.root, 'release', with_tests=True, platform='linux', jobs=4)
        self.assertEqual(commands[0][:5], ['make', '-C', str(self.root / 'build/make-linux'), '-j4',
                                           'config=release_x64'])
        self.assertLess(commands[0].index('AfterNightPlatform'), commands[0].index('AfterNightControls'))
        self.assertLess(commands[0].index('AfterNightEngine'), commands[0].index('AfterNightControls'))
        self.assertLess(commands[0].index('AfterNightControls'), commands[0].index('AfterNightApp'))
        self.assertEqual(commands[1][2], str(self.root / 'build/make-linux/tests'))
        self.assertIn('Test_SystemInformationPanel', commands[1])

    def test_without_tests_only_the_main_workspace_is_built(self) -> None:
        self.plant()
        self.assertEqual(len(active.build_commands(self.root, 'release', with_tests=False, platform='linux', jobs=1)), 1)

    def test_the_windows_build_names_projects_to_msbuild(self) -> None:
        self.plant()
        commands = active.build_commands(self.root, 'debug', with_tests=True, platform='windows', jobs=1)
        self.assertEqual(commands[0][0], 'msbuild')
        self.assertTrue(commands[0][1].endswith(os.path.join('vs2026-msvc', 'AfterNight.slnx')))
        self.assertIn('/p:Configuration=Debug;Platform=x64', commands[0])
        named = next(argument[3:].split(';') for argument in commands[0] if argument.startswith('/t:'))
        self.assertLess(named.index('AfterNightPlatform'), named.index('AfterNightControls'))

    def test_a_tree_the_registry_does_not_hold_is_refused_before_anything_builds(self) -> None:
        self.projects['Test_Stray'] = ['tests', 'Test_Stray', [], [], []]
        self.plant()
        with self.assertRaisesRegex(active.RefusedError, 'Test_Stray is generated'):
            active.build_commands(self.root, 'release', with_tests=True, platform='linux', jobs=1)


class DeployTest(ActiveCase):
    def deploy(self, **options):
        self.plant()
        destination = self.root / 'deploy/out'
        arguments = dict(config='release', platform='linux', destination=destination, read_imports=self.reader,
                         commit='0123abc')
        arguments.update(options)
        return active.deploy(self.root, **arguments), destination

    def test_a_deployment_holds_the_targets_and_their_first_party_closure(self) -> None:
        self.plant_binaries()
        manifest, destination = self.deploy(targets=['Test_Widgets'])
        self.assertTrue((destination / 'Test_Widgets').is_file())
        self.assertTrue((destination / 'libAfterNightEngine.so').is_file())
        self.assertTrue((destination / 'libAfterNightPlatform.so').is_file())
        self.assertTrue((destination / 'lib/libbundled.so.1').is_file())
        self.assertFalse((destination / 'Test_Platform').exists())
        self.assertIn('libstdc++.so.6', manifest['resolvedBySystem'])
        written = json.loads((destination / active.MANIFEST).read_text())
        self.assertEqual(written['files']['Test_Widgets']['sha256'], manifest['files']['Test_Widgets']['sha256'])
        self.assertEqual(written['targets'], ['Test_Widgets'])
        self.assertEqual(written['commit'], '0123abc')

    def test_a_profile_deploys_every_target_it_ships(self) -> None:
        self.plant_binaries()
        manifest, destination = self.deploy(profile='product')
        self.assertEqual(manifest['targets'], ['AfterNightApp', 'AfterNightControls', 'AfterNightPlatform'])
        self.assertTrue((destination / 'AfterNight').is_file())

    def test_a_non_empty_destination_is_refused_unless_it_is_replaced(self) -> None:
        self.plant_binaries()
        destination = self.root / 'deploy/out'
        destination.mkdir(parents=True)
        (destination / 'stale.so').write_text('old')
        with self.assertRaisesRegex(active.RefusedError, 'is not empty'):
            self.deploy(targets=['Test_Platform'])
        self.deploy(targets=['Test_Platform'], replace=True)
        self.assertFalse((destination / 'stale.so').exists())

    def test_a_binary_of_the_wrong_generation_is_refused(self) -> None:
        self.imports['Test_Platform'] = ['libAfterNightPlatform.so', 'libAfterNightEngine.so']
        self.plant_binaries()
        with self.assertRaisesRegex(active.RefusedError,
                                    'Test_Platform is class none and its import closure holds '
                                    'libAfterNightEngine.so'):
            self.deploy(targets=['Test_Platform'])

    def test_a_target_that_was_never_built_is_refused(self) -> None:
        self.plant_binaries([name for name in self.imports if name != 'Test_Platform'])
        with self.assertRaisesRegex(active.RefusedError, 'Test_Platform has no binary'):
            self.deploy(targets=['Test_Platform'])


class OutputsTest(ActiveCase):
    def findings(self):
        self.plant()
        return active.output_findings(self.root, self.bin, 'linux', read_imports=self.reader)

    def test_a_clean_directory_has_no_finding(self) -> None:
        self.plant_binaries()
        self.assertEqual(self.findings()[0], [])

    def test_an_output_of_the_wrong_generation_is_stale(self) -> None:
        self.imports['Test_Platform'] = ['libAfterNightPlatform.so', 'libAfterNightEngine.so']
        self.plant_binaries()
        problems, _ = self.findings()
        self.assertTrue(any('Test_Platform: Test_Platform is class none and its import closure holds '
                            'libAfterNightEngine.so' in p for p in problems), problems)

    def test_a_deployment_that_changed_after_it_was_written_is_stale(self) -> None:
        self.plant_binaries()
        _, destination = DeployTest.deploy(self, targets=['Test_Platform'])
        (destination / 'Test_Platform').write_bytes(b'rebuilt in place')
        (destination / 'extra.so').write_bytes(b'copied by hand')
        problems, _ = active.output_findings(self.root, destination, 'linux', read_imports=self.reader)
        self.assertTrue(any('Test_Platform differs from what the deployment recorded' in p for p in problems), problems)
        self.assertTrue(any('extra.so is not in the deployment record' in p for p in problems), problems)

    def test_the_command_line_names_what_it_read(self) -> None:
        self.plant_binaries()
        self.plant()
        stream = io.StringIO()
        with redirect_stdout(stream):
            code = active.main(['outputs', '--binaries', str(self.bin), '--repo-root', str(self.root)],
                               read_imports=self.reader)
        self.assertEqual(code, 0, stream.getvalue())
        self.assertIn('binaries of', stream.getvalue())


class InvalidationTest(ActiveCase):
    def setUp(self) -> None:
        super().setUp()
        self.retire(RETIRED)

    def test_invalidation_removes_what_a_retired_target_staged(self) -> None:
        self.plant()
        kept = ['bin/msvc/release/Test_GoneNotes.txt', 'bin/msvc/release/Test_Platform.exe']
        self.plant_files(RETIRED_OUTPUTS + kept)
        removed = active.invalidate(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in removed), sorted(RETIRED_OUTPUTS))
        for relative in kept:
            self.assertTrue((self.root / relative).exists(), relative)

    # The retired targets are the ones the list names and the registry does not declare: a name the registry
    # declares again is a target it builds, whose outputs are current.
    def test_a_retired_name_the_registry_declares_again_is_not_retired(self) -> None:
        self.retire('Test_Platform')
        self.plant()
        staged = ['bin/msvc/release/Test_Gone.exe', 'bin/clang/debug/Test_Gone_d', 'lib/msvc/Test_Gone.lib']
        kept = ['bin/msvc/release/Test_Platform.exe', 'lib/msvc/AfterNightPlatform.lib']
        self.plant_files(staged + kept)
        registry = planted.targets.read_registry(self.root)
        self.assertEqual(active.retired_targets(registry),
                         ['AfterNightEngineBenchCompare', 'Test_FixtureNext', 'Test_Gone', 'cfitsio', 'libxisf',
                          'pthreads'])
        removed = active.invalidate(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in removed), sorted(staged))
        for relative in kept:
            self.assertTrue((self.root / relative).exists(), relative)

    # The 3rdparty workspace built LibXISF from a vendored tree it no longer holds, into binaries named xisf and xisfd.
    # A host that built it keeps them under 3rdparty/, from where the deploy scripts copy every library beside the
    # product, and in the configurations they were deployed to; every copy goes, under the names the binaries took.
    def test_invalidation_removes_a_retired_vendored_library_wherever_it_was_built_or_deployed(self) -> None:
        self.plant()
        staged = ['3rdparty/bin/msvc/release/xisf.dll', '3rdparty/lib/msvc/release/xisf.lib',
                  '3rdparty/lib/msvc/release/xisf.exp', '3rdparty/bin/msvc/debug/xisfd.dll',
                  '3rdparty/bin/msvc/debug/xisfd.pdb', '3rdparty/lib/msvc/debug/xisfd.lib',
                  '3rdparty/bin/clang/release/libxisf.so', '3rdparty/bin/clang/debug/libxisfd.so',
                  'bin/msvc/release/xisf.dll', 'bin/msvc/debug/xisfd.dll', 'bin/msvc/debug/xisfd.pdb',
                  'bin/clang/release/libxisf.so', 'bin/clang/debug/libxisfd.so']
        kept = ['3rdparty/bin/msvc/release/cgmic.dll', '3rdparty/lib/msvc/release/cgmic.lib',
                'bin/msvc/release/openxisf-1.1.dll', 'bin/msvc/debug/openxisf-1.1d.dll',
                'bin/clang/release/libcgmic.so']
        left = ['build/vs2026-msvc/3rdparty/libxisf.vcxproj', 'build/make-linux/3rdparty/libxisf.make']
        self.plant_files(staged + kept)
        self.plant_files(left, b'generated')
        registry = planted.targets.read_registry(self.root)
        self.assertIn('libxisf', active.retired_targets(registry))
        removed = active.invalidate(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in removed), sorted(staged))
        for relative in kept:
            self.assertTrue((self.root / relative).exists(), relative)
        projects = active.remove_retired_projects(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in projects), sorted(left))

    # CFITSIO, and the threads library only CFITSIO loaded, were deployed from vcpkg beside the binaries until OpenFITS
    # took CFITSIO's place behind the FITS codec. A host that deployed them keeps the copies in every configuration
    # they went to; every copy goes, and OpenFITS's own library stays.
    def test_invalidation_removes_the_retired_fits_library_and_its_threads_library_where_they_were_deployed(
            self) -> None:
        self.plant()
        staged = ['bin/msvc/release/cfitsio.dll', 'bin/msvc/debug/cfitsio.dll', 'bin/msvc/release/pthreadVC3.dll',
                  'bin/msvc/debug/pthreadVC3d.dll']
        kept = ['bin/msvc/release/openfits-1.0.dll', 'bin/msvc/debug/openfits-1.0d.dll',
                'bin/clang/release/libopenfits.so.1.0', 'bin/msvc/release/z.dll']
        self.plant_files(staged + kept)
        registry = planted.targets.read_registry(self.root)
        self.assertIn('cfitsio', active.retired_targets(registry))
        self.assertIn('pthreads', active.retired_targets(registry))
        removed = active.invalidate(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in removed), sorted(staged))
        for relative in kept:
            self.assertTrue((self.root / relative).exists(), relative)

    # When the engine became the only one, the fixture suite named after the development engine took the engine's
    # name and the runner that compared the two engines went. A clone generated and built before keeps their projects,
    # which the build refuses, and their binaries, which the runners would execute as current suites.
    def test_invalidation_removes_what_the_one_engine_renamed_or_retired(self) -> None:
        self.plant()
        staged = ['bin/clang/release/Test_FixtureNext', 'bin/clang/release/Test_FixtureNext.debug',
                  'bin/clang/debug/Test_FixtureNext_d', 'bin/msvc/release/Test_FixtureNext.exe',
                  'bin/msvc/debug/Test_FixtureNext_d.pdb', 'bin/clang/release/AfterNightEngineBenchCompare',
                  'bin/msvc/release/AfterNightEngineBenchCompare.exe',
                  'bin/msvc/debug/AfterNightEngineBenchCompare_d.exe']
        kept = ['bin/clang/release/Test_FixtureEngine', 'bin/clang/release/AfterNightEngineBench',
                'bin/msvc/release/Test_FixtureEngine.exe']
        left = ['build/make-linux/tests/Test_FixtureNext.make',
                'build/make-linux/tests/AfterNightEngineBenchCompare.make',
                'build/vs2026-msvc/tests/Test_FixtureNext.vcxproj',
                'build/vs2026-msvc/tests/AfterNightEngineBenchCompare.vcxproj.filters']
        self.plant_files(staged + kept)
        self.plant_files(left, b'generated')
        registry = planted.targets.read_registry(self.root)
        self.assertIn('Test_FixtureNext', active.retired_targets(registry))
        self.assertIn('AfterNightEngineBenchCompare', active.retired_targets(registry))
        removed = active.invalidate(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in removed), sorted(staged))
        for relative in kept:
            self.assertTrue((self.root / relative).exists(), relative)
        projects = active.remove_retired_projects(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in projects), sorted(left))

    def test_invalidation_removes_the_generated_projects_a_retired_target_left(self) -> None:
        self.plant()
        left = ['build/make-linux/tests/Test_Gone.make',
                'build/codelite-linux/tests/Test_Gone.project',
                'build/vs2026-msvc/tests/Test_Gone.vcxproj',
                'build/vs2026-msvc/tests/Test_Gone.vcxproj.filters',
                'build/vs2026-msvc/tests/Test_Gone.vcxproj.user',
                'build/vs2026-clang/tests/Test_Gone.vcxproj',
                'build/vs2026-clang/tests/Test_Gone.vcxproj.filters']
        kept = ['build/make-linux/tests/Test_GoneNotes.make',
                'build/codelite-linux/tests/Test_Platform.project', 'build/vs2026-msvc/tests/Test_Platform.vcxproj',
                'build/vs2026-clang/tests/Test_Platform.vcxproj', 'build/vs2026-msvc/tests/Test_Gone.txt']
        self.plant_files(left + kept, b'generated')
        self.assertTrue(any('Test_Gone is generated' in problem for problem in planted.targets.check(self.root)))
        dry = active.remove_retired_projects(self.root, dry_run=True)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in dry), sorted(left))
        for relative in left:
            self.assertTrue((self.root / relative).exists(), relative)
        removed = active.remove_retired_projects(self.root)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in removed), sorted(left))
        for relative in left:
            self.assertFalse((self.root / relative).exists(), relative)
        for relative in kept:
            self.assertTrue((self.root / relative).exists(), relative)
        self.assertFalse(any('Test_Gone' in problem for problem in planted.targets.check(self.root)))

    def test_the_command_line_says_which_generated_projects_it_removed(self) -> None:
        self.plant()
        path = self.root / 'build/make-linux/tests/Test_Gone.make'
        path.write_bytes(b'generated')
        stream = io.StringIO()
        with redirect_stdout(stream):
            code = active.main(['invalidate', '--repo-root', str(self.root)])
        self.assertEqual(code, 0, stream.getvalue())
        self.assertIn('build/make-linux/tests/Test_Gone.make', stream.getvalue())
        self.assertIn('removed 1 generated project file(s) of retired targets', stream.getvalue())
        self.assertFalse(path.exists())

    def test_a_dry_run_of_the_invalidation_removes_nothing(self) -> None:
        self.plant()
        self.plant_binaries()
        self.plant_files(['bin/clang/release/Test_Gone'])
        removed = active.invalidate(self.root, dry_run=True)
        self.assertEqual([path.name for path in removed], ['Test_Gone'])
        self.assertTrue((self.bin / 'Test_Gone').exists())

    def test_the_command_line_invalidates_and_says_what_it_removed(self) -> None:
        self.plant_binaries()
        self.plant()
        self.plant_files(['bin/clang/release/Test_Gone'])
        stream = io.StringIO()
        with redirect_stdout(stream):
            code = active.main(['invalidate', '--repo-root', str(self.root)])
        self.assertEqual(code, 0, stream.getvalue())
        self.assertIn('bin/clang/release/Test_Gone', stream.getvalue())
        self.assertIn('removed 1 staged output', stream.getvalue())
        self.assertFalse((self.bin / 'Test_Gone').exists())


if __name__ == '__main__':
    unittest.main()

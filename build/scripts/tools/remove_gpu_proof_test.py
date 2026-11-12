# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for remove_gpu_proof.

Run with:
    python build/scripts/tools/remove_gpu_proof_test.py

Most cases drive the removal over a throwaway tree and a manifest written for it, so what they prove
is the mechanism: an anchor that does not resolve is refused rather than guessed at, a mention left
behind is a finding, and applying a plan writes exactly what checking it computed. The last cases
read the repository and its own manifest, because a procedure that only ever runs on synthetic input
proves nothing about the tree it exists to remove.
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

import remove_gpu_proof as removal  # noqa: E402


BACKEND_SOURCE = 'const char* kName = "OpenCL proof backend";\n'

WORKSPACE = (
    'local common = require "common"\n'
    '-- The OpenCL proof backend is generated only when asked for.\n'
    'local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") == "1"\n'
    '\n'
    'workspace "AfterNight"\n'
    '    project "Engine"\n'
)

CHECKER = (
    'ADAPTERS = {\n'
    "    'AfterNightEngineGmicAdapter': 'Test_EngineGmicAdapter',\n"
    '}\n'
    '\n'
    'BACKENDS = {\n'
    "    'AfterNightEngineOpenClBackend': 'Test_EngineOpenClBackend',\n"
    '}\n'
    '\n'
    'SDK_LIBRARIES = ("cudart",)\n'
)

GUARD = 'SDK_LIBRARIES = ("opencl", "cudart")\n'


class RemovalCase(unittest.TestCase):
    """A tree whose every declared path and anchor is where the manifest says it is."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-remove-gpu-proof-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.write('src/Backend/Backend.cpp', BACKEND_SOURCE)
        self.write('src/Backend/Backend.h', BACKEND_SOURCE)
        self.write('build/premake/workspace.lua', WORKSPACE)
        self.write('build/scripts/check_graph.py', CHECKER)
        self.write('build/scripts/guard.py', GUARD)
        self.write('AGENTS.md', 'The OpenCL proof backend is opt-in.\n')
        self.write('docs/record.md', 'The device seam was proved on OpenCL.\n')

        self.removed_paths = ('src/Backend',)
        self.edits = (
            removal.Edit('build/premake/workspace.lua',
                         start='-- The OpenCL proof backend is generated only when asked for.',
                         before='workspace "AfterNight"',
                         removes=3,
                         replacement=('',)),
            removal.Edit('build/scripts/check_graph.py',
                         start='BACKENDS = {',
                         before='SDK_LIBRARIES = ("cudart",)',
                         removes=4,
                         replacement=('BACKENDS = {}', '')),
        )
        self.manifest = dict(removed_paths=self.removed_paths, edits=self.edits,
                             scan_exempt=('build/scripts/guard.py',),
                             documentation_records=('docs/record.md',),
                             documentation_edits=('AGENTS.md',))

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='')
        return path

    def read(self, relative):
        return (self.root / relative).read_text(encoding='utf-8', newline='')

    def plan(self, **overrides):
        arguments = dict(self.manifest)
        arguments.update(overrides)
        return removal.plan(self.root, **arguments)

    def check(self, **overrides):
        arguments = dict(self.manifest)
        arguments.update(overrides)
        return removal.check(self.root, **arguments)


class PlanTests(RemovalCase):
    def test_a_tree_matching_the_manifest_plans_with_no_problem(self):
        result = self.plan()
        self.assertEqual(result.problems, [])

    def test_the_plan_names_every_file_the_removed_directory_holds(self):
        result = self.plan()
        self.assertEqual(sorted(result.removed), ['src/Backend/Backend.cpp', 'src/Backend/Backend.h'])

    def test_the_plan_rewrites_the_anchored_region_and_nothing_else(self):
        result = self.plan()
        self.assertEqual(result.rewritten['build/premake/workspace.lua'],
                         'local common = require "common"\n'
                         '\n'
                         'workspace "AfterNight"\n'
                         '    project "Engine"\n')
        self.assertEqual(result.rewritten['build/scripts/check_graph.py'],
                         'ADAPTERS = {\n'
                         "    'AfterNightEngineGmicAdapter': 'Test_EngineGmicAdapter',\n"
                         '}\n'
                         '\n'
                         'BACKENDS = {}\n'
                         '\n'
                         'SDK_LIBRARIES = ("cudart",)\n')

    def test_a_region_of_one_line_needs_no_end_anchor(self):
        self.write('build/scripts/one.py', 'keep = 1\nOPENCL = 2\nkeep = 3\n')
        edits = self.edits + (removal.Edit('build/scripts/one.py', start='OPENCL = 2',
                                           before=None, removes=1, replacement=()),)
        result = self.plan(edits=edits)
        self.assertEqual(result.problems, [])
        self.assertEqual(result.rewritten['build/scripts/one.py'], 'keep = 1\nkeep = 3\n')

    def test_two_edits_to_one_file_are_applied_to_the_same_text(self):
        self.write('build/scripts/two.py', 'a = 1\nOPENCL_ONE = 2\nb = 3\nOPENCL_TWO = 4\nc = 5\n')
        edits = self.edits + (
            removal.Edit('build/scripts/two.py', start='OPENCL_ONE = 2', before=None, removes=1,
                         replacement=()),
            removal.Edit('build/scripts/two.py', start='OPENCL_TWO = 4', before=None, removes=1,
                         replacement=()),
        )
        result = self.plan(edits=edits)
        self.assertEqual(result.problems, [])
        self.assertEqual(result.rewritten['build/scripts/two.py'], 'a = 1\nb = 3\nc = 5\n')


class AnchorTests(RemovalCase):
    def test_a_declared_path_that_is_already_gone_is_a_problem(self):
        shutil.rmtree(self.root / 'src/Backend')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('src/Backend', problems[0])

    def test_a_declared_file_that_is_not_there_is_a_problem(self):
        (self.root / 'build/premake/workspace.lua').unlink()
        problems = self.check()
        self.assertTrue(any('build/premake/workspace.lua' in problem for problem in problems), problems)

    def test_a_start_anchor_that_is_not_in_the_file_is_a_problem(self):
        self.write('build/premake/workspace.lua', WORKSPACE.replace('-- The OpenCL proof backend is '
                                                                    'generated only when asked for.\n', ''))
        problems = self.check()
        self.assertTrue(any('start anchor' in problem for problem in problems), problems)

    def test_a_start_anchor_that_appears_twice_is_a_problem(self):
        doubled = WORKSPACE.replace('local openClBackendEnabled',
                                    '-- The OpenCL proof backend is generated only when asked for.\n'
                                    'local openClBackendEnabled')
        self.write('build/premake/workspace.lua', doubled)
        problems = self.check()
        self.assertTrue(any('appears 2 times' in problem for problem in problems), problems)

    def test_an_end_anchor_that_never_follows_the_start_is_a_problem(self):
        self.write('build/premake/workspace.lua', WORKSPACE.replace('workspace "AfterNight"\n', ''))
        problems = self.check()
        self.assertTrue(any('end anchor' in problem for problem in problems), problems)

    def test_an_edit_that_changes_nothing_is_a_problem(self):
        edits = (removal.Edit('build/scripts/guard.py', start='SDK_LIBRARIES = ("opencl", "cudart")',
                              before=None, removes=1,
                              replacement=('SDK_LIBRARIES = ("opencl", "cudart")',)),)
        problems = self.check(edits=edits, removed_paths=(), scan_exempt=('build/scripts/guard.py',))
        self.assertTrue(any('changes nothing' in problem for problem in problems), problems)


class SurvivingMentionTests(RemovalCase):
    def test_a_mention_left_in_a_scanned_file_is_a_problem(self):
        self.write('build/scripts/extra.py', 'SDK = "opencl"\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('build/scripts/extra.py:1', problems[0])

    def test_a_mention_spelled_inside_a_name_is_found(self):
        self.write('build/scripts/extra.py', 'TARGET = "AfterNightEngineOpenClBackend"\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('build/scripts/extra.py:1', problems[0])

    def test_an_open_cluster_is_not_the_proof(self):
        # A catalog's open clusters carry the token's letters and have nothing to do with a GPU.
        self.write('build/scripts/extra.py', 'KINDS = ("openCluster", "OpenCluster", "OPENCLUSTER")\n')
        self.assertEqual(self.check(), [])

    def test_a_line_that_names_an_open_cluster_and_the_proof_is_still_found(self):
        self.write('build/scripts/extra.py', 'PAIR = ("openCluster", "OpenClBackend")\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('build/scripts/extra.py:1', problems[0])

    def test_an_exempt_guard_file_may_name_the_sdk_it_refuses(self):
        self.assertEqual(self.check(), [])

    def test_a_guard_file_that_is_not_exempt_is_a_problem(self):
        problems = self.check(scan_exempt=())
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('build/scripts/guard.py:1', problems[0])

    def test_markdown_under_a_scanned_root_is_prose_and_is_not_scanned(self):
        self.write('src/Suite/README.md', 'A GPU backend could use OpenCL one day.\n')
        self.assertEqual(self.check(), [])

    def test_a_file_the_removal_deletes_is_not_scanned(self):
        self.assertEqual(self.check(), [])

    def test_an_undeclared_documentation_file_that_names_the_proof_is_a_problem(self):
        self.write('docs/guide.md', 'Run with OpenCL enabled.\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('docs/guide.md', problems[0])

    def test_a_declared_documentation_file_is_reported_as_the_prose_step(self):
        result = self.plan()
        self.assertEqual(result.documentation, ['AGENTS.md'])

    def test_a_record_is_retained_and_is_not_a_prose_step(self):
        result = self.plan()
        self.assertNotIn('docs/record.md', result.documentation)


class GeneratedArtifactTests(RemovalCase):
    """Build output the proof leaves behind. None of it is tracked, and a stale project file
    describing a target the workspace no longer generates is read by the build-graph check."""

    def setUp(self):
        super().setUp()
        self.write('build/vs2026-msvc/GpuBackend.vcxproj', '<Project />\n')
        self.write('build/vs2026-msvc/GpuBackend.vcxproj.filters', '<Project />\n')
        self.write('bin/msvc/release/GpuBackend.dll', 'binary')
        self.manifest['generated_globs'] = ('build/vs2026-msvc/GpuBackend.vcxproj*',
                                            'bin/msvc/*/GpuBackend.*',
                                            'obj/GpuBackend')

    def test_the_plan_names_the_generated_artifacts_it_would_sweep(self):
        self.assertEqual(sorted(self.plan().generated),
                         ['bin/msvc/release/GpuBackend.dll',
                          'build/vs2026-msvc/GpuBackend.vcxproj',
                          'build/vs2026-msvc/GpuBackend.vcxproj.filters'])

    def test_a_generated_artifact_that_is_not_there_is_not_a_problem(self):
        self.assertEqual(self.check(), [])

    def test_apply_sweeps_them(self):
        removal.apply(self.root, **self.manifest)
        self.assertFalse((self.root / 'build/vs2026-msvc/GpuBackend.vcxproj').exists())
        self.assertFalse((self.root / 'bin/msvc/release/GpuBackend.dll').exists())

    def test_a_generated_artifact_is_not_scanned_for_a_surviving_mention(self):
        self.write('build/vs2026-msvc/GpuBackend.vcxproj', '<ClCompile Include="OpenCl.cpp" />\n')
        self.assertEqual(self.check(), [])


class RegionSizeTests(RemovalCase):
    """Each region records the number of lines it holds, so what grows inside it is refused."""

    def grow(self):
        self.write('build/premake/workspace.lua',
                   WORKSPACE.replace('local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") == "1"\n',
                                     'local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") == "1"\n'
                                     'local benchmarkLibrary = "EngineBenchCommon.lib"\n'))

    def test_a_line_written_inside_a_region_is_refused(self):
        self.grow()
        problems = self.check()
        self.assertTrue(any('holds 4 line(s) rather than the 3 it records' in problem
                            for problem in problems), problems)

    def test_the_line_written_inside_a_region_survives_the_refused_apply(self):
        self.grow()
        problems = removal.apply(self.root, **self.manifest)
        self.assertNotEqual(problems, [])
        self.assertIn('local benchmarkLibrary', self.read('build/premake/workspace.lua'))

    def test_a_region_that_has_lost_a_line_is_refused_too(self):
        self.write('build/premake/workspace.lua',
                   WORKSPACE.replace('local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") == "1"\n', ''))
        problems = self.check()
        self.assertTrue(any('holds 2 line(s) rather than the 3 it records' in problem
                            for problem in problems), problems)


class ApplyTests(RemovalCase):
    def test_apply_deletes_the_declared_paths(self):
        problems = removal.apply(self.root, **self.manifest)
        self.assertEqual(problems, [])
        self.assertFalse((self.root / 'src/Backend').exists())

    def test_apply_writes_what_the_plan_computed(self):
        expected = self.plan().rewritten
        removal.apply(self.root, **self.manifest)
        for relative, text in expected.items():
            self.assertEqual(self.read(relative), text)

    def test_apply_preserves_each_files_line_endings(self):
        crlf = WORKSPACE.replace('\n', '\r\n')
        self.write('build/premake/workspace.lua', crlf)
        removal.apply(self.root, **self.manifest)
        written = (self.root / 'build/premake/workspace.lua').read_bytes()
        self.assertNotIn(b'\n', written.replace(b'\r\n', b''))

    def test_apply_refuses_a_plan_with_a_problem_and_changes_nothing(self):
        self.write('build/scripts/extra.py', 'SDK = "opencl"\n')
        before = self.read('build/premake/workspace.lua')
        problems = removal.apply(self.root, **self.manifest)
        self.assertNotEqual(problems, [])
        self.assertEqual(self.read('build/premake/workspace.lua'), before)
        self.assertTrue((self.root / 'src/Backend').exists())


class RepositoryTests(unittest.TestCase):
    """The manifest against the repository it was written for."""

    def test_the_manifest_resolves_against_the_repository(self):
        problems = removal.check(removal.REPO_ROOT)
        self.assertEqual(problems, [], '\n'.join(problems))

    def test_checking_the_repository_exits_zero(self):
        stream = io.StringIO()
        self.assertEqual(removal.main([], stream=stream), 0, stream.getvalue())

    def test_the_manifest_removes_the_backend_and_its_suite_whole(self):
        self.assertIn('src/AfterNightEngineOpenClBackend', removal.REMOVED_PATHS)
        self.assertIn('src/AfterNightTests/Test_EngineOpenClBackend', removal.REMOVED_PATHS)

    def test_the_manifest_sweeps_the_generated_projects_of_both_visual_studio_trees(self):
        swept = ' '.join(removal.GENERATED_GLOBS)
        for tree in ('build/vs2026-msvc', 'build/vs2026-clang'):
            self.assertIn('%s/AfterNightEngineOpenClBackend.vcxproj*' % tree, swept)
            self.assertIn('%s/tests/Test_EngineOpenClBackend.vcxproj*' % tree, swept)

    def test_the_manifest_removes_itself(self):
        here = pathlib.Path(__file__).resolve()
        for path in (here.with_name('remove_gpu_proof.py'), here):
            self.assertIn(path.relative_to(removal.REPO_ROOT).as_posix(), removal.REMOVED_PATHS)

    def test_upstream_ffmpeg_opencl_options_are_not_mistaken_for_the_proof(self):
        # The pinned overlay retains upstream package features this build does not select. Those
        # feature declarations do not belong to the AfterNight proof and survive its removal.
        ffmpeg_files = (
            'build/vcpkg/overlay-ports/ffmpeg/0004-dependencies.patch',
            'build/vcpkg/overlay-ports/ffmpeg/portfile.cmake',
            'build/vcpkg/overlay-ports/ffmpeg/vcpkg-cmake-wrapper.cmake',
            'build/vcpkg/overlay-ports/ffmpeg/vcpkg.json',
        )
        planned = removal.plan(removal.REPO_ROOT)
        for relative in ffmpeg_files:
            self.assertIn(relative, removal.SCAN_EXEMPT)
            self.assertTrue((removal.REPO_ROOT / relative).is_file(), relative)
            self.assertNotIn(relative, planned.removed)

    def test_the_safety_register_loses_the_proof_review_and_component(self):
        planned = removal.plan(removal.REPO_ROOT)
        register = planned.rewritten['build/scripts/tools/safety_register.txt']
        checker = planned.rewritten['build/scripts/tools/check_safety_register.py']
        self.assertNotIn('review opencl', register.lower())
        self.assertNotIn('opencl-backend', register.lower())
        self.assertNotIn('opencl-backend', checker.lower())

    def test_every_record_it_keeps_still_names_the_proof(self):
        # A record listed here is left exactly as it is when the proof goes; one that no longer names the
        # proof is a stale entry that would exempt a later mention from the check.
        for kept in removal.DOCUMENTATION_RECORDS:
            text = (removal.REPO_ROOT / kept).read_text(encoding='utf-8')
            self.assertTrue(removal.names_the_proof(text), kept)

    def test_every_record_it_keeps_is_in_the_tree(self):
        for kept in removal.DOCUMENTATION_RECORDS:
            self.assertTrue((removal.REPO_ROOT / kept).exists(), kept)

    def test_the_removal_leaves_the_build_record_the_target_select_script_keeps(self):
        # The region that deletes the two opt-in lines sits directly above the block that records what
        # the tree was generated for, which arrived later; a region reaching past it would take it too.
        script = removal.plan(removal.REPO_ROOT).rewritten['build/scripts/msvc/build_afternightengine.bat']
        self.assertIn('SANITIZER_MARKER=', script)
        self.assertIn('call :append_rebuild', script)
        self.assertNotIn('OpenClBackend', script)

    def test_the_removal_leaves_the_benchmark_library_names_the_test_workspace_links(self):
        # Same shape in the test workspace: the opt-in switch is followed by the names every suite that
        # links the benchmark layer resolves, and by the sweep of the suite's stale project file.
        workspace = removal.plan(removal.REPO_ROOT).rewritten['build/premake/afternight-test-pm5.lua']
        self.assertIn('local sanitizerLibrarySuffix', workspace)
        self.assertIn('local benchCommonReleaseLib', workspace)
        self.assertIn('local benchCommonDebugLib', workspace)
        self.assertNotIn('OpenClBackend', workspace)

    def test_the_removal_keeps_the_packagers_qt_test_exclusion_at_the_deployments_lib(self):
        # The packager keeps out the build's Qt Test where the deployer places it for the suites, anchored there,
        # so the copy the bundled Python's PySide6 carries ships; the line the removal writes drops the backend and
        # keeps that anchor, because an unanchored pattern would strip PySide6's copy again.
        packager = removal.plan(removal.REPO_ROOT).rewritten['installer/linux_packager.sh']
        self.assertIn('--exclude="/lib/libQt6Test*"', packager)
        self.assertNotIn('--exclude="libQt6Test*"', packager)
        self.assertNotIn('OpenClBackend', packager)


    def test_the_header_architecture_check_loses_the_backend_and_keeps_every_other_component(self):
        # It holds all four components on this side of the workspace to one declaration budget, so
        # removing the proof has to take that component and leave the engine and both adapters.
        relative = 'build/scripts/tools/check_header_architecture.py'
        rewritten = removal.plan(removal.REPO_ROOT).rewritten[relative]
        self.assertNotIn('OpenCl', rewritten)
        self.assertIn("Component('AfterNightEngine')", rewritten)
        self.assertIn("Component('AfterNightEnginePythonAdapter')", rewritten)
        self.assertIn("Component('AfterNightEngineGmicAdapter')", rewritten)

    def test_the_seam_the_proof_implements_is_not_removed(self):
        # The procedure keeps the vendor-neutral seam and deletes only the instrument that proved it.
        seam = ('src/AfterNightEngine/include/AfterNightEngine/Algorithms/Kernel/DeviceBackend.h',
                'src/AfterNightEngine/include/AfterNightEngine/Algorithms/Kernel/Device.h',
                'src/AfterNightEngine/include/AfterNightEngine/Algorithms/Kernel/DeviceChain.h',
                'src/AfterNightEngine/include/AfterNightEngine/Runtime/DeviceResource.h',
                'src/AfterNightEngine/include/AfterNightEngine/Runtime/DeviceEvent.h',
                'src/AfterNightTests/Test_EngineKernels/MockDeviceBackend.h')
        planned = removal.plan(removal.REPO_ROOT)
        for header in seam:
            self.assertTrue((removal.REPO_ROOT / header).is_file(), header)
            self.assertNotIn(header, planned.removed)


if __name__ == '__main__':
    unittest.main()

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Delete the opt-in GPU proof backend, and prove the procedure for doing so is still accurate.

The GPU proof is deletable on purpose: it is an instrument for showing that the engine's device seam
can carry a real accelerator, never a part of the engine, and a maintainer has to be able to drop it
without archaeology. This file is that procedure, written as something that runs rather than as a
list to follow by hand, so that it is checked on every local validation instead of being discovered to
be out of date on the day it is used.

What it removes is the instrument. What it keeps is the seam: `Runtime/DeviceResource.h`,
`Runtime/DeviceEvent.h`, `Runtime/DeviceError.h`, `Algorithms/Kernel/DeviceBackend.h`,
`Algorithms/Kernel/Device.h`, `Algorithms/Kernel/DeviceChain.h`, the kernel contracts the proofs
added, and `MockDeviceBackend` in `Test_EngineKernels`, which keeps exercising all of it in every
configuration with no GPU present. The engine never contained GPU code, so nothing of the engine is
touched.

Three kinds of step, and one deliberate omission:

1. paths removed whole - the backend's source tree, its suite, the SDK install script and its vcpkg
   triplet, the checker that guards the opt-in, and this file and its test;
2. anchored regions removed from or rewritten in the build wiring - both Premake workspaces, the
   target-select build script, the Doxygen input, and the entries six checkers and their tests keep
   for the backend. The start anchor of a region must appear exactly once in its file and the end
   anchor is the first line after it, so an edit that no longer matches the tree is refused rather
   than applied to whatever is there now; and
3. `--check`, which computes all of that without writing anything and then reads the result: no file
   the engine or a build is made of may still name the proof. That is what stops the procedure
   rotting, because a new mention added anywhere in `build/` or `src/` fails this check until the
   manifest below covers it.

The omission is prose. Documentation that describes the proof is edited by a person, not by this
script: the files holding it are listed in `DOCUMENTATION_EDITS`, and a record that keeps naming the
proof because it records it would be listed in `DOCUMENTATION_RECORDS` and stay. A documentation file
that names the proof and is in neither list fails the check, so the prose step cannot go stale either.

Usage:
    python build/scripts/tools/remove_gpu_proof.py              # check the procedure against the tree
    python build/scripts/tools/remove_gpu_proof.py --apply      # perform it

Exits 0 when every step resolves and nothing is left behind, 1 otherwise, printing one line per
problem.

Tests: build/scripts/tools/remove_gpu_proof_test.py
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

# How the proof spells itself. A substring rather than a token, because every name it goes by carries
# it - the module, the suite, the switch, the triplet, the export macro - and a word boundary would
# find none of them. `cuda`, `sycl` and the rest are deliberately not here: they belong to
# check_build_graph.py, which stays and refuses a GPU SDK in any generated target, and one of them is
# also an FFmpeg hardware decoder this engine names for reasons that have nothing to do with a GPU
# backend.
PROOF_TOKENS = ('opencl',)

# What carries a token without spelling the proof: an open cluster, the kind of object a catalog
# names, which the product writes as `openCluster` and `OpenCluster`.
PROOF_TOKEN_EXCEPTIONS = ('opencluster',)

# Where a build can be made of something. Markdown under these roots is prose and is read by a
# person, like the documentation below.
SCANNED_ROOTS = ('build', 'src')
PROSE_SUFFIXES = ('.md',)

# Files that may still name the SDK after the proof is gone. The build-graph checker names it because
# refusing any generated target that reaches a GPU SDK is its job. The FFmpeg overlay is an upstream
# package recipe with an unselected OpenCL feature; it neither belongs to the proof nor makes the
# engine reach that SDK. Keep the exemption to the four files that describe that optional feature so
# a new mention anywhere else still has to join the removal manifest.
SCAN_EXEMPT = (
    'build/scripts/tools/check_build_graph.py',
    'build/vcpkg/overlay-ports/ffmpeg/0004-dependencies.patch',
    'build/vcpkg/overlay-ports/ffmpeg/portfile.cmake',
    'build/vcpkg/overlay-ports/ffmpeg/vcpkg-cmake-wrapper.cmake',
    'build/vcpkg/overlay-ports/ffmpeg/vcpkg.json',
)

# Documentation a person edits when the proof goes: each describes the proof in prose that goes with it.
DOCUMENTATION_EDITS = (
    'AGENTS.md',
    'CLAUDE.md',
    'docs/dev/guides/BACKENDS.md',
    'docs/dev/guides/BENCHMARKS.md',
    'docs/dev/guides/BUILD.md',
    'docs/dev/guides/TESTING.md',
)

# Documentation that names the proof because it records it, which removing the proof does not make untrue,
# and which stays exactly as it is. None does today.
DOCUMENTATION_RECORDS = ()

# Everything the proof owns outright.
REMOVED_PATHS = (
    'src/AfterNightEngineOpenClBackend',
    'src/AfterNightTests/Test_EngineOpenClBackend',
    'build/scripts/msvc/install_opencl_backend_deps.bat',
    'build/vcpkg/triplets/x64-windows-opencl.cmake',
    'build/scripts/tools/check_gpu_backend_optionality.py',
    'build/scripts/tools/check_gpu_backend_optionality_test.py',
    'build/scripts/tools/remove_gpu_proof.py',
    'build/scripts/tools/remove_gpu_proof_test.py',
)

# Build output the proof leaves behind, swept whenever it is there and never missed when it is not.
# None of it is tracked, so deleting sources alone would leave a generated project describing a target
# no workspace generates any more - which check_build_graph.py reads and refuses - and a built backend
# in the directory a packager copies from. Premake deletes no project file, and the generation that
# used to remove these goes with the rest of the opt-in, so this sweep is what replaces it. The
# installed SDK is deliberately not here: `vcpkg/installed/x64-windows-opencl` belongs to the vcpkg
# checkout rather than to this repository, and is removed with vcpkg's own tools.
GENERATED_GLOBS = (
    'build/vs2026-msvc/AfterNightEngineOpenClBackend.vcxproj*',
    'build/vs2026-msvc/tests/Test_EngineOpenClBackend.vcxproj*',
    'build/vs2026-clang/AfterNightEngineOpenClBackend.vcxproj*',
    'build/vs2026-clang/tests/Test_EngineOpenClBackend.vcxproj*',
    'bin/*/*/AfterNightEngineOpenClBackend*',
    'bin/*/*/Test_EngineOpenClBackend*',
    'lib/*/AfterNightEngineOpenClBackend*',
    'obj/AfterNightEngineOpenClBackend',
    'obj/AfterNightTests/Test_EngineOpenClBackend',
)


@dataclasses.dataclass(frozen=True)
class Edit:
    """One region of one file, from a line that appears there exactly once.

    `before` is the first line after the start that ends the region, which is not itself replaced;
    `None` makes the region the start line alone. `replacement` is the lines that take its place, so
    an empty tuple deletes it.

    `removes` is how many lines the region holds, and it is checked rather than derived. Two anchors
    say where a region begins and ends but not what is inside it, so a line added between them joins
    what the removal deletes, silently and for as long as nobody removes the proof. That is not
    hypothetical: the block recording which instrumentation a tree was generated for, and the
    benchmark library names every suite links, were each written inside a region whose anchors were
    chosen before they existed, and the removal took both. A recorded size turns that into a refusal.
    """

    path: str
    start: str
    before: str | None
    removes: int
    replacement: tuple[str, ...]


EDITS = (
    # ---- The main workspace: the opt-in switch, the project-file sweep it shares, and the project.
    Edit('build/premake/afternight-pm5.lua',
         start='-- The OpenCL proof backend has the opposite default: it is generated only when AFTERNIGHT_OPENCL=1',
         before='workspace "AfterNight"',
         removes=22,
         replacement=('',)),
    Edit('build/premake/afternight-pm5.lua',
         start='    -- The AfterNightEngineOpenClBackend shared library: the opt-in OpenCL proof backend.',
         before='    -- The CLI interface',
         removes=97,
         replacement=()),

    # ---- The test workspace: the same switch, the external reference, and the suite.
    # The switch and the sweep of the suite's stale project file are two regions rather than one,
    # because what sits between them - the names every suite that links the benchmark layer resolves -
    # belongs to the workspace and arrived after this procedure was written.
    Edit('build/premake/afternight-test-pm5.lua',
         start="-- The opt-in OpenCL proof backend's suite is generated with the backend or not at all, "
               'under the switch',
         before='-- The engine-neutral benchmark layer is the one static library of this workspace that an',
         removes=5,
         replacement=('',)),
    Edit('build/premake/afternight-test-pm5.lua',
         start="-- As in afternight-pm5.lua: a generation without the opt-in removes the suite's project "
               'file an earlier',
         before='workspace "AfterNightTest"',
         removes=13,
         replacement=()),
    Edit('build/premake/afternight-test-pm5.lua',
         start='    -- The opt-in OpenCL proof backend, referenced for its one suite and only in a tree '
               'that asked for it.',
         before='    filter {} -- Clear all filters',
         removes=19,
         replacement=()),
    Edit('build/premake/afternight-test-pm5.lua',
         start="    -- The opt-in OpenCL proof backend's suite, generated with the backend or not at all. "
               'What it covers',
         before='    ----------------------------------------------------------------------------------',
         removes=14,
         replacement=()),

    # ---- The target-select build script, which builds the pair when a solution names them.
    Edit('build/scripts/msvc/build_afternightengine.bat',
         start='REM The opt-in OpenCL proof backend and its suite exist only in a tree generated with',
         before='set "MAIN_TARGETS=AfterNightEngine;AfterNightEnginePythonAdapter;'
                'AfterNightEngineGmicAdapter"',
         removes=4,
         replacement=()),
    # The two opt-in lines end where the block that records the tree's instrumentation begins, which
    # this script needs and which arrived after this procedure was written.
    Edit('build/scripts/msvc/build_afternightengine.bat',
         start='findstr /C:"AfterNightEngineOpenClBackend.vcxproj" '
               '"..\\..\\%PROJECT_TREE%\\AfterNight.slnx" >nul 2>&1 && '
               'set "MAIN_TARGETS=%MAIN_TARGETS%;AfterNightEngineOpenClBackend"',
         before='REM An instrumented generation and an ordinary one write their binaries to one directory and their',
         removes=3,
         replacement=('',)),

    # ---- The provenance register loses the SDK it recorded, and the checker that reads it loses the
    #      backend's tree. Both keep everything else: the register still holds every other library,
    #      and the checker still refuses a dependency nobody recorded.
    Edit('build/scripts/tools/dependencies.txt',
         start='library opencl',
         before='library afternight-rawspeed',
         removes=23,
         replacement=()),
    Edit('build/scripts/tools/check_dependency_provenance.py',
         start="    'src/AfterNightEngineOpenClBackend',",
         before=')',
         removes=1,
         replacement=()),

    # ---- The safety register loses the non-shipping proof dependency and its component kind.
    #      The checker keeps reviewing every shipped engine and adapter dependency.
    Edit('build/scripts/tools/check_safety_register.py',
         start="COMPONENTS = ('engine', 'python-adapter', 'gmic-adapter', 'opencl-backend', 'tests', "
               "'tooling')",
         before=None,
         removes=1,
         replacement=("COMPONENTS = ('engine', 'python-adapter', 'gmic-adapter', 'tests', 'tooling')",)),
    Edit('build/scripts/tools/safety_register.txt',
         start='#       component   engine | python-adapter | gmic-adapter | opencl-backend | tests | tooling |',
         before=None,
         removes=1,
         replacement=('#       component   engine | python-adapter | gmic-adapter | tests | tooling |',)),
    Edit('build/scripts/tools/safety_register.txt',
         start='review opencl',
         before='review afternight-rawspeed',
         removes=8,
         replacement=()),

    # ---- The documented surface: the backend published one, so Doxygen read it.
    Edit('build/doxygen/AfterNightEngine.doxyfile',
         start='INPUT                  = src/AfterNightEngine/include '
               'src/AfterNightEngineGmicAdapter/include '
               'src/AfterNightEngineOpenClBackend/include '
               'src/AfterNightEnginePythonAdapter/include',
         before=None,
         removes=1,
         replacement=('INPUT                  = src/AfterNightEngine/include '
                      'src/AfterNightEngineGmicAdapter/include '
                      'src/AfterNightEnginePythonAdapter/include',)),

    # ---- check_build_graph.py keeps the backend rules and loses the backend. The table empties
    #      rather than disappearing, because the rules are what a later accelerator arrives into.
    Edit('build/scripts/tools/check_build_graph.py',
         start='# The opt-in GPU backends, and the one suite each is allowed to be linked by. Unlike an '
               'adapter, a',
         before='# How a GPU SDK shows up in a generated project. A directory is recognised by a whole '
                'path component -',
         removes=7,
         replacement=('# The opt-in GPU backends, and the one suite each is allowed to be linked by. '
                      'Unlike an adapter, a',
                      '# backend is absent unless its build asked for it, so a tree without one is the '
                      'default rather than a',
                      '# regression; the rules below apply to whichever tree does contain one. There is '
                      'none in the tree, and',
                      '# the rules stay, so the next one is held to them from its first generation.',
                      'OPTIONAL_BACKEND_SUITES = {}',
                      '')),
    Edit('build/scripts/tools/check_build_graph_test.py',
         start="OPENCL_SDK_INCLUDE = os.path.join('..', '..', 'vcpkg', 'installed', 'x64-windows-opencl', "
               "'include')",
         before='class MakefileTreeCase(unittest.TestCase):',
         removes=98,
         replacement=("CUDA_TOOLKIT_INCLUDE = 'C:\\\\Program Files\\\\NVIDIA GPU Computing Toolkit"
                      "\\\\CUDA\\\\v13.0\\\\include'",
                      '',
                      '',
                      'class GpuSdkGuardTests(VisualStudioTreeCase):',
                      '    """No target may reach a GPU SDK. No tree has one, and this is the rule that',
                      '    keeps the next accelerator from arriving as a dependency of everything."""',
                      '',
                      '    def test_a_default_tree_reaches_no_gpu_sdk_and_passes(self):',
                      '        self.build_tree()',
                      '        code, output = self.run_check()',
                      '        self.assertEqual(code, 0, output)',
                      '',
                      '    def test_the_engine_including_a_gpu_sdk_fails(self):',
                      '        self.build_tree(engine_includes=ENGINE_INCLUDE + \';\' + '
                      'CUDA_TOOLKIT_INCLUDE)',
                      '        code, output = self.run_check()',
                      '        self.assertEqual(code, 1)',
                      "        self.assertIn('AfterNightEngine reaches a GPU SDK through', output)",
                      '',
                      '    def test_a_product_target_including_a_cuda_toolkit_fails(self):',
                      "        self.build_tree(extra_main=[('AfterNightGpuTool', "
                      "{'includes': CUDA_TOOLKIT_INCLUDE})])",
                      '        code, output = self.run_check()',
                      '        self.assertEqual(code, 1)',
                      "        self.assertIn('AfterNightGpuTool reaches a GPU SDK through', output)",
                      '',
                      '')),

    # ---- check_forbidden_constructs.py scanned the backend's tree and its export macro.
    Edit('build/scripts/tools/check_forbidden_constructs.py',
         start="    'src/AfterNightEngineOpenClBackend',",
         before=None,
         removes=1,
         replacement=()),
    Edit('build/scripts/tools/check_forbidden_constructs.py',
         start="    'src/AfterNightTests/Test_EngineOpenClBackend',",
         before=None,
         removes=1,
         replacement=()),
    Edit('build/scripts/tools/check_forbidden_constructs.py',
         start="    ('src/AfterNightEngineOpenClBackend/include/AfterNightEngineOpenClBackend/',",
         before="    ('src/AfterNightEnginePythonAdapter/include/AfterNightEnginePythonAdapter/',",
         removes=2,
         replacement=()),
    Edit('build/scripts/tools/check_forbidden_constructs_test.py',
         start='    def test_an_exported_template_in_the_gpu_backends_surface_is_a_finding(self):',
         before='    def test_every_optional_adapter_the_build_graph_declares_is_scanned(self):',
         removes=10,
         replacement=()),

    # ---- check_include_layers.py read the backend with the optional adapters' rule set.
    Edit('build/scripts/tools/check_include_layers.py',
         start="    # The opt-in GPU backend, for the same reason: it implements the engine's device seam "
               'from outside.',
         before="    'AfterNightControls',",
         removes=2,
         replacement=()),
    Edit('build/scripts/tools/check_include_layers.py',
         start='# The opt-in GPU backends. The OpenCL one loads the loader its graphics driver installed '
               'from the',
         before='# Every optional component whose tree this checker reads with the adapter rule set.',
         removes=8,
         replacement=('# The opt-in GPU backends. A backend implements the engine\'s device seam from '
                      'outside, under the same',
                      '# rules an adapter has. There is none in the tree, and the table is what the next '
                      'one arrives into.',
                      'OPTIONAL_BACKENDS = ()',
                      '')),
    Edit('build/scripts/tools/check_include_layers_test.py',
         start="    'gpu': 'CL/cl.h',",
         before=None,
         removes=1,
         replacement=()),
    Edit('build/scripts/tools/check_include_layers_test.py',
         start='class OpenClBackendTests(OptionalAdapterCases, EngineTreeCase):',
         before='class AdapterTableTests(unittest.TestCase):',
         removes=27,
         replacement=()),
    Edit('build/scripts/tools/check_include_layers_test.py',
         start='        covered = {case.LIBRARY for case in (GmicAdapterTests, OpenClBackendTests, '
               'PythonAdapterTests)}',
         before=None,
         removes=1,
         replacement=('        covered = {case.LIBRARY for case in (GmicAdapterTests, '
                      'PythonAdapterTests)}',)),
    # The kind of every backend in an empty table is the empty set, not {'backend'}. The rule the
    # assertion states is that a backend declares itself one, which is what it says once none is there.
    Edit('build/scripts/tools/check_include_layers_test.py',
         start="        self.assertEqual({backend.kind for backend in layers.OPTIONAL_BACKENDS}, "
               "{'backend'})",
         before=None,
         removes=1,
         replacement=('        self.assertTrue(all(backend.kind == \'backend\' '
                      'for backend in layers.OPTIONAL_BACKENDS))',)),

    # ---- The consumer target registry declares the backend and its suite as opt-in targets, and
    #      check_installer_payload.py derives from it that no pattern may ship either.
    Edit('build/scripts/tools/consumer_targets.txt',
         start='# The opt-in OpenCL proof backend and its suite.',
         before='# ---- The engine-free extension boundary.',
         removes=21,
         replacement=()),
    Edit('build/scripts/tools/check_installer_payload_test.py',
         start='class OptionalGpuBackendShipsTests(PackagerCase):',
         before='class BenchmarkShipsTests(PackagerCase):',
         removes=17,
         replacement=()),
    # The packagers name the backend among what they keep out, and so does the payload test's
    # description of them.
    Edit('build/scripts/tools/check_installer_payload_test.py',
         start='                         \'AfterNightEnginePythonAdapter*,AfterNightEngineOpenClBackend*,\'',
         before=None,
         removes=1,
         replacement=('                         \'AfterNightEnginePythonAdapter*,\'',)),
    Edit('build/scripts/tools/check_installer_payload_test.py',
         start='                       \'--exclude="libAfterNightEnginePythonAdapter*" \'',
         before=None,
         removes=1,
         replacement=('                       \'--exclude="libAfterNightEnginePythonAdapter*"\')',)),
    Edit('build/scripts/tools/check_installer_payload_test.py',
         start='                       \'--exclude="libAfterNightEngineOpenClBackend*"\')',
         before=None,
         removes=1,
         replacement=()),
    Edit('installer/win_installer.iss',
         start='Source: "..\\bin\\msvc\\release\\*"; DestDir: "{app}"; Excludes: "*.gitkeep,gtest.dll,*.log,*.pdb,'
               '.engine-instrumentation,Test_*,Fuzz_*,Raw*Bench*,Raw*Oracle*,RawRuntimeProbe*,RawNativeTests*,'
               'AfterNightEngineBench*,AfterNightWorkflowDriver*,AfterNightEnginePythonAdapter*,'
               'AfterNightEngineOpenClBackend*,afternight_libraw_rd.*,afternight_rawspeedd.*,opencv_*,raw_r*,'
               'lcms2-2*"; Flags: ignoreversion recursesubdirs',
         before=None,
         removes=1,
         replacement=('Source: "..\\bin\\msvc\\release\\*"; DestDir: "{app}"; Excludes: "*.gitkeep,gtest.dll,*.log,*.pdb,'
                      '.engine-instrumentation,Test_*,Fuzz_*,Raw*Bench*,Raw*Oracle*,RawRuntimeProbe*,'
                      'RawNativeTests*,AfterNightEngineBench*,AfterNightWorkflowDriver*,'
                      'AfterNightEnginePythonAdapter*,afternight_libraw_rd.*,afternight_rawspeedd.*,opencv_*,'
                      'raw_r*,lcms2-2*"; Flags: ignoreversion recursesubdirs',)),
    Edit('installer/linux_packager.sh',
         start='rsync -av --exclude="$APP_NAME" --exclude="*.debug" --exclude="*.log" --exclude="/core" '
               '--exclude="/core.*" --exclude="/vgcore.*" --exclude=".engine-instrumentation" --exclude="Test_*" '
               '--exclude="Fuzz_*" --exclude="/lib/libQt6Test*" --exclude="Raw*Bench*" --exclude="Raw*Oracle*" '
               '--exclude="RawRuntimeProbe*" '
               '--exclude="RawNativeTests*" --exclude="AfterNightEngineBench*" '
               '--exclude="AfterNightWorkflowDriver*" --exclude="libAfterNightEnginePythonAdapter*" '
               '--exclude="libAfterNightEngineOpenClBackend*" "$DEPLOY_DIR/" "$APPDIR/usr/lib/"',
         before=None,
         removes=1,
         replacement=('rsync -av --exclude="$APP_NAME" --exclude="*.debug" --exclude="*.log" --exclude="/core" '
                      '--exclude="/core.*" --exclude="/vgcore.*" --exclude=".engine-instrumentation" '
                      '--exclude="Test_*" --exclude="Fuzz_*" --exclude="/lib/libQt6Test*" --exclude="Raw*Bench*" '
                      '--exclude="Raw*Oracle*" '
                      '--exclude="RawRuntimeProbe*" --exclude="RawNativeTests*" --exclude="AfterNightEngineBench*" '
                      '--exclude="AfterNightWorkflowDriver*" --exclude="libAfterNightEnginePythonAdapter*" '
                      '"$DEPLOY_DIR/" "$APPDIR/usr/lib/"',)),


    # ---- The reference read the backend's surface with its own decoration expanded away, and the
    #      internal reference read its implementation.
    Edit('build/doxygen/AfterNightEngine.doxyfile',
         start="# The opt-in device backend's published surface is read like the adapters', with its own "
               'decoration',
         before='CLASS_GRAPH            = NO',
         removes=6,
         replacement=()),
    Edit('build/doxygen/AfterNightEngineInternal.doxyfile',
         start="# The opt-in device backend's implementation is read like the adapters'. These four lines go "
               'with the',
         before='CLASS_GRAPH            = NO',
         removes=7,
         replacement=()),

    # ---- check_engine_documentation.py held the backend's every file to the documentation floor.
    Edit('build/scripts/tools/check_engine_documentation.py',
         start="    'src/AfterNightEngineOpenClBackend',",
         before=None,
         removes=1,
         replacement=()),

    # ---- check_public_documentation.py documented the backend's published surface.
    Edit('build/scripts/tools/check_public_documentation.py',
         start="# surface it had not read. The opt-in GPU backend's surface is read whether or not a tree "
               'was generated',
         before="PUBLIC_INCLUDE_DIRS = (PUBLIC_INCLUDE_DIR, 'src/AfterNightEngineGmicAdapter/include',",
         removes=2,
         replacement=('# surface it had not read.',)),
    Edit('build/scripts/tools/check_public_documentation.py',
         start="                       'src/AfterNightEngineOpenClBackend/include',",
         before=None,
         removes=1,
         replacement=()),

    # ---- check_process_state.py counted the backend's one process-lifetime object: the loader module
    #      the operating system keeps. The four categories and the rest of the table stay.
    Edit('build/scripts/tools/check_process_state.py',
         start="    'src/AfterNightEngineOpenClBackend',",
         before=None,
         removes=1,
         replacement=()),
    Edit('build/scripts/tools/check_process_state.py',
         start='    # ---- The opt-in proof backend, which exists only in a tree generated with its switch.',
         before=')',
         removes=6,
         replacement=()),

    # ---- check_header_architecture.py held the backend's headers to the same declaration budget,
    #      expanded-body register and instantiation register as the engine's.
    Edit('build/scripts/tools/check_header_architecture.py',
         start="    Component('AfterNightEngineOpenClBackend'),",
         before=None,
         removes=1,
         replacement=()),

    # ---- The sanitizer matrix stated why the backend's suite is the one row no configuration runs,
    #      and the runner's own suite used that row as its example of a skip naming a target nothing
    #      defines. The three rows go with the suite they name; the example is kept and renamed,
    #      because the rule it states - a skip row for an absent target is allowed - outlives the
    #      proof that happened to be the first absent target.
    Edit('build/scripts/tools/sanitizer_matrix.txt',
         start="run address suite.openclbackend Test_EngineOpenClBackend skip=The opt-in proof backend is non-shipping, absent from a default tree, and its work runs inside a vendor driver this campaign did not compile. A report from there would be the driver's and there would be nothing to fix in this engine.",
         before=None,
         removes=1,
         replacement=()),
    Edit('build/scripts/tools/sanitizer_matrix.txt',
         start="run leak suite.openclbackend Test_EngineOpenClBackend skip=The opt-in proof backend is non-shipping and absent from a default tree, and a vendor driver's own allocations are not this engine's.",
         before=None,
         removes=1,
         replacement=()),
    Edit('build/scripts/tools/sanitizer_matrix.txt',
         start="run thread suite.openclbackend Test_EngineOpenClBackend skip=The opt-in proof backend is non-shipping and absent from a default tree, and a vendor driver's threads are not instrumented.",
         before=None,
         removes=1,
         replacement=()),
    Edit('build/scripts/tools/run_sanitizer_campaign_test.py',
         start="            MATRIX + 'run address suite.opencl Test_EngineOpenClBackend skip=the opt-in "
               "proof backend '",
         before='        self.assertEqual(code, 0, output)',
         removes=2,
         replacement=("            MATRIX + 'run address suite.absent Test_EngineAbsentSuite skip=a suite "
                      "that no '",
                      "                     'generated tree defines is still a row worth stating\\n')",)),

    # ---- The conformance registry lists the backend's suite as the one suite a default tree does
    #      not generate. The row goes with the suite; the `optional` presence stays, because the rule
    #      it states - a registered suite a generated tree may not contain - outlives the proof that
    #      happened to be the first one.
    Edit('build/scripts/tools/conformance_matrix.txt',
         start="# The device proof's suite, generated with its backend or not at all. `optional` is decided by",
         before='# ---------------------------------------------------------------------------------------------',
         removes=4,
         replacement=()),

    # ---- The mock's own cases say where the same shape of work runs on a real device.
    Edit('src/AfterNightTests/Test_EngineKernels/DeviceChainTest.cpp',
         start='// fail and to be stopped so that neither leaves work nobody waits for.',
         before='#include "MockDeviceBackend.h"',
         removes=4,
         replacement=('// fail and to be stopped so that neither leaves work nobody waits for.', '')),
    Edit('src/AfterNightTests/Test_EngineKernels/DevicePointwiseProgramTest.cpp',
         start='// scalar evaluator and narrows it as the host executor does, so its result is the host '
               "reference's bit for",
         before='#include "MockDeviceBackend.h"',
         removes=4,
         replacement=('// scalar evaluator and narrows it as the host executor does, so its result is the '
                      "host reference's bit for",
                      '// bit and a difference is a defect in the seam rather than in arithmetic.',
                      '')),
    Edit('src/AfterNightTests/Test_EngineKernels/DeviceStencil3x3Test.cpp',
         start='// The direct stencil through the checked device facade, against the mock backend in both '
               'completion',
         before='#include "MockDeviceBackend.h"',
         removes=3,
         replacement=('// The direct stencil through the checked device facade, against the mock backend '
                      'in both completion',
                      "// modes. The plan's halo, border rule and device-allocated scratch are what it "
                      'exercises.',
                      '')),
)


@dataclasses.dataclass
class Plan:
    """What a removal would delete, what it would rewrite, what a person still has to edit."""

    removed: list[str] = dataclasses.field(default_factory=list)
    generated: list[str] = dataclasses.field(default_factory=list)
    rewritten: dict[str, str] = dataclasses.field(default_factory=dict)
    documentation: list[str] = dataclasses.field(default_factory=list)
    problems: list[str] = dataclasses.field(default_factory=list)


def _relative(repo_root, path):
    return path.relative_to(repo_root).as_posix()


def tracked_files(repo_root, roots):
    """Every file under `roots` that the repository tracks, or that is there when git cannot say."""
    result = subprocess.run(['git', 'ls-files', '--', *roots], cwd=repo_root,
                            capture_output=True, text=True, check=False)
    if result.returncode == 0 and result.stdout.strip():
        return sorted(line for line in result.stdout.splitlines() if line)
    found = []
    for root in roots:
        base = repo_root / root
        if not base.is_dir():
            continue
        for path in base.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts:
                found.append(_relative(repo_root, path))
    return sorted(found)


def names_the_proof(text):
    """The one-based numbers of the lines in `text` that spell the proof."""
    def spells_it(lowered):
        for exception in PROOF_TOKEN_EXCEPTIONS:
            lowered = lowered.replace(exception, '')
        return any(token in lowered for token in PROOF_TOKENS)

    if not spells_it(text.lower()):
        return []
    return [number for number, line in enumerate(text.splitlines(), start=1) if spells_it(line.lower())]


def read(path):
    """One file's text and the line ending it uses, whichever that is."""
    data = path.read_bytes()
    text = data.decode('utf-8')
    return text.replace('\r\n', '\n'), ('\r\n' if b'\r\n' in data else '\n')


def probe(path):
    """One file's text for searching, or None when it holds bytes nobody writes a mention in."""
    data = path.read_bytes()
    if b'\x00' in data:
        return None
    return data.decode('utf-8', errors='replace').replace('\r\n', '\n')


def apply_edit(text, edit):
    """`text` with one edit made, or the reason it could not be."""
    lines = text.split('\n')
    starts = [index for index, line in enumerate(lines) if line == edit.start]
    if not starts:
        return None, '%s: the start anchor is not there: %s' % (edit.path, edit.start)
    if len(starts) > 1:
        return None, '%s: the start anchor appears %d times, so the region it opens is not one region: %s' \
            % (edit.path, len(starts), edit.start)
    start = starts[0]
    if edit.before is None:
        stop = start + 1
    else:
        following = [index for index in range(start + 1, len(lines)) if lines[index] == edit.before]
        if not following:
            return None, '%s: the end anchor never follows the start anchor: %s' % (edit.path, edit.before)
        stop = following[0]
    if stop - start != edit.removes:
        return None, ('%s: the region holds %d line(s) rather than the %d it records, so what it deletes '
                      'is no longer what it was written for - narrow it or record the new size: %s'
                      % (edit.path, stop - start, edit.removes, edit.start))
    changed = lines[:start] + list(edit.replacement) + lines[stop:]
    if changed == lines:
        return None, '%s: the edit at %s changes nothing, so it has already been made or is misaimed' \
            % (edit.path, edit.start)
    return '\n'.join(changed), None


def plan(repo_root, removed_paths=REMOVED_PATHS, edits=EDITS, scan_exempt=SCAN_EXEMPT,
         scanned_roots=SCANNED_ROOTS, documentation_records=DOCUMENTATION_RECORDS,
         documentation_edits=DOCUMENTATION_EDITS, generated_globs=GENERATED_GLOBS):
    """What removing the proof from `repo_root` would do, without doing any of it."""
    result = Plan()
    endings = {}

    for pattern in generated_globs:
        for path in sorted(repo_root.glob(pattern)):
            result.generated.append(_relative(repo_root, path))

    for relative in removed_paths:
        path = repo_root / relative
        if path.is_dir():
            result.removed.extend(sorted(_relative(repo_root, child)
                                         for child in path.rglob('*') if child.is_file()))
        elif path.is_file():
            result.removed.append(relative)
        else:
            result.problems.append('%s: nothing to remove there, so the procedure no longer describes '
                                   'the tree' % relative)

    for edit in edits:
        path = repo_root / edit.path
        if not path.is_file():
            result.problems.append('%s: the file an edit names is not there' % edit.path)
            continue
        if edit.path in result.rewritten:
            text = result.rewritten[edit.path]
        else:
            text, endings[edit.path] = read(path)
        changed, problem = apply_edit(text, edit)
        if problem is not None:
            result.problems.append(problem)
            continue
        result.rewritten[edit.path] = changed

    removed = set(result.removed) | set(removed_paths) | set(result.generated)
    for relative in tracked_files(repo_root, scanned_roots):
        if any(relative.startswith(generated + '/') for generated in result.generated):
            continue
        if relative in removed or relative in scan_exempt:
            continue
        if any(relative.endswith(suffix) for suffix in PROSE_SUFFIXES):
            continue
        if relative in result.rewritten:
            text = result.rewritten[relative]
        else:
            path = repo_root / relative
            if not path.is_file():
                continue
            text = probe(path)
            if text is None:
                continue
        for number in names_the_proof(text):
            result.problems.append('%s:%d still names the GPU proof after it is removed' % (relative, number))

    for relative in tracked_files(repo_root, ('docs',)) + sorted(
            _relative(repo_root, path) for path in repo_root.glob('*.md')):
        if any(relative.startswith(record) for record in documentation_records):
            continue
        path = repo_root / relative
        if not path.is_file():
            continue
        text = probe(path)
        if text is None or not names_the_proof(text):
            continue
        if relative in documentation_edits:
            if relative not in result.documentation:
                result.documentation.append(relative)
        else:
            result.problems.append('%s names the GPU proof and is neither a record nor a documented '
                                   'edit, so the prose step does not cover it' % relative)

    for relative in documentation_edits:
        if relative not in result.documentation:
            result.problems.append('%s is listed as prose to edit and names nothing of the proof' % relative)

    result.endings = endings
    return result


def check(repo_root, **manifest):
    return plan(repo_root, **manifest).problems


def apply(repo_root, **manifest):
    """Perform the removal, or refuse it whole and change nothing."""
    result = plan(repo_root, **manifest)
    if result.problems:
        return result.problems

    for relative in list(manifest.get('removed_paths', REMOVED_PATHS)) + result.generated:
        path = repo_root / relative
        if path.is_dir():
            for child in sorted(path.rglob('*'), key=lambda item: len(item.parts), reverse=True):
                child.unlink() if child.is_file() else child.rmdir()
            path.rmdir()
        elif path.is_file():
            path.unlink()

    for relative, text in result.rewritten.items():
        ending = result.endings.get(relative, '\n')
        (repo_root / relative).write_bytes(text.replace('\n', ending).encode('utf-8'))
    return []


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--apply', action='store_true',
                        help='perform the removal instead of checking that it still describes the tree')
    parser.add_argument('--repo-root', default=str(REPO_ROOT),
                        help='repository root to work in; defaults to the one containing this script')
    arguments = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout
    repo_root = pathlib.Path(arguments.repo_root).resolve()

    result = plan(repo_root)
    for problem in result.problems:
        print(problem, file=out)
    if result.problems:
        print('%d problem(s): the GPU proof removal procedure does not describe this tree'
              % len(result.problems), file=out)
        return 1

    if arguments.apply:
        apply(repo_root)
        print('Removed the GPU proof: %d files deleted, %d rewritten, and %d generated artefact(s) '
              'swept.' % (len(result.removed), len(result.rewritten), len(result.generated)), file=out)
        print('Regenerate the project files, then build and run the checks; no GPU SDK is needed for '
              'any of it.', file=out)
        print('Still to edit by hand, where each describes the proof: %s' % ', '.join(result.documentation),
              file=out)
        return 0

    print('The GPU proof removal procedure describes this tree: %d files would be deleted, %d '
          'rewritten, and %s edited by hand.'
          % (len(result.removed), len(result.rewritten), ', '.join(result.documentation)), file=out)
    return 0


if __name__ == '__main__':
    sys.exit(main())

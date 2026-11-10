# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_installer_payload.

Run with:
    python build/scripts/tools/check_installer_payload_test.py

Every case writes a throwaway packager pair beside a copy of the repository's target registry, source
lists and package profiles, from which the lists the packagers are held to are derived, and writes
nothing outside its own temporary directory. The final case does read the repository's own packagers,
because a checker that only ever passes on synthetic input proves nothing about the files it exists to
guard.
"""

from __future__ import annotations

import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_installer_payload as payload  # noqa: E402


# The version a case's application states, and both packagers name.
CASE_VERSION = '1.2.3'

ISS_TEMPLATE = """\
#define MyAppVersion "1.2.3"
[Setup]
AppName=AfterNight
VersionInfoVersion=1.2.3.0

[Files]
Source: "..\\bin\\msvc\\release\\*"; DestDir: "{app}"; Excludes: "%s"; Flags: ignoreversion recursesubdirs
Source: "..\\docs\\user-guide\\site\\*"; DestDir: "{app}\\docs"; Flags: ignoreversion recursesubdirs
"""

SHELL_TEMPLATE = """\
#!/bin/bash

APP_NAME="AfterNight"
APP_VERSION="1.2.3"
DEPLOY_DIR="../bin/clang/release"
APPDIR="AppDir"

cp "$DEPLOY_DIR/$APP_NAME" "$APPDIR/usr/lib/"
rsync -av %s "$DEPLOY_DIR/" "$APPDIR/usr/lib/"
cat > "$APPDIR/AppRun" <<EOF
#!/bin/bash
HERE=\\$(dirname "\\$(readlink -f "\\$0")")
exec "\\$HERE/usr/lib/$APP_NAME" "\\$@"
EOF
%s"""

CLI_LAUNCHER = """\
cat > "$APPDIR/usr/bin/AfterNightCLI" <<EOF
#!/bin/bash
ROOT=\\$(dirname "\\$(readlink -f "\\$0")")/../..
export LD_LIBRARY_PATH="\\$ROOT/usr/lib:\\$ROOT/usr/lib/lib:\\$LD_LIBRARY_PATH"
exec "\\$ROOT/usr/lib/AfterNightCLI" "\\$@"
EOF
"""

MSVC_TARGET_SELECT_TEMPLATE = """\
@echo off
set "SANITIZER_MARKER=..\\..\\..\\bin\\%BIN_DIR%\\%CONFIG%\\{record}"
if exist "%SANITIZER_MARKER%" set /p BUILT_SANITIZER=<"%SANITIZER_MARKER%"
"""

LINUX_TARGET_SELECT_TEMPLATE = """\
#!/usr/bin/env bash
SANITIZER_MARKER="../../../bin/clang/${{CONFIG}}/{record}"
"""

# The product: the engine, its G'MIC adapter, the consumer runtime, the libraries, both hosts, the CLI, the runtime
# backend, the style plugin and the private RAW runtime the engine imports ship; the Python adapter, the opt-in GPU
# backend, every validation tool and the reference libraries only the library parity suite imports stay out.
GOOD_WINDOWS_EXCLUDES = ('*.gitkeep,gtest.dll,*.log,*.pdb,.engine-instrumentation,Test_*,Fuzz_*,Raw*Bench*,Raw*Oracle*,'
                         'RawRuntimeProbe*,RawNativeTests*,AfterNightEngineBench*,AfterNightWorkflowDriver*,'
                         'AfterNightEnginePythonAdapter*,AfterNightEngineOpenClBackend*,'
                         'afternight_libraw_rd.*,afternight_rawspeedd.*,opencv_*,raw_r*,lcms2-2*')
GOOD_LINUX_EXCLUDES = ('--exclude="$APP_NAME" --exclude="*.debug" --exclude="*.log" --exclude="/core" '
                       '--exclude="/core.*" --exclude="/vgcore.*" --exclude=".engine-instrumentation" '
                       '--exclude="Test_*" --exclude="Fuzz_*" --exclude="/lib/libQt6Test*" --exclude="Raw*Bench*" '
                       '--exclude="Raw*Oracle*" '
                       '--exclude="RawRuntimeProbe*" --exclude="RawNativeTests*" '
                       '--exclude="AfterNightEngineBench*" --exclude="AfterNightWorkflowDriver*" '
                       '--exclude="libAfterNightEnginePythonAdapter*" '
                       '--exclude="libAfterNightEngineOpenClBackend*"')


REGISTERED_FUZZ_TARGETS = ('Fuzz_EngineDescriptor', 'Fuzz_EngineCodec')

# The RAW qualification project's install rules, grouped as the real project groups them.
QUALIFICATION_INSTALLS = (('RawRuntimeProbe',),
                          ('RawEngineBench',),
                          ('RawPrivateOracle',), ('RawNativeTests',), ('RawStockOracle',))

PRIVATE_RAW_LIBRARIES = ('afternight_libraw_r{suffix}.dll', 'afternight_rawspeed{suffix}.dll')


class PackagerCase(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='an-payload-test-')
        os.makedirs(os.path.join(self.root, 'installer'))
        self.addCleanup(shutil.rmtree, self.root, True)
        for relative in payload.REGISTRIES:
            destination = os.path.join(self.root, relative)
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            shutil.copy(os.path.join(payload.REPO_ROOT, relative), destination)
        shutil.copytree(os.path.join(payload.REPO_ROOT, payload.SDK_PACKAGE), os.path.join(self.root, payload.SDK_PACKAGE),
                        ignore=shutil.ignore_patterns('__pycache__'))
        self.write_file(payload.PRODUCT_VERSION_SOURCE,
                        'inline constexpr char kProductVersion[] = "%s";\n' % CASE_VERSION)

    def edit_registry(self, relative, old, new):
        path = os.path.join(self.root, relative)
        with open(path, encoding='utf-8') as handle:
            text = handle.read()
        self.assertIn(old, text)
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(text.replace(old, new, 1))

    def write(self, windows_excludes=GOOD_WINDOWS_EXCLUDES, linux_excludes=GOOD_LINUX_EXCLUDES,
              fuzz_targets=REGISTERED_FUZZ_TARGETS, record='.engine-instrumentation',
              qualification_installs=QUALIFICATION_INSTALLS, private_libraries=PRIVATE_RAW_LIBRARIES,
              cli_launcher=CLI_LAUNCHER):
        with open(os.path.join(self.root, 'installer', 'win_installer.iss'), 'w',
                  encoding='utf-8') as handle:
            handle.write(ISS_TEMPLATE % windows_excludes)
        with open(os.path.join(self.root, 'installer', 'linux_packager.sh'), 'w',
                  encoding='utf-8') as handle:
            handle.write(SHELL_TEMPLATE % (linux_excludes, cli_launcher))
        if fuzz_targets is not None:
            registry = os.path.join(self.root, payload.FUZZ_REGISTRY)
            os.makedirs(os.path.dirname(registry), exist_ok=True)
            with open(registry, 'w', encoding='utf-8') as handle:
                handle.write('# The fuzz campaign registry.\n\n')
                for name in fuzz_targets:
                    handle.write('%s   iterations=1000  seconds=60\n' % name)
        if record is not None:
            for relative, template in zip(payload.TARGET_SELECT_SCRIPTS,
                                          (MSVC_TARGET_SELECT_TEMPLATE, LINUX_TARGET_SELECT_TEMPLATE)):
                self.write_file(relative, template.format(record=record))
        if qualification_installs is not None:
            lines = ['cmake_minimum_required(VERSION 3.25)', 'project(AfterNightRawQualification CXX)']
            for index, targets in enumerate(qualification_installs):
                rule = 'install(TARGETS %s RUNTIME DESTINATION .)' % ' '.join(targets)
                # One rule sits inside a condition, as the real project's engine drivers do.
                lines.extend(('if(AN_ENGINE_DRIVERS)', '    ' + rule, 'endif()') if index == 1 else (rule,))
            self.write_file(payload.RAW_QUALIFICATION_PROJECT, '\n'.join(lines) + '\n')
        if private_libraries is not None:
            self.write_file(payload.RAW_DEPLOYMENT,
                            '"""Deploy the private RAW runtime."""\n\nPRIVATE_LIBRARIES = %r\n'
                            % (tuple(private_libraries),))

    def write_file(self, relative, text):
        path = os.path.join(self.root, relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(text)

    def run_check(self):
        stream = io.StringIO()
        code = payload.main(['--repo-root', self.root], stream=stream)
        return code, stream.getvalue()


class AcceptedPayloadTests(PackagerCase):
    def test_correct_exclusions_pass(self):
        self.write()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_reports_both_packagers(self):
        self.write()
        _, output = self.run_check()
        self.assertIn('win_installer.iss', output)
        self.assertIn('linux_packager.sh', output)

    def test_the_packagers_produce_the_product(self):
        self.write()
        _, output = self.run_check()
        self.assertIn('against the product profile', output)


class DesktopEntryTests(PackagerCase):
    """appimagetool refuses a desktop entry holding a carriage return, so the one the Linux packager copies is LF in the
    checkout and by the attribute every fresh checkout is written by."""

    def test_a_desktop_entry_with_carriage_returns_is_refused(self):
        self.write()
        with open(os.path.join(self.root, payload.DESKTOP_ENTRY), 'wb') as handle:
            handle.write(b'[Desktop Entry]\r\nName=AfterNight\r\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('carriage return', output)

    def test_a_desktop_entry_with_line_feeds_passes(self):
        self.write()
        with open(os.path.join(self.root, payload.DESKTOP_ENTRY), 'wb') as handle:
            handle.write(b'[Desktop Entry]\nName=AfterNight\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_the_repository_checks_its_desktop_entry_out_with_line_feeds(self):
        self.assertEqual(payload.desktop_entry_problems(payload.REPO_ROOT), [])


class CliLauncherTests(PackagerCase):
    """The AppImage's command-line tool has no AppRun of its own, so the packager writes it a launcher that has it load
    the package's libraries, as AppRun has the application load them: a run path reaches only a binary's own imports,
    and the third-party libraries below usr/lib/lib would otherwise take the host's copies of what they need."""

    def test_a_packager_that_writes_no_launcher_for_the_tool_is_refused(self):
        self.write(cli_launcher='')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('AfterNightCLI', output)

    def test_a_launcher_that_names_not_both_library_directories_is_refused(self):
        self.write(cli_launcher=CLI_LAUNCHER.replace(':\\$ROOT/usr/lib/lib', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('usr/lib/lib', output)

    def test_a_launcher_that_starts_another_binary_is_refused(self):
        self.write(cli_launcher=CLI_LAUNCHER.replace('usr/lib/AfterNightCLI', 'usr/bin/AfterNightCLI'))
        code, output = self.run_check()
        self.assertEqual(code, 1, output)

    def test_the_launcher_the_template_writes_passes(self):
        self.write()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


class RuntimeDebrisShipsTests(PackagerCase):
    """Suite logs and crash files are written beside the binaries and are never product payload."""

    def test_linux_missing_log_exclusion_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace('--exclude="*.log" ', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('test_persistence.log would be packaged', output)
        self.assertIn('test_thread_safety.log would be packaged', output)

    def test_linux_missing_core_exclusions_fail(self):
        excludes = GOOD_LINUX_EXCLUDES.replace('--exclude="/core" ', '')
        excludes = excludes.replace('--exclude="/core.*" ', '').replace('--exclude="/vgcore.*" ', '')
        self.write(linux_excludes=excludes)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('core would be packaged', output)
        self.assertIn('core.12345 would be packaged', output)
        self.assertIn('vgcore.12345 would be packaged', output)


class PythonAdapterShipsTests(PackagerCase):
    """The optional Python adapter is named, so a payload that ships the engine cannot ship it."""

    def test_windows_missing_python_adapter_exclusion_fails(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('AfterNightEnginePythonAdapter*,', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: AfterNightEnginePythonAdapter.dll would be packaged', output)
        self.assertIn('win_installer.iss: AfterNightEnginePythonAdapter_d.dll would be packaged', output)


class ProductShipsTests(PackagerCase):
    """The engine, the consumer runtime, the libraries, both hosts, the CLI, the runtime backend and the style plugin
    are the product, so an exclusion of any of them drops it."""

    def test_windows_leftover_exclusions_drop_the_product(self):
        for pattern, name in (('AfterNightEngine*', 'AfterNightEngine.dll'),
                              ('AfterNightConsumerRuntime*', 'AfterNightConsumerRuntime.dll'),
                              ('AfterNightCLI*', 'AfterNightCLI.exe'),
                              ('an_extension_runtime_backend*', 'an_extension_runtime_backend.dll'),
                              ('AfterNightControls*', 'AfterNightControls.dll'),
                              ('AfterNightProcesses*', 'AfterNightProcesses.dll'),
                              ('qafternight*', 'qafternight.dll'),
                              ('AfterNightExtensionHost.exe', 'AfterNightExtensionHost.exe')):
            with self.subTest(pattern=pattern):
                self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',' + pattern)
                code, output = self.run_check()
                self.assertEqual(code, 1)
                self.assertIn('win_installer.iss: %s would be excluded from the package' % name, output)

    def test_linux_leftover_exclusions_drop_the_product(self):
        for pattern, name in (('libAfterNightEngine*', 'libAfterNightEngine.so'),
                              ('libAfterNightConsumerRuntime*', 'libAfterNightConsumerRuntime.so'),
                              ('AfterNightCLI*', 'AfterNightCLI'),
                              ('liban_extension_runtime_backend*', 'liban_extension_runtime_backend.so'),
                              ('libAfterNightControls*', 'libAfterNightControls.so'),
                              ('libAfterNightProcesses*', 'libAfterNightProcesses.so'),
                              ('libqafternight*', 'libqafternight.so'),
                              ('AfterNightExtensionHost', 'AfterNightExtensionHost')):
            with self.subTest(pattern=pattern):
                self.write(linux_excludes=GOOD_LINUX_EXCLUDES + ' --exclude="%s"' % pattern)
                code, output = self.run_check()
                self.assertEqual(code, 1)
                self.assertIn('linux_packager.sh: %s would be excluded from the package' % name, output)



class OptionalGpuBackendShipsTests(PackagerCase):
    """The opt-in GPU backend is named, so a payload that ships the engine and its G'MIC adapter cannot ship it."""

    def test_windows_missing_backend_exclusion_fails(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('AfterNightEngineOpenClBackend*,', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineOpenClBackend.dll would be packaged', output)
        self.assertIn('AfterNightEngineOpenClBackend_d.dll would be packaged', output)

    def test_linux_missing_backend_exclusion_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace(' --exclude="libAfterNightEngineOpenClBackend*"', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('libAfterNightEngineOpenClBackend.so would be packaged', output)


class BenchmarkShipsTests(PackagerCase):
    def test_windows_missing_benchmark_exclusion_fails(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('AfterNightEngineBench*,', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineBench.exe would be packaged', output)

    def test_linux_missing_benchmark_exclusion_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace('--exclude="AfterNightEngineBench*" ', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineBench would be packaged', output)

    def test_excluding_the_runner_by_its_release_name_alone_is_not_enough(self):
        """A name-by-name list decays silently: the Debug binary is a name of its own."""
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('AfterNightEngineBench*,',
                                                                  'AfterNightEngineBench.exe,'))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineBench_d.exe would be packaged', output)
        self.assertNotIn('AfterNightEngineBench.exe would be packaged', output)


class FuzzTargetShipsTests(PackagerCase):
    """The fuzz targets are built beside the product's binaries, so a packager ships them unless it excludes them."""

    def test_windows_missing_fuzz_exclusion_fails(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('Fuzz_*,', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: Fuzz_EngineCodec.exe would be packaged', output)
        self.assertIn('win_installer.iss: Fuzz_EngineCodec_d.exe would be packaged', output)

    def test_linux_missing_fuzz_exclusion_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace('--exclude="Fuzz_*" ', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: Fuzz_EngineDescriptor would be packaged', output)
        self.assertIn('linux_packager.sh: Fuzz_EngineDescriptor_d would be packaged', output)

    def test_a_target_registered_after_the_exclusion_was_written_is_held_too(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('Fuzz_*,', 'Fuzz_EngineDescriptor*,Fuzz_EngineCodec*,'),
                   fuzz_targets=REGISTERED_FUZZ_TARGETS + ('Fuzz_EngineLater',))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Fuzz_EngineLater.exe would be packaged', output)
        self.assertNotIn('Fuzz_EngineCodec.exe would be packaged', output)

    def test_a_missing_registry_is_reported_rather_than_read_as_no_targets(self):
        self.write(fuzz_targets=None)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('fuzz_targets.txt was not found', output)


class InstrumentationRecordShipsTests(PackagerCase):
    """The target-select build records its instrumentation in a file beside the binaries a packager sweeps."""

    def test_windows_missing_record_exclusion_fails(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('.engine-instrumentation,', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: .engine-instrumentation would be packaged', output)

    def test_linux_missing_record_exclusion_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace('--exclude=".engine-instrumentation" ', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: .engine-instrumentation would be packaged', output)

    def test_the_record_is_named_by_the_scripts_that_write_it(self):
        self.write(record='.build-instrumentation')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: .build-instrumentation would be packaged', output)
        self.assertIn('linux_packager.sh: .build-instrumentation would be packaged', output)

    def test_a_missing_target_select_script_is_reported_rather_than_read_as_no_record(self):
        self.write(record=None)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('build/scripts/msvc/build_afternightengine.bat was not found', output)
        self.assertIn('build/scripts/linux/build_afternightengine.sh was not found', output)


class QualificationToolShipsTests(PackagerCase):
    """The RAW qualification project installs its drivers and oracles into the deployed directory."""

    def test_windows_missing_tool_exclusions_fail(self):
        excludes = GOOD_WINDOWS_EXCLUDES
        for pattern in ('Raw*Bench*,', 'Raw*Oracle*,', 'RawRuntimeProbe*,', 'RawNativeTests*,'):
            excludes = excludes.replace(pattern, '')
        self.write(windows_excludes=excludes)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        for name in (tool + '.exe' for group in QUALIFICATION_INSTALLS for tool in group):
            self.assertIn('win_installer.iss: %s would be packaged' % name, output)

    def test_linux_missing_tool_exclusions_fail(self):
        excludes = GOOD_LINUX_EXCLUDES
        for pattern in ('--exclude="Raw*Bench*" ', '--exclude="Raw*Oracle*" ', '--exclude="RawRuntimeProbe*" ',
                        '--exclude="RawNativeTests*" '):
            excludes = excludes.replace(pattern, '')
        self.write(linux_excludes=excludes)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: RawEngineBench would be packaged', output)
        self.assertIn('linux_packager.sh: RawNativeTests would be packaged', output)

    def test_a_tool_installed_after_the_exclusion_was_written_is_held_too(self):
        self.write(qualification_installs=QUALIFICATION_INSTALLS + (('RawLaterProbe',),))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: RawLaterProbe.exe would be packaged', output)
        self.assertIn('linux_packager.sh: RawLaterProbe would be packaged', output)
        self.assertNotIn('RawRuntimeProbe.exe would be packaged', output)

    def test_a_missing_qualification_project_is_reported_rather_than_read_as_no_tools(self):
        self.write(qualification_installs=None)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('build/scripts/tools/rawspeed/CMakeLists.txt was not found', output)


class PrivateRawRuntimeShipsTests(PackagerCase):
    """The private LibRaw and RawSpeed packages are deployed for the engine, which alone imports them: the product
    ships their Release builds and keeps their Debug builds out."""

    def test_windows_exclusion_of_the_private_runtime_drops_the_engines_raw_decoder(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',afternight_*')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: afternight_libraw_r.dll would be excluded from the package', output)
        self.assertIn('win_installer.iss: afternight_rawspeed.dll would be excluded from the package', output)

    def test_windows_missing_debug_private_runtime_exclusion_fails(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace(',afternight_libraw_rd.*,afternight_rawspeedd.*', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: afternight_libraw_rd.dll would be packaged', output)
        self.assertIn('win_installer.iss: afternight_rawspeedd.dll would be packaged', output)
        self.assertNotIn('afternight_libraw_r.dll', output)

    def test_a_library_added_to_the_deployment_is_held_too(self):
        self.write(private_libraries=PRIVATE_RAW_LIBRARIES + ('afternight_x3f{suffix}.dll',))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: afternight_x3fd.dll would be packaged', output)
        self.assertNotIn('afternight_rawspeedd.dll would be packaged', output)


    def test_a_missing_deployment_script_is_reported_rather_than_read_as_no_libraries(self):
        self.write(private_libraries=None)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('build/scripts/tools/deploy_raw_deps.py was not found', output)


class OverBroadExclusionTests(PackagerCase):
    """An exclusion wide enough to drop the product would otherwise pass every check above."""

    def test_windows_exclusion_dropping_the_engine_fails(self):
        self.write(windows_excludes='*.pdb,Test_*,AfterNight*')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngine.dll', output)

    def test_linux_exclusion_dropping_the_engine_fails(self):
        self.write(linux_excludes='--exclude="$APP_NAME" --exclude="libAfterNight*"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('libAfterNightEngine.so', output)

    def test_windows_exclusion_dropping_the_platform_library_fails(self):
        """The logger and the crash handler live in the platform library, which every product binary loads."""
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',AfterNightPlatform*')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: AfterNightPlatform.dll would be excluded from the package', output)

    def test_linux_exclusion_dropping_the_platform_library_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES + ' --exclude="libAfterNightPlatform*"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('libAfterNightPlatform.so would be excluded from the package', output)

    def test_windows_exclusion_dropping_a_qt_runtime_file_fails(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',Qt6*')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Qt6Core.dll', output)

    def test_windows_exclusion_dropping_the_products_raw_decoder_fails(self):
        """Inno Setup matches without case, so a pattern written for the RAW tools can take the product's LibRaw."""
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',*raw*')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: afternight_libraw_r.dll would be excluded from the package', output)


class NestedPayloadTests(PackagerCase):
    """A pattern written for a dump at the payload's root must not reach the product's files below it."""

    def test_an_unanchored_dump_exclusion_takes_an_sdk_module_and_fails(self):
        """rsync matches a pattern with no slash at every depth, so core.* takes the SDK's core.py."""
        excludes = GOOD_LINUX_EXCLUDES.replace('"/core"', '"core"').replace('"/core.*"', '"core.*"')
        self.write(linux_excludes=excludes)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: PythonLib/lib/python3.14/site-packages/afternight/core.py would be excluded '
                      'from the package', output)

    def test_an_exclusion_of_a_directory_takes_every_module_below_it(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES + ' --exclude="afternight"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('PythonLib/lib/python3.14/site-packages/afternight/__init__.py would be excluded', output)

    def test_an_inno_exclusion_without_a_backslash_also_matches_below_the_root(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',core.*')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: PythonLib/Lib/site-packages/afternight/core.py would be excluded', output)

    def test_an_anchored_pattern_matches_at_the_root_only(self):
        rule = payload.PayloadRule('rsync', ['/core.*', '\\vgcore.*', '*.log'], case_sensitive=True)
        self.assertTrue(rule.is_excluded('core.12345'))
        self.assertTrue(rule.is_excluded('vgcore.12345'))
        self.assertFalse(rule.excludes_path('PythonLib/lib/python3.14/site-packages/afternight/core.py'))
        self.assertTrue(rule.excludes_path('core.12345'))
        self.assertTrue(rule.excludes_path('logs/run.log'))

    def test_a_missing_sdk_package_is_reported(self):
        shutil.rmtree(os.path.join(self.root, payload.SDK_PACKAGE))
        self.write()
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('python/modules/afternight does not exist', output)


class RegistryDerivedListTests(PackagerCase):
    """The lists come from the target registry and the packagers' profile, so a target declared tomorrow is held."""

    def test_a_target_declared_after_the_exclusions_were_written_is_held(self):
        self.write()
        self.edit_registry(payload.REGISTRY, '# ---- The platform library',
                           'target AfterNightProbe\n    workspace    main\n    kind         console\n'
                           '    class        none\n    state        active\n    depends      -\n'
                           '    sources      src/AfterNightPlatform\n\n# ---- The platform library')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: AfterNightProbe.exe would be packaged', output)
        self.assertIn('linux_packager.sh: AfterNightProbe would be packaged', output)

    def test_every_target_the_profile_ships_is_held_in(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',*HostLite*',
                   linux_excludes=GOOD_LINUX_EXCLUDES + ' --exclude="*HostLite*"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: AfterNightExtensionHostLite.exe would be excluded from the package', output)
        self.assertIn('linux_packager.sh: AfterNightExtensionHostLite would be excluded from the package', output)

    def test_a_third_party_runtime_built_from_source_is_held_in(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES + ',cgmic*')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: cgmic.dll would be excluded from the package', output)


    def test_a_missing_registry_is_reported_rather_than_read_as_no_targets(self):
        self.write()
        os.remove(os.path.join(self.root, payload.REGISTRY))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('consumer_targets.txt does not exist', output)


class MalformedPackagerTests(PackagerCase):
    def test_missing_windows_payload_rule_fails(self):
        self.write()
        with open(os.path.join(self.root, 'installer', 'win_installer.iss'), 'w',
                  encoding='utf-8') as handle:
            handle.write('[Setup]\nAppName=AfterNight\n')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('no rule', output.lower())

    def test_missing_linux_payload_rule_fails(self):
        self.write()
        with open(os.path.join(self.root, 'installer', 'linux_packager.sh'), 'w',
                  encoding='utf-8') as handle:
            handle.write('#!/bin/bash\nAPP_NAME="AfterNight"\n')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('no rule', output.lower())

    def test_absent_packager_fails(self):
        self.write()
        os.remove(os.path.join(self.root, 'installer', 'linux_packager.sh'))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh', output)


class VariableExpansionTests(PackagerCase):
    def test_shell_variable_in_an_exclusion_is_resolved(self):
        """`--exclude="$APP_NAME"` must exclude the app binary, not a file literally named so."""
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace('"$APP_NAME"', '"${APP_NAME}"'))
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


class LinuxApplicationPlacementTests(PackagerCase):
    """The binary the AppImage packager copies by a rule of its own lands beside what its sweep carries, and AppRun
    starts it there: the application looks beside itself for the bundled Python, the extension hosts, the bundled
    extensions and the crash handler, so an application copied anywhere else runs without all of them."""

    def edit_packager(self, old, new):
        path = os.path.join(self.root, payload.LINUX_PACKAGER)
        with open(path, encoding='utf-8') as handle:
            text = handle.read()
        self.assertIn(old, text)
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(text.replace(old, new, 1))

    def test_an_application_copied_and_started_beside_the_payload_passes(self):
        self.write()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_an_application_copied_apart_from_the_payload_fails(self):
        self.write()
        self.edit_packager('cp "$DEPLOY_DIR/$APP_NAME" "$APPDIR/usr/lib/"', 'cp "$DEPLOY_DIR/$APP_NAME" "$APPDIR/usr/bin/"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: copies AfterNight into AppDir/usr/bin, apart from AppDir/usr/lib', output)

    def test_an_application_named_in_its_destination_is_read_as_copied_there(self):
        self.write()
        self.edit_packager('cp "$DEPLOY_DIR/$APP_NAME" "$APPDIR/usr/lib/"',
                           'cp "$DEPLOY_DIR/$APP_NAME" "$APPDIR/usr/lib/$APP_NAME"')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_an_apprun_starting_the_application_elsewhere_fails(self):
        self.write()
        self.edit_packager('exec "\\$HERE/usr/lib/$APP_NAME"', 'exec "\\$HERE/usr/bin/$APP_NAME"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: AppRun starts AfterNight from AppDir/usr/bin, not from AppDir/usr/lib', output)

    def test_an_application_the_sweep_leaves_out_and_nothing_copies_fails(self):
        self.write()
        self.edit_packager('cp "$DEPLOY_DIR/$APP_NAME" "$APPDIR/usr/lib/"\n', '')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: copies AfterNight by no rule of its own', output)

    def test_a_packager_writing_no_apprun_fails(self):
        self.write()
        self.edit_packager('exec "\\$HERE/usr/lib/$APP_NAME" "\\$@"\n', '')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: writes no AppRun that starts a binary', output)


class PackagedVersionTests(PackagerCase):
    """Both packagers name the version the application states."""

    def edit_packager(self, relative, old, new):
        path = os.path.join(self.root, relative)
        with open(path, encoding='utf-8') as handle:
            text = handle.read()
        self.assertIn(old, text)
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(text.replace(old, new, 1))

    def test_the_versions_the_application_states_pass(self):
        self.write()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_an_installer_naming_another_version_fails(self):
        self.write()
        self.edit_packager(payload.WINDOWS_PACKAGER, '#define MyAppVersion "1.2.3"', '#define MyAppVersion "1.2.2"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: states version 1.2.2 where the application states 1.2.3', output)

    def test_an_installer_file_version_of_another_release_fails(self):
        self.write()
        self.edit_packager(payload.WINDOWS_PACKAGER, 'VersionInfoVersion=1.2.3.0', 'VersionInfoVersion=1.2.2.0')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('states version 1.2.2.0 where the application states 1.2.3 (1.2.3.0 or 1.2.3)', output)

    def test_an_installer_taking_its_file_version_from_its_one_statement_passes(self):
        self.write()
        self.edit_packager(payload.WINDOWS_PACKAGER, 'VersionInfoVersion=1.2.3.0',
                           'VersionInfoVersion={#MyAppVersion}')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_a_file_version_taken_from_a_statement_of_another_release_fails(self):
        self.write()
        self.edit_packager(payload.WINDOWS_PACKAGER, 'VersionInfoVersion=1.2.3.0',
                           'VersionInfoVersion={#MyAppVersion}')
        self.edit_packager(payload.WINDOWS_PACKAGER, '#define MyAppVersion "1.2.3"', '#define MyAppVersion "1.2.2"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('states version 1.2.2 where the application states 1.2.3 (1.2.3.0 or 1.2.3)', output)

    def test_an_installer_stating_no_file_version_fails(self):
        self.write()
        self.edit_packager(payload.WINDOWS_PACKAGER, 'VersionInfoVersion=1.2.3.0\n', '')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('win_installer.iss: states no version where', output)

    def test_an_appimage_naming_another_version_fails(self):
        self.write()
        self.edit_packager(payload.LINUX_PACKAGER, 'APP_VERSION="1.2.3"', 'APP_VERSION="0.5.2"')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: states version 0.5.2 where the application states 1.2.3', output)

    def test_a_packager_stating_no_version_fails(self):
        self.write()
        self.edit_packager(payload.LINUX_PACKAGER, 'APP_VERSION="1.2.3"\n', '')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('linux_packager.sh: states no version', output)

    def test_an_application_stating_no_version_fails(self):
        self.write()
        self.write_file(payload.PRODUCT_VERSION_SOURCE, '// nothing stated\n')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('states no kProductVersion', output)


class RepositoryPackagerTests(unittest.TestCase):
    def test_repository_packagers_pass(self):
        stream = io.StringIO()
        code = payload.main([], stream=stream)
        self.assertEqual(code, 0, stream.getvalue())


class OptionalAdapterPayloadTests(PackagerCase):
    """The optional Python adapter is excluded by its own name, and nothing else in a payload rule covers it."""

    def test_nothing_but_its_own_exclusion_covers_the_python_adapter(self):
        self.write(windows_excludes=GOOD_WINDOWS_EXCLUDES.replace('AfterNightEnginePythonAdapter*,', ''),
                   linux_excludes=GOOD_LINUX_EXCLUDES.replace(' --exclude="libAfterNightEnginePythonAdapter*"', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEnginePythonAdapter.dll would be packaged', output)
        self.assertIn('libAfterNightEnginePythonAdapter.so would be packaged', output)


class PathPatternTests(unittest.TestCase):
    """A pattern with a slash inside it is a path, as both packagers read one: anchored, it names one place below the
    root; unanchored, it matches the tail of a path at any depth; and a wildcard never crosses a separator."""

    PYSIDE_COPY = 'PythonLib/lib/python3.14/site-packages/PySide6/Qt/lib/libQt6Test.so.6'

    def test_an_anchored_path_pattern_matches_only_its_place(self):
        rule = payload.PayloadRule('rsync', ['/lib/libQt6Test*'], case_sensitive=True)
        self.assertTrue(rule.excludes_path('lib/libQt6Test.so.6'))
        self.assertFalse(rule.excludes_path(self.PYSIDE_COPY))
        self.assertFalse(rule.is_excluded('libQt6Test.so.6'))

    def test_an_unanchored_path_pattern_matches_the_tail_of_a_path_at_any_depth(self):
        rule = payload.PayloadRule('rsync', ['Qt/lib/libQt6Test*'], case_sensitive=True)
        self.assertTrue(rule.excludes_path(self.PYSIDE_COPY))
        self.assertFalse(rule.excludes_path('lib/libQt6Test.so.6'))

    def test_a_wildcard_does_not_cross_a_separator(self):
        rule = payload.PayloadRule('rsync', ['/lib*so.6'], case_sensitive=True)
        self.assertFalse(rule.excludes_path('lib/libQt6Test.so.6'))
        self.assertTrue(rule.excludes_path('libicuuc.so.6'))

    def test_an_excluded_directory_named_by_a_path_takes_everything_below_it(self):
        rule = payload.PayloadRule('inno', ['\\PythonLib\\Lib\\test'], case_sensitive=False)
        self.assertTrue(rule.excludes_path('PythonLib/Lib/test/support/os_helper.py'))
        self.assertFalse(rule.excludes_path('PythonLib/Lib/site-packages/afternight/__init__.py'))


class QtTestShipsTests(PackagerCase):
    """Qt Test is deployed in lib/ beside the Linux suites, which find Qt only there, and no shipped binary links it;
    the bundled Python's PySide6 carries a copy of its own under PythonLib, which ships with it."""

    PYSIDE_COPY = 'PythonLib/lib/python3.14/site-packages/PySide6/Qt/lib/libQt6Test.so.6'

    def test_linux_missing_qt_test_exclusion_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace(' --exclude="/lib/libQt6Test*"', ''))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('lib/libQt6Test.so.6 would be packaged', output)

    def test_an_unanchored_qt_test_exclusion_takes_the_bundled_pythons_copy_and_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace('--exclude="/lib/libQt6Test*"',
                                                              '--exclude="libQt6Test*"'))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn(self.PYSIDE_COPY + ' would be excluded from the package', output)
        self.assertNotIn('lib/libQt6Test.so.6 would be packaged', output)

    def test_a_qt_test_exclusion_anchored_at_the_root_leaves_the_deployed_copy_in_and_fails(self):
        self.write(linux_excludes=GOOD_LINUX_EXCLUDES.replace('--exclude="/lib/libQt6Test*"',
                                                              '--exclude="/libQt6Test*"'))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('lib/libQt6Test.so.6 would be packaged', output)
        self.assertNotIn(self.PYSIDE_COPY + ' would be excluded', output)


if __name__ == '__main__':
    unittest.main(verbosity=2)

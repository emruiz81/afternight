# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_deployed_runtimes.

Run with:
    python build/scripts/tools/check_deployed_runtimes_test.py

Most cases build a throwaway deployment out of an expectation the test states itself, so the suite
needs nothing from the real repository and writes nothing outside its own temporary directory. The
last cases do read the repository's own generated tree, because a checker that only ever passes on
synthetic input proves nothing about the directory it exists to guard.
"""

from __future__ import annotations

import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_deployed_runtimes as deployed  # noqa: E402


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))


def expectation(*entries):
    """The expectation the graph would produce, stated directly so a case can vary one field."""
    return {name: deployed.Expected(name=name, symbol=symbol, project=project)
            for name, symbol, project in entries}


WINDOWS_EXPECTATION = expectation(
    ('AfterNightEngine.dll', 'AfterNightEngine.pdb', 'AfterNightEngine'),
    ('AfterNight.exe', 'AfterNight.pdb', 'AfterNightApp'),
    ('Test_EngineCore.exe', 'Test_EngineCore.pdb', 'Test_EngineCore'),
)

LINUX_EXPECTATION = expectation(
    ('libAfterNightEngine.so', 'libAfterNightEngine.so.debug', 'AfterNightEngine'),
)


class DeploymentCase(unittest.TestCase):
    """A temporary directory playing the part of `bin/<toolchain>/<configuration>`."""

    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix='afternight-deployed-')
        self.addCleanup(self._remove)

    def _remove(self):
        for base, directories, files in os.walk(self.directory, topdown=False):
            for name in files:
                os.remove(os.path.join(base, name))
            for name in directories:
                os.rmdir(os.path.join(base, name))
        os.rmdir(self.directory)

    def write(self, name, content=None):
        path = os.path.join(self.directory, name)
        with open(path, 'wb') as handle:
            handle.write(content if content is not None else b'binary ' + name.encode())
        return path

    def deploy(self, expected, skip=(), symbolless=()):
        """Write every expected binary and symbol file, minus the ones a case wants missing."""
        for name, entry in expected.items():
            if name not in skip:
                self.write(name)
            if entry.symbol and name not in symbolless and name not in skip:
                self.write(entry.symbol)

    def check(self, expected, **kwargs):
        stream = io.StringIO()
        problems, reading = deployed.check_deployment(self.directory, expected, **kwargs)
        for problem in problems:
            print(problem, file=stream)
        return problems, reading, stream.getvalue()


class PresenceTests(DeploymentCase):
    def test_a_complete_deployment_passes_and_says_what_it_read(self):
        self.deploy(WINDOWS_EXPECTATION)
        problems, reading, _ = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(problems, [])
        self.assertEqual(reading.expected, len(WINDOWS_EXPECTATION))
        self.assertEqual(reading.present, len(WINDOWS_EXPECTATION))
        self.assertEqual(reading.with_symbols, len(WINDOWS_EXPECTATION))

    def test_a_missing_binary_is_named_once_with_the_project_that_produces_it(self):
        self.deploy(WINDOWS_EXPECTATION, skip=('AfterNightEngine.dll',))
        problems, reading, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(len(problems), 1)
        self.assertIn('AfterNightEngine.dll is missing', output)
        self.assertIn('AfterNightEngine', output)
        self.assertEqual(reading.present, len(WINDOWS_EXPECTATION) - 1)

    def test_a_deployed_binary_without_its_symbols_fails(self):
        self.deploy(WINDOWS_EXPECTATION, symbolless=('AfterNight.exe',))
        problems, reading, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(len(problems), 1)
        self.assertIn('AfterNight.pdb', output)
        self.assertEqual(reading.with_symbols, len(WINDOWS_EXPECTATION) - 1)

    def test_an_empty_directory_fails_rather_than_reading_as_clean(self):
        problems, reading, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(reading.present, 0)
        self.assertTrue(problems)
        self.assertIn('AfterNight.exe', output)

    def test_an_empty_expectation_is_a_finding_rather_than_a_pass(self):
        self.deploy(WINDOWS_EXPECTATION)
        problems, reading, output = self.check({})
        self.assertTrue(problems)
        self.assertIn('places no binary', output)
        self.assertEqual(reading.expected, 0)


class SymbolTests(DeploymentCase):
    def test_an_unstripped_linux_binary_stands_in_for_a_missing_sidecar(self):
        # A Linux Debug build carries its debug information inside the binary and writes no
        # sidecar, so the sidecar's absence is only a finding when the binary was stripped.
        self.deploy(LINUX_EXPECTATION, symbolless=LINUX_EXPECTATION)
        for name in LINUX_EXPECTATION:
            self.write(name, b'\x7fELF ' + name.encode() + b' unstripped .debug_info here')
        problems, reading, _ = self.check(LINUX_EXPECTATION)
        self.assertEqual(problems, [])
        self.assertEqual(reading.with_symbols, len(LINUX_EXPECTATION))

    def test_a_stripped_linux_binary_with_no_sidecar_fails(self):
        self.deploy(LINUX_EXPECTATION, symbolless=LINUX_EXPECTATION)
        problems, _, output = self.check(LINUX_EXPECTATION)
        self.assertEqual(len(problems), len(LINUX_EXPECTATION))
        self.assertIn('libAfterNightEngine.so.debug', output)

    def test_an_empty_symbol_file_is_not_symbols(self):
        self.deploy(WINDOWS_EXPECTATION)
        with open(os.path.join(self.directory, 'AfterNight.pdb'), 'wb'):
            pass
        problems, reading, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(len(problems), 1)
        self.assertIn('is empty', output)
        self.assertEqual(reading.with_symbols, len(WINDOWS_EXPECTATION) - 1)


def elf_with_debuglink(sidecar, checksum):
    """A minimal 64-bit little-endian ELF carrying a `.gnu_debuglink` and nothing else of substance.

    Hand-built rather than fetched, because this suite has to exercise the Linux reading on a host
    that cannot produce a Linux binary, and the only parts the checker reads are the section table,
    the section name table and one section's contents. Three sections: the mandatory null one, the
    debug link, and the name table itself, which needs a header of its own or the name table and the
    section it names would be the same entry.
    """
    payload = sidecar.encode() + b'\0'
    payload += b'\0' * ((4 - len(payload) % 4) % 4)
    payload += checksum.to_bytes(4, 'little')

    names = [b'', b'.gnu_debuglink', b'.shstrtab']
    name_table = b''.join(name + b'\0' for name in names)
    offsets, cursor = [], 0
    for name in names:
        offsets.append(cursor)
        cursor += len(name) + 1

    header_size, entry_size, count = 64, 64, len(names)
    table = header_size
    payload_offset = table + entry_size * count
    names_offset = payload_offset + len(payload)

    image = bytearray(b'\x7fELF\x02\x01\x01' + b'\0' * 9 + b'\x03\0\x3e\0\x01\0\0\0' + b'\0' * 8)
    image += (0).to_bytes(8, 'little')                      # e_phoff
    image += table.to_bytes(8, 'little')                    # e_shoff
    image += (0).to_bytes(4, 'little')                      # e_flags
    image += header_size.to_bytes(2, 'little')              # e_ehsize
    image += (0).to_bytes(2, 'little') + (0).to_bytes(2, 'little')
    image += entry_size.to_bytes(2, 'little')               # e_shentsize
    image += count.to_bytes(2, 'little')                    # e_shnum
    image += (count - 1).to_bytes(2, 'little')              # e_shstrndx

    def section(name_offset, offset, size):
        entry = bytearray(entry_size)
        entry[0:4] = name_offset.to_bytes(4, 'little')
        entry[0x18:0x20] = offset.to_bytes(8, 'little')
        entry[0x20:0x28] = size.to_bytes(8, 'little')
        return bytes(entry)

    image += section(offsets[0], 0, 0)
    image += section(offsets[1], payload_offset, len(payload))
    image += section(offsets[2], names_offset, len(name_table))
    image += payload + name_table
    return bytes(image)


class MatchingSymbolTests(DeploymentCase):
    """Symbols have to be the binary's own, not a file of about the right name beside it."""

    def test_a_linux_binary_whose_debuglink_checksum_disagrees_fails(self):
        sidecar = b'the debug information of another build'
        self.write('libAfterNightEngine.so.debug', sidecar)
        self.write('libAfterNightEngine.so',
                   elf_with_debuglink('libAfterNightEngine.so.debug', 0xdeadbeef))
        problems, _, output = self.check(LINUX_EXPECTATION)
        self.assertEqual(len(problems), 1)
        self.assertIn("different build's symbols", output)
        self.assertIn('deadbeef', output)

    def test_a_linux_binary_whose_debuglink_matches_passes(self):
        for name in LINUX_EXPECTATION:
            content = b'debug information of ' + name.encode()
            self.write(name + '.debug', content)
            self.write(name,
                       elf_with_debuglink(name + '.debug',
                                          deployed.zlib.crc32(content) & 0xffffffff))
        problems, reading, _ = self.check(LINUX_EXPECTATION)
        self.assertEqual(problems, [])
        self.assertEqual(reading.with_symbols, len(LINUX_EXPECTATION))

    def test_a_linux_binary_whose_named_sidecar_is_absent_fails(self):
        self.write('libAfterNightEngine.so',
                   elf_with_debuglink('libAfterNightEngine.so.debug', 0))
        problems, _, output = self.check(LINUX_EXPECTATION)
        self.assertEqual(len(problems), 1)
        self.assertIn('separated debug information', output)

    def test_the_repositorys_own_windows_binaries_carry_their_own_symbols(self):
        directory = os.path.join(REPO_ROOT, 'bin', 'msvc', 'release')
        engine = os.path.join(directory, 'AfterNightEngine.dll')
        if not os.path.isfile(engine):
            self.skipTest('the engine is not deployed in this checkout')
        with open(engine, 'rb') as handle:
            stated = deployed._codeview(handle.read())
        self.assertIsNotNone(stated, 'a deployed Release DLL states the symbols it was built with')
        name, signature = stated
        self.assertEqual(name, 'AfterNightEngine.pdb')
        with open(os.path.join(directory, name), 'rb') as handle:
            self.assertIn(signature, handle.read())


class DistinctnessTests(DeploymentCase):

    def test_a_case_insensitive_collision_fails(self):
        # Both packagers and the Windows filesystem match without case, so two deployed names that
        # differ only in case are one file on one platform and two on the other. The pair is stated
        # rather than written, because on this host writing the second one would replace the first
        # and there would be nothing left to find.
        self.deploy(WINDOWS_EXPECTATION)
        listing = sorted(os.listdir(self.directory)) + ['afternight.EXE']
        problems, _, output = self.check(WINDOWS_EXPECTATION, deployed=listing)
        self.assertTrue(any('case' in problem for problem in problems))
        self.assertIn('afternight.EXE', output)

    def test_a_stray_engine_binary_no_project_produces_fails(self):
        # The shape this is written against is a target that was renamed, removed, or generated
        # only when a build asked for it: its binary stays in a directory nothing sweeps, and it
        # loads in preference to nothing at all. Named generically on purpose, so the case does not
        # depend on whichever optional component exists at the time.
        self.deploy(WINDOWS_EXPECTATION)
        self.write('AfterNightEngineRetiredBackend.dll')
        problems, _, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(len(problems), 1)
        self.assertIn('AfterNightEngineRetiredBackend.dll', output)
        self.assertIn('no generated project', output)

    def test_a_foreign_binary_that_is_not_this_product_is_left_alone(self):
        self.deploy(WINDOWS_EXPECTATION)
        self.write('Qt6Core.dll')
        self.write('afternight_libraw_r.dll')
        self.write('RawStockOracle.exe')
        problems, _, _ = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(problems, [])

    def test_the_engine_must_be_deployed(self):
        without_engine = {name: entry for name, entry in WINDOWS_EXPECTATION.items()
                          if name != 'AfterNightEngine.dll'}
        self.deploy(without_engine)
        problems, _, output = self.check(without_engine)
        self.assertTrue(problems)
        self.assertIn('the engine has to be deployed here', output)


class InstrumentationTests(DeploymentCase):
    """A sanitizer generation builds into the same directory as an ordinary one, so a binary no ordinary build rebuilt
    after it stays instrumented below a directory that records none - loadable by no process without the sanitizer
    runtime, and shipped by a package made from the directory - wherever below it the binary is."""

    ELF = b'\x7fELF\x02\x01\x01' + b'\0' * 9

    def plugin(self, name, content):
        os.makedirs(os.path.join(self.directory, 'styles'), exist_ok=True)
        return self.write(os.path.join('styles', name), content)

    def test_an_instrumented_plugin_below_an_ordinary_deployment_fails(self):
        self.deploy(WINDOWS_EXPECTATION)
        self.write('.engine-instrumentation', b'none\n')
        self.plugin('libqafternight.so', self.ELF + b'\0__asan_init\0__asan_memset\0')
        problems, _, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(len(problems), 1, output)
        self.assertIn('styles/libqafternight.so carries a sanitizer runtime', output)

    def test_a_directory_with_no_record_is_read_as_ordinary(self):
        self.deploy(WINDOWS_EXPECTATION)
        self.plugin('libqafternight.so', self.ELF + b'\0__tsan_init\0')
        problems, _, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(len(problems), 1, output)
        self.assertIn('styles/libqafternight.so', output)

    def test_a_windows_binary_importing_the_sanitizer_runtime_fails(self):
        self.deploy(WINDOWS_EXPECTATION)
        self.write('.engine-instrumentation', b'none\r\n')
        self.plugin('qafternight.dll', b'MZ' + b'\0' * 62 + b'clang_rt.asan_dynamic-x86_64.dll\0')
        problems, _, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(len(problems), 1, output)
        self.assertIn('styles/qafternight.dll carries a sanitizer runtime', output)

    def test_an_instrumented_deployment_that_records_its_sanitizer_passes(self):
        self.deploy(WINDOWS_EXPECTATION)
        self.write('.engine-instrumentation', b'address+tbb\n')
        self.plugin('libqafternight.so', self.ELF + b'\0__asan_init\0')
        problems, _, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(problems, [], output)

    def test_ordinary_binaries_and_debug_sidecars_below_the_deployment_pass(self):
        self.deploy(WINDOWS_EXPECTATION)
        self.write('.engine-instrumentation', b'none\n')
        self.plugin('libqafternight.so', self.ELF + b'\0QStylePlugin\0')
        # A sidecar of an instrumented build names the runtime in its debug information and is never loaded.
        self.plugin('libqafternight.so.debug', self.ELF + b'\0__asan_init\0')
        problems, _, output = self.check(WINDOWS_EXPECTATION)
        self.assertEqual(problems, [], output)


class VisualCppRuntimeTests(DeploymentCase):
    """A Windows deployment carries the Visual C++ runtime its binaries import. Windows provides the universal CRT, and
    nothing provides the rest but a redistributable something else installed, so a package made from a deployment that
    leans on one starts only where that happened to be done - on a build host always, on a clean machine never."""

    def imports(self, table):
        return lambda path: table.get(os.path.basename(path), set())

    def test_a_runtime_library_a_binary_imports_and_the_deployment_lacks_fails(self):
        self.write('AfterNight.exe')
        self.write('vcruntime140.dll')
        problems = deployed.missing_visual_cpp_runtime(self.directory, self.imports({
            'AfterNight.exe': {'KERNEL32.dll', 'VCRUNTIME140.dll', 'MSVCP140.dll', 'api-ms-win-crt-runtime-l1-1-0.dll'}}))
        self.assertEqual(problems, ['%s: AfterNight.exe imports MSVCP140.dll, a Visual C++ runtime library the '
                                    'deployment does not carry' % self.directory])

    def test_a_deployment_carrying_the_runtime_it_imports_passes(self):
        for name in ('AfterNight.exe', 'AfterNightEngine.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll',
                     'msvcp140_atomic_wait.dll'):
            self.write(name)
        table = {'AfterNight.exe': {'VCRUNTIME140.dll', 'MSVCP140.dll'},
                 'AfterNightEngine.dll': {'VCRUNTIME140_1.dll', 'MSVCP140_ATOMIC_WAIT.dll', 'USER32.dll'}}
        self.assertEqual(deployed.missing_visual_cpp_runtime(self.directory, self.imports(table)), [])

    def test_the_debug_runtime_and_windows_own_libraries_are_not_the_deployments_to_carry(self):
        self.write('AfterNight_d.exe')
        table = {'AfterNight_d.exe': {'VCRUNTIME140D.dll', 'MSVCP140D.dll', 'ucrtbased.dll', 'KERNEL32.dll',
                                      'api-ms-win-crt-heap-l1-1-0.dll'}}
        self.assertEqual(deployed.missing_visual_cpp_runtime(self.directory, self.imports(table)), [])

    def test_a_binary_below_the_deployment_is_not_read(self):
        # A Python package below the deployment carries the runtime it was built against beside itself.
        os.makedirs(os.path.join(self.directory, 'PythonLib'))
        self.write(os.path.join('PythonLib', 'shiboken6.dll'))
        table = {'shiboken6.dll': {'MSVCP140.dll'}}
        self.assertEqual(deployed.missing_visual_cpp_runtime(self.directory, self.imports(table)), [])

    def test_the_deployment_check_holds_it(self):
        self.deploy(WINDOWS_EXPECTATION)
        problems, _ = deployed.check_deployment(self.directory, WINDOWS_EXPECTATION,
                                                imports_of=self.imports({'AfterNight.exe': {'MSVCP140.dll'}}))
        self.assertIn('%s: AfterNight.exe imports MSVCP140.dll, a Visual C++ runtime library the deployment does not '
                      'carry' % self.directory, problems)


class QtLibraryTests(DeploymentCase):
    """A Linux deployment carries in `lib/` every Qt library a binary at its top level needs. A binary that links Qt
    names `$ORIGIN/lib` as its one run path, so a Qt library missing there is the host distribution's - another build,
    usually of another version - and a suite that runs on a build host stops at its first private symbol."""

    def needs(self, table):
        return lambda path: table.get(os.path.basename(path), set())

    def test_a_qt_library_a_binary_needs_and_lib_lacks_fails(self):
        os.makedirs(os.path.join(self.directory, 'lib'))
        self.write(os.path.join('lib', 'libQt6Core.so.6'))
        self.write('Test_Widgets')
        problems = deployed.missing_qt_libraries(self.directory, self.needs({
            'Test_Widgets': {'libQt6Core.so.6', 'libQt6Test.so.6', 'libc.so.6'}}))
        self.assertEqual(problems, ['%s: Test_Widgets needs libQt6Test.so.6, which lib/, its one run path, does not '
                                    'carry, so the host\'s copy is loaded in its place' % self.directory])

    def test_a_deployment_carrying_the_qt_its_binaries_need_passes(self):
        os.makedirs(os.path.join(self.directory, 'lib'))
        for name in ('libQt6Core.so.6', 'libQt6Test.so.6'):
            self.write(os.path.join('lib', name))
        self.write('Test_Widgets')
        self.write('libAfterNightControls.so')
        table = {'Test_Widgets': {'libQt6Core.so.6', 'libQt6Test.so.6'},
                 'libAfterNightControls.so': {'libQt6Core.so.6', 'libkddockwidgets-qt6.so.2.4', 'libstdc++.so.6'}}
        self.assertEqual(deployed.missing_qt_libraries(self.directory, self.needs(table)), [])

    def test_a_binary_below_the_deployment_is_not_read(self):
        # A plugin is loaded by a process whose Qt is already mapped, and a Python package carries its own Qt.
        os.makedirs(os.path.join(self.directory, 'styles'))
        self.write(os.path.join('styles', 'libqafternight.so'))
        table = {'libqafternight.so': {'libQt6Widgets.so.6'}}
        self.assertEqual(deployed.missing_qt_libraries(self.directory, self.needs(table)), [])

    def test_the_deployment_check_holds_it(self):
        self.deploy(LINUX_EXPECTATION)
        problems, _ = deployed.check_deployment(self.directory, LINUX_EXPECTATION,
                                                needed_of=self.needs({'libAfterNightEngine.so': {'libQt6Core.so.6'}}))
        self.assertIn('%s: libAfterNightEngine.so needs libQt6Core.so.6, which lib/, its one run path, does not carry, '
                      'so the host\'s copy is loaded in its place' % self.directory, problems)

    @unittest.skipUnless(open(sys.executable, 'rb').read(4) == b'\x7fELF', 'needs an ELF interpreter to read')
    def test_an_elf_binarys_needed_libraries_are_read_from_its_dynamic_section(self):
        needed = deployed.elf_needed_libraries(sys.executable)
        self.assertTrue(any(name.startswith('libc.so') for name in needed), needed)

    def test_a_file_that_is_not_an_elf_binary_needs_nothing(self):
        self.assertEqual(deployed.elf_needed_libraries(self.write('notes.txt', b'plain text')), set())
        self.assertEqual(deployed.elf_needed_libraries(self.write('empty', b'')), set())


class ResolutionTests(DeploymentCase):
    """Every library a product binary at a Linux deployment's top level needs, and every library those need, resolves
    where the loader will look - the run path of the file that needs it, then the system's cache. A deployment whose
    Qt needs an ICU it does not carry and the system does not have starts nowhere."""

    def table(self, needed, paths):
        return (lambda path: needed.get(os.path.relpath(path, self.directory).replace(os.sep, '/'), set()),
                lambda path: paths.get(os.path.relpath(path, self.directory).replace(os.sep, '/'), []))

    def deploy_qt(self):
        os.makedirs(os.path.join(self.directory, 'lib'))
        self.write('AfterNight')
        self.write(os.path.join('lib', 'libQt6Core.so.6'))
        return ({'AfterNight': {'libQt6Core.so.6', 'libc.so.6'}, 'lib/libQt6Core.so.6': {'libicui18n.so.73', 'libc.so.6'}},
                {'AfterNight': ['$ORIGIN/lib'], 'lib/libQt6Core.so.6': ['$ORIGIN']})

    def test_a_library_neither_the_deployment_nor_the_system_carries_fails(self):
        needed, paths = self.deploy_qt()
        needed_of, run_paths_of = self.table(needed, paths)
        self.assertEqual(deployed.unresolved_libraries(self.directory, needed_of, run_paths_of, {'libc.so.6'}),
                         ['%s: AfterNight needs libicui18n.so.73 through lib/libQt6Core.so.6, which neither the '
                          'deployment nor the system carries' % self.directory])

    def test_a_library_the_deployment_carries_where_the_run_path_looks_passes(self):
        needed, paths = self.deploy_qt()
        self.write(os.path.join('lib', 'libicui18n.so.73'))
        needed_of, run_paths_of = self.table(needed, paths)
        self.assertEqual(deployed.unresolved_libraries(self.directory, needed_of, run_paths_of, {'libc.so.6'}), [])

    def test_a_library_the_system_carries_passes(self):
        needed, paths = self.deploy_qt()
        needed_of, run_paths_of = self.table(needed, paths)
        system = {'libc.so.6', 'libicui18n.so.73'}
        self.assertEqual(deployed.unresolved_libraries(self.directory, needed_of, run_paths_of, system), [])

    def test_a_binary_that_is_not_the_products_is_not_read(self):
        # A third-party runtime's copy beside the product answers for itself.
        self.write('python3.14')
        needed_of, run_paths_of = self.table({'python3.14': {'libpython3.14.so.1.0'}}, {'python3.14': ['$ORIGIN/../lib']})
        self.assertEqual(deployed.unresolved_libraries(self.directory, needed_of, run_paths_of, set()), [])

    def test_a_validation_tool_is_left_to_the_runner_that_states_its_library_path(self):
        for name in ('Test_EngineCore', 'Fuzz_EngineCodec', 'AfterNightEngineBench'):
            self.write(name)
        needed = {name: {'libAfterNightEngine.so'} for name in ('Test_EngineCore', 'Fuzz_EngineCodec',
                                                               'AfterNightEngineBench')}
        needed_of, run_paths_of = self.table(needed, {})
        self.assertEqual(deployed.unresolved_libraries(self.directory, needed_of, run_paths_of, set()), [])

    def test_the_loader_cache_is_read_from_the_system_directories_when_ldconfig_is_not_on_the_path(self):
        # A user's path on Debian names no /sbin, where ldconfig lives.
        listing = ('\tlibc.so.6 (libc6,x86-64, OS ABI: Linux 3.2.0) => /lib/x86_64-linux-gnu/libc.so.6\n'
                   '\tlibc.so.6 (libc6) => /lib/i386-linux-gnu/libc.so.6\n')
        asked = []

        def run(command, **_):
            asked.append(command[0])
            if command[0] == 'ldconfig':
                raise OSError('not on the path')
            return subprocess.CompletedProcess(command, 0, stdout=listing, stderr='')

        self.assertEqual(deployed.system_libraries(run), {'libc.so.6'})
        self.assertEqual(asked, ['ldconfig', '/sbin/ldconfig'])

    def test_a_loader_cache_no_ldconfig_can_read_is_none_rather_than_empty(self):
        def run(command, **_):
            raise OSError('no ldconfig on this host')

        self.assertIsNone(deployed.system_libraries(run))

    def test_a_library_the_deployment_does_not_carry_is_undecided_where_the_loader_cache_cannot_be_read(self):
        # Inspected from a host with no ldconfig - Windows reading a Linux deployment - the check says what it could not
        # decide rather than naming every library of the C runtime as missing.
        needed, paths = self.deploy_qt()
        needed_of, run_paths_of = self.table(needed, paths)
        problems = deployed.unresolved_libraries(self.directory, needed_of, run_paths_of, None,
                                                 read_system=lambda: None)
        self.assertEqual(problems, ['%s: AfterNight needs libc.so.6, libicui18n.so.73, which the deployment does not '
                                    'carry; the loader cache could not be read here (no ldconfig runs on this host), '
                                    'so whether the system carries them is undecided' % self.directory])

    def test_the_deployment_check_holds_it(self):
        self.deploy(LINUX_EXPECTATION)
        needed_of, run_paths_of = self.table({'libAfterNightEngine.so': {'libtbb.so.12'}}, {})
        problems, _ = deployed.check_deployment(self.directory, LINUX_EXPECTATION, needed_of=needed_of,
                                                run_paths_of=run_paths_of, system_libraries=set())
        self.assertIn('%s: libAfterNightEngine.so needs libtbb.so.12, which neither the deployment nor the system '
                      'carries' % self.directory, problems)


class RunPathTests(DeploymentCase):
    """No binary below a Linux deployment names a directory of the machine that built it in its run path. Such a
    directory is one no other machine has, and on the build host it satisfies an import the deployment does not carry,
    so a package made from it starts there and nowhere else."""

    def setUp(self):
        super().setUp()
        self.host = tempfile.mkdtemp(prefix='afternight-build-host-')
        self.addCleanup(os.rmdir, self.host)

    def paths(self, table):
        return lambda path: table.get(os.path.relpath(path, self.directory).replace(os.sep, '/'), [])

    def test_a_run_path_naming_a_directory_of_this_host_fails(self):
        os.makedirs(os.path.join(self.directory, 'lib'))
        self.write(os.path.join('lib', 'libkddockwidgets-qt6.so.2.4.1'))
        self.write('AfterNightExtensionHostLite')
        table = {'lib/libkddockwidgets-qt6.so.2.4.1': ['$ORIGIN/../lib', self.host],
                 'AfterNightExtensionHostLite': ['$ORIGIN', '$ORIGIN/lib']}
        self.assertEqual(deployed.run_paths_of_this_host(self.directory, self.paths(table)),
                         ['%s: lib/libkddockwidgets-qt6.so.2.4.1 names %s in its run path, a directory of the machine '
                          'that built it' % (self.directory, self.host)])

    def test_origin_system_and_absent_directories_are_not_the_hosts(self):
        os.makedirs(os.path.join(self.directory, 'PythonLib', 'lib'))
        self.write(os.path.join('PythonLib', 'lib', 'libtcl9.0.so'))
        self.write('libproxy.so.1')
        table = {'PythonLib/lib/libtcl9.0.so': ['/tools/deps/lib-that-no-host-has'],
                 'libproxy.so.1': ['/usr/lib/x86_64-linux-gnu/libproxy', '/lib', '$ORIGIN/../lib']}
        self.assertEqual(deployed.run_paths_of_this_host(self.directory, self.paths(table)), [])

    def test_a_debug_sidecar_and_a_link_are_not_read(self):
        self.write('libAfterNightEngine.so.debug')
        try:
            os.symlink('libAfterNightEngine.so.debug', os.path.join(self.directory, 'libAfterNightEngine.so'))
        except OSError as error:
            self.skipTest('this account cannot make a file link here: %s' % error)
        table = {'libAfterNightEngine.so.debug': [self.host], 'libAfterNightEngine.so': [self.host]}
        self.assertEqual(deployed.run_paths_of_this_host(self.directory, self.paths(table)), [])

    def test_the_deployment_check_holds_it(self):
        self.deploy(LINUX_EXPECTATION)
        problems, _ = deployed.check_deployment(self.directory, LINUX_EXPECTATION,
                                                run_paths_of=self.paths({'libAfterNightEngine.so': [self.host]}))
        self.assertIn('%s: libAfterNightEngine.so names %s in its run path, a directory of the machine that built it'
                      % (self.directory, self.host), problems)

    @unittest.skipUnless(shutil.which('readelf') and open(sys.executable, 'rb').read(4) == b'\x7fELF',
                         'needs an ELF interpreter and readelf to read it with')
    def test_an_elf_binarys_run_path_is_read_from_its_dynamic_section_as_readelf_reads_it(self):
        output = subprocess.run(['readelf', '-d', sys.executable], capture_output=True, text=True, check=True).stdout
        stated = [entry for value in re.findall(r'Library r(?:un)?path: \[(.*?)\]', output) for entry in value.split(':')]
        self.assertEqual(deployed.elf_run_paths(sys.executable), stated)
        self.assertEqual(deployed.elf_run_paths(self.write('notes.txt', b'plain text')), [])


class LongPathTests(DeploymentCase):
    """Every executable of the product declares itself long-path aware on Windows, because a write into a deep directory
    fails at the pending file it creates in a process that does not, wherever Windows has long paths turned on."""

    AWARE = b'MZ' + b'\0' * 62 + b'<ws2:longPathAware>true</ws2:longPathAware>'

    def test_a_product_executable_without_the_declaration_fails(self):
        self.write('AfterNight.exe', self.AWARE)
        self.write('AfterNightCLI.exe', b'MZ' + b'\0' * 62)
        self.assertEqual(deployed.executables_not_long_path_aware(self.directory),
                         ['%s: AfterNightCLI.exe does not declare itself long-path aware' % self.directory])

    def test_tests_libraries_and_foreign_executables_are_not_held(self):
        for name in ('Test_EngineCore.exe', 'AfterNightEngine.dll', 'python.exe', 'crashpad_handler.exe'):
            self.write(name, b'MZ' + b'\0' * 62)
        self.assertEqual(deployed.executables_not_long_path_aware(self.directory), [])

    def test_an_elf_deployment_is_not_held(self):
        self.write('AfterNightCLI', b'\x7fELF' + b'\0' * 12)
        self.assertEqual(deployed.executables_not_long_path_aware(self.directory), [])


class RepositoryTests(unittest.TestCase):
    """The expectation read from this repository's own generated tree."""

    def test_the_windows_release_expectation_names_both_engines_and_their_symbols(self):
        expected = deployed.expected_deployment(REPO_ROOT, os.path.join(REPO_ROOT, 'bin', 'msvc', 'release'))
        if expected is None:
            self.skipTest('the MSVC tree is not generated in this checkout')
        self.assertIn('AfterNightEngine.dll', expected)
        self.assertEqual(expected['AfterNightEngine.dll'].symbol, 'AfterNightEngine.pdb')
        self.assertEqual(expected['AfterNightEngine.dll'].project, 'AfterNightEngine')
        # The test workspace deploys into the same directory, so the expectation has to span both.
        self.assertIn('Test_EngineCore.exe', expected)

    def test_the_debug_expectation_is_the_suffixed_spelling(self):
        expected = deployed.expected_deployment(REPO_ROOT, os.path.join(REPO_ROOT, 'bin', 'msvc', 'debug'))
        if expected is None:
            self.skipTest('the MSVC tree is not generated in this checkout')
        self.assertIn('AfterNightEngine_d.dll', expected)
        self.assertNotIn('AfterNightEngine.dll', expected)

    def test_an_undeployed_directory_is_reported_rather_than_read_as_clean(self):
        stream = io.StringIO()
        code = deployed.main(['--binaries', os.path.join(REPO_ROOT, 'bin', 'msvc', 'no-such-configuration')],
                             stream=stream)
        self.assertEqual(code, 1)
        self.assertIn('no such directory', stream.getvalue())


if __name__ == '__main__':
    unittest.main(verbosity=2)

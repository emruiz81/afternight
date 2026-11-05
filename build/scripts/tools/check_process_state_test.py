# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_process_state.

Run with:
    python build/scripts/tools/check_process_state_test.py

Most cases build a throwaway tree with a declared table of their own, so the suite needs nothing
from the real repository. The scanner cases matter more than usual here: the whole check rests on
telling a declaration that creates storage from one that declares a function, and that is a lexical
decision about C++ made without a C++ parser. So the shapes this tree actually contains - a factory
returning `Result<T>`, an out-of-line `operator=`, a magic static, a namespace-scope mutex, an
initializer that calls a function - each have a case saying which side of the line they are on.

The last cases read the repository itself, because a checker that only ever runs on synthetic input
proves nothing about the tree it exists to guard.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_process_state as process_state  # noqa: E402


class ScannerCase(unittest.TestCase):
    """What the source scanner calls an object, on one statement at a time."""

    def declared(self, text, indented=False):
        readable = process_state.readable_source(text)
        names = []
        for _, statement, was_indented in process_state.statements(readable):
            name = process_state.declared_object(statement, indented or was_indented)
            if name is not None:
                names.append(name)
        return names

    def test_a_namespace_scope_mutex_is_an_object(self):
        self.assertEqual(self.declared('std::mutex gLibraryMutex;\n'), ['gLibraryMutex'])

    def test_a_function_local_static_is_an_object(self):
        self.assertEqual(self.declared('    static std::mutex lock;\n'), ['lock'])

    def test_a_static_initialized_from_a_call_is_an_object(self):
        self.assertEqual(self.declared('    static const bool kReady = initialize(&callback, &guard);\n'),
                         ['kReady'])

    def test_a_brace_initialized_atomic_is_an_object(self):
        self.assertEqual(self.declared('    static std::atomic<std::uint64_t> counter{0u};\n'), ['counter'])

    def test_a_thread_local_pointer_is_an_object(self):
        self.assertEqual(self.declared('thread_local Scope* tCancellation = nullptr;\n'), ['tCancellation'])

    def test_a_static_factory_is_not_an_object(self):
        self.assertEqual(self.declared('    [[nodiscard]] static Result<Image> create(const Descriptor& d);\n'), [])

    def test_an_out_of_line_assignment_operator_is_not_an_object(self):
        # The `=` of `operator=` is the one place the initializer rule would read a function as an
        # object, and there are thirty of these in the tree.
        self.assertEqual(self.declared('Image& Image::operator=(Image&& other) noexcept = default;\n'), [])

    def test_a_defaulted_comparison_operator_is_not_an_object(self):
        self.assertEqual(self.declared('bool operator==(const Program& left, const Program& right) noexcept;\n'), [])

    def test_a_constexpr_value_is_not_an_object(self):
        self.assertEqual(self.declared('constexpr double kReach = 3.0;\n'), [])
        self.assertEqual(self.declared('inline constexpr std::uint32_t kLattice = 4u;\n'), [])

    def test_an_indented_local_variable_is_not_an_object(self):
        self.assertEqual(self.declared('    std::atomic<std::uint64_t> counted{0u};\n'), [])

    def test_a_wrapped_declaration_is_read_whole(self):
        self.assertEqual(self.declared('std::atomic<PythonInterpreterState*>\n'
                                       '    gActiveAdapter{nullptr};\n'), ['gActiveAdapter'])

    def test_a_declaration_inside_a_comment_is_not_an_object(self):
        self.assertEqual(self.declared('// static std::mutex explained;\n'
                                       '/* static std::mutex alsoExplained; */\n'), [])

    def test_a_declaration_inside_a_string_is_not_an_object(self):
        self.assertEqual(self.declared('const char* kText = "static std::mutex quoted;";\n'), ['kText'])


class DeclaredTreeCase(unittest.TestCase):
    """A throwaway tree, with the declared table replaced for the duration of one case."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-process-state-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        for tree in process_state.SCANNED_TREES:
            (self.root / tree).mkdir(parents=True, exist_ok=True)
        self.original = process_state.DECLARED
        self.addCleanup(self.restore)

    def restore(self):
        process_state.DECLARED = self.original

    def declare(self, *data):
        process_state.DECLARED = tuple(data)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def check(self):
        return process_state.check_source(self.root)

    def test_an_empty_tree_with_an_empty_table_passes(self):
        self.declare()
        self.assertEqual(self.check(), [])

    def test_a_declared_object_passes(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/runtime/Ids.cpp', 'counter', 'nextId',
            process_state.IDENTITY, 'numbers contexts across the process'))
        self.write('src/AfterNightEngine/src/runtime/Ids.cpp',
                   'namespace an {\n'
                   'std::uint64_t nextId() {\n'
                   '    static std::atomic<std::uint64_t> counter{0u};\n'
                   '    return counter.fetch_add(1u);\n'
                   '}\n'
                   '}\n')
        self.assertEqual(self.check(), [])

    def test_an_undeclared_object_is_a_finding(self):
        self.declare()
        self.write('src/AfterNightEngine/src/algorithms/Cache.cpp',
                   'namespace an {\n'
                   'std::map<int, int> gPlanCache;\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('gPlanCache', problems[0])
        self.assertIn('process lifetime', problems[0])

    def test_a_declared_object_that_no_longer_exists_is_a_finding(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/runtime/Gone.cpp', 'removed', '',
            process_state.CONSTANT, 'was once here'))
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('no longer exists', problems[0])

    def test_an_identity_that_is_not_atomic_is_a_finding(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/runtime/Ids.cpp', 'counter', '',
            process_state.IDENTITY, 'numbers contexts across the process'))
        self.write('src/AfterNightEngine/src/runtime/Ids.cpp',
                   'namespace an {\n'
                   'std::uint64_t counter = 0u;\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('not an atomic', problems[0])

    def test_a_constant_that_is_not_const_is_a_finding(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/algorithms/Scale.cpp', 'kScale', '',
            process_state.CONSTANT, 'a scale the language cannot spell'))
        self.write('src/AfterNightEngine/src/algorithms/Scale.cpp',
                   'namespace an {\n'
                   'double kScale = 1.0 / std::exp(-0.5);\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('is not const', problems[0])

    def test_library_state_inside_the_engines_own_code_is_a_finding(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/algorithms/Filter.cpp', 'lock', 'plannerLock',
            process_state.LIBRARY_STATE, 'serializes a library planner'))
        self.write('src/AfterNightEngine/src/algorithms/Filter.cpp',
                   'namespace an {\n'
                   'std::mutex& plannerLock() {\n'
                   '    static std::mutex lock;\n'
                   '    return lock;\n'
                   '}\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('interop', problems[0])

    def test_library_state_inside_an_interop_directory_passes(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/interop/fftw/Plan.cpp', 'lock', 'plannerLock',
            process_state.LIBRARY_STATE, 'serializes a library planner'))
        self.write('src/AfterNightEngine/src/interop/fftw/Plan.cpp',
                   'namespace an {\n'
                   'std::mutex& plannerLock() {\n'
                   '    static std::mutex lock;\n'
                   '    return lock;\n'
                   '}\n'
                   '}\n')
        self.assertEqual(self.check(), [])

    def test_an_adapter_seat_inside_the_engine_is_a_finding(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/runtime/Seat.cpp', 'gActive', '',
            process_state.ADAPTER_SEAT, 'whether the seat is taken'))
        self.write('src/AfterNightEngine/src/runtime/Seat.cpp',
                   'namespace an {\n'
                   'std::atomic<void*> gActive{nullptr};\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('one per process', problems[0])

    def test_an_adapter_seat_inside_an_adapter_passes(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEnginePythonAdapter/src/Runtime.cpp', 'gActive', '',
            process_state.ADAPTER_SEAT, 'whether the seat is taken'))
        self.write('src/AfterNightEnginePythonAdapter/src/Runtime.cpp',
                   'namespace an {\n'
                   'std::atomic<void*> gActive{nullptr};\n'
                   '}\n')
        self.assertEqual(self.check(), [])

    def test_an_unknown_category_is_a_finding(self):
        self.declare(process_state.ProcessDatum(
            'src/AfterNightEngine/src/runtime/Ids.cpp', 'counter', '',
            'convenient', 'it was easier this way'))
        self.write('src/AfterNightEngine/src/runtime/Ids.cpp',
                   'namespace an {\n'
                   'std::atomic<std::uint64_t> counter{0u};\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('not one of', problems[0])

    def test_a_tree_that_is_absent_takes_its_declarations_with_it(self):
        # An optional library exists only in a tree generated with its switch, and removing it deletes
        # the directory. Its entries must not then read as an inventory that has rotted.
        original = process_state.SCANNED_TREES
        self.addCleanup(setattr, process_state, 'SCANNED_TREES', original)
        absent = 'src/AfterNightEngineOptionalLibrary'
        process_state.SCANNED_TREES = original + (absent,)
        self.declare(process_state.ProcessDatum(
            absent + '/src/Runtime.cpp', 'gLoaded', '',
            process_state.ADAPTER_SEAT, 'whether the library this adapter needs is loaded'))
        self.assertEqual(self.check(), [])


class ObjectSymbolCase(unittest.TestCase):
    """What the object reader keeps and what it throws away, without needing a compiler."""

    def test_a_writable_data_symbol_is_kept(self):
        line = "00000000 b struct std::atomic<unsigned __int64> `unsigned __int64 __cdecl " \
               "an::`anonymous namespace'::nextDeviceId(void)'::`2'::counter"
        match = process_state.SYMBOL_LINE.match(line)
        self.assertIsNotNone(match)
        self.assertIn(match.group(1), process_state.WRITABLE_SECTIONS)
        self.assertFalse(process_state.COMPILER_RECORD.search(match.group(2)))

    def test_a_relocated_constant_is_not_writable_state(self):
        """ELF puts a `constexpr` table that needs relocation in `.data.rel.ro`, which `nm` letters
        the same way it letters `.data`. The section is what says it is read-only once loaded."""
        line = ('an::(anonymous namespace)::kByteSwapTable|0000000000000240|   d  |'
                '            OBJECT|0000000000000040|     |.data.rel.ro')
        self.assertFalse(process_state.writable_section(process_state.SYSV_LINE.match(line).group('section')))

    def test_a_function_local_static_in_its_own_bss_section_is_writable_state(self):
        line = ('an::EngineContextState::nextId()::counter|0000000000000000|   V  |'
                '            OBJECT|0000000000000008|     |.bss._ZZN2an18EngineContextState6nextIdEvE7counter')
        match = process_state.SYSV_LINE.match(line)
        self.assertTrue(process_state.writable_section(match.group('section')))
        self.assertEqual(match.group('name'), 'an::EngineContextState::nextId()::counter')

    def test_a_read_only_section_is_not_writable_state(self):
        for section in ('.rodata', '.rdata', '.text', '.data.rel.ro.local'):
            self.assertFalse(process_state.writable_section(section), section)

    def test_the_writable_sections_both_front_ends_emit_are_writable_state(self):
        for section in ('.data', '.bss', '.data$r', '.bss$x'):
            self.assertTrue(process_state.writable_section(section), section)

    def test_a_text_symbol_is_not_a_data_symbol(self):
        match = process_state.SYMBOL_LINE.match("00000000 t $LN10")
        self.assertIsNotNone(match)
        self.assertNotIn(match.group(1), process_state.WRITABLE_SECTIONS)

    def test_run_time_type_information_is_a_compiler_record(self):
        self.assertTrue(process_state.COMPILER_RECORD.search("class an::DeviceState `RTTI Type Descriptor'"))

    def test_a_magic_statics_guard_variable_is_a_compiler_record(self):
        self.assertTrue(process_state.COMPILER_RECORD.search(
            "int `bool __cdecl an::`anonymous namespace'::ready(void)'::`2'::$TSS0"))
        self.assertTrue(process_state.COMPILER_RECORD.search('guard variable for an::ready()::kReady'))

    def test_a_scope_tells_two_counters_apart(self):
        first = process_state.ProcessDatum('a.cpp', 'counter', 'nextDeviceId', process_state.IDENTITY, 'one')
        second = process_state.ProcessDatum('b.cpp', 'counter', 'nextOperationId', process_state.IDENTITY, 'two')
        name = "an::`anonymous namespace'::nextOperationId(void)'::`2'::counter"
        self.assertNotIn(first.scope, name)
        self.assertIn(second.scope, name)


class RepositoryCase(unittest.TestCase):
    """The real tree, which is what the check is for."""

    def test_the_real_repository_is_clean(self):
        problems = process_state.check_source(process_state.REPO_ROOT)
        self.assertEqual(problems, [], '\n'.join(problems))

    def test_the_real_repository_has_something_to_scan(self):
        # A check whose trees had all been renamed would pass the case above by finding nothing at
        # all, which is the way a green check goes quiet.
        found = process_state.scan_source(process_state.REPO_ROOT)
        self.assertGreaterEqual(len(found), 20, 'the scanned trees reached almost nothing')

    def test_every_declaration_names_a_file_that_exists(self):
        for datum in process_state.declared_for(process_state.present_trees(process_state.REPO_ROOT)):
            self.assertTrue((process_state.REPO_ROOT / datum.path).is_file(), datum.path)

    def test_every_declaration_carries_a_reason(self):
        for datum in process_state.DECLARED:
            self.assertIn(datum.category, process_state.CATEGORIES, datum.symbol)
            self.assertGreater(len(datum.reason), 40, datum.symbol)
            self.assertTrue(datum.reason.endswith('.'), datum.symbol)

    def test_the_engine_itself_holds_no_adapter_seat(self):
        # The rule the category placement exists for, stated once as a property of the table rather
        # than as a consequence of the tree that happens to be checked out.
        for datum in process_state.DECLARED:
            if datum.path.startswith(process_state.ENGINE_TREE):
                self.assertNotEqual(datum.category, process_state.ADAPTER_SEAT, datum.path)
                if datum.category == process_state.LIBRARY_STATE:
                    self.assertTrue(datum.path.startswith(process_state.ENGINE_INTEROP), datum.path)

    def test_every_optional_adapter_and_backend_the_build_graph_declares_is_scanned(self):
        # An adapter added to the build and not to this checker is a library whose process state
        # nobody counts.
        import check_build_graph as graph  # noqa: E402
        for adapter in list(graph.OPTIONAL_ADAPTER_SUITES) + list(graph.OPTIONAL_BACKEND_SUITES):
            self.assertIn('src/' + adapter, process_state.SCANNED_TREES)


if __name__ == '__main__':
    unittest.main(verbosity=2)

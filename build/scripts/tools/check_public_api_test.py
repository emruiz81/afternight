# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_public_api.

Run with:
    python build/scripts/tools/check_public_api_test.py

Most cases write a throwaway component holding two or three headers, because what is being tested is
what the walk reads out of a header and which tier a difference lands in, and a synthetic header
states that in five lines where a real one takes two hundred. The repository cases are the other
half: a reader that only ever agrees with headers written to suit it proves nothing about the
surface it guards, and one of them is the property that makes the record usable at all - that
reading the tree twice produces the same snapshot.
"""

from __future__ import annotations

import io
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_public_api as api  # noqa: E402


COMPONENT = api.Component('probe', 'include/Probe', 'PROBE_API', 'Probe')


def header(body):
    """One header around a body, with the namespace every published header of this engine has."""
    return '#pragma once\n\n#include <cstdint>\n\nnamespace an\n{\n%s}\n' % body


def entities(body, component=COMPONENT):
    """Every entity one synthetic header declares."""
    return api._entities_of(component, 'Probe.h', api.declarations(header(body)))


def texts(body, kind):
    """The recorded text of every entity of one kind, in declared order."""
    return [entity.text for entity in entities(body) if entity.kind == kind]


class WalkTests(unittest.TestCase):
    def test_an_exported_declaration_is_recorded_whole(self):
        found = texts('[[nodiscard]] PROBE_API int measure(double value) noexcept;\n', 'api')
        self.assertEqual(found, ['[[nodiscard]] PROBE_API int measure(double value) noexcept'])

    def test_a_declaration_that_is_not_exported_is_not_a_symbol(self):
        self.assertEqual(texts('int measure(double value) noexcept;\n', 'api'), [])

    def test_the_members_of_a_type_are_recorded_in_declared_order(self):
        found = texts('struct Request\n{\n    int first = 1;\n    double second = 2.0;\n};\n', 'member')
        self.assertEqual(found, ['int first = 1', 'double second = 2.0'])

    def test_a_declaration_broken_after_its_opening_parenthesis_reads_as_one_on_one_line(self):
        broken = texts('PROBE_API int measure(\n    double value, int count) noexcept;\n', 'api')
        joined = texts('PROBE_API int measure(double value,\n                      int count) noexcept;\n', 'api')
        self.assertEqual(broken, ['PROBE_API int measure(double value, int count) noexcept'])
        self.assertEqual(joined, broken)

    def test_a_brace_initialized_member_is_a_member(self):
        found = texts('struct Request\n{\n    std::optional<int> region{};\n};\n', 'member')
        self.assertEqual(found, ['std::optional<int> region {}'])

    def test_a_default_argument_in_braces_does_not_open_a_scope(self):
        body = ('class Frame\n{\npublic:\n'
                '    static PROBE_API Frame create(const View& view = {});\n'
                '    int m_first = 0;\n};\n')
        self.assertEqual(texts(body, 'member'), ['int m_first = 0'])

    def test_an_assignment_operator_is_a_function_and_not_a_member(self):
        body = 'class Frame\n{\n    Frame& operator=(const Frame&) = default;\n    int m_first = 0;\n};\n'
        self.assertEqual(texts(body, 'member'), ['int m_first = 0'])

    def test_an_inline_body_declares_nothing(self):
        body = ('class Frame\n{\npublic:\n'
                '    int first() const noexcept\n    {\n        return m_first;\n    }\n'
                '    int m_first = 0;\n};\n')
        self.assertEqual(texts(body, 'member'), ['int m_first = 0'])

    def test_an_enumeration_records_its_enumerators_in_order(self):
        found = texts('enum class Access : std::uint8_t\n{\n    WholeFrame,\n    Region\n};\n', 'enum')
        self.assertEqual(found, ['WholeFrame Region'])

    def test_an_enumerator_with_a_value_records_the_value(self):
        found = texts('enum class Access\n{\n    WholeFrame = 3,\n    Region = 4\n};\n', 'enum')
        self.assertEqual(found, ['WholeFrame=3 Region=4'])

    def test_a_decorated_type_is_named_by_the_type_and_not_by_its_decoration(self):
        """A polymorphic type a caller derives from carries a visibility decoration on some
        platforms, and the decoration is not what the type is called."""
        found = [entity.name for entity in entities('class PROBE_INTERFACE Codec\n{\n};\n')
                 if entity.kind == 'type']
        self.assertEqual(found, ['an::Codec'])

    def test_a_decoration_is_part_of_the_recorded_shape(self):
        found = [entity.text for entity in entities('class PROBE_INTERFACE Codec\n{\n};\n')
                 if entity.kind == 'type']
        self.assertEqual(found, ['class PROBE_INTERFACE Codec'])

    def test_a_type_whose_own_name_is_capitals_is_still_named_by_it(self):
        found = [entity.name for entity in entities('struct RGB\n{\n};\n') if entity.kind == 'type']
        self.assertEqual(found, ['an::RGB'])

    def test_a_nested_type_carries_the_name_it_is_nested_in(self):
        names = [entity.name for entity in entities('struct Outer\n{\n    struct Inner\n    {\n'
                                                    '        int value = 0;\n    };\n};\n')
                 if entity.kind == 'type']
        self.assertEqual(names, ['an::Outer', 'an::Outer::Inner'])

    def test_a_specialization_is_named_by_its_arguments(self):
        body = ('template <typename T> struct Traits\n{\n    int value = 0;\n};\n'
                'template <> struct Traits<double>\n{\n    double value = 0.0;\n};\n')
        names = [entity.name for entity in entities(body) if entity.kind == 'type']
        self.assertEqual(names, ['an::Traits', 'an::Traits<double>'])

    def test_a_comment_declares_nothing(self):
        body = ('/// A member this type does not have: int phantom = 0;\n'
                'struct Request\n{\n    int first = 0;\n};\n')
        self.assertEqual(texts(body, 'member'), ['int first = 0'])

    def test_a_directive_declares_nothing(self):
        body = '#define PROBE_THING(x) { x }\nstruct Request\n{\n    int first = 0;\n};\n'
        self.assertEqual(texts(body, 'member'), ['int first = 0'])


class DifferenceTests(unittest.TestCase):
    def snapshot(self, body, version='1.0.0', generation=1):
        """A recorded snapshot of one synthetic header."""
        found = entities(body)
        digest = api.header_digest(header(body))
        return version, generation, {('probe', 'Probe.h'): digest}, found

    def tiers(self, before, after):
        """The tier of every difference between two versions of one header."""
        return [tier for tier, _ in api.differences(self.snapshot(before), self.snapshot(after))]

    def messages(self, before, after):
        return [message for _, message in api.differences(self.snapshot(before), self.snapshot(after))]

    def test_a_member_added_to_a_public_type_is_an_interface_change(self):
        before = 'struct Request\n{\n    int first = 0;\n};\n'
        after = 'struct Request\n{\n    int first = 0;\n    int second = 0;\n};\n'
        self.assertIn(api.ABI, self.tiers(before, after))

    def test_a_member_inserted_before_another_is_an_interface_change(self):
        before = 'struct Request\n{\n    int first = 0;\n    int second = 0;\n};\n'
        after = 'struct Request\n{\n    int inserted = 0;\n    int first = 0;\n    int second = 0;\n};\n'
        self.assertIn(api.ABI, self.tiers(before, after))

    def test_a_changed_parameter_is_an_interface_change(self):
        before = 'PROBE_API int measure(double value);\n'
        after = 'PROBE_API int measure(float value);\n'
        self.assertIn(api.ABI, self.tiers(before, after))

    def test_a_withdrawn_declaration_is_an_interface_change(self):
        before = 'PROBE_API int measure(double value);\nPROBE_API int count();\n'
        after = 'PROBE_API int count();\n'
        self.assertEqual(self.tiers(before, after).count(api.ABI), 1)

    def test_a_renumbered_enumerator_is_an_interface_change(self):
        before = 'enum class Access\n{\n    WholeFrame = 0,\n    Region = 1\n};\n'
        after = 'enum class Access\n{\n    WholeFrame = 1,\n    Region = 0\n};\n'
        self.assertIn(api.ABI, self.tiers(before, after))

    def test_an_enumerator_appended_at_the_end_is_surface_alone(self):
        before = 'enum class Access\n{\n    WholeFrame,\n    Region\n};\n'
        after = 'enum class Access\n{\n    WholeFrame,\n    Region,\n    Tile\n};\n'
        tiers = self.tiers(before, after)
        self.assertIn(api.SURFACE, tiers)
        self.assertNotIn(api.ABI, tiers)

    def test_an_enumerator_inserted_in_the_middle_is_an_interface_change(self):
        before = 'enum class Access\n{\n    WholeFrame,\n    Region\n};\n'
        after = 'enum class Access\n{\n    WholeFrame,\n    Tile,\n    Region\n};\n'
        self.assertIn(api.ABI, self.tiers(before, after))

    def test_a_type_that_gains_a_visible_identity_is_surface_alone(self):
        """A decoration adds a symbol to the library and takes none away, so an object compiled
        against the recorded header still finds everything it found before."""
        before = 'class Codec\n{\n    int first = 0;\n};\n'
        after = 'class PROBE_INTERFACE Codec\n{\n    int first = 0;\n};\n'
        self.assertEqual(self.tiers(before, after), [api.SURFACE, api.SURFACE])

    def test_a_type_that_loses_a_visible_identity_is_an_interface_change(self):
        """The other direction is not: a consumer that derives from the type stops finding it."""
        before = 'class PROBE_INTERFACE Codec\n{\n    int first = 0;\n};\n'
        after = 'class Codec\n{\n    int first = 0;\n};\n'
        self.assertIn(api.ABI, self.tiers(before, after))

    def test_a_type_that_exchanges_one_decoration_for_another_is_an_interface_change(self):
        before = 'class PROBE_INTERFACE Codec\n{\n    int first = 0;\n};\n'
        after = 'class PROBE_API Codec\n{\n    int first = 0;\n};\n'
        self.assertIn(api.ABI, self.tiers(before, after))

    def test_an_added_overload_is_surface_alone(self):
        before = 'PROBE_API int measure(double value);\n'
        after = 'PROBE_API int measure(double value);\nPROBE_API int measure(float value);\n'
        tiers = self.tiers(before, after)
        self.assertIn(api.SURFACE, tiers)
        self.assertNotIn(api.ABI, tiers)

    def test_an_added_type_is_surface_alone(self):
        before = 'struct Request\n{\n    int first = 0;\n};\n'
        after = 'struct Request\n{\n    int first = 0;\n};\nstruct Report\n{\n    int count = 0;\n};\n'
        self.assertNotIn(api.ABI, self.tiers(before, after))

    def test_a_changed_inline_body_is_surface_alone(self):
        before = 'inline int measure()\n{\n    return 1;\n}\n'
        after = 'inline int measure()\n{\n    return 2;\n}\n'
        tiers = self.tiers(before, after)
        self.assertEqual(tiers, [api.SURFACE])

    def test_a_rewritten_comment_is_no_difference_at_all(self):
        before = '/// One sentence.\nstruct Request\n{\n    int first = 0;\n};\n'
        after = '/// Another sentence entirely, saying more.\nstruct Request\n{\n    int first = 0;\n};\n'
        self.assertEqual(self.tiers(before, after), [])

    def test_a_changed_default_string_is_seen_by_the_digest(self):
        before = 'struct Request\n{\n    const char* stage = "read";\n};\n'
        after = 'struct Request\n{\n    const char* stage = "write";\n};\n'
        self.assertEqual(self.tiers(before, after), [api.SURFACE])


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)
        (self.root / api.VERSION_HEADER).parent.mkdir(parents=True, exist_ok=True)
        self.write_version('0.1.0', 1)
        self.component = api.Component('probe', 'include/Probe', 'PROBE_API', 'Probe')
        (self.root / 'include' / 'Probe').mkdir(parents=True, exist_ok=True)
        self.write_header('struct Request\n{\n    int first = 0;\n};\n')
        self.components = api.COMPONENTS
        api.COMPONENTS = (self.component,)
        self.addCleanup(self.restore)
        (self.root / 'build' / 'scripts' / 'tools').mkdir(parents=True, exist_ok=True)

    def restore(self):
        api.COMPONENTS = self.components

    def write_version(self, version, generation):
        (self.root / api.VERSION_HEADER).write_text(
            'inline constexpr const char* kEngineVersionString = "%s";\n'
            'inline constexpr int kEngineAbiGeneration = %d;\n' % (version, generation),
            encoding='utf-8')

    def write_header(self, body):
        (self.root / 'include' / 'Probe' / 'Probe.h').write_text(header(body), encoding='utf-8')

    def run_check(self):
        stream = io.StringIO()
        code = api.main(['--repo-root', str(self.root), '--snapshot', api.SNAPSHOT], stream=stream)
        return code, stream.getvalue()

    def record(self):
        return api.main(['--repo-root', str(self.root), '--record', '--snapshot', api.SNAPSHOT],
                        stream=io.StringIO())

    def test_a_recorded_surface_checks_clean(self):
        self.record()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_an_unrecorded_surface_says_so(self):
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('never been recorded', output)

    def test_an_interface_change_needs_the_generation_to_move(self):
        self.record()
        self.write_header('struct Request\n{\n    int first = 0;\n    int second = 0;\n};\n')
        self.write_version('0.2.0', 1)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('kEngineAbiGeneration is still 1', output)
        self.write_version('0.2.0', 2)
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertNotIn('kEngineAbiGeneration is still', output)
        self.assertIn('record it again', output)

    def test_an_addition_needs_the_version_to_move_and_not_the_generation(self):
        self.record()
        self.write_header('struct Request\n{\n    int first = 0;\n};\nPROBE_API int count();\n')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('while the version stays 0.1.0', output)
        self.assertNotIn('kEngineAbiGeneration is still', output)

    def test_a_version_that_moved_without_the_surface_is_re_recorded(self):
        self.record()
        self.write_version('0.1.1', 1)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('names the version it belongs to', output)

    def test_recording_again_closes_every_difference(self):
        self.record()
        self.write_header('struct Request\n{\n    int first = 0;\n    int second = 0;\n};\n')
        self.write_version('0.2.0', 2)
        self.record()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


class HistoryTests(unittest.TestCase):
    """The history reads each commit out of git, so these build a small repository a commit at a time."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)
        self.components = api.COMPONENTS
        api.COMPONENTS = (api.Component('probe', 'include/Probe', 'PROBE_API', 'Probe'),)
        self.addCleanup(self.restore)
        self.git('init', '-q')
        self.git('config', 'user.email', 'probe@example.invalid')
        self.git('config', 'user.name', 'Probe')
        self.git('config', 'core.autocrlf', 'false')
        (self.root / api.VERSION_HEADER).parent.mkdir(parents=True, exist_ok=True)
        (self.root / 'include' / 'Probe').mkdir(parents=True, exist_ok=True)
        (self.root / 'build' / 'scripts' / 'tools').mkdir(parents=True, exist_ok=True)
        self.commit('struct Request\n{\n    int first = 0;\n};\n', '0.1.0', 1, 'base')
        self.base = self.git('rev-parse', 'HEAD').strip()

    def restore(self):
        api.COMPONENTS = self.components

    def git(self, *arguments):
        return api._git(self.root, *arguments).decode('utf-8')

    def commit(self, body, version, generation, message, record=True):
        (self.root / api.VERSION_HEADER).write_text(
            'inline constexpr const char* kEngineVersionString = "%s";\n'
            'inline constexpr int kEngineAbiGeneration = %d;\n' % (version, generation), encoding='utf-8')
        (self.root / 'include' / 'Probe' / 'Probe.h').write_text(header(body), encoding='utf-8')
        if record:
            api.record(self.root)
        self.git('add', '-A')
        self.git('commit', '-q', '-m', message)

    def history(self):
        return api.history(self.root, self.base)

    def test_changes_that_obey_both_tiers_pass_and_are_each_listed(self):
        self.commit('struct Request\n{\n    int first = 0;\n};\nPROBE_API int count();\n', '0.2.0', 1, 'add')
        self.commit('struct Request\n{\n    int first = 0;\n    int second = 0;\n};\nPROBE_API int count();\n',
                    '0.3.0', 2, 'grow')
        lines, problems = self.history()
        self.assertEqual(problems, [])
        self.assertEqual(len(lines), 2, lines)
        # The changed header's digest is a difference of its own, beside the declaration or member it changed.
        self.assertIn('0.1.0/1 -> 0.2.0/1: 2 surface and 0 interface difference(s) - add', lines[0])
        self.assertIn('0.2.0/1 -> 0.3.0/2: 1 surface and 1 interface difference(s) - grow', lines[1])

    def test_a_surface_change_under_an_unmoved_version_is_refused(self):
        self.commit('struct Request\n{\n    int first = 0;\n};\nPROBE_API int count();\n', '0.1.0', 1, 'add')
        _, problems = self.history()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('the surface changed and the version stayed 0.1.0', problems[0])

    def test_an_interface_change_under_an_unmoved_generation_is_refused(self):
        self.commit('struct Request\n{\n    int first = 0;\n    int second = 0;\n};\n', '0.2.0', 1, 'grow')
        _, problems = self.history()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('an interface change left the generation at 1', problems[0])

    def test_a_commit_that_left_its_record_stale_is_refused(self):
        self.commit('struct Request\n{\n    int first = 0;\n};\nPROBE_API int count();\n', '0.2.0', 1, 'stale',
                    record=False)
        _, problems = self.history()
        self.assertTrue(any('does not describe its own surface' in problem for problem in problems), problems)

    def test_a_later_commit_that_moves_the_version_does_not_hide_an_earlier_one(self):
        self.commit('struct Request\n{\n    int first = 0;\n};\nPROBE_API int count();\n', '0.1.0', 1, 'early')
        self.commit('struct Request\n{\n    int first = 0;\n};\nPROBE_API int count();\nPROBE_API int more();\n',
                    '0.2.0', 1, 'later')
        self.assertEqual(api.check(self.root), [])
        _, problems = self.history()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('early', problems[0])

    def test_a_comment_is_listed_with_no_difference(self):
        self.commit('/// A request.\nstruct Request\n{\n    int first = 0;\n};\n', '0.1.0', 1, 'comment')
        lines, problems = self.history()
        self.assertEqual(problems, [])
        self.assertIn('0.1.0/1 -> 0.1.0/1: 0 surface and 0 interface difference(s) - comment', lines[0])

    def test_the_command_line_answers_with_its_exit_code(self):
        self.commit('struct Request\n{\n    int first = 0;\n};\nPROBE_API int count();\n', '0.1.0', 1, 'add')
        stream = io.StringIO()
        self.assertEqual(api.main(['--repo-root', str(self.root), '--history', self.base], stream=stream), 1)
        self.assertIn('ERROR:', stream.getvalue())
        stream = io.StringIO()
        self.assertEqual(api.main(['--repo-root', str(self.root), '--history', 'HEAD'], stream=stream), 0)
        self.assertIn('0 commit(s) since HEAD', stream.getvalue())


class RenameHistoryTests(unittest.TestCase):
    """The history reads a commit that spells the engine's names as the digests do under the published names, so a
    commit that renames the engine's directory, its export macro and the record moves nothing it publishes."""

    FROZEN = 'AFTERNIGHTENGINE' + 'NEXT'
    FROZEN_NAME = 'AfterNightEngine' + 'Next'

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)
        self.components = api.COMPONENTS
        self.version_header = api.VERSION_HEADER
        self.addCleanup(self.restore)
        self.git('init', '-q')
        self.git('config', 'user.email', 'probe@example.invalid')
        self.git('config', 'user.name', 'Probe')
        self.git('config', 'core.autocrlf', 'false')
        self.spell(self.FROZEN_NAME, self.FROZEN)
        self.write('struct Request\n{\n    int first = 0;\n};\n%s_API int count();\n' % self.FROZEN, '0.1.0', 1)
        api.record(self.root)
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'base')
        self.base = self.git('rev-parse', 'HEAD').strip()

    def restore(self):
        api.COMPONENTS = self.components
        api.VERSION_HEADER = self.version_header

    def git(self, *arguments):
        return api._git(self.root, *arguments).decode('utf-8')

    def spell(self, name, macro):
        """Point the reader at the engine as the tree spells it."""
        root = 'src/%s/include/%s' % (name, name)
        api.COMPONENTS = (api.Component('engine', root, macro + '_API', name),)
        api.VERSION_HEADER = root + '/Core/Version.h'

    def write(self, body, version, generation):
        (self.root / api.VERSION_HEADER).parent.mkdir(parents=True, exist_ok=True)
        (self.root / api.VERSION_HEADER).write_text(
            'inline constexpr const char* kEngineVersionString = "%s";\n'
            'inline constexpr int kEngineAbiGeneration = %d;\n' % (version, generation), encoding='utf-8')
        (self.root / api.COMPONENTS[0].include_root / 'Probe.h').write_text(header(body), encoding='utf-8')
        (self.root / 'build' / 'scripts' / 'tools').mkdir(parents=True, exist_ok=True)

    def rename_engine(self):
        """What the rename does to this tree: the directory moved, and every file respelled."""
        self.git('mv', 'src/%s' % self.FROZEN_NAME, 'src/AfterNightEngine')
        self.git('mv', 'src/AfterNightEngine/include/%s' % self.FROZEN_NAME,
                 'src/AfterNightEngine/include/AfterNightEngine')
        for path in self.root.rglob('*'):
            if path.is_file() and '.git' not in path.parts:
                text = path.read_text(encoding='utf-8')
                path.write_text(text.replace(self.FROZEN_NAME, 'AfterNightEngine').replace(self.FROZEN,
                                                                                            'AFTERNIGHTENGINE'),
                                encoding='utf-8')
        self.spell('AfterNightEngine', 'AFTERNIGHTENGINE')

    def test_a_commit_that_renames_the_engine_moves_nothing_it_publishes(self):
        self.rename_engine()
        self.assertEqual(api.check(self.root), [], 'the renamed record describes the renamed tree')
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'rename')
        lines, problems = api.history(self.root, self.base)
        self.assertEqual(problems, [])
        self.assertEqual(len(lines), 1, lines)
        self.assertIn('0.1.0/1 -> 0.1.0/1: 0 surface and 0 interface difference(s) - rename', lines[0])

    def test_a_surface_change_beside_the_rename_is_still_seen(self):
        self.rename_engine()
        self.write('struct Request\n{\n    int first = 0;\n};\nAFTERNIGHTENGINE_API int count();\n'
                   'AFTERNIGHTENGINE_API int more();\n', '0.1.0', 1)
        api.record(self.root)
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'rename and more')
        _, problems = api.history(self.root, self.base)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('the surface changed and the version stayed 0.1.0', problems[0])


class JoiningTests(unittest.TestCase):
    """A formatter that moves a line break to just after a declaration's opening parenthesis, or from there, changes
    nothing the header declares. A record states how it joined a declaration's lines, and one written before it said
    so - which joined them with the space that break leaves - is read under its own rule, so the history crosses the
    commit that changed the rule."""

    BROKEN = 'PROBE_API int measure(\n    double value, int count) noexcept;\n'
    JOINED = 'PROBE_API int measure(double value,\n                      int count) noexcept;\n'

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)
        self.components = api.COMPONENTS
        api.COMPONENTS = (api.Component('probe', 'include/Probe', 'PROBE_API', 'Probe'),)
        self.addCleanup(self.restore)
        self.git('init', '-q')
        self.git('config', 'user.email', 'probe@example.invalid')
        self.git('config', 'user.name', 'Probe')
        self.git('config', 'core.autocrlf', 'false')
        (self.root / 'include' / 'Probe').mkdir(parents=True, exist_ok=True)
        (self.root / api.VERSION_HEADER).parent.mkdir(parents=True, exist_ok=True)
        (self.root / api.VERSION_HEADER).write_text(
            'inline constexpr const char* kEngineVersionString = "0.1.0";\n'
            'inline constexpr int kEngineAbiGeneration = 1;\n', encoding='utf-8')
        (self.root / 'build' / 'scripts' / 'tools').mkdir(parents=True, exist_ok=True)

    def restore(self):
        api.COMPONENTS = self.components

    def git(self, *arguments):
        return api._git(self.root, *arguments).decode('utf-8')

    def write(self, body):
        text = header(body)
        (self.root / 'include' / 'Probe' / 'Probe.h').write_text(text, encoding='utf-8')
        return text

    def commit(self, message):
        self.git('add', '-A')
        self.git('commit', '-q', '-m', message)

    def write_earlier_record(self, text):
        """The record the tool wrote before it stated its joining: the break's space kept, in the text and the
        digest."""
        lines = ['version 0.1.0 1',
                 'api probe Probe.h an PROBE_API int measure( double value, int count) noexcept',
                 'header probe Probe.h %s' % api.header_digest(text, joining=1)]
        (self.root / api.SNAPSHOT).write_bytes(('\r\n'.join(lines) + '\r\n').encode('utf-8'))

    def test_a_line_break_after_an_opening_parenthesis_moves_no_digest(self):
        self.assertEqual(api.header_digest(header(self.BROKEN)), api.header_digest(header(self.JOINED)))

    def test_a_record_written_before_the_joining_was_stated_still_describes_its_tree(self):
        self.write_earlier_record(self.write(self.BROKEN))
        self.assertEqual(api.check(self.root), [])

    def test_the_history_crosses_the_record_that_states_its_joining_and_a_reflow_after_it(self):
        self.write_earlier_record(self.write(self.BROKEN))
        self.commit('base')
        base = self.git('rev-parse', 'HEAD').strip()
        api.record(self.root)
        self.commit('record the joining')
        self.write(self.JOINED)
        self.assertEqual(api.check(self.root), [], 'the reflowed header publishes what was recorded')
        self.commit('reflow')
        lines, problems = api.history(self.root, base)
        self.assertEqual(problems, [])
        self.assertEqual(len(lines), 2, lines)
        self.assertIn('0.1.0/1 -> 0.1.0/1: 0 surface and 0 interface difference(s) - record the joining', lines[0])
        self.assertIn('0.1.0/1 -> 0.1.0/1: 0 surface and 0 interface difference(s) - reflow', lines[1])

    def test_a_record_states_its_joining(self):
        self.write(self.BROKEN)
        api.record(self.root)
        recorded = (self.root / api.SNAPSHOT).read_text(encoding='utf-8').splitlines()
        self.assertEqual(recorded[1], 'joining %d' % api.JOINING)


class SymbolTests(unittest.TestCase):
    def test_a_microsoft_mangling_gives_up_its_function_name(self):
        self.assertEqual(api.microsoft_name('?engineVersion@an@@YA?AUEngineVersion@1@XZ'), 'engineVersion')
        self.assertEqual(api.microsoft_name('?create@ImageStorageBytes@an@@SA@XZ'), 'create')
        self.assertEqual(api.microsoft_name('??0ImageStorageBytes@an@@QEAA@XZ'), 'ImageStorageBytes')
        self.assertEqual(api.microsoft_name('??1ImageStorageBytes@an@@QEAA@XZ'), 'ImageStorageBytes')
        self.assertEqual(api.microsoft_name('??8an@@YA_NAEBUEngineVersion@0@0@Z'), 'operator')

    def test_an_itanium_mangling_gives_up_its_function_name(self):
        self.assertEqual(api.itanium_name('_ZN2an13engineVersionEv'), 'engineVersion')
        self.assertEqual(api.itanium_name('_ZNK2an5Image6layoutEv'), 'layout')
        self.assertEqual(api.itanium_name('_ZN2an17ImageStorageBytesC1EOS0_'), 'ImageStorageBytes')
        self.assertEqual(api.itanium_name('_ZN2aneqERKNS_13EngineVersionES2_'), 'operator')

    def test_a_declaration_gives_up_the_name_a_symbol_table_answers_to(self):
        self.assertEqual(api.declared_name('[[nodiscard]] PROBE_API int measure(double value) noexcept'),
                         'measure')
        self.assertEqual(api.declared_name('static PROBE_API Result<Frame> create(const View& view)'),
                         'create')
        self.assertEqual(api.declared_name('friend PROBE_API bool operator==(const Frame&, const Frame&)'),
                         'operator')

    def test_an_export_table_is_read_for_its_names(self):
        output = ('Export {\n  Ordinal: 1\n  Name: ?engineVersion@an@@YAXXZ\n}\n'
                  'Export {\n  Ordinal: 2\n  Name: ?count@an@@YAHXZ\n}\n')
        self.assertEqual(api.exported_names(output, True), {'engineVersion', 'count'})

    def test_a_debug_deployment_is_read_as_readily_as_a_release_one(self):
        directory = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(lambda: [path.unlink() for path in directory.iterdir()] and None)
        component = api.Component('probe', 'include/Probe', 'PROBE_API', 'Probe')
        self.assertIsNone(api._library_path(directory, component))
        (directory / 'Probe_d.dll').write_bytes(b'MZ')
        self.assertEqual(api._library_path(directory, component).name, 'Probe_d.dll')

    def test_an_undefined_dynamic_symbol_is_not_an_export(self):
        output = ('DynamicSymbol {\n  Name: _ZN2an13engineVersionEv (11)\n  Section: .text\n}\n'
                  'DynamicSymbol {\n  Name: _ZN3std6memcpyEv (22)\n  Section: Undefined\n}\n')
        self.assertEqual(api.exported_names(output, False), {'engineVersion'})


    def test_a_mangling_gives_up_the_namespace_it_opens_with(self):
        self.assertEqual(api.itanium_namespace('_ZN2an13engineVersionEv'), 'an')
        self.assertEqual(api.itanium_namespace('_ZNK2an5Image6layoutEv'), 'an')
        self.assertEqual(api.itanium_namespace('_ZN8openxisf6readerD1Ev'), 'openxisf')
        self.assertIsNone(api.itanium_namespace('_ZNSt6vectorIiED1Ev'))
        self.assertIsNone(api.itanium_namespace('png_read_row'))

    def test_a_symbol_of_another_library_is_not_read_as_this_engines(self):
        """Namespace std is default-visible by the standard library's own decision, and another
        library's types are visible by theirs, so neither is this engine publishing its internals."""
        output = ('DynamicSymbol {\n  Name: _ZN2an13engineVersionEv (11)\n  Section: .text\n}\n'
                  'DynamicSymbol {\n  Name: _ZNSt6vectorIiED1Ev (22)\n  Section: .text\n}\n'
                  'DynamicSymbol {\n  Name: _ZN8openxisf6readerD1Ev (33)\n  Section: .text\n}\n')
        self.assertEqual(api.exported_names(output, False), {'engineVersion'})
        self.assertEqual(api.foreign_symbols(output),
                         {'_ZNSt6vectorIiED1Ev', '_ZN8openxisf6readerD1Ev'})

    def test_a_foreign_symbol_that_is_undefined_is_not_read_at_all(self):
        output = ('DynamicSymbol {\n  Name: _ZNSt6vectorIiED1Ev (22)\n  Section: Undefined\n}\n')
        self.assertEqual(api.foreign_symbols(output), set())

    def test_an_export_table_is_never_read_for_foreign_symbols(self):
        """Only a decorated declaration reaches a COFF export table, so there is nothing to sort."""
        output = 'Export {\n  Ordinal: 1\n  Name: ?engineVersion@an@@YAXXZ\n}\n'
        self.assertEqual(api.foreign_symbols(output), set())


    def test_a_reading_that_opened_nothing_says_so_rather_than_passing(self):
        with tempfile.TemporaryDirectory() as directory:
            problems, reading = api.check_binaries(api.REPO_ROOT, directory, tool=None)
        self.assertTrue(problems)
        self.assertEqual(reading, (0, 0, 0))


    def test_a_type_decorated_for_visible_identity_is_part_of_the_binary_interface(self):
        """It exports its type information and its vtable rather than a declaration, and a caller
        that derives from it needs both, so a library that exports them is not publishing internals.
        """
        found = entities('class PROBE_INTERFACE Codec\n{\n    int first = 0;\n};\n'
                         '[[nodiscard]] PROBE_API int measure(double value) noexcept;\n')
        self.assertEqual(api.declared_binary_names(found, portable_executable=False), {'Codec', 'measure'})

    def test_an_export_table_owes_nothing_for_a_decorated_type(self):
        """The decoration is empty on Windows, and the compiler there emits a polymorphic type's
        vtable and type information in every unit that needs them, so the library exports neither and
        a caller that derives from the type links without them."""
        found = entities('class PROBE_INTERFACE Codec\n{\n    int first = 0;\n};\n'
                         '[[nodiscard]] PROBE_API int measure(double value) noexcept;\n')
        self.assertEqual(api.declared_binary_names(found, portable_executable=True), {'measure'})

    def test_an_undecorated_type_declares_no_symbol_of_its_own(self):
        found = entities('struct Request\n{\n    int first = 0;\n};\n')
        self.assertEqual(api.declared_binary_names(found, portable_executable=False), set())
        self.assertEqual(api.declared_binary_names(found, portable_executable=True), set())


class FrozenSpellingTests(unittest.TestCase):
    """A rename of the export macros and the library's name moves no header digest."""

    FROZEN = 'AFTERNIGHTENGINE' + 'NEXT'
    FROZEN_NAME = 'AfterNightEngine' + 'Next'

    def header(self, macro, name):
        return ('namespace an {\n[[nodiscard]] %s_API int f() noexcept;\n'
                'inline constexpr const char* kName = "%s";\n}\n' % (macro, name))

    def test_a_header_spelled_with_the_canonical_names_digests_as_the_frozen_one(self):
        frozen = api.header_digest(self.header(self.FROZEN, self.FROZEN_NAME))
        canonical = api.header_digest(self.header('AFTERNIGHTENGINE', 'AfterNightEngine'))
        self.assertEqual(canonical, frozen)

    def test_any_other_change_still_moves_the_digest(self):
        frozen = api.header_digest(self.header(self.FROZEN, self.FROZEN_NAME))
        self.assertNotEqual(api.header_digest(self.header(self.FROZEN, 'OtherName')), frozen)


class RepositoryTests(unittest.TestCase):
    def test_the_repository_passes(self):
        stream = io.StringIO()
        code = api.main([], stream=stream)
        self.assertEqual(code, 0, stream.getvalue())

    def test_the_identity_decoration_is_empty_only_where_the_export_table_is_read_without_it(self):
        """The binary reading owes a decorated type nothing on Windows because the engine's own
        header makes the decoration empty there. Were that header to give it a meaning, an export
        table would owe the type's symbols, so the rule and the header are read together."""
        self.assertTrue(api.identity_decoration_is_empty_on_windows(api.REPO_ROOT))

    def test_reading_the_tree_twice_produces_the_same_record(self):
        first = api.snapshot_lines(api.REPO_ROOT)
        second = api.snapshot_lines(api.REPO_ROOT)
        self.assertEqual(first, second)

    def test_the_record_is_the_one_in_the_tree(self):
        recorded = (api.REPO_ROOT / api.SNAPSHOT).read_text(encoding='utf-8')
        self.assertEqual(recorded.replace('\r\n', '\n').strip().split('\n'),
                         api.snapshot_lines(api.REPO_ROOT))

    def test_every_recorded_line_is_readable_again(self):
        text = (api.REPO_ROOT / api.SNAPSHOT).read_text(encoding='utf-8')
        version, generation, digests, entities_read = api.parse_snapshot(text)
        self.assertEqual((version, generation), api.read_version(api.REPO_ROOT))
        self.assertGreater(len(digests), 200)
        self.assertGreater(len(entities_read), 5000)

    def test_the_engine_publishes_the_types_the_freeze_is_about(self):
        entities_read, _ = api.read_surface(api.REPO_ROOT)
        names = {entity.name for entity in entities_read if entity.kind == 'type'}
        for named in ('an::EngineContext', 'an::ExecutionContext', 'an::ImageDescriptor',
                      'an::StackFrameSource'):
            self.assertIn(named, names)

    def test_the_optional_adapters_are_part_of_the_frozen_surface(self):
        entities_read, _ = api.read_surface(api.REPO_ROOT)
        components = {entity.component for entity in entities_read}
        self.assertEqual(components, {'engine', 'python-adapter', 'gmic-adapter'})


if __name__ == '__main__':
    unittest.main(verbosity=2)

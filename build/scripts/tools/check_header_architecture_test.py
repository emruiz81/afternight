# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_header_architecture.

Run with:
    python build/scripts/tools/check_header_architecture_test.py

Most cases write a throwaway tree holding only the files the checker reads, so the suite needs
nothing from the real repository and writes nothing outside its own temporary directory. The last
case reads the repository itself, because a checker that only ever passes on synthetic input proves
nothing about the tree it exists to guard.
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

import check_header_architecture as architecture  # noqa: E402


ENGINE = 'src/AfterNightEngine'
ADAPTER = 'src/AfterNightEnginePythonAdapter'

SMALL_HEADER = (
    '// A description of what this header is for.\n'
    '#pragma once\n'
    '\n'
    '#include <cstdint>\n'
    '\n'
    'namespace an\n'
    '{\n'
    '/// One value.\n'
    'enum class Small : std::uint8_t\n'
    '{\n'
    '    One,\n'
    '};\n'
    '}\n'
)


def declarations(count, name='filler'):
    """A body of `count` declaration lines, each one line long."""
    return ''.join('int %s%d();\n' % (name, index) for index in range(count))


def header(body):
    """One compilable-looking header around a body."""
    return '#pragma once\n\nnamespace an\n{\n%s}\n' % body


# The declaration lines `header` adds around a body: the namespace and its two braces.
WRAPPER = 3


class ArchitectureCase(unittest.TestCase):
    """A tree whose headers are small, whose one expanded body is registered, and which
    instantiates nothing explicitly."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-header-architecture-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        self.write(ENGINE + '/include/AfterNightEngine/Core/Small.h', SMALL_HEADER)
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n')
        for component in architecture.COMPONENTS[1:]:
            stem = component.include_prefix.rstrip('/')
            self.write('%s/include/%s/Bridge.h' % (component.root, stem), SMALL_HEADER)
            self.write('%s/src/Bridge.cpp' % component.root, '#include <%s/Bridge.h>\n' % stem)
        self.register(())

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def register(self, entries):
        """Replace the expanded-body register for one case."""
        previous = architecture.EXPANDED_BODIES
        architecture.EXPANDED_BODIES = tuple(entries)
        self.addCleanup(setattr, architecture, 'EXPANDED_BODIES', previous)

    def register_instantiations(self, entries):
        previous = architecture.EXPLICIT_INSTANTIATIONS
        architecture.EXPLICIT_INSTANTIATIONS = tuple(entries)
        self.addCleanup(setattr, architecture, 'EXPLICIT_INSTANTIATIONS', previous)

    def plant_expanded_body(self):
        """The registered shape: a private header of expanded bodies with one including unit."""
        relative = ENGINE + '/src/algorithms/spatial/Expanded.h'
        self.write(relative, header(declarations(architecture.MAX_DECLARATION_LINES + 20)))
        self.write(ENGINE + '/src/algorithms/spatial/Caller.cpp',
                   '#include "algorithms/spatial/Caller.h"\n'
                   '#include "algorithms/spatial/Expanded.h"\n')
        self.register((architecture.ExpandedBody(
            relative, ENGINE + '/src/algorithms/spatial/Caller.cpp',
            'hand-pruned selection networks whose comparator order is the algorithm'),))
        return relative

    def run_check(self):
        stream = io.StringIO()
        code = architecture.main(['--repo-root', str(self.root)], stream=stream)
        return code, stream.getvalue()

    def assertClean(self):
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def assertRejected(self, fragment):
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn(fragment, output)


class DeclarationWeightTests(ArchitectureCase):
    """No header publishes more declaration than the budget allows."""

    def test_a_tree_of_small_headers_passes(self):
        self.assertClean()

    def test_a_public_header_over_the_budget_fails(self):
        self.write(ENGINE + '/include/AfterNightEngine/Core/Wide.h',
                   header(declarations(architecture.MAX_DECLARATION_LINES + 1 - WRAPPER)))
        self.assertRejected('Core/Wide.h declares %d lines against a budget of %d'
                            % (architecture.MAX_DECLARATION_LINES + 1,
                               architecture.MAX_DECLARATION_LINES))

    def test_a_header_exactly_on_the_budget_passes(self):
        self.write(ENGINE + '/include/AfterNightEngine/Core/Wide.h',
                   header(declarations(architecture.MAX_DECLARATION_LINES - WRAPPER)))
        self.assertClean()

    def test_a_private_header_over_the_budget_fails(self):
        self.write(ENGINE + '/src/core/Wide.h',
                   header(declarations(architecture.MAX_DECLARATION_LINES + 1)))
        self.assertRejected('src/core/Wide.h declares')

    def test_an_adapter_header_over_the_budget_fails(self):
        self.write(ADAPTER + '/src/Wide.h',
                   header(declarations(architecture.MAX_DECLARATION_LINES + 1)))
        self.assertRejected('AfterNightEnginePythonAdapter/src/Wide.h declares')

    def test_an_inline_definition_file_is_a_header_too(self):
        self.write(ENGINE + '/src/kernels/reference/Row.inl',
                   declarations(architecture.MAX_DECLARATION_LINES + 1))
        self.assertRejected('kernels/reference/Row.inl declares')

    def test_documentation_is_not_declaration(self):
        prose = ''.join('/// A sentence about the thing below, number %d.\n' % index
                        for index in range(4000))
        self.write(ENGINE + '/include/AfterNightEngine/Core/Documented.h',
                   header(prose + declarations(10)))
        self.assertClean()

    def test_a_block_comment_is_not_declaration(self):
        prose = '/*\n' + ''.join(' * line %d\n' % index for index in range(4000)) + ' */\n'
        self.write(ENGINE + '/include/AfterNightEngine/Core/Documented.h',
                   header(prose + declarations(10)))
        self.assertClean()

    def test_preprocessor_lines_are_not_declaration(self):
        directives = ''.join('#include <cstdint>\n' for _ in range(600))
        self.write(ENGINE + '/include/AfterNightEngine/Core/Included.h',
                   '#pragma once\n' + directives + header(declarations(10)))
        self.assertClean()

    def test_a_multi_line_macro_is_preprocessor_to_its_last_continued_line(self):
        macro = '#define AN_THING(x) \\\n' + ''.join('    do_%d(x) \\\n' % index
                                                     for index in range(600)) + '    done(x)\n'
        self.write(ENGINE + '/include/AfterNightEngine/Core/Macro.h',
                   '#pragma once\n' + macro + header(declarations(10)))
        self.assertClean()

    def test_a_source_file_is_not_held_to_the_header_budget(self):
        self.write(ENGINE + '/src/core/Wide.cpp',
                   '#include "core/Wide.h"\n' + declarations(architecture.MAX_DECLARATION_LINES + 400))
        self.assertClean()


class ExpandedBodyTests(ArchitectureCase):
    """A header of expanded bodies is registered, private, and included exactly once."""

    def test_the_registered_shape_passes(self):
        self.plant_expanded_body()
        self.assertClean()

    def test_an_unregistered_header_over_the_budget_names_the_register(self):
        self.write(ENGINE + '/src/algorithms/spatial/Expanded.h',
                   header(declarations(architecture.MAX_DECLARATION_LINES + 20)))
        self.assertRejected('no register entry explains it')

    def test_a_registered_header_that_no_longer_exceeds_the_budget_fails(self):
        relative = self.plant_expanded_body()
        self.write(relative, header(declarations(10)))
        self.assertRejected('is registered as an expanded body but declares %d lines' % (10 + WRAPPER))

    def test_a_registered_header_a_second_file_includes_fails(self):
        self.plant_expanded_body()
        self.write(ENGINE + '/src/algorithms/spatial/Other.cpp',
                   '#include "algorithms/spatial/Other.h"\n'
                   '#include "algorithms/spatial/Expanded.h"\n')
        self.assertRejected('is included by src/AfterNightEngine/src/algorithms/spatial/Other.cpp '
                            'as well')

    def test_a_registered_header_another_header_includes_fails(self):
        self.plant_expanded_body()
        self.write(ENGINE + '/src/algorithms/spatial/Other.h',
                   '#pragma once\n#include "algorithms/spatial/Expanded.h"\n')
        self.assertRejected('is included by src/AfterNightEngine/src/algorithms/spatial/Other.h '
                            'as well')

    def test_a_registered_header_its_stated_unit_does_not_include_fails(self):
        self.plant_expanded_body()
        self.write(ENGINE + '/src/algorithms/spatial/Caller.cpp',
                   '#include "algorithms/spatial/Caller.h"\n')
        self.assertRejected('which does not include it')

    def test_a_registered_header_that_is_published_fails(self):
        relative = ENGINE + '/include/AfterNightEngine/Core/Expanded.h'
        self.write(relative, header(declarations(architecture.MAX_DECLARATION_LINES + 20)))
        self.write(ENGINE + '/src/core/Caller.cpp',
                   '#include "core/Caller.h"\n'
                   '#include <AfterNightEngine/Core/Expanded.h>\n')
        self.register((architecture.ExpandedBody(relative, ENGINE + '/src/core/Caller.cpp',
                                                 'a reason'),))
        self.assertRejected('is published; expanded code is private')

    def test_a_registered_header_that_does_not_exist_fails(self):
        self.register((architecture.ExpandedBody(ENGINE + '/src/core/Gone.h',
                                                 ENGINE + '/src/core/Small.cpp', 'a reason'),))
        self.assertRejected('src/core/Gone.h is registered as an expanded body but does not exist')

    def test_a_register_entry_naming_a_unit_that_does_not_exist_fails(self):
        relative = self.plant_expanded_body()
        self.register((architecture.ExpandedBody(relative, ENGINE + '/src/core/Gone.cpp', 'a reason'),))
        self.assertRejected('src/core/Gone.cpp, which does not exist')

    def test_a_register_entry_with_no_reason_fails(self):
        relative = ENGINE + '/src/algorithms/spatial/Expanded.h'
        self.write(relative, header(declarations(architecture.MAX_DECLARATION_LINES + 20)))
        self.write(ENGINE + '/src/algorithms/spatial/Caller.cpp',
                   '#include "algorithms/spatial/Caller.h"\n'
                   '#include "algorithms/spatial/Expanded.h"\n')
        self.register((architecture.ExpandedBody(relative,
                                                 ENGINE + '/src/algorithms/spatial/Caller.cpp', '  '),))
        self.assertRejected('is registered with no reason')


class ExplicitInstantiationTests(ArchitectureCase):
    """An explicit instantiation is a decision about the exported ABI, so it is registered."""

    def test_a_tree_that_instantiates_nothing_explicitly_passes(self):
        self.assertClean()

    def test_an_explicit_instantiation_definition_in_a_source_fails(self):
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n'
                   'template class an::Holder<float>;\n')
        self.assertRejected('src/core/Small.cpp:2 declares an explicit instantiation')

    def test_an_explicit_instantiation_declaration_in_a_header_fails(self):
        self.write(ENGINE + '/include/AfterNightEngine/Core/Small.h',
                   SMALL_HEADER + 'extern template class Holder<float>;\n')
        self.assertRejected('Core/Small.h:14 declares an explicit instantiation')

    def test_an_explicit_instantiation_of_a_function_fails(self):
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n'
                   'template void an::convert<float>(int);\n')
        self.assertRejected('declares an explicit instantiation')

    def test_a_template_definition_is_not_an_instantiation(self):
        self.write(ENGINE + '/include/AfterNightEngine/Core/Small.h',
                   SMALL_HEADER
                   + 'template <typename Sample>\nstruct Holder\n{\n    Sample value;\n};\n')
        self.assertClean()

    def test_a_template_specialization_is_not_an_instantiation(self):
        self.write(ENGINE + '/include/AfterNightEngine/Core/Small.h',
                   SMALL_HEADER
                   + 'template <>\nstruct Holder<float>\n{\n    float value;\n};\n')
        self.assertClean()

    def test_the_words_in_a_comment_are_not_an_instantiation(self):
        self.write(ENGINE + '/include/AfterNightEngine/Core/Small.h',
                   SMALL_HEADER
                   + '// There is no extern template class Holder<float>; here, deliberately.\n')
        self.assertClean()

    def test_the_keyword_written_across_two_lines_is_still_an_instantiation(self):
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n'
                   'template\nclass an::Holder<float>;\n')
        self.assertRejected('declares an explicit instantiation')

    def test_the_dependent_member_disambiguator_is_not_an_instantiation(self):
        """`object.template get<T>()` wrapped after the dot starts a line with the keyword."""
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n'
                   'void call(Body& body)\n{\n    body.\n'
                   '        template operator()<float>();\n}\n')
        self.assertClean()

    def test_an_identifier_that_begins_with_the_keyword_is_not_an_instantiation(self):
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n'
                   'int templateCount();\n')
        self.assertClean()

    def test_the_words_in_a_string_literal_are_not_an_instantiation(self):
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n'
                   'const char* kText = "template class an::Holder<float>;";\n')
        self.assertClean()

    def test_a_registered_instantiation_passes(self):
        self.register_instantiations((architecture.ExplicitInstantiation(
            ENGINE + '/src/core/Small.cpp', 'one exported layout the ABI record names'),))
        self.write(ENGINE + '/src/core/Small.cpp',
                   '#include <AfterNightEngine/Core/Small.h>\n'
                   'template class an::Holder<float>;\n')
        self.assertClean()

    def test_a_registered_file_that_instantiates_nothing_fails(self):
        self.register_instantiations((architecture.ExplicitInstantiation(
            ENGINE + '/src/core/Small.cpp', 'a reason'),))
        self.assertRejected('src/core/Small.cpp is registered as explicitly instantiating a template, '
                            'and does not')


class ComponentTests(ArchitectureCase):
    """The checker reads this engine's side of the workspace and says which part it read."""

    def test_a_missing_component_is_reported_rather_than_skipped(self):
        shutil.rmtree(self.root / ADAPTER)
        self.assertRejected('%s: not found' % ADAPTER)

    def test_the_summary_names_what_was_read(self):
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('header', output)


class RepositoryTests(unittest.TestCase):
    def test_the_repository_passes(self):
        stream = io.StringIO()
        code = architecture.main([], stream=stream)
        self.assertEqual(code, 0, stream.getvalue())

    def test_the_register_names_the_median_selection_networks(self):
        registered = [entry.header for entry in architecture.EXPANDED_BODIES]
        self.assertIn('src/AfterNightEngine/src/algorithms/spatial/MedianNetworks.h', registered)

    def test_the_engine_instantiates_no_template_explicitly(self):
        self.assertEqual(architecture.EXPLICIT_INSTANTIATIONS, ())


if __name__ == '__main__':
    unittest.main(verbosity=2)

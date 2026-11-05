#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for consumer_targets.py.

Every case plants a throwaway repository: a small target registry, source list and package profile file, the two
test runners' suite lists, a few sources, and a generated make tree whose projects agree with the registry. A case
then breaks one thing and asks for the finding it causes. Nothing of the real repository is read except by the one
case that checks the checked-in registry against this checkout's generated tree.
"""

from __future__ import annotations

import copy
import hashlib
import io
import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import unittest
import unittest.mock
from contextlib import redirect_stdout

import consumer_targets as targets

# The planted registry: the platform library, the engine, a library and an application on it, and the suites over them.
REGISTRY = '''# the planted registry

target AfterNightPlatform
    workspace    main
    kind         shared
    class        none
    state        active
    depends      -
    sources      src/AfterNightPlatform
    ship         product

target AfterNightEngine
    workspace    main
    kind         shared
    class        engine
    state        active
    depends      -
    sources      src/AfterNightEngine

target AfterNightControls
    workspace    main
    kind         shared
    class        engine
    state        active
    depends      AfterNightEngine, AfterNightPlatform
    sources      src/AfterNightControls
    ship         product

target AfterNightApp
    workspace    main
    kind         windowed
    output       AfterNight
    class        engine
    state        active
    depends      AfterNightControls, AfterNightEngine, AfterNightPlatform
    sources      src/AfterNightApp
    ship         product

target Test_Platform
    workspace    tests
    kind         console
    class        none
    state        active
    depends      AfterNightPlatform
    sources      src/AfterNightTests
    fixture      none
    runner       product

target Test_EngineCore
    workspace    tests
    kind         console
    class        engine
    state        active
    depends      AfterNightEngine
    sources      src/AfterNightTests
    fixture      engine

target Test_Widgets
    workspace    tests
    kind         console
    class        engine
    state        active
    depends      AfterNightEngine, AfterNightPlatform
    sources      src/AfterNightTests, src/AfterNightControls
    fixture      adapter
    runner       product

target Test_SystemInformationPanel
    workspace    tests
    kind         console
    class        none
    state        active
    depends      -
    sources      @Test_SystemInformationPanel
    fixture      none
    runner       product
'''

SOURCES = '''# the planted source lists

sources Test_SystemInformationPanel
    unit     src/AfterNightTests/Test_SystemInformationPanel/TestMain.cpp
    unit     src/AfterNightTests/Test_SystemInformationPanel/TestParserLinux.cpp only=linux
    unit     src/AfterNightTests/Test_SystemInformationPanel/TestParserWindows.cpp only=windows
    unit     src/AfterNightControls/controls/Parser.cpp
    header   src/AfterNightControls/controls/Parser.h
    include  src/AfterNightControls
'''

PROFILES = '''# the planted package profiles

profile product
    title        product
    form         installer
    active       yes
    packagers    yes
    ship-windows Qt6Core.dll
    ship-linux   libQt6Core.so.6
    keep-out-windows  AfterNight.log
    keep-out-linux    core.12345
    separate-linux    AfterNight
'''

# A second profile, for the cases about what one profile alone may be.
DEPLOYMENT_PROFILE = '''
profile deployment
    title        clean deployment
    form         deployment
'''


# The suites both runners expect.
RUNNER_SUITES = ['Test_Platform', 'Test_SystemInformationPanel', 'Test_Widgets']


def runner_scripts(suites) -> tuple[str, str]:
    """(run_tests.sh, run_tests.bat) expecting `suites`, the shell's list broken before its last name."""
    ordered = sorted(suites)
    shell = ('#!/bin/bash\nexpected_tests=(' + ' '.join(ordered[:-1]) + '\n    ' + ordered[-1] + ')\n'
             'for expected in "${expected_tests[@]}"; do :; done\n')
    batch = '@echo off\nset "TEST_NAMES=' + ' '.join(ordered) + '"\n'
    return shell, batch


RUN_TESTS_SH, RUN_TESTS_BAT = runner_scripts(RUNNER_SUITES)

FILES = {
    'src/AfterNightPlatform/Logging.cpp': '#include "Logging.h"\n',
    'src/AfterNightPlatform/Logging.h': '#pragma once\n',
    'src/AfterNightEngine/include/AfterNightEngine/Core/Status.h': '#pragma once\n',
    'src/AfterNightEngine/src/Status.cpp': '#include <AfterNightEngine/Core/Status.h>\n',
    'src/AfterNightControls/controls/Viewer.cpp': '#include <AfterNightEngine/Core/Status.h>\n',
    'src/AfterNightControls/controls/Parser.h': '#pragma once\nint parse();\n',
    'src/AfterNightControls/controls/Parser.cpp': '#include "controls/Parser.h"\nint parse() { return 0; }\n',
    'src/AfterNightControls/controls/IAppContext.cpp': '// a unit the consumer suites\' factory adds\n',
    'src/AfterNightApp/main.cpp': '#include "controls/Viewer.h"\n',
    'src/AfterNightTests/Test_Platform/TestLogging.cpp': '#include "Logging.h"\n',
    'src/AfterNightTests/Test_EngineCore/TestStatus.cpp': '#include <AfterNightEngine/Core/Status.h>\n',
    'src/AfterNightTests/Test_Widgets/TestViewer.cpp': '#include <AfterNightEngine/Core/Status.h>\n',
    'src/AfterNightTests/Test_SystemInformationPanel/TestMain.cpp': '#include "controls/Parser.h"\n',
    'src/AfterNightTests/Test_SystemInformationPanel/TestParserLinux.cpp': '#include "controls/Parser.h"\n',
    'src/AfterNightTests/Test_SystemInformationPanel/TestParserWindows.cpp': '#include "controls/Parser.h"\n',
}

# What each generated project compiles, includes and links, as the generator would state it.
PROJECTS = {
    'AfterNightPlatform': ('main', 'libAfterNightPlatform.so', ['src/AfterNightPlatform/Logging.cpp'],
                           ['src/AfterNightPlatform'], []),
    'AfterNightEngine': ('main', 'libAfterNightEngine.so', ['src/AfterNightEngine/src/Status.cpp'],
                             ['src/AfterNightEngine/include'], []),
    'AfterNightControls': ('main', 'libAfterNightControls.so', ['src/AfterNightControls/controls/Viewer.cpp'],
                           ['src/AfterNightControls', 'src/AfterNightEngine/include', 'src/AfterNightPlatform'],
                           ['AfterNightEngine', 'AfterNightPlatform']),
    'AfterNightApp': ('main', 'AfterNight', ['src/AfterNightApp/main.cpp'],
                      ['src/AfterNightApp', 'src/AfterNightControls', 'src/AfterNightEngine/include'],
                      ['AfterNightControls', 'AfterNightEngine', 'AfterNightPlatform']),
    'Test_Platform': ('tests', 'Test_Platform', ['src/AfterNightTests/Test_Platform/TestLogging.cpp'],
                      ['src/AfterNightPlatform'], ['AfterNightPlatform']),
    'Test_EngineCore': ('tests', 'Test_EngineCore', ['src/AfterNightTests/Test_EngineCore/TestStatus.cpp'],
                      ['src/AfterNightEngine/include'], ['AfterNightEngine']),
    'Test_Widgets': ('tests', 'Test_Widgets', ['src/AfterNightTests/Test_Widgets/TestViewer.cpp',
                                               'src/AfterNightControls/controls/Viewer.cpp'],
                     ['src/AfterNightTests', 'src/AfterNightControls', 'src/AfterNightEngine/include'],
                     ['AfterNightEngine', 'AfterNightPlatform']),
    'Test_SystemInformationPanel': ('tests', 'Test_SystemInformationPanel',
                                    ['src/AfterNightTests/Test_SystemInformationPanel/TestMain.cpp',
                                     'src/AfterNightTests/Test_SystemInformationPanel/TestParserLinux.cpp',
                                     'src/AfterNightControls/controls/Parser.cpp'],
                                    ['src/AfterNightControls'], []),
}

# The engine's published header every planted engine user includes.
ENGINE_HEADER = 'src/AfterNightEngine/include/AfterNightEngine/Core/Status.h'


def write(root: pathlib.Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))


def make_project(output: str, sources: list[str], includes: list[str], links: list[str], depth: int,
                 flags: list[str] | None = None) -> str:
    up = '../' * depth
    debug = (output[:-3] + '_d.so') if output.endswith('.so') else output + '_d'
    lines = ['INCLUDES += ' + ' '.join('-I' + up + include for include in includes),
             'ALL_CXXFLAGS += $(CXXFLAGS) ' + ' '.join(flags or []),
             'LIBS +=', 'LDDEPS +=',
             'ifeq ($(config),release_x64)', 'TARGETDIR = ' + up + 'bin/clang/release',
             'TARGET = $(TARGETDIR)/' + output,
             'ALL_LDFLAGS += $(LDFLAGS) ' + ' '.join('-l' + link for link in links),
             'else ifeq ($(config),debug_x64)', 'TARGETDIR = ' + up + 'bin/clang/debug',
             'TARGET = $(TARGETDIR)/' + debug,
             'ALL_LDFLAGS += $(LDFLAGS) ' + ' '.join('-l' + link + '_d' for link in links), 'endif']
    for source in sources:
        lines.append(f'$(OBJDIR)/{pathlib.PurePosixPath(source).stem}.o: {up}{source}')
    return '\n'.join(lines) + '\n'


class PlantedCase(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        for relative, text in FILES.items():
            write(self.root, relative, text)
        self.registry = REGISTRY
        self.sources = SOURCES
        self.profiles = PROFILES
        self.run_tests_sh = RUN_TESTS_SH
        self.run_tests_bat = RUN_TESTS_BAT
        self.projects = {name: list(copy.deepcopy(value)) for name, value in PROJECTS.items()}
        write(self.root, 'build/premake/premake5.lua', '-- definitions\n')

    def plant(self) -> None:
        write(self.root, targets.REGISTRY, self.registry)
        write(self.root, targets.SOURCE_LISTS, self.sources)
        write(self.root, targets.PROFILES, self.profiles)
        write(self.root, targets.RUNNERS[0], self.run_tests_sh)
        write(self.root, targets.RUNNERS[1], self.run_tests_bat)
        tree = self.root / 'build/make-linux'
        if tree.exists():
            shutil.rmtree(tree)
        for name, (workspace, output, sources, includes, links, *flags) in self.projects.items():
            relative = f'build/make-linux/{name}.make' if workspace == 'main' else f'build/make-linux/tests/{name}.make'
            write(self.root, relative, make_project(output, sources, includes, links,
                                                    2 if workspace == 'main' else 3, flags[0] if flags else None))
        stamp = tree / '.premake-generated'
        stamp.write_text('')
        later = time.time() + 5
        os.utime(stamp, (later, later))

    def findings(self) -> list[str]:
        self.plant()
        return targets.check(self.root)

    def assertFinding(self, fragment: str) -> None:
        findings = self.findings()
        self.assertTrue(any(fragment in finding for finding in findings),
                        f'no finding contains {fragment!r}:\n' + '\n'.join(findings))

    def edit(self, attribute: str, old: str, new: str) -> None:
        text = getattr(self, attribute)
        self.assertIn(old, text)
        setattr(self, attribute, text.replace(old, new, 1))

    def block(self, name: str) -> str:
        start = self.registry.index(f'target {name}\n')
        end = self.registry.find('\ntarget ', start + 1)
        return self.registry[start:] if end < 0 else self.registry[start:end + 1]

    def edit_target(self, name: str, old: str, new: str) -> None:
        block = self.block(name)
        self.assertIn(old, block)
        self.registry = self.registry.replace(block, block.replace(old, new, 1))



class RegistryTest(PlantedCase):
    def test_the_planted_repository_passes(self) -> None:
        self.assertEqual(self.findings(), [])

    def test_the_command_line_check_passes_and_counts_what_it_read(self) -> None:
        self.plant()
        stream = io.StringIO()
        with redirect_stdout(stream):
            code = targets.main(['check', '--repo-root', str(self.root)])
        self.assertEqual(code, 0, stream.getvalue())
        self.assertIn('8 targets', stream.getvalue())
        self.assertIn('make-linux', stream.getvalue())

    def test_an_unknown_field_is_refused(self) -> None:
        self.edit_target('Test_EngineCore', '    fixture      engine\n', '    fixture      engine\n    colour  blue\n')
        self.assertFinding('colour')

    def test_a_missing_field_is_refused(self) -> None:
        self.edit_target('Test_EngineCore', '    class        engine\n', '')
        self.assertFinding('Test_EngineCore states no class')

    def test_an_unknown_class_is_refused(self) -> None:
        self.edit_target('Test_EngineCore', 'class        engine', 'class        both')
        self.assertFinding("Test_EngineCore has class 'both', which is not one of none, engine")

    def test_an_unknown_fixture_is_refused(self) -> None:
        self.edit_target('Test_Widgets', 'fixture      adapter', 'fixture      bridge')
        self.assertFinding("Test_Widgets has fixture 'bridge', which is not one of none, engine, neutral, adapter")

    def test_a_target_declared_twice_is_refused(self) -> None:
        self.registry += '\n' + self.block('Test_EngineCore')
        self.assertFinding('Test_EngineCore is declared twice')

    def test_a_dependency_on_an_undeclared_target_is_refused(self) -> None:
        self.edit_target('Test_EngineCore', 'depends      AfterNightEngine',
                         'depends      AfterNightEngine, AfterNightMissing')
        self.assertFinding('AfterNightMissing')

    def test_a_neutral_target_depending_on_an_engine_is_refused(self) -> None:
        self.edit_target('Test_Platform', 'depends      AfterNightPlatform',
                         'depends      AfterNightPlatform, AfterNightEngine')
        self.projects['Test_Platform'][4].append('AfterNightEngine')
        self.assertFinding('Test_Platform is class none and reaches AfterNightEngine')

    def test_an_engine_target_that_reaches_no_engine_is_refused(self) -> None:
        self.edit_target('Test_Platform', 'class        none', 'class        engine')
        self.assertFinding('Test_Platform is class engine and reaches no engine')

    def test_a_fixture_of_the_wrong_class_is_refused(self) -> None:
        self.edit_target('Test_EngineCore', 'fixture      engine', 'fixture      neutral')
        self.assertFinding('Test_EngineCore has the neutral fixture, which belongs to class none, and is class engine')

    # The fixture adapter builds its inputs through the engine, so a target that reaches none cannot use it.
    def test_the_fixture_adapter_belongs_to_an_engine_target(self) -> None:
        self.edit_target('Test_Platform', 'fixture      none', 'fixture      adapter')
        self.assertFinding('Test_Platform has the adapter fixture, which belongs to class engine, and is class none')

    def test_a_product_target_has_no_fixture(self) -> None:
        self.edit_target('AfterNightEngine', '    sources      src/AfterNightEngine\n',
                         '    sources      src/AfterNightEngine\n    fixture      engine\n')
        self.assertFinding('AfterNightEngine is not a test and names a fixture')

    def test_shipping_in_an_unknown_profile_is_refused(self) -> None:
        self.edit_target('AfterNightPlatform', 'ship         product', 'ship         elsewhere')
        self.assertFinding('AfterNightPlatform ships in elsewhere, which')

    def test_an_unknown_profile_field_is_refused(self) -> None:
        self.edit('profiles', '    form         installer\n', '    form         installer\n    colour       blue\n')
        self.assertFinding('has a field "colour" a profile does not define')

    def test_a_profile_that_ships_nothing_carries_no_target(self) -> None:
        self.edit('profiles', 'form         installer', 'form         none')
        self.assertFinding('the package profile product ships nothing and AfterNightPlatform names it')

    def test_exactly_one_profile_is_active(self) -> None:
        self.profiles += DEPLOYMENT_PROFILE
        self.assertEqual(self.findings(), [])
        self.edit('profiles', 'form         deployment', 'form         deployment\n    active       yes')
        self.assertFinding('2 package profiles are active')

    def test_exactly_one_profile_is_what_the_packagers_produce(self) -> None:
        self.edit('profiles', '    packagers    yes\n', '')
        self.assertFinding('0 package profiles are marked as what the packagers produce')

    def test_an_isolated_target_names_a_source_list_that_exists(self) -> None:
        self.edit_target('Test_SystemInformationPanel', '@Test_SystemInformationPanel', '@Test_Nothing')
        self.assertFinding('Test_Nothing')

    def test_a_source_list_no_target_uses_is_refused(self) -> None:
        self.sources += '\nsources Test_Orphan\n    unit     src/AfterNightTests/Test_Platform/TestLogging.cpp\n'
        self.assertFinding('Test_Orphan is a source list no target compiles')


class RunnerTest(PlantedCase):
    def test_a_runner_missing_an_active_suite_is_refused(self) -> None:
        self.edit('run_tests_sh', 'Test_Platform ', '')
        self.assertFinding('run_tests.sh does not expect Test_Platform')



class SourceListTest(PlantedCase):
    def test_a_unit_that_does_not_exist_is_refused(self) -> None:
        self.edit('sources', 'TestMain.cpp', 'TestMissing.cpp')
        self.assertFinding('TestMissing.cpp does not exist')

    def test_a_wildcard_is_refused(self) -> None:
        self.edit('sources', 'controls/Parser.cpp', 'controls/*.cpp')
        self.assertFinding('names files by pattern')

    def test_a_stub_needs_a_reason(self) -> None:
        self.sources += '    stub     src/AfterNightTests/Test_SystemInformationPanel/TestParserLinux.cpp\n'
        self.assertFinding('states no reason')

    def test_a_stub_lives_in_the_test_tree(self) -> None:
        self.sources += '    stub     src/AfterNightControls/controls/Parser.cpp stands in for the parser\n'
        self.assertFinding('a stub belongs under src/AfterNightTests/')

    def test_a_header_the_closure_reaches_has_to_be_listed(self) -> None:
        self.edit('sources', '    header   src/AfterNightControls/controls/Parser.h\n', '')
        self.assertFinding('reaches src/AfterNightControls/controls/Parser.h, which the list does not name')

    def test_an_include_the_factory_defines_away_is_not_read(self) -> None:
        write(self.root, 'src/AfterNightControls/controls/Parser.cpp',
              '#ifndef AFTERNIGHT_TESTS\n#include "stdafx.hpp"\n#endif\n#include "controls/Parser.h"\n')
        write(self.root, 'src/AfterNightControls/stdafx.hpp',
              '#pragma once\n#include <AfterNightEngine/Core/Status.h>\n')
        self.assertEqual(self.findings(), [])

    def test_an_include_under_a_macro_the_list_defines_is_not_read(self) -> None:
        write(self.root, 'src/AfterNightControls/controls/Parser.cpp',
              '#if !defined(PARSER_STANDALONE)\n#include "stdafx.hpp"\n#endif\n#include "controls/Parser.h"\n')
        write(self.root, 'src/AfterNightControls/stdafx.hpp', '#pragma once\n')
        self.assertFinding('reaches src/AfterNightControls/stdafx.hpp, which the list does not name')
        self.sources += '    define   PARSER_STANDALONE\n'
        self.assertEqual(self.findings(), [])

    def test_an_include_under_an_undecided_condition_is_read(self) -> None:
        write(self.root, 'src/AfterNightControls/controls/Parser.cpp',
              '#ifdef _WIN32\n#include "stdafx.hpp"\n#else\n#include "controls/Parser.h"\n#endif\n')
        write(self.root, 'src/AfterNightControls/stdafx.hpp', '#pragma once\n')
        self.assertFinding('reaches src/AfterNightControls/stdafx.hpp, which the list does not name')

    def test_a_neutral_list_reaching_an_engine_header_is_refused(self) -> None:
        write(self.root, 'src/AfterNightControls/controls/Parser.h',
              '#pragma once\n#include "../../AfterNightEngine/include/AfterNightEngine/Core/Status.h"\n')
        self.assertFinding(f'Test_SystemInformationPanel is class none and its sources reach {ENGINE_HEADER}')

    def test_a_list_including_an_engine_header_by_name_is_refused(self) -> None:
        write(self.root, 'src/AfterNightControls/controls/Parser.cpp',
              '#include "controls/Parser.h"\n#include <AfterNightEngine/Core/Status.h>\n')
        self.assertFinding('Test_SystemInformationPanel is class none and src/AfterNightControls/controls/Parser.cpp '
                           'includes AfterNightEngine/Core/Status.h, which is '
                           f'{ENGINE_HEADER} of an engine it excludes')

    def test_an_include_that_names_no_engine_header_is_not_refused(self) -> None:
        write(self.root, 'src/AfterNightControls/controls/Parser.h', '#pragma once\n#include <vector>\n#include <QString>\n')
        self.assertEqual(self.findings(), [])

    def link_platform(self) -> None:
        """The planted panel suite's list links the platform library and names the Qt modules of a harness."""
        self.sources += '    link     AfterNightPlatform\n    qt       core gui widgets\n'
        self.edit_target('Test_SystemInformationPanel', 'depends      -', 'depends      AfterNightPlatform')
        self.projects['Test_SystemInformationPanel'][4].append('AfterNightPlatform')

    def test_a_listed_library_the_record_depends_on_is_accepted(self) -> None:
        self.link_platform()
        self.assertEqual(self.findings(), [])
        listing = targets.read_registry(self.root).sources['Test_SystemInformationPanel']
        self.assertEqual(listing.links, ('AfterNightPlatform',))
        self.assertEqual(listing.qt, ('core', 'gui', 'widgets'))

    def test_a_listed_library_the_record_does_not_depend_on_is_refused(self) -> None:
        self.link_platform()
        self.edit_target('Test_SystemInformationPanel', 'depends      AfterNightPlatform', 'depends      -')
        self.assertFinding('Test_SystemInformationPanel links AfterNightPlatform through its source list, and its '
                           'record does not depend on it')

    def test_a_listed_library_the_registry_does_not_declare_is_refused(self) -> None:
        self.sources += '    link     AfterNightMissing\n'
        self.assertFinding('Test_SystemInformationPanel: the source list links AfterNightMissing, which the registry '
                           'does not declare')

    def test_a_listed_link_that_is_not_a_library_is_refused(self) -> None:
        self.sources += '    link     Test_Platform\n'
        self.assertFinding('Test_SystemInformationPanel: the source list links Test_Platform, which is not a library')

    def test_a_qt_module_is_named_by_one_lower_case_word(self) -> None:
        self.sources += '    qt       Core,gui\n'
        self.assertFinding("names the Qt module 'Core,gui'")

    # A third-party library that is no registry target is named by the one name the isolated factory knows its
    # include directory, macros and link line by; the generated project's link line then names it, and nothing of the
    # registry's targets is read into it.
    def test_an_external_library_the_factory_knows_is_accepted(self) -> None:
        self.sources += '    external kddockwidgets\n'
        self.assertEqual(self.findings(), [])
        listing = targets.read_registry(self.root).sources['Test_SystemInformationPanel']
        self.assertEqual(listing.externals, ('kddockwidgets',))

    # The integration report's Markdown renderer links cmark-gfm, which the factory knows by name too.
    def test_the_markdown_renderer_is_an_external_the_factory_knows(self) -> None:
        self.sources += '    external cmark\n'
        self.assertEqual(self.findings(), [])
        listing = targets.read_registry(self.root).sources['Test_SystemInformationPanel']
        self.assertEqual(listing.externals, ('cmark',))

    # The extension manager's cases sign and compress the repository assets they install, through libsodium and zstd.
    def test_the_signing_and_compression_libraries_are_externals_the_factory_knows(self) -> None:
        self.sources += '    external sodium\n    external zstd\n'
        self.assertEqual(self.findings(), [])
        listing = targets.read_registry(self.root).sources['Test_SystemInformationPanel']
        self.assertEqual(listing.externals, ('sodium', 'zstd'))

    # The full host's native suite starts the bundled interpreter as the host does, through the factory's python name.
    def test_the_interpreter_is_an_external_the_factory_knows(self) -> None:
        self.sources += '    external python\n'
        self.assertEqual(self.findings(), [])
        listing = targets.read_registry(self.root).sources['Test_SystemInformationPanel']
        self.assertEqual(listing.externals, ('python',))

    def test_an_external_library_the_factory_does_not_know_is_refused(self) -> None:
        self.sources += '    external qwt\n'
        self.assertFinding("links the external library 'qwt', which the isolated factory does not know")


class GeneratedTreeTest(PlantedCase):
    def test_a_generated_project_the_registry_does_not_declare_is_refused(self) -> None:
        self.projects['Test_Stray'] = ['tests', 'Test_Stray', [], [], []]
        self.assertFinding('Test_Stray is generated and consumer_targets.txt does not declare it')

    def test_an_active_target_that_is_not_generated_is_refused(self) -> None:
        del self.projects['Test_EngineCore']
        self.assertFinding('Test_EngineCore is active and not generated')

    def test_a_link_the_registry_does_not_state_is_refused(self) -> None:
        self.projects['Test_Platform'][4].append('AfterNightControls')
        self.assertFinding('Test_Platform links AfterNightControls, which its depends does not name')

    def test_a_dependency_the_project_does_not_link_is_refused(self) -> None:
        self.projects['Test_Platform'][4].remove('AfterNightPlatform')
        self.assertFinding('Test_Platform names AfterNightPlatform in depends and does not link it')

    def test_a_sanitizer_generations_library_spelling_names_its_target(self) -> None:
        """A sanitizer generation names its static libraries apart, so that a tree switched back cannot link one."""
        links = self.projects['Test_Platform'][4]
        self.projects['Test_Platform'][4] = [link + '-sanitize-address' if link == 'AfterNightPlatform' else link
                                             for link in links]
        self.assertEqual(self.findings(), [])

    def test_a_sanitizer_generations_output_is_its_registry_output(self) -> None:
        self.assertEqual(targets._output_stem('../../lib/clang/libEngineBenchCommon_d-sanitize-thread.a'),
                         'EngineBenchCommon_d')
        self.assertEqual(targets._output_stem('EngineBenchCommon-sanitize-address.lib'), 'EngineBenchCommon')
        self.assertEqual(targets._output_stem('../../lib/clang/libEngineBenchCommon-sanitize-fuzzer.a'),
                         'EngineBenchCommon')

    def test_a_dependency_reached_through_what_a_project_links_is_linked(self) -> None:
        """On Windows a library carries the libraries it uses; its consumers reach them through it."""
        self.projects['AfterNightApp'][4].remove('AfterNightPlatform')
        self.assertEqual(self.findings(), [])

    def test_a_neutral_target_including_an_engine_directory_is_refused(self) -> None:
        self.projects['Test_Platform'][3].append('src/AfterNightEngine/include')
        self.assertFinding('Test_Platform is class none and includes src/AfterNightEngine/include')

    def test_a_relative_include_of_an_excluded_engine_is_refused(self) -> None:
        write(self.root, 'src/AfterNightTests/Test_Platform/TestLogging.cpp',
              '#include "../../AfterNightEngine/include/AfterNightEngine/Core/Status.h"\n')
        self.assertFinding('Test_Platform is class none and src/AfterNightTests/Test_Platform/TestLogging.cpp '
                           f'reaches {ENGINE_HEADER}')

    def test_an_output_name_that_disagrees_is_refused(self) -> None:
        self.projects['AfterNightApp'][1] = 'AfterNightApp'
        self.assertFinding('AfterNightApp produces AfterNightApp, and the registry says AfterNight')

    def test_a_kind_that_disagrees_is_refused(self) -> None:
        self.projects['AfterNightPlatform'][1] = 'AfterNightPlatform'
        self.assertFinding('AfterNightPlatform is declared shared and generated as an executable')

    def test_an_isolated_target_compiling_an_unlisted_source_is_refused(self) -> None:
        self.projects['Test_SystemInformationPanel'][2].append('src/AfterNightControls/controls/IAppContext.cpp')
        self.assertFinding('Test_SystemInformationPanel compiles src/AfterNightControls/controls/IAppContext.cpp, '
                           'which its source list does not name')

    def test_an_isolated_target_missing_a_listed_unit_is_refused(self) -> None:
        self.projects['Test_SystemInformationPanel'][2].remove('src/AfterNightControls/controls/Parser.cpp')
        self.assertFinding('Test_SystemInformationPanel does not compile src/AfterNightControls/controls/Parser.cpp')

    # libFuzzer supplies `main`, so the generator leaves out the standalone driver a fuzz target's list names.
    def test_a_libfuzzer_generation_leaves_the_standalone_driver_out(self) -> None:
        write(self.root, targets.FUZZ_DRIVER, 'int main() { return 0; }\n')
        self.sources += f'    unit     {targets.FUZZ_DRIVER}\n'
        self.projects['Test_SystemInformationPanel'].append(['-fsanitize=fuzzer,address,undefined'])
        self.assertEqual(self.findings(), [])

    def test_an_ordinary_generation_compiles_the_standalone_driver_its_list_names(self) -> None:
        write(self.root, targets.FUZZ_DRIVER, 'int main() { return 0; }\n')
        self.sources += f'    unit     {targets.FUZZ_DRIVER}\n'
        self.assertFinding(f'Test_SystemInformationPanel does not compile {targets.FUZZ_DRIVER}')

    def test_an_isolated_target_with_an_undeclared_include_directory_is_refused(self) -> None:
        self.projects['Test_SystemInformationPanel'][3].append('src/AfterNightApp')
        self.assertFinding('Test_SystemInformationPanel includes src/AfterNightApp, which its source list does not name')

    def test_a_compiled_source_outside_the_declared_roots_is_refused(self) -> None:
        self.projects['Test_EngineCore'][2].append('src/AfterNightControls/controls/Viewer.cpp')
        self.assertFinding('Test_EngineCore compiles src/AfterNightControls/controls/Viewer.cpp, outside its sources')


class OrderAndManifestTest(PlantedCase):
    def test_the_build_order_puts_every_dependency_first(self) -> None:
        self.plant()
        registry = targets.read_registry(self.root)
        order = targets.build_order(registry, 'main')
        self.assertLess(order.index('AfterNightPlatform'), order.index('AfterNightControls'))
        self.assertLess(order.index('AfterNightEngine'), order.index('AfterNightControls'))
        self.assertLess(order.index('AfterNightControls'), order.index('AfterNightApp'))
        self.assertIn('Test_Widgets', targets.build_order(registry, 'tests'))
        self.assertNotIn('Test_Widgets', order)

    def test_the_manifest_digest_is_the_documented_git_digest_over_the_active_closure(self) -> None:
        self.plant()
        subprocess.run(['git', 'init', '-q'], cwd=self.root, check=True)
        subprocess.run(['git', 'add', '-A'], cwd=self.root, check=True)
        subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-m', 'planted'],
                       cwd=self.root, check=True)
        registry = targets.read_registry(self.root)
        digest, paths = targets.manifest_digest(registry, self.root)
        listing = subprocess.run(['git', 'ls-tree', '-r', 'HEAD', '--'] + paths, cwd=self.root, check=True,
                                 capture_output=True, text=True).stdout
        lines = sorted((line.split()[2] + ' ' + line.split('\t', 1)[1] for line in listing.splitlines()),
                       key=lambda line: line.split(' ', 1)[1])
        self.assertEqual(digest, hashlib.sha256(''.join(line + '\n' for line in lines).encode()).hexdigest())
        self.assertIn('src/AfterNightControls', paths)
        self.assertIn('build/premake', paths)

    def test_a_tracked_file_is_covered_by_a_root_it_is_or_lies_under(self) -> None:
        roots = ['src/AfterNightControls', 'build/premake/afternight-common.lua']
        self.assertTrue(targets.covered_by('src/AfterNightControls/app/A.h', roots))
        self.assertTrue(targets.covered_by('build/premake/afternight-common.lua', roots))
        self.assertFalse(targets.covered_by('src/AfterNightControlsExtra/A.h', roots))
        self.assertFalse(targets.covered_by('build/premake/afternight-common.lua.bak', roots))

    def test_the_manifest_digest_does_not_put_its_paths_on_the_command_line(self) -> None:
        # A closure of explicit lists names hundreds of files; Windows refuses a command line that long.
        self.plant()
        subprocess.run(['git', 'init', '-q'], cwd=self.root, check=True)
        subprocess.run(['git', 'add', '-A'], cwd=self.root, check=True)
        subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-m', 'planted'],
                       cwd=self.root, check=True)
        calls = []
        real = subprocess.run

        def recording(arguments, *rest, **options):
            calls.append(arguments)
            return real(arguments, *rest, **options)
        with unittest.mock.patch.object(targets.subprocess, 'run', recording):
            targets.manifest_digest(targets.read_registry(self.root), self.root)
        self.assertEqual(len(calls), 1)
        self.assertNotIn('--', calls[0])

    def test_the_binary_names_follow_the_platform_and_the_configuration(self) -> None:
        self.plant()
        registry = targets.read_registry(self.root)
        self.assertEqual(targets.binary_names(registry.targets['AfterNightEngine'], 'windows'),
                         ('AfterNightEngine.dll', 'AfterNightEngine_d.dll'))
        self.assertEqual(targets.binary_names(registry.targets['AfterNightApp'], 'linux'), ('AfterNight', 'AfterNight_d'))
        self.assertEqual(targets.binary_names(registry.targets['AfterNightPlatform'], 'linux'),
                         ('libAfterNightPlatform.so', 'libAfterNightPlatform_d.so'))


class RepositoryTest(unittest.TestCase):
    # The registry is held to the generated tree, and a fresh worktree has none; premake_targets_test.py generates one
    # from the checked-in definitions and holds the registry to it there.
    @unittest.skipIf(targets.current_tree(targets.REPO_ROOT) is None,
                     'no generated project tree in this checkout is current with its definitions')
    def test_the_checked_in_registry_holds(self) -> None:
        stream = io.StringIO()
        with redirect_stdout(stream):
            code = targets.main(['check'])
        self.assertEqual(code, 0, stream.getvalue())


if __name__ == '__main__':
    unittest.main()

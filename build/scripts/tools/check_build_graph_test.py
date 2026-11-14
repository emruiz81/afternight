# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_build_graph.

Run with:
    python build/scripts/tools/check_build_graph_test.py

Every case builds a throwaway generated-project tree, so the suite needs nothing from the real
repository, never generates projects, and writes nothing outside its own temporary directory.
"""

from __future__ import annotations

import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_build_graph as graph  # noqa: E402


VCXPROJ = """\
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <OutDir>{outdir}</OutDir>
    <IntDir>{intdir}</IntDir>
    <TargetName>{target}</TargetName>
    <TargetExt>{targetext}</TargetExt>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <AdditionalIncludeDirectories>{includes};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <AdditionalDependencies>{links};%(AdditionalDependencies)</AdditionalDependencies>
      <ImportLibrary>{importlib}</ImportLibrary>
      <ProgramDatabaseFile>{pdb}</ProgramDatabaseFile>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
{sources}
  </ItemGroup>
</Project>
"""

ENGINE_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightEngine', 'include')
ENGINE_SOURCE = os.path.join('..', '..', 'src', 'AfterNightEngine', 'src', 'core', 'LibraryInfo.cpp')

# The engine the product's targets link.
PRODUCT_ENGINE = graph.ENGINE_PROJECT


def write_vcxproj(directory, name, target=None, includes='', links='', sources=(), outdir='',
                  intdir='', targetext='.dll', importlib='', pdb=''):
    body = '\n'.join('    <ClCompile Include="%s" />' % source for source in sources)
    text = VCXPROJ.format(
        target=target or name,
        includes=includes,
        links=links,
        sources=body,
        outdir=outdir,
        intdir=intdir,
        targetext=targetext,
        importlib=importlib,
        pdb=pdb)
    with open(os.path.join(directory, name + '.vcxproj'), 'w', encoding='utf-8') as handle:
        handle.write(text)


def write_slnx(directory, name, projects):
    """`projects` maps a project file path to the list of project paths it builds after."""
    lines = ['<Solution Version="1.4">']
    for path, dependencies in projects.items():
        lines.append('  <Project Path="%s" Id="{00000000-0000-0000-0000-000000000000}">' % path)
        for dependency in dependencies:
            lines.append('    <BuildDependency Project="%s" />' % dependency)
        lines.append('  </Project>')
    lines.append('</Solution>')
    with open(os.path.join(directory, name), 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(lines))


class VisualStudioTreeCase(unittest.TestCase):
    """Builds a minimal but structurally faithful generated MSVC tree under a temporary root."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='an-graph-test-')
        self.main = os.path.join(self.root, 'build', 'vs2026-msvc')
        self.tests = os.path.join(self.main, 'tests')
        os.makedirs(self.tests)
        self.addCleanup(shutil.rmtree, self.root, True)

    def build_tree(self, extra_main=(), extra_tests=(), engine_links='', engine_includes=None,
                   engine_sources=None, consumer_links=PRODUCT_ENGINE + '.lib',
                   main_dependencies=None, drop_main=(), adapter_links=None, test_references=None,
                   main_overrides=None, test_overrides=None):
        main_overrides = main_overrides or {}
        test_overrides = test_overrides or {}
        main_projects = {}
        for name in graph.EXPECTED_MAIN_PROJECTS:
            if name in drop_main:
                continue
            # The platform library links nothing of this workspace, as an engine does not, and the style plugin
            # links the Controls library whose theme it draws.
            links = '' if name in graph.ENGINE_PROJECTS + (graph.PLATFORM_PROJECT,) else consumer_links
            built_after = PRODUCT_ENGINE
            if name == graph.STYLE_PLUGIN_PROJECT:
                links, built_after = graph.STYLE_PLUGIN_THROUGH + '.lib', graph.STYLE_PLUGIN_THROUGH
            kwargs = dict({'links': links}, **main_overrides.get(name, {}))
            links = kwargs['links']
            write_vcxproj(self.main, name, **kwargs)
            # A project that links another one of this workspace also builds after it, which is
            # what the generator emits and what the build-order check reads.
            main_projects[name + '.vcxproj'] = [built_after + '.vcxproj'] if links else []
        if graph.ENGINE_PROJECT not in drop_main:
            write_vcxproj(self.main, graph.ENGINE_PROJECT, links=engine_links,
                          includes=ENGINE_INCLUDE if engine_includes is None else engine_includes,
                          sources=(ENGINE_SOURCE,) if engine_sources is None else engine_sources)
            main_projects[graph.ENGINE_PROJECT + '.vcxproj'] = []
        # The optional adapters, which the generator emits in every tree and which link the
        # engine's published surface. Overridable per case, because what these rules are about is
        # a link line that is wrong in one direction or the other.
        for adapter in sorted(graph.OPTIONAL_ADAPTER_SUITES):
            if adapter in drop_main:
                continue
            links = adapter_links.get(adapter, 'AfterNightEngine.lib') if adapter_links else \
                'AfterNightEngine.lib'
            write_vcxproj(self.main, adapter, links=links, includes=ENGINE_INCLUDE)
            main_projects[adapter + '.vcxproj'] = [graph.ENGINE_PROJECT + '.vcxproj']
        for name, kwargs in extra_main:
            write_vcxproj(self.main, name, **kwargs)
            main_projects[name + '.vcxproj'] = []
        if main_dependencies:
            for project, dependencies in main_dependencies.items():
                main_projects[project] = dependencies
        write_slnx(self.main, 'AfterNight.slnx', main_projects)

        engine_project = os.path.join('..', PRODUCT_ENGINE + '.vcxproj')
        platform_project = os.path.join('..', 'AfterNightPlatform.vcxproj')
        test_projects = {engine_project: [], platform_project: []}
        # The test solution also lists each optional adapter from the main workspace, so that its suite can
        # build after it, and orders the adapter behind the engine it links - which is what the generator
        # emits once the external project states its own dependency.
        referenced = {}
        for adapter in sorted(graph.OPTIONAL_ADAPTER_SUITES):
            if adapter not in drop_main:
                referenced[adapter] = os.path.join('..', adapter + '.vcxproj')
                test_projects[referenced[adapter]] = [engine_project]
        referenced[PRODUCT_ENGINE] = engine_project
        referenced[graph.PLATFORM_PROJECT] = platform_project
        for name in graph.EXPECTED_TEST_PROJECTS:
            if graph.is_engine_target(name):
                links = 'AfterNightEngine.lib;gtest.lib'
                dependencies = [engine_project]
            elif name == graph.PLATFORM_SUITE:
                links = 'AfterNightPlatform.lib;gtest.lib'
                dependencies = [platform_project]
            else:
                links = PRODUCT_ENGINE + '.lib;gtest.lib'
                dependencies = [engine_project]
            kwargs = dict({'links': links}, **test_overrides.get(name, {}))
            linked = [graph.link_base_name(entry) for entry in kwargs['links'].split(';')]
            dependencies = sorted(set(dependencies) | {referenced[entry] for entry in linked if entry in referenced})
            write_vcxproj(self.tests, name, **kwargs)
            test_projects[name + '.vcxproj'] = dependencies
        for name, kwargs in extra_tests:
            write_vcxproj(self.tests, name, **kwargs)
            # Whatever it links from this solution, it also builds after - a project of this workspace
            # or one the solution lists from the main workspace alike. The generator emits both
            # together, so a harness that emitted only the link would not be faithful.
            linked = [graph.link_base_name(entry) for entry in kwargs.get('links', '').split(';')]
            test_projects[name + '.vcxproj'] = (
                [entry + '.vcxproj' for entry in linked if entry in graph.EXPECTED_TEST_PROJECTS]
                + [referenced[entry] for entry in linked if entry in referenced])
        test_projects.update(test_references or {})
        write_slnx(self.tests, 'AfterNightTest.slnx', test_projects)

    def run_check(self):
        stream = io.StringIO()
        code = graph.main(['--repo-root', self.root], stream=stream)
        return code, stream.getvalue()


class CleanTreeTests(VisualStudioTreeCase):
    def test_clean_tree_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_reports_which_trees_were_inspected(self):
        self.build_tree()
        _, output = self.run_check()
        self.assertIn('vs2026-msvc', output)



class MissingProjectTests(VisualStudioTreeCase):
    def test_missing_pre_existing_project_fails(self):
        self.build_tree(drop_main=('AfterNightProcesses',))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightProcesses', output)

    def test_missing_pre_existing_test_project_fails(self):
        self.build_tree()
        os.remove(os.path.join(self.tests, 'Test_Persistence.vcxproj'))
        write_slnx(self.tests, 'AfterNightTest.slnx',
                   {name + '.vcxproj': [] for name in graph.EXPECTED_TEST_PROJECTS
                    if name != 'Test_Persistence'})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Test_Persistence', output)

    def test_missing_engine_fails(self):
        self.build_tree(drop_main=(graph.ENGINE_PROJECT,))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn(graph.ENGINE_PROJECT, output)



class EngineTestProjectTests(VisualStudioTreeCase):

    def test_engine_test_project_linking_the_engine_passes(self):
        self.build_tree(extra_tests=[('Test_EngineCore', {'links': 'AfterNightEngine.lib;gtest.lib',
                                                        'includes': ENGINE_INCLUDE})])
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_engine_test_project_linking_another_first_party_library_fails(self):
        """Any AfterNight component but the engine would break self-containment."""
        self.build_tree(extra_tests=[('Test_EngineCore', {'links': 'AfterNightControls.lib'})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightControls', output)

    def test_engine_test_project_including_another_first_party_tree_fails(self):
        self.build_tree(extra_tests=[('Test_EngineCore', {
            'links': 'AfterNightEngine.lib',
            'includes': os.path.join('..', '..', 'src', 'AfterNightProcesses')})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightProcesses', output)

    def test_engine_test_project_including_the_engines_private_tree_fails(self):
        self.build_tree(extra_tests=[('Test_EnginePublicHeaders', {
            'links': 'AfterNightEngine.lib',
            'includes': os.path.join('..', '..', 'src', 'AfterNightEngine', 'src')})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('private', output)

    def test_the_engine_itself_may_use_its_own_private_tree(self):
        self.build_tree(engine_includes=ENGINE_INCLUDE + ';' +
                        os.path.join('..', '..', 'src', 'AfterNightEngine', 'src'))
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


    def test_fuzz_target_linking_the_engine_passes(self):
        """A fuzz target is an engine target: it links the engine and nothing else of AfterNight's."""
        self.build_tree(extra_tests=[('Fuzz_EngineCodec', {'links': 'AfterNightEngine.lib',
                                                         'includes': ENGINE_INCLUDE})])
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


    def test_the_fault_suite_is_expected_to_exist(self):
        """The fault campaign's suite is part of the floor: losing it would lose the campaign."""
        self.assertIn('Test_EngineFaults', graph.EXPECTED_TEST_PROJECTS)

    def test_the_fault_suite_is_an_engine_target(self):
        """It links the engine like every other suite, and is held to an engine target's rules."""
        self.assertTrue(graph.is_engine_target('Test_EngineFaults'))


    def test_the_benchmark_layers_suite_is_not_an_engine_target(self):
        """It shares the benchmark layer's prefix, not this engine's, and links no engine."""
        self.assertFalse(graph.is_engine_target('Test_EngineBenchCommon'))
        self.assertTrue(graph.is_engine_target(graph.BENCH_RUNNER))

    def test_fuzz_target_including_the_engines_private_tree_fails(self):
        self.build_tree(extra_tests=[('Fuzz_EngineCodec', {
            'links': 'AfterNightEngine.lib',
            'includes': os.path.join('..', '..', 'src', 'AfterNightEngine', 'src')})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('private', output)


class LinkedStaticLibraryTests(VisualStudioTreeCase):
    """An engine target's isolation has to hold through the code it links, not only its own.

    The benchmark runner links a static library of engine-neutral fixtures, and a static library
    is not a boundary: its objects end up inside the executable. So a forbidden include directory, a
    source or an import library named by that library reaches the runner exactly as if the runner had
    named it, while every line of the runner's own project stays clean.
    """

    def engine_runner(self, library_kwargs):
        return [('EngineBenchCommon', dict(library_kwargs, targetext='.lib')),
                ('AfterNightEngineBench',
                 {'links': 'EngineBenchCommon.lib;AfterNightEngine.lib',
                  'includes': ENGINE_INCLUDE})]


    def test_a_clean_linked_static_library_passes(self):
        self.build_tree(extra_tests=self.engine_runner({'includes': os.path.join('..', '..', 'src',
                                                                                 'AfterNightBench', 'common')}))
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_the_reach_is_followed_through_two_static_libraries(self):
        self.build_tree(extra_tests=[
            ('BenchFixtures', {'links': 'AfterNightControls.lib', 'targetext': '.lib'}),
            ('EngineBenchCommon', {'links': 'BenchFixtures.lib', 'targetext': '.lib'}),
            ('AfterNightEngineBench', {'links': 'EngineBenchCommon.lib;AfterNightEngine.lib',
                                            'includes': ENGINE_INCLUDE})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('BenchFixtures', output)

    def test_a_shared_library_is_not_followed(self):
        """An import library is a boundary: the code behind it is not linked into the consumer."""
        self.build_tree(extra_tests=[
            ('AfterNightEngineBench', {'links': 'AfterNightEngine.lib', 'includes': ENGINE_INCLUDE})])
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


class RunnerEngineTests(VisualStudioTreeCase):
    """A runner that links no engine still writes a result file; nothing else would notice."""


    def test_the_engine_runner_without_an_engine_fails(self):
        self.build_tree(extra_tests=[('AfterNightEngineBench', {'links': 'EngineBenchCommon.lib'})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineBench', output)


class OutputCollisionTests(VisualStudioTreeCase):
    def test_two_projects_producing_one_binary_fail(self):
        self.build_tree(extra_main=[('AfterNightClone', {'target': graph.ENGINE_PROJECT})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn(graph.ENGINE_PROJECT, output)

    def test_two_projects_sharing_an_intermediate_directory_fail(self):
        shared = os.path.join('..', '..', 'obj', 'shared')
        self.build_tree(extra_main=[
            ('FirstExtra', {'intdir': shared}),
            ('SecondExtra', {'intdir': shared}),
        ])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('intermediate directory', output)

    def test_two_projects_producing_one_import_library_fail(self):
        shared = os.path.join('..', '..', 'lib', 'SharedEngine.lib')
        self.build_tree(extra_main=[
            ('FirstExtra', {'importlib': shared}),
            ('SecondExtra', {'importlib': shared}),
        ])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('import library', output)

    def test_two_projects_producing_one_program_database_fail(self):
        shared = os.path.join('..', '..', 'bin', 'SharedEngine.pdb')
        self.build_tree(extra_main=[
            ('FirstExtra', {'pdb': shared}),
            ('SecondExtra', {'pdb': shared}),
        ])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('symbol file', output)


class CrossTreeCollisionTests(unittest.TestCase):
    """Two generated trees may not write to one object directory, binary or symbol file.

    Stated against the function rather than against a pair of built trees, because what is being
    checked is the comparison itself: the same project exists in both trees and only the toolchain
    each was generated for distinguishes their output.
    """

    @staticmethod
    def tree(label, directory, name, intermediate, binary):
        target = graph.Target(name, os.path.join(directory, name + '.vcxproj'))
        target.add_artifact('intermediate directory', intermediate)
        target.add_artifact('target binary', binary)
        target.add_artifact('target binary name', os.path.basename(binary))
        workspace = graph.Workspace(label, ())
        workspace.targets[name] = target
        return (label, [workspace])

    def check(self, *trees):
        return graph.check_cross_tree_collisions(list(trees))

    def test_separate_object_trees_pass(self):
        problems = self.check(
            self.tree('vs2026-msvc', 'build/vs2026-msvc', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/msvc/', '../../bin/msvc/release/Test_EngineImage.exe'),
            self.tree('vs2026-clang', 'build/vs2026-clang', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/clang-cl/', '../../bin/clang-cl/release/Test_EngineImage.exe'))
        self.assertEqual(problems, [])

    def test_one_object_tree_shared_by_two_toolchains_fails(self):
        problems = self.check(
            self.tree('vs2026-msvc', 'build/vs2026-msvc', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/msvc/', '../../bin/msvc/release/Test_EngineImage.exe'),
            self.tree('vs2026-clang', 'build/vs2026-clang', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/msvc/', '../../bin/clang-cl/release/Test_EngineImage.exe'))
        self.assertEqual(len(problems), 1)
        self.assertIn('intermediate directory', problems[0])
        self.assertIn('vs2026-msvc', problems[0])
        self.assertIn('vs2026-clang', problems[0])

    def test_one_binary_written_by_two_toolchains_fails(self):
        problems = self.check(
            self.tree('vs2026-msvc', 'build/vs2026-msvc', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/msvc/', '../../bin/msvc/release/Test_EngineImage.exe'),
            self.tree('vs2026-clang', 'build/vs2026-clang', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/clang-cl/', '../../bin/msvc/release/Test_EngineImage.exe'))
        self.assertEqual(len(problems), 1)
        self.assertIn('target binary', problems[0])

    def test_the_shared_binary_name_is_not_a_collision(self):
        """Every tree writes a file of the same name; only a shared path is the fault."""
        problems = self.check(
            self.tree('vs2026-msvc', 'build/vs2026-msvc', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/msvc/', '../../bin/msvc/release/Test_EngineImage.exe'),
            self.tree('make-linux', 'build/make-linux', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/clang/', '../../bin/clang/release/Test_EngineImage'))
        self.assertEqual(problems, [])

    def test_one_tree_alone_cannot_collide_with_itself(self):
        problems = self.check(
            self.tree('vs2026-msvc', 'build/vs2026-msvc', 'Test_EngineImage',
                      '../../obj/Test_EngineImage/msvc/', '../../bin/msvc/release/Test_EngineImage.exe'))
        self.assertEqual(problems, [])


class EmptyTreeTests(VisualStudioTreeCase):
    def test_generated_tree_without_projects_fails_loudly(self):
        """A front end that silently parses nothing must never read as a pass."""
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('no project', output.lower())


class OptionalAdapterTests(VisualStudioTreeCase):
    """The optional adapter's two link rules, read from both ends."""

    def test_clean_tree_with_the_adapter_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_adapter_that_does_not_link_the_engine_fails(self):
        self.build_tree(adapter_links={'AfterNightEnginePythonAdapter': 'python314.lib'})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEnginePythonAdapter does not link AfterNightEngine', output)

    def test_the_engine_linking_an_adapter_fails(self):
        self.build_tree(engine_links='AfterNightEnginePythonAdapter.lib')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('an optional adapter is never a dependency of the engine', output)

    def test_another_suite_linking_an_adapter_fails(self):
        self.build_tree(extra_tests=[(
            'Test_EngineImage2',
            {'links': 'AfterNightEngine.lib;AfterNightEnginePythonAdapter.lib'})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('only Test_EnginePythonAdapter may link an optional adapter', output)

    def test_the_adapters_own_suite_may_link_it(self):
        self.build_tree(extra_tests=[(
            'Test_EnginePythonAdapter',
            {'links': 'AfterNightEngine.lib;AfterNightEnginePythonAdapter.lib;gtest.lib'})])
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def adapter_problems(self):
        """The optional adapters' link rules alone, over the test workspace: whether a consumer may reach the engine
        at all is the target registry's, which a synthetic tree does not carry."""
        tests = graph.load_vs_workspace(self.tests, 'vs2026-msvc/tests', graph.EXPECTED_TEST_PROJECTS)
        return graph.check_optional_adapters(tests)

    def test_a_named_consumer_may_link_the_adapter_it_asked_for(self):
        self.build_tree(main_overrides={'AfterNightProcesses': {
            'links': 'AfterNightControls.lib;AfterNightEngine.lib;AfterNightEngineGmicAdapter.lib'}})
        main = graph.load_vs_workspace(self.main, 'vs2026-msvc', graph.EXPECTED_MAIN_PROJECTS)
        self.assertEqual(graph.check_optional_adapters(main), [])

    def test_the_suite_of_the_processes_it_runs_reaches_the_adapter_only_through_the_library(self):
        self.build_tree(extra_tests=[(
            'Test_ProcessesSpatial',
            {'links': 'AfterNightEngine.lib;AfterNightProcesses.lib;AfterNightEngineGmicAdapter.lib;gtest.lib'})])
        problems = self.adapter_problems()
        expected = ('Test_ProcessesSpatial links AfterNightEngineGmicAdapter; only Test_EngineGmicAdapter or '
                    'AfterNightProcesses may link an optional adapter')
        self.assertTrue(any(expected in problem for problem in problems), problems)

    def test_a_named_consumer_of_one_adapter_may_not_link_another(self):
        self.build_tree(extra_tests=[(
            'Test_ProcessesSpatial',
            {'links': 'AfterNightEngine.lib;AfterNightEnginePythonAdapter.lib;gtest.lib'})])
        problems = self.adapter_problems()
        expected = ('Test_ProcessesSpatial links AfterNightEnginePythonAdapter; only Test_EnginePythonAdapter '
                    'may link an optional adapter')
        self.assertTrue(any(expected in problem for problem in problems), problems)

    def test_the_refusal_names_every_target_that_may_link_the_adapter(self):
        self.build_tree(extra_tests=[(
            'Test_EngineImage2',
            {'links': 'AfterNightEngine.lib;AfterNightEngineGmicAdapter.lib'})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('only Test_EngineGmicAdapter or AfterNightProcesses may link an optional adapter', output)

    def test_a_product_target_building_after_an_adapter_fails(self):
        self.build_tree(main_dependencies={
            'AfterNightApp.vcxproj': [PRODUCT_ENGINE + '.vcxproj',
                                      'AfterNightEnginePythonAdapter.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('has taken a dependency on an optional adapter', output)

    def test_a_missing_adapter_project_fails(self):
        self.build_tree(drop_main=('AfterNightEnginePythonAdapter',))
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('project AfterNightEnginePythonAdapter is not generated', output)


class ReferencedProjectBuildOrderTests(VisualStudioTreeCase):
    """A solution that lists a project from another workspace has to order it behind what it links.

    The other workspace's solution stating the edge does not order a build of this one: a clean rebuild of
    the test solution under /m linked both optional adapters before the engine's import library existed.
    """

    def test_a_referenced_adapter_ordered_behind_the_engine_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_a_referenced_adapter_the_solution_does_not_order_fails(self):
        self.build_tree(test_references={os.path.join('..', 'AfterNightEnginePythonAdapter.vcxproj'): []})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('vs2026-msvc/tests: AfterNightTest.slnx lists AfterNightEnginePythonAdapter from another '
                      'workspace and does not build it after AfterNightEngine, which it links', output)
        self.assertNotIn('AfterNightEngineGmicAdapter from another workspace', output)

    def test_a_suite_linking_a_referenced_adapter_it_does_not_build_after_fails(self):
        self.build_tree(
            extra_tests=[(
                'Test_EnginePythonAdapter',
                {'links': 'AfterNightEngine.lib;AfterNightEnginePythonAdapter.lib;gtest.lib'})],
            test_references={'Test_EnginePythonAdapter.vcxproj': [os.path.join('..', 'AfterNightEngine.vcxproj')]})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightTest.slnx lists Test_EnginePythonAdapter and does not build it after '
                      'AfterNightEnginePythonAdapter, which it links', output)

    def test_a_referenced_project_whose_file_is_missing_is_left_to_the_missing_project_rule(self):
        self.build_tree(test_references={os.path.join('..', 'AfterNightEngineVanished.vcxproj'): []})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


class PlatformIsolationTests(VisualStudioTreeCase):
    """The platform library reaches no engine, no Qt and no other product library; its suite reaches it alone."""

    def test_platform_library_linking_the_engine_fails(self):
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {'links': PRODUCT_ENGINE + '.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightPlatform links ' + PRODUCT_ENGINE, output)

    def test_platform_library_linking_the_engines_debug_library_fails(self):
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {'links': 'AfterNightEngine_d.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightPlatform links AfterNightEngine', output)

    def test_platform_library_including_an_engine_tree_fails(self):
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {'includes': ENGINE_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightPlatform includes', output)

    def test_platform_library_linking_qt_fails(self):
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {'links': 'Qt6Cored.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightPlatform reaches Qt', output)

    def test_platform_library_including_qt_fails(self):
        qt_include = os.path.join('..', '..', 'Qt6', '6.10.3', 'msvc2022_64', 'include')
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {'includes': qt_include}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightPlatform reaches Qt', output)

    def test_platform_library_compiling_a_foreign_source_fails(self):
        foreign = os.path.join('..', '..', 'src', 'AfterNightControls', 'app', 'SessionConsoleBridge.cpp')
        own = os.path.join('..', '..', 'src', 'AfterNightPlatform', 'src', 'Logging.cpp')
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {'sources': (own, foreign)}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('SessionConsoleBridge.cpp', output)
        self.assertNotIn('Logging.cpp', output)

    def test_platform_library_compiling_a_source_outside_every_product_tree_fails(self):
        # A vendored source belongs to no first-party project, so only the own-sources rule can refuse it.
        vendored = os.path.join('..', '..', '3rdparty', 'crashpad', 'client', 'crashpad_client_linux.cc')
        own = os.path.join('..', '..', 'src', 'AfterNightPlatform', 'src', 'CrashHandler.cpp')
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {'sources': (own, vendored)}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('crashpad_client_linux.cc, which is not one of its own sources', output)
        self.assertNotIn('CrashHandler.cpp', output)

    def test_platform_suite_linking_the_engine_fails(self):
        self.build_tree(test_overrides={graph.PLATFORM_SUITE: {
            'links': 'AfterNightPlatform.lib;' + PRODUCT_ENGINE + '.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Test_Platform links ' + PRODUCT_ENGINE, output)

    def test_platform_suite_linking_the_platform_library_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_lite_host_linking_the_platform_library_fails(self):
        self.build_tree(main_overrides={'AfterNightExtensionHostLite': {'links': 'AfterNightPlatform.lib'}},
                        main_dependencies={'AfterNightExtensionHostLite.vcxproj': ['AfterNightPlatform.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightExtensionHostLite links AfterNightPlatform', output)
        self.assertIn('AfterNightExtensionHostLite builds after AfterNightPlatform', output)

    def test_protocol_including_the_platform_library_fails(self):
        platform_include = os.path.join('..', '..', 'src', 'AfterNightPlatform', 'include')
        self.build_tree(main_overrides={'AfterNightExtensionProtocol': {'includes': platform_include}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightExtensionProtocol includes', output)


class StylePluginIsolationTests(VisualStudioTreeCase):
    """The style plugin reaches the engine only through the Controls library whose theme it draws."""

    def test_style_plugin_linking_controls_alone_passes(self):
        self.build_tree(main_overrides={graph.STYLE_PLUGIN_PROJECT: {'links': 'AfterNightControls.lib'}},
                        main_dependencies={'AfterNightStylePlugin.vcxproj': ['AfterNightControls.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_style_plugin_linking_the_engine_fails(self):
        self.build_tree(main_overrides={graph.STYLE_PLUGIN_PROJECT: {
            'links': 'AfterNightControls.lib;AfterNightEngine.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightStylePlugin links AfterNightEngine; the style plugin reaches the engine only '
                      'through AfterNightControls', output)

    def test_style_plugin_linking_another_product_library_fails(self):
        self.build_tree(main_overrides={graph.STYLE_PLUGIN_PROJECT: {
            'links': 'AfterNightControls.lib;AfterNightProcesses.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightStylePlugin links AfterNightProcesses', output)

    def test_style_plugin_searching_an_engine_tree_fails(self):
        self.build_tree(main_overrides={graph.STYLE_PLUGIN_PROJECT: {'includes': ENGINE_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightStylePlugin includes', output)
        self.assertIn('from the AfterNightEngine tree', output)


RUNTIME_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightConsumerRuntime', 'include')
RUNTIME_SOURCE = os.path.join('..', '..', 'src', 'AfterNightConsumerRuntime', 'src', 'SessionCore.cpp')
PLATFORM_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightPlatform', 'include')


class ConsumerRuntimeIsolationTests(VisualStudioTreeCase):
    """The consumer runtime reaches the engine and the platform library and nothing else; nothing below it reaches
    it."""

    def runtime_problems(self, runtime=None, suite=None, main_overrides=None):
        runtime = dict({'links': 'AfterNightEngine.lib;AfterNightPlatform.lib',
                        'includes': ';'.join((RUNTIME_INCLUDE, ENGINE_INCLUDE, PLATFORM_INCLUDE)),
                        'sources': (RUNTIME_SOURCE,)}, **(runtime or {}))
        suite = dict({'links': 'AfterNightConsumerRuntime.lib;AfterNightEngine.lib;AfterNightPlatform.lib;gtest.lib',
                      'includes': ';'.join((RUNTIME_INCLUDE, ENGINE_INCLUDE, PLATFORM_INCLUDE))}, **(suite or {}))
        self.build_tree(extra_main=[(graph.CONSUMER_RUNTIME_PROJECT, runtime)],
                        extra_tests=[(graph.CONSUMER_RUNTIME_SUITE, suite)], main_overrides=main_overrides)
        main = graph.load_vs_workspace(self.main, 'vs2026-msvc', graph.EXPECTED_MAIN_PROJECTS)
        tests = graph.load_vs_workspace(self.tests, 'vs2026-msvc/tests', graph.EXPECTED_TEST_PROJECTS)
        return graph.check_consumer_runtime_isolation(main) + graph.check_consumer_runtime_isolation(tests)

    def test_the_runtime_on_the_new_engine_and_the_platform_library_passes(self):
        self.assertEqual(self.runtime_problems(), [])


    def test_the_runtime_linking_a_product_library_fails(self):
        problems = self.runtime_problems(runtime={'links': 'AfterNightEngine.lib;AfterNightControls.lib'})
        self.assertTrue(any('AfterNightConsumerRuntime links AfterNightControls' in problem for problem in problems),
                        problems)

    def test_the_runtime_reaching_qt_fails(self):
        qt_include = os.path.join('..', '..', 'Qt6', '6.10.3', 'msvc2022_64', 'include')
        problems = self.runtime_problems(runtime={'includes': qt_include})
        self.assertTrue(any('AfterNightConsumerRuntime reaches Qt' in problem for problem in problems), problems)

    def test_the_runtime_reaching_python_fails(self):
        problems = self.runtime_problems(runtime={'links': 'AfterNightEngine.lib;python314.lib'})
        self.assertTrue(any('AfterNightConsumerRuntime reaches Python' in problem for problem in problems), problems)

    def test_the_runtime_compiling_a_foreign_source_fails(self):
        foreign = os.path.join('..', '..', 'src', 'AfterNightControls', 'app', 'SessionConsoleBridge.cpp')
        problems = self.runtime_problems(runtime={'sources': (RUNTIME_SOURCE, foreign)})
        self.assertTrue(any('SessionConsoleBridge.cpp' in problem for problem in problems), problems)
        self.assertFalse(any('SessionCore.cpp' in problem for problem in problems), problems)

    def test_the_suite_reaching_qt_fails(self):
        problems = self.runtime_problems(suite={'links': 'AfterNightConsumerRuntime.lib;Qt6Core.lib'})
        self.assertTrue(any('Test_ConsumerRuntime reaches Qt' in problem for problem in problems), problems)


    def test_the_platform_library_linking_the_runtime_fails(self):
        problems = self.runtime_problems(main_overrides={graph.PLATFORM_PROJECT: {'links': 'AfterNightConsumerRuntime.lib'}})
        self.assertTrue(any('AfterNightPlatform links AfterNightConsumerRuntime' in problem for problem in problems),
                        problems)

    def test_the_lite_host_including_the_runtime_fails(self):
        problems = self.runtime_problems(main_overrides={'AfterNightExtensionHostLite': {'includes': RUNTIME_INCLUDE}})
        self.assertTrue(any('AfterNightExtensionHostLite includes' in problem for problem in problems), problems)


CLI_SOURCE = os.path.join('..', '..', 'src', 'AfterNightCLI', 'OneShotRun.cpp')
CLI_SUITE_SOURCES = (os.path.join('..', '..', 'src', 'AfterNightTests', 'Test_AfterNightCLI', 'OneShotRunTest.cpp'),
                     os.path.join('..', '..', 'src', 'AfterNightTests', 'fixtures', 'common', 'FixtureFits.cpp'),
                     CLI_SOURCE)


class CommandLineToolIsolationTests(VisualStudioTreeCase):
    """The command-line tool and its suite reach no Qt, no Python and no product library, and compile only what is
    theirs."""

    def tool_problems(self, tool=None, suite=None):
        tool = dict({'links': 'AfterNightConsumerRuntime.lib;AfterNightEngine.lib;AfterNightPlatform.lib',
                     'includes': ';'.join((RUNTIME_INCLUDE, ENGINE_INCLUDE, PLATFORM_INCLUDE)),
                     'sources': (CLI_SOURCE,)}, **(tool or {}))
        suite = dict({'links': 'AfterNightConsumerRuntime.lib;AfterNightEngine.lib;AfterNightPlatform.lib;gtest.lib',
                      'includes': ';'.join((RUNTIME_INCLUDE, ENGINE_INCLUDE, PLATFORM_INCLUDE)),
                      'sources': CLI_SUITE_SOURCES}, **(suite or {}))
        self.build_tree(extra_tests=[(graph.CLI_SUITE, suite)], main_overrides={graph.CLI_PROJECT: tool})
        main = graph.load_vs_workspace(self.main, 'vs2026-msvc', graph.EXPECTED_MAIN_PROJECTS)
        tests = graph.load_vs_workspace(self.tests, 'vs2026-msvc/tests', graph.EXPECTED_TEST_PROJECTS)
        return graph.check_command_line_tool_isolation(main) + graph.check_command_line_tool_isolation(tests)

    def test_the_tool_and_its_suite_on_the_runtime_pass(self):
        self.assertEqual(self.tool_problems(), [])

    def test_the_default_tree_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_the_tool_linking_a_product_library_fails(self):
        problems = self.tool_problems(tool={'links': 'AfterNightEngine.lib;AfterNightControls.lib'})
        self.assertTrue(any('AfterNightCLI links AfterNightControls' in problem for problem in problems), problems)

    def test_the_tool_linking_an_optional_adapter_fails(self):
        problems = self.tool_problems(tool={'links': 'AfterNightEngine.lib;AfterNightEngineGmicAdapter.lib'})
        self.assertTrue(any('AfterNightCLI links AfterNightEngineGmicAdapter' in problem for problem in problems),
                        problems)

    def test_the_tool_reaching_qt_fails(self):
        qt_include = os.path.join('..', '..', 'Qt6', '6.10.3', 'msvc2022_64', 'include')
        problems = self.tool_problems(tool={'includes': qt_include})
        self.assertTrue(any('AfterNightCLI reaches Qt' in problem for problem in problems), problems)

    def test_the_tool_reaching_python_fails(self):
        problems = self.tool_problems(tool={'links': 'AfterNightEngine.lib;python314.lib'})
        self.assertTrue(any('AfterNightCLI reaches Python' in problem for problem in problems), problems)

    def test_the_tool_compiling_a_foreign_source_fails(self):
        foreign = os.path.join('..', '..', 'src', 'AfterNightControls', 'app', 'SessionConsoleBridge.cpp')
        problems = self.tool_problems(tool={'sources': (CLI_SOURCE, foreign)})
        self.assertTrue(any('SessionConsoleBridge.cpp' in problem for problem in problems), problems)
        self.assertFalse(any('OneShotRun.cpp' in problem for problem in problems), problems)

    def test_the_suite_compiling_a_product_library_source_fails(self):
        foreign = os.path.join('..', '..', 'src', 'AfterNightControls', 'app', 'ApplicationStorage.cpp')
        problems = self.tool_problems(suite={'sources': CLI_SUITE_SOURCES + (foreign,)})
        self.assertTrue(any('Test_AfterNightCLI compiles' in problem and 'ApplicationStorage.cpp' in problem
                            for problem in problems), problems)
        self.assertFalse(any('FixtureFits.cpp' in problem for problem in problems), problems)

    def test_the_suite_reaching_qt_fails(self):
        problems = self.tool_problems(suite={'links': 'AfterNightConsumerRuntime.lib;Qt6Core.lib'})
        self.assertTrue(any('Test_AfterNightCLI reaches Qt' in problem for problem in problems), problems)


BACKEND_SOURCE = os.path.join('..', '..', 'src', 'AfterNightExtensionRuntimeBackend', 'RuntimeSeat.cpp')
BACKEND_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightExtensionRuntimeBackend')
BACKEND_SUITE_SOURCE = os.path.join('..', '..', 'src', 'AfterNightTests', 'Test_ExtensionRuntimeBackend',
                                    'WritableExportTest.cpp')
PYTHON_INCLUDE = os.path.join('..', '..', 'python', 'python', 'tools_msvc', 'include')


class RuntimeBackendIsolationTests(VisualStudioTreeCase):
    """The runtime backend and its driver reach the interpreter but no Qt and no product library, and compile only what
    is theirs."""

    def backend_problems(self, backend=None, suite=None):
        backend = dict({'links': 'AfterNightConsumerRuntime.lib;AfterNightEngine.lib;AfterNightPlatform.lib;'
                                 'python314.lib',
                        'includes': ';'.join((BACKEND_INCLUDE, RUNTIME_INCLUDE, ENGINE_INCLUDE, PLATFORM_INCLUDE,
                                              PYTHON_INCLUDE)),
                        'sources': (BACKEND_SOURCE,)}, **(backend or {}))
        suite = dict({'links': 'AfterNightConsumerRuntime.lib;AfterNightEngine.lib;AfterNightPlatform.lib;'
                               'python314.lib;gtest.lib',
                      'includes': ';'.join((BACKEND_INCLUDE, RUNTIME_INCLUDE, ENGINE_INCLUDE, PLATFORM_INCLUDE,
                                            PYTHON_INCLUDE)),
                      'sources': (BACKEND_SUITE_SOURCE,)}, **(suite or {}))
        self.build_tree(extra_tests=[(graph.RUNTIME_BACKEND_SUITE, suite)],
                        main_overrides={graph.RUNTIME_BACKEND_PROJECT: backend})
        main = graph.load_vs_workspace(self.main, 'vs2026-msvc', graph.EXPECTED_MAIN_PROJECTS)
        tests = graph.load_vs_workspace(self.tests, 'vs2026-msvc/tests', graph.EXPECTED_TEST_PROJECTS)
        return graph.check_runtime_backend_isolation(main) + graph.check_runtime_backend_isolation(tests)

    def test_the_backend_and_its_driver_on_the_runtime_and_the_interpreter_pass(self):
        self.assertEqual(self.backend_problems(), [])

    def test_the_default_tree_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_the_backend_linking_a_product_library_fails(self):
        problems = self.backend_problems(backend={'links': 'AfterNightEngine.lib;AfterNightControls.lib'})
        self.assertTrue(any('AfterNightExtensionRuntimeBackend links AfterNightControls' in problem
                            for problem in problems), problems)

    def test_the_backend_linking_an_optional_adapter_fails(self):
        problems = self.backend_problems(
            backend={'links': 'AfterNightEngine.lib;AfterNightEnginePythonAdapter.lib'})
        self.assertTrue(any('AfterNightExtensionRuntimeBackend links AfterNightEnginePythonAdapter' in problem
                            for problem in problems), problems)

    def test_the_backend_including_the_protocol_library_fails(self):
        protocol = os.path.join('..', '..', 'src', 'AfterNightExtensionProtocol')
        problems = self.backend_problems(backend={'includes': ';'.join((BACKEND_INCLUDE, protocol))})
        self.assertTrue(any('AfterNightExtensionRuntimeBackend includes' in problem and
                            'AfterNightExtensionProtocol' in problem for problem in problems), problems)

    def test_the_backend_reaching_qt_fails(self):
        qt_include = os.path.join('..', '..', 'Qt6', '6.10.3', 'msvc2022_64', 'include')
        problems = self.backend_problems(backend={'includes': ';'.join((BACKEND_INCLUDE, qt_include))})
        self.assertTrue(any('AfterNightExtensionRuntimeBackend reaches Qt' in problem for problem in problems),
                        problems)

    def test_the_backend_compiling_a_foreign_source_fails(self):
        foreign = os.path.join('..', '..', 'src', 'AfterNightControls', 'app', 'ExtensionHostManager.cpp')
        problems = self.backend_problems(backend={'sources': (BACKEND_SOURCE, foreign)})
        self.assertTrue(any('ExtensionHostManager.cpp' in problem for problem in problems), problems)
        self.assertFalse(any('RuntimeSeat.cpp' in problem for problem in problems), problems)

    def test_the_driver_compiling_the_backends_source_fails(self):
        problems = self.backend_problems(suite={'sources': (BACKEND_SUITE_SOURCE, BACKEND_SOURCE)})
        self.assertTrue(any('Test_ExtensionRuntimeBackend compiles' in problem and 'RuntimeSeat.cpp' in problem
                            for problem in problems), problems)
        self.assertFalse(any('WritableExportTest.cpp' in problem for problem in problems), problems)

    def test_the_driver_reaching_qt_fails(self):
        problems = self.backend_problems(suite={'links': 'AfterNightConsumerRuntime.lib;Qt6Core.lib'})
        self.assertTrue(any('Test_ExtensionRuntimeBackend reaches Qt' in problem for problem in problems), problems)

    def test_the_driver_linking_a_product_library_fails(self):
        problems = self.backend_problems(suite={'links': 'AfterNightConsumerRuntime.lib;AfterNightProcesses.lib'})
        self.assertTrue(any('Test_ExtensionRuntimeBackend links AfterNightProcesses' in problem
                            for problem in problems), problems)


PROCESSES_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightProcesses')
CONTROLS_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightControls')


class ProductLayerTests(VisualStudioTreeCase):
    """Controls sits below Processes, and both below the application and the full host."""

    def test_controls_including_the_processes_tree_fails(self):
        self.build_tree(main_overrides={'AfterNightControls': {'includes': CONTROLS_INCLUDE + ';' + PROCESSES_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightControls includes %s from the AfterNightProcesses tree' % PROCESSES_INCLUDE, output)

    def test_controls_linking_processes_fails(self):
        self.build_tree(main_overrides={'AfterNightControls': {
                            'links': PRODUCT_ENGINE + '.lib;AfterNightProcesses.lib'}},
                        main_dependencies={'AfterNightControls.vcxproj': ['AfterNightProcesses.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightControls links AfterNightProcesses', output)
        self.assertIn('AfterNightControls builds after AfterNightProcesses', output)

    def test_controls_compiling_a_processes_source_fails(self):
        foreign = os.path.join('..', '..', 'src', 'AfterNightProcesses', 'processes', 'CurvesProcess.cpp')
        self.build_tree(main_overrides={'AfterNightControls': {'sources': (foreign,)}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightControls compiles %s from the AfterNightProcesses tree' % foreign, output)

    def test_processes_including_the_application_tree_fails(self):
        application = os.path.join('..', '..', 'src', 'AfterNightApp')
        self.build_tree(main_overrides={'AfterNightProcesses': {'includes': application}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightProcesses includes %s from the AfterNightApp tree' % application, output)

    def test_processes_including_the_controls_tree_passes(self):
        self.build_tree(main_overrides={'AfterNightProcesses': {'includes': PROCESSES_INCLUDE + ';' + CONTROLS_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_a_suite_including_both_libraries_passes(self):
        self.build_tree(test_overrides={'Test_ProcessFramework': {
            'includes': CONTROLS_INCLUDE + ';' + PROCESSES_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


SHELL_CORE_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightShellCore')


class ShellCoreLayerTests(VisualStudioTreeCase):
    """The shell core sits above Controls and Processes and below the application; no extension host reaches it."""

    def test_the_shell_core_is_generated(self):
        self.assertIn('AfterNightShellCore', graph.EXPECTED_MAIN_PROJECTS)
        self.assertIn('Test_ShellCore', graph.EXPECTED_TEST_PROJECTS)

    def test_controls_including_the_shell_core_tree_fails(self):
        self.build_tree(main_overrides={'AfterNightControls': {'includes': CONTROLS_INCLUDE + ';' + SHELL_CORE_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightControls includes %s from the AfterNightShellCore tree' % SHELL_CORE_INCLUDE, output)

    def test_processes_linking_the_shell_core_fails(self):
        self.build_tree(main_overrides={'AfterNightProcesses': {
                            'links': PRODUCT_ENGINE + '.lib;AfterNightShellCore.lib'}},
                        main_dependencies={'AfterNightProcesses.vcxproj': ['AfterNightShellCore.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightProcesses links AfterNightShellCore', output)
        self.assertIn('AfterNightProcesses builds after AfterNightShellCore', output)

    def test_the_shell_core_including_the_application_tree_fails(self):
        application = os.path.join('..', '..', 'src', 'AfterNightApp')
        self.build_tree(main_overrides={'AfterNightShellCore': {'includes': SHELL_CORE_INCLUDE + ';' + application}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightShellCore includes %s from the AfterNightApp tree' % application, output)

    def test_the_shell_core_linking_the_full_host_fails(self):
        self.build_tree(main_overrides={'AfterNightShellCore': {
            'links': PRODUCT_ENGINE + '.lib;AfterNightExtensionHost.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightShellCore links AfterNightExtensionHost', output)

    def test_the_shell_core_reaching_processes_and_controls_passes(self):
        self.build_tree(main_overrides={'AfterNightShellCore': {
                            'includes': SHELL_CORE_INCLUDE + ';' + PROCESSES_INCLUDE + ';' + CONTROLS_INCLUDE,
                            'links': PRODUCT_ENGINE + '.lib;AfterNightProcesses.lib;AfterNightControls.lib'}},
                        main_dependencies={'AfterNightShellCore.vcxproj': [
                            PRODUCT_ENGINE + '.vcxproj', 'AfterNightProcesses.vcxproj', 'AfterNightControls.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_the_full_host_linking_the_shell_core_fails(self):
        self.build_tree(main_overrides={'AfterNightExtensionHost': {
            'links': PRODUCT_ENGINE + '.lib;AfterNightShellCore.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightExtensionHost links AfterNightShellCore; an extension host links no shell library',
                      output)

    def test_the_full_host_including_the_shell_core_tree_fails(self):
        self.build_tree(main_overrides={'AfterNightExtensionHost': {'includes': SHELL_CORE_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightExtensionHost includes %s from the AfterNightShellCore tree' % SHELL_CORE_INCLUDE,
                      output)

    def test_the_application_linking_the_shell_core_passes(self):
        self.build_tree(main_overrides={'AfterNightApp': {
                            'links': PRODUCT_ENGINE + '.lib;AfterNightShellCore.lib;AfterNightProcesses.lib'}},
                        main_dependencies={'AfterNightApp.vcxproj': [
                            PRODUCT_ENGINE + '.vcxproj', 'AfterNightShellCore.vcxproj', 'AfterNightProcesses.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


SHELL_CLASSIC_INCLUDE = os.path.join('..', '..', 'src', 'AfterNightShellClassic')


class ClassicShellLayerTests(VisualStudioTreeCase):
    """The classic shell sits above the shell core and below the application; no extension host reaches it."""

    def test_the_classic_shell_is_generated(self):
        self.assertIn('AfterNightShellClassic', graph.EXPECTED_MAIN_PROJECTS)
        self.assertIn('Test_ShellClassic', graph.EXPECTED_TEST_PROJECTS)
        self.assertIn('Test_ShellParity', graph.EXPECTED_TEST_PROJECTS)

    def test_the_shell_core_including_the_classic_shell_tree_fails(self):
        self.build_tree(main_overrides={'AfterNightShellCore': {
            'includes': SHELL_CORE_INCLUDE + ';' + SHELL_CLASSIC_INCLUDE}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightShellCore includes %s from the AfterNightShellClassic tree' % SHELL_CLASSIC_INCLUDE,
                      output)

    def test_controls_linking_the_classic_shell_fails(self):
        self.build_tree(main_overrides={'AfterNightControls': {
            'links': PRODUCT_ENGINE + '.lib;AfterNightShellClassic.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightControls links AfterNightShellClassic', output)

    def test_the_classic_shell_including_the_application_tree_fails(self):
        application = os.path.join('..', '..', 'src', 'AfterNightApp')
        self.build_tree(main_overrides={'AfterNightShellClassic': {
            'includes': SHELL_CLASSIC_INCLUDE + ';' + application}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightShellClassic includes %s from the AfterNightApp tree' % application, output)

    def test_the_full_host_linking_the_classic_shell_fails(self):
        self.build_tree(main_overrides={'AfterNightExtensionHost': {
            'links': PRODUCT_ENGINE + '.lib;AfterNightShellClassic.lib'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightExtensionHost links AfterNightShellClassic; an extension host links no shell library',
                      output)

    def test_the_classic_shell_reaching_the_shell_core_passes(self):
        self.build_tree(main_overrides={'AfterNightShellClassic': {
                            'includes': SHELL_CLASSIC_INCLUDE + ';' + SHELL_CORE_INCLUDE + ';' + CONTROLS_INCLUDE,
                            'links': PRODUCT_ENGINE + '.lib;AfterNightShellCore.lib;AfterNightControls.lib'}},
                        main_dependencies={'AfterNightShellClassic.vcxproj': [
                            PRODUCT_ENGINE + '.vcxproj', 'AfterNightShellCore.vcxproj', 'AfterNightControls.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


OPENCL_SDK_INCLUDE = os.path.join('..', '..', 'vcpkg', 'installed', 'x64-windows-opencl', 'include')
GPU_BACKEND = 'AfterNightEngineOpenClBackend'
GPU_BACKEND_SUITE = 'Test_EngineOpenClBackend'


class OptionalGpuBackendTests(VisualStudioTreeCase):
    """The opt-in GPU backend: absent from a default tree, generated together with its suite when it
    is asked for, and the only target that reaches a GPU SDK."""

    def build_opted_in_tree(self, backend=True, suite=True, backend_links=None, suite_links=None,
                            extra_main=(), extra_tests=(), main_dependencies=None, **kwargs):
        main = list(extra_main)
        dependencies = {}
        if backend:
            main.append((GPU_BACKEND, {
                'links': ('AfterNightEngine.lib;OpenCL.lib;delayimp.lib' if backend_links is None
                          else backend_links),
                'includes': ENGINE_INCLUDE + ';' + OPENCL_SDK_INCLUDE}))
            dependencies[GPU_BACKEND + '.vcxproj'] = [graph.ENGINE_PROJECT + '.vcxproj']
        dependencies.update(main_dependencies or {})
        tests = list(extra_tests)
        if suite:
            tests.append((GPU_BACKEND_SUITE, {
                'links': ('AfterNightEngine.lib;%s.lib;gtest.lib' % GPU_BACKEND if suite_links is None
                          else suite_links),
                'includes': ENGINE_INCLUDE}))
        self.build_tree(extra_main=main, extra_tests=tests, main_dependencies=dependencies, **kwargs)

    def test_a_default_tree_generates_no_gpu_backend_and_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertNotIn(GPU_BACKEND, output)

    def test_an_opted_in_tree_with_the_backend_and_its_suite_passes(self):
        self.build_opted_in_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_a_backend_generated_without_its_suite_fails(self):
        self.build_opted_in_tree(suite=False)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('%s is generated without %s' % (GPU_BACKEND, GPU_BACKEND_SUITE), output)

    def test_a_suite_generated_without_its_backend_fails(self):
        self.build_opted_in_tree(backend=False, suite_links='AfterNightEngine.lib;gtest.lib')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('%s is generated without %s' % (GPU_BACKEND_SUITE, GPU_BACKEND), output)

    def test_a_backend_that_does_not_link_the_engine_fails(self):
        self.build_opted_in_tree(backend_links='OpenCL.lib;delayimp.lib')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('%s does not link AfterNightEngine' % GPU_BACKEND, output)

    def test_the_engine_linking_the_backend_fails(self):
        self.build_opted_in_tree(engine_links=GPU_BACKEND + '.lib')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('an optional GPU backend is never a dependency of the engine', output)

    def test_another_suite_linking_the_backend_fails(self):
        self.build_opted_in_tree(extra_tests=[(
            'Test_EngineKernels2', {'links': 'AfterNightEngine.lib;%s.lib' % GPU_BACKEND})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('only %s may link an optional GPU backend' % GPU_BACKEND_SUITE, output)

    def test_a_product_target_building_after_the_backend_fails(self):
        self.build_opted_in_tree(main_dependencies={
            'AfterNightCLI.vcxproj': [PRODUCT_ENGINE + '.vcxproj', GPU_BACKEND + '.vcxproj']})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('has taken a dependency on an optional GPU backend', output)

    def test_the_engine_including_a_gpu_sdk_fails(self):
        self.build_tree(engine_includes=ENGINE_INCLUDE + ';' + OPENCL_SDK_INCLUDE)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngine reaches a GPU SDK through', output)

    def test_the_backends_own_suite_linking_the_gpu_sdk_fails(self):
        self.build_opted_in_tree(
            suite_links='AfterNightEngine.lib;%s.lib;OpenCL.lib;gtest.lib' % GPU_BACKEND)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('%s reaches a GPU SDK through OpenCL.lib' % GPU_BACKEND_SUITE, output)

    def test_a_product_target_including_a_cuda_toolkit_fails(self):
        toolkit = 'C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v13.0\\include'
        self.build_tree(extra_main=[('AfterNightGpuTool', {'includes': toolkit})])
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightGpuTool reaches a GPU SDK through', output)


class MakefileTreeCase(unittest.TestCase):
    """The Linux generator emits makefiles rather than MSBuild projects."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='an-graph-make-test-')
        self.make = os.path.join(self.root, 'build', 'make-linux')
        self.tests = os.path.join(self.make, 'tests')
        os.makedirs(self.tests)
        self.addCleanup(shutil.rmtree, self.root, True)

    def write_project(self, directory, name, includes='', libs='', sources=(), target=None,
                      lddeps='', cxxflags='-m64 -O3 -mavx2 -std=c++20'):
        lines = [
            'PROJECT = %s' % name,
            'TARGETDIR = ../../bin/clang/release',
            'TARGET = $(TARGETDIR)/%s' % (target or name),
            'ALL_CXXFLAGS += %s' % cxxflags,
            'INCLUDES += %s' % includes,
            'LIBS += %s' % libs,
            'LDDEPS += %s' % lddeps,
            '',
        ]
        for source in sources:
            stem = os.path.basename(source).rsplit('.', 1)[0]
            lines.append('$(OBJDIR)/%s.o: %s' % (stem, source))
        with open(os.path.join(directory, name + '.make'), 'w', encoding='utf-8') as handle:
            handle.write('\n'.join(lines))

    def write_workspace(self, directory, names, prerequisites=None):
        """`prerequisites` maps a project name to the projects it must build after."""
        prerequisites = prerequisites or {}
        lines = ['PROJECTS := %s' % ' '.join(names), '', 'all: $(PROJECTS)', '']
        for name in names:
            lines.append('%s:%s' % (name, ''.join(' ' + entry for entry in prerequisites.get(name, []))))
            lines.append('\t@${MAKE} --no-print-directory -C . -f %s.make' % name)
            lines.append('')
        lines.append('clean:')
        lines.append('')
        with open(os.path.join(directory, 'Makefile'), 'w', encoding='utf-8') as handle:
            handle.write('\n'.join(lines))

    def build_tree(self, engine_libs='',
                   engine_includes='-I../../src/AfterNightEngine/include',
                   test_libs=None, test_prerequisites=None, test_lddeps=None, adapter_libs=None,
                   main_overrides=None):
        main_overrides = main_overrides or {}
        for name in graph.EXPECTED_MAIN_PROJECTS:
            libs = '' if name == graph.PLATFORM_PROJECT else '-l' + PRODUCT_ENGINE
            if name == graph.STYLE_PLUGIN_PROJECT:
                # The style plugin links the Controls library whose theme it draws, as the generator emits it.
                libs = '-l' + graph.STYLE_PLUGIN_THROUGH
            kwargs = dict({'target': 'lib%s.so' % name, 'libs': libs}, **main_overrides.get(name, {}))
            self.write_project(self.make, name, **kwargs)
        self.write_project(self.make, graph.ENGINE_PROJECT, includes=engine_includes,
                           libs=engine_libs, target='libAfterNightEngine.so',
                           cxxflags='-m64 -O3 -mavx2 -ffp-contract=off -std=c++20',
                           sources=('../../src/AfterNightEngine/src/core/LibraryInfo.cpp',))
        for adapter in sorted(graph.OPTIONAL_ADAPTER_SUITES):
            self.write_project(self.make, adapter, includes=engine_includes,
                               libs=adapter_libs.get(adapter, '-lAfterNightEngine')
                               if adapter_libs else '-lAfterNightEngine',
                               target='lib%s.so' % adapter)
        adapters = sorted(graph.OPTIONAL_ADAPTER_SUITES)
        main_names = list(graph.EXPECTED_MAIN_PROJECTS) + [graph.ENGINE_PROJECT] + adapters
        prerequisites = {name: [PRODUCT_ENGINE] for name in main_names
                         if name not in graph.ENGINE_PROJECTS + (graph.PLATFORM_PROJECT,)
                         and name not in adapters}
        for adapter in adapters:
            prerequisites[adapter] = [graph.ENGINE_PROJECT]
        prerequisites[graph.STYLE_PLUGIN_PROJECT] = [graph.STYLE_PLUGIN_THROUGH]
        self.write_workspace(self.make, main_names, prerequisites)
        test_libs = test_libs or {}
        test_lddeps = test_lddeps or {}
        for name in graph.EXPECTED_TEST_PROJECTS:
            # EngineBenchCommon is a static archive, which is what makes a missing link
            # prerequisite on it a stale-binary trap rather than a cosmetic omission.
            default_libs = ('-lAfterNightEngine -lgtest' if graph.is_engine_target(name)
                            else '-lAfterNightPlatform -lgtest' if name == graph.PLATFORM_SUITE
                            else '-l%s -lgtest' % PRODUCT_ENGINE)
            self.write_project(self.tests, name, libs=test_libs.get(name, default_libs),
                               lddeps=test_lddeps.get(name, ''),
                               target='libEngineBenchCommon.a' if name == 'EngineBenchCommon' else None)
        self.write_workspace(self.tests, list(graph.EXPECTED_TEST_PROJECTS), test_prerequisites)

    def run_check(self):
        stream = io.StringIO()
        code = graph.main(['--repo-root', self.root], stream=stream)
        return code, stream.getvalue()

    def test_clean_make_tree_passes(self):
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_platform_library_reaching_qt_in_a_make_tree_fails(self):
        self.build_tree(main_overrides={graph.PLATFORM_PROJECT: {
            'includes': '-I../../Qt6/6.10.3/gcc_64/include', 'libs': '-lQt6Core'}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightPlatform reaches Qt', output)

    def test_controls_including_the_processes_tree_in_a_make_tree_fails(self):
        self.build_tree(main_overrides={'AfterNightControls': {
            'includes': '-I../../src/AfterNightControls -I../../src/AfterNightProcesses',
            'libs': '-l' + PRODUCT_ENGINE}})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightControls includes ../../src/AfterNightProcesses from the AfterNightProcesses tree',
                      output)


    def test_an_engine_consumer_may_name_a_library_by_its_bare_name(self):
        self.build_tree(test_libs={'AfterNightEngineBench': '-lAfterNightEngine -lavformat -lavcodec'})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


    def test_linking_a_workspace_library_without_declaring_it_fails(self):
        """A parallel build may reach the consumer before the library it links is current."""
        self.build_tree(test_libs={'AfterNightEngineBench': '-lAfterNightEngine -lEngineBenchCommon'})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineBench links EngineBenchCommon', output)

    def test_linking_a_workspace_library_and_declaring_it_passes(self):
        self.build_tree(test_libs={'AfterNightEngineBench': '-lAfterNightEngine -lEngineBenchCommon'},
                        test_prerequisites={'AfterNightEngineBench': ['EngineBenchCommon']},
                        test_lddeps={'AfterNightEngineBench': '../../lib/clang/libEngineBenchCommon.a'})
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def test_build_order_without_a_link_prerequisite_fails(self):
        """Ordering alone leaves the consumer linked against the previous library.

        The consumer's binary is newer than its own objects, so make relinks nothing when only the
        library changed - and the result is one process holding two layouts of one class.
        """
        self.build_tree(test_libs={'AfterNightEngineBench': '-lAfterNightEngine -lEngineBenchCommon'},
                        test_prerequisites={'AfterNightEngineBench': ['EngineBenchCommon']})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineBench links the static library EngineBenchCommon', output)
        self.assertIn('relink', output)

    def test_the_debug_spelling_of_a_workspace_library_names_the_same_project(self):
        self.build_tree(test_libs={'AfterNightEngineBench': '-lAfterNightEngine -lEngineBenchCommon_d'})
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('AfterNightEngineBench links EngineBenchCommon', output)

    def test_a_library_outside_this_workspace_needs_no_prerequisite(self):
        """The engine builds in the other workspace, which a makefile here cannot name."""
        self.build_tree()
        code, output = self.run_check()
        self.assertEqual(code, 0, output)


class StaleTreeTests(VisualStudioTreeCase):
    """A tree older than the definitions that produce it describes a graph nobody builds."""

    def age_definitions(self, newer_than_tree):
        premake = os.path.join(self.root, 'build', 'premake')
        os.makedirs(premake, exist_ok=True)
        definition = os.path.join(premake, 'afternight-pm5.lua')
        with open(definition, 'w', encoding='utf-8') as handle:
            handle.write('-- definitions\n')
        newest = 0.0
        for base, _directories, files in os.walk(self.main):
            for name in files:
                newest = max(newest, os.path.getmtime(os.path.join(base, name)))
        offset = 60 if newer_than_tree else -60
        os.utime(definition, (newest + offset, newest + offset))

    def test_stale_tree_is_skipped_and_reported(self):
        self.build_tree(engine_links='AfterNightControls.lib')
        self.age_definitions(newer_than_tree=True)
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('stale', output.lower())
        self.assertIn('no current generated project tree', output.lower())

    def test_current_tree_is_checked(self):
        self.build_tree()
        self.age_definitions(newer_than_tree=False)
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertNotIn('stale', output.lower())

    def test_generation_stamp_proves_an_unchanged_tree_was_regenerated(self):
        self.build_tree()
        self.age_definitions(newer_than_tree=True)
        definition = os.path.join(self.root, 'build', 'premake', 'afternight-pm5.lua')
        stamp = os.path.join(self.main, '.premake-generated')
        with open(stamp, 'w', encoding='utf-8') as handle:
            handle.write('generated\n')
        definition_mtime = os.path.getmtime(definition)
        os.utime(stamp, (definition_mtime + 60, definition_mtime + 60))

        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertNotIn('stale', output.lower())


class InstructionSetTests(unittest.TestCase):
    """No project is compiled at a raised instruction set, and only the vector units name one.

    The engine carries kernels for more than one instruction set, and the safe arrangement is the
    one where the sets exist as per-target functions inside an ordinary object. The dangerous one -
    a whole translation unit built with a raised switch - produces the same test results and the
    same benchmark numbers, right up to the first host without those instructions. Nothing but the
    generated project says which arrangement a build has.
    """

    def workspace(self, targets):
        space = graph.Workspace('tree/main', graph.EXPECTED_MAIN_PROJECTS)
        for name, flags in targets.items():
            target = graph.Target(name, name + '.vcxproj')
            target.compile_flags = list(flags)
            space.targets[name] = target
        return space

    def test_the_packaged_floor_at_project_scope_is_what_a_clean_tree_looks_like(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('the whole project', '/arch:AVX2'),
                                    ('the whole project', 'AdvancedVectorExtensions2'),
                                    ('the whole project', '/utf-8')],
            'AfterNightControls': [('the whole project', '-mavx2')],
        })
        self.assertEqual(graph.check_instruction_sets(space), [])

    def test_a_project_wide_avx512_switch_is_refused(self):
        space = self.workspace({graph.ENGINE_PROJECT: [('the whole project', '/arch:AVX512')]})
        problems = graph.check_instruction_sets(space)
        self.assertTrue(any('above the packaged floor' in problem for problem in problems), problems)

    def test_a_per_file_avx512_switch_is_refused_even_on_a_vector_unit(self):
        """The isolation is the abstraction's, not the build system's: no switch says AVX-512."""
        space = self.workspace({
            graph.ENGINE_PROJECT: [
                ('src/backends/cpu/SimdKernelTables.cpp', '-mavx512f')]})
        problems = graph.check_instruction_sets(space)
        self.assertTrue(any('above the packaged floor' in problem for problem in problems), problems)

    def test_an_avx512_switch_in_any_other_project_is_refused_too(self):
        space = self.workspace({'AfterNightControls': [('the whole project', '/arch:AVX512')]})
        problems = graph.check_instruction_sets(space)
        self.assertTrue(any('above the packaged floor' in problem for problem in problems), problems)

    def test_a_project_wide_switch_that_is_not_the_floor_is_refused_in_the_engine(self):
        space = self.workspace({graph.ENGINE_PROJECT: [('the whole project', '/arch:SSE2')]})
        problems = graph.check_instruction_sets(space)
        self.assertTrue(any('packaged floor and nothing else' in problem for problem in problems),
                        problems)

    def test_a_per_file_switch_on_a_unit_that_is_not_a_vector_unit_is_refused(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('src/image/Image.cpp', '-mfma')]})
        problems = graph.check_instruction_sets(space)
        self.assertTrue(any('only the vector translation units' in problem for problem in problems),
                        problems)

    def test_the_floor_completing_flags_on_a_vector_unit_are_permitted(self):
        """These complete the floor for a compiler whose own floor switch implies less."""
        space = self.workspace({
            graph.ENGINE_PROJECT: [
                ('../../src/AfterNightEngine/src/backends/cpu/SimdKernelTables.cpp', '-maes'),
                ('../../src/AfterNightEngine/src/backends/cpu/SimdKernelTables.cpp', '-mpclmul'),
                ('../../src/AfterNightEngine/src/backends/cpu/SimdKernelTables.cpp', '-mfma'),
                ('../../src/AfterNightEngine/src/backends/cpu/DemosaicAmazeCpu.cpp', '-maes'),
                ('../../src/AfterNightEngine/src/backends/cpu/DemosaicRcdCpu.cpp', '-mpclmul')]})
        self.assertEqual(graph.check_instruction_sets(space), [])

    def test_non_vector_stacking_and_demosaic_dispatch_units_are_still_refused(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('src/algorithms/stacking/StackMethods.cpp', '-mfma')]})
        self.assertTrue(graph.check_instruction_sets(space))
        space = self.workspace({
            graph.ENGINE_PROJECT: [
                ('src/backends/cpu/DemosaicKernelsCpu.cpp', '-maes')]})
        problems = graph.check_instruction_sets(space)
        self.assertTrue(any('only the vector translation units' in problem for problem in problems),
                        problems)

    def test_a_non_instruction_set_option_is_not_a_finding(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('src/image/Image.cpp', '/utf-8'),
                                    ('the whole project', '-fPIC')]})
        self.assertEqual(graph.check_instruction_sets(space), [])

    def test_a_generated_visual_studio_project_reports_both_scopes(self):
        root = tempfile.mkdtemp(prefix='an-graph-isa-')
        self.addCleanup(shutil.rmtree, root, True)
        path = os.path.join(root, 'P.vcxproj')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemDefinitionGroup>\n'
                '    <ClCompile>\n'
                '      <AdditionalOptions>/arch:AVX2 %(AdditionalOptions)</AdditionalOptions>\n'
                '      <EnableEnhancedInstructionSet>AdvancedVectorExtensions2'
                '</EnableEnhancedInstructionSet>\n'
                '    </ClCompile>\n'
                '  </ItemDefinitionGroup>\n'
                '  <ItemGroup>\n'
                '    <ClCompile Include="a\\b\\Simd.cpp">\n'
                '      <AdditionalOptions>-maes %(AdditionalOptions)</AdditionalOptions>\n'
                '    </ClCompile>\n'
                '  </ItemGroup>\n'
                '</Project>\n')
        target = graph.parse_vcxproj(path)
        self.assertIn(('the whole project', '/arch:AVX2'), target.compile_flags)
        self.assertIn(('the whole project', 'AdvancedVectorExtensions2'), target.compile_flags)
        self.assertIn(('a\\b\\Simd.cpp', '-maes'), target.compile_flags)

    def test_a_generated_makefile_pairs_a_per_file_flag_set_with_its_source(self):
        root = tempfile.mkdtemp(prefix='an-graph-isa-make-')
        self.addCleanup(shutil.rmtree, root, True)
        path = os.path.join(root, 'P.make')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(
                'ALL_CXXFLAGS += $(CXXFLAGS) -m64 -mavx2\n'
                'PERFILE_FLAGS_0 = $(ALL_CXXFLAGS) -maes -mpclmul\n'
                'TARGET = $(TARGETDIR)/libP.so\n'
                '$(OBJDIR)/Other.o: ../src/Other.cpp\n'
                '\t@echo "$(notdir $<)"\n'
                '\t$(SILENT) $(CXX) $(ALL_CXXFLAGS) -c "$<"\n'
                '$(OBJDIR)/Simd.o: ../src/Simd.cpp\n'
                '\t@echo "$(notdir $<)"\n'
                '\t$(SILENT) $(CXX) $(PERFILE_FLAGS_0) -c "$<"\n')
        target = graph.parse_make_project(path)
        self.assertIn(('the whole project', '-mavx2'), target.compile_flags)
        self.assertIn(('../src/Simd.cpp', '-maes'), target.compile_flags)
        self.assertNotIn(('../src/Other.cpp', '-maes'), target.compile_flags)

    def test_stack_vector_sources_can_complete_but_not_raise_the_packaged_floor(self):
        for name in ('StackAccumulatorCpu.cpp', 'StackMethodCpu.cpp', 'StackRejectionCpu.cpp', 'StackSourceCpu.cpp'):
            with self.subTest(name=name):
                source = 'src/backends/cpu/' + name
                allowed = self.workspace({graph.ENGINE_PROJECT: [(source, '-mfma'), (source, '-mf16c')]})
                self.assertEqual(graph.check_instruction_sets(allowed), [])
                raised = self.workspace({graph.ENGINE_PROJECT: [(source, '-mavx512f')]})
                self.assertTrue(graph.check_instruction_sets(raised))

    def test_every_declared_vector_source_exists_in_the_engine(self):
        """A renamed vector unit would otherwise turn the per-file rule into an allowance for none."""
        engine = os.path.join(graph.REPO_ROOT, 'src', 'AfterNightEngine', 'src')
        present = set()
        for base, _directories, files in os.walk(engine):
            present.update(files)
        for name in graph.ENGINE_VECTOR_SOURCES:
            self.assertIn(name, present)


class TargetRegistryTests(VisualStudioTreeCase):
    """The target registry is an input of the generator, and every inspected tree is held to it."""

    def test_the_registry_is_a_definition_a_tree_can_be_older_than(self):
        definitions = os.path.join(self.root, 'build', 'premake', 'premake5.lua')
        os.makedirs(os.path.dirname(definitions), exist_ok=True)
        with open(definitions, 'w') as handle:
            handle.write('-- definitions\n')
        os.utime(definitions, (1000, 1000))
        for relative in ('consumer_targets.txt', 'isolated_sources.txt'):
            registry = os.path.join(self.root, 'build', 'scripts', 'tools', relative)
            os.makedirs(os.path.dirname(registry), exist_ok=True)
            with open(registry, 'w') as handle:
                handle.write('# registry\n')
            os.utime(registry, (2000, 2000))
            self.assertEqual(graph.newest_definition_mtime(self.root), 2000)
            os.utime(registry, (500, 500))
        self.assertEqual(graph.newest_definition_mtime(self.root), 1000)

    def test_a_tree_the_registry_does_not_declare_fails(self):
        self.build_tree()
        registry = os.path.join(self.root, 'build', 'scripts', 'tools', 'consumer_targets.txt')
        os.makedirs(os.path.dirname(registry), exist_ok=True)
        with open(registry, 'w') as handle:
            handle.write('# an empty registry declares nothing\n')
        os.utime(registry, (1000, 1000))
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('vs2026-msvc: AfterNightApp is generated and consumer_targets.txt does not declare it', output)

    def test_the_shell_core_without_its_record_fails(self):
        self.build_tree()
        registry = os.path.join(self.root, 'build', 'scripts', 'tools', 'consumer_targets.txt')
        os.makedirs(os.path.dirname(registry), exist_ok=True)
        with open(registry, 'w') as handle:
            handle.write('# a registry that declares no shell core\n')
        os.utime(registry, (1000, 1000))
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('vs2026-msvc: AfterNightShellCore is generated and consumer_targets.txt does not declare it',
                      output)
        self.assertIn('vs2026-msvc: Test_ShellCore is generated and consumer_targets.txt does not declare it', output)


class NoGeneratedTreeTests(unittest.TestCase):
    def test_missing_generated_tree_fails(self):
        root = tempfile.mkdtemp(prefix='an-graph-empty-')
        self.addCleanup(shutil.rmtree, root, True)
        stream = io.StringIO()
        code = graph.main(['--repo-root', root], stream=stream)
        self.assertEqual(code, 1)
        self.assertIn('generate', stream.getvalue().lower())


class ContractionTests(unittest.TestCase):
    """Nothing contracts implicitly, and the engine states the setting rather than inheriting it.

    This is the check whose absence is hardest to notice. Contraction moves the scalar reference and
    the kernels it judges together, so every differential test still passes and only the meaning of
    "matches the reference" has changed. A command line is the only place it shows.
    """

    def workspace(self, targets, label='tree/main'):
        space = graph.Workspace(label, graph.EXPECTED_MAIN_PROJECTS)
        for name, flags in targets.items():
            target = graph.Target(name, name + '.vcxproj')
            target.compile_flags = list(flags)
            space.targets[name] = target
        return space

    def test_a_stated_contraction_setting_is_what_a_clean_gnu_tree_looks_like(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('the whole project', '-mavx2'),
                                    ('the whole project', '-ffp-contract=off'),
                                    ('src/backends/cpu/SimdKernelTables.cpp', '-mfma')]})
        self.assertEqual(graph.check_contraction('make-linux', space), [])

    def test_the_clang_cl_spelling_counts_as_stating_it(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('the whole project', '/arch:AVX2'),
                                    ('the whole project', '/clang:-ffp-contract=off')]})
        self.assertEqual(graph.check_contraction('vs2026-clang', space), [])

    def test_an_engine_that_states_nothing_is_refused_where_a_switch_exists(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('the whole project', '-mavx2'),
                                    ('src/backends/cpu/SimdKernelTables.cpp', '-mfma')]})
        problems = graph.check_contraction('make-linux', space)
        self.assertTrue(any('states no contraction setting' in problem for problem in problems),
                        problems)

    def test_the_microsoft_tree_needs_no_switch_because_it_has_none(self):
        """`/fp:contract-` is rejected as an unknown option, so the rule there is the prohibition."""
        space = self.workspace({graph.ENGINE_PROJECT: [('the whole project', '/arch:AVX2')]})
        self.assertEqual(graph.check_contraction('vs2026-msvc', space), [])

    def test_turning_contraction_on_is_refused_even_on_the_flagless_tree(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('the whole project', '/arch:AVX2'),
                                    ('the whole project', '/fp:contract')]})
        problems = graph.check_contraction('vs2026-msvc', space)
        self.assertTrue(any('implicit fused multiply-add' in problem for problem in problems),
                        problems)

    def test_fast_math_is_refused_in_the_shipping_engine_too(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: [('the whole project', '-ffp-contract=off'), ('the whole project', '-ffast-math')]})
        problems = graph.check_contraction('make-linux', space)
        self.assertTrue(any('-ffast-math' in problem and graph.ENGINE_PROJECT in problem for problem in problems),
                        problems)

    def test_a_per_file_contraction_setting_does_not_satisfy_the_project_rule(self):
        """One unit stating it leaves every other unit on the default, which is what went wrong."""
        space = self.workspace({
            graph.ENGINE_PROJECT: [
                ('the whole project', '-mavx2'),
                ('src/backends/cpu/SimdKernelTables.cpp', '-ffp-contract=off')]})
        problems = graph.check_contraction('make-linux', space)
        self.assertTrue(any('states no contraction setting' in problem for problem in problems),
                        problems)

    def test_a_workspace_without_the_engine_asks_nothing_of_it(self):
        space = self.workspace({'AfterNightControls': [('the whole project', '-mavx2')]},
                               label='tree/tests')
        self.assertEqual(graph.check_contraction('make-linux', space), [])


class PrecompiledHeaderTests(unittest.TestCase):
    """Nothing on this engine's side of the workspace compiles through a precompiled header.

    A precompiled header makes everything it holds transitive in every translation unit of the
    target, so its include graph is not what its include lines say. A precompiled header and a forced
    include are the same fact in a build: a header no source asked for, in every unit of the target.
    """

    def workspace(self, targets, label='tree/main', expected=graph.EXPECTED_MAIN_PROJECTS):
        space = graph.Workspace(label, expected)
        for name, (precompiled, forced) in targets.items():
            target = graph.Target(name, name + '.vcxproj')
            target.precompiled_headers = list(precompiled)
            target.forced_includes = list(forced)
            space.targets[name] = target
        return space

    def test_a_tree_where_only_a_product_library_has_one_passes(self):
        space = self.workspace({
            graph.ENGINE_PROJECT: ((), ()),
            'AfterNightControls': ((('the whole project', 'Use stdafx.hpp'),),
                                   (('the whole project', 'stdafx.hpp'),)),
        })
        self.assertEqual(graph.check_precompiled_headers(space), [])

    def test_the_engine_compiling_through_one_is_refused(self):
        space = self.workspace({graph.ENGINE_PROJECT: ((('the whole project', 'Use pch.hpp'),), ())})
        problems = graph.check_precompiled_headers(space)
        self.assertTrue(any('compiles through a precompiled header' in problem for problem in problems),
                        problems)
        self.assertTrue(any(graph.ENGINE_PROJECT in problem for problem in problems), problems)

    def test_an_engine_suite_compiling_through_one_is_refused(self):
        space = self.workspace({'Test_EngineCore': ((('the whole project', 'Create pch.hpp'),), ())},
                               label='tree/tests', expected=graph.EXPECTED_TEST_PROJECTS)
        self.assertTrue(graph.check_precompiled_headers(space))

    def test_an_optional_adapter_compiling_through_one_is_refused(self):
        space = self.workspace({
            'AfterNightEnginePythonAdapter': ((('the whole project', 'Use pch.hpp'),), ())})
        self.assertTrue(graph.check_precompiled_headers(space))

    def test_a_forced_include_is_refused_on_its_own(self):
        space = self.workspace({graph.ENGINE_PROJECT: ((), (('the whole project', 'framework.h'),))})
        problems = graph.check_precompiled_headers(space)
        self.assertTrue(any('forces an include' in problem for problem in problems), problems)
        self.assertTrue(any('framework.h' in problem for problem in problems), problems)

    def test_a_per_file_precompiled_header_is_refused_too(self):
        space = self.workspace({graph.ENGINE_PROJECT: ((('src/core/Version.cpp', 'Use pch.hpp'),), ())})
        problems = graph.check_precompiled_headers(space)
        self.assertTrue(any('src/core/Version.cpp' in problem for problem in problems), problems)

    def test_the_visual_studio_setting_that_means_none_is_not_a_finding(self):
        """Premake emits `NotUsing` for every file of a project without one; that is the clean tree."""
        root = tempfile.mkdtemp(prefix='an-graph-pch-vs-')
        self.addCleanup(shutil.rmtree, root, True)
        path = os.path.join(root, 'AfterNightEngine.vcxproj')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemDefinitionGroup>\n'
                '    <ClCompile>\n'
                '      <PrecompiledHeader>NotUsing</PrecompiledHeader>\n'
                '    </ClCompile>\n'
                '  </ItemDefinitionGroup>\n'
                '</Project>\n')
        target = graph.parse_vcxproj(path)
        self.assertEqual(target.precompiled_headers, [])
        self.assertEqual(target.forced_includes, [])

    def test_a_generated_visual_studio_project_reports_both_scopes(self):
        root = tempfile.mkdtemp(prefix='an-graph-pch-vs-used-')
        self.addCleanup(shutil.rmtree, root, True)
        path = os.path.join(root, 'AfterNightEngine.vcxproj')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
                '  <ItemDefinitionGroup>\n'
                '    <ClCompile>\n'
                '      <PrecompiledHeader>Use</PrecompiledHeader>\n'
                '      <PrecompiledHeaderFile>pch.hpp</PrecompiledHeaderFile>\n'
                '      <ForcedIncludeFiles>framework.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>\n'
                '    </ClCompile>\n'
                '  </ItemDefinitionGroup>\n'
                '  <ItemGroup>\n'
                '    <ClCompile Include="a\\b\\pch.cpp">\n'
                '      <PrecompiledHeader>Create</PrecompiledHeader>\n'
                '    </ClCompile>\n'
                '  </ItemGroup>\n'
                '</Project>\n')
        target = graph.parse_vcxproj(path)
        self.assertIn(('the whole project', 'Use pch.hpp'), target.precompiled_headers)
        self.assertIn(('a\\b\\pch.cpp', 'Create pch.hpp'), target.precompiled_headers)
        self.assertIn(('the whole project', 'framework.h'), target.forced_includes)

    def test_a_generated_makefile_reports_its_precompiled_header_and_forced_include(self):
        root = tempfile.mkdtemp(prefix='an-graph-pch-make-')
        self.addCleanup(shutil.rmtree, root, True)
        path = os.path.join(root, 'AfterNightEngine.make')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(
                'PCH = ../../src/AfterNightEngine/pch.hpp\n'
                'PCH_PLACEHOLDER = $(OBJDIR)/$(notdir $(PCH))\n'
                'GCH = $(PCH_PLACEHOLDER).gch\n'
                'FORCE_INCLUDE += -include framework.h\n'
                'ALL_CXXFLAGS += $(CXXFLAGS) -m64\n'
                '$(OBJDIR)/Thing.o: ../../src/AfterNightEngine/src/Thing.cpp\n'
                '\t$(SILENT) $(CXX) -include $(PCH_PLACEHOLDER) $(ALL_CXXFLAGS) -c "$<"\n'
                '-include $(OBJECTS:%.o=%.d)\n')
        target = graph.parse_make_project(path)
        self.assertIn(('the whole project', '../../src/AfterNightEngine/pch.hpp'),
                      target.precompiled_headers)
        self.assertIn(('the whole project', '-include framework.h'), target.forced_includes)

    def test_a_generated_makefile_without_one_reports_neither(self):
        """An empty `FORCE_INCLUDE +=` and the unused `.gch` rule template are what premake always
        emits; only a value means the target has one."""
        root = tempfile.mkdtemp(prefix='an-graph-pch-make-clean-')
        self.addCleanup(shutil.rmtree, root, True)
        path = os.path.join(root, 'AfterNightEngine.make')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(
                'FORCE_INCLUDE +=\n'
                'ALL_CXXFLAGS += $(CXXFLAGS) -m64\n'
                '$(PCH_PLACEHOLDER).gch: $(PCH)\n'
                '\t$(SILENT) $(CXX) -x c++-header $(ALL_CXXFLAGS) -o "$@" -c "$<"\n'
                '$(OBJDIR)/Thing.o: ../../src/AfterNightEngine/src/Thing.cpp\n'
                '\t$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -c "$<"\n'
                '-include $(OBJECTS:%.o=%.d)\n'
                'ifneq (,$(PCH))\n'
                '  -include $(PCH_PLACEHOLDER).d\n'
                'endif\n')
        target = graph.parse_make_project(path)
        self.assertEqual(target.precompiled_headers, [])
        self.assertEqual(target.forced_includes, [])

    def test_a_generated_makefiles_own_name_among_its_link_prerequisites_is_not_a_link(self):
        """Every binary depends on its own makefile, so that a changed link line relinks it; the makefile is a
        prerequisite of the link and nothing the link reads."""
        root = tempfile.mkdtemp(prefix='an-graph-own-make-')
        self.addCleanup(shutil.rmtree, root, True)
        path = os.path.join(root, 'Fuzz_EngineCodec.make')
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(
                'LDDEPS += ../../bin/clang/release/libEngineBenchCommon.a\n'
                'LDDEPS += Fuzz_EngineCodec.make\n'
                'TARGET = $(TARGETDIR)/Fuzz_EngineCodec\n')
        target = graph.parse_make_project(path)
        self.assertIn('../../bin/clang/release/libEngineBenchCommon.a', target.links)
        self.assertIn('../../bin/clang/release/libEngineBenchCommon.a', target.link_dependencies)
        self.assertNotIn('Fuzz_EngineCodec.make', target.links)


if __name__ == '__main__':
    unittest.main(verbosity=2)

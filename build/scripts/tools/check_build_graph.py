# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Validate the build graph of the engine and of everything generated beside it.

AfterNightEngine is an ordinary project in the main AfterNight workspace, and the product's
targets link it. That arrangement only stays safe while three properties hold, and none of them is
visible in a build log:

1. every project the repository generates is still generated. A target must not be bought by
   quietly dropping another one from the workspace;
2. the engine's own targets - its suites, fuzz targets and benchmark runner - link nothing of
   AfterNight's but the engine, so what they test and measure is the engine and not the product
   built on it; and
3. the engine links nothing else of AfterNight's, includes no header out of its tree, and compiles
   no source of another target, so it stays usable without the product.

An optional adapter is a fourth kind of target and carries the same properties from the other
side: `AfterNightEnginePythonAdapter` and `AfterNightEngineGmicAdapter` link the engine's
published surface, the engine links neither them nor the libraries they adapt, and nothing links an
adapter but its own suite and the consumers named here as having asked for it. That is what
"optional" means in a build rather than in prose - a consumer that did not ask for Python does not
acquire a Python dependency by linking this engine, and the same holds for G'MIC.

An opt-in GPU backend is the same kind of target with the opposite default. It is generated only when
a build asks for it by name, so a default tree has none and that absence is not a finding; when one is
generated it carries an adapter's two link rules, it is generated together with its suite or not at
all, and it is the only target in either workspace whose include directories, library directories or
link line reach a GPU SDK. That last rule is the build-graph half of "the proof leaves no dependency
behind": an engine, product target or suite that started including a CUDA toolkit or linking an
OpenCL loader would have made a GPU SDK part of a build that never asked for one.

A fifth property is cheap to check here and expensive to discover later: no two projects may
produce the same output file. Every project writes into one `bin/<toolchain>/<config>` directory, so
a name collision would make a wildcard copy rule overwrite one binary with another.

The fifth has a second half that only two trees can show: no two *trees* may write to one object
directory, binary or symbol file either. The same project exists in every generated tree, and the
toolchain each was generated for is the only thing that distinguishes their output, so one tree
built after another silently replaces it. A shared object directory is the worse half, because
clang-cl matches MSVC's name mangling and a binary holding objects from both links instead of
failing.

A sixth is invisible everywhere else: no project may be compiled at an instruction set above the
packaged floor. The engine has kernels for more than one, and they exist as per-target functions
the vector abstraction emits inside an ordinary object rather than as a whole translation unit
built with a raised switch - because a raised switch frees every function in that unit to use
instructions the host running the dispatch may not have. Only the generated projects say which
of the two arrangements a build actually has.

A seventh is invisible in the same way and fails more quietly than any of them: no project may
leave implicit fused multiply-add on. Contraction belongs where a kernel writes `MulAdd` and the scalar
reference for that kernel writes `std::fma`, and nowhere else - a compiler free to fuse anything it
likes will fuse the reference as readily as the kernel that reference exists to judge, so the two
still agree and only the oracle has moved. Most of the engine cannot emit the instruction at all,
because the packaged floor does not imply it on a GNU-style driver; the vector unit is compiled with
it and so needs the setting stated rather than inherited.

An eighth decides whether any of the engine's include rules mean what they say: no target on the
engine's side may compile through a precompiled header or a forced include. Either puts every header
it names into every translation unit of a target without an include line saying so, so the include
graph stops being what the include lines say. The engine's graph is read out of those lines by
`check_include_layers.py` and budgeted from them by `check_budgets.py`. The product targets keep
their precompiled headers; this says nothing about them.

A ninth holds the platform library to what makes it shareable: `AfterNightPlatform` - the logger, its
configuration and the crash handler every product target uses - reaches no engine, no Qt and no other
product library, and compiles only its own sources; its suite reaches the library and nothing else of
AfterNight's; and the Lite host and the protocol library, which the licensing boundary keeps engine-free,
do not reach the platform library either.

A tenth holds the product's own libraries to their order: `AfterNightControls` reaches neither
`AfterNightProcesses` nor the application or the full host, and `AfterNightProcesses` reaches neither of those
two. A library that reached upward could not be built or tested without the target above it.

An eleventh holds every inspected tree to the target registry, `build/scripts/tools/consumer_targets.txt`, which
states per target whether it reaches the engine, what it links and what it may compile: every generated project is
declared and every declared one generated, and no target includes, compiles or links the engine when its class
excludes it. The rules are `consumer_targets.py`'s own; this applies them to each current tree, Visual Studio ones
included, because that tool reads only the first. The registry and the source lists it names are inputs of the
generator, so a tree older than either is stale like one older than the definitions.

A twelfth holds the consumer runtime to what lets every consumer of the engine share it: `AfterNightConsumerRuntime`
- the engine session, operation scope, image asset and memory allowances - reaches the engine and the platform
library and nothing else of AfterNight's, no Qt and no Python, and compiles only its own sources; its suite adds the
runtime itself and nothing else; and neither the engine, the platform library, the Lite host nor the protocol library
reaches it. The command-line tool and the runtime backend load it without Qt.

A thirteenth holds the command-line tool to what lets it run where no product is installed: `AfterNightCLI` and its
suite reach no Qt, no Python and no product library - not Controls, Processes, the application, either host, the
protocol library, the style plugin, an optional adapter or the GPU backend - and the tool compiles only its own
sources, its suite only the tool's, its own and the engine-neutral fixture layer's. Whether they reach the engine is
the registry's class rule, which the eleventh applies: the tool is class `engine`, so the consumer runtime, the engine
and the platform library are its whole first-party closure.

This reads the *generated* projects rather than the Premake sources, because the generated graph is
what the compiler and linker actually obey.

Usage:
    python build/scripts/tools/check_build_graph.py [--repo-root <dir>]

Exits 0 when every property holds, 1 otherwise, printing one line per problem.

Both generator front ends are supported: Visual Studio (`build/vs2026-msvc`, `build/vs2026-clang`)
and GNU Make (`build/make-linux`). A generated tree that exists but yields no projects is reported
as a problem rather than skipped, so a front end whose assumptions stop matching its generator
fails loudly instead of passing vacuously.

Tests: build/scripts/tools/check_build_graph_test.py
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ElementTree

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

# The projects the repository generates beside the engine, which `check_engine_present` asks for on its own.
# Losing one is a regression in the product graph, so this list is a floor, not an inventory: a new project is
# allowed, a missing one is not.
EXPECTED_MAIN_PROJECTS = (
    'AfterNightApp',
    'AfterNightCLI',
    'AfterNightControls',
    'AfterNightExtensionHost',
    'AfterNightExtensionHostLite',
    'AfterNightExtensionProtocol',
    'AfterNightExtensionRuntimeBackend',
    'AfterNightPlatform',
    'AfterNightProcesses',
    'AfterNightStylePlugin',
)

EXPECTED_TEST_PROJECTS = (
    'AfterNightEngineBench',
    'EngineBenchCommon',
    'Test_AfterNightApp',
    'Test_EngineBenchCommon',
    'Test_ExtensionHostNative',
    'Test_ExtensionRuntimeBackend',
    'Test_FixtureNeutral',
    'Test_FixtureEngine',
    'Test_ImageIntegration',
    'Test_ImageProcessControl',
    'Test_ImageRTPreviewControl',
    'Test_ImageViewControl',
    'Test_MainDashboard',
    'Test_MaskDrawing',
    'Test_Persistence',
    'Test_Platform',
    'Test_ProcessFramework',
    'Test_EngineConsumerProbes',
    'Test_EngineCalibration',
    'Test_EngineCore',
    'Test_EngineFaults',
    'Test_EngineFixtureBridge',
    'Test_EngineGmicAdapter',
    'Test_EngineImage',
    'Test_EngineImageOps',
    'Test_EngineInterop',
    'Test_EngineKernels',
    'Test_EngineLibraryParity',
    'Test_EnginePublicHeaders',
    'Test_EnginePythonAdapter',
    'Test_EngineRuntime',
    'Test_EngineStars',
    'Test_SystemInformationPanel',
    'Test_ThemeSystem',
    'Test_Widgets',
)

ENGINE_PROJECT = 'AfterNightEngine'

# The engine, as every target can reach it. Whether a product target may reach it is its registry class's to decide,
# so the rules below that are about a target's other first-party reach leave it to that rule.
ENGINE_PROJECTS = (
    ENGINE_PROJECT,
)

# The platform library: the logger, its configuration and the crash handler, which every product target shares and
# the engine does not own. It must reach no engine, no Qt and no other product library, because the command-line tool
# and the runtime backend load it without Qt. Its suite is held to the same closure plus the library itself.
PLATFORM_PROJECT = 'AfterNightPlatform'
PLATFORM_SUITE = 'Test_Platform'
PLATFORM_SOURCE_DIR = ('src', 'afternightplatform')

# The style plugin Qt loads by name, and the one library it may reach the engine through: the Controls library whose
# theme it draws.
STYLE_PLUGIN_PROJECT = 'AfterNightStylePlugin'
STYLE_PLUGIN_THROUGH = 'AfterNightControls'

# The engine-free extension boundary, which the licensing boundary keeps free of every AfterNight library but
# the protocol - the platform library included.
PLATFORM_FREE_PROJECTS = ('AfterNightExtensionHostLite', 'AfterNightExtensionProtocol')

# The consumer runtime and its suite, and what each may link of this workspace. The runtime is built on the engine's
# published surface and logs through the platform library; its suite adds the runtime. The projects that must not
# reach it are the engine and the platform library below it, and the engine-free extension boundary.
CONSUMER_RUNTIME_PROJECT = 'AfterNightConsumerRuntime'
CONSUMER_RUNTIME_SUITE = 'Test_ConsumerRuntime'
CONSUMER_RUNTIME_SOURCE_DIR = ('src', 'afternightconsumerruntime')
CONSUMER_RUNTIME_LINKS = (ENGINE_PROJECT, PLATFORM_PROJECT)
CONSUMER_RUNTIME_FREE_PROJECTS = ENGINE_PROJECTS + (PLATFORM_PROJECT,) + PLATFORM_FREE_PROJECTS

# The command-line tool and its suite. What they may reach of AfterNight's beside the engine their registry class
# reaches: the platform library and the consumer runtime, which is not a project this list names. The suite compiles
# the tool's units, its own and the engine-neutral fixture layer's.
CLI_PROJECT = 'AfterNightCLI'
CLI_SUITE = 'Test_AfterNightCLI'
CLI_SOURCE_DIR = ('src', 'afternightcli')
CLI_SUITE_SOURCE_DIRS = (CLI_SOURCE_DIR, ('src', 'afternighttests', 'test_afternightcli'),
                         ('src', 'afternighttests', 'fixtures', 'common'))

# The runtime backend and its driver. The backend brings the interpreter its host owns, so Python is what it may
# reach beside the platform library and the consumer runtime, which is not a project this list names; the driver adds
# the backend's own headers, to read the session capsule as the full host does, and builds after the backend, whose
# module it loads. Neither reaches Qt: the backend runs in the full host's process without bringing a second copy.
RUNTIME_BACKEND_PROJECT = 'AfterNightExtensionRuntimeBackend'
RUNTIME_BACKEND_SUITE = 'Test_ExtensionRuntimeBackend'
RUNTIME_BACKEND_SOURCE_DIR = ('src', 'afternightextensionruntimebackend')
RUNTIME_BACKEND_SUITE_SOURCE_DIRS = (('src', 'afternighttests', 'test_extensionruntimebackend'),)

# The product's two Qt libraries and the targets above each. Controls is the reusable layer Processes is built on,
# and the application and the full host compose both, so a library that reached upward would make its own
# consumer a dependency: it could no longer be built or tested before the target above it.
PRODUCT_LAYERS = (
    ('AfterNightControls', ('AfterNightProcesses', 'AfterNightApp', 'AfterNightExtensionHost')),
    ('AfterNightProcesses', ('AfterNightApp', 'AfterNightExtensionHost')),
)

# The optional adapters, and the one suite each is allowed to be linked by. An adapter exists so a
# consumer that wants the library it adapts can ask for it, which means the interesting property is
# who *else* reaches it: the engine must not, no product target may, and no other engine
# suite may either - otherwise "optional" would be a word in a document rather than a fact about a
# link line. The value is the suite that tests the adapter, which is the one target that has to link
# it.
OPTIONAL_ADAPTER_SUITES = {
    'AfterNightEngineGmicAdapter': 'Test_EngineGmicAdapter',
    'AfterNightEnginePythonAdapter': 'Test_EnginePythonAdapter',
}

# The targets besides its own suite that link an optional adapter because they asked for the library
# it adapts, each named here on purpose so that a convenience link anywhere else still fails. The
# product's processes library runs its G'MIC processes through the G'MIC adapter; the suite that holds
# those processes reaches the adapter only through the library it links, so it is not named. A build
# without the library still links the adapter, which then refuses every call by name.
OPTIONAL_ADAPTER_CONSUMERS = {
    'AfterNightEngineGmicAdapter': ('AfterNightProcesses',),
}

# The opt-in GPU backends, and the one suite each is allowed to be linked by. Unlike an adapter, a
# backend is absent unless its build asked for it, so a tree without one is the default rather than a
# regression; the rules below apply to whichever tree does contain one.
OPTIONAL_BACKEND_SUITES = {
    'AfterNightEngineOpenClBackend': 'Test_EngineOpenClBackend',
}

# How a GPU SDK shows up in a generated project. A directory is recognised by a whole path component -
# the opt-in backend's own vcpkg triplet, or the NVIDIA toolkit's install tree - and a link by the
# library it names. Whole components, for the reason `references_directory` gives: a substring test
# would find `cuda` inside a name that has nothing to do with it.
GPU_SDK_DIRECTORY_COMPONENTS = ('x64-windows-opencl', 'nvidia gpu computing toolkit', 'cuda')
GPU_SDK_LIBRARIES = ('opencl', 'cudart', 'nvrtc')

ENGINE_SOURCE_DIR = ('src', 'afternightengine')

# The engine's private implementation tree. An engine *test* reaching into it stops testing the
# published surface and starts testing an implementation detail, which is how a public API ends up
# with no coverage of its own while every test passes.
ENGINE_PRIVATE_DIR = ('src', 'afternightengine', 'src')

# This engine's benchmark runner, the one target of the benchmark layer that is held to this engine's rules.
BENCH_RUNNER = 'AfterNightEngineBench'

# Suites of the test workspace a prefix test would take for this engine's that hold none of it: the
# engine-neutral benchmark layer's.
NEUTRAL_SUITES = ('Test_EngineBenchCommon',)

# Which engine each benchmark runner must link, keyed by the runner's name. A runner that linked no
# engine would measure nothing while still producing a result file; one that linked another would
# measure the wrong engine and report it under this one's name. Both are invisible in the output, so
# they are checked here.
RUNNER_ENGINES = (
    (BENCH_RUNNER, ENGINE_PROJECT),
)

# Directories a generator may have written, relative to the repository root. Each entry is the
# workspace directory; its `tests` subdirectory, when present, holds the test workspace.
#
# The CodeLite workspace the Linux generator also emits is deliberately absent: it is an editor
# convenience, no build script consumes it, and it comes from the same Premake definitions as the
# makefiles, so a violation could not appear there without appearing in a tree checked here.
GENERATED_TREES = (
    ('vs2026-msvc', os.path.join('build', 'vs2026-msvc'), 'vs'),
    ('vs2026-clang', os.path.join('build', 'vs2026-clang'), 'vs'),
    ('make-linux', os.path.join('build', 'make-linux'), 'make'),
)

# Where the definitions that produce every tree live. A tree older than these describes a graph
# that is no longer the one being built.
PREMAKE_DIR = os.path.join('build', 'premake')
GENERATION_STAMP = '.premake-generated'
# The registries the generator reads besides the definitions: which targets exist, and what an isolated one compiles.
GENERATOR_REGISTRIES = (os.path.join('build', 'scripts', 'tools', 'consumer_targets.txt'),
                        os.path.join('build', 'scripts', 'tools', 'isolated_sources.txt'))

MSBUILD_NAMESPACE = '{http://schemas.microsoft.com/developer/msbuild/2003}'


def is_engine_target(name):
    """True for a target that is allowed - and required - to depend on the engine.

    Recognised by name so that a new suite or fuzz target needs no change here: the engine itself,
    any `Test_Engine*` suite but the benchmark layer's, any `Fuzz_Engine*` target, and the engine's
    benchmark runner. An optional adapter or GPU backend is one
    too - it links the engine's published surface on purpose, and what is checked about it is the
    other direction, in `check_optional_adapters` and `check_optional_backends`.

    A fuzz target is named apart from the suites on purpose. It is not a gtest program and must not
    be swept up by a runner that executes every `Test_*` binary it finds: it runs until its budget
    is spent rather than until its cases are done.
    """
    return (name == ENGINE_PROJECT or name in OPTIONAL_ADAPTER_SUITES or name in OPTIONAL_BACKEND_SUITES
            or (name.startswith('Test_Engine') and name not in NEUTRAL_SUITES) or name.startswith('Fuzz_Engine')
            or name == BENCH_RUNNER)


class Target:
    """One generated project: what it links, what it includes, and what it compiles."""

    def __init__(self, name, location):
        self.name = name
        self.location = location
        self.links = []
        self.include_dirs = []
        self.library_dirs = []
        self.sources = []
        self.build_dependencies = []
        self.link_dependencies = []
        self.outputs = []
        self.artifacts = []
        self.compile_flags = []
        # Two ways a build puts a header into a translation unit that never asked for one, each as
        # (scope, what), where scope is a source path for a per-file setting and "the whole
        # project" otherwise. Both are empty for a target that compiles what its sources include.
        self.precompiled_headers = []
        self.forced_includes = []

    def add_artifact(self, kind, path):
        path = path.strip()
        if path:
            self.artifacts.append((kind, path))

    def __repr__(self):  # pragma: no cover - diagnostic aid only
        return 'Target(%s)' % self.name


class Workspace:
    """A generated workspace: a label for messages plus its targets by name."""

    def __init__(self, label, expected, kind='vs'):
        self.label = label
        self.expected = expected
        self.kind = kind
        self.targets = {}
        # Per generated solution, every project it lists: {solution file name: {project name: (Target or None,
        # [the projects this solution builds it after])}}. A project generated into another workspace's
        # directory is read from there; its Target is None only when its file does not exist.
        self.solutions = {}


def path_components(text):
    """Lowercased path components of `text`, with separators of either platform and no `.`/`..`."""
    parts = re.split(r'[\\/]+', text.strip())
    return tuple(part.lower() for part in parts if part not in ('', '.', '..'))


def references_directory(text, directory):
    """True when `text` is, or descends into, the directory named by the `directory` components.

    Compares whole path components. A substring test would report `src/AfterNightEngineGmicAdapter`
    as a reference to `src/AfterNightEngine`, which is exactly the mistake this checker exists to
    avoid making about itself.
    """
    components = path_components(text)
    span = len(directory)
    for index in range(len(components) - span + 1):
        if components[index:index + span] == directory:
            return True
    return False


def link_base_name(entry):
    """Reduce one link entry to the project name it refers to, or '' when it names no project.

    Accepts what either linker command line carries: `AfterNightPlatform.lib`, a path to one,
    `-lAfterNightPlatform`, and the `_d` debug spelling of each.
    """
    text = entry.strip()
    if not text:
        return ''
    if text.startswith('-l'):
        text = text[2:]
    text = re.split(r'[\\/]', text)[-1]
    for suffix in ('.lib', '.a', '.so'):
        if text.lower().endswith(suffix):
            text = text[: -len(suffix)]
    if text.lower().startswith('lib'):
        text = text[3:]
    if text.endswith('_d'):
        text = text[:-2]
    return text


def links_project(target, project):
    """True when `target` links the import or shared library produced by `project`."""
    return any(link_base_name(entry).lower() == project.lower() for entry in target.links)


# --- Visual Studio front end -----------------------------------------------------------------


def _element_texts(root, tag):
    for element in root.iter(MSBUILD_NAMESPACE + tag):
        if element.text:
            yield element.text


def parse_vcxproj(path):
    """Read one generated `.vcxproj` into a Target."""
    name = os.path.splitext(os.path.basename(path))[0]
    target = Target(name, path)
    try:
        root = ElementTree.parse(path).getroot()
    except ElementTree.ParseError as error:
        target.outputs.append(name)
        target.add_artifact('target binary name', name)
        target.sources.append('<unparsable: %s>' % error)
        return target

    for text in _element_texts(root, 'AdditionalDependencies'):
        target.links.extend(entry for entry in text.split(';') if not entry.startswith('%('))
    for text in _element_texts(root, 'AdditionalIncludeDirectories'):
        target.include_dirs.extend(entry for entry in text.split(';') if not entry.startswith('%('))
    for text in _element_texts(root, 'AdditionalLibraryDirectories'):
        target.library_dirs.extend(entry for entry in text.split(';') if not entry.startswith('%('))
    for text in _element_texts(root, 'AdditionalOptions'):
        target.links.extend(entry for entry in text.split() if entry.startswith('-l'))
    link_outputs = {}
    for group in root.iter(MSBUILD_NAMESPACE + 'ItemDefinitionGroup'):
        condition = group.get('Condition') or ''
        values = link_outputs.setdefault(condition, {})
        for tag, kind in (
                ('ImportLibrary', 'import library'),
                ('ProgramDatabaseFile', 'symbol file'),
                ('ProgramDataBaseFile', 'symbol file')):
            element = group.find('.//' + MSBUILD_NAMESPACE + tag)
            if element is not None and element.text and element.text.strip():
                values[tag] = element.text.strip()
                target.add_artifact(kind, element.text)

    for group in root.iter(MSBUILD_NAMESPACE + 'PropertyGroup'):
        values = {}
        for element in group:
            tag = element.tag.rsplit('}', 1)[-1]
            if element.text and element.text.strip():
                values[tag] = element.text.strip()
        intermediate = values.get('IntDir', '')
        if intermediate:
            target.add_artifact('intermediate directory', intermediate)

        target_name = values.get('TargetName', '')
        if not target_name:
            continue
        target.outputs.append(target_name)
        output_directory = values.get('OutDir', '')
        extension = values.get('TargetExt', '')
        if output_directory:
            binary = output_directory.rstrip('/\\') + os.sep + target_name + extension
            target.add_artifact('target binary', binary)
            condition = group.get('Condition') or ''
            configured = link_outputs.get(condition, {})
            has_explicit_pdb = ('ProgramDatabaseFile' in configured or
                                'ProgramDataBaseFile' in configured)
            if extension.lower() in ('.dll', '.exe') and not has_explicit_pdb:
                target.add_artifact(
                    'symbol file',
                    output_directory.rstrip('/\\') + os.sep + target_name + '.pdb')
        else:
            target.add_artifact('target binary name', target_name + extension)
    # The precompiled header a per-file `Create` or `Use` refers to is stated once, at project
    # scope, so it is read before the per-file settings that inherit it.
    project_precompiled_file = ''
    for group in root.iter(MSBUILD_NAMESPACE + 'ItemDefinitionGroup'):
        element = group.find('.//' + MSBUILD_NAMESPACE + 'PrecompiledHeaderFile')
        if element is not None and element.text and element.text.strip():
            project_precompiled_file = element.text.strip()

    for element in root.iter(MSBUILD_NAMESPACE + 'ClCompile'):
        include = element.get('Include')
        if include:
            target.sources.append(include)
        # A compile option is project-wide when it sits in an item definition and per-file
        # when it sits on the file's own item, and the difference is the whole point: one
        # raises a translation unit and the other raises a project.
        scope = include if include else 'the whole project'
        setting = element.find(MSBUILD_NAMESPACE + 'PrecompiledHeader')
        if setting is not None and setting.text and setting.text.strip() not in ('', 'NotUsing'):
            named = element.find(MSBUILD_NAMESPACE + 'PrecompiledHeaderFile')
            spelled = named.text.strip() if named is not None and named.text else project_precompiled_file
            target.precompiled_headers.append(
                (scope, (setting.text.strip() + ' ' + spelled).strip()))
        forced = element.find(MSBUILD_NAMESPACE + 'ForcedIncludeFiles')
        if forced is not None and forced.text:
            for entry in forced.text.split(';'):
                if entry.strip() and not entry.strip().startswith('%('):
                    target.forced_includes.append((scope, entry.strip()))
        for tag in ('AdditionalOptions', 'EnableEnhancedInstructionSet'):
            option = element.find(MSBUILD_NAMESPACE + tag)
            if option is None or not option.text:
                continue
            for flag in option.text.split():
                if not flag.startswith('%('):
                    target.compile_flags.append((scope, flag))
    if not target.outputs:
        target.outputs.append(name)
        target.add_artifact('target binary name', name)
    return target


def parse_slnx_entries(path):
    """Read a generated `.slnx` into {project name: (project file path, [dependency project names])}.

    The project file path is resolved against the solution's directory, which is what tells a project
    of this workspace from one the solution lists from another.
    """
    entries = {}
    try:
        root = ElementTree.parse(path).getroot()
    except (ElementTree.ParseError, OSError):
        return entries
    directory = os.path.dirname(os.path.abspath(path))
    for project in root.iter('Project'):
        project_path = (project.get('Path') or '').replace('\\', os.sep).replace('/', os.sep)
        name = os.path.splitext(os.path.basename(project_path))[0]
        if not name:
            continue
        names = []
        for dependency in project.iter('BuildDependency'):
            dependency_path = (dependency.get('Project') or '').replace('\\', os.sep).replace('/', os.sep)
            dependency_name = os.path.splitext(os.path.basename(dependency_path))[0]
            if dependency_name:
                names.append(dependency_name)
        entries[name] = (os.path.normpath(os.path.join(directory, project_path)), names)
    return entries


def parse_slnx(path):
    """Read a generated `.slnx` into {project name: [dependency project names]}."""
    return {name: dependencies for name, (_location, dependencies) in parse_slnx_entries(path).items()}


def load_vs_workspace(directory, label, expected):
    workspace = Workspace(label, expected, kind='vs')
    if not os.path.isdir(directory):
        return None
    for entry in sorted(os.listdir(directory)):
        if entry.lower().endswith('.vcxproj'):
            target = parse_vcxproj(os.path.join(directory, entry))
            workspace.targets[target.name] = target
    for entry in sorted(os.listdir(directory)):
        if entry.lower().endswith('.slnx'):
            listed = {}
            for name, (location, dependencies) in parse_slnx_entries(os.path.join(directory, entry)).items():
                if name in workspace.targets:
                    workspace.targets[name].build_dependencies.extend(dependencies)
                    listed[name] = (workspace.targets[name], dependencies)
                elif os.path.isfile(location):
                    listed[name] = (parse_vcxproj(location), dependencies)
                else:
                    listed[name] = (None, dependencies)
            workspace.solutions[entry] = listed
    return workspace


# --- GNU Make front end ----------------------------------------------------------------------

_MAKE_ASSIGNMENT = re.compile(r'^\s*(?P<name>[A-Z_][A-Z_0-9]*)\s*(?:\+=|:=|=)\s*(?P<value>.*)$')
_MAKE_RULE_SOURCE = re.compile(r'^\s*\$\(OBJDIR\)/\S+\s*:\s*(?P<source>\S+)')
_MAKE_PERFILE_USE = re.compile(r'\$\((?P<name>PERFILE_FLAGS_[0-9]+)\)')
_MAKE_PROJECT_RULE = re.compile(r'^(?P<name>[A-Za-z_][A-Za-z0-9_.+-]*)\s*:(?!=)(?P<prerequisites>.*)$')


def parse_make_project(path):
    """Read one generated `.make` project file into a Target."""
    name = os.path.splitext(os.path.basename(path))[0]
    target = Target(name, path)
    variables = {}
    per_file_flags = {}
    pending_source = None
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            # A per-file flag set is a variable, and which file uses it is only visible in the
            # recipe that names both. Nothing else in this file pairs the two, so the pairing is
            # kept here rather than reconstructed from a naming convention.
            recipe = _MAKE_PERFILE_USE.search(line)
            if recipe and pending_source is not None:
                for flag in per_file_flags.get(recipe.group('name'), ()):
                    target.compile_flags.append((pending_source, flag))
                pending_source = None
            rule = _MAKE_RULE_SOURCE.match(line)
            if rule:
                target.sources.append(rule.group('source'))
                pending_source = rule.group('source')
                continue
            assignment = _MAKE_ASSIGNMENT.match(line)
            if not assignment:
                continue
            key = assignment.group('name')
            value = assignment.group('value')
            variables[key] = value.strip()
            if key.startswith('PERFILE_FLAGS_'):
                per_file_flags[key] = [flag for flag in value.split() if not flag.startswith('$(')]
            elif key in ('ALL_CXXFLAGS', 'ALL_CFLAGS'):
                target.compile_flags.extend(
                    ('the whole project', flag) for flag in value.split() if not flag.startswith('$('))
            elif key == 'PCH':
                # Premake writes this only for a project that has one; a project without it has no
                # such assignment at all, while the `.gch` rule template and the empty
                # `FORCE_INCLUDE +=` are written for every project and say nothing.
                if value.strip():
                    target.precompiled_headers.append(('the whole project', value.strip()))
            elif key == 'FORCE_INCLUDE':
                if value.strip():
                    target.forced_includes.append(('the whole project', value.strip()))
            if key == 'INCLUDES':
                target.include_dirs.extend(entry[2:] for entry in value.split() if entry.startswith('-I'))
            elif key in ('LIBS', 'LDDEPS', 'ALL_LDFLAGS', 'LINKCMD'):
                target.links.extend(entry for entry in value.split() if entry.startswith('-l'))
                target.library_dirs.extend(entry[2:] for entry in value.split() if entry.startswith('-L'))
                if key in ('LIBS', 'LDDEPS'):
                    # A library named as a file rather than with -l: both a link input and, in
                    # LDDEPS, the prerequisite that makes the consumer relink when it changes. The
                    # project's own makefile is a prerequisite too, so that a changed link line
                    # relinks it, and nothing the link reads.
                    target.links.extend(entry for entry in value.split()
                                        if not entry.startswith('-') and not entry.endswith('.make'))
                if key == 'LDDEPS':
                    target.link_dependencies.extend(value.split())
            elif key == 'TARGET':
                expanded = value.strip().replace('$(TARGETDIR)', variables.get('TARGETDIR', ''))
                produced = os.path.basename(expanded)
                if produced and '$' not in produced:
                    target.outputs.append(produced)
                    target.add_artifact('target binary', expanded)
                    target.add_artifact('symbol file', expanded + '.debug')
            elif key == 'OBJDIR':
                target.add_artifact('intermediate directory', value)
    if not target.outputs:
        target.outputs.append(name)
        target.add_artifact('target binary name', name)
    return target


def parse_make_workspace_order(path, known):
    """Read the master `Makefile` into {project name: [project names it builds after]}.

    Only prerequisites naming a project of this workspace are kept. `all`, `clean` and the
    variable-expanded `$(PROJECTS)` say nothing about the order of any two projects.
    """
    order = {}
    try:
        handle = open(path, encoding='utf-8', errors='replace')
    except OSError:
        return order
    with handle:
        for line in handle:
            rule = _MAKE_PROJECT_RULE.match(line)
            if not rule:
                continue
            name = rule.group('name')
            if name not in known:
                continue
            order[name] = [entry for entry in rule.group('prerequisites').split() if entry in known]
    return order


def load_make_workspace(directory, label, expected):
    workspace = Workspace(label, expected, kind='make')
    if not os.path.isdir(directory):
        return None
    for entry in sorted(os.listdir(directory)):
        if entry.lower().endswith('.make'):
            target = parse_make_project(os.path.join(directory, entry))
            workspace.targets[target.name] = target
    order = parse_make_workspace_order(os.path.join(directory, 'Makefile'), workspace.targets)
    for name, prerequisites in order.items():
        workspace.targets[name].build_dependencies.extend(prerequisites)
    return workspace


# --- checks ----------------------------------------------------------------------------------


def check_expected_projects(workspace):
    problems = []
    for name in workspace.expected:
        if name not in workspace.targets:
            problems.append('%s: project %s is no longer generated' % (workspace.label, name))
    return problems


def check_engine_present(workspace):
    problems = []
    if ENGINE_PROJECT not in workspace.targets:
        problems.append('%s: project %s is not generated' % (workspace.label, ENGINE_PROJECT))
    # An optional adapter is generated in every tree whether or not the library it adapts is
    # installed: a build without the library produces an adapter that reports itself absent, which
    # is the case a consumer has to be able to compile against. A project that stopped being
    # generated would make that case unbuildable instead.
    for adapter in sorted(OPTIONAL_ADAPTER_SUITES):
        if adapter not in workspace.targets:
            problems.append('%s: project %s is not generated' % (workspace.label, adapter))
    return problems


def linked_static_libraries(workspace, name):
    """The workspace static libraries whose objects end up inside this target, transitively.

    A static library is not a boundary. Its objects are copied into whatever links it, so a forbidden
    include directory, source or import library named by one reaches the consumer exactly as if the
    consumer had named it - while every line of the consumer's own project stays clean. The engine's
    benchmark runner links one such library of engine-neutral fixtures, which is the arrangement
    this exists for.

    A shared library is a boundary and is not followed: its code is resolved when the process loads,
    and what it links is its own project's business, checked there.
    """
    reached = []
    seen = {name}
    frontier = [name]
    while frontier:
        current = workspace.targets.get(frontier.pop())
        if current is None:
            continue
        for other in sorted(workspace.targets):
            if other in seen or not produces_static_library(workspace.targets[other]):
                continue
            if links_project(current, other):
                seen.add(other)
                reached.append(other)
                frontier.append(other)
    return reached


def check_engine_isolation(workspace):
    """No engine target may reach any first-party component other than the engine.

    An engine suite that linked Controls or Processes would compile, pass, and quietly make the
    engine depend on the product it is meant to be independent of. External libraries - gtest,
    oneTBB, the codecs - are unrestricted; only AfterNight's own components are.

    The question is asked of everything an engine target links into itself, not only of its own
    project file, so that a static library cannot carry the dependency in on its behalf.
    """
    forbidden = tuple(name for name in EXPECTED_MAIN_PROJECTS if name != ENGINE_PROJECT)
    problems = []
    for name in sorted(workspace.targets):
        if not is_engine_target(name):
            continue
        carriers = [(name, workspace.targets[name])]
        carriers.extend((library, workspace.targets[library])
                        for library in linked_static_libraries(workspace, name))
        for carrier, target in carriers:
            through = '' if carrier == name else ' through the static library %s' % carrier
            for project in forbidden:
                for entry in target.links:
                    if link_base_name(entry).lower() == project.lower():
                        problems.append('%s: %s links %s%s'
                                        % (workspace.label, name, entry.strip(), through))
                for directory in target.include_dirs:
                    if references_directory(directory, ('src', project.lower())):
                        problems.append('%s: %s includes %s from the %s tree%s'
                                        % (workspace.label, name, directory, project, through))
                for source in target.sources:
                    if references_directory(source, ('src', project.lower())):
                        problems.append('%s: %s compiles %s from the %s tree%s'
                                        % (workspace.label, name, source, project, through))
                if project in target.build_dependencies and carrier == name:
                    problems.append('%s: %s builds after %s' % (workspace.label, name, project))
        if name == ENGINE_PROJECT:
            continue
        for directory in workspace.targets[name].include_dirs:
            if references_directory(directory, ENGINE_PRIVATE_DIR):
                problems.append('%s: %s includes %s, a private directory of %s'
                                % (workspace.label, name, directory, ENGINE_PROJECT))
    return problems



def reaches_qt(target):
    """Whether a target includes, searches or links any Qt 6 module.

    Qt is recognised by a whole path component starting `qt6` - the repository's `Qt6/<version>` tree - and by a
    link to a `Qt6*` library, which is how either generator spells every module.
    """
    directories = list(target.include_dirs) + list(target.library_dirs)
    if any(part.startswith('qt6') for directory in directories for part in path_components(directory)):
        return True
    return any(link_base_name(entry).lower().startswith('qt6') for entry in target.links)


def check_platform_isolation(workspace):
    """The platform library and its suite reach no engine, no Qt and no other product library, and the
    engine-free extension boundary does not reach the platform library.

    Linking, including, compiling and building after are each a way to acquire a dependency, so each is
    refused. The library compiles only its own sources; its suite may add the library itself and nothing else
    of AfterNight's.
    """
    first_party = tuple(name for name in EXPECTED_MAIN_PROJECTS if name != PLATFORM_PROJECT)
    first_party += (ENGINE_PROJECT,) + tuple(sorted(OPTIONAL_ADAPTER_SUITES)) + tuple(sorted(OPTIONAL_BACKEND_SUITES))
    problems = []
    for name, allowed in ((PLATFORM_PROJECT, ()), (PLATFORM_SUITE, (PLATFORM_PROJECT,))):
        target = workspace.targets.get(name)
        if target is None:
            continue
        for project in first_party:
            if project in allowed:
                continue
            if links_project(target, project):
                problems.append('%s: %s links %s; the platform library and its suite reach no engine and no '
                                'other product library' % (workspace.label, name, project))
            for directory in target.include_dirs:
                if references_directory(directory, ('src', project.lower())):
                    problems.append('%s: %s includes %s from the %s tree'
                                    % (workspace.label, name, directory, project))
            for source in target.sources:
                if references_directory(source, ('src', project.lower())):
                    problems.append('%s: %s compiles %s from the %s tree'
                                    % (workspace.label, name, source, project))
            if project in target.build_dependencies:
                problems.append('%s: %s builds after %s' % (workspace.label, name, project))
        if reaches_qt(target):
            problems.append('%s: %s reaches Qt; the platform library is Qt-free so the command-line tool and the '
                            'runtime backend can load it' % (workspace.label, name))
        if name == PLATFORM_PROJECT:
            for source in target.sources:
                if not references_directory(source, PLATFORM_SOURCE_DIR):
                    problems.append('%s: %s compiles %s, which is not one of its own sources'
                                    % (workspace.label, name, source))
    for name in PLATFORM_FREE_PROJECTS:
        target = workspace.targets.get(name)
        if target is None:
            continue
        if links_project(target, PLATFORM_PROJECT):
            problems.append('%s: %s links %s; the engine-free extension boundary links nothing of AfterNight\'s '
                            'but the protocol' % (workspace.label, name, PLATFORM_PROJECT))
        for directory in target.include_dirs:
            if references_directory(directory, PLATFORM_SOURCE_DIR):
                problems.append('%s: %s includes %s from the %s tree'
                                % (workspace.label, name, directory, PLATFORM_PROJECT))
        if PLATFORM_PROJECT in target.build_dependencies:
            problems.append('%s: %s builds after %s' % (workspace.label, name, PLATFORM_PROJECT))
    return problems


def check_style_plugin_isolation(workspace):
    """The style plugin links nothing of this workspace but the Controls library, and searches and compiles nothing of
    an engine's tree, so whatever engine it reaches it reaches through Controls.

    Qt loads the plugin by name into a process that already holds one engine; a link or search path of its own would
    let it reach an engine by another way than the library whose theme it draws.
    """
    target = workspace.targets.get(STYLE_PLUGIN_PROJECT)
    if target is None:
        return []
    problems = []
    first_party = tuple(name for name in EXPECTED_MAIN_PROJECTS
                        if name not in (STYLE_PLUGIN_PROJECT, STYLE_PLUGIN_THROUGH))
    first_party += (ENGINE_PROJECT, CONSUMER_RUNTIME_PROJECT)
    for project in first_party:
        if links_project(target, project):
            problems.append('%s: %s links %s; the style plugin reaches the engine only through %s'
                            % (workspace.label, STYLE_PLUGIN_PROJECT, project, STYLE_PLUGIN_THROUGH))
    for project in ENGINE_PROJECTS:
        for directory in target.include_dirs:
            if references_directory(directory, ('src', project.lower())):
                problems.append('%s: %s includes %s from the %s tree'
                                % (workspace.label, STYLE_PLUGIN_PROJECT, directory, project))
        for source in target.sources:
            if references_directory(source, ('src', project.lower())):
                problems.append('%s: %s compiles %s from the %s tree'
                                % (workspace.label, STYLE_PLUGIN_PROJECT, source, project))
    return problems


def reaches_python(target):
    """Whether a target includes, searches or links the Python interpreter.

    Python is recognised by a whole path component starting `python` - the repository's bundled `python/python/...`
    tree and an interpreter's `include/python3.x` - and by a link to a `python*` library.
    """
    directories = list(target.include_dirs) + list(target.library_dirs)
    if any(part.startswith('python') for directory in directories for part in path_components(directory)):
        return True
    return any(link_base_name(entry).lower().startswith('python') for entry in target.links)


def check_consumer_runtime_isolation(workspace):
    """The consumer runtime and its suite reach the engine and the platform library and nothing else of AfterNight's,
    no Qt and no Python; and nothing below the runtime, nor the engine-free extension boundary, reaches it.

    Linking, including, compiling and building after are each a way to acquire a dependency, so each is refused, as
    for the platform library. A tree without the runtime is not checked here: the registry decides whether it must
    exist.
    """
    first_party = tuple(name for name in EXPECTED_MAIN_PROJECTS if name not in CONSUMER_RUNTIME_LINKS)
    first_party += (ENGINE_PROJECT, CONSUMER_RUNTIME_PROJECT) + tuple(sorted(OPTIONAL_ADAPTER_SUITES))
    first_party += tuple(sorted(OPTIONAL_BACKEND_SUITES))
    problems = []
    for name, allowed in ((CONSUMER_RUNTIME_PROJECT, CONSUMER_RUNTIME_LINKS + (CONSUMER_RUNTIME_PROJECT,)),
                          (CONSUMER_RUNTIME_SUITE, CONSUMER_RUNTIME_LINKS + (CONSUMER_RUNTIME_PROJECT,))):
        target = workspace.targets.get(name)
        if target is None:
            continue
        for project in first_party:
            if project in allowed:
                continue
            if links_project(target, project):
                problems.append('%s: %s links %s; the consumer runtime and its suite reach the engine and the '
                                'platform library and nothing else of AfterNight\'s' % (workspace.label, name, project))
            for directory in target.include_dirs:
                if references_directory(directory, ('src', project.lower())):
                    problems.append('%s: %s includes %s from the %s tree'
                                    % (workspace.label, name, directory, project))
            for source in target.sources:
                if references_directory(source, ('src', project.lower())):
                    problems.append('%s: %s compiles %s from the %s tree'
                                    % (workspace.label, name, source, project))
            if project in target.build_dependencies:
                problems.append('%s: %s builds after %s' % (workspace.label, name, project))
        if reaches_qt(target):
            problems.append('%s: %s reaches Qt; the consumer runtime is Qt-free so the command-line tool and the '
                            'runtime backend can load it' % (workspace.label, name))
        if reaches_python(target):
            problems.append('%s: %s reaches Python; the consumer runtime is Python-free, and the runtime backend '
                            'brings the interpreter its host owns' % (workspace.label, name))
        if name == CONSUMER_RUNTIME_PROJECT:
            for source in target.sources:
                if not references_directory(source, CONSUMER_RUNTIME_SOURCE_DIR):
                    problems.append('%s: %s compiles %s, which is not one of its own sources'
                                    % (workspace.label, name, source))
    for name in CONSUMER_RUNTIME_FREE_PROJECTS:
        target = workspace.targets.get(name)
        if target is None:
            continue
        if links_project(target, CONSUMER_RUNTIME_PROJECT):
            problems.append('%s: %s links %s; nothing below the consumer runtime, and nothing of the engine-free '
                            'extension boundary, reaches it' % (workspace.label, name, CONSUMER_RUNTIME_PROJECT))
        for directory in target.include_dirs:
            if references_directory(directory, CONSUMER_RUNTIME_SOURCE_DIR):
                problems.append('%s: %s includes %s from the %s tree'
                                % (workspace.label, name, directory, CONSUMER_RUNTIME_PROJECT))
        if CONSUMER_RUNTIME_PROJECT in target.build_dependencies:
            problems.append('%s: %s builds after %s' % (workspace.label, name, CONSUMER_RUNTIME_PROJECT))
    return problems


def check_command_line_tool_isolation(workspace):
    """The command-line tool and its suite reach no Qt, no Python and no product library, and compile only what is
    theirs.

    Linking, including, compiling and building after are each a way to acquire a dependency, as for the platform
    library, so each is refused - except that the suite builds after the tool, whose binary it runs. The engine is not
    named here: whether a target reaches it is its registry class, held by the registry's own rule.
    """
    allowed = ENGINE_PROJECTS + (PLATFORM_PROJECT, CLI_PROJECT)
    forbidden = tuple(name for name in EXPECTED_MAIN_PROJECTS if name not in allowed)
    forbidden += tuple(sorted(OPTIONAL_ADAPTER_SUITES)) + tuple(sorted(OPTIONAL_BACKEND_SUITES))
    problems = []
    for name, own in ((CLI_PROJECT, (CLI_SOURCE_DIR,)), (CLI_SUITE, CLI_SUITE_SOURCE_DIRS)):
        target = workspace.targets.get(name)
        if target is None:
            continue
        for project in forbidden:
            if links_project(target, project):
                problems.append('%s: %s links %s; the command-line tool and its suite reach no product library'
                                % (workspace.label, name, project))
            for directory in target.include_dirs:
                if references_directory(directory, ('src', project.lower())):
                    problems.append('%s: %s includes %s from the %s tree'
                                    % (workspace.label, name, directory, project))
            if project in target.build_dependencies:
                problems.append('%s: %s builds after %s' % (workspace.label, name, project))
        if reaches_qt(target):
            problems.append('%s: %s reaches Qt; the command-line tool runs where no Qt is installed'
                            % (workspace.label, name))
        if reaches_python(target):
            problems.append('%s: %s reaches Python; the command-line tool runs where no interpreter is installed'
                            % (workspace.label, name))
        for source in target.sources:
            if not any(references_directory(source, directory) for directory in own):
                problems.append('%s: %s compiles %s, which is not one of its own sources'
                                % (workspace.label, name, source))
    return problems


def check_runtime_backend_isolation(workspace):
    """The runtime backend and its driver reach no Qt and no product library, and compile only what is theirs.

    Linking, including, compiling and building after are each a way to acquire a dependency, as for the command-line
    tool, so each is refused - except that the driver includes the backend's headers and builds after it, whose module
    it loads. Python is the backend's to reach: it is the interpreter's module. The engine is not named here: whether a
    target reaches it is its registry class, held by the registry's own rule.
    """
    allowed = ENGINE_PROJECTS + (PLATFORM_PROJECT, RUNTIME_BACKEND_PROJECT)
    forbidden = tuple(name for name in EXPECTED_MAIN_PROJECTS if name not in allowed)
    forbidden += tuple(sorted(OPTIONAL_ADAPTER_SUITES)) + tuple(sorted(OPTIONAL_BACKEND_SUITES))
    problems = []
    for name, own in ((RUNTIME_BACKEND_PROJECT, (RUNTIME_BACKEND_SOURCE_DIR,)),
                      (RUNTIME_BACKEND_SUITE, RUNTIME_BACKEND_SUITE_SOURCE_DIRS)):
        target = workspace.targets.get(name)
        if target is None:
            continue
        for project in forbidden:
            if links_project(target, project):
                problems.append('%s: %s links %s; the runtime backend and its driver reach no product library'
                                % (workspace.label, name, project))
            for directory in target.include_dirs:
                if references_directory(directory, ('src', project.lower())):
                    problems.append('%s: %s includes %s from the %s tree'
                                    % (workspace.label, name, directory, project))
            if project in target.build_dependencies:
                problems.append('%s: %s builds after %s' % (workspace.label, name, project))
        if reaches_qt(target):
            problems.append('%s: %s reaches Qt; the runtime backend is loaded into the full host, whose Qt it must '
                            'not bring a second copy of' % (workspace.label, name))
        for source in target.sources:
            if not any(references_directory(source, directory) for directory in own):
                problems.append('%s: %s compiles %s, which is not one of its own sources'
                                % (workspace.label, name, source))
    return problems


def check_product_layers(workspace):
    """No product library reaches a product target above it.

    Linking, including, compiling and building after are each a way to acquire a dependency, as they are for the
    platform library, so each is refused. The rule is about the libraries alone: a suite may reach both, because
    it tests them together.
    """
    problems = []
    for name, above in PRODUCT_LAYERS:
        target = workspace.targets.get(name)
        if target is None:
            continue
        for project in above:
            if links_project(target, project):
                problems.append('%s: %s links %s; a product library links only the libraries below it'
                                % (workspace.label, name, project))
            for directory in target.include_dirs:
                if references_directory(directory, ('src', project.lower())):
                    problems.append('%s: %s includes %s from the %s tree' % (workspace.label, name, directory, project))
            for source in target.sources:
                if references_directory(source, ('src', project.lower())):
                    problems.append('%s: %s compiles %s from the %s tree' % (workspace.label, name, source, project))
            if project in target.build_dependencies:
                problems.append('%s: %s builds after %s' % (workspace.label, name, project))
    return problems


def check_precompiled_headers(workspace):
    """Nothing on this engine's side of the workspace compiles through a header it did not include.

    A precompiled header makes everything it holds transitive in every translation unit of a
    target, so the target's include graph is no longer what its include lines say. The engine's own
    include graph is read by `check_include_layers.py` out of those lines, and a precompiled header
    or a forced include would put a header into every unit of a target without writing one - which
    would make that reading, and the budgets measured from it, describe a build that is not
    happening.

    Both settings are the same fact and both are refused. A precompiled header is not refused
    anywhere else: the product targets have theirs, and this says nothing about them.
    """
    problems = []
    for name in sorted(workspace.targets):
        if not is_engine_target(name):
            continue
        target = workspace.targets[name]
        for scope, precompiled in target.precompiled_headers:
            problems.append('%s: %s compiles through a precompiled header (%s: %s); this engine has '
                            'none, so that its include graph is the one its include lines describe'
                            % (workspace.label, name, scope, precompiled))
        for scope, forced in target.forced_includes:
            problems.append('%s: %s forces an include (%s: %s); a header no source asked for is in '
                            'every translation unit of that target and in none of its include lines'
                            % (workspace.label, name, scope, forced))
    return problems


def check_optional_adapters(workspace):
    """Each optional adapter links the engine, and nothing but its own suite links the adapter.

    Both halves are the same rule read from opposite ends. An adapter that stopped linking the
    engine would have stopped adapting it; anything else that starts linking an adapter
    has quietly made an optional dependency a required one, and the way that happens is a convenience
    link added to a suite or a runner rather than a deliberate change here.

    The engine is named separately in the message it produces, because that one is not a
    packaging mistake but the coupling the whole arrangement exists to prevent: an engine that linked
    its Python adapter would charge every consumer of the engine for a Python runtime.
    """
    problems = []
    for adapter, suite in sorted(OPTIONAL_ADAPTER_SUITES.items()):
        problems.extend(_check_optional_component(workspace, adapter, suite, 'adapter',
                                                  'the library it adapts',
                                                  OPTIONAL_ADAPTER_CONSUMERS.get(adapter, ())))
    return problems


def check_optional_backends(workspace):
    """Each generated GPU backend links the engine, and nothing but its own suite links the backend.

    The rules an adapter has, for the same reasons, read from both ends. An engine that linked the
    backend would charge every consumer for a GPU SDK, which is the dependency the proof is required
    not to leave behind.
    """
    problems = []
    for backend, suite in sorted(OPTIONAL_BACKEND_SUITES.items()):
        problems.extend(_check_optional_component(workspace, backend, suite, 'GPU backend', 'a GPU SDK'))
    return problems


def _check_optional_component(workspace, component, suite, kind, acquired, consumers=()):
    """The two link rules of one optional component, which only its own suite and its named consumers may link."""
    problems = []
    target = workspace.targets.get(component)
    if target is not None and not links_project(target, ENGINE_PROJECT):
        problems.append('%s: %s does not link %s' % (workspace.label, component, ENGINE_PROJECT))
    allowed = ' or '.join((suite,) + tuple(consumers))
    for name in sorted(workspace.targets):
        if name in (component, suite) or name in consumers:
            continue
        other = workspace.targets[name]
        if links_project(other, component):
            if name == ENGINE_PROJECT:
                problems.append('%s: %s links %s; an optional %s is never a dependency of the engine, '
                                'or every consumer of the engine acquires %s'
                                % (workspace.label, name, component, kind, acquired))
            else:
                problems.append('%s: %s links %s; only %s may link an optional %s'
                                % (workspace.label, name, component, allowed, kind))
        if component in other.build_dependencies:
            problems.append('%s: %s builds after %s, so it has taken a dependency on an optional %s'
                            % (workspace.label, name, component, kind))
    return problems


def check_optional_backend_pairing(label, workspaces):
    """A GPU backend and its suite are generated together or not at all.

    One without the other is a tree generated half with the opt-in and half without it: a backend no
    suite proves, or a suite that cannot link.
    """
    generated = set()
    for workspace in workspaces:
        generated.update(workspace.targets)
    problems = []
    for backend, suite in sorted(OPTIONAL_BACKEND_SUITES.items()):
        for present, absent in ((backend, suite), (suite, backend)):
            if present in generated and absent not in generated:
                problems.append('%s: %s is generated without %s; an opt-in GPU backend and its suite are '
                                'generated together or not at all' % (label, present, absent))
    return problems


def gpu_sdk_reached_by(target):
    """The include directory, library directory or link entry through which a target reaches a GPU SDK."""
    for directory in target.include_dirs + target.library_dirs:
        if any(component in GPU_SDK_DIRECTORY_COMPONENTS for component in path_components(directory)):
            return directory.strip()
    for entry in target.links:
        if link_base_name(entry).lower() in GPU_SDK_LIBRARIES:
            return entry.strip()
    return None


def check_gpu_sdk_reach(workspace):
    """No target but an opt-in GPU backend reaches a GPU SDK.

    Not the engine, not a product target, and not the backend's own suite, which tests the backend
    through its published surface and so has no reason to compile against the SDK beneath it.
    """
    problems = []
    for name in sorted(workspace.targets):
        if name in OPTIONAL_BACKEND_SUITES:
            continue
        reached = gpu_sdk_reached_by(workspace.targets[name])
        if reached is not None:
            problems.append('%s: %s reaches a GPU SDK through %s; only an opt-in GPU backend may, so that a '
                            'build that never asked for one acquires none' % (workspace.label, name, reached))
    return problems


def check_runner_engines(workspace):
    """Every benchmark runner links the one engine its name claims."""
    problems = []
    for name in sorted(workspace.targets):
        for runner, engine in RUNNER_ENGINES:
            if name == runner and not links_project(workspace.targets[name], engine):
                problems.append('%s: %s does not link %s' % (workspace.label, name, engine))
    return problems



def check_intra_workspace_build_order(workspace):
    """A project that links another project of its own workspace must build after it.

    Without that edge a parallel build is free to link a consumer against a library it has not
    rebuilt yet, or one that does not exist at all in a clean tree. The failure is a linker error
    about symbols the source plainly defines, it depends on which job the scheduler happened to
    start first, and building a second time usually clears it - so it reads as a flake rather than
    as the missing edge it is.

    Only libraries produced inside the same workspace are checked. One workspace's makefile cannot
    name another's project, and the build scripts order the two workspaces themselves.
    """
    problems = []
    for name in sorted(workspace.targets):
        target = workspace.targets[name]
        declared = {entry.lower() for entry in target.build_dependencies}
        for other in sorted(workspace.targets):
            if other == name or other.lower() in declared:
                continue
            if links_project(target, other):
                problems.append('%s: %s links %s but does not declare it as a build dependency'
                                % (workspace.label, name, other))
    return problems


def check_solution_build_order(workspace):
    """A solution builds every project it lists after the projects of that solution it links.

    The workspace rule above reads the projects generated into one directory. A solution may also list a
    project from another workspace's directory - the test solution lists the engine and its optional adapters
    so the suites can build after them - and the edge that orders such a project behind the library it links
    has to be stated in this solution too: the other workspace's solution stating it orders a build of that
    solution, not of this one. Without it a clean or rebuilt test solution under /m linked both optional
    adapters before the engine's import library existed and failed with LNK1181, which reads as a flake.

    Pairs of projects that both belong to this workspace are the workspace rule's, and are not reported twice.
    """
    problems = []
    for solution, listed in sorted(workspace.solutions.items()):
        for name, (target, dependencies) in sorted(listed.items()):
            if target is None:
                continue
            declared = {entry.lower() for entry in dependencies}
            for other in sorted(listed):
                if other == name or other.lower() in declared:
                    continue
                if name in workspace.targets and other in workspace.targets:
                    continue
                if not links_project(target, other):
                    continue
                where = '' if name in workspace.targets else ' from another workspace'
                problems.append('%s: %s lists %s%s and does not build it after %s, which it links'
                                % (workspace.label, solution, name, where, other))
    return problems


def produces_static_library(target):
    """True when this project's output is a static archive rather than a shared library.

    Both front ends are asked the same way. A makefile names the archive in `TARGET`, so the output
    carries `.a`; a generated `.vcxproj` puts the extension in `TargetExt`, which reaches the target
    binary artifact rather than the output name. A shared library's own import library is a separate
    artifact kind, so it cannot be mistaken for this.
    """
    if any(output.lower().endswith('.a') for output in target.outputs):
        return True
    return any(kind in ('target binary', 'target binary name') and path.lower().endswith(('.lib', '.a'))
               for kind, path in target.artifacts)


def check_relink_dependencies(workspace):
    """A makefile target must name the workspace *static* library it links as a link prerequisite.

    Build order alone is not enough. When only the archive changes, the consumer's binary is still
    newer than its own object files, so make relinks nothing and the executable keeps the code it
    was linked against - one process then holds two layouts of one class, with no diagnostic and no
    build error.

    Shared libraries are exempt: their code is resolved when the process loads, so a consumer picks
    up a rebuilt one without being relinked. MSBuild relinks on a project reference, so this is a
    check of the make front end, which needs the dependency stated.
    """
    if workspace.kind != 'make':
        return []
    problems = []
    for name in sorted(workspace.targets):
        target = workspace.targets[name]
        declared = {link_base_name(entry).lower() for entry in target.link_dependencies}
        for other in sorted(workspace.targets):
            if other == name or other.lower() in declared:
                continue
            if produces_static_library(workspace.targets[other]) and links_project(target, other):
                problems.append('%s: %s links the static library %s without a link prerequisite on '
                                'it, so it will not relink when %s changes'
                                % (workspace.label, name, other, other))
    return problems


def _artifact_key(target, kind, artifact):
    if kind == 'target binary name':
        return 'name:' + artifact.lower()
    project_directory = os.path.dirname(os.path.abspath(target.location))
    resolved = artifact.replace('$(ProjectDir)', project_directory + os.sep)
    resolved = resolved.replace('\\', os.sep).replace('/', os.sep)
    if not os.path.isabs(resolved):
        resolved = os.path.join(project_directory, resolved)
    return os.path.normcase(os.path.normpath(resolved)).lower()


def check_output_collisions(label, workspaces):
    """No two projects may share a binary, import library, symbol file, or object directory."""
    owners = {}
    problems = []
    for workspace in workspaces:
        for name in sorted(workspace.targets):
            target = workspace.targets[name]
            for kind, artifact in target.artifacts:
                key = _artifact_key(target, kind, artifact)
                previous = owners.get(key)
                if previous is not None and previous[0] != name:
                    problems.append('%s: %s and %s share %s %s'
                                    % (label, previous[0], name, kind, artifact))
                else:
                    owners[key] = (name, kind)
    return problems


# Which compiler switches select an instruction set. Two spellings for one idea: MSVC states it as
# a target architecture and the GNU-style compilers as a feature at a time, so a rule written
# against one of them would hold on one platform and say nothing on the other.
_ARCH_FLAG = re.compile(r'^/arch:(?P<value>[A-Za-z0-9.+]+)$', re.IGNORECASE)
_FEATURE_FLAG = re.compile(
    r'^-m(?P<value>avx[0-9a-z._]*|sse[0-9._]*|ssse3|fma|f16c|bmi2?|lzcnt|movbe|aes|pclmul|popcnt|'
    r'xop|fma4|neon|arch=[A-Za-z0-9_.+-]+)$', re.IGNORECASE)

# The instruction sets that must never appear on a command line in this repository. They exist in
# the engine as per-target functions the vector abstraction emits inside one ordinary object; a
# switch would raise a whole translation unit instead, and every function in it - including the
# ones a host without those instructions is about to call - would be free to use them.
_ABOVE_FLOOR = ('avx512', 'avx10')

# The packaged floor, in every spelling a generator emits for it. A project may select this and
# nothing else at project scope: raising a whole project is exactly what the isolation exists to
# prevent, and lowering one would silently produce a build narrower than the product's floor.
PACKAGED_FLOOR_FLAGS = ('/arch:avx2', '-mavx2', 'advancedvectorextensions2')

# The engine's translation units that are permitted instruction-set flags of their own. These are
# the units where the vector abstraction compiles one copy of a kernel per target, and the flags
# they carry complete the packaged floor for a compiler whose own floor switch implies less than
# MSVC's does - they never raise it. Everything else in the engine is compiled identically.
ENGINE_VECTOR_SOURCES = (
    'CalibrationCpu.cpp',
    'ColorAdjustmentCpu.cpp',
    'DemosaicAmazeCpu.cpp',
    'DemosaicBilinearCpu.cpp',
    'DemosaicFastCpu.cpp',
    'DemosaicLuminanceAhdCpu.cpp',
    'DemosaicMarkesteijnOneCpu.cpp',
    'DemosaicMarkesteijnThreeCpu.cpp',
    'DemosaicRcdCpu.cpp',
    'DemosaicXTransFastCpu.cpp',
    'LocalNormalizationCpu.cpp',
    'NormalizationCpu.cpp',
    'PresentationCpu.cpp',
    'PsfFitCpu.cpp',
    'SimdKernelTables.cpp',
    'StackAccumulatorCpu.cpp',
    'StackMethodCpu.cpp',
    'StackRejectionCpu.cpp',
    'StackSourceCpu.cpp',
    'StretchCpu.cpp',
    'WarpCpu.cpp',
)


# Which compiler switches turn implicit fused multiply-add on, in every spelling a generator here
# could emit. `/fp:fast` and `-ffast-math` are listed with the contraction switches rather than
# separately because turning them on turns contraction on among much else.
CONTRACTION_ON_FLAGS = ('/fp:contract', '/fp:fast', '-ffast-math', '-ofast', '-ffp-contract=fast',
                        '-ffp-contract=on', '-ffp-contract=fast-honor-pragmas')

# Which switches turn it off, in the two spellings the engine uses.
CONTRACTION_OFF_FLAGS = ('-ffp-contract=off', '/clang:-ffp-contract=off')

# The trees whose compiler has no switch to state it with. Microsoft's `/fp:precise` is the default
# and does not contract, `/fp:contract` turns contraction on, and `/fp:contract-` is rejected as an
# unknown option - so the rule there is the prohibition above and nothing further. Naming the tree
# rather than inferring the compiler keeps the exemption something a reader can check.
CONTRACTION_FLAGLESS_TREES = ('vs2026-msvc',)


def is_instruction_set_flag(flag):
    """Whether one compiler switch selects an instruction set."""
    return bool(_ARCH_FLAG.match(flag) or _FEATURE_FLAG.match(flag))


def names_above_floor(flag):
    """Whether one switch names an instruction set above the packaged floor."""
    lowered = flag.lower()
    return any(marker in lowered for marker in _ABOVE_FLOOR)


def check_instruction_sets(workspace):
    """No project raises its instruction set, and only the vector units name one per file.

    The engine compiles kernels for more than one instruction set, and how that is arranged decides
    whether the arrangement is safe. Compiling a whole translation unit at a raised target makes
    every function in it - the dispatch that chose it, the tables it reads, the tail of a loop -
    free to use instructions the host may not have, and nothing in a build log says so. This engine
    instead lets the vector abstraction emit one attributed copy of each kernel per target inside an
    otherwise ordinary object, so no command line in the repository names a set above the floor.

    That property is invisible in the source and lives entirely in the generated projects, which is
    why it is checked here rather than beside the kernels.
    """
    problems = []
    for name in sorted(workspace.targets):
        target = workspace.targets[name]
        for scope, flag in target.compile_flags:
            where = '%s: %s' % (workspace.label, name)
            if names_above_floor(flag):
                problems.append(
                    '%s compiles %s with %s; an instruction set above the packaged floor is emitted '
                    'per function by the vector abstraction and never selected for a whole '
                    'translation unit' % (where, scope, flag))
                continue
            if not is_instruction_set_flag(flag):
                continue
            if scope == 'the whole project':
                if name == ENGINE_PROJECT and flag.lower() not in PACKAGED_FLOOR_FLAGS:
                    problems.append(
                        '%s compiles %s with %s; the engine is built at the packaged floor '
                        'and nothing else' % (where, scope, flag))
                continue
            if name != ENGINE_PROJECT:
                continue
            source = os.path.basename(scope.replace('\\', '/'))
            if source not in ENGINE_VECTOR_SOURCES:
                problems.append(
                    '%s compiles %s with %s; only the vector translation units carry instruction-set '
                    'flags of their own, and they are %s'
                    % (where, scope, flag, ', '.join(ENGINE_VECTOR_SOURCES)))
    return problems


def check_contraction(label, workspace):
    """Nothing contracts implicitly, and the engine says so instead of inheriting a default.

    The failure this prevents is silent by construction. Every compiled backend still agrees with
    every other, because they all fuse; what moves is the scalar reference they are all compared
    against, and a differential harness reports agreement either way. The only place the difference
    is visible is a command line, which is why it is checked here.

    The prohibition applies to every project of every tree: a contraction switch anywhere would be a
    numeric change to a shipping binary. The positive half applies to the engine alone, and only on
    a toolchain that has a switch to state it with.
    """
    problems = []
    for name in sorted(workspace.targets):
        target = workspace.targets[name]
        for scope, flag in target.compile_flags:
            if flag.lower() in CONTRACTION_ON_FLAGS:
                problems.append(
                    '%s: %s compiles %s with %s; implicit fused multiply-add changes where a '
                    'rounding happens and would fuse a scalar reference as readily as the kernel '
                    'that reference judges' % (workspace.label, name, scope, flag))
    if label in CONTRACTION_FLAGLESS_TREES:
        return problems
    target = workspace.targets.get(ENGINE_PROJECT)
    if target is None:
        return problems
    stated = any(scope == 'the whole project' and flag.lower() in CONTRACTION_OFF_FLAGS
                 for scope, flag in target.compile_flags)
    if not stated:
        problems.append(
            '%s: %s states no contraction setting; this toolchain contracts by default, so the '
            'engine names one of %s at project scope rather than relying on which of its '
            'translation units happens to have the instruction available'
            % (workspace.label, ENGINE_PROJECT, ', '.join(CONTRACTION_OFF_FLAGS)))
    return problems


def check_cross_tree_collisions(trees):
    """No two generated trees may write to one object directory, binary or symbol file.

    `check_output_collisions` asks the question inside one tree, which is where two *projects* can
    collide. Two *trees* collide differently: the same project exists in both, and the toolchain
    each was generated for is the only thing that distinguishes their output. A shared object
    directory is the worse half of that, because clang-cl matches MSVC's name mangling, so a binary
    holding objects from both links instead of failing.

    `trees` is [(label, workspaces)]. Only paths are compared; a target-binary *name* is shared
    across trees by design, since each tree writes its own copy into its own directory.
    """
    owners = {}
    problems = []
    for label, workspaces in trees:
        claimed = {}
        for workspace in workspaces:
            for name in sorted(workspace.targets):
                target = workspace.targets[name]
                for kind, artifact in target.artifacts:
                    if kind == 'target binary name':
                        continue
                    claimed.setdefault(_artifact_key(target, kind, artifact), (name, kind))
        for key, (name, kind) in sorted(claimed.items()):
            previous = owners.get(key)
            if previous is not None and previous[0] != label:
                problems.append('%s and %s share the %s %s, so one toolchain\'s build overwrites '
                                'the other\'s in place' % (previous[0], label, kind, key))
            else:
                owners[key] = (label, name)
    return problems


def check_tree(label, workspaces):
    problems = []
    for workspace in workspaces:
        if not workspace.targets:
            problems.append('%s: no project files found; regenerate with 01-create_project_files'
                            % workspace.label)
            continue
        problems.extend(check_expected_projects(workspace))
        problems.extend(check_engine_isolation(workspace))
        problems.extend(check_platform_isolation(workspace))
        problems.extend(check_style_plugin_isolation(workspace))
        problems.extend(check_consumer_runtime_isolation(workspace))
        problems.extend(check_command_line_tool_isolation(workspace))
        problems.extend(check_runtime_backend_isolation(workspace))
        problems.extend(check_product_layers(workspace))
        problems.extend(check_precompiled_headers(workspace))
        problems.extend(check_optional_adapters(workspace))
        problems.extend(check_optional_backends(workspace))
        problems.extend(check_gpu_sdk_reach(workspace))
        problems.extend(check_runner_engines(workspace))
        problems.extend(check_intra_workspace_build_order(workspace))
        problems.extend(check_solution_build_order(workspace))
        problems.extend(check_relink_dependencies(workspace))
        problems.extend(check_instruction_sets(workspace))
        problems.extend(check_contraction(label, workspace))
    if any(workspace.targets for workspace in workspaces):
        problems.extend(check_output_collisions(label, workspaces))
        problems.extend(check_optional_backend_pairing(label, workspaces))
    return problems


def newest_definition_mtime(repo_root):
    """Newest modification time among the Premake definitions, or 0 when none are present.

    0 means "cannot tell", and every tree is then treated as current. That is the right default for
    a checkout that holds generated trees but not the definitions - a job that only unpacks build
    output - because refusing to check anything would be worse than checking a tree that might be
    stale.
    """
    newest = 0.0
    for base, _directories, files in os.walk(os.path.join(repo_root, PREMAKE_DIR)):
        for name in files:
            if name.lower().endswith('.lua'):
                newest = max(newest, os.path.getmtime(os.path.join(base, name)))
    for relative in GENERATOR_REGISTRIES:
        path = os.path.join(repo_root, relative)
        if os.path.isfile(path):
            newest = max(newest, os.path.getmtime(path))
    return newest


def newest_project_mtime(workspaces):
    """Newest modification time among the generated project files of one tree."""
    newest = 0.0
    for workspace in workspaces:
        for target in workspace.targets.values():
            try:
                newest = max(newest, os.path.getmtime(target.location))
            except OSError:
                continue
    return newest


def generation_mtime(directory, workspaces):
    """When this tree was last generated, falling back for older generator scripts.

    Premake deliberately leaves a project file untouched when its generated bytes are unchanged.
    A successful comment-only regeneration can therefore have no project newer than the input
    definition. The generator scripts write a stamp after both workspaces succeed so freshness
    describes the generation event, not whether Premake happened to rewrite an identical file.
    """
    stamp = os.path.join(directory, GENERATION_STAMP)
    try:
        return os.path.getmtime(stamp)
    except OSError:
        return newest_project_mtime(workspaces)



def load_tree(repo_root, label, relative, kind):
    """Load the main and test workspaces of one generated tree, or None when it is not generated."""
    directory = os.path.join(repo_root, relative)
    if not os.path.isdir(directory):
        return None
    loader = load_vs_workspace if kind == 'vs' else load_make_workspace
    main = loader(directory, '%s/main' % label, EXPECTED_MAIN_PROJECTS)
    tests = loader(os.path.join(directory, 'tests'), '%s/tests' % label, EXPECTED_TEST_PROJECTS)
    workspaces = [workspace for workspace in (main, tests) if workspace is not None]
    return workspaces or None



def registry_findings(repo_root, trees):
    """The target registry's rules for every current tree, or nothing when the checkout holds no registry.

    A checkout without the registry is `consumer_targets.py check`'s finding, not this one's: the fixtures of this
    checker's own tests hold no registry, and every rule above has to be answerable without one.
    """
    if not os.path.isfile(os.path.join(repo_root, GENERATOR_REGISTRIES[0])):
        return []
    # Imported here because that module imports this one for its readers.
    import pathlib
    import consumer_targets
    registry = consumer_targets.read_registry(pathlib.Path(repo_root))
    problems = list(registry.problems)
    kinds = {label: (relative, kind) for label, relative, kind in GENERATED_TREES}
    for label, workspaces in trees:
        relative, kind = kinds[label]
        loader = load_vs_workspace if kind == 'vs' else load_make_workspace
        third = loader(os.path.join(repo_root, relative, '3rdparty'), '%s/3rdparty' % label, ())
        extra = [third] if third is not None and third.targets else []
        problems.extend(consumer_targets.generated_findings(registry, label, list(workspaces) + extra,
                                                            pathlib.Path(repo_root)))
    return problems


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=REPO_ROOT,
                        help='repository root to inspect (default: the checkout this script lives in)')
    options = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout

    problems = []
    inspected = []
    current = []
    stale = []
    definitions_mtime = newest_definition_mtime(options.repo_root)
    for label, relative, kind in GENERATED_TREES:
        directory = os.path.join(options.repo_root, relative)
        workspaces = load_tree(options.repo_root, label, relative, kind)
        if workspaces is None:
            continue
        if not any(workspace.targets for workspace in workspaces):
            inspected.append(label)
            problems.append('%s: no project files found; regenerate with 01-create_project_files'
                            % label)
            continue
        if definitions_mtime > generation_mtime(directory, workspaces):
            # Older than the definitions that produce it, so it describes a graph that is not the
            # one being built. Reported rather than checked: treating it as authoritative would be
            # a false result either way, and a host that generates for one platform only would
            # otherwise fail on the other platform's leftovers.
            stale.append(label)
            continue
        inspected.append(label)
        current.append((label, workspaces))
        problems.extend(check_tree(label, workspaces))
        main_workspace = workspaces[0]
        if main_workspace.targets:
            problems.extend(check_engine_present(main_workspace))

    problems.extend(check_cross_tree_collisions(current))
    problems.extend(registry_findings(options.repo_root, current))

    if stale:
        out.write('skipped stale generated trees, older than the Premake definitions: %s\n' % ', '.join(stale))

    if not inspected:
        out.write('no current generated project tree found; generate projects first '
                  '(01-create_project_files)\n')
        return 1

    out.write('inspected generated trees: %s\n' % ', '.join(inspected))
    for problem in problems:
        out.write('%s\n' % problem)
    if problems:
        out.write('%d build-graph problem(s)\n' % len(problems))
        return 1
    out.write('build graph ok\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())

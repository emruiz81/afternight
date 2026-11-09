#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Build, deploy and inspect the active target set the target registry names.

The numbered build scripts build whatever a generated tree holds; these entry points build, deploy and read exactly
what `build/scripts/tools/consumer_targets.txt` says is active, and refuse a tree or a directory that disagrees with
it - so a stale project, a leftover binary of a retired target or a binary that reaches the engine its class
excludes cannot pass for the active set.

`build` checks the generated tree against the registry first and stops on any finding, then builds the active
targets of the main workspace, and with `--tests` of the test workspace, in dependency order: one `make` per
workspace on Linux, one `msbuild` per solution on Windows. The third-party workspace is the numbered `03*` scripts'.

`deploy` copies a package profile's targets, or the named ones, from the configuration's output directory into a
clean deployment directory - `deploy/<profile or targets>/<toolchain>/<configuration>` unless one is named - with
every library their import closure reaches that the output directory holds, and writes `deploy-manifest.json`: the
commit, the targets, every file's SHA-256 and imports, and the imports left to the system. It refuses a non-empty
directory unless told to replace it, a target that is not active or was never built, and a closure holding an engine
the target's class excludes. The output directory is never a deployment directory, so a stale library there cannot
satisfy a deployed binary.

`outputs` reads a directory of binaries and reports what is stale: an active target's binary whose import closure
holds the engine its class excludes and, for a deployment directory, a file that differs from its record or that the
record does not name. What a generated tree no longer produces at all is `check_deployed_runtimes.py`'s finding.

`invalidate` removes every staged output of a retired target - its binary in both configurations' spellings, its
symbols, its import library and export file - from each configuration's output directory and each toolchain's
library directory, so what it built before cannot pass for current, and the project files an earlier generation
wrote for it, which the generator does not know by name and Premake never deletes. Generating the projects runs it,
and `build` runs it before it builds.

Imports are read with llvm-readobj, through `binary_imports.py`, as the isolation and payload checks read them.

Usage:
    python build/scripts/tools/active_targets.py build (release|debug) [--tests] [--jobs N] [--dry-run]
    python build/scripts/tools/active_targets.py deploy (release|debug) (--profile P | --targets A,B)
                                                  [--destination DIR] [--replace] [--binary-tool TOOL]
    python build/scripts/tools/active_targets.py outputs --binaries DIR [--binary-tool TOOL]
    python build/scripts/tools/active_targets.py invalidate [--dry-run]

Tests: build/scripts/tools/active_targets_test.py
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import binary_imports  # noqa: E402
import consumer_targets  # noqa: E402

REPO_ROOT = consumer_targets.REPO_ROOT
MANIFEST = 'deploy-manifest.json'
# Where a configuration's binaries are, relative to the output directory, besides its root: the libraries the
# deployment scripts bundle beside the product.
LIBRARY_SUBDIRECTORIES = ('', 'lib')


class RefusedError(Exception):
    """The registry, the generated tree or a directory disagrees with what the command was asked to do."""


def host_platform() -> str:
    return 'windows' if os.name == 'nt' else 'linux'


def _toolchain(platform: str) -> str:
    return 'msvc' if platform == 'windows' else 'clang'


# ---- Build.

def build_commands(repo_root, config: str, with_tests: bool, platform: str, jobs: int) -> list[list[str]]:
    """The commands that build the active set, after the generated tree has been held to the registry."""
    repo_root = pathlib.Path(repo_root)
    problems = consumer_targets.check(repo_root)
    if problems:
        raise RefusedError('the generated tree and the registry disagree, so nothing is built:\n  '
                           + '\n  '.join(problems))
    registry = consumer_targets.read_registry(repo_root)
    commands = []
    for workspace in ('main', 'tests') if with_tests else ('main',):
        names = consumer_targets.build_order(registry, workspace)
        if not names:
            continue
        if platform == 'windows':
            solution = (repo_root / 'build/vs2026-msvc' / ('AfterNight.slnx' if workspace == 'main'
                                                          else 'tests/AfterNightTest.slnx'))
            commands.append(['msbuild', str(solution), '/m', '/t:' + ';'.join(names),
                             '/p:Configuration=' + config.capitalize() + ';Platform=x64'])
        else:
            directory = repo_root / 'build/make-linux' / ('' if workspace == 'main' else 'tests')
            commands.append(['make', '-C', str(directory).rstrip('/'), f'-j{jobs}', f'config={config}_x64'] + names)
    return commands


# ---- Imports.

def find_reader(tool: str | None) -> str | None:
    """llvm-readobj, the one named, or a versioned install of it on this host."""
    return binary_imports.find_reader(tool)


def import_reader(tool: str):
    """A reader of one binary's imports through `tool`, which reads each file once however many closures hold it."""
    cache: dict[pathlib.Path, list[str] | None] = {}

    def read(path: pathlib.Path):
        resolved = pathlib.Path(path).resolve()
        if resolved not in cache:
            cache[resolved] = binary_imports.read_imports(tool, pathlib.Path(path))
        return cache[resolved]
    return read


def _index(directory: pathlib.Path) -> dict[str, pathlib.Path]:
    """{file name: path} of the libraries a configuration's output directory holds, root first."""
    index: dict[str, pathlib.Path] = {}
    for subdirectory in LIBRARY_SUBDIRECTORIES:
        base = directory / subdirectory if subdirectory else directory
        if not base.is_dir():
            continue
        for path in sorted(base.iterdir()):
            if path.is_file() and path.name not in index:
                index[path.name] = path
    return index


def _engine_class_of(name: str) -> str | None:
    for engine_class, engine in consumer_targets.ENGINES.items():
        if binary_imports.is_engine_module(name, engine):
            return engine_class
    return None


def _closure(start: pathlib.Path, index: dict[str, pathlib.Path], read_imports):
    """({name: path} of every file the start reaches through the directory, {name: imports}, names left to the system)."""
    reached = {start.name: start}
    imports: dict[str, list[str]] = {}
    system: set[str] = set()
    frontier = [start]
    while frontier:
        current = frontier.pop()
        if current.name in imports:
            continue
        names = read_imports(current)
        imports[current.name] = list(names or [])
        for name in names or ():
            if name in index:
                if name not in reached:
                    reached[name] = index[name]
                    frontier.append(index[name])
            else:
                system.add(name)
    return reached, imports, system


def _generation_findings(entry, reached) -> list[str]:
    problems = []
    for name in sorted(reached):
        engine_class = _engine_class_of(name)
        if engine_class is not None and engine_class in consumer_targets._excluded(entry.engine_class):
            problems.append(f'{entry.name} is class {entry.engine_class} and its import closure holds {name}')
    return problems


def _binary_owners(registry, platform: str) -> dict[str, tuple[str, str]]:
    """{file name: (target, configuration)} for every declared target's binary."""
    owners = {}
    for name, entry in registry.targets.items():
        for configuration, spelled in zip(('release', 'debug'), consumer_targets.binary_names(entry, platform)):
            owners[spelled] = (name, configuration)
    return owners


def _find_binary(directory: pathlib.Path, name: str) -> pathlib.Path | None:
    found = sorted(path for path in directory.rglob(name) if path.is_file() and 'PythonLib' not in path.parts)
    return found[0] if found else None


def _digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# ---- Deploy.

def deploy(repo_root, config: str, platform: str, destination, read_imports, profile: str | None = None,
           targets=None, replace: bool = False, commit: str | None = None) -> dict:
    repo_root = pathlib.Path(repo_root)
    registry = consumer_targets.read_registry(repo_root)
    if registry.problems:
        raise RefusedError('the registry does not read:\n  ' + '\n  '.join(registry.problems))
    if profile is not None:
        if profile not in registry.profiles:
            raise RefusedError(f'{profile} is not a package profile')
        chosen = sorted(name for name, entry in registry.targets.items() if profile in entry.ship)
    else:
        chosen = sorted(targets or ())
    if not chosen:
        raise RefusedError('nothing to deploy: name a profile that ships a target, or the targets')
    source = repo_root / 'bin' / _toolchain(platform) / config
    index = _index(source)
    plan: dict[str, pathlib.Path] = {}
    files: dict[str, dict] = {}
    system: set[str] = set()
    problems = []
    for name in chosen:
        entry = registry.targets.get(name)
        if entry is None:
            problems.append(f'{name} is not declared in the registry')
            continue
        if entry.state != 'active':
            problems.append(f'{name} is {entry.state}, so it is not deployed')
            continue
        names = consumer_targets.binary_names(entry, platform)
        if not names:
            continue
        spelled = names[0] if config == 'release' else names[1]
        binary = index.get(spelled) or _find_binary(source, spelled)
        if binary is None:
            problems.append(f'{name} has no binary {spelled} under {source}; build it first')
            continue
        reached, imports, left = _closure(binary, index, read_imports)
        problems.extend(_generation_findings(entry, reached))
        system |= left
        for library, path in reached.items():
            plan[path.relative_to(source).as_posix()] = path
            files[path.relative_to(source).as_posix()] = {'imports': imports.get(library, [])}
    if problems:
        raise RefusedError('nothing was deployed:\n  ' + '\n  '.join(problems))

    destination = pathlib.Path(destination)
    if destination.exists() and any(destination.iterdir()):
        if not replace:
            raise RefusedError(f'{destination} is not empty; name another directory or replace it')
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for relative, path in sorted(plan.items()):
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        files[relative]['sha256'] = _digest(target)
    manifest = {
        'commit': commit,
        'configuration': config,
        'platform': platform,
        'profile': profile,
        'targets': chosen,
        'source': str(source),
        'files': files,
        'resolvedBySystem': sorted(system - set(plan)),
    }
    (destination / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return manifest


# ---- Invalidation.

# What a build leaves of a target beside its binary on Windows: its symbols, its import library and export file, and
# the incremental linker's state. On Linux the binary's split debug information is the one companion.
WINDOWS_STAGED_SUFFIXES = ('.exe', '.dll', '.pdb', '.lib', '.exp', '.ilk')


# Targets the registry no longer declares whose outputs a host may still hold from a build before they went. A runner
# executes every suite binary it finds beside it, so a retired suite's binary left in place would run as if it were
# current; its outputs are removed. LibXISF was built by the 3rdparty workspace from a vendored tree until OpenXISF took
# its place behind the XISF codec; the deploy scripts copy every library that workspace staged, so its binaries are
# removed there as well as where they were deployed. CFITSIO, and the threads library only it loaded, were deployed
# from vcpkg beside the binaries until OpenFITS took CFITSIO's place behind the FITS codec; their copies are removed
# where they were deployed, so that a packager sweeping the directory ships neither. When the engine became the only
# one, the fixture suite named after the development engine was renamed Test_FixtureEngine and the runner comparing the
# two engines went; a clone generated before keeps their projects, which the build refuses, and their binaries, which
# the runners would execute.
RETIRED_TARGETS = ('AfterNightEngineBenchCompare', 'Test_FixtureNext', 'cfitsio', 'libxisf', 'pthreads')

# The names a retired target's binaries took when they were not its own, as `output` and `debug` gave them.
RETIRED_OUTPUT_NAMES = {'cfitsio': ('cfitsio',), 'libxisf': ('xisf', 'xisfd'), 'pthreads': ('pthreadVC3', 'pthreadVC3d')}


def retired_targets(registry) -> list[str]:
    """Every target named here that the registry does not declare: one it declares again is a target it builds."""
    return sorted(name for name in set(RETIRED_TARGETS) if name not in registry.targets)


def retired_staged_names(name: str) -> set[str]:
    """Every file name a build of a retired target staged, under its output and debug names - the default ones, or
    the ones its binaries took - with a shared library's Linux spelling."""
    stems = RETIRED_OUTPUT_NAMES.get(name, (name, name + '_d'))
    names = {stem + suffix for stem in stems for suffix in WINDOWS_STAGED_SUFFIXES}
    names.update(spelling for stem in stems for spelling in (stem, stem + '.debug', 'lib' + stem + '.so'))
    return names


def _staged_directories(repo_root: pathlib.Path) -> list[pathlib.Path]:
    """Each toolchain's configuration output directories under `bin`, each toolchain's library directory, and the
    configuration directories the 3rdparty workspace stages its libraries and import libraries in."""
    directories = []
    for base, nested in (('bin', ('release', 'debug')), ('lib', ('',)),
                         ('3rdparty/bin', ('release', 'debug')), ('3rdparty/lib', ('release', 'debug'))):
        root = repo_root / base
        if not root.is_dir():
            continue
        for toolchain in sorted(path for path in root.iterdir() if path.is_dir()):
            directories.extend(toolchain / name for name in nested if (toolchain / name).is_dir())
    return directories


def invalidate(repo_root, dry_run: bool = False) -> list[pathlib.Path]:
    """Remove every staged output of a retired target, and return what was (or, dry, would be) removed.

    A retired target is in no generated tree, so nothing rebuilds or deletes what it built before; left in place,
    its binary passes for current, satisfies a loader looking beside it and reaches a packager sweeping the directory.
    Outputs live in each configuration's directory - nested ones, such as the style plugin's, included - and import
    libraries in the toolchain's library directory. The bundled Python tree is not an output of any target.
    """
    repo_root = pathlib.Path(repo_root)
    registry = consumer_targets.read_registry(repo_root)
    names = set()
    for name in retired_targets(registry):
        names |= retired_staged_names(name)
    removed = []
    for directory in _staged_directories(repo_root):
        for path in sorted(directory.rglob('*')):
            if path.name in names and path.is_file() and 'PythonLib' not in path.relative_to(directory).parts:
                removed.append(path)
                if not dry_run:
                    path.unlink()
    return removed


# The trees a generator writes projects into - every one `01-create_project_files` writes, the Clang-on-Windows tree
# included although no script builds it. Premake never deletes a file it no longer writes, so a project written before
# its target was retired stays behind, and a tree that still holds it is refused for naming a target the registry does
# not declare.
GENERATED_TREES = ('build/make-linux', 'build/codelite-linux', 'build/vs2026-msvc', 'build/vs2026-clang')
GENERATED_PROJECT_SUFFIXES = ('.make', '.project', '.vcxproj', '.vcxproj.filters', '.vcxproj.user')


def remove_retired_projects(repo_root, dry_run: bool = False) -> list[pathlib.Path]:
    """Remove the generated project files of every retired target, and return what was (or, dry, would be) removed.

    A retired target is no longer in the registry, so the generator does not know its name and an earlier
    generation's project would stay: on the next build the generated tree names a target the registry does not declare
    and nothing is built.
    """
    repo_root = pathlib.Path(repo_root)
    registry = consumer_targets.read_registry(repo_root)
    names = {name + suffix for name in retired_targets(registry) for suffix in GENERATED_PROJECT_SUFFIXES}
    removed = []
    for tree in GENERATED_TREES:
        root = repo_root / tree
        if not root.is_dir():
            continue
        for path in sorted(root.rglob('*')):
            if path.name in names and path.is_file():
                removed.append(path)
                if not dry_run:
                    path.unlink()
    return removed


# ---- Outputs.

def output_findings(repo_root, directory, platform: str, read_imports) -> tuple[list[str], tuple[int, int]]:
    """(what is stale in `directory`, (binaries of declared targets read, files in their closures))."""
    repo_root = pathlib.Path(repo_root)
    directory = pathlib.Path(directory)
    registry = consumer_targets.read_registry(repo_root)
    problems = list(registry.problems)
    if not directory.is_dir():
        return problems + [f'{directory}: no such directory, so nothing was read'], (0, 0)
    owners = _binary_owners(registry, platform)
    index = _index(directory)
    read = 0
    reached_total: set[str] = set()
    for path in sorted(directory.rglob('*')):
        if not path.is_file() or path.name not in owners or 'PythonLib' in path.parts:
            continue
        name, _configuration = owners[path.name]
        entry = registry.targets[name]
        reached, _imports, _left = _closure(path, index, read_imports)
        read += 1
        reached_total |= set(reached)
        problems.extend(f'{directory}: {path.name}: {finding}' for finding in _generation_findings(entry, reached))
    record = directory / MANIFEST
    if record.is_file():
        files = json.loads(record.read_text(encoding='utf-8')).get('files', {})
        for relative, facts in sorted(files.items()):
            path = directory / relative
            if not path.is_file():
                problems.append(f'{directory}: {relative} is in the deployment record and missing')
            elif _digest(path) != facts.get('sha256'):
                problems.append(f'{directory}: {relative} differs from what the deployment recorded')
        for path in sorted(directory.rglob('*')):
            relative = path.relative_to(directory).as_posix()
            if path.is_file() and relative != MANIFEST and relative not in files:
                problems.append(f'{directory}: {relative} is not in the deployment record')
    return problems, (read, len(reached_total))


# ---- The command line.

def _commit(repo_root: pathlib.Path) -> str | None:
    result = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=repo_root, capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def main(argv=None, read_imports=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('command', choices=('build', 'deploy', 'outputs', 'invalidate'))
    parser.add_argument('config', nargs='?', choices=('release', 'debug'), default='release')
    parser.add_argument('--tests', action='store_true', help='build: the test workspace too')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1, help='build: parallel jobs on Linux')
    parser.add_argument('--dry-run', action='store_true',
                        help='build: print the commands instead of running them; invalidate: remove nothing')
    parser.add_argument('--profile', help='deploy: the package profile whose targets are deployed')
    parser.add_argument('--targets', help='deploy: comma-separated targets to deploy instead of a profile')
    parser.add_argument('--destination', help='deploy: the directory to create (default deploy/<name>/<toolchain>/<config>)')
    parser.add_argument('--replace', action='store_true', help='deploy: remove a non-empty destination first')
    parser.add_argument('--binaries', help='outputs: the directory whose binaries are read')
    parser.add_argument('--binary-tool', help='llvm-readobj, or another reader that prints imports')
    parser.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    arguments = parser.parse_args(argv)
    repo_root = arguments.repo_root.resolve()
    platform = host_platform()

    if read_imports is None and arguments.command in ('deploy', 'outputs'):
        tool = find_reader(arguments.binary_tool)
        if tool is None:
            print('no symbol reader was found; pass --binary-tool <llvm-readobj>')
            return 1
        read_imports = import_reader(tool)

    if arguments.command == 'invalidate':
        removed = invalidate(repo_root, dry_run=arguments.dry_run)
        for path in removed:
            print(path.relative_to(repo_root).as_posix())
        verb = 'would remove' if arguments.dry_run else 'removed'
        print(f'{verb} {len(removed)} staged output(s) of targets the registry does not build')
        projects = remove_retired_projects(repo_root, dry_run=arguments.dry_run)
        for path in projects:
            print(path.relative_to(repo_root).as_posix())
        print(f'{verb} {len(projects)} generated project file(s) of retired targets')
        return 0

    try:
        if arguments.command == 'build':
            commands = build_commands(repo_root, arguments.config, arguments.tests, platform, arguments.jobs)
            if not arguments.dry_run:
                for path in invalidate(repo_root):
                    print(f'removed {path.relative_to(repo_root).as_posix()}, the output of a target the registry '
                          'does not build')
            for command in commands:
                print(' '.join(command), flush=True)
                if not arguments.dry_run:
                    subprocess.run(command, check=True)
            return 0
        if arguments.command == 'deploy':
            if bool(arguments.profile) == bool(arguments.targets):
                parser.error('deploy takes a --profile or --targets, not both and not neither')
            targets = [name.strip() for name in arguments.targets.split(',')] if arguments.targets else None
            label = arguments.profile or 'targets'
            destination = (pathlib.Path(arguments.destination) if arguments.destination
                           else repo_root / 'deploy' / label / _toolchain(platform) / arguments.config)
            manifest = deploy(repo_root, arguments.config, platform, destination, read_imports,
                              profile=arguments.profile, targets=targets, replace=arguments.replace,
                              commit=_commit(repo_root))
            print(f'deployed {len(manifest["targets"])} targets and {len(manifest["files"])} files into '
                  f'{destination}; {len(manifest["resolvedBySystem"])} imports are left to the system')
            return 0
    except RefusedError as error:
        print(error)
        return 1
    except subprocess.CalledProcessError as error:
        print(f'the build stopped: {" ".join(error.cmd)} exited {error.returncode}')
        return 1

    if not arguments.binaries:
        parser.error('outputs takes --binaries <directory>')
    problems, (read, reached) = output_findings(repo_root, arguments.binaries, platform, read_imports)
    for problem in problems:
        print(problem)
    if problems:
        print(f'{len(problems)} stale output(s)')
        return 1
    print(f'{arguments.binaries}: read the binaries of {read} declared targets and {reached} files their imports reach; '
          'nothing is stale')
    return 0


if __name__ == '__main__':
    sys.exit(main())

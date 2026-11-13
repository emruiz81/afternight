#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold every generated target to the registry that says which engine it reaches, whether it is built, and why.

Which targets are built, which of them reach the engine and which must not, what each may compile, and what ships,
are facts the generator, the build scripts, the packagers and every check have to agree on. They are stated per
target in `build/scripts/tools/consumer_targets.txt`, and this tool holds the tree to it.

Three registries, all hand-written, none generated:

- `consumer_targets.txt` - one record per generated target: its workspace, kind and output name; its engine
  class (`none`, or `engine` when its link closure holds the engine); its state (`active` or `opt-in`); the registry
  targets it links; the source roots it compiles and includes from, or the name of an explicit source list; for a
  test, the fixture it builds its inputs through; whether the numbered runners expect it; and the package profiles
  that ship it.
- `isolated_sources.txt` - the explicit source list of an isolated test executable: every translation unit, every
  first-party header the units reach, every stub standing in for a production unit with the reason it is there,
  every include directory, and every third-party library it links that is not a registry target. No pattern, no
  suite glob, and nothing added behind the list's back.
- `package_profiles.txt` - what each package profile is, which one is active, which one the real packagers
  produce, and the payload that is no target's output.

**What `check` holds**, from a checkout and the first generated tree newer than the Premake definitions:

- The registry reads: known fields, valid values, every target once, every dependency declared, and exactly one
  active profile and one the packagers produce.
- The classes agree: a `none` target reaches no engine through its dependencies and an `engine` one reaches it, and a
  fixture matches its target's class.
- Both runners expect exactly the active suites the registry says they run.
- Every source list names files that exist, no pattern, a reason for every stub, every first-party file its
  units reach, and only external libraries the isolated factory knows how to link; and a list of class `none`
  reaches no header of the engine.
- The generated tree agrees: every project is declared and every active target generated; kind, output and
  first-party links match; no `none` target includes a directory, compiles a source or links the library of the
  engine, nor do its sources reach a header of it, even through a relative include; an isolated target compiles
  exactly its directories; and every other target compiles and includes only under the roots it and its
  dependencies declare.

`order` prints the active targets of a workspace in build order, `suites` the suites the runners expect, and
`manifest` a digest of the sources a build is made from, so a measurement can name what it measured: the SHA-256 of
`<blob> <path>` lines, sorted by path, over the roots of every active target and the build definitions, at a
revision.

Usage:
    python build/scripts/tools/consumer_targets.py check [--repo-root <dir>]
    python build/scripts/tools/consumer_targets.py order [main|tests|3rdparty] [--repo-root <dir>]
    python build/scripts/tools/consumer_targets.py suites [--repo-root <dir>]
    python build/scripts/tools/consumer_targets.py manifest [--rev <revision>] [--repo-root <dir>]

Tests: build/scripts/tools/consumer_targets_test.py
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import hashlib
import os
import pathlib
import posixpath
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_build_graph as build_graph  # noqa: E402
import consumer_source  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
REGISTRY = 'build/scripts/tools/consumer_targets.txt'
SOURCE_LISTS = 'build/scripts/tools/isolated_sources.txt'
PROFILES = 'build/scripts/tools/package_profiles.txt'
RUNNERS = ('build/scripts/linux/run_tests.sh', 'build/scripts/msvc/run_tests.bat')
# The inputs every build reads besides the sources of its targets, so every manifest covers them.
BUILD_DEFINITIONS = ('build/premake', 'build/scripts/tools', 'python')

NONE = '-'
WORKSPACES = ('main', 'tests', '3rdparty')
KINDS = ('shared', 'static', 'console', 'windowed')
CLASSES = ('none', 'engine')
STATES = ('active', 'opt-in')
# How a test gets its image inputs: it builds none, it builds them through the engine it tests, it takes them from the
# engine-neutral fixture layer, or it builds them through the engine's fixture adapter (src/AfterNightTests/fixtures/
# engine).
FIXTURES = ('none', 'engine', 'neutral', 'adapter')
RUNNER_VALUES = ('product', NONE)
FORMS = ('installer', 'deployment', 'none')

# Which engine each class reaches, and where the engine's tree is. The engine is the only target whose own class is
# not derived from what it links.
ENGINES = {
    'engine': 'AfterNightEngine',
}
ENGINE_TREES = {
    'engine': 'src/AfterNightEngine',
}
# The directories the isolated factory adds for a class, beside what its list names.
CLASS_INCLUDES = {
    'none': (),
    'engine': ('src/AfterNightEngine/include',),
}
# The neutral layer reaches no engine and the fixture adapter reaches the engine, so each belongs to exactly that
# class.
FIXTURE_CLASS = {
    'neutral': 'none',
    'adapter': 'engine',
}

TARGET_FIELDS = {'workspace', 'kind', 'output', 'debug', 'class', 'state', 'depends', 'sources', 'fixture', 'runner',
                 'ship', 'note'}
TARGET_REQUIRED = ('workspace', 'kind', 'class', 'state', 'depends', 'sources')
PROFILE_FIELDS = {'title', 'form', 'active', 'packagers', 'ship-windows', 'ship-linux', 'keep-out-windows',
                  'keep-out-linux', 'separate-linux', 'note'}
PROFILE_REQUIRED = ('title', 'form')
SOURCE_KINDS = ('unit', 'header', 'stub', 'include', 'define', 'link', 'external', 'qt')
# The third-party libraries a source list's `external` entry may name: those the isolated factory in
# build/premake/afternight-test-pm5.lua knows the include directory, macros and link line of; oneTBB adds a link line
# on Linux only, because MSVC links it through its own header; libsodium and zstd sign and compress the extension
# repository assets the extension manager's cases build.
KNOWN_EXTERNALS = ('kddockwidgets', 'python', 'sodium', 'tbb', 'zstd')
# Every test the factory generates is compiled with this defined, as every suite of the test workspace is.
FACTORY_DEFINES = ('AFTERNIGHT_TESTS',)
PLATFORMS = ('linux', 'windows')
COMPILED_SUFFIXES = ('.cpp', '.c', '.cc')
# The fuzz harness's standalone driver, which supplies `main` where libFuzzer does not; the generator leaves it out of
# a libFuzzer generation (build/premake/afternight-test-pm5.lua).
FUZZ_DRIVER = 'src/AfterNightTests/FuzzEngine/FuzzDriver.cpp'


# ---- The three registries.

@dataclasses.dataclass(frozen=True)
class TargetEntry:
    """One record of consumer_targets.txt."""

    name: str
    workspace: str
    kind: str
    output: str
    debug_output: str
    engine_class: str
    state: str
    depends: tuple[str, ...]
    roots: tuple[str, ...]
    source_list: str | None
    fixture: str
    runner: str
    ship: tuple[str, ...]
    line: int

    @property
    def is_test(self) -> bool:
        return self.workspace == 'tests' and self.kind in ('console', 'windowed')


@dataclasses.dataclass(frozen=True)
class SourceList:
    """One record of isolated_sources.txt: an isolated executable's explicit sources."""

    name: str
    units: tuple[tuple[str, str], ...]
    headers: tuple[str, ...]
    stubs: tuple[tuple[str, str], ...]
    includes: tuple[str, ...]
    defines: tuple[str, ...]
    line: int
    # The registry libraries the executable links beyond its class's engine, and the Qt modules it uses.
    links: tuple[str, ...] = ()
    qt: tuple[str, ...] = ()
    # The third-party libraries it links that are not registry targets.
    externals: tuple[str, ...] = ()

    def compiled(self, platform: str) -> set[str]:
        """The translation units, stubs included, that a tree for `platform` compiles."""
        chosen = {path for path, only in self.units if only in ('', platform)}
        chosen.update(path for path, _ in self.stubs)
        return chosen

    def files(self) -> set[str]:
        return {path for path, _ in self.units} | set(self.headers) | {path for path, _ in self.stubs}


@dataclasses.dataclass(frozen=True)
class Profile:
    """One record of package_profiles.txt."""

    name: str
    title: str
    form: str
    active: bool
    packagers: bool
    ship: dict[str, tuple[str, ...]]
    keep_out: dict[str, tuple[str, ...]]
    separate_linux: tuple[str, ...]
    line: int


@dataclasses.dataclass
class Registry:
    targets: dict[str, TargetEntry]
    sources: dict[str, SourceList]
    profiles: dict[str, Profile]
    problems: list[str]

    def active_profile(self) -> Profile | None:
        found = [profile for profile in self.profiles.values() if profile.active]
        return found[0] if len(found) == 1 else None

    def packager_profile(self) -> Profile | None:
        found = [profile for profile in self.profiles.values() if profile.packagers]
        return found[0] if len(found) == 1 else None


def _records(text: str, keyword: str, relative: str, problems: list[str]):
    """(name, {field: [values]}, line) for every record of a file in the registries' shared format.

    A record opens with `<keyword> <name>` at the start of a line; each field is an indented `<field> <value>` line,
    and a line indented further than a field continues its value. Comments and blank lines are ignored.
    """
    record = None
    last_field = None
    field_indent = 0
    for number, raw in enumerate(text.replace('\r\n', '\n').split('\n'), start=1):
        if not raw.strip() or raw.lstrip().startswith('#'):
            continue
        indent = len(raw) - len(raw.lstrip())
        if indent == 0:
            if record is not None:
                yield record
            words = raw.split()
            if words[0] != keyword or len(words) != 2:
                problems.append(f'{relative}:{number}: expected "{keyword} <name>", found {raw.strip()!r}')
                record = None
                continue
            record = (words[1], collections.OrderedDict(), number)
            last_field = None
            continue
        if record is None:
            problems.append(f'{relative}:{number}: a field outside any record')
            continue
        if last_field is not None and indent > field_indent:
            record[1][last_field][-1] += ' ' + raw.strip()
            continue
        parts = raw.strip().split(None, 1)
        last_field = parts[0]
        field_indent = indent
        record[1].setdefault(last_field, []).append(parts[1].strip() if len(parts) > 1 else '')
    if record is not None:
        yield record


def _list(value: str) -> tuple[str, ...]:
    if value.strip() == NONE:
        return ()
    return tuple(part.strip() for part in value.split(',') if part.strip())


def _parse_targets(text: str, problems: list[str]) -> dict[str, TargetEntry]:
    entries: dict[str, TargetEntry] = {}
    for name, fields, line in _records(text, 'target', REGISTRY, problems):
        where = f'{REGISTRY}:{line}: {name}'
        for field, values in fields.items():
            if field not in TARGET_FIELDS:
                problems.append(f'{where} has a field "{field}" the registry does not define')
            elif len(values) > 1 and field != 'note':
                problems.append(f'{where} states {field} twice')
        missing = [field for field in TARGET_REQUIRED if field not in fields]
        for field in missing:
            problems.append(f'{name} states no {field}')
        if missing:
            continue
        value = {field: values[0] for field, values in fields.items()}
        invalid = False
        for field, allowed in (('workspace', WORKSPACES), ('kind', KINDS), ('class', CLASSES), ('state', STATES),
                               ('fixture', FIXTURES + (NONE,)), ('runner', RUNNER_VALUES)):
            if field in value and value[field] not in allowed:
                problems.append(f'{where} has {field} {value[field]!r}, which is not one of {", ".join(allowed)}')
                invalid = True
        if invalid:
            continue
        sources = value['sources'].strip()
        source_list = sources[1:] if sources.startswith('@') else None
        output = value.get('output', name)
        if name in entries:
            problems.append(f'{name} is declared twice, at lines {entries[name].line} and {line}')
            continue
        entries[name] = TargetEntry(
            name=name, workspace=value['workspace'], kind=value['kind'], output=output,
            debug_output=value.get('debug', output + '_d'), engine_class=value['class'], state=value['state'],
            depends=_list(value['depends']), roots=() if source_list else _list(sources), source_list=source_list,
            fixture=value.get('fixture', NONE), runner=value.get('runner', NONE), ship=_list(value.get('ship', NONE)),
            line=line)
    return entries


def _parse_sources(text: str, problems: list[str]) -> dict[str, SourceList]:
    lists: dict[str, SourceList] = {}
    for name, fields, line in _records(text, 'sources', SOURCE_LISTS, problems):
        where = f'{SOURCE_LISTS}:{line}: {name}'
        units, headers, stubs, includes, defines, links, externals, qt = [], [], [], [], [], [], [], []
        for field, values in fields.items():
            if field not in SOURCE_KINDS:
                problems.append(f'{where} has an entry "{field}" a source list does not define')
                continue
            for entry in values:
                words = entry.split()
                if not words:
                    problems.append(f'{where} has an empty {field}')
                    continue
                if field == 'qt':
                    for module in words:
                        if not re.fullmatch(r'[a-z][a-z0-9]*', module):
                            problems.append(f'{where} names the Qt module {module!r}, which is not one lower-case word')
                        else:
                            qt.append(module)
                    continue
                path = words[0].rstrip('/')
                if any(character in path for character in '*?['):
                    problems.append(f'{where} names files by pattern in {path}; a source list names each file')
                    continue
                if field == 'unit':
                    only = ''
                    for word in words[1:]:
                        if word.startswith('only=') and word[5:] in PLATFORMS:
                            only = word[5:]
                        else:
                            problems.append(f'{where}: {path} carries {word!r}, which is not only=linux or '
                                            'only=windows')
                    units.append((path, only))
                elif field == 'stub':
                    reason = ' '.join(words[1:])
                    if not reason:
                        problems.append(f'{where}: the stub {path} states no reason')
                    if not path.startswith('src/AfterNightTests/'):
                        problems.append(f'{where}: the stub {path} is outside the test tree; a stub belongs under '
                                        'src/AfterNightTests/')
                    stubs.append((path, reason))
                elif field == 'header':
                    headers.append(path)
                elif field == 'define':
                    if not re.fullmatch(r'[A-Za-z_]\w*', path):
                        problems.append(f'{where} defines {path!r}, which is not a macro name')
                    defines.append(path)
                elif field == 'link':
                    links.append(path)
                elif field == 'external':
                    if path not in KNOWN_EXTERNALS:
                        problems.append(f'{where} links the external library {path!r}, which the isolated factory '
                                        f'does not know; it knows {", ".join(KNOWN_EXTERNALS)}')
                    externals.append(path)
                else:
                    includes.append(path)
        if name in lists:
            problems.append(f'{SOURCE_LISTS}: the source list {name} is declared twice')
            continue
        lists[name] = SourceList(name, tuple(units), tuple(headers), tuple(stubs), tuple(includes), tuple(defines),
                                 line, tuple(links), tuple(qt), tuple(externals))
    return lists


def _parse_profiles(text: str, problems: list[str]) -> dict[str, Profile]:
    profiles: dict[str, Profile] = {}
    for name, fields, line in _records(text, 'profile', PROFILES, problems):
        where = f'{PROFILES}:{line}: {name}'
        for field in fields:
            if field not in PROFILE_FIELDS:
                problems.append(f'{where} has a field "{field}" a profile does not define')
        missing = [field for field in PROFILE_REQUIRED if field not in fields]
        for field in missing:
            problems.append(f'the package profile {name} states no {field}')
        if missing:
            continue
        value = {field: ' '.join(values) if field.startswith(('ship-', 'keep-out-', 'separate-')) else values[0]
                 for field, values in fields.items()}
        if value['form'] not in FORMS:
            problems.append(f'{where} has form {value["form"]!r}, which is not one of {", ".join(FORMS)}')
        profiles[name] = Profile(
            name=name, title=value['title'], form=value['form'],
            active=value.get('active', 'no') == 'yes', packagers=value.get('packagers', 'no') == 'yes',
            ship={platform: _list(value.get('ship-' + platform, NONE)) for platform in PLATFORMS},
            keep_out={platform: _list(value.get('keep-out-' + platform, NONE)) for platform in PLATFORMS},
            separate_linux=_list(value.get('separate-linux', NONE)), line=line)
    return profiles


def read_registry(repo_root: pathlib.Path = REPO_ROOT) -> Registry:
    """The three registries of the checkout at repo_root, with every problem found reading them."""
    problems: list[str] = []
    texts = {}
    for relative in (REGISTRY, SOURCE_LISTS, PROFILES):
        path = pathlib.Path(repo_root) / relative
        if not path.is_file():
            problems.append(f'{relative} does not exist')
            texts[relative] = ''
        else:
            texts[relative] = path.read_bytes().decode('utf-8')
    return Registry(_parse_targets(texts[REGISTRY], problems), _parse_sources(texts[SOURCE_LISTS], problems),
                    _parse_profiles(texts[PROFILES], problems), problems)


# ---- Rules the registry holds on its own.

def _closure(registry: Registry, name: str) -> set[str]:
    """Every registry target `name` links, directly or through what those link; not `name` itself."""
    seen: set[str] = set()
    frontier = list(registry.targets[name].depends) if name in registry.targets else []
    while frontier:
        current = frontier.pop()
        if current in seen or current not in registry.targets:
            continue
        seen.add(current)
        frontier.extend(registry.targets[current].depends)
    return seen


def engines_reached(registry: Registry, name: str) -> set[str]:
    """The engine classes a target's link closure holds; an engine reaches its own class."""
    reached = {engine_class for engine_class, engine in ENGINES.items() if engine == name}
    for other in _closure(registry, name):
        reached.update(engine_class for engine_class, engine in ENGINES.items() if engine == other)
    return reached


def registry_findings(registry: Registry, repo_root: pathlib.Path) -> list[str]:
    problems = list(registry.problems)
    targets = registry.targets
    for entry in targets.values():
        for dependency in entry.depends:
            if dependency not in targets:
                problems.append(f'{entry.name} depends on {dependency}, which the registry does not declare')
            elif dependency == entry.name:
                problems.append(f'{entry.name} depends on itself')
        if entry.fixture != NONE and not entry.is_test:
            problems.append(f'{entry.name} is not a test and names a fixture')
        if entry.is_test and entry.fixture == NONE:
            problems.append(f'{entry.name} is a test and names no fixture; use none when it builds no image input')
        expected_class = FIXTURE_CLASS.get(entry.fixture)
        if expected_class and expected_class != entry.engine_class:
            problems.append(f'{entry.name} has the {entry.fixture} fixture, which belongs to class {expected_class}, and '
                            f'is class {entry.engine_class}')
        if entry.fixture == 'engine' and entry.engine_class == 'none':
            problems.append(f'{entry.name} builds its fixtures through an engine and is class none')
        if entry.runner == 'product' and not entry.is_test:
            problems.append(f'{entry.name} is expected by the runners and is not a test')
        if entry.source_list is not None and entry.source_list not in registry.sources:
            problems.append(f'{entry.name} compiles the source list {entry.source_list}, which '
                            f'{SOURCE_LISTS} does not declare')
        elif entry.source_list is not None:
            # A library the list links is one the generated project links, so the record has to say so too.
            for library in registry.sources[entry.source_list].links:
                if library not in targets:
                    problems.append(f'{entry.name}: the source list links {library}, which the registry does not '
                                    'declare')
                elif targets[library].kind not in ('shared', 'static'):
                    problems.append(f'{entry.name}: the source list links {library}, which is not a library')
                elif library not in entry.depends:
                    problems.append(f'{entry.name} links {library} through its source list, and its record does not '
                                    'depend on it')
        for root in entry.roots:
            if not (pathlib.Path(repo_root) / root).exists():
                problems.append(f'{entry.name} names the source root {root}, which does not exist')
        for profile in entry.ship:
            if profile not in registry.profiles:
                problems.append(f'{entry.name} ships in {profile}, which {PROFILES} does not declare')

    live = set(targets)
    for name in sorted(live):
        entry = targets[name]
        reached = engines_reached(registry, name)
        if entry.engine_class == 'none':
            for engine_class in sorted(reached):
                problems.append(f'{name} is class none and reaches {ENGINES[engine_class]}')
        else:
            if entry.engine_class not in reached:
                problems.append(f'{name} is class {entry.engine_class} and reaches no engine')

    used = collections.Counter(entry.source_list for entry in targets.values() if entry.source_list)
    for name in sorted(registry.sources):
        if used[name] == 0:
            problems.append(f'{name} is a source list no target compiles')
        elif used[name] > 1:
            problems.append(f'{name} is a source list {used[name]} targets compile; each isolated target has its own')

    active = [profile.name for profile in registry.profiles.values() if profile.active]
    if len(active) != 1:
        problems.append(f'{len(active)} package profiles are active in {PROFILES}; exactly one is')
    packaged = [profile.name for profile in registry.profiles.values() if profile.packagers]
    if len(packaged) != 1:
        problems.append(f'{len(packaged)} package profiles are marked as what the packagers produce; exactly one is')
    for profile in registry.profiles.values():
        shipped = [entry for entry in targets.values() if profile.name in entry.ship]
        if profile.form == 'none' and shipped:
            problems.append(f'the package profile {profile.name} ships nothing and {shipped[0].name} names it')
    return problems


def runner_suites(registry: Registry) -> list[str]:
    """The suites the numbered runners expect: the active tests the registry says they run."""
    return sorted(name for name, entry in registry.targets.items()
                  if entry.runner == 'product' and entry.state == 'active')


def _runner_list(relative: str, text: str) -> list[str] | None:
    if relative.endswith('.sh'):
        match = re.search(r'expected_tests=\((.*?)\)', text, re.DOTALL)
        return match.group(1).split() if match else None
    match = re.search(r'set "TEST_NAMES=([^"]*)"', text)
    return match.group(1).split() if match else None


def runner_findings(registry: Registry, repo_root: pathlib.Path) -> list[str]:
    expected = set(runner_suites(registry))
    problems = []
    for relative in RUNNERS:
        name = pathlib.PurePosixPath(relative).name
        path = pathlib.Path(repo_root) / relative
        listed = _runner_list(relative, path.read_bytes().decode('utf-8')) if path.is_file() else None
        if listed is None:
            problems.append(f'{relative} states no list of the suites it expects')
            continue
        for suite in sorted(expected - set(listed)):
            problems.append(f'{name} does not expect {suite}, which the registry says the runners run')
        for suite in sorted(set(listed) - expected):
            state = registry.targets[suite].state if suite in registry.targets else 'not declared'
            problems.append(f'{name} expects {suite}, which is {state} or not a runner suite in the registry')
    return problems


# ---- Include closures.

_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.MULTILINE)
_CONDITIONAL = re.compile(r'^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
_DEFINED = re.compile(r'^(!?)\s*defined\s*(?:\(\s*(\w+)\s*\)|\s+(\w+))$')


def _decided(directive: str, condition: str, defined: frozenset[str]) -> bool | None:
    """Whether a conditional's branch is taken, when a macro known to be defined decides it; None otherwise.

    Only a macro the build is known to define decides anything. That it is absent from the set says nothing -
    the compiler, the platform or a header may define it - so such a branch is read, along with its alternative.
    """
    condition = condition.strip()
    if directive == 'ifdef':
        return True if condition in defined else None
    if directive == 'ifndef':
        return False if condition in defined else None
    match = _DEFINED.match(condition)
    if match is None or (match.group(2) or match.group(3)) not in defined:
        return None
    return match.group(1) != '!'


def active_text(text: str, defined) -> str:
    """The text with every branch a known-defined macro rules out blanked, line count preserved."""
    defined = frozenset(defined)
    if not defined:
        return text
    stack: list[dict[str, bool]] = []
    lines = []
    for line in text.split('\n'):
        match = _CONDITIONAL.match(line)
        if match:
            directive, condition = match.group(1), match.group(2)
            if directive in ('if', 'ifdef', 'ifndef'):
                value = _decided(directive, condition, defined)
                stack.append({'active': value is not False, 'settled': value is True})
            elif directive == 'elif' and stack:
                if stack[-1]['settled']:
                    stack[-1]['active'] = False
                else:
                    value = _decided('if', condition, defined)
                    stack[-1]['active'] = value is not False
                    stack[-1]['settled'] = value is True
            elif directive == 'else' and stack:
                stack[-1]['active'] = not stack[-1]['settled']
            elif directive == 'endif' and stack:
                stack.pop()
        lines.append(line if all(frame['active'] for frame in stack) else '')
    return '\n'.join(lines)


class IncludeReader:
    """Reads include lines once per file and resolves them the way a compiler searches.

    `defined` names macros the build is known to define; a branch one of them rules out is not read.
    """

    def __init__(self, repo_root: pathlib.Path, defined=()):
        self.repo_root = pathlib.Path(repo_root)
        self.defined = frozenset(defined)
        self._includes: dict[str, list[tuple[str, str]]] = {}

    def includes(self, path: str) -> list[tuple[str, str]]:
        if path not in self._includes:
            full = self.repo_root / path
            text = consumer_source.without_comments(consumer_source.read_text(full)) if full.is_file() else ''
            text = active_text(text, self.defined)
            self._includes[path] = [(match.group(1), match.group(2).strip()) for match in _INCLUDE.finditer(text)]
        return self._includes[path]

    def resolve(self, including: str, bracket: str, name: str, dirs) -> str | None:
        bases = ((str(pathlib.PurePosixPath(including).parent),) if bracket == '"' else ()) + tuple(dirs)
        for base in bases:
            # Repository paths are forward-slashed on every host; the host's own normpath would turn them
            # into backslashes on Windows, and nothing would resolve there.
            candidate = posixpath.normpath(posixpath.join(base, name.replace('\\', '/')))
            if not candidate.startswith('..') and (self.repo_root / candidate).is_file():
                return candidate
        return None

    def closure(self, start: str, dirs, unresolved: list[tuple[str, str]] | None = None) -> tuple[set[str], set[str]]:
        """(first-party files outside the engine's tree, files of the engine's tree) that `start` reaches.

        Each include nothing in `dirs` resolves is appended to `unresolved`, when given, as (including file, name).
        """
        files = {start}
        engine_files: set[str] = set()
        queue = collections.deque([start])
        while queue:
            current = queue.popleft()
            for bracket, name in self.includes(current):
                resolved = self.resolve(current, bracket, name, dirs)
                if resolved is None and unresolved is not None:
                    unresolved.append((current, name))
                if resolved is None or not resolved.startswith('src/'):
                    continue
                if _engine_of(resolved) is not None:
                    engine_files.add(resolved)
                    continue
                if resolved not in files:
                    files.add(resolved)
                    queue.append(resolved)
        return files, engine_files


def _under(path: str, root: str) -> bool:
    return path == root or path.startswith(root.rstrip('/') + '/')


def _engine_of(path: str) -> str | None:
    """The engine class whose tree holds `path`, by whole path component."""
    for engine_class, tree in ENGINE_TREES.items():
        if _under(path, tree):
            return engine_class
    return None


def _excluded(engine_class: str) -> tuple[str, ...]:
    """The engine classes a target of `engine_class` may reach nothing of."""
    return {'none': ('engine',), 'engine': ()}[engine_class]


# The directory the engine's own headers are included from: its published surface, included by its
# `AfterNightEngine/Area/Name.h` path.
ENGINE_INCLUDE_ROOTS = {
    'engine': 'src/AfterNightEngine/include',
}


def _engine_header_named(repo_root: pathlib.Path, engine_class: str, name: str) -> str | None:
    """The header of that engine an include of `name` would reach once the engine's directory were on the path."""
    candidate = posixpath.normpath(posixpath.join(ENGINE_INCLUDE_ROOTS[engine_class], name.replace('\\', '/')))
    if candidate.startswith('..') or not _under(candidate, ENGINE_INCLUDE_ROOTS[engine_class]):
        return None
    return candidate if (pathlib.Path(repo_root) / candidate).is_file() else None


def source_list_findings(registry: Registry, repo_root: pathlib.Path) -> list[str]:
    problems = []
    owner = {entry.source_list: entry for entry in registry.targets.values() if entry.source_list}
    for name, listing in sorted(registry.sources.items()):
        reader = IncludeReader(repo_root, FACTORY_DEFINES + listing.defines)
        for path in sorted(listing.files()):
            if not (pathlib.Path(repo_root) / path).is_file():
                problems.append(f'{name}: {path} does not exist')
        for directory in listing.includes:
            if not (pathlib.Path(repo_root) / directory).is_dir():
                problems.append(f'{name}: the include directory {directory} does not exist')
        entry = owner.get(name)
        engine_class = entry.engine_class if entry else 'none'
        dirs = tuple(listing.includes) + CLASS_INCLUDES.get(engine_class, ())
        named = listing.files()
        reported: set[str] = set()
        for unit in sorted({path for path, _ in listing.units} | {path for path, _ in listing.stubs}):
            if not (pathlib.Path(repo_root) / unit).is_file():
                continue
            unresolved: list[tuple[str, str]] = []
            files, engine_files = reader.closure(unit, dirs, unresolved)
            for path in sorted(files - named):
                if path not in reported:
                    reported.add(path)
                    problems.append(f'{name}: {unit} reaches {path}, which the list does not name')
            for path in sorted(engine_files):
                if _engine_of(path) in _excluded(engine_class) and path not in reported:
                    reported.add(path)
                    problems.append(f'{name} is class {engine_class} and its sources reach {path}')
            # An include nothing the list names resolves, written as a header of an engine the class excludes is
            # included - `<AfterNightEngine/Core/Status.h>` - reaches that engine as surely as a path does: the
            # compiler finds it the moment the engine's directory is on the path.
            for including, included in unresolved:
                for excluded in _excluded(engine_class):
                    header = _engine_header_named(repo_root, excluded, included)
                    if header is not None and (including, header) not in reported:
                        reported.add((including, header))
                        problems.append(f'{name} is class {engine_class} and {including} includes {included}, '
                                        f'which is {header} of an engine it excludes')
    return problems


# ---- The generated tree.

def _tree_platform(label: str) -> str:
    return 'linux' if label.startswith('make') else 'windows'


def _repository_path(repo_root: pathlib.Path, base: str, entry: str) -> str | None:
    entry = entry.strip().strip('"')
    if not entry or '$' in entry or '%' in entry:
        return None
    path = os.path.normpath(os.path.join(base, entry.replace('\\', '/')))
    relative = os.path.relpath(path, str(repo_root))
    if relative.startswith('..'):
        return None
    return pathlib.PurePath(relative).as_posix()


def current_tree(repo_root: pathlib.Path):
    """(label, [main, tests, 3rdparty workspaces]) of the first tree newer than the Premake definitions, or None."""
    definitions = newest_definition_mtime(repo_root)
    for label, relative, kind in build_graph.GENERATED_TREES:
        workspaces = build_graph.load_tree(str(repo_root), label, relative, kind)
        if not workspaces or not any(workspace.targets for workspace in workspaces):
            continue
        directory = os.path.join(str(repo_root), relative)
        if definitions > build_graph.generation_mtime(directory, workspaces):
            continue
        loader = build_graph.load_vs_workspace if kind == 'vs' else build_graph.load_make_workspace
        third = loader(os.path.join(directory, '3rdparty'), '%s/3rdparty' % label, ())
        return label, workspaces + ([third] if third is not None and third.targets else [])
    return None


def newest_definition_mtime(repo_root: pathlib.Path) -> float:
    """The Premake definitions and the registries they read: a tree older than any of them is stale."""
    newest = build_graph.newest_definition_mtime(str(repo_root))
    for relative in (REGISTRY, SOURCE_LISTS):
        path = pathlib.Path(repo_root) / relative
        if path.is_file():
            newest = max(newest, path.stat().st_mtime)
    return newest


def _workspace_name(label: str) -> str:
    return label.rsplit('/', 1)[-1]


def _generated_kind(target) -> str:
    if build_graph.produces_static_library(target):
        return 'static'
    names = [output for output in target.outputs] + [path for kind, path in target.artifacts
                                                     if kind in ('target binary', 'target binary name')]
    if any(name.lower().endswith(('.so', '.dll')) for name in names):
        return 'shared'
    return 'executable'


# The suffix a sanitizer generation gives a static library (afternight-common.lua's applyEngineSanitizer), so that a tree
# switched back to an ordinary generation cannot link an instrumented one; the library is still its target's.
_INSTRUMENTED_SUFFIX = re.compile(r'-sanitize-(?:address|thread|fuzzer)$', re.IGNORECASE)


def _output_stem(output: str) -> str:
    stem = re.split(r'[\\/]', output)[-1]
    for suffix in ('.so', '.a', '.dll', '.lib', '.exe'):
        if stem.lower().endswith(suffix):
            stem = stem[:-len(suffix)]
    if stem.startswith('lib'):
        stem = stem[3:]
    return _INSTRUMENTED_SUFFIX.sub('', stem)


def _linked_targets(registry: Registry, generated) -> set[str]:
    """The registry targets whose library a generated project names on its link line, by output name."""
    by_output: dict[str, str] = {}
    for name, entry in registry.targets.items():
        if entry.kind in ('shared', 'static'):
            for spelled in (entry.output, entry.debug_output):
                by_output[spelled.lower()] = name
                by_output[_output_stem(spelled).lower()] = name
    linked = set()
    for link in generated.links:
        text = link.strip()
        if text.startswith('-l'):
            text = text[2:]
        text = re.split(r'[\\/]', text)[-1]
        for suffix in ('.lib', '.a', '.so'):
            if text.lower().endswith(suffix):
                text = text[:-len(suffix)]
        text = _INSTRUMENTED_SUFFIX.sub('', text)
        candidates = [text, text[3:] if text.startswith('lib') else text]
        for candidate in candidates:
            if candidate.lower() in by_output:
                linked.add(by_output[candidate.lower()])
                break
    return linked


def generated_findings(registry: Registry, label: str, workspaces, repo_root: pathlib.Path) -> list[str]:
    problems = []
    platform = _tree_platform(label)
    generated: dict[str, tuple[str, object]] = {}
    for workspace in workspaces:
        for name, target in workspace.targets.items():
            generated[name] = (_workspace_name(workspace.label), target)
    present_workspaces = {_workspace_name(workspace.label) for workspace in workspaces}
    reader = IncludeReader(repo_root)

    for name in sorted(generated):
        workspace, target = generated[name]
        entry = registry.targets.get(name)
        if entry is None:
            problems.append(f'{label}: {name} is generated and consumer_targets.txt does not declare it')
            continue
        if entry.workspace != workspace:
            problems.append(f'{label}: {name} is generated in the {workspace} workspace and declared in '
                            f'{entry.workspace}')
    for name, entry in sorted(registry.targets.items()):
        if entry.state == 'active' and name not in generated and entry.workspace in present_workspaces:
            problems.append(f'{label}: {name} is active and not generated')

    # What each project links of the registry's targets, and what that reaches in this tree. A platform may leave a
    # library to the one that uses it - the engine's import library carries the codecs it links on Windows, where its
    # consumers name only the engine - so a declared dependency is honoured by the closure, not only the line.
    linked_by = {name: _linked_targets(registry, target) - {name} for name, (_, target) in generated.items()}

    def link_closure(start: str) -> set[str]:
        seen: set[str] = set()
        frontier = list(linked_by.get(start, ()))
        while frontier:
            current = frontier.pop()
            if current not in seen:
                seen.add(current)
                frontier.extend(linked_by.get(current, ()))
        return seen

    for name in sorted(generated):
        workspace, target = generated[name]
        entry = registry.targets.get(name)
        if entry is None:
            continue
        base = os.path.dirname(os.path.abspath(target.location))
        # Kind and output.
        kind = _generated_kind(target)
        declared = 'executable' if entry.kind in ('console', 'windowed') else entry.kind
        if kind != declared:
            problems.append(f'{label}: {name} is declared {entry.kind} and generated as '
                            f'{"an executable" if kind == "executable" else "a " + kind + " library"}')
        stems = {_output_stem(output) for output in target.outputs}
        expected = {_output_stem(entry.output), _output_stem(entry.debug_output)}
        if stems and not stems <= expected:
            # The shortest unexpected name is the release spelling; its debug spelling adds nothing to the finding.
            problems.append(f'{label}: {name} produces {min(stems - expected, key=len)}, and the registry says '
                            f'{entry.output}')
        # First-party links: every one the line names is declared, and every declared one is in the link closure.
        linked = linked_by[name]
        for other in sorted(linked - set(entry.depends)):
            problems.append(f'{label}: {name} links {other}, which its depends does not name')
        reached = link_closure(name)
        for other in sorted(set(entry.depends) - reached):
            if other in registry.targets and registry.targets[other].kind in ('shared', 'static'):
                problems.append(f'{label}: {name} names {other} in depends and does not link it, directly or through '
                                'what it links')

        includes = [path for path in (_repository_path(repo_root, base, directory)
                                      for directory in target.include_dirs) if path]
        sources = [path for path in (_repository_path(repo_root, base, source) for source in target.sources) if path]
        sources = [path for path in sources if not path.startswith('obj/')]
        # What a class excludes, read directly.
        if name not in ENGINES.values():
            carriers = [(name, includes, sources)]
            for library in build_graph.linked_static_libraries(
                    next(w for w in workspaces if name in w.targets), name):
                other = next(w for w in workspaces if library in w.targets).targets[library]
                other_base = os.path.dirname(os.path.abspath(other.location))
                carriers.append((library,
                                 [p for p in (_repository_path(repo_root, other_base, d) for d in other.include_dirs)
                                  if p],
                                 [p for p in (_repository_path(repo_root, other_base, s) for s in other.sources)
                                  if p]))
            for carrier, carried_includes, carried_sources in carriers:
                through = '' if carrier == name else f' through the static library {carrier}'
                for directory in carried_includes:
                    if _engine_of(directory) in _excluded(entry.engine_class):
                        problems.append(f'{label}: {name} is class {entry.engine_class} and includes {directory}'
                                        f'{through}')
                for source in carried_sources:
                    if _engine_of(source) in _excluded(entry.engine_class):
                        problems.append(f'{label}: {name} is class {entry.engine_class} and compiles {source}'
                                        f'{through}')

        if entry.source_list is not None:
            listing = registry.sources.get(entry.source_list)
            if listing is None:
                continue
            compiled = {path for path in sources if path.endswith(COMPILED_SUFFIXES)}
            wanted = listing.compiled(platform)
            if any('-fsanitize=fuzzer' in flag for _, flag in target.compile_flags):
                wanted.discard(FUZZ_DRIVER)
            for path in sorted(compiled - wanted):
                problems.append(f'{label}: {name} compiles {path}, which its source list does not name')
            for path in sorted(wanted - compiled):
                problems.append(f'{label}: {name} does not compile {path}, which its source list names')
            allowed = set(listing.includes) | set(CLASS_INCLUDES[entry.engine_class])
            for directory in sorted({path for path in includes if path.startswith('src/')} - allowed):
                problems.append(f'{label}: {name} includes {directory}, which its source list does not name')
            continue

        # Everything a target compiles and includes lies under its roots or its dependencies' roots.
        roots = set(entry.roots)
        for other in _closure(registry, name):
            roots.update(registry.targets[other].roots)
        for path in sorted({path for path in sources if path.endswith(COMPILED_SUFFIXES)}):
            if path.startswith('src/') and not any(_under(path, root) for root in roots):
                problems.append(f'{label}: {name} compiles {path}, outside its sources and those of what it links')
        for directory in sorted({path for path in includes if path.startswith('src/')}):
            if not any(_under(directory, root) for root in roots):
                problems.append(f'{label}: {name} includes {directory}, outside its sources and those of what it '
                                'links')
        # A target is read through its include lines, relative ones included, for a header of an engine its class
        # excludes.
        if entry.engine_class in CLASSES and name not in ENGINES.values():
            for source in sorted(path for path in sources if path.endswith(COMPILED_SUFFIXES)
                                 and _engine_of(path) is None and path.startswith('src/')):
                _, engine_files = reader.closure(source, includes)
                for path in sorted(engine_files):
                    if _engine_of(path) in _excluded(entry.engine_class):
                        problems.append(f'{label}: {name} is class {entry.engine_class} and {source} reaches {path}')
    return problems


# ---- Order, names and manifests.

def build_order(registry: Registry, workspace: str | None = None, states=('active',)) -> list[str]:
    """The targets of `workspace` in `states`, each after every target it depends on; ties by name."""
    chosen = {name for name, entry in registry.targets.items()
              if entry.state in states and (workspace is None or entry.workspace == workspace)}
    remaining = {name: {dependency for dependency in registry.targets[name].depends if dependency in chosen}
                 for name in chosen}
    order = []
    while remaining:
        ready = sorted(name for name, needs in remaining.items() if not needs)
        if not ready:
            raise ValueError('the registry\'s dependencies have a cycle among ' + ', '.join(sorted(remaining)))
        order.append(ready[0])
        del remaining[ready[0]]
        for needs in remaining.values():
            needs.discard(ready[0])
    return order


def binary_names(entry: TargetEntry, platform: str) -> tuple[str, ...]:
    """(release, debug) file names of a target's binary on `platform`; empty for a static library."""
    if entry.kind == 'static':
        return ()
    if platform == 'windows':
        extension = '.dll' if entry.kind == 'shared' else '.exe'
        return entry.output + extension, entry.debug_output + extension
    if entry.kind == 'shared':
        return f'lib{entry.output}.so', f'lib{entry.debug_output}.so'
    return entry.output, entry.debug_output


def manifest_paths(registry: Registry, names=None) -> list[str]:
    """The roots of the named targets - every active one by default - and of the build definitions."""
    chosen = set(names) if names else {name for name, entry in registry.targets.items() if entry.state == 'active'}
    for name in list(chosen):
        chosen.update(_closure(registry, name))
    paths = set(BUILD_DEFINITIONS)
    for name in chosen:
        entry = registry.targets[name]
        paths.update(entry.roots)
        if entry.source_list and entry.source_list in registry.sources:
            listing = registry.sources[entry.source_list]
            paths.update(listing.files())
    minimal = []
    for path in sorted(paths):
        if not any(_under(path, kept) for kept in minimal):
            minimal.append(path)
    return minimal


def covered_by(path: str, roots: list[str]) -> bool:
    """Whether a tracked file is one of the roots or under one, as a literal git pathspec matches it."""
    return any(path == root or path.startswith(root.rstrip('/') + '/') for root in roots)


def manifest_digest(registry: Registry, repo_root: pathlib.Path, rev: str | None = 'HEAD', names=None):
    """(the source-manifest digest at `rev`, the paths it covers); no digest when rev is None."""
    paths = manifest_paths(registry, names)
    if rev is None:
        return None, paths
    # The whole tree is listed and filtered here: the roots of a closure that compiles an explicit list are its files,
    # and a few hundred of them exceed the longest command line Windows accepts.
    listing = subprocess.run(['git', '-c', 'core.quotePath=false', 'ls-tree', '-r', rev], cwd=repo_root, check=True,
                             capture_output=True, text=True, encoding='utf-8').stdout
    lines = []
    for line in listing.splitlines():
        meta, path = line.split('\t', 1)
        if covered_by(path, paths):
            lines.append((path, meta.split()[2] + ' ' + path))
    lines.sort()
    return hashlib.sha256(''.join(text + '\n' for _, text in lines).encode('utf-8')).hexdigest(), paths


# ---- The command line.

def check(repo_root: pathlib.Path) -> list[str]:
    """Every rule's findings for the checkout at repo_root."""
    repo_root = pathlib.Path(repo_root)
    registry = read_registry(repo_root)
    problems = registry_findings(registry, repo_root)
    problems.extend(runner_findings(registry, repo_root))
    problems.extend(source_list_findings(registry, repo_root))
    tree = current_tree(repo_root)
    if tree is None:
        problems.append('no current generated project tree; generate projects first (01-create_project_files)')
    else:
        problems.extend(generated_findings(registry, tree[0], tree[1], repo_root))
    return problems


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('command', choices=('check', 'order', 'suites', 'manifest'))
    parser.add_argument('workspace', nargs='?', choices=WORKSPACES)
    parser.add_argument('--rev', default='HEAD', help='the revision a manifest is taken at (default HEAD)')
    parser.add_argument('--targets', help='comma-separated targets a manifest covers (default every active one)')
    parser.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    arguments = parser.parse_args(argv)
    repo_root = arguments.repo_root.resolve()
    if arguments.command == 'check':
        problems = check(repo_root)
        for problem in problems:
            print(problem)
        if problems:
            print(f'{len(problems)} target registry problem(s)')
            return 1
        registry = read_registry(repo_root)
        states = collections.Counter(entry.state for entry in registry.targets.values())
        label = current_tree(repo_root)[0]
        print(f'the target registry holds: {len(registry.targets)} targets '
              f'({", ".join(f"{count} {state}" for state, count in sorted(states.items()))}), '
              f'{len(registry.sources)} source lists and {len(registry.profiles)} package profiles, against {label}')
        return 0
    registry = read_registry(repo_root)
    if registry.problems:
        for problem in registry.problems:
            print(problem, file=sys.stderr)
        return 1
    if arguments.command == 'order':
        print('\n'.join(build_order(registry, arguments.workspace)))
    elif arguments.command == 'suites':
        print('\n'.join(runner_suites(registry)))
    else:
        names = [name.strip() for name in arguments.targets.split(',')] if arguments.targets else None
        unknown = [name for name in names or () if name not in registry.targets]
        if unknown:
            print('not declared in the registry: ' + ', '.join(unknown), file=sys.stderr)
            return 1
        digest, paths = manifest_digest(registry, repo_root, arguments.rev, names)
        print(f'{digest}  {arguments.rev}  {len(paths)} roots')
    return 0


if __name__ == '__main__':
    sys.exit(main())

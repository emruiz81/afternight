# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the product's own code to a register of every object it keeps for the life of the process.

The engine owns no globals, and `check_process_state.py` holds it to that. The product makes the
matching promise: it has no process-default engine service and no unowned cache; the application and
the command-line tool own their engine sessions; the full host borrows its runtime backend's one
session through one registered module seat; and the logger, its configuration, the crash handler and
the UI registrations have named entries with lifetime owners and reasons. This check is that promise
made mechanical, over a separate register, so the engine's rules and categories stay exactly as they
are.

`build/scripts/tools/consumer_state.txt` is the register. Every object with process lifetime that
the product's libraries and executables declare must have an entry, and every entry must name an
object that exists: both directions, so state cannot be added silently and an entry cannot outlive
its object. "Process lifetime" is read the way the engine check reads it - storage that outlives
every call - with the same scanner, run over each file with its byte-order mark removed and its raw
string literals blanked, because the product holds style sheets in them. One kind of declaration is
exempt: a value declared `const` at the head of its declaration, such as a string table or a file
name, which is written before anything reads it and never again. Listing two hundred string constants
would bury the entries that matter; a `const` object that is not immutable in practice is still a
defect a reviewer can see.

Each entry states where it is, its category, who creates and ends it, from when to when it lives, how
concurrent access to it is ordered, and why it exists. The categories, and where each may live:

- `platform-service`: the logger, its configuration and the crash handler. Only the platform library
  holds them.
- `module-seat`: the handleless C ABI of the runtime backend needs one registered seat per process,
  each extension host keeps its one embedded interpreter's bridge, and the command-line tool keeps the
  one interrupt watch its console handler reaches - a callback the operating system calls with no
  context. Only the backend, the two hosts and the command-line tool may hold one, and a seat is the
  only category besides `pending-rehome` allowed to hold an engine session.
- `application-seat`: the application's main window and the context the Qt layers reach the
  application's services through (`appContext()`), each set once on the GUI thread when the
  application or an extension host starts and cleared before it shuts down. Only the application,
  Controls' context locator and the `core/` directory of the shell core - the application without a
  window, which composes the context - hold one, and it holds no engine image, context or session: the
  session the context lends is owned by the application or the runtime backend.
- `theme-service`: the design tokens and installed styles the Qt framework requires process-wide.
  Only the theme directory holds them.
- `ui-registration`: a registry or lookup table built once and read afterwards - the process
  registry, extension status, documentation tables. Only the Qt layers hold them.
- `ui-state`: state the GUI keeps between uses of a view, touched on the GUI thread - a remembered sort
  order, a one-time warning latch. Only the Qt layers hold it, and its entry says it is GUI-thread
  state or names the lock.
- `identity`: a counter that hands out process-unique numbers, such as database connection names. It
  owns nothing and is only ever incremented, and it must be an atomic.
- `constant`: an immutable value the exemption does not recognise. It must be declared `const`.
- `pending-rehome`: state that none of the categories above permits - a process-wide service locator,
  an unowned cache, a registry keyed by live views - registered while it waits to be replaced or
  re-homed. Its reason says what replaces or re-homes it. This is an inventory of what has to change,
  not a permission.

No category but a module seat or a pending-rehome entry may hold an engine image, image handle,
engine context or session: an unowned image cache and a process-default engine context are the two
things never allowed.

Usage:
    python build/scripts/tools/check_consumer_state.py [--repo-root <dir>]

Exits 0 when the register and the tree agree, 1 otherwise, printing one line per finding.

Tests: build/scripts/tools/check_consumer_state_test.py
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_process_state as engine_state  # noqa: E402
import consumer_source  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
REGISTER = 'build/scripts/tools/consumer_state.txt'

# The product's libraries and executables. A tree that is not there is not scanned.
SCANNED_TREES = (
    'src/AfterNightPlatform',
    'src/AfterNightConsumerRuntime',
    'src/AfterNightControls',
    'src/AfterNightProcesses',
    'src/AfterNightShellCore',
    'src/AfterNightShellClassic',
    'src/AfterNightApp',
    'src/AfterNightCLI',
    'src/AfterNightExtensionHost',
    'src/AfterNightExtensionHostLite',
    'src/AfterNightExtensionProtocol',
    'src/AfterNightExtensionRuntimeBackend',
    'src/AfterNightStylePlugin',
    'src/AfterNightWorkflowDriver',
)

PLATFORM_SERVICE = 'platform-service'
MODULE_SEAT = 'module-seat'
APPLICATION_SEAT = 'application-seat'
THEME_SERVICE = 'theme-service'
UI_REGISTRATION = 'ui-registration'
UI_STATE = 'ui-state'
IDENTITY = 'identity'
CONSTANT = 'constant'
PENDING_REHOME = 'pending-rehome'
CATEGORIES = (PLATFORM_SERVICE, MODULE_SEAT, APPLICATION_SEAT, THEME_SERVICE, UI_REGISTRATION, UI_STATE, IDENTITY,
              CONSTANT, PENDING_REHOME)

QT_LAYERS = ('src/AfterNightControls/', 'src/AfterNightProcesses/', 'src/AfterNightShellCore/',
             'src/AfterNightShellClassic/', 'src/AfterNightApp/', 'src/AfterNightExtensionHost/')
LOCATIONS = {
    PLATFORM_SERVICE: ('src/AfterNightPlatform/',),
    MODULE_SEAT: ('src/AfterNightExtensionRuntimeBackend/', 'src/AfterNightExtensionHost/',
                  'src/AfterNightExtensionHostLite/', 'src/AfterNightCLI/'),
    APPLICATION_SEAT: ('src/AfterNightApp/', 'src/AfterNightControls/controls/IAppContext.cpp',
                       'src/AfterNightShellCore/core/', 'src/AfterNightShellClassic/'),
    THEME_SERVICE: ('src/AfterNightControls/theme/',),
    UI_REGISTRATION: QT_LAYERS,
    UI_STATE: QT_LAYERS,
}
LOCATION_REASONS = {
    PLATFORM_SERVICE: 'the platform library',
    MODULE_SEAT: 'the runtime backend, an extension host or the command-line tool',
    APPLICATION_SEAT: "the application, its context locator, the shell core's core or a shell",
    THEME_SERVICE: 'the theme directory',
    UI_REGISTRATION: 'a Qt layer',
    UI_STATE: 'a Qt layer',
}

# Spellings that mean an engine image, handle, context or session.
ENGINE_HOLDINGS = re.compile(r'\b(?:image_sptr|sequence_sptr|an::image\b|EngineContext|EngineSession|ImageAsset|'
                             r'ImageStorage|Image\s*<)')

FIELDS = ('scope', 'category', 'owner', 'lifetime', 'synchronization', 'reason')
# A field starts at this indentation; a line indented further continues the field above it.
FIELD_INDENT = 4

# A declaration whose head says `const` before the type, or directly before the name.
_IMMUTABLE = re.compile(r'^(?:(?:static|inline|extern|thread_local|constinit|[A-Z_]+_API)\s+)*const\b|'
                        r'\bconst\s+\w+\s*(?:\[[^\]]*\]\s*)*$')
_EXTERN_BLOCK = re.compile(r'^\s*extern\s*\{')


@dataclasses.dataclass(frozen=True)
class Entry:
    path: str
    symbol: str
    line: int
    fields: dict[str, str]


def _head(declaration: str) -> str:
    """The declaration up to its initializer, which is where its constness is stated."""
    match = re.search(r'[={]', declaration)
    return (declaration[:match.start()] if match else declaration).rstrip(' ;')


def is_immutable(declaration: str) -> bool:
    head = _head(declaration)
    return bool(_IMMUTABLE.search(head)) and 'mutable' not in head


def without_directives(code: str) -> str:
    """The code with every preprocessor line blanked, continuation lines included.

    The statement reader joins lines until one ends a statement, and a directive never does: a
    definition that directly follows an `#include` would otherwise be read as part of the directive
    and dropped.
    """
    lines = code.split('\n')
    continuing = False
    for index, line in enumerate(lines):
        if continuing or line.lstrip().startswith('#'):
            continuing = line.rstrip().endswith('\\')
            lines[index] = ''
    return '\n'.join(lines)


def scan_file(path: pathlib.Path) -> dict[str, tuple[int, str]]:
    """{symbol: (line, declaration)} for every non-exempt object with process lifetime in one file."""
    code = without_directives(consumer_source.code_only(consumer_source.read_text(path)))
    found: dict[str, tuple[int, str]] = {}
    for line, statement, indented in engine_state.statements(code):
        if _EXTERN_BLOCK.match(statement):
            continue
        name = engine_state.declared_object(statement, indented)
        if name is None:
            continue
        declaration = ' '.join(statement.split())
        if is_immutable(declaration):
            continue
        found.setdefault(name, (line, declaration))
    return found


def scan(repo_root: pathlib.Path) -> dict[tuple[str, str], tuple[int, str]]:
    """{(path, symbol): (line, declaration)} for the whole product."""
    files = []
    for tree in SCANNED_TREES:
        base = repo_root / tree
        if base.is_dir():
            files.extend(path for path in base.rglob('*')
                         if path.is_file() and path.suffix in consumer_source.SOURCE_SUFFIXES)
    found = {}
    for path in sorted(set(files)):
        relative = path.relative_to(repo_root).as_posix()
        for symbol, value in scan_file(path).items():
            found[(relative, symbol)] = value
    return found


def read_register(repo_root: pathlib.Path) -> tuple[list[Entry], list[str]]:
    """The register's entries, and a problem for every line that is not part of one."""
    path = repo_root / REGISTER
    if not path.is_file():
        return [], [f'{REGISTER}: does not exist']
    entries: list[Entry] = []
    problems = []
    current: dict | None = None
    field = None
    lines = path.read_bytes().decode('utf-8').replace('\r\n', '\n').split('\n')
    for number, line in enumerate(lines, start=1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if not line.startswith((' ', '\t')):
            words = line.split()
            if len(words) != 3 or words[0] != 'state':
                problems.append(f'{REGISTER}:{number}: a record starts with "state <path> <symbol>"')
                current = None
                continue
            current = {'path': words[1], 'symbol': words[2], 'line': number, 'fields': {}}
            entries.append(current)  # type: ignore[arg-type]
            field = None
            continue
        if current is None:
            problems.append(f'{REGISTER}:{number}: a field outside a record')
            continue
        words = line.split(None, 1)
        indentation = len(line) - len(line.lstrip())
        if indentation <= FIELD_INDENT and words[0] in FIELDS:
            field = words[0]
            if field in current['fields']:
                problems.append(f'{REGISTER}:{number}: {field} appears twice in one record')
            current['fields'][field] = words[1].strip() if len(words) > 1 else ''
        elif indentation > FIELD_INDENT and field is not None:
            current['fields'][field] += ' ' + line.strip()
        else:
            # Only a line indented further than a field continues one, so an unknown name is not hidden in the last.
            problems.append(f'{REGISTER}:{number}: {words[0]!r} is not a field')
            field = None
    return [Entry(item['path'], item['symbol'], item['line'], item['fields']) for item in entries], problems


def entry_problems(entry: Entry, declaration: str | None) -> list[str]:
    """Every way one entry fails its own rules."""
    where = f'{entry.path}: {entry.symbol}'
    problems = []
    for field in FIELDS:
        if not entry.fields.get(field):
            problems.append(f'{where}: the entry has no {field}')
    category = entry.fields.get('category', '')
    if category not in CATEGORIES:
        problems.append(f'{where}: category {category!r} is not one of {", ".join(CATEGORIES)}')
        return problems
    locations = LOCATIONS.get(category)
    if locations and not entry.path.startswith(locations):
        problems.append(f'{where}: {category} state may only live in {LOCATION_REASONS[category]}')
    if declaration is not None:
        if category == CONSTANT and not re.search(r'\bconst\b', declaration):
            problems.append(f'{where}: declared a constant but is not const')
        if category == IDENTITY and 'atomic' not in declaration:
            problems.append(f'{where}: an identity counter handed out from more than one thread is an atomic')
        if category not in (MODULE_SEAT, PENDING_REHOME) and ENGINE_HOLDINGS.search(declaration):
            problems.append(f'{where}: {category} state holds an engine image, context or session; an unowned image '
                            'cache and a process-default engine context are never allowed')
    if category == UI_STATE and 'GUI thread' not in entry.fields.get('synchronization', '') \
            and 'lock' not in entry.fields.get('synchronization', '') \
            and 'mutex' not in entry.fields.get('synchronization', ''):
        problems.append(f'{where}: ui-state names the GUI thread or the lock that orders access to it')
    return problems


def check(repo_root: pathlib.Path) -> list[str]:
    entries, problems = read_register(repo_root)
    found = scan(repo_root)
    registered = {}
    for entry in entries:
        key = (entry.path, entry.symbol)
        if key in registered:
            problems.append(f'{entry.path}: {entry.symbol} is registered twice')
            continue
        registered[key] = entry
    for key, (line, declaration) in sorted(found.items()):
        if key not in registered:
            problems.append(f'{key[0]}:{line}: {key[1]} has process lifetime and no entry in {REGISTER}: '
                            f'{declaration[:120]}')
    for key, entry in sorted(registered.items()):
        if key not in found:
            problems.append(f'{REGISTER}:{entry.line}: {entry.symbol} is registered in {entry.path}, which declares no '
                            'such object; remove the entry with the object')
            problems.extend(entry_problems(entry, None))
        else:
            problems.extend(entry_problems(entry, found[key][1]))
    return problems


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    arguments = parser.parse_args(argv)
    repo_root = arguments.repo_root.resolve()
    problems = check(repo_root)
    for problem in problems:
        print(f'ERROR: {problem}', file=sys.stderr)
    if problems:
        return 1
    entries, _ = read_register(repo_root)
    counts = {}
    for entry in entries:
        counts[entry.fields['category']] = counts.get(entry.fields['category'], 0) + 1
    print(f'consumer process state matches its register: {len(entries)} entries ('
          + ', '.join(f'{name} {counts[name]}' for name in CATEGORIES if name in counts) + ')')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold this engine to its rule that it owns no process-wide state it has not declared.

`Runtime/EngineContext.h` owns the state an engine keeps between calls, and the consequence is
written into the contributor guide as one sentence: the engine owns no globals. A sentence is not a
mechanism. This is the mechanism.

The question it answers is narrow and mechanical: which objects in the engine's libraries have
*process lifetime* - storage that outlives every call, every execution and every engine context.
That is not the same question as "which declarations say `static`". A `static` member function
declares no storage; a `constexpr` value is a compile-time constant the compiler may never give an
address at all; and a namespace-scope `std::mutex` with no `static` on it is process-wide state
whatever it is spelled like. So the scan looks for declarations that create storage, and every one
it finds must appear in the table below with a category and a reason. A declaration that does not is
a finding, and stays one until somebody decides which of the four categories it is in - or removes
it, which is the outcome this check exists to make the easy one.

The four categories, and why each is allowed to exist at all:

1. `identity`. A counter that gives things process-unique numbers. Two engine contexts in one
   process must not answer the same identity, and a progress sink shared by two operations must be
   able to tell them apart, so the numbering is process-wide by necessity. It owns no service, no
   thread and no allocation, it is only ever incremented, and nothing reads it as state. An entry
   here must be an atomic.

2. `constant`. An immutable value with process lifetime: a host fact detected once, a scale the
   language cannot spell as a constant expression, an empty value an accessor returns a reference
   to. It is written before anything can read it and never again. An entry here must be const.

3. `library-state`. State that belongs to a library this tree links rather than to this engine: its
   planner lock, its one-time initialization, its loaded module. The library makes it process-wide
   and gives its callers no choice; the engine's part is to serialize or observe it, never to keep
   anything of its own beside it. An entry here may only live in an interop directory of the engine
   or in an optional adapter or backend - the engine's own algorithm, image and runtime code may not
   acquire a library's process state.

4. `adapter-seat`. An optional adapter's record of the single per-process instance of the library it
   adapts. One Python interpreter and one G'MIC library exist per process, so the adapter that owns
   one has to know whether the seat is taken, which thread is starting it, and why the last start
   failed. An entry here may only live in an optional adapter or backend, and `thread_local` is
   included here: a foreign callback that runs on the thread that installed it reads its scope
   through one.

Everything else is refused by not being in the table. A cache, a registry, a settings object, a
logger, a lazily built lookup table and a shared mutable buffer are all things this engine does not
have, and each of them would have to be argued into one of the four categories in a change that a
reader can see.

Two things are deliberately outside this check. The tests and the benchmark runner are not scanned:
an executable owns its own process, and a test that installs something process-wide is making a
decision about its own process rather than about a library its callers link. And third-party code is
not scanned: what oneTBB or Highway keeps is their design, and this check is about what this engine
adds.

Usage:
    python build/scripts/tools/check_process_state.py [--repo-root <dir>]
    python build/scripts/tools/check_process_state.py --objects <directory> [--symbol-tool <path>]

The default reads the source. `--objects` reads what a compiler actually emitted: every symbol the
built objects place in a writable data section, which is every object with process lifetime that is
not a constant the linker could fold into read-only memory. Which section that is comes from the
section name rather than from the symbol class, because ELF places a relocated `constexpr` table in
`.data.rel.ro` and letters it exactly as it letters `.data`. It needs a build and a symbol tool that
demangles and names sections - `llvm-nm -C --format=sysv` or `nm -C --format=sysv` - and it is the
stronger evidence of the two, because it sees
through templates, macros and anything a lexical scan can be fooled by. The two are meant to agree,
and a change to this surface should say that they did.

Exits 0 when the declared table and the tree agree, 1 otherwise, printing one line per finding.

Tests: build/scripts/tools/check_process_state_test.py
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import shutil
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

# The engine, the two optional adapters and the opt-in proof backend: every library on the engine's
# side of the tree. A directory that is not there is not scanned - the proof backend exists only in a
# tree generated with its switch, and its removal takes it away entirely.
SCANNED_TREES = (
    'src/AfterNightEngine',
    'src/AfterNightEngineGmicAdapter',
    'src/AfterNightEngineOpenClBackend',
    'src/AfterNightEnginePythonAdapter',
)

# The engine's own code, and the one part of it that is allowed to hold a library's process state.
ENGINE_TREE = 'src/AfterNightEngine/'
ENGINE_INTEROP = 'src/AfterNightEngine/src/interop/'

SOURCE_SUFFIXES = ('.h', '.hpp', '.inl', '.cpp')

IDENTITY = 'identity'
CONSTANT = 'constant'
LIBRARY_STATE = 'library-state'
ADAPTER_SEAT = 'adapter-seat'
CATEGORIES = (IDENTITY, CONSTANT, LIBRARY_STATE, ADAPTER_SEAT)


@dataclasses.dataclass(frozen=True)
class ProcessDatum:
    """One object with process lifetime, and the argument for it existing.

    `scope` is the function or class the declaration sits in, empty for one at namespace scope. The
    source scan does not need it - a path and a name are unique there - and the object scan does,
    because three counters in three functions are all called `counter`.
    """

    path: str
    symbol: str
    scope: str
    category: str
    reason: str


# Every object with process lifetime in the engine's libraries. Twenty-one of them, and the
# argument for each is in its own sentence rather than in a comment somewhere near the code.
DECLARED = (
    # ---- The engine. Three counters, four constants, one piece of a library's own state.
    ProcessDatum(
        'src/AfterNightEngine/src/runtime/EngineContextState.h', 'counter', 'EngineContextState::nextId',
        IDENTITY,
        'Engine contexts are numbered across the process, because two of them coexist by design and a '
        'diagnostic that could not tell them apart would be reporting about neither.'),
    ProcessDatum(
        'src/AfterNightEngine/src/runtime/ExecutionContext.cpp', 'counter', 'nextOperationId',
        IDENTITY,
        'Operations are numbered across the process, because a sink shared by two executions - in one '
        'context or in two - separates their reports by this number.'),
    ProcessDatum(
        'src/AfterNightEngine/src/kernels/reference/Device.cpp', 'counter', 'nextDeviceId',
        IDENTITY,
        'Devices are numbered across the process, so that work submitted to one device can be refused '
        'by another without the two having to share anything.'),
    ProcessDatum(
        'src/AfterNightEngine/src/backends/cpu/CpuCapabilities.cpp', 'kDetected', 'cpuCapabilities',
        CONSTANT,
        'What this processor supports is detected once and never changes; the dispatch table reads it '
        'and no engine context can alter it.'),
    ProcessDatum(
        'src/AfterNightEngine/src/algorithms/stars/StarMask.cpp', 'kTruncatedBellScale', '',
        CONSTANT,
        'The truncated bell is scaled by the reciprocal of a standard-library exponential, which is not '
        'a constant expression; it is computed when the library loads and read from one function.'),
    ProcessDatum(
        'src/AfterNightEngine/src/kernels/reference/DeviceChain.cpp', 'kNone', 'noResource',
        CONSTANT,
        'A chain accessor asked for a buffer it does not have returns a reference to one empty resource '
        'that holds no allocation, no device and no lease.'),
    ProcessDatum(
        'src/AfterNightEngine/src/algorithms/catalog/SpectrumCatalog.cpp', 'kEmptyManifest',
        'SpectrumCatalog::manifest',
        CONSTANT,
        'A catalog with no state answers its manifest accessor with one value-initialized manifest '
        'rather than with a dangling reference.'),
    ProcessDatum(
        'src/AfterNightEngine/src/interop/fftw/FftwRealTransform.cpp', 'lock', 'plannerLock',
        LIBRARY_STATE,
        "FFTW's plan creation and destruction read and write the library's own wisdom, so two threads "
        'planning unrelated transforms race; two engine contexts still share one planner because the '
        'library gives them no choice.'),
    # ---- The optional Python adapter. One interpreter exists per process, so the seat is one too.
    ProcessDatum(
        'src/AfterNightEnginePythonAdapter/src/PythonRuntime.cpp', 'gActiveAdapter', '',
        ADAPTER_SEAT,
        'CPython has one interpreter per process, so this is whether the seat is taken and the immutable '
        'runtime version it reports; the owner clears it after finalization and capability reports read it '
        'from any thread without borrowing adapter storage.'),
    ProcessDatum(
        'src/AfterNightEnginePythonAdapter/src/PythonRuntime.cpp', 'gStartupMutex', '',
        ADAPTER_SEAT,
        'Starting and finalizing that one interpreter are serialized, so two threads cannot both find '
        'the seat free.'),
    ProcessDatum(
        'src/AfterNightEnginePythonAdapter/src/PythonRuntime.cpp', 'gLastFailureCode', '',
        ADAPTER_SEAT,
        'Why the last start in this process failed, for a capability report that has no adapter to ask.'),
    ProcessDatum(
        'src/AfterNightEnginePythonAdapter/src/PythonRuntime.cpp', 'gLastFailureReason', '',
        ADAPTER_SEAT,
        'The named reason beside that code, for the same report.'),
    ProcessDatum(
        'src/AfterNightEnginePythonAdapter/src/PythonRuntime.cpp', 'tCancellation', '',
        ADAPTER_SEAT,
        'A trace function is installed per thread state and runs on the thread whose code it traces, so '
        'the scope it reads is thread-local; nothing else can see it, which is what makes it safe where '
        'a cross-thread interrupt would not be.'),

    # ---- The optional G'MIC adapter. The library keeps state of its own, so calls into it are serialized.
    ProcessDatum(
        'src/AfterNightEngineGmicAdapter/src/GmicSession.cpp', 'gLibraryMutex', '',
        ADAPTER_SEAT,
        "G'MIC and CImg keep state of their own, so every call into the library is serialized whatever "
        'engine context asked for it.'),
    ProcessDatum(
        'src/AfterNightEngineGmicAdapter/src/GmicSession.cpp', 'gProbeMutex', '',
        ADAPTER_SEAT,
        'The one probe that decides whether the library is usable happens once, and two threads asking '
        'at the same time get one answer.'),
    ProcessDatum(
        'src/AfterNightEngineGmicAdapter/src/GmicSession.cpp', 'gProbed', '',
        ADAPTER_SEAT,
        'Whether that probe has run, read without the lock by a capability report that cannot block.'),
    ProcessDatum(
        'src/AfterNightEngineGmicAdapter/src/GmicSession.cpp', 'gOpenSessions', '',
        ADAPTER_SEAT,
        'How many adapter sessions are open in this process, which is what lets the last one out tear '
        "down the library's state and no earlier one do it."),
    ProcessDatum(
        'src/AfterNightEngineGmicAdapter/src/GmicSession.cpp', 'gCallsInFlight', '',
        ADAPTER_SEAT,
        'How many calls are inside the library now, so a teardown that would pull state out from under '
        'one is refused rather than raced.'),
    ProcessDatum(
        'src/AfterNightEngineGmicAdapter/src/GmicSession.cpp', 'gLastFailureCode', '',
        ADAPTER_SEAT,
        'Why the last session in this process failed, for a capability report that has no session to ask.'),
    ProcessDatum(
        'src/AfterNightEngineGmicAdapter/src/GmicSession.cpp', 'gLastFailureReason', '',
        ADAPTER_SEAT,
        'The named reason beside that code, for the same report.'),

    # ---- The opt-in proof backend, which exists only in a tree generated with its switch.
    ProcessDatum(
        'src/AfterNightEngineOpenClBackend/src/OpenClRuntime.cpp', 'kModule', 'loaderModule',
        LIBRARY_STATE,
        'A loaded library is a property of the process: every delay-loaded import this backend makes '
        "resolves against the operating system's one OpenCL loader, which is never unloaded."),
)

BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.DOTALL)
LINE_COMMENT = re.compile(r'//[^\n]*')
STRING_LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
CHARACTER_LITERAL = re.compile(r"'(?:[^'\\\n]|\\.)*'")

STORAGE_KEYWORD = re.compile(r'\b(?:static|thread_local)\b')
COMPILE_TIME = re.compile(r'\b(?:constexpr|consteval|constinit)\b')

# Lines that begin something other than an object declaration. Namespace contents in this tree are
# not indented, which is what makes a namespace-scope declaration recognisable at all; these are the
# other things that start in the first column.
NOT_A_DECLARATION = re.compile(
    r'^\s*(?:#|\}|\{|\)|namespace\b|class\b|struct\b|union\b|enum\b|template\b|using\b|typedef\b|'
    r'static_assert\b|return\b|extern\s+"C"|friend\b|public:|private:|protected:|explicit\b|'
    r'virtual\b|concept\b|requires\b|if\b|for\b|while\b|switch\b|case\b|default:|do\b|else\b|try\b|'
    r'catch\b|goto\b|delete\b|throw\b|co_return\b|\w+:$)')

TRAILING_NAME = re.compile(r'(\w+)\s*$')


def blank_out(match):
    """Replace a matched region with spaces, keeping every newline so line numbers survive."""
    return re.sub(r'[^\n]', ' ', match.group(0))


def readable_source(text):
    """The source with comments and literals blanked, so a rule reads declarations rather than prose."""
    text = BLOCK_COMMENT.sub(blank_out, text)
    text = LINE_COMMENT.sub(blank_out, text)
    text = STRING_LITERAL.sub(blank_out, text)
    return CHARACTER_LITERAL.sub(blank_out, text)


def statements(text):
    """Yield (line number, joined statement, whether the first line was indented).

    A declaration can wrap over several lines, so lines are joined until a terminator is reached at
    bracket depth zero. Twelve lines is the cut-off: nothing this check is looking for is longer, and
    a runaway join would swallow a whole function body.
    """
    lines = text.split('\n')
    index = 0
    while index < len(lines):
        if not lines[index].strip():
            index += 1
            continue
        start = index
        buffer = ''
        depth = 0
        while index < len(lines):
            current = lines[index]
            buffer += current + '\n'
            for character in current:
                if character in '([':
                    depth += 1
                elif character in ')]':
                    depth -= 1
            index += 1
            ended = buffer.rstrip()
            if depth <= 0 and ended.endswith((';', '{', '}')):
                break
            if index - start > 12:
                break
        yield start + 1, buffer, lines[start][:1] in (' ', '\t')


def declared_object(statement, indented):
    """The name this statement gives an object with process lifetime, or None.

    The discrimination that matters is between a declaration that creates storage and one that
    declares a function, and it is made the way the language makes it: whichever of an initializer,
    a terminator and a parameter list comes first at bracket depth zero. `static Result<X> create(A)`
    reaches a parameter list and is a function; `static std::mutex lock;` reaches its terminator with
    no parentheses at all and is an object; `static const bool kReady = f(&a);` reaches an
    initializer first and is an object whose value happens to come from a call.
    """
    first_line = statement.split('\n')[0]
    if NOT_A_DECLARATION.search(first_line):
        return None
    text = ' '.join(statement.split())
    if COMPILE_TIME.search(text):
        return None
    if STORAGE_KEYWORD.search(text) is None and indented:
        return None

    depth = 0
    kind = None
    position = 0
    for index, character in enumerate(text):
        if character in '([':
            if depth == 0:
                kind, position = 'parameters', index
                break
            depth += 1
        elif character in ')]':
            depth -= 1
        elif depth != 0:
            continue
        elif character == '{':
            kind, position = 'initializer', index
            break
        elif character == ';':
            kind, position = 'terminator', index
            break
        elif character == '=' and _is_assignment(text, index):
            kind, position = 'initializer', index
            break
    if kind is None or kind == 'parameters':
        return None
    name = TRAILING_NAME.search(text[:position].strip())
    return name.group(1) if name is not None else None


def _is_assignment(text, index):
    """Whether the `=` at this index introduces an initializer rather than naming an operator."""
    if text[index + 1:index + 2] == '=':
        return False
    before = text[:index].rstrip()
    if before.endswith(('!', '<', '>', '+', '-', '*', '/', '%', '&', '|', '^', '=')):
        return False
    return not before.endswith('operator')


def scan_source(repo_root):
    """Every object with process lifetime the source declares, keyed by path and name."""
    found = {}
    for tree in SCANNED_TREES:
        root = repo_root / tree
        if not root.is_dir():
            continue
        for path in sorted(root.rglob('*')):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(repo_root).as_posix()
            text = readable_source(path.read_bytes().decode('utf-8'))
            for line, statement, indented in statements(text):
                name = declared_object(statement, indented)
                if name is None:
                    continue
                found.setdefault((relative, name), (line, ' '.join(statement.split())))
    return found


def present_trees(repo_root):
    """The scanned trees that exist in this checkout."""
    return tuple(tree for tree in SCANNED_TREES if (repo_root / tree).is_dir())


def declared_for(trees):
    """The declared table, restricted to the trees this checkout has."""
    return tuple(datum for datum in DECLARED if any(datum.path.startswith(tree + '/') for tree in trees))


def category_findings(datum, declaration):
    """Every way one declaration fails to be what its category claims it is."""
    findings = []
    if datum.category not in CATEGORIES:
        findings.append('%s: %s is declared %r, which is not one of %s'
                        % (datum.path, datum.symbol, datum.category, ', '.join(CATEGORIES)))
        return findings
    if datum.category == IDENTITY and 'atomic' not in declaration:
        findings.append('%s: %s is declared an identity counter but is not an atomic. A number handed '
                        'out from more than one thread is either atomic or a race.' % (datum.path, datum.symbol))
    if datum.category == CONSTANT and not re.search(r'\bconst\b', declaration):
        findings.append('%s: %s is declared a constant but is not const. A value with process lifetime '
                        'that anything may write is state, and state belongs to an engine context.'
                        % (datum.path, datum.symbol))
    if datum.category == LIBRARY_STATE and datum.path.startswith(ENGINE_TREE) \
            and not datum.path.startswith(ENGINE_INTEROP):
        findings.append("%s: %s claims to be a library's process state outside %s. The engine's own "
                        'algorithm, image and runtime code does not acquire one; an adapter directory does.'
                        % (datum.path, datum.symbol, ENGINE_INTEROP))
    if datum.category == ADAPTER_SEAT and datum.path.startswith(ENGINE_TREE):
        findings.append('%s: %s claims to be an optional adapter\'s seat inside the engine. The engine '
                        'links no library that is one per process; an optional adapter does.'
                        % (datum.path, datum.symbol))
    return findings


def check_source(repo_root):
    """Every finding from reading the source."""
    trees = present_trees(repo_root)
    declared = declared_for(trees)
    found = scan_source(repo_root)
    findings = []
    by_key = {(datum.path, datum.symbol): datum for datum in declared}

    for key in sorted(found.keys() - by_key.keys()):
        line, statement = found[key]
        findings.append('%s:%d: %s has process lifetime and is not declared in check_process_state.py. '
                        'Remove it, or add it with the category and the reason it exists.'
                        % (key[0], line, key[1]))
    for key in sorted(by_key.keys() - found.keys()):
        findings.append('%s: %s is declared in check_process_state.py and no longer exists. A table that '
                        'keeps a removed entry stops being an inventory.' % key)
    for key in sorted(by_key.keys() & found.keys()):
        findings.extend(category_findings(by_key[key], found[key][1]))
    return findings


# What a compiler puts in a writable data section that is a record about the program rather than a
# part of it: run-time type information, virtual tables, the guard variable a function-local static
# with a non-constant initializer needs, and the line numbers a debug standard library records.
COMPILER_RECORD = re.compile(
    r"`RTTI |`vftable'|`vbtable'|`local static thread guard'|`local static guard'|::\$TSS\d|"
    r'__LINE__Var|`typeof|guard variable for |vtable for |typeinfo for |typeinfo name for ')

SYMBOL_LINE = re.compile(r'^\s*[0-9a-fA-F]*\s*([A-Za-z])\s+(.*)$')
WRITABLE_SECTIONS = frozenset('bBdD')
TRAILING_SYMBOL = re.compile(r'(\w+)\s*$')

# The section-named form of the same listing, which both `llvm-nm` and `nm` produce with
# `--format=sysv`: name, value, class, type, size, line, section.
SYSV_LINE = re.compile(r'^(?P<name>[^|]*?)\s*\|(?P<value>[^|]*)\|(?P<class>[^|]*)\|'
                       r'(?P<type>[^|]*)\|(?P<size>[^|]*)\|(?P<line>[^|]*)\|\s*(?P<section>\S*)\s*$')

# What a section name means, which is the question the symbol class cannot answer on ELF: a
# `constexpr` table whose members need relocation goes to `.data.rel.ro`, and `nm` letters that `d`
# exactly as it letters `.data`. It is read-only once the loader has relocated it, so an engine that
# kept every one of them would be reported as owning a hundred pieces of process state it cannot
# write. A section a compiler gives its own suffix - `.bss._ZZN2an...`, `.data$r` - is still that
# section, so the prefix is what is matched.
READ_ONLY_AFTER_RELOCATION = ('.data.rel.ro',)
WRITABLE_SECTION_PREFIXES = ('.data', '.bss', '.tbss', '.tdata')


def writable_section(section):
    """Whether a section name is memory the program can write after it is loaded.

    @param section the section a symbol was placed in, as a symbol dumper names it.
    @return whether an object there has writable process lifetime.
    """
    name = (section or '').strip()
    if name.startswith(READ_ONLY_AFTER_RELOCATION):
        return False
    return name.startswith(WRITABLE_SECTION_PREFIXES)


def object_symbols(objects, symbol_tool):
    """Every demangled data symbol the built objects place in a writable section."""
    files = [path for path in sorted(objects.rglob('*')) if path.suffix in ('.obj', '.o')]
    if not files:
        return None, ['no object file below %s; build the tree before reading its symbols' % objects]
    symbols = []
    problems = []
    batch = 64
    for start in range(0, len(files), batch):
        completed = subprocess.run([symbol_tool, '-C', '--format=sysv'] +
                                   [str(path) for path in files[start:start + batch]],
                                   capture_output=True, text=True, check=False)
        if completed.returncode != 0 and not completed.stdout:
            problems.append('%s could not read the objects: %s' % (symbol_tool, completed.stderr.strip()))
            continue
        for line in completed.stdout.split('\n'):
            match = SYSV_LINE.match(line)
            if match is None or not writable_section(match.group('section')):
                continue
            name = match.group('name').strip()
            if COMPILER_RECORD.search(name) or 'an::' not in name:
                continue
            symbols.append(name)
    return sorted(set(symbols)), problems


def check_objects(repo_root, objects, symbol_tool):
    """Every finding from reading what the compiler emitted."""
    symbols, findings = object_symbols(objects, symbol_tool)
    if symbols is None:
        return findings
    declared = declared_for(present_trees(repo_root))
    for name in symbols:
        trailing = TRAILING_SYMBOL.search(name)
        if trailing is None:
            continue
        matched = [datum for datum in declared
                   if datum.symbol == trailing.group(1) and (not datum.scope or datum.scope in name)]
        if not matched:
            findings.append('%s has process lifetime in the built objects and is not declared in '
                            'check_process_state.py.' % name)
    return findings


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--objects', default=None,
                        help='read the built objects below this directory instead of the source')
    parser.add_argument('--symbol-tool', default=None,
                        help='a symbol dumper that demangles with -C; llvm-nm or nm')
    arguments = parser.parse_args(argv)

    repo_root = pathlib.Path(arguments.repo_root)
    if arguments.objects is None:
        findings = check_source(repo_root)
        subject = 'declaration'
    else:
        tool = arguments.symbol_tool or shutil.which('llvm-nm') or shutil.which('nm')
        if tool is None:
            print('no symbol tool found; pass --symbol-tool with a path to llvm-nm or nm')
            return 1
        findings = check_objects(repo_root, pathlib.Path(arguments.objects), tool)
        subject = 'symbol'

    for finding in findings:
        print(finding)
    if findings:
        print('%d process-state %s(s) to resolve' % (len(findings), subject))
        return 1

    print('every object with process lifetime is declared, with a category and a reason')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

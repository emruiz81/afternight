# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prove that a deployed directory really holds what the build graph places there, each with its own symbols.

`check_build_graph.py` already refuses a *definition* in which two projects could share a binary,
an import library, a symbol file or an object directory. That is a statement about the generated
graph. This one is about the directory a developer, a test run, a benchmark and a crash
symbolication actually load from, and the two are not the same claim: a graph that is correct and a
build that never ran print the same clean line, and the failure this is written against - one
binary overwriting or silently omitting another, or a binary deployed without the symbols its
configuration promises - happens when something copies, not when something generates.

The expectation is read from the generated tree rather than listed here. Every target binary the
graph places in the given directory has to be there, with the symbol file that configuration
produces, so a suite that was never built and an engine that was never deployed are both findings
rather than silence. Listing the names instead would make this check the one place that never
learns about a new target.

Three further things it refuses, each a real way a shared deployment directory goes wrong:

  - two deployed names that differ only in case, because Windows and both packagers match without
    case, so such a pair is one file on one platform and two on the other;
  - a binary of this product that no generated project produces any more, which is what a renamed,
    removed or opt-in target leaves in a directory that nothing sweeps. Anything that is not this
    product's own binary - a third-party runtime, a privately packaged library, a tool built by
    another build system - is left alone, because this check owns the two solutions' output and
    nothing else; and
  - a binary anywhere below the directory that names a sanitizer runtime while the directory's
    `.engine-instrumentation` records none, which is what a sanitizer generation leaves where no
    ordinary build rebuilt after it, a plugin in a directory of its own as much as a library
    beside the application. This one reads every binary below the directory, a third-party one
    included, because an ordinary tree holds no instrumented binary of anyone's.

Symbols are asked for as the configuration produces them, and they are asked of the binary rather
than of the directory listing. `AGENTS.md` states the rule a crash dump is read under: the symbols
must come from the exact build of the binary that crashed, and a different build of identical source
produces non-matching ones. A check that only found a file of about the right name beside the binary
would pass on symbols nothing can be symbolicated with, which is the failure worth catching here,
because it is silent until someone has a dump and no way to read it.

So on Windows the binary's CodeView record is read: it names the `.pdb` the linker wrote and carries
the signature that identifies it, and both the name and the signature have to be the file beside the
binary. On Linux a Release build splits its debug information into a `.debug` sidecar, strips the
binary and records a `.gnu_debuglink` naming the sidecar with a CRC-32 of its contents, so the name
and the checksum are both verified. A Debug build there keeps its debug information inside and writes
no sidecar, so a missing sidecar is a finding only when the binary has been stripped, which is read
from the binary rather than assumed.

A Windows deployment also carries the Visual C++ runtime its binaries import - each redistributable library a
binary at its top level names - because Windows provides only the universal CRT, and nothing provides the rest but a
redistributable something else installed: a package made from a deployment that leans on one starts on a build host
and on no clean machine. The debug runtime is not redistributable and is not held here, and a library below the top
level - a Python package's - carries the runtime it was built against beside itself.

A Linux deployment carries in `lib/` every Qt library a binary at its top level needs, because a binary that links Qt
names `$ORIGIN/lib` as its one run path: a Qt library missing there is the host distribution's - another build, usually
of another version - and a binary that ran on a build host stops at its first private symbol. The suites are held as
much as the product, because they run from the same directory and Qt Test is a library only they load.

Every library a binary the product ships at a Linux deployment's top level needs, and every library those need,
resolves where the loader looks - the run path of the file that needs it, then the system's cache. A Qt that needs an ICU neither the
deployment nor the system carries leaves a deployment that starts nowhere, and a dependency analysis that followed the
distribution's Qt rather than the build's copies exactly the wrong ICU.

No binary below a Linux deployment names a directory of the machine that built it in its run path - an absolute
directory that exists on this host outside the system's own. No other machine has that directory, and on this one it
satisfies an import the deployment does not carry, so a package made from the deployment starts here and nowhere else.
A third party's build that put its link directories there, and a binary GNU Make left linked with options its project
no longer states, both show here.

Usage:
    python build/scripts/tools/check_deployed_runtimes.py --binaries bin/msvc/release
    python build/scripts/tools/check_deployed_runtimes.py --binaries bin/clang/debug

Exits 0 when the deployment holds, 1 otherwise, printing one line per problem. A clean run prints
what it read, because a directory that was never deployed and one that was deployed completely must
not print the same line.

Tests: build/scripts/tools/check_deployed_runtimes_test.py
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import mmap
import os
import pathlib
import re
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_build_graph as graph  # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

# Which generated tree produces which deployment directory. The toolchain component of the
# deployment path is the same one `afternight-common.lua` derives the tree from, so the pairing is
# read here rather than guessed from the host this runs on: a Linux deployment can be inspected from
# Windows and every answer but one must not change - what the system's loader cache carries is read
# from the running host, so that clause is decided on a host of the deployment's kind and reported as
# undecided anywhere else.
TREE_BY_TOOLCHAIN = {
    'msvc': ('vs2026-msvc', os.path.join('build', 'vs2026-msvc'), 'vs'),
    'clang-cl': ('vs2026-clang', os.path.join('build', 'vs2026-clang'), 'vs'),
    'clang': ('make-linux', os.path.join('build', 'make-linux'), 'make'),
}

# Every engine the build graph knows, each of which the deployment has to hold. This is the property
# the whole arrangement rests on, so it is named rather than derived from the expectation: an
# expectation that had quietly lost an engine would otherwise pass every other check in this file.
ENGINE_PROJECTS = graph.ENGINE_PROJECTS

# What makes a file this product's own binary rather than something deployed beside it. The prefix
# is matched with case, because the privately packaged RAW libraries are spelled in lower case and
# belong to `deploy_raw_deps.py` rather than to either solution.
PRODUCT_PREFIXES = ('AfterNight', 'Test_', 'Fuzz_', 'EngineBench')
BINARY_SUFFIXES = ('.dll', '.exe', '.so')

# The record the target-select build scripts keep beside the binaries of the instrumentation they were built with -
# `none`, or a sanitizer's name and what it was built with after a `+` - and what an instrumented binary names: the
# sanitizer runtime's own entry points, or on Windows its import.
INSTRUMENTATION_RECORD = '.engine-instrumentation'
SANITIZER_MARKERS = (b'__asan_init', b'__tsan_init', b'clang_rt.asan')

# The Visual C++ runtime libraries Microsoft redistributes, which a deployment carries when its binaries import them.
VISUAL_CPP_RUNTIME = re.compile(
    r'(vcruntime140(_1|_threads)?|msvcp140(_1|_2|_atomic_wait|_codecvt_ids)?|concrt140|vccorlib140|vcomp140)\.dll',
    re.IGNORECASE)

# Qt's own libraries, which a Linux binary that links Qt finds through its one run path, `lib/` beside it.
QT_LIBRARY = re.compile(r'libQt6\w*\.so(\.\d+)*')

# An ELF section that holds the dynamic linking table, and the table's entries that end it, name a needed library and
# state a run path.
SHT_DYNAMIC = 6
DT_NULL = 0
DT_NEEDED = 1
DT_RPATH = 15
DT_RUNPATH = 29

# The directories a run path may name and still be any machine's: the system's own.
SYSTEM_DIRECTORIES = ('/usr/', '/lib/', '/lib64/')

# The section name an unstripped ELF binary still carries. `objcopy --strip-debug` removes the debug
# sections and their names with them, so its presence answers "was this stripped" without linking a
# symbol reader into this check.
DEBUG_SECTION_NAME = b'.debug_info'

Expected = collections.namedtuple('Expected', ('name', 'symbol', 'project'))
Reading = collections.namedtuple('Reading', ('expected', 'present', 'with_symbols', 'foreign'))


def _basename(path):
    return os.path.basename(path.replace('\\', '/').rstrip('/'))


def _directory_of(target, artifact):
    """The directory one artifact of one target resolves to, as the graph's own key resolves it."""
    key = graph._artifact_key(target, 'target binary', artifact)
    return os.path.dirname(key)


def tree_for(directory):
    """The generated tree whose projects deploy into this directory, or None for an unknown one."""
    parts = os.path.normpath(os.path.abspath(directory)).replace('\\', '/').split('/')
    if len(parts) < 2:
        return None
    return TREE_BY_TOOLCHAIN.get(parts[-2])


def expected_deployment(repo_root, directory):
    """What the generated graph places in one deployment directory, as {file name: Expected}.

    Returns None when the tree that would produce this directory is not generated, so that an
    ungenerated checkout is reported as such rather than as an empty expectation.
    """
    tree = tree_for(directory)
    if tree is None:
        return None
    label, relative, kind = tree
    workspaces = graph.load_tree(repo_root, label, relative, kind)
    if workspaces is None:
        return None
    wanted = os.path.normcase(os.path.normpath(os.path.abspath(directory))).lower()
    expected = {}
    for workspace in workspaces:
        for name in sorted(workspace.targets):
            target = workspace.targets[name]
            binaries = [artifact for kind_, artifact in target.artifacts if kind_ == 'target binary']
            symbols = [artifact for kind_, artifact in target.artifacts if kind_ == 'symbol file']
            for binary in binaries:
                if _directory_of(target, binary) != wanted:
                    continue
                produced = _basename(binary)
                stem = os.path.splitext(produced)[0]
                # The symbol file of *this* configuration: a project states one per configuration and
                # they resolve into their own directories, so the one beside this binary is the one
                # whose own directory matches and whose name is built from the same target name.
                companion = ''
                for symbol in symbols:
                    if _directory_of(target, symbol) != wanted:
                        continue
                    candidate = _basename(symbol)
                    if candidate.startswith(stem) or candidate.startswith(produced):
                        companion = candidate
                        break
                expected[produced] = Expected(name=produced, symbol=companion, project=name)
    return expected


def _is_product_binary(name):
    if not name.lower().endswith(BINARY_SUFFIXES) and '.so.' not in name:
        return False
    stem = name[3:] if name.startswith('lib') else name
    return stem.startswith(PRODUCT_PREFIXES)


def _looks_stripped(data):
    """Whether an ELF image has had its debug sections removed.

    Read as a byte scan for the section name rather than by walking the section table, because the
    only question here is whether the names of the debug sections survived, and a file that is not
    an ELF binary at all answers "not stripped" and is judged by its sidecar instead.
    """
    return DEBUG_SECTION_NAME not in data


def _codeview(data):
    """(pdb name, signature) from a PE image's CodeView record, or None when it carries none."""
    try:
        header = struct.unpack_from('<I', data, 0x3c)[0]
        if data[header:header + 4] != b'PE\0\0':
            return None
        sections, = struct.unpack_from('<H', data, header + 6)
        optional_size, = struct.unpack_from('<H', data, header + 20)
        optional = header + 24
        magic, = struct.unpack_from('<H', data, optional)
        directory = optional + (112 if magic == 0x20b else 96) + 6 * 8
        address, size = struct.unpack_from('<II', data, directory)
        if not address or not size:
            return None
        table = optional + optional_size
        placed = []
        for index in range(sections):
            entry = table + index * 40
            virtual_size, virtual_address = struct.unpack_from('<II', data, entry + 8)
            raw, = struct.unpack_from('<I', data, entry + 20)
            placed.append((virtual_address, max(virtual_size, 1), raw))
        offset = None
        for virtual_address, virtual_size, raw in placed:
            if virtual_address <= address < virtual_address + virtual_size:
                offset = raw + (address - virtual_address)
                break
        if offset is None:
            return None
        for index in range(size // 28):
            entry = offset + index * 28
            kind, length = struct.unpack_from('<II', data, entry + 12)
            raw, = struct.unpack_from('<I', data, entry + 24)
            if kind != 2:
                continue
            record = data[raw:raw + length]
            if record[:4] != b'RSDS' or len(record) < 25:
                continue
            signature = record[4:20]
            name = record[24:].split(b'\0')[0].decode('utf-8', 'replace')
            return _basename(name), signature
    except (struct.error, IndexError):
        return None
    return None


def _debuglink(data):
    """(sidecar name, CRC-32) from an ELF image's `.gnu_debuglink`, or None when it has none."""
    try:
        if data[:4] != b'\x7fELF' or data[4] != 2:
            return None
        table, = struct.unpack_from('<Q', data, 0x28)
        entry_size, count, names_index = struct.unpack_from('<HHH', data, 0x3a)
        if not table or not count:
            return None
        names = table + names_index * entry_size
        names_offset, = struct.unpack_from('<Q', data, names + 0x18)
        for index in range(count):
            header = table + index * entry_size
            name_offset, = struct.unpack_from('<I', data, header)
            name_end = data.index(b'\0', names_offset + name_offset)
            if data[names_offset + name_offset:name_end] != b'.gnu_debuglink':
                continue
            offset, size = struct.unpack_from('<QQ', data, header + 0x18)
            section = data[offset:offset + size]
            name = section.split(b'\0')[0]
            checksum, = struct.unpack_from('<I', section, len(section) - 4)
            return name.decode('utf-8', 'replace'), checksum
    except (struct.error, IndexError, ValueError):
        return None
    return None


def _elf_dynamic(path):
    """The (tag, text) entries of a 64-bit little-endian ELF binary's dynamic section that name a string.

    @param path the file.
    @return the needed libraries and run paths in the order the section states them; empty for a file that is not such a
    binary or has no dynamic section.
    """
    try:
        with open(path, 'rb') as handle:
            if handle.read(6) != b'\x7fELF\x02\x01':
                return []
            data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
    except (OSError, ValueError):
        return []
    with data:
        try:
            table, = struct.unpack_from('<Q', data, 0x28)
            entry_size, count = struct.unpack_from('<HH', data, 0x3a)
            for index in range(count):
                header = table + index * entry_size
                kind, = struct.unpack_from('<I', data, header + 4)
                if kind != SHT_DYNAMIC:
                    continue
                offset, size = struct.unpack_from('<QQ', data, header + 0x18)
                link, = struct.unpack_from('<I', data, header + 0x28)
                strings, = struct.unpack_from('<Q', data, table + link * entry_size + 0x18)
                entries = []
                for entry in range(offset, offset + size, 16):
                    tag, value = struct.unpack_from('<qQ', data, entry)
                    if tag == DT_NULL:
                        break
                    if tag in (DT_NEEDED, DT_RPATH, DT_RUNPATH):
                        start = strings + value
                        entries.append((tag, data[start:data.find(b'\0', start)].decode('utf-8', 'replace')))
                return entries
        except (struct.error, IndexError, ValueError):
            return []
    return []


def elf_needed_libraries(path):
    """The libraries a 64-bit little-endian ELF binary names as needed.

    @param path the file.
    @return the names, empty for a file that is not such a binary.
    """
    return {text for tag, text in _elf_dynamic(path) if tag == DT_NEEDED}


def elf_run_paths(path):
    """The entries of a 64-bit little-endian ELF binary's run path, `DT_RPATH` or `DT_RUNPATH`, in order.

    @param path the file.
    @return the entries, empty for a file that is not such a binary or states none.
    """
    return [entry for tag, text in _elf_dynamic(path) if tag in (DT_RPATH, DT_RUNPATH) for entry in text.split(':')]


def _read(path):
    try:
        with open(path, 'rb') as handle:
            return handle.read()
    except OSError:
        return b''


def _has_symbols(directory, entry):
    """Whether one deployed binary's symbols are present and are its own.

    Returns (answer, problem). The binary is asked which symbol file it was built with wherever its
    format records one, so a file of the right name from another build is a finding rather than a
    pass.
    """
    if not entry.symbol:
        return True, None
    symbol_path = os.path.join(directory, entry.symbol)
    binary = _read(os.path.join(directory, entry.name))

    stated = _codeview(binary)
    if stated is not None:
        name, signature = stated
        beside = os.path.join(directory, name)
        if not os.path.isfile(beside):
            return False, ('%s: was built with %s, which is not deployed beside it'
                           % (entry.name, name))
        if os.path.getsize(beside) == 0:
            return False, '%s: the symbol file %s is empty' % (entry.name, name)
        if signature not in _read(beside):
            return False, ('%s: %s is a different build\'s symbols; it does not carry the signature '
                           '%s was built with' % (entry.name, name, entry.name))
        return True, None

    linked = _debuglink(binary)
    if linked is not None:
        name, checksum = linked
        beside = os.path.join(directory, name)
        if not os.path.isfile(beside):
            return False, ('%s: names %s as its separated debug information, which is not deployed '
                           'beside it' % (entry.name, name))
        actual = zlib.crc32(_read(beside)) & 0xffffffff
        if actual != checksum:
            return False, ('%s: %s is a different build\'s symbols; its checksum is %08x and %s '
                           'names %08x' % (entry.name, name, actual, entry.name, checksum))
        return True, None

    if os.path.isfile(symbol_path):
        if os.path.getsize(symbol_path) == 0:
            return False, '%s: the symbol file %s is empty' % (entry.name, entry.symbol)
        return True, None
    if entry.symbol.endswith('.debug') and not _looks_stripped(binary):
        # A Linux Debug build writes no sidecar because it never split anything out.
        return True, None
    return False, ('%s: deployed without its symbols; %s is missing, and %s carries none of its own'
                   % (entry.name, entry.symbol, entry.name))


def _digest(path):
    hashed = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            hashed.update(block)
    return hashed.hexdigest()


def recorded_instrumentation(directory):
    """The sanitizer the directory's record names, or `none` when it names none or there is no record."""
    try:
        with open(os.path.join(directory, INSTRUMENTATION_RECORD), encoding='utf-8', errors='replace') as handle:
            text = handle.read().strip()
    except OSError:
        return 'none'
    return text.split('+', 1)[0].strip() or 'none'


def instrumented_binaries(directory):
    """Every ELF or PE file below the directory that names a sanitizer runtime, relative to it, in order.

    A debug sidecar is never loaded and a link is read where it points, so neither is read here.
    """
    found = []
    for base, directories, files in os.walk(directory):
        directories.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            if name.endswith('.debug') or os.path.islink(path):
                continue
            try:
                with open(path, 'rb') as handle:
                    magic = handle.read(4)
                    if magic != b'\x7fELF' and magic[:2] != b'MZ':
                        continue
                    data = magic + handle.read()
            except OSError:
                continue
            if any(marker in data for marker in SANITIZER_MARKERS):
                found.append(os.path.relpath(path, directory).replace(os.sep, '/'))
    return found


def _pe_imports(path):
    """A PE binary's imports, read directly; an ELF binary or a file that is not one imports nothing here."""
    with open(path, 'rb') as handle:
        if handle.read(2) != b'MZ':
            return set()
    import deploy_raw_deps  # noqa: E402 - the reader the RAW deployment already proves
    try:
        return deploy_raw_deps.pe_imports(pathlib.Path(path))
    except (ValueError, IndexError):
        return set()


def missing_visual_cpp_runtime(directory, imports_of=_pe_imports):
    """Each Visual C++ runtime library a binary at the directory's top level imports and the directory does not carry.

    @param directory the deployed directory.
    @param imports_of what a binary imports, by name.
    @return one problem per binary and library, in order.
    """
    present = {name.lower() for name in os.listdir(directory)}
    problems = []
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if not name.lower().endswith(('.dll', '.exe')) or not os.path.isfile(path):
            continue
        for imported in sorted(imports_of(path) or (), key=str.lower):
            if VISUAL_CPP_RUNTIME.fullmatch(imported) and imported.lower() not in present:
                problems.append('%s: %s imports %s, a Visual C++ runtime library the deployment does not carry'
                                % (directory, name, imported))
    return problems


def missing_qt_libraries(directory, needed_of=elf_needed_libraries):
    """Each Qt library a binary at the directory's top level needs and the directory's `lib/` does not carry.

    @param directory the deployed directory.
    @param needed_of the libraries a binary names as needed.
    @return one problem per binary and library, in order.
    """
    library = os.path.join(directory, 'lib')
    problems = []
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if name.endswith('.debug') or os.path.islink(path) or not os.path.isfile(path):
            continue
        for needed in sorted(needed_of(path) or ()):
            if QT_LIBRARY.fullmatch(needed) and not os.path.isfile(os.path.join(library, needed)):
                problems.append('%s: %s needs %s, which lib/, its one run path, does not carry, so the host\'s copy is '
                                'loaded in its place' % (directory, name, needed))
    return problems


# The validation tools beside the product, which their runners start with the deployment on the library path and no
# package ships: the suites, the fuzz targets and the two drivers.
VALIDATION_TOOL_PREFIXES = ('Test_', 'Fuzz_', 'EngineBench', 'AfterNightEngineBench', 'AfterNightWorkflowDriver')

# What `ldconfig -p` prints for one library of the system's cache: its name, its kind and where it is.
_LOADER_CACHE_ENTRY = re.compile(r'^\s+(\S+) \(([^)]*)\) => \S+')


# Where ldconfig is found: on the path, or in the system directories a user's path need not name (Debian's does not).
_LDCONFIG_CANDIDATES = ('ldconfig', '/sbin/ldconfig', '/usr/sbin/ldconfig')

# The system's cache before anything asked for it.
_UNREAD = object()


def system_libraries(run=subprocess.run):
    """The names the host's dynamic loader cache resolves for a 64-bit x86 binary, or None where no ldconfig runs here -
    a host without one, Windows among them - which is not the answer a cache that holds nothing gives.

    @param run how a command is run, with subprocess.run's signature.
    @return the set of names, or None when the cache could not be read.
    """
    for candidate in _LDCONFIG_CANDIDATES:
        try:
            listed = run([candidate, '-p'], capture_output=True, text=True, check=False).stdout
        except OSError:
            continue
        return {match.group(1) for match in map(_LOADER_CACHE_ENTRY.match, listed.splitlines())
                if match and 'x86-64' in match.group(2)}
    return None


def unresolved_libraries(directory, needed_of=elf_needed_libraries, run_paths_of=elf_run_paths, system=None,
                         read_system=system_libraries):
    """Each library a binary the product ships at the directory's top level needs, directly or through the libraries it
    loads, that resolves neither where the needing file's run path looks nor in the system's cache. The validation
    tools are left to their runners, which put the deployment on the library path. Where the host's cache cannot be
    read - no ldconfig runs here - a library the deployment does not carry is reported as undecided rather than missing,
    once per binary: a Windows host reading a Linux deployment knows nothing of the system the deployment is for, and
    naming every library of the C runtime as missing would say otherwise.

    @param directory the deployed directory.
    @param needed_of the libraries a binary names as needed.
    @param run_paths_of the entries of a binary's run path.
    @param system the names the system's cache resolves; read through read_system when None.
    @param read_system how the host's cache is read when no system is given; None from it is a cache that could not be
    read.
    @return one problem per binary and library, in order, or one per binary where the cache could not be read.
    """
    problems = []
    cache = system if system is not None else _UNREAD
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        stem = name[3:] if name.startswith('lib') else name
        if not stem.startswith(PRODUCT_PREFIXES) or stem.startswith(VALIDATION_TOOL_PREFIXES):
            continue
        if name.endswith('.debug') or os.path.islink(path) or not os.path.isfile(path):
            continue
        undecided = set()
        pending, seen = [path], {os.path.realpath(path)}
        while pending:
            current = pending.pop(0)
            origin = os.path.dirname(current)
            searched = [entry.replace('${ORIGIN}', origin).replace('$ORIGIN', origin)
                        for entry in run_paths_of(current) or ()]
            for needed in sorted(needed_of(current) or ()):
                found = next((os.path.join(entry, needed) for entry in searched
                              if os.path.isfile(os.path.join(entry, needed))), None)
                if found is not None:
                    if os.path.realpath(found) not in seen:
                        seen.add(os.path.realpath(found))
                        pending.append(found)
                    continue
                if cache is _UNREAD:
                    cache = read_system()
                if cache is None:
                    undecided.add(needed)
                elif needed not in cache:
                    through = ('' if current == path else ' through %s'
                               % os.path.relpath(current, directory).replace(os.sep, '/'))
                    problems.append('%s: %s needs %s%s, which neither the deployment nor the system carries'
                                    % (directory, name, needed, through))
        if undecided:
            problems.append('%s: %s needs %s, which the deployment does not carry; the loader cache could not be read '
                            'here (no ldconfig runs on this host), so whether the system carries %s is undecided'
                            % (directory, name, ', '.join(sorted(undecided)), 'it' if len(undecided) == 1 else 'them'))
    return problems


def run_paths_of_this_host(directory, run_paths_of=elf_run_paths):
    """Each binary below the directory whose run path names a directory of the machine that built it.

    A directory of this host is an absolute one that exists here outside the system's own. A debug sidecar is never
    loaded and a link is read where it points, so neither is read here.

    @param directory the deployed directory.
    @param run_paths_of the entries of a binary's run path.
    @return one problem per binary and entry, in order.
    """
    problems = []
    for base, directories, files in os.walk(directory):
        directories.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            if name.endswith('.debug') or os.path.islink(path):
                continue
            for entry in run_paths_of(path) or ():
                if ((entry.startswith('/') or os.path.isabs(entry))
                        and not (entry.rstrip('/') + '/').startswith(SYSTEM_DIRECTORIES) and os.path.isdir(entry)):
                    problems.append('%s: %s names %s in its run path, a directory of the machine that built it'
                                    % (directory, os.path.relpath(path, directory).replace(os.sep, '/'), entry))
    return problems


# What an executable that declares itself long-path aware carries in the manifest the toolset embeds in it.
LONG_PATH_AWARE = b'longPathAware>true<'


def executables_not_long_path_aware(directory):
    """Each executable of the product at the directory's top level that does not declare itself long-path aware.

    A test, a library and an executable another project ships are not held: the rule is the product's own processes.
    """
    problems = []
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if not name.startswith('AfterNight') or not name.lower().endswith('.exe') or not os.path.isfile(path):
            continue
        data = _read(path)
        if data[:2] == b'MZ' and LONG_PATH_AWARE not in data:
            problems.append('%s: %s does not declare itself long-path aware' % (directory, name))
    return problems


def check_deployment(directory, expected, deployed=None, imports_of=_pe_imports, needed_of=elf_needed_libraries,
                     run_paths_of=elf_run_paths, system_libraries=None):
    """Hold one deployed directory to what the graph places there. Returns (problems, Reading)."""
    problems = []
    if deployed is None:
        deployed = sorted(name for name in os.listdir(directory)
                          if os.path.isfile(os.path.join(directory, name)))
    if not expected:
        problems.append('%s: the generated graph places no binary here, so this reading proves '
                        'nothing about it' % directory)
        return problems, Reading(0, 0, 0, 0)

    present = []
    with_symbols = 0
    for name in sorted(expected):
        entry = expected[name]
        if not os.path.isfile(os.path.join(directory, name)):
            problems.append('%s: %s is missing, which %s produces'
                            % (directory, name, entry.project))
            continue
        present.append(name)
        good, problem = _has_symbols(directory, entry)
        if good:
            with_symbols += 1
        else:
            problems.append(problem)

    # The engine, as a file of its own. An engine the graph places here but the deployment lacks has
    # already been reported by name above, so this looks at what the graph itself offers: an
    # expectation that names no binary of the engine is a directory this arrangement was never true of.
    declared = {}
    for name, entry in expected.items():
        if entry.project in ENGINE_PROJECTS:
            declared[entry.project] = name
    if len(declared) < len(ENGINE_PROJECTS):
        absent = [project for project in ENGINE_PROJECTS if project not in declared]
        problems.append('%s: the engine has to be deployed here, and the graph places no binary of %s '
                        'here' % (directory, ', '.join(absent)))

    # Two deployed names that differ only in case.
    by_folded = collections.defaultdict(list)
    for name in deployed:
        by_folded[name.lower()].append(name)
    for folded in sorted(by_folded):
        names = sorted(by_folded[folded])
        if len(names) > 1:
            problems.append('%s: %s differ only in case, so they are one file where a filesystem or '
                            'a packager matches without it' % (directory, ' and '.join(names)))

    # A binary of this product that no generated project produces any more.
    foreign = 0
    produced = {name.lower() for name in expected}
    produced.update(entry.symbol.lower() for entry in expected.values() if entry.symbol)
    for name in deployed:
        if not _is_product_binary(name):
            continue
        if name.lower() in produced:
            continue
        foreign += 1
        problems.append('%s: %s is deployed but no generated project produces it, so it is left over '
                        'from a target that was renamed, removed or generated optionally'
                        % (directory, name))

    # A binary built with a sanitizer below a directory that records none. A sanitizer generation builds into the same
    # directory as an ordinary one, so whatever no ordinary build rebuilt after it stays instrumented - a plugin in a
    # directory of its own as much as a library beside the application - and no process without that runtime loads it.
    if recorded_instrumentation(directory) == 'none':
        for relative in instrumented_binaries(directory):
            problems.append('%s: %s carries a sanitizer runtime\'s entry points, though the directory records no '
                            'instrumentation, so no process without that runtime can load it and a package made from '
                            'here ships it' % (directory, relative))

    problems.extend(missing_visual_cpp_runtime(directory, imports_of))
    problems.extend(missing_qt_libraries(directory, needed_of))
    problems.extend(unresolved_libraries(directory, needed_of, run_paths_of, system_libraries))
    problems.extend(run_paths_of_this_host(directory, run_paths_of))
    problems.extend(executables_not_long_path_aware(directory))

    return problems, Reading(len(expected), len(present), with_symbols, foreign)


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=REPO_ROOT,
                        help='repository root to inspect (default: the checkout this script lives in)')
    parser.add_argument('--binaries', required=True,
                        help='the deployed directory to read, such as bin/msvc/release')
    options = parser.parse_args(argv)
    stream = stream or sys.stdout

    directory = os.path.abspath(options.binaries)
    if not os.path.isdir(directory):
        print('%s: no such directory, so nothing was read' % directory, file=stream)
        return 1

    expected = expected_deployment(options.repo_root, directory)
    if expected is None:
        print('%s: no generated tree produces this directory, so nothing was read' % directory,
              file=stream)
        return 1

    problems, reading = check_deployment(directory, expected)
    for problem in problems:
        print(problem, file=stream)
    if problems:
        print('%d deployment problem(s).' % len(problems), file=stream)
        return 1
    print('%d binaries the generated graph deploys into %s are present, all %d with their symbols, '
          'every engine among them, nothing of this product is left over there, the Visual C++ runtime its binaries '
          'import is beside them, the Qt libraries they need are in lib/, every library they need resolves, no run path '
          'below it names a directory of this host, and every binary below it is built with the instrumentation it '
          'records (%s)'
          % (reading.present, directory, reading.with_symbols, recorded_instrumentation(directory)), file=stream)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

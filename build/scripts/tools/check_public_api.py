# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the published surface of AfterNightEngine to the snapshot it was frozen at.

This engine's binary interface is version-locked in the tree rather than promised to anyone: the
library reports the version and the interface generation it was built from, the caller's headers
state their own, and a disagreement is fatal. That machinery detects a *stale* consumer. It does
nothing about the change that produced the staleness, because nothing reads the surface itself -
whether a member arrived in a public struct, whether an exported signature moved, whether an
enumerator was renumbered - so the two numbers that make the check work are maintained by whoever
remembers to maintain them.

This reads the surface and compares it with what was recorded, which turns that discipline into a
rule with two tiers:

1. **Any difference at all requires the version triple to have moved.** The recorded triple belongs
   to the recorded surface; a surface that has changed under an unchanged version is a version that
   describes two different things.

2. **A difference an already-compiled object would be wrong about additionally requires the
   interface generation to have moved.** Those are exactly the ones `Core/Version.h` names: a member
   added to or removed from a public type, a parameter or return type changed, an exported
   declaration withdrawn, an enumerator renumbered or removed. An addition - a new header, a new
   type, a new declaration, an enumerator appended - is not one of them, because an object compiled
   without it is still right about everything it does know.

What it reads is the declarations, not the prose: comments are removed before anything is compared,
so the documentation this engine requires of every public symbol can be rewritten without touching
the record, and a declaration's lines are joined without the space a break after its opening parenthesis
leaves, so a formatter can move that break without touching it either. A record states that joining, and
one written before it did is read under the rule it was written with. What it does not read is the implementation, which is `check_include_layers.py`'s and
`check_header_architecture.py`'s, or whether a declaration is documented at all, which is
`check_public_documentation.py`'s.

The components are the ones every default tree builds and publishes an include root for: the engine
and its two optional adapters, each of which a consumer may compile against. A component a tree only
contains when it was generated with a switch is deliberately absent - a freeze record that describes
some trees is not a freeze record.

A freeze is only as good as every change that walked through it, and the record at the tip says
nothing about the commits before it: a change that moved the surface without moving the version, and a
later one that moved the version for something else, leave a tip that checks clean. `--history <base>`
reads the surface again at every commit since `base` that touched a published header or the record, from
git rather than the working tree, and holds each one to two things: that its record described its own
surface, and that what it changed against its first parent's record obeyed the two tiers above.

There is a second reading, because the first one reads promises. `--binaries <dir>` opens the built
libraries and compares what they export with what the headers declare exported, in both directions:
a declaration no symbol backs is a link error in the first program outside this engine and nothing
before it, and a symbol no declaration names is private code that has reached the binary interface.
It needs a build and a symbol reader, so it is recorded per host rather than run from a checkout.

Usage:
    python build/scripts/tools/check_public_api.py [--repo-root <dir>] [--snapshot <file>]
    python build/scripts/tools/check_public_api.py --record
    python build/scripts/tools/check_public_api.py --history <base commit>
    python build/scripts/tools/check_public_api.py --binaries bin/msvc/release [--binary-tool <llvm-readobj>]

Exits 0 when the surface is the one recorded, and 1 otherwise, printing one line per difference plus
the rule each one has to satisfy before the snapshot is recorded again. A difference never passes by
itself: a freeze that a change can walk past without anyone reading it is not a freeze.

Tests: build/scripts/tools/check_public_api_test.py
"""

from __future__ import annotations

import argparse
import hashlib
import io
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import source_scan  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

SNAPSHOT = 'build/scripts/tools/public_api.txt'

VERSION_HEADER = 'src/AfterNightEngine/include/AfterNightEngine/Core/Version.h'


class Component:
    """One library with a published include root, and the macro that marks its exports."""

    def __init__(self, name, include_root, export_macro, library):
        self.name = name
        self.include_root = include_root
        self.export_macro = export_macro
        self.library = library

    def headers(self, repo_root):
        """Every published header of this component, by path relative to its include root."""
        root = repo_root / self.include_root
        if not root.is_dir():
            return []
        return sorted(str(path.relative_to(root)).replace('\\', '/')
                      for path in root.rglob('*.h'))


COMPONENTS = (
    Component('engine',
              'src/AfterNightEngine/include/AfterNightEngine',
              'AFTERNIGHTENGINE_API',
              'AfterNightEngine'),
    Component('python-adapter',
              'src/AfterNightEnginePythonAdapter/include/AfterNightEnginePythonAdapter',
              'AFTERNIGHTENGINEPYTHONADAPTER_API',
              'AfterNightEnginePythonAdapter'),
    Component('gmic-adapter',
              'src/AfterNightEngineGmicAdapter/include/AfterNightEngineGmicAdapter',
              'AFTERNIGHTENGINEGMICADAPTER_API',
              'AfterNightEngineGmicAdapter'),
)

# A difference an object compiled against the older header would be wrong about.
ABI = 'abi'

# A difference that surface alone, which such an object is still right about.
SURFACE = 'surface'

DIRECTIVE = re.compile(r'^[ \t]*#')

# The arguments of an explicit specialization are part of the name: `SampleTypeTraits<UInt8>` and
# `SampleTypeTraits<Float64>` are two types, and a record that called them one would not round-trip.
# A type may carry a decoration between the keyword and its name - the macro that gives a
# polymorphic type visible identity outside this library. The optional group only consumes a
# capitalised token when a name still follows it, so a type whose own name is capitals keeps it.
TYPE_HEAD = re.compile(r'\b(class|struct)\s+(?:[A-Z][A-Z0-9_]*\s+)?([A-Za-z_]\w*)\s*(<[^;{]*>)?')

# The decoration TYPE_HEAD steps over, read on its own so that gaining one can be told from
# losing one.
TYPE_DECORATION = re.compile(r'\b(class|struct)\s+[A-Z][A-Z0-9_]*\s+(?=[A-Za-z_])')

ENUM_HEAD = re.compile(r'\benum\s+(?:class|struct)?\s*([A-Za-z_]\w*)')

NAMESPACE_HEAD = re.compile(r'\bnamespace\s+([A-Za-z_][\w:]*)?\s*$')

NOT_A_MEMBER = ('using ', 'typedef ', 'static_assert', 'friend ', 'template ', 'template<',
                'public:', 'private:', 'protected:')

# `operator=`, `operator()` and their siblings carry the punctuation this walk uses to tell a
# function from a member, so they are spelled as a plain name before anything is decided.
OPERATOR = re.compile(r'operator\s*(?:\(\)|\[\]|new\b|delete\b|[^\w\s(]+)')

# Every component read here lives in this one namespace, so a mangling that opens with it names
# this engine's code and one that does not names somebody else's.
ENGINE_NAMESPACE = 'an'

VERSION_STRING = re.compile(r'kEngineVersionString\s*=\s*"([^"]+)"')

ABI_GENERATION = re.compile(r'kEngineAbiGeneration\s*=\s*(\d+)')


class Entity:
    """One recorded fact about the surface: what it is, where it is, and what it says."""

    def __init__(self, kind, component, path, name, text):
        self.kind = kind
        self.component = component
        self.path = path
        self.name = name
        self.text = text

    @property
    def key(self):
        """What makes two entities the same thing across two readings of the tree.

        A declaration's own text is its identity, so an overload added beside one is an addition
        while a parameter changed on one is a removal and an addition - which is what it is to an
        object that already compiled the old spelling. The path is deliberately not part of it: a
        declaration moved between two published headers is the same declaration.
        """
        if self.kind == 'api':
            return (self.kind, self.component, self.text)
        return (self.kind, self.component, self.name)

    @property
    def line(self):
        """The snapshot line for this entity."""
        return '%s %s %s %s %s' % (self.kind, self.component, self.path, self.name, self.text)


# How a declaration's lines are joined into one line of text. A formatter that moves a line break to just after an
# opening parenthesis, or from there, changes nothing a header declares, so the space that break leaves is not part of
# the text. Records written before a record stated its joining are joining 1, which kept that space in a declaration's
# text and in the digests; such a record is read under its own rule, which is what lets the history cross the commit
# that recorded the surface under this one.
JOINING = 2

AFTER_OPENING_PARENTHESIS = re.compile(r'\( ')


def normalize(text, joining=JOINING):
    """One line of whitespace-collapsed declaration text."""
    joined = ' '.join(text.split())
    return joined if joining < 2 else AFTER_OPENING_PARENTHESIS.sub('(', joined)


def without_directives(text):
    """The source with every preprocessor line removed and its line count preserved.

    A directive is not a declaration, and a macro definition can carry braces that would be read as
    a scope by the walk below. Continuations are removed with the line that opened them.
    """
    kept = []
    continued = False
    for line in text.split('\n'):
        directive = continued or DIRECTIVE.match(line) is not None
        continued = directive and line.rstrip().endswith('\\')
        kept.append('' if directive else line)
    return '\n'.join(kept)


def declarations(text):
    """The header reduced to what it declares: no comments, no directives, no literal contents.

    The contents of literals are blanked because a brace or a semicolon inside one is punctuation
    rather than structure, and the walk below reads structure. What that costs - a changed default
    string is invisible to an entity's text - is paid by `header_digest`, which reads the literals.
    """
    code, _ = source_scan.blank_literals(source_scan.strip_comments(text))
    return without_directives(code)


# The spelling of the export macros and the library's name that a record's header digests are taken under. A
# rename of those names changes nothing a header declares, so a header is digested with them spelled this way
# and a rename moves no digest, which keeps records comparable across one. The replacements are built from parts
# so that a rename of the names in this tree leaves them as they are; on a header that already spells them this
# way they change nothing.
FROZEN_SPELLINGS = (
    (re.compile('AFTERNIGHTENGINE(?!' + 'NEXT)'), 'AFTERNIGHTENGINE' + 'NEXT'),
    (re.compile('AfterNightEngine(?!' + 'Next)'), 'AfterNightEngine' + 'Next'),
)


def frozen_spelling(text):
    """The text with the canonical names spelled as the surface was frozen."""
    for pattern, spelling in FROZEN_SPELLINGS:
        text = pattern.sub(spelling, text)
    return text


# The other direction, for the history: a commit whose headers, directories and record spell the names as the
# digests do is read under the published names, so a commit that renames them is compared with a parent that
# publishes what it does, under the names it publishes it by.
CANONICAL_SPELLINGS = (
    (re.compile('AFTERNIGHTENGINE' + 'NEXT'), 'AFTERNIGHTENGINE'),
    (re.compile('AfterNightEngine' + 'Next'), 'AfterNightEngine'),
)


def canonical_spelling(text):
    """The text with the names the digests spell replaced by the published ones."""
    for pattern, spelling in CANONICAL_SPELLINGS:
        text = pattern.sub(spelling, text)
    return text


def header_digest(text, joining=JOINING):
    """A digest of everything a header declares, literals included.

    Deliberately not taken over the file: the documentation this engine requires of every public
    symbol is rewritten far more often than the surface is, and a record that moved every time a
    sentence did would be re-recorded without being read. Taken over the frozen spelling of the names,
    with the lines joined as `joining` joins them.
    """
    return hashlib.sha256(frozen_spelling(
        normalize(without_directives(source_scan.strip_comments(text)), joining)).encode('utf-8')).hexdigest()[:16]


def _matching_brace(text, opening):
    """One past the `}` closing the `{` at `opening`, or the end of the text."""
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return index + 1
    return len(text)


def _enumerators(body):
    """The enumerators of one enumeration body, as `name=value` or `name` in declared order."""
    result = []
    depth = 0
    current = []
    for character in body:
        if character in '([{<':
            depth += 1
        elif character in ')]}>':
            depth -= 1
        if character == ',' and depth == 0:
            result.append(normalize(''.join(current)))
            current = []
        else:
            current.append(character)
    result.append(normalize(''.join(current)))
    return [entry.replace(' ', '') for entry in result if entry]


def _scope_name(scope):
    """The qualified name of the current scope, `an::Image` and not `an :: Image`."""
    return '::'.join(name for kind, name, _ in scope if name)


def _classify_head(head):
    """What a `{` opens: a namespace, a type, an enumeration, or something with no name here."""
    head = normalize(head)
    if not head:
        return 'other', ''
    if ENUM_HEAD.search(head) and ' enum' in ' ' + head:
        match = ENUM_HEAD.search(head)
        return 'enum', match.group(1)
    namespace = NAMESPACE_HEAD.search(head)
    if namespace is not None:
        return 'namespace', namespace.group(1) or ''
    declaration = head[:head.index(' : ')] if ' : ' in head else head
    match = None
    for match in TYPE_HEAD.finditer(declaration):
        pass
    if match is not None:
        return 'type', match.group(2) + (match.group(3) or '').replace(' ', '')
    return 'other', ''


def _is_member(statement):
    """Whether a statement inside a type declares a non-static data member.

    Decided by what comes first: a declarator followed by `(` is a function, and anything else that
    reaches a `;` inside a type body and is not a declaration of some other kind holds storage.
    """
    if not statement or statement.startswith(NOT_A_MEMBER):
        return False
    if statement.startswith('static ') or ' static ' in statement:
        return False
    if statement.startswith('enum ') or statement.startswith('class ') or statement.startswith('struct '):
        return False
    spelled = OPERATOR.sub('operator_', statement)
    first = min((spelled.index(character) for character in '(={' if character in spelled),
                default=len(spelled))
    return not (first < len(spelled) and spelled[first] == '(')


def read_component(component, repo_root, joining=JOINING):
    """Every entity this component publishes, plus the digest of each header's declarations taken under `joining`."""
    entities = []
    digests = {}
    root = repo_root / component.include_root
    for relative in component.headers(repo_root):
        text = (root / relative).read_text(encoding='utf-8', errors='replace')
        digests[relative] = header_digest(text, joining)
        entities.extend(_entities_of(component, relative, declarations(text)))
    return entities, digests


def _entities_of(component, relative, code):
    """The walk: one pass over a header's declarations, tracking the scope each one sits in."""
    found = []
    scope = []
    depth = 0
    pending = []
    index = 0
    length = len(code)
    while index < length:
        character = code[index]
        if character not in '{};':
            pending.append(character)
            index += 1
            continue
        statement = normalize(''.join(pending))
        if character == ';':
            pending = []
            index += 1
            if component.export_macro in statement:
                found.append(Entity('api', component.name, relative, _scope_name(scope), statement))
            elif scope and scope[-1][0] == 'type' and _is_member(statement):
                found.append(Entity('member', component.name, relative,
                                    '%s#%03d' % (_scope_name(scope), _member_count(found, scope)),
                                    statement))
            continue
        if character == '}':
            pending = []
            index += 1
            if scope and scope[-1][2] == depth:
                scope.pop()
            depth -= 1
            continue
        kind, name = _classify_head(statement)
        if kind == 'other':
            # A `{` that is not a scope is an initializer - a member's, or a default argument's -
            # and it belongs to the statement being read rather than opening one. Which it is, is
            # decided by what follows the matching brace: a declaration continues, a body does not.
            closing = _matching_brace(code, index)
            following = code[closing:closing + 8].lstrip()[:1]
            if following in (';', ',', ')'):
                pending.extend(' ' + code[index:closing])
                index = closing
                continue
        pending = []
        if kind == 'enum':
            closing = _matching_brace(code, index)
            qualified = '::'.join(filter(None, [_scope_name(scope), name]))
            found.append(Entity('enum', component.name, relative, qualified,
                                ' '.join(_enumerators(code[index + 1:closing - 1]))))
            index = closing
            continue
        depth += 1
        index += 1
        if component.export_macro in statement:
            found.append(Entity('api', component.name, relative, _scope_name(scope), statement))
        if kind == 'type':
            qualified = '::'.join(filter(None, [_scope_name(scope), name]))
            found.append(Entity('type', component.name, relative, qualified, _type_shape(statement)))
            scope.append(('type', name, depth))
        elif kind == 'namespace':
            scope.append(('namespace', name, depth))
        else:
            scope.append(('other', '', depth))
    return found


def _member_count(found, scope):
    """How many members of the current type have been recorded, which orders them."""
    qualified = _scope_name(scope)
    return sum(1 for entity in found
               if entity.kind == 'member' and entity.name.rsplit('#', 1)[0] == qualified)


def _decoration_added(before, after):
    """Whether a type's head differs from the recorded one only by a decoration it did not carry.

    Adding one gives a polymorphic type an identity outside the library that it did not have, which
    adds a symbol and takes none away: an object compiled against the recorded header finds
    everything it found before. Losing one, or exchanging one for another, is the other direction
    and stays an interface change, because a consumer that derives from the type stops finding it.
    """
    return TYPE_DECORATION.search(after) is not None and TYPE_DECORATION.sub(r'\1 ', after) == before


def _type_shape(head):
    """A type's declaration without its template parameter names, which are not its shape."""
    return normalize(head)


def read_surface(repo_root, joining=JOINING):
    """Every entity of every component, and every published header's digest taken under `joining`."""
    entities = []
    digests = {}
    for component in COMPONENTS:
        found, component_digests = read_component(component, repo_root, joining)
        entities.extend(found)
        for relative, digest in component_digests.items():
            digests[(component.name, relative)] = digest
    return entities, digests


def read_version(repo_root):
    """The version triple and interface generation the public headers declare."""
    text = (repo_root / VERSION_HEADER).read_text(encoding='utf-8', errors='replace')
    version = VERSION_STRING.search(text)
    generation = ABI_GENERATION.search(text)
    if version is None or generation is None:
        raise ValueError('%s no longer states a version string and an interface generation'
                         % VERSION_HEADER)
    return version.group(1), int(generation.group(1))


def snapshot_lines(repo_root):
    """The snapshot this tree would record, as sorted lines."""
    entities, digests = read_surface(repo_root)
    version, generation = read_version(repo_root)
    lines = ['version %s %d' % (version, generation), 'joining %d' % JOINING]
    lines.extend('header %s %s %s' % (component, relative, digest)
                 for (component, relative), digest in digests.items())
    lines.extend(entity.line for entity in entities)
    return lines[:2] + sorted(lines[2:])


def recorded_joining(text):
    """The joining a recorded snapshot was written under: the one it states, or 1 for a record that states none."""
    for line in text.replace('\r\n', '\n').split('\n'):
        fields = line.strip().split()
        if len(fields) == 2 and fields[0] == 'joining':
            return int(fields[1])
    return 1


def parse_snapshot(text):
    """A recorded snapshot as its version, its generation, its digests and its entities.

    Each entity's text is read as this tool joins it, which turns a record of an earlier joining into the text the
    tool reads from the same declaration; its digests stay what they were, and are compared with the tree's taken
    under `recorded_joining`.
    """
    version = None
    generation = None
    digests = {}
    entities = []
    for line in text.replace('\r\n', '\n').split('\n'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        fields = line.split(' ', 1)
        kind = fields[0]
        if kind == 'version':
            parts = fields[1].split()
            version, generation = parts[0], int(parts[1])
            continue
        if kind == 'joining':
            continue
        if kind == 'header':
            component, relative, digest = fields[1].split(' ', 2)
            digests[(component, relative)] = digest
            continue
        component, relative, name, body = fields[1].split(' ', 3)
        entities.append(Entity(kind, component, relative, name, normalize(body)))
    return version, generation, digests, entities


def _owner(entity):
    """The type a member belongs to, which is its recorded name without its position."""
    return entity.name.rsplit('#', 1)[0]


def _enumerator_change(before, after):
    """Whether two enumerator lists differ by more than names appended to the end."""
    old = before.split()
    new = after.split()
    return old != new[:len(old)]


def differences(recorded, current):
    """Every difference between a recorded snapshot and the tree, each with the tier it is in.

    `recorded` and `current` are both (version, generation, digests, entities).
    """
    findings = []
    _, _, old_digests, old_entities = recorded
    _, _, new_digests, new_entities = current

    old_by_key = {entity.key: entity for entity in old_entities}
    new_by_key = {entity.key: entity for entity in new_entities}
    old_types = {entity.name for entity in old_entities if entity.kind == 'type'}
    new_types = {entity.name for entity in new_entities if entity.kind == 'type'}

    for key in sorted(old_by_key.keys() - new_by_key.keys(), key=repr):
        entity = old_by_key[key]
        if entity.kind == 'member' and _owner(entity) not in new_types:
            continue  # The type itself is gone, and its own finding says so.
        findings.append((ABI, '%s %s is no longer published: %s'
                         % (entity.kind, entity.name or entity.path, entity.text or entity.path)))
    for key in sorted(new_by_key.keys() - old_by_key.keys(), key=repr):
        entity = new_by_key[key]
        if entity.kind == 'member':
            if _owner(entity) not in old_types:
                continue  # A new type arrives with its members; the type's own finding says so.
            # A member appended to a type that already existed grows a layout a compiled object
            # believes it knows, which is the whole reason the generation exists.
            findings.append((ABI, 'member %s is new: %s' % (entity.name, entity.text)))
            continue
        findings.append((SURFACE, '%s %s is new: %s'
                         % (entity.kind, entity.name or entity.path, entity.text or entity.path)))
    for key in sorted(old_by_key.keys() & new_by_key.keys(), key=repr):
        before = old_by_key[key]
        after = new_by_key[key]
        if before.text == after.text:
            continue
        if before.kind == 'enum' and not _enumerator_change(before.text, after.text):
            findings.append((SURFACE, 'enum %s gained an enumerator at its end: %s'
                             % (after.name, after.text)))
            continue
        if before.kind == 'type' and _decoration_added(before.text, after.text):
            findings.append((SURFACE, 'type %s gained a visible identity: %s' % (after.name, after.text)))
            continue
        findings.append((ABI, '%s %s changed: %s -> %s' % (after.kind, after.name, before.text, after.text)))

    for path in sorted(old_digests.keys() - new_digests.keys()):
        findings.append((SURFACE, 'header %s/%s is no longer published' % path))
    for path in sorted(new_digests.keys() - old_digests.keys()):
        findings.append((SURFACE, 'header %s/%s is new' % path))
    for path in sorted(old_digests.keys() & new_digests.keys()):
        if old_digests[path] != new_digests[path]:
            findings.append((SURFACE, 'header %s/%s declares something other than what was recorded' % path))
    return findings


def check(repo_root, snapshot_path=SNAPSHOT):
    """Every problem with the tree's published surface, as printable lines."""
    path = repo_root / snapshot_path
    if not path.is_file():
        return ['%s: the surface has never been recorded; run this with --record' % snapshot_path]
    text = path.read_text(encoding='utf-8')
    recorded = parse_snapshot(text)
    entities, digests = read_surface(repo_root, recorded_joining(text))
    version, generation = read_version(repo_root)
    current = (version, generation, digests, entities)

    findings = differences(recorded, current)
    recorded_version, recorded_generation = recorded[0], recorded[1]
    if not findings:
        if (version, generation) == (recorded_version, recorded_generation):
            return []
        return ['the published surface is the recorded one, but %s states %s/%d against the recorded '
                '%s/%d; re-record so the snapshot names the version it belongs to'
                % (VERSION_HEADER, version, generation, recorded_version, recorded_generation)]

    problems = ['%s: %s' % (tier, message) for tier, message in findings]
    if version == recorded_version:
        problems.append('the published surface differs from %s while the version stays %s; a version '
                        'triple describes one surface, so move kEngineVersion* before re-recording'
                        % (snapshot_path, version))
    if any(tier == ABI for tier, _ in findings) and generation <= recorded_generation:
        problems.append('at least one difference above is one an object compiled against the recorded '
                        'headers would be wrong about, and kEngineAbiGeneration is still %d; raise it '
                        'before re-recording' % generation)
    problems.append('%s no longer describes the published surface; once the rules above hold, record '
                    'it again with --record' % snapshot_path)
    return problems


def record(repo_root, snapshot_path=SNAPSHOT):
    """Write what the tree publishes, replacing whatever was recorded."""
    path = repo_root / snapshot_path
    body = '\r\n'.join(snapshot_lines(repo_root)) + '\r\n'
    path.write_bytes(body.encode('utf-8'))
    return body.count('\r\n')


# ---------------------------------------------------------------------------------------------
# The second reading: what the built library actually exports.
#
# The snapshot above is what the headers promise. A promise that no symbol backs is a link error in
# the first program outside this engine and nothing before it, which is exactly the failure the
# standalone client exists to catch - and the client can only catch it for the declarations it
# happens to name. This reads the export table instead, so the answer covers all of them.
# ---------------------------------------------------------------------------------------------

# Two-letter Itanium codes for the operators this engine overloads. Their own names are not in the
# mangling, so every one of them answers to the same bucket on both platforms.
ITANIUM_OPERATORS = frozenset(['eq', 'ne', 'lt', 'gt', 'le', 'ge', 'ss', 'pl', 'mi', 'ml', 'dv',
                               'aS', 'ix', 'cl', 'nt', 'aa', 'oo', 'co', 'an', 'or', 'eo'])

OPERATOR_NAME = 'operator'


def declared_name(text):
    """The name an exported declaration declares, which is what a symbol table can be asked for."""
    spelled = OPERATOR.sub(OPERATOR_NAME + '_', text)
    match = re.search(r'([A-Za-z_]\w*)\s*\(', spelled)
    if match is None:
        return None
    return OPERATOR_NAME if match.group(1) == OPERATOR_NAME + '_' else match.group(1)


def microsoft_name(symbol):
    """The function name inside a Microsoft mangling, or the symbol itself when it carries none."""
    if not symbol.startswith('?'):
        return symbol
    if symbol.startswith('??0') or symbol.startswith('??1'):
        rest = symbol[3:]
    elif symbol.startswith('??'):
        return OPERATOR_NAME
    else:
        rest = symbol[1:]
    if rest.startswith('$'):
        rest = rest[1:]
    return rest.split('@', 1)[0]


def itanium_name(symbol):
    """The function name inside an Itanium mangling, or the symbol itself when it carries none."""
    if not symbol.startswith('_Z'):
        return symbol
    body = symbol[2:]
    if body.startswith('N'):
        body = body[1:]
    names = []
    index = 0
    while index < len(body):
        character = body[index]
        if character.isdigit():
            end = index
            while end < len(body) and body[end].isdigit():
                end += 1
            length = int(body[index:end])
            names.append(body[end:end + length])
            index = end + length
            continue
        if character in 'KVrRO':
            index += 1
            continue
        if character in 'CD':
            return names[-1] if names else symbol
        if body[index:index + 2] in ITANIUM_OPERATORS:
            return OPERATOR_NAME
        break
    return names[-1] if names else symbol


def itanium_namespace(symbol):
    """The outermost name an Itanium mangling opens with, or None when it opens with none.

    A substitution stands where a nested name would be for the standard library - `_ZNSt` rather
    than `_ZN3std` - so that answers None, which is the right answer for this reading: it is not
    this engine's. An unmangled name answers None for the same reason.
    """
    if not symbol.startswith('_Z'):
        return None
    body = symbol[2:]
    if not body.startswith('N'):
        return None
    body = body[1:]
    while body and body[0] in 'KVrRO':
        body = body[1:]
    index = 0
    while index < len(body) and body[index].isdigit():
        index += 1
    if index == 0:
        return None
    length = int(body[:index])
    return body[index:index + length] or None


def _defined_dynamic_symbols(output):
    """Every name a symbol reader printed for a dynamic symbol this library defines."""
    defined = []
    name = None
    section = None
    for line in output.split('\n'):
        stripped = line.strip()
        if stripped in ('Symbol {', 'DynamicSymbol {'):
            name, section = None, None
        elif stripped.startswith('Name:'):
            name = re.sub(r'\s*\(\d+\)$', '', stripped.split(':', 1)[1].strip())
        elif stripped.startswith('Section:'):
            section = stripped.split(':', 1)[1].strip().split(' ')[0]
        elif stripped == '}' and name and section and section != 'Undefined':
            defined.append(name)
            name, section = None, None
    return defined


def foreign_symbols(output):
    """The symbols one ELF library makes visible that are not this engine's to hide.

    Namespace std is `_GLIBCXX_VISIBILITY(default)` by the standard library's own decision, because
    the type identity of everything in it has to be one thing across a process; another library's
    types are visible by that library's decision for the same reason. Neither is this engine
    publishing its implementation, and `-fvisibility=hidden` does not reach either, so they are read
    and counted rather than compared against the published surface. An export table has nothing of
    the kind, since only a decorated declaration reaches one.
    """
    return {symbol for symbol in _defined_dynamic_symbols(output)
            if itanium_namespace(symbol) != ENGINE_NAMESPACE}


def exported_names(output, portable_executable):
    """Every name a symbol reader printed for one library's exports."""
    names = set()
    if portable_executable:
        for line in output.split('\n'):
            stripped = line.strip()
            if stripped.startswith('Name:'):
                names.add(microsoft_name(stripped.split(':', 1)[1].strip()))
        return names
    for symbol in _defined_dynamic_symbols(output):
        if itanium_namespace(symbol) == ENGINE_NAMESPACE:
            names.add(itanium_name(symbol))
    return names


def _library_path(directory, component):
    """Where a component's built library is, under either platform's name for it.

    The debug configurations of this workspace carry a `_d` suffix, so a debug deployment is read as
    readily as a release one rather than reported as a directory holding nothing.
    """
    for stem in (component.library, component.library + '_d'):
        for candidate in ('%s.dll' % stem, 'lib%s.so' % stem, '%s.so' % stem, 'lib%s.dylib' % stem):
            path = directory / candidate
            if path.is_file():
                return path
    return None


def declared_binary_names(entities, portable_executable):
    """Every name one component's headers promise its library carries a symbol for.

    An exported declaration promises the symbol it names. A polymorphic type decorated for visible
    identity promises its type information and its vtable instead of a declaration, and a caller
    that implements it needs both, so it belongs to the binary interface just as squarely - on a
    platform where the decoration means something. `Core/Api.h` makes it empty on Windows, where the
    compiler emits a polymorphic type's vtable and type information in every unit that needs them,
    so an export table owes nothing for it; `identity_decoration_is_empty_on_windows` holds the
    header to that.
    """
    names = {declared_name(entity.text) for entity in entities if entity.kind == 'api'}
    if not portable_executable:
        names.update(entity.name.rsplit('::', 1)[-1] for entity in entities
                     if entity.kind == 'type' and TYPE_DECORATION.search(entity.text))
    names.discard(None)
    return names


def identity_decoration_is_empty_on_windows(repo_root):
    """Whether the engine's identity decoration expands to nothing on Windows.

    It is the fact the export-table reading rests on, so it is read from the header rather than
    assumed: a Windows spelling with a meaning would owe the decorated types' symbols there too.
    """
    path = pathlib.Path(repo_root) / 'src/AfterNightEngine/include/AfterNightEngine/Core/Api.h'
    text = path.read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')
    windows_spelling = r'#if defined\(_WIN32\)\n#define AFTERNIGHTENGINE_INTERFACE[ \t]*\n#else\n'
    return re.search(windows_spelling, text) is not None


def check_binaries(repo_root, directory, tool):
    """What the built libraries export against what the headers declare.

    Returns the findings and the reading they came from - how many libraries were opened and how
    many names were read - because a run that opened nothing prints the same clean line as one that
    read the whole surface.
    """
    directory = pathlib.Path(directory)
    if not directory.is_dir():
        return ['%s: no such directory, so no library was read' % directory], (0, 0, 0)
    resolved = shutil.which(tool) if tool else (shutil.which('llvm-readobj') or shutil.which('readobj'))
    if not resolved and tool and pathlib.Path(tool).is_file():
        resolved = tool
    if not resolved:
        return ['no symbol reader was found; pass --binary-tool <llvm-readobj>'], (0, 0, 0)

    problems = []
    libraries = 0
    read = 0
    foreign = 0
    for component in COMPONENTS:
        path = _library_path(directory, component)
        if path is None:
            problems.append('%s: no built library of the %s component is there, so its exports were '
                            'not read' % (directory, component.name))
            continue
        with path.open('rb') as handle:
            portable_executable = handle.read(2) == b'MZ'
        flag = '--coff-exports' if portable_executable else '--dyn-symbols'
        result = subprocess.run([resolved, flag, str(path)], capture_output=True, text=True, check=False)
        if result.returncode != 0:
            problems.append('%s: the symbol reader refused it: %s' % (path.name, result.stderr.strip()))
            continue
        libraries += 1
        exported = exported_names(result.stdout, portable_executable)
        read += len(exported)
        foreign += 0 if portable_executable else len(foreign_symbols(result.stdout))
        entities, _ = read_component(component, repo_root)
        if portable_executable and not identity_decoration_is_empty_on_windows(repo_root):
            problems.append('%s: the identity decoration means something on Windows, so this reading '
                            'no longer knows which symbols a decorated type owes an export table'
                            % path.name)
        declared = declared_binary_names(entities, portable_executable)
        for name in sorted(declared - exported):
            problems.append('%s: %s is declared exported and the library exports no such name, so the '
                            'first program outside this engine to name it fails to link'
                            % (path.name, name))
        for name in sorted(exported - declared):
            problems.append('%s: %s is exported and no published header declares it, so something '
                            'private is in the binary interface' % (path.name, name))
    return problems, (libraries, read, foreign)


# ---------------------------------------------------------------------------------------------
# The history: every change the freeze let through since a base, each read on its own.
# ---------------------------------------------------------------------------------------------


class HistoryError(RuntimeError):
    """Git could not answer a question the history needs."""


def _git(repo_root, *arguments):
    result = subprocess.run(['git', '-C', str(repo_root), *arguments], capture_output=True)
    if result.returncode != 0:
        raise HistoryError('git %s failed: %s' % (' '.join(arguments),
                                                  result.stderr.decode('utf-8', 'replace').strip()))
    return result.stdout


def frozen_paths(snapshot_path=SNAPSHOT):
    """What the freeze reads: every component's include root, the version header and the record."""
    return [component.include_root for component in COMPONENTS] + [VERSION_HEADER, snapshot_path]


def _materialize(repo_root, commit, paths, destination):
    """The paths as they stood at a commit, written under destination in the canonical spelling.

    A path the commit lacks is looked for under the names the digests spell, and written under the published ones; a
    path the commit holds under neither is skipped. Every file read is respelled, which changes nothing in one that
    already spells the published names.
    """
    sources = {}
    for path in paths:
        for candidate in (path, frozen_spelling(path)):
            if _git(repo_root, 'ls-tree', '--name-only', commit, '--', candidate).strip():
                sources[candidate] = path
                break
    if not sources:
        return
    data = _git(repo_root, 'archive', '--format=tar', commit, '--', *sources)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            source = next(candidate for candidate in sources
                          if member.name == candidate or member.name.startswith(candidate + '/'))
            target = destination / (sources[source] + member.name[len(source):])
            target.parent.mkdir(parents=True, exist_ok=True)
            content = archive.extractfile(member).read()
            try:
                content = canonical_spelling(content.decode('utf-8')).encode('utf-8')
            except UnicodeDecodeError:
                pass
            target.write_bytes(content)


class _Reading:
    """One commit's record, the joining it was written under, and the surface its tree publishes under each
    joining."""

    def __init__(self, recorded, joining, current):
        self.recorded = recorded
        self.joining = joining
        self.current = current

    def surface(self, joining):
        """The surface this commit's tree publishes, its digests taken under `joining`."""
        return self.current[joining]


def _read_commit(repo_root, commit, paths, snapshot_path):
    with tempfile.TemporaryDirectory(prefix='an-public-api-history-') as directory:
        root = pathlib.Path(directory)
        _materialize(repo_root, commit, paths, root)
        record_path = root / snapshot_path
        recorded = None
        joining = JOINING
        if record_path.is_file():
            text = record_path.read_text(encoding='utf-8')
            recorded = parse_snapshot(text)
            joining = recorded_joining(text)
        version, generation = read_version(root)
        current = {}
        for each in range(1, JOINING + 1):
            entities, digests = read_surface(root, each)
            current[each] = (version, generation, digests, entities)
        return _Reading(recorded, joining, current)


def history(repo_root, base, snapshot_path=SNAPSHOT):
    """(one line per commit, problems) for every commit since base that touched what the freeze reads.

    Each commit is compared with its first parent, the commit it was made on; a merge that brought
    another line's changes in compares with the line it merged into.
    """
    paths = frozen_paths(snapshot_path)
    commits = _git(repo_root, 'rev-list', '--reverse', '%s..HEAD' % base, '--', *paths).decode().split()
    readings = {}

    def reading(commit):
        if commit not in readings:
            readings[commit] = _read_commit(repo_root, commit, paths, snapshot_path)
        return readings[commit]

    lines = []
    problems = []
    for commit in commits:
        short = _git(repo_root, 'rev-parse', '--short=9', commit).decode().strip()
        subject = _git(repo_root, 'log', '-1', '--format=%s', commit).decode().strip()
        parent = _git(repo_root, 'rev-parse', commit + '^1').decode().strip()
        after = reading(commit)
        before = reading(parent)
        version, generation = after.surface(JOINING)[0], after.surface(JOINING)[1]
        if after.recorded is None:
            problems.append('%s %s: has no record at %s' % (short, subject, snapshot_path))
            continue
        own = differences(after.recorded, after.surface(after.joining))
        if own or (after.recorded[0], after.recorded[1]) != (version, generation):
            problems.append('%s %s: its record does not describe its own surface (%d difference(s), recorded '
                            '%s/%d against %s/%d)' % (short, subject, len(own), after.recorded[0],
                                                      after.recorded[1], version, generation))
        if before.recorded is None:
            problems.append('%s %s: its first parent has no record to be compared with' % (short, subject))
            continue
        changed = differences(before.recorded, after.surface(before.joining))
        interface = [message for tier, message in changed if tier == ABI]
        was_version, was_generation = before.recorded[0], before.recorded[1]
        verdict = []
        if changed and version == was_version:
            verdict.append('the surface changed and the version stayed %s' % version)
        if interface and generation <= was_generation:
            verdict.append('an interface change left the generation at %d' % generation)
        for broken in verdict:
            problems.append('%s %s: %s' % (short, subject, broken))
        lines.append('%s %s/%d -> %s/%d: %d surface and %d interface difference(s)%s - %s'
                     % (short, was_version, was_generation, version, generation, len(changed) - len(interface),
                        len(interface), '' if not verdict else ' (REFUSED)', subject))
    return lines, problems


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--snapshot', default=SNAPSHOT)
    parser.add_argument('--record', action='store_true',
                        help='write the snapshot from the tree instead of checking it')
    parser.add_argument('--binaries', default=None,
                        help='a deployed directory whose libraries are read for what they export')
    parser.add_argument('--binary-tool', default=None, help='the symbol reader to use')
    parser.add_argument('--history', default=None, metavar='BASE',
                        help='hold every commit since BASE that touched the published surface to the rules')
    arguments = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout
    repo_root = pathlib.Path(arguments.repo_root).resolve()

    if arguments.history is not None:
        try:
            lines, problems = history(repo_root, arguments.history, arguments.snapshot)
        except HistoryError as error:
            print(str(error), file=out)
            return 1
        for line in lines:
            print(line, file=out)
        for problem in problems:
            print('ERROR: %s' % problem, file=out)
        if problems:
            return 1
        print('%d commit(s) since %s touched the published surface or its record; each recorded its own '
              'surface and moved the version and the generation as its differences required'
              % (len(lines), arguments.history), file=out)
        return 0

    if arguments.record:
        written = record(repo_root, arguments.snapshot)
        print('recorded %d lines of published surface in %s' % (written, arguments.snapshot), file=out)
        return 0

    if arguments.binaries is not None:
        problems, (libraries, read, foreign) = check_binaries(repo_root, arguments.binaries,
                                                              arguments.binary_tool)
        for problem in problems:
            print(problem, file=out)
        if problems:
            return 1
        reading = ('%d built libraries export the %d names their published headers declare, and '
                   'nothing else of their own' % (libraries, read))
        if foreign:
            reading += (', beside %d symbol(s) the C++ runtime and other libraries make visible by '
                        'their own decision' % foreign)
        print(reading, file=out)
        return 0

    problems = check(repo_root, arguments.snapshot)
    for problem in problems:
        print(problem, file=out)
    if problems:
        return 1
    print('the published surface is the one recorded in %s' % arguments.snapshot, file=out)
    return 0


if __name__ == '__main__':
    sys.exit(main())

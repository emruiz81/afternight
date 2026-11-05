# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the engine's provenance register to the tree it describes.

`build/scripts/tools/dependencies.txt` records, for every third-party library and every ported
subsystem the engine carries, the eight things somebody has to be able to look up: version, source,
licence, upstream, security channel, build owner, deploy owner and adapter boundary. A register is
only worth reading while it is true, and the three ways it stops being true are all silent:

1. **a dependency arrives and nobody records it.** A link line gains an entry, a codec gains an
   include, an adapter gains a library - and the register still describes the tree of a month ago;
2. **a dependency leaves and its record stays.** That is worse than no register, because a row
   nobody can reach reads exactly like coverage; and
3. **a version moves.** The number written down and the number inside the binary stop agreeing,
   which is invisible until somebody has to answer whether an advisory applies.

So the register is not read on its own. Every claim in it is joined to something that would have to
change with it:

* the **include lines** of every engine source, through the same external-library groups
  `check_include_layers.py` enforces the boundaries with. A group that is reached and not registered
  is a dependency nobody recorded; a group registered and reached by nothing is a record nobody
  removed;
* the **link lines** of the generated projects, for the libraries a build acquires without any
  source including them - zlib behind CFITSIO, Little-CMS behind LibRaw, the compressors behind the
  vendored XISF library. A licence question applies to those exactly as much;
* the **install and deploy scripts**, so "build owner" and "deploy owner" name something that
  exists and actually mentions the package and the runtime file they claim;
* the engine's **own dependency report** and the **suite that pins it**, so the register, the
  binary and the test agree about which libraries are inside this engine and which of the two
  versions each of them publishes; and
* for a ported subsystem, the **files it names**, each of which must still carry the phrase that
  says where it came from. A file that lost its attribution is how a licence obligation goes
  missing, and a lexical sweep for a third-party copyright notice catches a new file that arrived
  with one and no record.

Usage:
    python build/scripts/tools/check_dependency_provenance.py [--repo-root <dir>]
    python build/scripts/tools/check_dependency_provenance.py --installed [--vcpkg-root <dir>]

The default reads the repository. `--installed` additionally compares every packaged version in the
register against the package database of the host it is run on, which is the one check that needs a
provisioned machine and is therefore recorded per host rather than assumed.

Exits 0 when the register and the tree agree, 1 otherwise, printing one line per finding.

Tests: build/scripts/tools/check_dependency_provenance_test.py
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_build_graph as graph  # noqa: E402
import check_include_layers as layers  # noqa: E402

REPO_ROOT = layers.REPO_ROOT

REGISTER = 'build/scripts/tools/dependencies.txt'

# Where the engine's own report is assembled and where its shape is pinned. Both are read lexically:
# the point is that three independent statements of one fact agree, so reading two of them out of
# the same place would prove nothing.
ENGINE_TREES = (
    'src/AfterNightEngine',
    'src/AfterNightEnginePythonAdapter',
    'src/AfterNightEngineGmicAdapter',
    'src/AfterNightEngineOpenClBackend',
)
SUITE_EXPECTATION_FILE = 'src/AfterNightTests/Test_EngineCore/DependencyReportTest.cpp'

LIBRARY_FIELDS = ('role', 'kind', 'linkage', 'version', 'source', 'upstream', 'license', 'security',
                  'build', 'deploy', 'artifacts', 'boundary', 'targets', 'links')
LIBRARY_OPTIONAL_FIELDS = ('reported',)
PORTED_FIELDS = ('role', 'files', 'marker', 'upstream', 'author', 'license', 'relation', 'divergence',
                 'record')

KINDS = ('packaged', 'fetched', 'vendored', 'distribution', 'embedded', 'transitive')

# What a `fetched` record's source names: the upstream repository and release tag in backticks, and the commit that
# tag must resolve to. Every build script the record names fetches that repository's tag and pins that commit, so the
# Windows and the Linux script cannot build two releases.
FETCHED_RELEASE = re.compile(r'`([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)@([A-Za-z0-9_.+-]+)`')
FETCHED_COMMIT = re.compile(r'\b[0-9a-f]{40}\b')
LINKAGES = ('shared', 'static', 'header-only', 'mixed', 'shared on Windows, static on Linux')
RELATIONS = ('adapted', 're-derived', 'replaced', 'not-carried')

# Include groups that name no third-party library, so no record is expected for them.
#
# `platform` is the operating system's own file and process headers and `threads` the POSIX thread
# API; neither is a dependency anybody acquires. `simd` is the one mixed group: it carries Highway,
# which is registered, alongside the compiler's own intrinsic headers, which are not a library
# either - so the group is expected to be covered by exactly one record and is listed here only to
# document why it is not a group-per-library.
NON_LIBRARY_GROUPS = ('platform', 'threads')

# Link entries a generated project carries that name no dependency of this engine: the platform's
# own import libraries, and the maths library every GNU-style link line ends with.
SYSTEM_LINKS = frozenset([
    'kernel32', 'user32', 'gdi32', 'winspool', 'comdlg32', 'advapi32', 'shell32', 'ole32',
    'oleaut32', 'uuid', 'odbc32', 'odbccp32', 'delayimp', 'm', 'pthread', 'dl', 'rt', 'stdc++',
])

# A third-party attribution, as it appears in a source file that carries one. A file matching this
# must be claimed by a ported record, because these are the words a licence obligation is written in.
#
# A licence identifier counts on its own. A copyright counts only with a year beside it, because the
# bare word is ordinary prose - this engine's own comments say things like "a multi-line copyright
# banner" about a library's version string - and a sweep that reported those would be reporting
# sentences. The narrowing is deliberate, and this is its cost: a port whose upstream is named without
# a licence or a year is caught by its record's marker rather than by this sweep.
ATTRIBUTION = re.compile(r'(?:GPL-[23]|LGPL|Apache-2|BSD-[23]|CeCILL|MIT [Ll]icen'
                         r'|[Cc]opyright[^\n]{0,40}\b(?:19|20)\d{2}\b)')

# The licence header every file of this tree carries (check_license_headers.py): the program's own licence and the
# program's copyright line. Neither is a third party's, so the sweep reads a file without them. A derived file's
# header states its upstream's licence where that is not the program's own, and that line is still read, as is any
# copyright line naming another holder.
OWN_HEADER_LINE = re.compile(r'^[ \t]*//[ \t]*SPDX-(?:License-Identifier:[ \t]*GPL-3\.0-or-later'
                             r'|FileCopyrightText:[ \t]*\d{4}(?:[-, ]+\d{4})*[ \t]+Ezequiel Ruiz)[ \t]*$',
                             re.MULTILINE)

# A document of this tree, as a ported record's `record` field names one.
RECORD_DOCUMENT = re.compile(r'\b(docs/[A-Za-z0-9_./-]+\.md)\b')

VERSION_IN_TEXT = re.compile(r'\b\d+\.\d+(?:\.\d+)?\b')
PORT_SPEC = re.compile(r'`([A-Za-z0-9][A-Za-z0-9_.+-]*)(\[[^`\]]*\])?`')
REPORTED = re.compile(r'^compiled=(yes|no) loaded=(yes|no)$')
ENGINE_REPORT_NAME = re.compile(r'entry\.name\s*=\s*"([^"]+)"\s*;')
SUITE_EXPECTATION = re.compile(r'\{"([^"]+)",\s*(true|false),\s*(true|false)\}')


class Record:
    """One record of the register, with the line it started on so a finding can name it."""

    def __init__(self, kind, name, line):
        self.kind = kind
        self.name = name
        self.line = line
        self.fields = {}


def parse_register(text):
    """Read the register into records, reporting anything that is not one.

    A field's value continues onto any following indented line that does not begin a field, which is
    what lets a record explain itself in sentences rather than in fragments.
    """
    records = []
    problems = []
    current = None
    field = None
    for number, raw in enumerate(text.split('\n'), start=1):
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        header = re.match(r'^(library|ported) (\S+)\s*$', line)
        if header:
            current = Record(header.group(1), header.group(2), number)
            records.append(current)
            field = None
            continue
        if not line.startswith(' '):
            problems.append('%s:%d: this is neither a record header nor a field' % (REGISTER, number))
            continue
        if current is None:
            problems.append('%s:%d: a field before any record' % (REGISTER, number))
            continue
        named = re.match(r'^ {4}(\S+) {2,}(.*)$', line)
        if named:
            field = named.group(1)
            if field in current.fields:
                problems.append('%s:%d: %s states %s twice' % (REGISTER, number, current.name, field))
            current.fields[field] = named.group(2).strip()
            continue
        if field is None:
            problems.append('%s:%d: a continuation line before any field' % (REGISTER, number))
            continue
        current.fields[field] = (current.fields[field] + ' ' + line.strip()).strip()
    return records, problems


def check_schema(records, problems):
    """Every record has its own fields, no others, and the closed values are from their sets."""
    seen = {}
    for record in records:
        where = '%s:%d %s' % (REGISTER, record.line, record.name)
        if (record.kind, record.name) in seen:
            problems.append('%s: declared twice' % where)
        seen[(record.kind, record.name)] = record

        required = LIBRARY_FIELDS if record.kind == 'library' else PORTED_FIELDS
        allowed = set(required) | (set(LIBRARY_OPTIONAL_FIELDS) if record.kind == 'library' else set())
        for name in required:
            if not record.fields.get(name, '').strip():
                problems.append('%s: missing %s' % (where, name))
        for name in sorted(record.fields):
            if name not in allowed:
                problems.append('%s: %s is not a field of a %s record' % (where, name, record.kind))

        if record.kind == 'library':
            if record.fields.get('kind') not in KINDS:
                problems.append('%s: kind %r is not one of %s'
                                % (where, record.fields.get('kind'), ', '.join(KINDS)))
            if record.fields.get('linkage') not in LINKAGES:
                problems.append('%s: linkage %r is not one of %s'
                                % (where, record.fields.get('linkage'), ', '.join(LINKAGES)))
            reported = record.fields.get('reported')
            if reported is not None and not REPORTED.match(reported):
                problems.append('%s: reported %r is not compiled=<yes|no> loaded=<yes|no>'
                                % (where, reported))
        else:
            if record.fields.get('relation') not in RELATIONS:
                problems.append('%s: relation %r is not one of %s'
                                % (where, record.fields.get('relation'), ', '.join(RELATIONS)))
            if record.fields.get('relation') == 'not-carried':
                if record.fields.get('marker') != 'none':
                    problems.append('%s: a not-carried record has no attribution to keep, so its '
                                    'marker is `none`' % where)
            elif record.fields.get('marker') == 'none':
                problems.append('%s: a %s record carries an attribution, so it names the phrase that '
                                'keeps it' % (where, record.fields.get('relation')))


def libraries(records):
    return [record for record in records if record.kind == 'library']


def ported(records):
    return [record for record in records if record.kind == 'ported']


def comma_list(value):
    """The comma-separated items of a field, with `none` reading as no items."""
    if value.strip() == 'none':
        return []
    return [item.strip() for item in value.split(',') if item.strip()]


def registered_groups(records):
    """Every include group a record claims, by group name."""
    claimed = {}
    for record in libraries(records):
        for group in re.findall(r'include group `([^`]+)`', record.fields.get('boundary', '')):
            claimed.setdefault(group, []).append(record.name)
    return claimed


def reached_groups(repo_root):
    """Every external library group an engine source actually includes, and where it first does."""
    repo_root = pathlib.Path(repo_root)
    sources = []
    files, _ = layers.collect_files(repo_root)
    sources.extend(files)
    for component in layers.OPTIONAL_COMPONENTS:
        public, private, _ = layers.collect_adapter_files(repo_root, component)
        sources.extend(public)
        sources.extend(private)

    reached = {}
    for source in sources:
        for include in source.includes:
            group = layers.external_library_of(include.target)
            if group is None:
                continue
            reached.setdefault(group, source.relative)
    return reached


def check_groups(repo_root, records, problems):
    """Every reached group is registered, and every registered group is reached."""
    claimed = registered_groups(records)
    reached = reached_groups(repo_root)

    for group in sorted(reached):
        if group in NON_LIBRARY_GROUPS:
            continue
        if group not in claimed:
            problems.append('%s: the include group `%s` is reached by %s and no record claims it'
                            % (REGISTER, group, reached[group]))
    for group in sorted(claimed):
        if group not in reached:
            problems.append('%s: %s claims the include group `%s`, which no engine source includes'
                            % (REGISTER, ', '.join(claimed[group]), group))


def registered_links(records):
    """Every link token a record claims, lowercased, to the records that claim it.

    A token followed by `(SWITCH)` is one only a tree generated with that switch carries, and is
    returned as conditional so that a default tree not carrying it is not read as a stale record.
    """
    claimed = {}
    conditional = set()
    for record in libraries(records):
        for token in comma_list(record.fields.get('links', 'none')):
            switch = re.search(r'\(([A-Z0-9_]+)\)\s*$', token)
            base = graph.link_base_name(re.sub(r'\s*\([A-Z0-9_]+\)\s*$', '', token)).lower()
            claimed.setdefault(base, []).append(record.name)
            if switch:
                conditional.add(base)
    return claimed, conditional


# How each library this engine links spells its Debug build. A generated Debug configuration names
# `gsld` where the Release one names `gsl`, and treating those as two dependencies would put a second
# row in the register for every library that has one. The reduction is the library's own convention
# rather than a guess: a trailing `d` or `_debug` on a token that is otherwise claimed.
def link_spellings(token):
    """`token` and the Release spelling of it, when it looks like a Debug one."""
    yield token
    if token.endswith('_debug'):
        yield token[: -len('_debug')]
    if token.endswith('d') and len(token) > 1:
        yield token[:-1]


def usable_trees(repo_root):
    """Every generated tree that is current, and the labels of those that are not.

    The two exclusions are `check_build_graph.py`'s and are excluded for its reasons: a tree that
    was never generated here says nothing, and a tree older than the definitions that produce it
    describes a graph that is not the one being built - so reading its link lines would be a false
    result in either direction.
    """
    usable = []
    unusable = []
    definitions = graph.newest_definition_mtime(str(repo_root))
    for label, directory, kind in graph.GENERATED_TREES:
        workspaces = graph.load_tree(str(repo_root), label, directory, kind)
        if workspaces is None or not any(workspace.targets for workspace in workspaces):
            unusable.append(directory)
            continue
        if definitions > graph.generation_mtime(os.path.join(str(repo_root), directory), workspaces):
            unusable.append(directory + ' (stale)')
            continue
        usable.append((label, workspaces))
    return usable, unusable


def engine_link_tokens(repo_root):
    """Every link token an engine target carries, and the targets that carry it.

    Read from the generated projects rather than from the Premake sources, for the reason
    `check_build_graph.py` gives: the generated graph is what the linker obeys. A current tree that
    yields no engine target is a finding, because a front end whose assumptions stopped matching its
    generator would otherwise pass vacuously.
    """
    tokens = {}
    problems = []
    usable, unusable = usable_trees(repo_root)
    for label, workspaces in usable:
        found = 0
        for workspace in workspaces:
            for name, target in sorted(workspace.targets.items()):
                if not graph.is_engine_target(name):
                    continue
                found += 1
                for entry in target.links:
                    base = graph.link_base_name(entry).lower()
                    if base:
                        tokens.setdefault(base, set()).add(name)
        if found == 0:
            problems.append('%s: the generated tree is current and holds no engine target, so its '
                            'link lines prove nothing' % label)
    return tokens, problems, unusable


def is_first_party(token):
    """True for a link token that names a target of this repository rather than a dependency."""
    return token.startswith('afternight') and 'libraw' not in token and 'rawspeed' not in token \
        or token.startswith('enginebench') or token.startswith('test_')


def check_links(repo_root, records, problems, out=None):
    """Every link an engine target carries is claimed, and every claimed link is carried."""
    claimed, conditional = registered_links(records)
    carried, found, unusable = engine_link_tokens(repo_root)
    problems.extend(found)
    if not carried:
        problems.append('%s: no current generated tree to read link lines from; generate projects '
                        'first (01-create_project_files)' % REGISTER)
        return

    for token in sorted(carried):
        if token in SYSTEM_LINKS or is_first_party(token):
            continue
        if not any(spelling in claimed for spelling in link_spellings(token)):
            problems.append('%s: %s is linked by %s and no record claims it'
                            % (REGISTER, token, ', '.join(sorted(carried[token]))))

    # The other direction needs every tree, because a link line is per platform and per
    # configuration: `-lz` is on the Linux one only and `xisfd` on a Windows Debug one only, so a
    # host whose other front end is missing or stale cannot tell a record nobody removed from a
    # record about the tree it did not generate. When one is unusable the rule says so rather than
    # passing quietly.
    if unusable:
        print('not checking for links no target carries: %s' % ', '.join(unusable),
              file=out or sys.stdout)
        return
    for token in sorted(claimed):
        if token in conditional:
            continue
        if not any(spelling in carried for spelling in link_spellings(token)):
            problems.append('%s: %s claims the link %s, which no generated engine target carries'
                            % (REGISTER, ', '.join(claimed[token]), token))


def named_scripts(value):
    """Every repository path in a field that looks like a script."""
    return re.findall(r'\b(build/[A-Za-z0-9_./-]+\.(?:bat|sh|py|lua))\b', value)


def read_script(repo_root, relative):
    path = pathlib.Path(repo_root) / relative
    if not path.is_file():
        return None
    return path.read_bytes().decode('utf-8', 'replace').replace('\\', '/')


def check_fetched_release(record, where, build_scripts, repo_root, problems):
    """A fetched library's source names one release and one commit, and every build script fetches exactly those."""
    source = record.fields.get('source', '')
    releases = FETCHED_RELEASE.findall(source)
    commits = FETCHED_COMMIT.findall(source)
    if len(releases) != 1 or len(commits) != 1:
        problems.append('%s: a fetched record names the release it is fetched from as `owner/repository@tag` and '
                        'the commit that tag resolves to' % where)
        return
    (repository, tag), commit = releases[0], commits[0]
    for relative in build_scripts:
        text = read_script(repo_root, relative)
        if text is None:
            continue
        if 'github.com/%s' % repository not in text:
            problems.append('%s: %s does not fetch %s' % (where, relative, repository))
        if not re.search(r'(?<![A-Za-z0-9_.+-])%s(?![A-Za-z0-9_.+-])' % re.escape(tag), text):
            problems.append('%s: %s does not fetch %s' % (where, relative, tag))
        if commit not in text:
            problems.append('%s: %s does not pin %s' % (where, relative, commit))


def check_build_and_deploy(repo_root, records, problems):
    """Every script a record names exists, installs the port it claims, and copies the runtime."""
    for record in libraries(records):
        where = '%s:%d %s' % (REGISTER, record.line, record.name)

        build_scripts = named_scripts(record.fields.get('build', ''))
        if not build_scripts:
            problems.append('%s: build names no script in this repository' % where)
        texts = []
        for relative in build_scripts:
            text = read_script(repo_root, relative)
            if text is None:
                problems.append('%s: build names %s, which does not exist' % (where, relative))
            else:
                texts.append(text)

        if record.fields.get('kind') == 'packaged':
            ports = [name for name, _ in PORT_SPEC.findall(record.fields.get('source', ''))]
            if not ports:
                problems.append('%s: a packaged record names the port it comes from in backticks' % where)
            for name, features in PORT_SPEC.findall(record.fields.get('source', '')):
                spec = name + (features or '')
                if not any(spec in text for text in texts):
                    problems.append('%s: no script it names installs `%s`' % (where, spec))

        if record.fields.get('kind') == 'fetched':
            check_fetched_release(record, where, build_scripts, repo_root, problems)

        deploy_scripts = named_scripts(record.fields.get('deploy', ''))
        wanted = comma_list(record.fields.get('artifacts', 'none'))
        if wanted and not deploy_scripts:
            problems.append('%s: it deploys %s and names no script that does'
                            % (where, ', '.join(wanted)))
        deployed = []
        for relative in deploy_scripts:
            text = read_script(repo_root, relative)
            if text is None:
                problems.append('%s: deploy names %s, which does not exist' % (where, relative))
            else:
                deployed.append(text)
        for artifact in wanted:
            token = artifact.replace('\\', '/')
            if not any(token in text for text in deployed):
                problems.append('%s: no script it names deploys %s' % (where, artifact))


def engine_reported_names(repo_root):
    """Every dependency name the engine's own report assigns, read out of its contributors."""
    names = {}
    for tree in ENGINE_TREES:
        root = pathlib.Path(repo_root) / tree
        if not root.is_dir():
            continue
        for path in sorted(root.rglob('*.cpp')):
            text = path.read_bytes().decode('utf-8', 'replace')
            for name in ENGINE_REPORT_NAME.findall(text):
                names[name] = path.relative_to(repo_root).as_posix()
    return names


def suite_expectations(repo_root):
    """The table the suite holds the running binary to, as {name: (compiled, loaded)}."""
    path = pathlib.Path(repo_root) / SUITE_EXPECTATION_FILE
    if not path.is_file():
        return None
    text = path.read_bytes().decode('utf-8', 'replace')
    return {name: (compiled == 'true', loaded == 'true')
            for name, compiled, loaded in SUITE_EXPECTATION.findall(text)}


def check_engine_report(repo_root, records, problems):
    """The register, the engine's report and the suite name one set of libraries and one shape."""
    registered = {record.name: record for record in libraries(records) if 'reported' in record.fields}
    reported = engine_reported_names(repo_root)

    for name in sorted(reported):
        if name not in registered:
            problems.append('%s: the engine reports %s from %s and no record carries its shape'
                            % (REGISTER, name, reported[name]))
    for name in sorted(registered):
        if name not in reported:
            problems.append('%s: %s states what the engine reports about it and the engine reports '
                            'no such dependency' % (REGISTER, name))

    expectations = suite_expectations(repo_root)
    if expectations is None:
        problems.append('%s: the suite that pins the report does not exist' % SUITE_EXPECTATION_FILE)
        return
    for name in sorted(registered):
        match = REPORTED.match(registered[name].fields['reported'])
        if not match:
            continue
        shape = (match.group(1) == 'yes', match.group(2) == 'yes')
        if name not in expectations:
            problems.append('%s: %s is registered as reported and the suite does not pin it'
                            % (SUITE_EXPECTATION_FILE, name))
        elif expectations[name] != shape:
            problems.append('%s: %s is pinned as compiled=%s loaded=%s and registered as %s'
                            % (SUITE_EXPECTATION_FILE, name,
                               'yes' if expectations[name][0] else 'no',
                               'yes' if expectations[name][1] else 'no',
                               registered[name].fields['reported']))
    for name in sorted(expectations):
        if name not in registered:
            problems.append('%s: the suite pins %s and no record carries its shape'
                            % (SUITE_EXPECTATION_FILE, name))


def record_documents(value):
    """The documents of this tree a ported record's `record` field names, in its order.

    The field is a sentence - `docs/dev/guides/DEPENDENCIES.md, and the module comment of each file` - so a path
    is read out of it whatever punctuation follows it.
    """
    return RECORD_DOCUMENT.findall(value)


def check_ported(repo_root, records, problems):
    """Every ported record's files exist and still say where they came from, its record names a document that
    exists, and no file carrying an attribution is unclaimed."""
    claimed = {}
    for record in ported(records):
        where = '%s:%d %s' % (REGISTER, record.line, record.name)
        marker = record.fields.get('marker', '')
        for relative in comma_list(record.fields.get('files', 'none')):
            path = pathlib.Path(repo_root) / relative
            if not path.is_file():
                problems.append('%s: files names %s, which does not exist' % (where, relative))
                continue
            claimed[relative] = record.name
            if marker != 'none' and marker not in path.read_bytes().decode('utf-8', 'replace'):
                problems.append('%s: %s no longer contains %r, which is the attribution this record '
                                'keeps' % (where, relative, marker))
        documents = record_documents(record.fields.get('record', ''))
        if not documents:
            problems.append('%s: record names no document of this tree, so a reader has nowhere to look for '
                            'what was carried and why' % where)
        for relative in documents:
            if not (pathlib.Path(repo_root) / relative).is_file():
                problems.append('%s: record names %s, which does not exist' % (where, relative))

    for tree in ENGINE_TREES:
        root = pathlib.Path(repo_root) / tree
        if not root.is_dir():
            continue
        for path in sorted(root.rglob('*')):
            if not path.is_file() or path.suffix not in layers.SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(repo_root).as_posix()
            if relative in claimed:
                continue
            text = OWN_HEADER_LINE.sub('', path.read_bytes().decode('utf-8', 'replace').replace('\r\n', '\n'))
            if ATTRIBUTION.search(text):
                problems.append('%s: carries a third-party attribution and no ported record claims it'
                                % relative)


def installed_versions(vcpkg_root):
    """What the host's package database says is installed, as {(port, triplet): version}."""
    status = pathlib.Path(vcpkg_root) / 'installed' / 'vcpkg' / 'status'
    if not status.is_file():
        return None
    versions = {}
    for block in status.read_bytes().decode('utf-8', 'replace').split('\n\n'):
        fields = {}
        for line in block.splitlines():
            if ': ' in line:
                key, value = line.split(': ', 1)
                fields[key] = value.strip()
        if 'Feature' in fields or 'Package' not in fields:
            continue
        version = fields.get('Version') or fields.get('Version-String') or ''
        if version:
            versions[(fields['Package'], fields.get('Architecture', ''))] = version
    return versions


def installed_platform(versions):
    """Name the install-script platform described by one vcpkg package database."""
    triplets = {triplet for _, triplet in versions if triplet}
    if triplets and all('linux' in triplet for triplet in triplets):
        return 'linux'
    if triplets and all('windows' in triplet for triplet in triplets):
        return 'msvc'
    return None


def script_installs_port(text, spec):
    """Whether an actual vcpkg install command in a script names one exact port spec."""
    command = re.compile(r'\bvcpkg(?:\.exe)?\s+install\b(.*)', re.IGNORECASE)
    token = re.compile(r'(?<![A-Za-z0-9_.+\-\[])%s(?![A-Za-z0-9_.+\-\]])'
                       % re.escape(spec))
    return any(token.search(match.group(1)) for line in text.splitlines()
               for match in [command.search(line)] if match)


def platform_ports(repo_root, records, platform):
    """Ports the named install scripts actually acquire for this host platform."""
    expected = set()
    marker = '/%s/' % platform
    for record in libraries(records):
        if record.fields.get('kind') != 'packaged':
            continue
        scripts = [relative for relative in named_scripts(record.fields.get('build', ''))
                   if marker in relative.replace('\\', '/')]
        texts = [read_script(repo_root, relative) for relative in scripts]
        texts = [text for text in texts if text is not None]
        for name, features in PORT_SPEC.findall(record.fields.get('source', '')):
            spec = name + (features or '')
            if any(script_installs_port(text, spec) for text in texts):
                expected.add(name)
    return expected


def check_installed(records, vcpkg_root, problems, repo_root=None):
    """Every packaged version acquired on this platform is installed at its recorded version."""
    versions = installed_versions(vcpkg_root)
    if versions is None:
        problems.append('%s: no package database to compare against' % vcpkg_root)
        return
    by_port = {}
    for (port, _), version in versions.items():
        by_port.setdefault(port, set()).add(version)

    platform = installed_platform(versions)
    expected = platform_ports(repo_root, records, platform) if repo_root and platform else None

    for record in libraries(records):
        if record.fields.get('kind') != 'packaged':
            continue
        where = '%s:%d %s' % (REGISTER, record.line, record.name)
        recorded = set(VERSION_IN_TEXT.findall(record.fields.get('version', '')))
        for name, _ in PORT_SPEC.findall(record.fields.get('source', '')):
            if expected is not None and name not in expected:
                continue
            if name not in by_port:
                problems.append('%s: `%s` is not installed on this host' % (where, name))
                continue
            if not (by_port[name] & recorded):
                problems.append('%s: `%s` is installed at %s and the register records %s'
                                % (where, name, ', '.join(sorted(by_port[name])),
                                   record.fields.get('version')))


def check(repo_root, installed=False, vcpkg_root=None, out=None):
    """Return every finding, in the order the rules are stated."""
    path = pathlib.Path(repo_root) / REGISTER
    if not path.is_file():
        return ['%s: the register does not exist' % REGISTER]
    records, problems = parse_register(path.read_bytes().decode('utf-8'))
    check_schema(records, problems)
    if problems:
        return problems
    check_groups(repo_root, records, problems)
    check_links(repo_root, records, problems, out=out)
    check_build_and_deploy(repo_root, records, problems)
    check_engine_report(repo_root, records, problems)
    check_ported(repo_root, records, problems)
    if installed:
        check_installed(records, vcpkg_root or os.path.join(repo_root, 'vcpkg'), problems,
                        repo_root=repo_root)
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', default=REPO_ROOT)
    parser.add_argument('--installed', action='store_true',
                        help="also compare every packaged version against this host's package database")
    parser.add_argument('--vcpkg-root', default=None)
    arguments = parser.parse_args(argv)

    problems = check(arguments.repo_root, installed=arguments.installed, vcpkg_root=arguments.vcpkg_root)
    for problem in problems:
        print(problem)
    if problems:
        print('%d dependency provenance problem(s).' % len(problems))
        return 1
    print('the provenance register and the tree agree on every library, link, script and port.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

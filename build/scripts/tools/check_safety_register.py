# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the safety register to the tree, so "no unresolved critical or high defect" is read.

`build/scripts/tools/safety_register.txt` records two things a machine can read:

1. **what the advisories say about every library the engine loads.** `dependencies.txt` names each
   library the engine links, with its version and the channel an advisory about it arrives on; a
   `review` record says which version that channel was read for, when, against what, and which
   advisories name a range that includes it, so "no known vulnerability" is a sentence that can be
   checked; and
2. **every finding about the engine's safety, with a severity and what became of it.** A `finding`
   record is critical, high, medium or low, and open, repaired, not-applicable or accepted.

Each claim is joined to something that would have to change with it:

* a critical or high finding that is open or accepted fails, because none may be left unresolved and a
  waiver is not a resolution;
* a finding's `record` is a file of the tree - or a file kept at the archive tag, named
  `<tag>:<path>`, which a clone holding that tag verifies;
* a repair names the test that fails without it, and that test has to exist - a `TEST`/`TEST_F`/`TEST_P`
  under `src/AfterNightTests`, or a test function in one of this directory's Python suites - so deleting
  the test reopens the finding rather than leaving a repair nobody can see;
* every advisory a review lists is answered by exactly one finding filed against that library, so an
  advisory cannot be read and then left out;
* every library `dependencies.txt` registers has a review, at a version that appears as a whole token in
  the version that register pins - so a library that arrives, or a version that moves, fails here until
  somebody reads its advisories again; and
* with `--binaries`, every module the shipped engine binaries load from the deployed directory is named
  by a review, and every name a review gives on this platform is loaded. `dependencies.txt` is scoped to
  what a build links; what the loader brings in behind it - the codecs FFmpeg was built with, the
  compressors behind libtiff, the XML parser behind Exiv2 - reads a hostile file just as directly, and is
  read here from the binaries themselves.

Usage:
    python build/scripts/tools/check_safety_register.py [--repo-root <dir>]
    python build/scripts/tools/check_safety_register.py --binaries bin/msvc/release [--binary-tool <reader>]

The default reads the repository. `--binaries` additionally reads the import closure of the shipped
engine binaries in a Release deployment, which needs a built and deployed tree on each platform.

Exits 0 when the register and the tree agree, 1 otherwise, printing one line per problem. A clean run
prints what it read, because a register that recorded nothing and one that recorded everything must not
print the same line.

Tests: build/scripts/tools/check_safety_register_test.py
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import archive_record  # noqa: E402
import check_dependency_provenance as provenance  # noqa: E402
import binary_imports  # noqa: E402

# A file kept at the archive tag rather than in the tree is named `<tag>:<path>`.
ARCHIVE_TAG = archive_record.ARCHIVE_TAG

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
REGISTER = 'build/scripts/tools/safety_register.txt'
PROVENANCE = 'build/scripts/tools/dependencies.txt'
TEST_ROOT = 'src/AfterNightTests'
SCRIPT_ROOT = 'build/scripts/tools'

REVIEW_FIELDS = ('version', 'binaries', 'reviewed', 'source', 'advisories', 'outcome')
FINDING_FIELDS = ('title', 'severity', 'component', 'found', 'status', 'advisories', 'evidence', 'record')

SEVERITIES = ('critical', 'high', 'medium', 'low')
STATUSES = ('open', 'repaired', 'not-applicable', 'accepted')
# A finding at one of these severities is resolved only by a repair or by showing it cannot happen here.
BLOCKING = ('critical', 'high')
RESOLVED = ('repaired', 'not-applicable')
COMPONENTS = ('engine', 'python-adapter', 'gmic-adapter', 'opencl-backend', 'tests', 'tooling')

DATE = re.compile(r'^\d{4}-\d{2}-\d{2}$')
ADVISORY = re.compile(r'^[A-Z][A-Z0-9]*-[0-9A-Za-z][0-9A-Za-z-]*$')
SCRIPT_TEST = re.compile(r'^([A-Za-z_]\w*\.py)::([A-Za-z_]\w*)$')

# The binaries this engine ships, whose closure is the one a hostile file reaches. Its suites, fuzz
# targets and benchmark runners load more - GoogleTest, OpenCV - and ship nowhere.
SHIPPED = re.compile(r'^(lib)?AfterNightEngine[A-Za-z]*(_d)?(\.dll|\.so(\.\d+)*|\.dylib)$')


class Record:
    """One record of the register, with the line it started on so a problem can name it."""

    def __init__(self, kind, name, line):
        self.kind = kind
        self.name = name
        self.line = line
        self.fields = {}


def parse_register(text):
    """Read the register into records, reporting anything that is not one.

    A field's value continues onto any following indented line that does not begin a field.
    """
    records = []
    problems = []
    current = None
    field = None
    for number, raw in enumerate(text.replace('\r\n', '\n').split('\n'), start=1):
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        header = re.match(r'^(review|finding) (\S+)\s*$', line)
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


def comma_list(value):
    """The comma-separated items of a field, with `none` reading as no items."""
    if value.strip() == 'none':
        return []
    return [item.strip() for item in value.split(',') if item.strip()]


def _where(record):
    return '%s:%d %s %s' % (REGISTER, record.line, record.kind, record.name)


def reviews(records):
    return {record.name: record for record in records if record.kind == 'review'}


def findings(records):
    return [record for record in records if record.kind == 'finding']


def check_schema(repo_root, records, problems):
    """Every record has its own fields and no others, and every closed value is from its set."""
    seen = set()
    for record in records:
        where = _where(record)
        if (record.kind, record.name) in seen:
            problems.append('%s: declared twice' % where)
        seen.add((record.kind, record.name))
        required = REVIEW_FIELDS if record.kind == 'review' else FINDING_FIELDS
        for name in required:
            if not record.fields.get(name, '').strip():
                problems.append('%s: missing %s' % (where, name))
        for name in sorted(record.fields):
            if name not in required:
                problems.append('%s: %s is not a field of a %s record' % (where, name, record.kind))
        for advisory in comma_list(record.fields.get('advisories', 'none')):
            if not ADVISORY.match(advisory):
                problems.append('%s: %r is not an advisory identifier' % (where, advisory))
        if record.kind == 'review':
            if not DATE.match(record.fields.get('reviewed', '')):
                problems.append('%s: reviewed %r is not a date' % (where, record.fields.get('reviewed')))
            continue
        if record.fields.get('severity') not in SEVERITIES:
            problems.append('%s: severity %r is not one of %s'
                            % (where, record.fields.get('severity'), ', '.join(SEVERITIES)))
        if record.fields.get('status') not in STATUSES:
            problems.append('%s: status %r is not one of %s'
                            % (where, record.fields.get('status'), ', '.join(STATUSES)))
        component = record.fields.get('component', '')
        if component not in COMPONENTS and not component.startswith('dependency '):
            problems.append('%s: component %r is not one of %s, or `dependency <library>`'
                            % (where, component, ', '.join(COMPONENTS)))
        target = record.fields.get('record', '').split('#', 1)[0]
        cited = archive_record.cited_path(target)
        if cited is not None:
            # A clone without the tag cannot tell, and takes the citation as stated.
            if archive_record.holds(repo_root, cited) is False:
                problems.append('%s: its record %s is not at %s' % (where, cited, ARCHIVE_TAG))
        elif target and not (pathlib.Path(repo_root) / target).is_file():
            problems.append('%s: its record %s does not exist' % (where, target))


def named_tests(evidence):
    """The tests an evidence field names, each written `test <Suite.Case>` or `test <file.py>::<name>`.

    Only those two spellings are read, so the word in ordinary prose - "the test fails" - names nothing.
    """
    return re.findall(r'\btest\s+([A-Za-z_]\w*\.py::[A-Za-z_]\w*|[A-Za-z_]\w*\.[A-Za-z_]\w*)\b', evidence)


def named_releases(evidence):
    """The releases an evidence field names, each written `release <library> <version>`.

    A repair that is a newer release of a dependency has no test of this engine's to point at; what
    holds it is that the library is pinned at that release, which the review of that library states.
    """
    return re.findall(r'\brelease\s+([A-Za-z0-9][\w.+-]*)\s+([0-9][\w.+-]*[0-9A-Za-z])', evidence)


def _native_tests(repo_root):
    found = set()
    pattern = re.compile(r'\bTEST(?:_F|_P)?\s*\(\s*([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)\s*\)')
    for path in (pathlib.Path(repo_root) / TEST_ROOT).rglob('*.cpp'):
        for suite, case in pattern.findall(path.read_text(encoding='utf-8', errors='replace')):
            found.add('%s.%s' % (suite, case))
    return found


def _script_test_exists(repo_root, file_name, function):
    path = pathlib.Path(repo_root) / SCRIPT_ROOT / file_name
    if not path.is_file():
        return False
    return re.search(r'^\s+def %s\(' % re.escape(function), path.read_text(encoding='utf-8', errors='replace'),
                     re.MULTILINE) is not None


def check_resolution(repo_root, records, problems):
    """What resolves a finding, and the evidence a resolution rests on."""
    native = None
    reviewed = reviews(records)
    for record in findings(records):
        where = _where(record)
        severity = record.fields.get('severity')
        status = record.fields.get('status')
        evidence = record.fields.get('evidence', '')
        if severity in BLOCKING and status not in RESOLVED:
            problems.append('%s: a %s finding that is %s is unresolved, and the register admits none'
                            % (where, severity, status))
        tests = named_tests(evidence)
        releases = named_releases(evidence)
        if status == 'repaired' and not tests and not releases:
            problems.append('%s: a repair names no test and no release, so nothing shows it stays repaired' % where)
        for library, version in releases:
            review = reviewed.get(library)
            if review is None:
                problems.append('%s: names release %s %s, and no review reads %s' % (where, library, version, library))
            elif review.fields.get('version') != version:
                problems.append('%s: names release %s %s, and the review of %s reads %s, so the release that '
                                'carries the repair is not the one pinned' % (where, library, version, library,
                                                                             review.fields.get('version')))
        if status == 'not-applicable' and evidence.strip() in ('', 'none'):
            problems.append('%s: not applicable, with no reason why' % where)
        for test in tests:
            script_match = SCRIPT_TEST.match(test)
            if script_match:
                if not _script_test_exists(repo_root, script_match.group(1), script_match.group(2)):
                    problems.append('%s: names test %s, which %s does not define'
                                    % (where, test, SCRIPT_ROOT))
                continue
            if native is None:
                native = _native_tests(repo_root)
            if test not in native:
                problems.append('%s: names test %s, which no suite under %s defines' % (where, test, TEST_ROOT))


def check_advisories(records, problems):
    """Every advisory a review lists is answered once, by a finding filed against that library."""
    reviewed = reviews(records)
    listed = {}
    for name, record in reviewed.items():
        for advisory in comma_list(record.fields.get('advisories', 'none')):
            listed[advisory] = name
    answered = {}
    for record in findings(records):
        component = record.fields.get('component', '')
        library = component[len('dependency '):] if component.startswith('dependency ') else None
        if library is not None and library not in reviewed:
            problems.append('%s: is filed against %s, which no review reads' % (_where(record), library))
        for advisory in comma_list(record.fields.get('advisories', 'none')):
            answered.setdefault(advisory, []).append(record)
            if advisory not in listed:
                problems.append('%s: answers %s, which no review lists' % (_where(record), advisory))
            elif listed[advisory] != library:
                problems.append('%s: answers %s, which the review of %s lists, from outside that library'
                                % (_where(record), advisory, listed[advisory]))
    for advisory, library in sorted(listed.items()):
        answers = answered.get(advisory, [])
        if not answers:
            problems.append('%s: lists %s and no finding answers it' % (_where(reviewed[library]), advisory))
        elif len(answers) > 1:
            problems.append('%s: %s is answered by %d findings' % (_where(reviewed[library]), advisory,
                                                                   len(answers)))


def _version_tokens(text):
    return set(re.findall(r'[0-9A-Za-z][0-9A-Za-z.\-]*[0-9A-Za-z]|[0-9A-Za-z]', text))


def check_provenance(repo_root, records, problems):
    """Every library the provenance register pins is reviewed, at the version it pins."""
    path = pathlib.Path(repo_root) / PROVENANCE
    registered, parse_problems = provenance.parse_register(path.read_text(encoding='utf-8'))
    problems.extend(parse_problems)
    pinned = {record.name: record.fields.get('version', '') for record in registered if record.kind == 'library'}
    reviewed = reviews(records)
    for name, version_text in sorted(pinned.items()):
        review = reviewed.get(name)
        if review is None:
            problems.append('%s: %s is registered and nobody has read its advisories' % (PROVENANCE, name))
            continue
        version = review.fields.get('version', '')
        if version not in _version_tokens(version_text):
            problems.append('%s: read version %s, and %s pins "%s", so the review is stale'
                            % (_where(review), version, PROVENANCE, version_text))
    for name, review in sorted(reviewed.items()):
        if name not in pinned and not comma_list(review.fields.get('binaries', 'none')):
            problems.append('%s: %s registers no library of that name, so the review has to name the '
                            'binaries it is loaded as' % (_where(review), PROVENANCE))


def check(repo_root=REPO_ROOT):
    """The whole register against the tree. Returns the problems and the counts a clean run prints."""
    repo_root = pathlib.Path(repo_root)
    path = repo_root / REGISTER
    if not path.is_file():
        return ['%s: no register, so no advisory and no finding was read' % REGISTER], None
    records, problems = parse_register(path.read_text(encoding='utf-8'))
    check_schema(repo_root, records, problems)
    check_resolution(repo_root, records, problems)
    check_advisories(records, problems)
    check_provenance(repo_root, records, problems)
    counts = {status: 0 for status in STATUSES}
    for record in findings(records):
        counts[record.fields.get('status')] = counts.get(record.fields.get('status'), 0) + 1
    advisories = sum(len(comma_list(r.fields.get('advisories', 'none'))) for r in reviews(records).values())
    return problems, (len(reviews(records)), advisories, len(findings(records)), counts)


def read_closure(directory, tool):
    """The imports of every shipped engine binary and of everything they load from beside them.

    Returns the problems and a map from each module read to the names it imports. A directory holding no
    shipped binary is a problem rather than an empty closure, so a reading of nothing never passes.
    """
    directory = pathlib.Path(directory)
    if not directory.is_dir():
        return ['%s: no such directory, so no binary was read' % directory], {}
    resolved = shutil.which(tool) if tool else (shutil.which('llvm-readobj') or shutil.which('readobj'))
    if not resolved and tool and pathlib.Path(tool).is_file():
        resolved = tool
    if not resolved:
        return ['no symbol reader was found; pass --binary-tool <llvm-readobj>'], {}
    roots = [path for path in sorted(directory.iterdir()) if path.is_file() and SHIPPED.match(path.name)]
    if not roots:
        return ['%s: holds no shipped engine binary, so this reading proves nothing' % directory], {}
    problems = []
    closure = {}
    frontier = list(roots)
    while frontier:
        path = frontier.pop()
        if path.name in closure:
            continue
        names = binary_imports.read_imports(resolved, path)
        if names is None:
            problems.append('%s: could not be read by %s' % (path.name, resolved))
            names = []
        closure[path.name] = names
        for name in names:
            beside = directory / name
            if beside.is_file() and name not in closure:
                frontier.append(beside)
    return problems, closure


def _this_platform(name, portable_executable):
    if portable_executable:
        return name.lower().endswith('.dll')
    return '.so' in name or name.endswith('.dylib')


def check_closure(records, closure, portable_executable):
    """Every deployed module the shipped binaries load is reviewed, and every reviewed one is loaded."""
    problems = []
    loaded = {name for name in closure if not SHIPPED.match(name)}
    named = {}
    for review in reviews(records).values():
        for binary in comma_list(review.fields.get('binaries', 'none')):
            named.setdefault(binary.lower(), review)
    for name in sorted(loaded):
        if name.lower() not in named:
            importers = sorted(module for module, imports in closure.items() if name in imports)
            problems.append('%s is loaded by %s and no review names it, so nobody has read its advisories'
                            % (name, ', '.join(importers)))
    loaded_lower = {name.lower() for name in loaded}
    for binary, review in sorted(named.items()):
        if _this_platform(binary, portable_executable) and binary not in loaded_lower:
            problems.append('%s: names %s, which the shipped binaries do not load - the library moved or '
                            'was renamed, and the review with it' % (_where(review), binary))
    return problems


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--binaries', help='a Release deployment whose shipped engine binaries are read')
    parser.add_argument('--binary-tool', help='llvm-readobj, or another reader that prints imports')
    arguments = parser.parse_args(argv)
    stream = stream or sys.stdout

    problems, counts = check(arguments.repo_root)
    summary = None
    if counts is not None:
        reviewed, advisories, found, statuses = counts
        summary = ('%d libraries reviewed with %d advisories answered; %d findings - %s - and no critical or '
                   'high one unresolved' % (reviewed, advisories, found,
                                            ', '.join('%d %s' % (statuses[s], s) for s in STATUSES)))
    if arguments.binaries and counts is not None:
        records, _ = parse_register((pathlib.Path(arguments.repo_root) / REGISTER).read_text(encoding='utf-8'))
        read_problems, closure = read_closure(arguments.binaries, arguments.binary_tool)
        problems.extend(read_problems)
        if closure:
            first = next(iter(sorted(closure)))
            with (pathlib.Path(arguments.binaries) / first).open('rb') as handle:
                portable_executable = handle.read(2) == b'MZ'
            problems.extend(check_closure(records, closure, portable_executable))
            shipped = sum(1 for name in closure if SHIPPED.match(name))
            summary += ('; the %d shipped engine binaries load %d deployed modules, every one reviewed'
                        % (shipped, len(closure) - shipped))
    for problem in problems:
        print(problem, file=stream)
    if problems:
        print('%d safety register problem(s).' % len(problems), file=stream)
        return 1
    print(summary, file=stream)
    return 0


if __name__ == '__main__':
    sys.exit(main())

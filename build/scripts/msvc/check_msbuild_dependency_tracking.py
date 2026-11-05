# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Detect MSBuild object files whose recorded header dependencies are incomplete.

MSVC incremental builds decide what to recompile from the read logs FileTracker writes
next to each object file (``<IntDir>/<name>.tlog/CL.read.*.tlog``). When a build is
interrupted - or the tracker otherwise loses events - a source can end up with a read log
that is an ordered *prefix* of the headers it really read, while its ``.obj`` survives.
Every later build then treats that object as up to date against a dependency set that is
missing headers, so edits to those headers never reach it.

The result is one binary linked from objects compiled against different revisions of the
same header. When the header declares a type, those objects disagree about the type's
layout and the program reads fields at the wrong offsets: wrong values, or an access
violation with no meaningful stack. See docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md.

The check errs towards silence, never towards noise. For every source it walks the
in-repository headers reachable through includes written outside any preprocessor
conditional, and requires each of them to appear in that source's recorded read set: the
compiler cannot have skipped an unconditional include, so a missing one is proof the log
is short. Conditional includes and includes that resolve outside the source roots are left
out of the requirement, so a finding is always real - at the price of missing truncations
that dropped nothing but system headers.
Where one spelling could name more than one file in the tree - the test suites shadow
several headers with mocks - the read log itself settles which one the compiler opened,
so the walk follows the file that was really included rather than every candidate.

Projects built with a precompiled header are handled the way MSBuild reasons about them:
a source that reads a ``.pch`` does not re-read the headers baked into it, so the reads
recorded for the source that *creates* the PCH count towards every consumer.

Usage:
    python check_msbuild_dependency_tracking.py [--obj-root DIR] [--source-root DIR] [--fix]

Exit code 0 when every object is tracked correctly, 1 when findings remain, 2 on usage
errors. With --fix the offending objects are deleted so the next build recompiles them.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*(?:"([^"]+)"|<([^>]+)>)')
_COND_OPEN_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef)\b')
_COND_CLOSE_RE = re.compile(r'^\s*#\s*endif\b')
_BLOCK_COMMENT_RE = re.compile(r'/\*.*?\*/', re.DOTALL)
_SOURCE_SUFFIXES = ('.C', '.CC', '.CPP', '.CXX')
_HEADER_SUFFIXES = ('.h', '.hpp', '.hxx', '.inl')
_DEFAULT_SOURCE_ROOTS = (os.path.join('src'), os.path.join('3rdparty', 'src'))


def decode_tlog(raw):
    """Decode tlog bytes; MSBuild writes them as UTF-16LE with a BOM."""
    if raw[:2] == b'\xff\xfe':
        return raw.decode('utf-16-le', 'replace')
    return raw.decode('utf-8-sig', 'replace')


def parse_tlog(path):
    """Return {source (upper case): [recorded paths (upper case), in recorded order]}."""
    entries = {}
    current = []
    with open(path, 'rb') as handle:
        text = decode_tlog(handle.read())
    for line in text.splitlines():
        line = line.strip().lstrip('﻿')
        if not line:
            continue
        if line.startswith('^'):
            current = [part.upper() for part in line[1:].split('|') if part]
            for source in current:
                entries.setdefault(source, [])
        elif current:
            for source in current:
                entries[source].append(line.upper())
    return entries


def merge_read_tlogs(tlog_dir):
    """Union the CL.read.*.tlog entries of one .tlog directory, the way MSBuild reads them."""
    merged = {}
    for path in sorted(glob.glob(os.path.join(tlog_dir, 'CL.read.*.tlog'))):
        for source, reads in parse_tlog(path).items():
            merged.setdefault(source, set()).update(reads)
    return merged


class HeaderIndex:
    """In-repository headers, indexed for include resolution."""

    def __init__(self, source_roots):
        self._by_basename = {}
        self._known = set()
        for root in source_roots:
            if not os.path.isdir(root):
                continue
            for current, _subdirs, files in os.walk(root):
                for name in files:
                    if not name.endswith(_HEADER_SUFFIXES):
                        continue
                    path = os.path.normpath(os.path.join(current, name))
                    self._by_basename.setdefault(name.upper(), []).append(path)
                    self._known.add(path.upper())

    def candidates(self, spec, including_file):
        """Every file in the tree an include spelling could name.

        Resolution is directory-aware so the 30-odd headers that share a basename do not
        pull unrelated files into the dependency set: the file next to the includer, plus
        whatever matches the spelled path as a suffix. All of them are returned rather
        than only the first - the compiler's include order depends on project settings
        this script does not read, so the caller lets the read log decide which one was
        really opened.
        """
        spec = spec.replace('\\', '/')
        found = []
        sibling = os.path.normpath(os.path.join(os.path.dirname(including_file), spec))
        if sibling.upper() in self._known:
            found.append(sibling)

        by_name = self._by_basename.get(os.path.basename(spec).upper(), [])
        if '/' in spec:
            # A spelled-out path must match a spelled-out path. Falling back to the bare
            # basename here would let <kddockwidgets/core/Group.h> resolve to cfitsio's
            # group.h and invent a dependency the compiler never had.
            tail = os.path.normpath(spec).replace('\\', '/').upper()
            by_name = [path for path in by_name
                       if path.replace('\\', '/').upper().endswith('/' + tail)]

        seen = {path.upper() for path in found}
        found.extend(path for path in by_name if path.upper() not in seen)
        return found


def unconditional_includes(path):
    """Include specs written outside every preprocessor conditional in one file.

    Headers here are overwhelmingly ``#pragma once``, so their contents sit at depth 0
    like a .cpp's do. The few that use an include guard simply contribute nothing, which
    costs recall and never costs correctness.
    """
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as handle:
            text = handle.read()
    except OSError:
        return []

    text = _BLOCK_COMMENT_RE.sub(' ', text)
    specs = []
    depth = 0
    for line in text.splitlines():
        if line.lstrip().startswith('//'):
            continue
        if _COND_OPEN_RE.match(line):
            depth += 1
            continue
        if _COND_CLOSE_RE.match(line):
            depth = max(0, depth - 1)
            continue
        if depth:
            continue
        match = _INCLUDE_RE.match(line)
        if match:
            specs.append(match.group(1) or match.group(2))
    return specs


def untracked_headers(source_path, recorded, index, cache):
    """In-repository headers the source unconditionally includes but never recorded.

    ``recorded`` is the set of upper-case absolute paths the read log credits to this
    object. It doubles as the disambiguator: when a spelling matches several files in the
    tree, whichever one the log says was opened is the one the walk descends into. A
    spelling that matches nothing in the log is reported and not followed - its own
    includes cannot be attributed to a file that may never have been read.
    """
    missing = set()
    visited = set()
    pending = [source_path]
    while pending:
        current = pending.pop()
        key = current.upper()
        if key in visited:
            continue
        visited.add(key)
        specs = cache.get(key)
        if specs is None:
            specs = unconditional_includes(current)
            cache[key] = specs
        for spec in specs:
            candidates = index.candidates(spec, current)
            if not candidates:
                continue
            opened = [path for path in candidates if path.upper() in recorded]
            if opened:
                pending.extend(opened)
            else:
                missing.add(os.path.basename(spec.replace('\\', '/')))
    return missing


def precompiled_header_reads(entries):
    """Reads a PCH supplies to its consumers, or an empty set when no PCH is in use.

    A source compiled with /Yu records the .pch itself as a read and does not re-read the
    headers baked into it; the source compiled with /Yc records them all. The creator is
    therefore the source that does not read a .pch, and MSBuild rebuilds every consumer
    when the PCH is rebuilt, so crediting the creator's reads to consumers is exact.
    """
    consumers = {source for source, reads in entries.items()
                 if any(path.endswith('.PCH') for path in reads)}
    if not consumers:
        return set()
    supplied = set()
    for source, reads in entries.items():
        if source not in consumers:
            supplied.update(reads)
    return supplied


def analyse_tlog_dir(tlog_dir, index, cache):
    """Findings for one .tlog directory: sources whose read log misses required headers."""
    int_dir = os.path.dirname(tlog_dir)
    entries = merge_read_tlogs(tlog_dir)
    pch_reads = precompiled_header_reads(entries)
    findings = []
    for source, reads in entries.items():
        if not source.endswith(_SOURCE_SUFFIXES) or not os.path.exists(source):
            continue
        obj_path = os.path.join(int_dir, os.path.splitext(os.path.basename(source))[0] + '.obj')
        if not os.path.exists(obj_path):
            # Nothing stale can reach the link: the next build has to compile this source.
            continue
        recorded = reads | pch_reads if any(path.endswith('.PCH') for path in reads) else reads
        missing = sorted(untracked_headers(source, recorded, index, cache))
        if missing:
            findings.append({'source': source, 'obj': obj_path, 'missing': missing})
    return findings


def find_tlog_dirs(obj_root):
    """Every .tlog directory under obj_root that holds compiler read logs."""
    found = []
    for current, subdirs, _files in os.walk(obj_root):
        for name in subdirs:
            if not name.lower().endswith('.tlog'):
                continue
            candidate = os.path.join(current, name)
            if glob.glob(os.path.join(candidate, 'CL.read.*.tlog')):
                found.append(candidate)
    return sorted(found)


def run(obj_root, source_roots, fix, stream):
    if not os.path.isdir(obj_root):
        stream.write('Object root not found: {}\n'.format(obj_root))
        return 2

    tlog_dirs = find_tlog_dirs(obj_root)
    if not tlog_dirs:
        stream.write('No MSBuild tracking logs under {} - nothing to check.\n'.format(obj_root))
        return 0

    index = HeaderIndex(source_roots)
    cache = {}
    findings = []
    for tlog_dir in tlog_dirs:
        findings.extend(analyse_tlog_dir(tlog_dir, index, cache))

    if not findings:
        stream.write('Dependency tracking OK: {} tracking log(s) checked, no stale objects.\n'
                     .format(len(tlog_dirs)))
        return 0

    stream.write('Incomplete dependency tracking in {} object(s):\n'.format(len(findings)))
    unfixed = 0
    for finding in sorted(findings, key=lambda item: (item['source'], item['obj'])):
        shown = ', '.join(finding['missing'][:6])
        if len(finding['missing']) > 6:
            shown += ', ... (+{} more)'.format(len(finding['missing']) - 6)
        stream.write('  {}\n'.format(finding['source']))
        stream.write('    object: {}\n'.format(finding['obj']))
        stream.write('    untracked headers: {}\n'.format(shown))
        if not fix:
            continue
        try:
            os.remove(finding['obj'])
            stream.write('    removed stale object\n')
        except OSError as error:
            unfixed += 1
            stream.write('    FAILED to remove: {}\n'.format(error))

    if not fix:
        stream.write('\nRe-run with --fix to delete these objects, or rebuild from clean.\n')
        return 1
    if unfixed:
        stream.write('\n{} object(s) could not be removed; rebuild from clean.\n'.format(unfixed))
        return 1
    stream.write('\nRemoved {} stale object(s); the next build will recompile them.\n'
                 .format(len(findings)))
    return 0


def main(argv=None):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, '..', '..', '..'))

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--obj-root', default=os.path.join(repo_root, 'obj'),
                        help='Tree holding MSBuild intermediate output (default: %(default)s)')
    parser.add_argument('--source-root', action='append', dest='source_roots', default=None,
                        help='Directory of in-repository headers; repeatable '
                             '(default: src and 3rdparty/src)')
    parser.add_argument('--fix', action='store_true',
                        help='Delete objects with incomplete tracking so the next build recompiles them')
    args = parser.parse_args(argv)

    source_roots = args.source_roots or [os.path.join(repo_root, part)
                                         for part in _DEFAULT_SOURCE_ROOTS]
    return run(os.path.abspath(args.obj_root), [os.path.abspath(root) for root in source_roots],
               args.fix, sys.stdout)


if __name__ == '__main__':
    sys.exit(main())

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the header architecture of AfterNightEngine to what it was designed to be.

A header grows large one function at a time: an order-statistic network expanded into a header
because that was where the caller was, a conversion template added beside it because the header
already had templates, and then twenty translation units compiling all of it because one of them
needed a line. Nobody decides that. The budgets in `check_budgets.py` measure what a header costs a
compiler, which is the number that moves last; this measures the shape that produces the cost, which
is the one a change can still be argued about.

Three rules, and each of them is a property of the tree that no compiler, build log or include graph
reports:

1. **No header declares more than the budget allows.** The count is declaration lines - not blank,
   not a comment, not a preprocessor directive - so the documentation this engine requires of every
   public symbol is never what trips it. A header that needs more is not refused, it is registered:
   which is rule 2.

2. **A header of expanded bodies is private, registered, and included by exactly one translation
   unit.** Selection networks, generated tables and machine-expanded arithmetic are legitimate and
   are the reason a header gets large; what makes one a problem is being reachable, so that every
   consumer compiles it. The register is read in both directions - an
   unregistered header over the budget is a finding, and so is a registered one that no longer
   exceeds it or that a second file has started including - so an entry cannot outlive its reason.

3. **An explicit instantiation appears only where the register names it.** This engine has none, and
   that is a design outcome rather than an oversight: the work that would otherwise be instantiated
   per type in a header lives in private per-instruction-set translation units behind a dispatch
   table, so there is nothing whose instantiation cost an `extern template` would move. Adding one
   is a decision about the exported ABI - an exported instantiation pins a layout into it - so it is
   made in the register with a reason rather than in a file.

What this does not read: the include graph, which is `check_include_layers.py`'s, including the
umbrella rule; the per-header include and compile budgets, which are `check_budgets.py`'s; and
whether any target compiles through a precompiled header or a forced include, which is
`check_build_graph.py`'s, because only the generated projects say what a build actually does.

Comments and string literals are removed before any rule reads a line, because this engine's headers
discuss templates, instantiation and expanded bodies constantly and on purpose.

Usage:
    python build/scripts/tools/check_header_architecture.py [--repo-root <dir>]

Exits 0 when every rule holds, 1 otherwise, printing one line per problem.

Tests: build/scripts/tools/check_header_architecture_test.py
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import source_scan  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

HEADER_SUFFIXES = ('.h', '.hpp', '.inl')
SOURCE_SUFFIXES = HEADER_SUFFIXES + ('.cpp',)


class Component:
    """One binary of this engine's side of the workspace: where it publishes and where it does not.

    The adapters and the opt-in backend are held to the same rules as the engine. They are smaller
    and they are optional, which is exactly the condition under which a rule stops being applied.
    """

    def __init__(self, name):
        self.name = name
        self.root = 'src/' + name
        self.public_root = self.root + '/include'
        self.private_root = self.root + '/src'
        self.include_prefix = name + '/'


COMPONENTS = (
    Component('AfterNightEngine'),
    Component('AfterNightEnginePythonAdapter'),
    Component('AfterNightEngineGmicAdapter'),
    Component('AfterNightEngineOpenClBackend'),
)

# How much declaration one header may carry.
#
# Measured rather than chosen, and set where it refuses a shape rather than where it bites on
# growth. When this budget was set the widest header in all four components declared 440 lines -
# `kernels/reference/AlgorithmKernels.h`, which is the private vocabulary the algorithm executors
# and the compiled dispatch tables share: function-pointer types, parameter and outcome structs and
# selector declarations for about twenty kernel families, and no body at all. It grows one family
# at a time, so a ceiling just above it would be raised the next time a family arrives, which is
# the number that gets raised rather than the rule that gets respected. The widest published header
# declares 343 (`Image/Image.h`, which composes storage, description, metadata and the views an
# image hands out) and every other header is below 410.
#
# What the ceiling refuses is a header that carries template implementation for every includer to
# compile, which is the shape that makes a header expensive; a header that only declares stays well
# below it.
#
# The number is declaration lines rather than physical ones on purpose. Every public symbol here is
# documented to a mandatory standard, so about two thirds of the published surface is prose - the
# widest header is 723 physical lines for its 343 - and a physical-line budget would be a
# documentation budget wearing a compile-time budget's name.
MAX_DECLARATION_LINES = 600


class ExpandedBody:
    """One header whose body is expanded or generated rather than written, and its one consumer."""

    def __init__(self, header, unit, reason):
        self.header = header
        self.unit = unit
        self.reason = reason


EXPANDED_BODIES = (
    ExpandedBody(
        'src/AfterNightEngine/src/algorithms/spatial/MedianNetworks.h',
        'src/AfterNightEngine/src/algorithms/spatial/OrderFilter.cpp',
        'hand-pruned branch-free selection networks for the complete 3x3, 5x5, 7x7 and 9x9 Float32 '
        'median windows, the measured fast path for those windows. Expanded comparator sequences are '
        'what a selection network is; there is nothing to factor out, and the compare-exchange order '
        'is the algorithm. It is private and the order filter is its only consumer, so one translation '
        'unit compiles it rather than every file that reaches the filter'),
)


class ExplicitInstantiation:
    """One file allowed to instantiate a template explicitly, and why the engine needs it to."""

    def __init__(self, path, reason):
        self.path = path
        self.reason = reason


# Deliberately empty. See rule 3 in this file's opening description: the engine has no template
# whose instantiation cost an explicit instantiation would move, because the per-type work is in
# private per-instruction-set translation units behind a dispatch table rather than in a header.
EXPLICIT_INSTANTIATIONS = ()

_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.MULTILINE)

# An explicit instantiation at namespace scope, in each of its three spellings: the definition
# (`template class X<int>;`), the declaration that suppresses one (`extern template ...`), and the
# function form (`template void f<int>(...);`). A template *definition* and a *specialization* both
# carry a parameter list - `template <...>` or `template<>` - immediately after the keyword, which
# is the one thing that tells them apart from an instantiation lexically, so the pattern refuses to
# match when the next non-space character is `<`. The gap spans newlines, because the keyword and
# what it instantiates may be written on two lines; the word boundary is what stops an identifier
# that merely begins with the keyword from matching.
_INSTANTIATION = re.compile(r'^[ \t]*(extern[ \t]+template|template)\b(?!\s*<)\s*[A-Za-z_]',
                            re.MULTILINE)


def read_text(path):
    return path.read_text(encoding='utf-8', errors='replace')


def commentless(text):
    """The source with its comments removed and its literals intact, line for line.

    From `source_scan`, which needed it for the same reason and learned the same
    lesson: a regular expression that strips `//` to the end of the line also strips what follows
    a `"http://..."`, and what it strips is code.
    """
    return source_scan.strip_comments(text)


def code_of(text):
    """The source with comments removed and every literal blanked, keeping every line where it was.

    An include line's path is a literal, so this is not what includes are read from - `commentless`
    is. It is what the instantiation rule reads, because a sentence about `template class` inside a
    string is prose that happens to be quoted.
    """
    return source_scan.blank_literals(commentless(text))[0]


def declaration_lines(text):
    """How many lines of one file are a declaration: not blank, not a comment, not preprocessor.

    A preprocessor directive continues while its line ends in a backslash, so a multi-line macro
    counts as the one directive it is rather than as the body it expands to.
    """
    count = 0
    continued = False
    for line in code_of(text).splitlines():
        stripped = line.strip()
        if continued:
            continued = stripped.endswith('\\')
            continue
        if not stripped:
            continue
        if stripped.startswith('#'):
            continued = stripped.endswith('\\')
            continue
        count += 1
    return count


def component_files(repo_root, component):
    """Every source and header of one component, by repository-relative path, in a stable order."""
    base = repo_root / component.root
    found = {}
    for path in sorted(base.rglob('*')):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        found[path.relative_to(repo_root).as_posix()] = path
    return found


def collect(repo_root):
    """Every file of every component, and the components that are missing.

    Returns ({relative path: (component, absolute path)}, [problems]).
    """
    files = {}
    problems = []
    for component in COMPONENTS:
        if not (repo_root / component.root).is_dir():
            problems.append('%s: not found' % component.root)
            continue
        for relative, path in component_files(repo_root, component).items():
            files[relative] = (component, path)
    return files, problems


def is_public(component, relative):
    return relative.startswith(component.public_root + '/')


def resolve_include(component, relative, quoted, target):
    """Where one include line could point, as repository-relative paths in the order to try them.

    A quoted include is written from the component's private root - `"layer/Name.h"` - which is the
    spelling the layer rules require; it is also resolved beside the including file, because that is
    the other spelling a compiler would accept and this check should not be the reason one is
    missed. An angled include names a published header of any component by its own prefix.
    """
    if quoted:
        candidates = [component.private_root + '/' + target,
                      os.path.normpath(os.path.join(os.path.dirname(relative), target)).replace(os.sep, '/')]
        return candidates
    for other in COMPONENTS:
        if target.startswith(other.include_prefix):
            return [other.public_root + '/' + target]
    return []


def includers(files):
    """{included path: [the paths that include it]}, over every file of every component."""
    reached = {}
    for relative, (component, path) in sorted(files.items()):
        for match in _INCLUDE.finditer(commentless(read_text(path))):
            quoted = match.group(1) == '"'
            for candidate in resolve_include(component, relative, quoted, match.group(2).strip()):
                if candidate in files:
                    reached.setdefault(candidate, []).append(relative)
                    break
    return reached


def check_declaration_weight(files, weights, registered):
    problems = []
    for relative in sorted(files):
        if pathlib.PurePosixPath(relative).suffix not in HEADER_SUFFIXES:
            continue
        if weights[relative] <= MAX_DECLARATION_LINES:
            continue
        if relative in registered:
            continue
        problems.append('%s declares %d lines against a budget of %d, and no register entry explains '
                        'it; a header that carries an expanded body is registered in '
                        '%s, is private, and is included by exactly one translation unit'
                        % (relative, weights[relative], MAX_DECLARATION_LINES,
                           os.path.basename(__file__)))
    return problems


def check_expanded_bodies(files, weights, reached):
    problems = []
    for entry in EXPANDED_BODIES:
        if entry.header not in files:
            problems.append('%s is registered as an expanded body but does not exist' % entry.header)
            continue
        if not entry.reason.strip():
            problems.append('%s is registered with no reason; an expanded body is kept because '
                            'something about it cannot be written any other way, and that is what '
                            'the entry is for' % entry.header)
        component, _ = files[entry.header]
        if is_public(component, entry.header):
            problems.append('%s is registered as an expanded body but is published; expanded code is '
                            'private, so that one translation unit compiles it rather than every '
                            'consumer' % entry.header)
        if weights[entry.header] <= MAX_DECLARATION_LINES:
            problems.append('%s is registered as an expanded body but declares %d lines, within the '
                            'budget of %d; the register cannot outlive its reason'
                            % (entry.header, weights[entry.header], MAX_DECLARATION_LINES))
        if entry.unit not in files:
            problems.append('%s is registered as included by %s, which does not exist'
                            % (entry.header, entry.unit))
            continue
        including = reached.get(entry.header, [])
        if entry.unit not in including:
            problems.append('%s is registered as included by %s, which does not include it'
                            % (entry.header, entry.unit))
        for other in including:
            if other != entry.unit:
                problems.append('%s is registered as included only by %s, and is included by %s as '
                                'well; an expanded body is compiled once' % (entry.header, entry.unit, other))
    return problems


def _is_disambiguator(code, keyword_start):
    """Whether this `template` is the one that disambiguates a dependent member, not an instantiation.

    `object.template get<T>()` needs the keyword, and a call wrapped after the `.` puts it at the
    start of a line where it looks exactly like an instantiation. What tells them apart is what
    precedes it: a member access, never a statement boundary.
    """
    index = keyword_start - 1
    while index >= 0 and code[index] in ' \t\r\n':
        index -= 1
    if index < 0:
        return False
    if code[index] == '.':
        return True
    # Only the two-character forms, so that the `>` ending an `#include <...>` on the line above,
    # or a `:` ending a label, is not read as a member access.
    return code[index - 1:index + 1] in ('->', '::')


def check_explicit_instantiations(files):
    problems = []
    registered = {entry.path: entry for entry in EXPLICIT_INSTANTIATIONS}
    seen = set()
    for relative in sorted(files):
        _, path = files[relative]
        code = code_of(read_text(path))
        for match in _INSTANTIATION.finditer(code):
            if _is_disambiguator(code, match.start(1)):
                continue
            line = code.count('\n', 0, match.start()) + 1
            statement = code[match.start():code.find('\n', match.start())].strip()
            if relative in registered:
                seen.add(relative)
                continue
            problems.append('%s:%d declares an explicit instantiation (%s); this engine has none, '
                            'and adding one is a decision about the exported ABI rather than a '
                            'compile-time tactic, so it is registered in %s with its reason'
                            % (relative, line, statement, os.path.basename(__file__)))
    for relative in sorted(registered):
        if relative not in files:
            problems.append('%s is registered as explicitly instantiating a template but does not '
                            'exist' % relative)
        elif relative not in seen:
            problems.append('%s is registered as explicitly instantiating a template, and does not; '
                            'the register cannot outlive its reason' % relative)
    return problems


def check(repo_root):
    files, problems = collect(repo_root)
    weights = {relative: declaration_lines(read_text(path)) for relative, (_, path) in files.items()}
    registered = {entry.header for entry in EXPANDED_BODIES}
    reached = includers(files)
    problems += check_declaration_weight(files, weights, registered)
    problems += check_expanded_bodies(files, weights, reached)
    problems += check_explicit_instantiations(files)
    return problems, files, weights


def summary(files, weights):
    headers = [relative for relative in files
               if pathlib.PurePosixPath(relative).suffix in HEADER_SUFFIXES]
    ordinary = [weights[relative] for relative in headers
                if relative not in {entry.header for entry in EXPANDED_BODIES}]
    widest = max(ordinary) if ordinary else 0
    return ('the header architecture holds: %d header(s) across %d component(s), the widest '
            'declaring %d lines against a budget of %d, %d registered expanded body/bodies and %d '
            'explicit instantiation(s)'
            % (len(headers), len(COMPONENTS), widest, MAX_DECLARATION_LINES, len(EXPANDED_BODIES),
               len(EXPLICIT_INSTANTIATIONS)))


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT),
                        help='repository root to check; defaults to the one containing this script')
    arguments = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout

    problems, files, weights = check(pathlib.Path(arguments.repo_root).resolve())
    for problem in problems:
        print(problem, file=out)
    if problems:
        print('%d header architecture problem(s)' % len(problems), file=out)
        return 1
    print(summary(files, weights), file=out)
    return 0


if __name__ == '__main__':
    sys.exit(main())

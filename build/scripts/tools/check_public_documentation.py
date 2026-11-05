# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Refuse an undocumented public declaration in AfterNightEngine, using Doxygen as the check.

The engine's documentation standard is enforced in the change that writes the code, not audited once
at the end, and that only works if something mechanical says no. Doxygen already reads every public
declaration to build the reference, so it is also the cheapest thing that can notice one carrying no
comment: WARN_IF_UNDOCUMENTED and WARN_NO_PARAMDOC report it and WARN_AS_ERROR makes the report
fatal. One configuration file produces both the reference and the check, so there is no second symbol
inventory to keep in step.

What this can judge is narrow, and pretending otherwise would be worse than not checking at all. It
sees whether a declaration carries a comment. It cannot see whether that comment states ownership,
lifetime, units, ranges, thread safety, cancellation behaviour or error results - which is what the
standard actually asks for. Review enforces those. A green run here is a floor.

Two things about the reference itself are also held here, because each goes wrong silently:

* **Every published header opens with a `/// @file` block.** Its module documentation - what the
  header is for and how its parts relate, which is the best writing in the engine - reaches the
  reference only through that command. Written as ordinary `//` comments, it stays in the source and
  the header's page is blank. Only the licence header's two SPDX lines may stand above it.
* **The reference opens on its main page.** `USE_MDFILE_AS_MAINPAGE` names
  `build/doxygen/AfterNightEngine.mainpage.md`, the file exists, and it is part of the input;
  without it the reference opens on an empty page above an index of one namespace.

Two failures are deliberately distinguishable, because they need different responses:

* exit 1 - the engine has an undocumented declaration, or the configuration that finds them has been
  weakened. Fix the code, or restore the setting;
* exit 2 - Doxygen is not installed, or it could not run this check as configured. Nothing here can
  be read as a documented public surface.

A Doxygen too old for the configuration lands in the second of those, because Doxygen does not
refuse a tag it does not recognise: it prints one warning and carries on with that rule switched
off. `HIDE_UNDOC_NAMESPACES` is such a tag, understood from 1.10 and ignored by the 1.9.8 that
Ubuntu 24.04 ships, and it is the tag that makes an undocumented namespace a failure. So an ignored tag is not a finding about the engine and
is not a pass either - it means the run applied fewer rules than this file states, and the answer is
a newer Doxygen (`build/scripts/linux/install_doxygen.sh`), not an edit to the engine.

Usage:
    python build/scripts/tools/check_public_documentation.py [--repo-root <dir>]
                                                             [--doxygen <path>]
                                                             [--output-dir <dir>]

Without --output-dir the generated reference goes to a temporary directory and is removed; pass one
to keep the HTML. DOXYGEN_BIN names the executable when it is not on PATH.

Tests: build/scripts/tools/check_public_documentation_test.py
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

DOXYFILE = 'build/doxygen/AfterNightEngine.doxyfile'
PUBLIC_INCLUDE_DIR = 'src/AfterNightEngine/include'

# Every published surface in the tree, not only the engine's. An optional adapter is a second library
# with a second include root, and a declaration a consumer compiles against is documented wherever it
# lives - so the configured input is the set of them, and a run that lost one would report a clean
# surface it had not read. The opt-in GPU backend's surface is read whether or not a tree was generated
# with it, because it needs no SDK to be documented.
PUBLIC_INCLUDE_DIRS = (PUBLIC_INCLUDE_DIR, 'src/AfterNightEngineGmicAdapter/include',
                       'src/AfterNightEngineOpenClBackend/include',
                       'src/AfterNightEnginePythonAdapter/include')

# The settings the check depends on. Any one of them turned the other way makes the run silent
# rather than clean, which is the failure mode this guard exists for: a green tick over a check that
# has been switched off reads exactly like a green tick over documented code.
REQUIRED_SETTINGS = {
    'WARNINGS': 'YES',
    'WARN_IF_UNDOCUMENTED': 'YES',
    'WARN_IF_DOC_ERROR': 'YES',
    'WARN_IF_INCOMPLETE_DOC': 'YES',
    'WARN_NO_PARAMDOC': 'YES',
    'WARN_IF_UNDOC_ENUM_VAL': 'YES',
    'WARN_AS_ERROR': 'FAIL_ON_WARNINGS',
    'EXTRACT_ALL': 'NO',
    # Each of these hides a kind of undocumented declaration from the output, and hiding one also
    # stops it being reported: with HIDE_UNDOC_MEMBERS on, an undocumented member of a documented
    # struct produces no diagnostic at all. Measured against both 1.9.8 and 1.14.0 rather than read
    # off the manual, because this is the difference between a check and a green tick.
    'HIDE_UNDOC_MEMBERS': 'NO',
    'HIDE_UNDOC_CLASSES': 'NO',
    'HIDE_UNDOC_NAMESPACES': 'NO',
    'RECURSIVE': 'YES',
    'JAVADOC_AUTOBRIEF': 'YES',
    'INPUT': ' '.join(PUBLIC_INCLUDE_DIRS),
}

MAINPAGE = 'build/doxygen/AfterNightEngine.mainpage.md'
FILE_BLOCK = '/// @file'
# A line of the licence header every file opens with (check_license_headers.py).
LICENCE_LINE = re.compile(r'^// SPDX-(?:License-Identifier|FileCopyrightText): \S')

SETTING_PATTERN = re.compile(r'^\s*([A-Z_]+)\s*=\s*(.*?)\s*$')
APPEND_PATTERN = re.compile(r'^\s*([A-Z_]+)\s*\+=\s*(.*?)\s*$')
DIAGNOSTIC_PATTERN = re.compile(r'\b(?:warning|error)\b\s*:', re.IGNORECASE)

# A diagnostic about the engine is written in WARN_FORMAT, which this configuration sets to
# "$file:$line: $text". Anything else Doxygen reports is about the run itself - the configuration it
# was handed on standard input, or a path it could not read - and those are different findings with
# different fixes, so they are not counted together.
SOURCE_DIAGNOSTIC_PATTERN = re.compile(r'^.+?:\d+:\s*(?:warning|error)\s*:', re.IGNORECASE)

TOOL_UNUSABLE = 2
PROBLEMS_FOUND = 1
CLEAN = 0


def read_settings(text):
    """Every single-line setting in a Doxyfile, as a name to value map.

    Continuation lines and appends are not interpreted: this reads the file to confirm a handful of
    named settings, not to reimplement Doxygen's configuration parser.
    """
    settings = {}
    for line in text.splitlines():
        if line.lstrip().startswith('#'):
            continue
        match = SETTING_PATTERN.match(line)
        if match:
            settings[match.group(1)] = match.group(2).strip('"')
    return settings


def check_configuration(text):
    """The configuration still carries the settings that make this a check rather than a report."""
    settings = read_settings(text)
    problems = []
    for name in sorted(REQUIRED_SETTINGS):
        expected = REQUIRED_SETTINGS[name]
        actual = settings.get(name)
        if actual is None:
            problems.append('%s: %s is not set; it must be %s for this to be a check'
                            % (DOXYFILE, name, expected))
        elif actual != expected:
            problems.append('%s: %s is %s; it must be %s for this to be a check'
                            % (DOXYFILE, name, actual, expected))
    return problems


def appended_values(text, name):
    """Every value a `NAME += ...` line of a Doxyfile appends, in order."""
    values = []
    for line in text.splitlines():
        if line.lstrip().startswith('#'):
            continue
        match = APPEND_PATTERN.match(line)
        if match and match.group(1) == name:
            values += match.group(2).strip('"').split()
    return values


def check_mainpage(text, repo_root):
    """The reference opens on its main page: named, present, and part of the input."""
    settings = read_settings(text)
    problems = []
    if settings.get('USE_MDFILE_AS_MAINPAGE') != MAINPAGE:
        problems.append('%s: USE_MDFILE_AS_MAINPAGE is %s; it must be %s, or the reference opens on an empty '
                        'page' % (DOXYFILE, settings.get('USE_MDFILE_AS_MAINPAGE', 'not set'), MAINPAGE))
    if MAINPAGE not in settings.get('INPUT', '').split() + appended_values(text, 'INPUT'):
        problems.append('%s: INPUT does not read %s, so doxygen never sees the main page' % (DOXYFILE, MAINPAGE))
    if not (repo_root / MAINPAGE).is_file():
        problems.append('%s: the reference\'s main page does not exist' % MAINPAGE)
    return problems


def check_file_blocks(repo_root, include_dirs=PUBLIC_INCLUDE_DIRS):
    """Every published header whose module documentation would not reach the reference.

    The first line of the file must open a `/// @file` block, because Doxygen attaches a header's own
    documentation to its page only through that command and only when it is the header's leading
    comment. The licence header's SPDX lines may stand above it: they are plain comments, which Doxygen
    does not read, so the block is still the header's leading documentation.
    """
    problems = []
    for include_dir in include_dirs:
        base = repo_root / include_dir
        if not base.is_dir():
            continue
        for path in sorted(base.rglob('*')):
            if path.suffix not in ('.h', '.hpp') or not path.is_file():
                continue
            lines = path.read_bytes().decode('utf-8', errors='replace').lstrip('﻿').split('\n')
            index = 0
            while index < len(lines) and LICENCE_LINE.match(lines[index].rstrip('\r')):
                index += 1
            if index and index < len(lines) and not lines[index].strip():
                index += 1
            first = lines[index] if index < len(lines) else ''
            if first.rstrip('\r') != FILE_BLOCK:
                problems.append('%s:1: opens without a %s block, so its module documentation never reaches '
                                'the reference' % (path.relative_to(repo_root).as_posix(), FILE_BLOCK))
    return problems


def find_doxygen(explicit=None, environment=None):
    """The Doxygen executable to run, or None when there is none to run."""
    environment = os.environ if environment is None else environment
    for candidate in (explicit, environment.get('DOXYGEN_BIN')):
        if candidate:
            resolved = shutil.which(candidate) or (candidate if os.path.isfile(candidate) else None)
            if resolved:
                return resolved
            return None
    return shutil.which('doxygen')


def compose_configuration(text, output_dir):
    """The Doxyfile with the output directory appended, which is the only value a run overrides.

    Doxygen reads a configuration from standard input when it is given a single dash, and a later
    assignment wins, so the output can be redirected without editing the file or writing a copy of
    it next to the tree being checked.
    """
    return text + '\nOUTPUT_DIRECTORY = ' + str(output_dir).replace(os.sep, '/') + '\n'


def extract_diagnostics(output):
    """The lines of a Doxygen run that report a problem, in the order it reported them."""
    return [line.rstrip() for line in output.splitlines() if DIAGNOSTIC_PATTERN.search(line)]


def is_source_diagnostic(diagnostic):
    """Whether a diagnostic is about a declaration in the engine rather than about the run."""
    return SOURCE_DIAGNOSTIC_PATTERN.match(diagnostic) is not None


def run_doxygen(executable, configuration, repo_root, runner=None):
    """Run Doxygen over the configuration and return its exit code and combined output."""
    if runner is not None:
        return runner(executable, configuration, repo_root)
    completed = subprocess.run([executable, '-'], input=configuration, cwd=str(repo_root),
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               encoding='utf-8', errors='replace', check=False)
    return completed.returncode, completed.stdout


def main(argv=None, stream=None, runner=None, environment=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT),
                        help='repository root to check; defaults to the one containing this script')
    parser.add_argument('--doxygen', default=None,
                        help='Doxygen executable; defaults to DOXYGEN_BIN, then PATH')
    parser.add_argument('--output-dir', default=None,
                        help='keep the generated reference here instead of in a temporary directory')
    arguments = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout

    repo_root = pathlib.Path(arguments.repo_root).resolve()
    doxyfile = repo_root / DOXYFILE
    if not doxyfile.is_file():
        print('%s: the reference configuration does not exist' % DOXYFILE, file=out)
        return PROBLEMS_FOUND

    text = doxyfile.read_bytes().decode('utf-8')
    problems = check_configuration(text) + check_mainpage(text, repo_root)
    if problems:
        for problem in problems:
            print(problem, file=out)
        return PROBLEMS_FOUND

    # What a header's page shows needs no Doxygen to judge, so it is answered even when none is
    # installed, and it is a finding whatever the run below reports.
    unreachable = check_file_blocks(repo_root)
    for problem in unreachable:
        print(problem, file=out)

    executable = find_doxygen(arguments.doxygen, environment)
    if executable is None:
        print('doxygen was not found, so no public declaration was checked. Install it, or name it '
              'with --doxygen or DOXYGEN_BIN.', file=out)
        return PROBLEMS_FOUND if unreachable else TOOL_UNUSABLE

    if arguments.output_dir:
        output_dir = pathlib.Path(arguments.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
        code, output = run_doxygen(executable, compose_configuration(text, output_dir), repo_root,
                                   runner)
    else:
        with tempfile.TemporaryDirectory(prefix='an-engine-reference-') as temporary:
            code, output = run_doxygen(executable, compose_configuration(text, temporary),
                                       repo_root, runner)

    diagnostics = extract_diagnostics(output)
    for diagnostic in diagnostics:
        print(diagnostic, file=out)

    # A run that did not apply the configuration as written is answered first, whatever else it
    # reported. What it did find is still printed above and is still real work, but it cannot be
    # read as the whole answer, because the rules it dropped are exactly the ones nothing reported.
    about_the_run = [line for line in diagnostics if not is_source_diagnostic(line)]
    if about_the_run:
        print('doxygen did not run this check as %s configures it: %d diagnostic(s) above are '
              'about the run rather than about a declaration. A tag it ignores is a rule it is not '
              'applying, so this is neither a documented public surface nor a finding about one. '
              'Install a doxygen that understands the configuration with '
              'build/scripts/linux/install_doxygen.sh.' % (DOXYFILE, len(about_the_run)), file=out)
        return TOOL_UNUSABLE

    if diagnostics or unreachable:
        print('%d undocumented or malformed public declaration(s) and %d header(s) whose module '
              'documentation does not reach the reference in %s'
              % (len(diagnostics), len(unreachable), ' and '.join(PUBLIC_INCLUDE_DIRS)), file=out)
        return PROBLEMS_FOUND
    if code != 0:
        print('doxygen exited %d without reporting a diagnostic, so nothing can be concluded from '
              'this run' % code, file=out)
        return TOOL_UNUSABLE

    print('every public declaration in %s is documented' % ' and '.join(PUBLIC_INCLUDE_DIRS), file=out)
    return CLEAN


if __name__ == '__main__':
    sys.exit(main())

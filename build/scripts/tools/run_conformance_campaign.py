# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Run the AfterNightEngine functional conformance campaign, and hold it to its registry.

Conformance is the claim that every direct suite of this engine passes, and it is easy to appear to
have made. A suite that stopped being generated prints nothing at all, and a list of suites kept in
prose cannot notice; a suite that ran nothing exits zero; and a suite that skipped the case a reader
relies on passes all the same.

So this campaign:

1. reads `conformance_matrix.txt`, which registers every direct suite and the cases that must run and
   pass in it;
2. checks that registry against the repository - the generator defines every registered suite and
   the registry names every suite the generator defines unconditionally, and each nominated case
   exists; and
3. runs every registered suite and reads its own report rather than its exit code, because a suite
   that ran nothing exits zero.

Usage:
    python build/scripts/tools/run_conformance_campaign.py --check
    python build/scripts/tools/run_conformance_campaign.py --output <dir> [--config release|debug]

`--check` reads the registry and the repository and runs nothing, which is what the common check
list wants.

Tests: build/scripts/tools/run_conformance_campaign_test.py
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
REGISTRY = Path(__file__).resolve().parent / 'conformance_matrix.txt'

PREMAKE = Path('build') / 'premake' / 'afternight-test-pm5.lua'
TEST_SOURCES = Path('src') / 'AfterNightTests'

PRESENCES = ('always', 'optional')
# How a suite reports. Nearly all of them are Google Test programs that print a summary; the
# published-surface smoke client is a program that links the engine from outside and prints what it
# read, and judging it by a summary it never prints would report that it did not run.
HARNESSES = ('gtest', 'program')

# Where a generated project for one target would be. An optional suite is evidence only when the
# tree generates it: a binary a build of an earlier generated tree left in the deployed directory
# runs, passes and is three days older than the engine beside it.
GENERATED_PROJECTS = ((Path('build') / 'vs2026-msvc', '%s.vcxproj'),
                      (Path('build') / 'vs2026-clang', '%s.vcxproj'),
                      (Path('build') / 'make-linux', '%s.make'))

# `addEngineTestProject { name = "Test_EngineX"` and the one suite declared as a bare project. Read
# lexically rather than by running the generator: what this has to notice is a suite that stopped
# being generated, and a generator that no longer emits it would report that everything it emits is
# registered. The benchmark layer's suite shares the engine suites' prefix and is not an engine suite,
# so a bare project named for the benchmark is not one.
FACTORY_PATTERN = re.compile(r'addEngineTestProject\s*{\s*name\s*=\s*"(Test_Engine\w+)"')
PROJECT_PATTERN = re.compile(r'^\s*project\s+"(Test_Engine(?!Bench)\w+)"')
BLOCK_OPENERS = re.compile(r'^\s*(?:(?:local\s+)?function\b|if\b.*\bthen\s*$|for\b.*\bdo\s*$|while\b.*\bdo\s*$)')
BLOCK_CLOSER = re.compile(r'^\s*end\b')
CONDITION_OPENER = re.compile(r'^\s*if\b.*\bthen\s*$')

GTEST_CASE = re.compile(r'TEST(?:_F|_P)?\(\s*(\w+)\s*,\s*(\w+)\s*\)')

RAN_LINE = re.compile(r'^\[=+\] (\d+) tests? from \d+ test suites? ran\.', re.M)
PASSED_LINE = re.compile(r'^\[\s+PASSED\s+\] (\d+) tests?', re.M)
SKIPPED_LINE = re.compile(r'^\[\s+SKIPPED\s+\] (\d+) tests?', re.M)
FAILED_LINE = re.compile(r'^\[\s+FAILED\s+\] (\d+) tests?', re.M)
OK_LINE = re.compile(r'^\[\s+OK\s+\] ([\w./]+)', re.M)
# A skipped case, as opposed to the count line above it, which starts with a digit. A skip is
# not a failure and not a pass, and a campaign that reports only the number leaves the reader
# to guess whether the host or the build is what the suite could not reach.
SKIPPED_CASE = re.compile(r'^\[\s+SKIPPED\s+\] ([A-Za-z][\w./]*)', re.M)


class RegistryError(Exception):
    """The registry does not describe a campaign that can be run."""


class Suite:
    """One direct engine suite, and the cases that must run and pass in it."""

    def __init__(self, name, presence, harness='gtest'):
        self.name = name
        self.presence = presence
        self.harness = harness
        self.cases = []

    def __repr__(self):
        return 'Suite(%s, %s, %s, %d case(s))' % (self.name, self.presence, self.harness, len(self.cases))


class Registry:
    """The conformance surface: the suites and their nominated cases."""

    def __init__(self):
        self.suites = []

    def suite(self, name):
        for suite in self.suites:
            if suite.name == name:
                return suite
        return None


def parse_registry(lines, source):
    """Read the registry's record types.

    @param lines the registry's lines, without line endings.
    @param source what to name in an error.
    @return the registry.
    """
    registry = Registry()
    for number, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        fields = line.split()
        kind = fields[0]
        where = '%s:%d' % (source, number)
        if kind == 'suite':
            if len(fields) not in (3, 4):
                raise RegistryError('%s: suite needs a name and a presence' % where)
            name, presence = fields[1], fields[2]
            harness = fields[3] if len(fields) == 4 else 'gtest'
            if presence not in PRESENCES:
                raise RegistryError('%s: presence must be one of %s' % (where, ', '.join(PRESENCES)))
            if harness not in HARNESSES:
                raise RegistryError('%s: harness must be one of %s' % (where, ', '.join(HARNESSES)))
            if registry.suite(name) is not None:
                raise RegistryError('%s: %s is registered twice' % (where, name))
            registry.suites.append(Suite(name, presence, harness))
        elif kind == 'case':
            if len(fields) != 3:
                raise RegistryError('%s: case needs a suite and a test name' % where)
            suite = registry.suite(fields[1])
            if suite is None:
                raise RegistryError('%s: %s is not a registered suite' % (where, fields[1]))
            if suite.harness != 'gtest':
                raise RegistryError('%s: %s prints no case names to nominate one from' % (where, suite.name))
            suite.cases.append(fields[2])
        else:
            raise RegistryError('%s: %s is not a record type' % (where, kind))
    return registry


def read_registry(path=None):
    """Read the repository's registry.

    @param path the registry, or None for the one beside this script.
    @return the registry.
    """
    registry_path = Path(path) if path is not None else REGISTRY
    text = registry_path.read_text(encoding='utf-8')
    return parse_registry(text.splitlines(), str(registry_path))


def generated_suites(premake_text):
    """Which suites the generator defines, and which of them it defines only behind a condition.

    @param premake_text the test generator's source.
    @return a pair of sets: the unconditional suites and the conditional ones.
    """
    unconditional = set()
    conditional = set()
    stack = []
    for line in premake_text.splitlines():
        names = FACTORY_PATTERN.findall(line)
        project = PROJECT_PATTERN.match(line)
        if project is not None:
            names = names + [project.group(1)]
        for name in names:
            (conditional if 'if' in stack else unconditional).add(name)
        if BLOCK_OPENERS.match(line):
            stack.append('if' if CONDITION_OPENER.match(line) else 'block')
        elif BLOCK_CLOSER.match(line) and stack:
            stack.pop()
    return unconditional, conditional


def suite_cases(repo_root, suite):
    """Every case a suite's sources define.

    @param repo_root the repository root.
    @param suite the suite's name.
    @return the `Fixture.Case` names.
    """
    directory = Path(repo_root) / TEST_SOURCES / suite
    cases = set()
    if not directory.is_dir():
        return cases
    for path in sorted(directory.rglob('*')):
        if path.suffix.lower() not in ('.cpp', '.h', '.inl'):
            continue
        for fixture, case in GTEST_CASE.findall(path.read_text(encoding='utf-8', errors='replace')):
            cases.add('%s.%s' % (fixture, case))
    return cases


def check(repo_root, registry, stream=sys.stdout):
    """Whether the registry and the repository agree.

    @param repo_root the repository root.
    @param registry the parsed registry.
    @param stream where to report.
    @return zero when they do.
    """
    root = Path(repo_root)
    problems = []

    premake = root / PREMAKE
    if not premake.is_file():
        problems.append('the test generator is missing: %s' % PREMAKE)
        unconditional, conditional = set(), set()
    else:
        unconditional, conditional = generated_suites(premake.read_text(encoding='utf-8'))

    registered = {suite.name for suite in registry.suites}
    for suite in registry.suites:
        if suite.name not in unconditional and suite.name not in conditional:
            problems.append('%s is registered and the generator does not define it' % suite.name)
        elif suite.presence == 'always' and suite.name in conditional:
            problems.append('%s is registered as always present and is generated behind a condition'
                            % suite.name)
        elif suite.presence == 'optional' and suite.name in unconditional:
            problems.append('%s is registered as optional and is generated unconditionally' % suite.name)
    for name in sorted(unconditional - registered):
        problems.append('%s is generated unconditionally and the registry does not name it' % name)

    for suite in registry.suites:
        defined = suite_cases(root, suite.name)
        for case in suite.cases:
            if case not in defined:
                problems.append('%s nominates %s and its sources do not define it' % (suite.name, case))

    for problem in problems:
        print(problem, file=stream)
    print('%d suite(s), %d nominated case(s)'
          % (len(registry.suites), sum(len(suite.cases) for suite in registry.suites)), file=stream)
    return 1 if problems else 0


class GtestReport:
    """What one suite said about itself."""

    def __init__(self, ran, passed, skipped, failed, cases, skippedCases=()):
        self.ran = ran
        self.passed = passed
        self.skipped = skipped
        self.failed = failed
        self.cases = cases
        self.skippedCases = list(skippedCases)


def parse_gtest(output):
    """Read a suite's own report rather than its exit code.

    @param output the suite's standard output.
    @return the report, or None when the suite printed no summary at all.
    """
    ran = RAN_LINE.search(output)
    if ran is None:
        return None
    passed = PASSED_LINE.search(output)
    skipped = SKIPPED_LINE.search(output)
    failed = FAILED_LINE.search(output)
    return GtestReport(int(ran.group(1)),
                       int(passed.group(1)) if passed else 0,
                       int(skipped.group(1)) if skipped else 0,
                       int(failed.group(1)) if failed else 0,
                       set(OK_LINE.findall(output)), sorted(set(SKIPPED_CASE.findall(output))))


def judge_suite(suite, report, code, output=''):
    """Whether one suite did what the registry says it does.

    @param suite the registered suite.
    @param report what it printed, or None.
    @param code its exit code.
    @param output the whole output, for a suite that prints no summary.
    @return one message per problem.
    """
    if suite.harness == 'program':
        problems = []
        if code != 0:
            problems.append('%s exited %d' % (suite.name, code))
        if not output.strip():
            problems.append('%s printed nothing; it did not run' % suite.name)
        return problems
    if report is None:
        return ['%s printed no summary; it did not run' % suite.name]
    problems = []
    if report.ran == 0:
        problems.append('%s ran no test' % suite.name)
    if report.failed:
        problems.append('%s reported %d failing test(s)' % (suite.name, report.failed))
    if code != 0:
        problems.append('%s exited %d' % (suite.name, code))
    for case in suite.cases:
        if case not in report.cases:
            problems.append('%s did not run and pass %s' % (suite.name, case))
    return problems


def binary_directory(repo_root, config):
    """Where the built targets are.

    @param repo_root the repository root.
    @param config `release` or `debug`.
    @return the directory, whether or not it exists.
    """
    toolchain = 'msvc' if os.name == 'nt' else 'clang'
    return Path(repo_root) / 'bin' / toolchain / config


def binary_path(directory, name, config):
    """One built target.

    @param directory where the built targets are.
    @param name the target's name.
    @param config `release` or `debug`, which decides the debug suffix.
    @return the path, whether or not it exists.
    """
    suffix = '_d' if config == 'debug' else ''
    return Path(directory) / (name + suffix + ('.exe' if os.name == 'nt' else ''))


def is_generated(repo_root, suite):
    """Whether some generated tree defines a project for one suite.

    @param repo_root the repository root.
    @param suite the suite's name.
    @return True when a generated project for it exists.
    """
    for tree, pattern in GENERATED_PROJECTS:
        directory = Path(repo_root) / tree
        if not directory.is_dir():
            continue
        if any(path.is_file() for path in directory.rglob(pattern % suite)):
            return True
    return False


def run_suites(registry, repo_root, directory, config, output_directory, timeout, stream=sys.stdout):
    """Run every registered suite and judge it.

    @param registry the parsed registry.
    @param repo_root the repository root, whose generated trees say which suites exist.
    @param directory where the built suites are.
    @param config `release` or `debug`.
    @param output_directory where to keep each suite's output.
    @param timeout the per-suite wall-clock bound in seconds.
    @param stream where to report.
    @return a pair: one message per problem, and one summary record per suite.
    """
    problems = []
    summaries = []
    print('%-28s %7s %7s %7s %7s %9s' % ('suite', 'ran', 'passed', 'skipped', 'failed', 'seconds'),
          file=stream)
    for suite in registry.suites:
        executable = binary_path(directory, suite.name, config)
        generated = is_generated(repo_root, suite.name)
        if not generated:
            if suite.presence == 'optional':
                # A binary is not evidence that the tree contains the suite. This one ran an
                # earlier generated tree's leftover, three days older than the engine beside it, and
                # passed.
                state = 'not generated' if not executable.is_file() else 'not generated (stale binary ignored)'
                summaries.append({'suite': suite.name, 'state': state})
                print('%-28s %s' % (suite.name, state), file=stream)
                continue
            problems.append('%s is registered as always present and no generated tree defines it'
                            % suite.name)
            summaries.append({'suite': suite.name, 'state': 'not generated'})
            continue
        if not executable.is_file():
            problems.append('%s is registered and is not built: %s' % (suite.name, executable))
            summaries.append({'suite': suite.name, 'state': 'not built'})
            continue

        storage = Path(tempfile.mkdtemp(prefix='conformance-%s-' % suite.name))
        environment = dict(os.environ)
        environment['AFTERNIGHT_TEST_STORAGE_ROOT'] = str(storage)
        started = time.time()
        try:
            completed = subprocess.run([str(executable)], cwd=str(executable.parent), env=environment,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                       errors='replace', timeout=timeout, check=False)
            output, code = completed.stdout, completed.returncode
        except subprocess.TimeoutExpired as expired:
            output = (expired.output or '') if isinstance(expired.output, str) else ''
            code = -1
            problems.append('%s did not finish inside %d seconds' % (suite.name, timeout))
        finally:
            shutil.rmtree(storage, ignore_errors=True)
        seconds = time.time() - started

        (Path(output_directory) / ('%s.txt' % suite.name)).write_text(output, encoding='utf-8')
        report = parse_gtest(output) if suite.harness == 'gtest' else None
        problems.extend(judge_suite(suite, report, code, output))
        if suite.harness == 'program':
            summaries.append({'suite': suite.name, 'state': 'ran', 'harness': 'program',
                              'seconds': round(seconds, 1)})
            print('%-28s %7s %41.1f' % (suite.name, 'program', seconds), file=stream)
            continue
        if report is None:
            summaries.append({'suite': suite.name, 'state': 'no summary', 'seconds': round(seconds, 1)})
            print('%-28s %s' % (suite.name, 'no summary'), file=stream)
            continue
        summaries.append({'suite': suite.name, 'state': 'ran', 'ran': report.ran, 'passed': report.passed,
                          'skipped': report.skipped, 'failed': report.failed, 'seconds': round(seconds, 1),
                          'skippedCases': report.skippedCases})
        print('%-28s %7d %7d %7d %7d %9.1f'
              % (suite.name, report.ran, report.passed, report.skipped, report.failed, seconds),
              file=stream)
    return problems, summaries


def main(argv=None, stream=sys.stdout):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--registry', default=str(REGISTRY))
    parser.add_argument('--config', default='release', choices=('release', 'debug'))
    parser.add_argument('--check', action='store_true',
                        help='read the registry and the repository and run nothing')
    parser.add_argument('--output', default=None, help='where the evidence is written')
    parser.add_argument('--timeout', type=int, default=7200,
                        help='per-process wall-clock bound in seconds')
    args = parser.parse_args(argv)

    try:
        registry = read_registry(args.registry)
    except (RegistryError, OSError) as failure:
        print(failure, file=stream)
        return 1

    if args.check:
        return check(Path(args.repo_root), registry, stream=stream)

    if not args.output:
        print('--output is required for a run', file=stream)
        return 1
    output_directory = Path(args.output)
    output_directory.mkdir(parents=True, exist_ok=True)

    problems = []
    if check(Path(args.repo_root), registry, stream=stream) != 0:
        problems.append('the registry and the repository disagree')
    directory = binary_directory(args.repo_root, args.config)
    summary = {'config': args.config, 'binaries': str(directory)}

    suite_problems, suites = run_suites(registry, Path(args.repo_root), directory, args.config,
                                        output_directory, args.timeout, stream=stream)
    problems.extend(suite_problems)
    summary['suites'] = suites

    summary['problems'] = problems
    (output_directory / 'CONFORMANCE.json').write_text(json.dumps(summary, indent=1), encoding='utf-8')
    for problem in problems:
        print(problem, file=stream)
    print('%d problem(s)' % len(problems), file=stream)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())

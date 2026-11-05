# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Run the fault and cancellation campaign - the engine's and the consumer's - and hold it to its registry.

The campaign is one command rather than thirteen test cases somebody remembers to look at, because
the thing that has to be checked is not only that the suite passed. A fault scenario passes by its
subject behaving well on a failure path, so a scenario whose injection never fired, or whose census
found one injection point where it used to find forty, passes too - and prints a line that looks
exactly like the line a scenario that swept everything prints.

What this does:

1. reads `fault_points.txt`, which is the registry of scenarios and the points each must find;
2. reads the scenario declarations out of every campaign suite - `Test_EngineFaults`, the engine's, and
   `Test_ConsumerFaults`, which sweeps the product's own publication and read paths with the same
   harness - and refuses a registry that names a scenario no suite defines, a scenario a suite defines
   and the registry does not run, or a kind, a test name or a suite the two disagree about;
3. runs each suite the registry names under a wall-clock bound, because a deadlock is one of the
   things this campaign is looking for and a deadlocked suite never reports anything at all; and
4. reads the report line every scenario prints, and fails when one is missing, when a scenario found
   fewer points than it is registered for, or when not one of its injections fired.

Usage:
    python build/scripts/tools/run_fault_campaign.py [--config release]
    python build/scripts/tools/run_fault_campaign.py --check
    python build/scripts/tools/run_fault_campaign.py --scenario alloc.stream

`--check` reads the registry and the sources and runs nothing, which is what the common check list
wants. `--scenario` runs one scenario, which is what a person repairing one wants; a run of a subset
says so in its report and is not the campaign.

Tests: build/scripts/tools/run_fault_campaign_test.py
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
REGISTRY = Path(__file__).resolve().parent / 'fault_points.txt'
SUITE = 'Test_EngineFaults'
SUITE_SOURCES = Path('src') / 'AfterNightTests' / SUITE

# Every suite the campaign runs, and where its scenarios are declared. A scenario names its suite in the registry with a
# `suite=` field; one that names none is the engine's.
SUITES = {
    SUITE: SUITE_SOURCES,
    'Test_ConsumerFaults': Path('src') / 'AfterNightTests' / 'Test_ConsumerFaults',
}

# The kinds a scenario may inject, spelled as the registry spells them and as the suite's enumerator
# spells them. Both spellings are here because the check is that the two agree.
KINDS = {
    'Allocation': 'allocation',
    'Cancellation': 'cancellation',
    'InputOutput': 'io',
    'External': 'external',
}

# `TEST(FaultCampaign, TheName)` and, inside it, `Sweep sweep("label", FaultKind::Kind)`. Parsed
# lexically on purpose: the alternative is running the suite to find out what it defines, and then a
# scenario that stopped being compiled in would be invisible to the very check meant to catch it.
TEST_PATTERN = re.compile(r'TEST\(\s*FaultCampaign\s*,\s*(\w+)\s*\)')
SWEEP_PATTERN = re.compile(r'Sweep\s+\w+\s*\(\s*"([^"]+)"\s*,\s*(?:an::faults::)?FaultKind::(\w+)\s*\)')
REPORT_PATTERN = re.compile(r'an-fault-scenario\s+label=(\S+)\s+kind=(\S+)\s+points=(\d+)\s+runs=(\d+)\s+'
                            r'reached=(\d+)\s+refused=(\d+)\s+absorbed=(\d+)\s+seconds=([\d.]+)')


class RegistryError(Exception):
    """The registry does not describe a campaign that can be run."""


class Scenario:
    """One registered scenario: what it injects, where, and how much it must reach."""

    def __init__(self, label, kind, test, minimum, suite=SUITE):
        self.label = label
        self.kind = kind
        self.test = test
        self.minimum = minimum
        self.suite = suite

    def __repr__(self):
        return 'Scenario(%s, %s, %s, %d, %s)' % (self.label, self.kind, self.test, self.minimum, self.suite)


class Report:
    """What one scenario printed about itself."""

    def __init__(self, label, kind, points, runs, reached, refused, absorbed, seconds):
        self.label = label
        self.kind = kind
        self.points = points
        self.runs = runs
        self.reached = reached
        self.refused = refused
        self.absorbed = absorbed
        self.seconds = seconds


def read_registry(path=REGISTRY):
    """The scenarios the registry states.

    @param path the registry file.
    @return the scenarios, in the order they are listed.
    """
    scenarios = []
    seen = set()
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
        fields = stripped.split()
        if fields[0] != 'scenario':
            raise RegistryError('line %d: %s is not a record this registry has' % (number, fields[0]))
        if len(fields) not in (5, 6):
            raise RegistryError('line %d: a scenario is a label, a kind, a test name, a minimum and, optionally, '
                                'its suite' % number)
        label, kind, test, minimum = fields[1], fields[2], fields[3], fields[4]
        suite = SUITE
        if len(fields) == 6:
            key, separator, value = fields[5].partition('=')
            if key != 'suite' or not separator:
                raise RegistryError('line %d: %s is not a suite' % (number, fields[5]))
            if value not in SUITES:
                raise RegistryError('line %d: %s is not a suite of the campaign: %s'
                                    % (number, value, ', '.join(sorted(SUITES))))
            suite = value
        if label in seen:
            raise RegistryError('line %d: %s is listed twice' % (number, label))
        seen.add(label)
        if kind not in KINDS.values():
            raise RegistryError('line %d: %s is not one of %s' % (number, kind, ', '.join(sorted(KINDS.values()))))
        if not test.startswith('FaultCampaign.'):
            raise RegistryError('line %d: %s is not a case of the fault campaign' % (number, test))
        key, separator, value = minimum.partition('=')
        if key != 'minimum' or not separator:
            raise RegistryError('line %d: %s is not a minimum' % (number, minimum))
        try:
            count = int(value)
        except ValueError:
            raise RegistryError('line %d: %s is not a whole number' % (number, value)) from None
        if count <= 0:
            raise RegistryError('line %d: %s must find at least one injection point' % (number, label))
        scenarios.append(Scenario(label, kind, test, count, suite))
    if not scenarios:
        raise RegistryError('the registry names no scenario, so the campaign would pass by doing nothing')
    return scenarios


def declared_suites(repo_root=REPO_ROOT):
    """Which suite declares each scenario, by label.

    @param repo_root the repository root.
    @return the labels mapped to the suite whose sources declare them.
    """
    owners = {}
    for suite, sources in SUITES.items():
        directory = Path(repo_root) / sources
        if not directory.is_dir():
            continue
        for source in sorted(directory.glob('*.cpp')):
            for match in SWEEP_PATTERN.finditer(source.read_text(encoding='utf-8')):
                owners[match.group(1)] = suite
    return owners


def declared_scenarios(repo_root=REPO_ROOT):
    """Every scenario the campaign suites' sources declare, by label.

    @param repo_root the repository root.
    @return the labels mapped to the kind and test name they were declared with, and whether any
            suite's sources were found at all.
    """
    found = {}
    any_found = False
    for sources in SUITES.values():
        directory = Path(repo_root) / sources
        if not directory.is_dir():
            continue
        any_found = True
        for source in sorted(directory.glob('*.cpp')):
            text = source.read_text(encoding='utf-8')
            tests = [(match.start(), match.group(1)) for match in TEST_PATTERN.finditer(text)]
            for match in SWEEP_PATTERN.finditer(text):
                enclosing = [name for start, name in tests if start < match.start()]
                if not enclosing:
                    continue
                kind = KINDS.get(match.group(2))
                if kind is None:
                    continue
                found[match.group(1)] = ('FaultCampaign.' + enclosing[-1], kind)
    return found, any_found


def check(repo_root=REPO_ROOT, path=REGISTRY, stream=sys.stdout):
    """Whether the registry and the suite's sources describe the same campaign.

    @param repo_root the repository root.
    @param path the registry file.
    @param stream where to report.
    @return zero when they agree, one otherwise.
    """
    try:
        scenarios = read_registry(path)
    except (OSError, RegistryError) as error:
        print('the fault registry is not usable: %s' % error, file=stream)
        return 1

    declared, found_sources = declared_scenarios(repo_root)
    owners = declared_suites(repo_root)
    problems = []
    if not found_sources:
        print('the fault suite\'s sources were not found; the registry alone was checked', file=stream)
    else:
        registered = {scenario.label for scenario in scenarios}
        for label in sorted(registered - set(declared)):
            problems.append('%s is registered but no scenario in the suite declares it' % label)
        for label in sorted(set(declared) - registered):
            problems.append('%s is declared by the suite but the registry does not run it' % label)
        for scenario in scenarios:
            declaration = declared.get(scenario.label)
            if declaration is None:
                continue
            test, kind = declaration
            if test != scenario.test:
                problems.append('%s is registered as %s and declared in %s' % (scenario.label, scenario.test, test))
            if kind != scenario.kind:
                problems.append('%s is registered as %s and declared as %s' % (scenario.label, scenario.kind, kind))
            owner = owners.get(scenario.label)
            if owner is not None and owner != scenario.suite:
                problems.append('%s is registered in %s and declared in %s' % (scenario.label, scenario.suite, owner))

    for problem in problems:
        print(problem, file=stream)
    for scenario in scenarios:
        print('%-24s %-12s %-6d %-20s %s' % (scenario.label, scenario.kind, scenario.minimum, scenario.suite,
                                             scenario.test), file=stream)
    return 1 if problems else 0


def binary_directory(repo_root, config):
    """Where the built suite is.

    @param repo_root the repository root.
    @param config `release` or `debug`.
    @return the directory, whether or not it exists.
    """
    toolchain = 'msvc' if os.name == 'nt' else 'clang'
    return Path(repo_root) / 'bin' / toolchain / config


def binary_path(directory, config, suite=SUITE):
    """A campaign suite's executable.

    @param directory where the built targets are.
    @param config `release` or `debug`, which decides the debug suffix.
    @param suite which of the campaign's suites.
    @return the path, whether or not it exists.
    """
    suffix = '_d' if config == 'debug' else ''
    return directory / (suite + suffix + ('.exe' if os.name == 'nt' else ''))


def parse_reports(output):
    """Every report line the suite printed.

    @param output the suite's standard output.
    @return the reports, by label.
    """
    reports = {}
    for match in REPORT_PATTERN.finditer(output):
        reports[match.group(1)] = Report(match.group(1), match.group(2), int(match.group(3)), int(match.group(4)),
                                         int(match.group(5)), int(match.group(6)), int(match.group(7)),
                                         float(match.group(8)))
    return reports


def judge(scenarios, reports, code, stream=sys.stdout):
    """Whether the campaign did what the registry says it does.

    @param scenarios what was registered.
    @param reports what the suite printed.
    @param code the suite's exit code.
    @param stream where to report.
    @return zero when every scenario reached its registered minimum and the suite passed.
    """
    problems = []
    print('%-18s %-12s %8s %8s %8s %8s %8s' % ('scenario', 'kind', 'points', 'runs', 'reached', 'refused',
                                               'absorbed'), file=stream)
    for scenario in scenarios:
        report = reports.get(scenario.label)
        if report is None:
            problems.append('%s reported nothing; it did not run' % scenario.label)
            continue
        print('%-18s %-12s %8d %8d %8d %8d %8d' % (report.label, report.kind, report.points, report.runs,
                                                   report.reached, report.refused, report.absorbed), file=stream)
        if report.kind != scenario.kind:
            problems.append('%s ran as %s and is registered as %s' % (scenario.label, report.kind, scenario.kind))
        if report.points < scenario.minimum:
            problems.append('%s found %d injection point(s) and is registered for at least %d'
                            % (scenario.label, report.points, scenario.minimum))
        if report.reached == 0:
            problems.append('%s swept %d point(s) and not one injection fired' % (scenario.label, report.points))
    for label in sorted(set(reports) - {scenario.label for scenario in scenarios}):
        problems.append('%s reported a sweep and the registry does not name it' % label)
    if code != 0:
        problems.append('the suite reported a failure: exit %d' % code)

    for problem in problems:
        print(problem, file=stream)
    return 1 if problems else 0


def main(argv=None, stream=sys.stdout):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--registry', default=str(REGISTRY))
    parser.add_argument('--config', default='release', choices=('release', 'debug'))
    parser.add_argument('--scenario', action='append', default=[],
                        help='run only this scenario; may be given more than once')
    parser.add_argument('--storage-root', default=None)
    parser.add_argument('--timeout', type=int, default=900,
                        help='the wall-clock bound on the whole suite; a campaign that outlasts it has found '
                             'the deadlock it was looking for')
    parser.add_argument('--check', action='store_true',
                        help='read the registry and the suite\'s sources, and run nothing')
    args = parser.parse_args(argv)

    if args.check:
        return check(Path(args.repo_root), Path(args.registry), stream)

    try:
        scenarios = read_registry(Path(args.registry))
    except (OSError, RegistryError) as error:
        print('the fault registry is not usable: %s' % error, file=stream)
        return 2

    if args.scenario:
        wanted = set(args.scenario)
        unknown = sorted(wanted - {scenario.label for scenario in scenarios})
        if unknown:
            print('not registered: %s' % ', '.join(unknown), file=stream)
            return 2
        scenarios = [scenario for scenario in scenarios if scenario.label in wanted]
        print('this is a run of %d of the campaign\'s scenarios and is not the campaign' % len(scenarios), file=stream)

    # The suites the scenarios name, in the order the registry first names each.
    suites = []
    for scenario in scenarios:
        if scenario.suite not in suites:
            suites.append(scenario.suite)
    directory = binary_directory(Path(args.repo_root), args.config)
    executables = {suite: binary_path(directory, args.config, suite) for suite in suites}
    for suite, executable in executables.items():
        if not executable.is_file():
            print('%s is not built: %s' % (suite, executable), file=stream)
            return 2

    storage_root = (Path(args.storage_root) if args.storage_root
                    else Path(args.repo_root) / 'obj' / 'fault-storage')
    # A previous campaign's storage is removed rather than reused: a scenario that found a file an
    # earlier run left behind would be checking the wrong directory for residue.
    shutil.rmtree(storage_root, ignore_errors=True)
    storage_root.mkdir(parents=True, exist_ok=True)

    print('campaign: %d scenario(s) in %d suite(s), %s' % (len(scenarios), len(suites), args.config), file=stream)
    started = time.monotonic()
    outputs = []
    worst = 0
    for suite in suites:
        executable = executables[suite]
        command = [str(executable)]
        if args.scenario:
            command.append('--gtest_filter=' + ':'.join(scenario.test for scenario in scenarios
                                                        if scenario.suite == suite))
        environment = dict(os.environ)
        # Each suite its own storage, so one suite's residue is never another's to find.
        suite_storage = storage_root / suite
        suite_storage.mkdir(parents=True, exist_ok=True)
        environment['AFTERNIGHT_TEST_STORAGE_ROOT'] = str(suite_storage)
        print('running: %s' % ' '.join(command), file=stream)
        suite_started = time.monotonic()
        try:
            completed = subprocess.run(command, cwd=str(executable.parent), env=environment, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, check=False, timeout=args.timeout)
        except subprocess.TimeoutExpired as expired:
            elapsed = time.monotonic() - suite_started
            partial = expired.output.decode('utf-8', errors='replace') if expired.output else ''
            print(partial.rstrip(), file=stream)
            print('%s did not come back within %d s, which is the deadlock the campaign is looking for (%.1f s)'
                  % (suite, args.timeout, elapsed), file=stream)
            return 1
        output = completed.stdout.decode('utf-8', errors='replace')
        print(output.rstrip(), file=stream)
        outputs.append(output)
        if completed.returncode != 0 and worst == 0:
            worst = completed.returncode
    elapsed = time.monotonic() - started

    code = judge(scenarios, parse_reports('\n'.join(outputs)), worst, stream)
    print('%d scenario(s) in %.1f s' % (len(scenarios), elapsed), file=stream)
    if code == 0:
        print('every registered scenario swept its points and every injected run was answered', file=stream)
    return code


if __name__ == '__main__':
    raise SystemExit(main())

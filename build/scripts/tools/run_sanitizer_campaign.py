# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Run the sanitizer campaign - the engine's suites and the consumer's - over the matrix its registry states.

The campaign is one command rather than forty, because the thing that has to be reproducible is the
whole run: which configuration, which instrumentation, which rows, which options, and what every one
of them reported. A run of eight suites under the address sanitizer that said "the sanitizers pass"
would be telling the truth about the eight.

What this does:

1. reads `sanitizer_matrix.txt`, which is the registry of configurations, options and rows;
2. refuses a matrix that leaves a generated engine suite, or an active consumer suite the target
   registry names, out of a configuration, that skips one without a reason, that has no focused
   pipeline row, or whose options turn a check off without saying why;
3. refuses to run a row whose executable - or whose engine library beside it, or for a consumer
   suite any consumer library it links - does not carry the instrumentation the configuration names.
   **This is the point of the runner.** A sanitizer run over an uninstrumented binary prints exactly
   what a clean instrumented run prints, and the engine and the consumer libraries are separate
   shared libraries, so an instrumented test executable in front of an ordinary library would pass
   while covering none of the code the campaign exists to cover;
4. runs each row with the configuration's options, its own test-owned storage, and a wall-clock bound
   a deadlock cannot outlast; and
5. reads every row's output for a sanitizer report rather than trusting an exit code, because
   UndefinedBehaviorSanitizer prints and continues unless told otherwise and a test binary can
   swallow a nonzero exit for its own reasons. A row that ran no test at all is a failure too.

Usage:
    python build/scripts/tools/run_sanitizer_campaign.py --configuration address
    python build/scripts/tools/run_sanitizer_campaign.py --check
    python build/scripts/tools/run_sanitizer_campaign.py --configuration address --label suite.core

`--check` reads the matrix and the generated projects and runs nothing, which is what a build-graph
check wants. Running requires a tree generated with the matching `AFTERNIGHT_SANITIZE` and built; the
procedure is in docs/dev/LOCAL_VALIDATION.md.

Tests: build/scripts/tools/run_sanitizer_campaign_test.py
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
REGISTRY = Path(__file__).resolve().parent / 'sanitizer_matrix.txt'

# The consumer targets' registry, which says which suites are the product's and what each links.
TARGET_REGISTRY = Path('build') / 'scripts' / 'tools' / 'consumer_targets.txt'

# The consumer libraries a suite may link, which a row of that suite requires instrumented beside it.
CONSUMER_LIBRARIES = ('AfterNightPlatform', 'AfterNightConsumerRuntime', 'AfterNightExtensionProtocol',
                      'AfterNightControls', 'AfterNightProcesses')

# Where a generator writes the test workspace, and how a project file is named in each. Both front
# ends are read, because the matrix has to agree with whichever tree this host generated.
GENERATED_TEST_TREES = (
    (Path('build') / 'vs2026-msvc' / 'tests', '.vcxproj'),
    (Path('build') / 'vs2026-clang' / 'tests', '.vcxproj'),
    (Path('build') / 'make-linux' / 'tests', '.make'),
)

BUILDS = ('address', 'thread')
HOSTS = ('windows', 'linux')

# An option that turns a check off, or lets through something the sanitizer would otherwise stop on.
# Each one present in a configuration's options must carry a `weakened` line saying why. The list is
# closed on purpose: a token nobody thought about cannot be added to the options and go unexplained,
# because the checker only accepts the tokens it knows and the matrix only accepts reasons for those.
WEAKENING_TOKENS = (
    'detect_leaks=0',
    'halt_on_error=0',
    'abort_on_error=0',
    'exitcode=0',
    'detect_odr_violation=0',
    'detect_stack_use_after_return=0',
    'detect_container_overflow=0',
    'allocator_may_return_null=1',
    'suppressions=',
)

# What a sanitizer report looks like in a row's output. Exit codes are not enough: the
# undefined-behaviour sanitizer prints and continues unless every option says otherwise, and a test
# binary is free to return zero for reasons of its own.
FINDING_MARKERS = (
    'ERROR: AddressSanitizer',
    'ERROR: LeakSanitizer',
    'ERROR: ThreadSanitizer',
    'WARNING: ThreadSanitizer',
    'ERROR: UndefinedBehaviorSanitizer',
    'SUMMARY: UndefinedBehaviorSanitizer',
    'LeakSanitizer: detected memory leaks',
    ': runtime error:',
)

# The names of this engine in a suppression pattern. A suppression naming one of these is a bug
# somebody decided to stop seeing, which is the one thing this campaign may not contain.
ENGINE_NAMES = ('an::', 'AfterNightEngine', 'afternightengine', 'libAfterNightEngine')

SUPPRESSION_KINDS = ('race', 'deadlock', 'mutex', 'thread', 'signal', 'called_from_lib',
                     'interceptor_name', 'interceptor_via_fun', 'interceptor_via_lib', 'leak')

TESTS_RAN = re.compile(r'\[==========\] (\d+) tests? from \d+ test (?:suite|case)s? ran\.')


class MatrixError(Exception):
    """The matrix does not describe a campaign that can be run."""


class Configuration:
    """One instrumented configuration: what it was built with, where it runs, and under what options."""

    def __init__(self, name, build, hosts, timeout):
        self.name = name
        self.build = build
        self.hosts = hosts
        self.timeout = timeout
        self.options = {}
        self.weakened = {}

    def runs_on(self, host):
        """Whether this configuration can be run on a host at all.

        @param host `windows` or `linux`.
        @return whether the matrix says it runs there.
        """
        return host in self.hosts

    def present_weakening_tokens(self):
        """The weakening tokens this configuration's options actually contain.

        The options are read as the sanitizer runtimes read them - colon-separated settings - rather
        than as text, because `print_suppressions=1` contains `suppressions=` as a substring and is
        the opposite of a weakening.

        @return the tokens, in the order the closed list states them.
        """
        settings = set()
        for value in self.options.values():
            settings.update(part.strip() for part in value.split(':') if part.strip())
        present = []
        for token in WEAKENING_TOKENS:
            if token.endswith('='):
                if any(setting.startswith(token) for setting in settings):
                    present.append(token)
            elif token in settings:
                present.append(token)
        return tuple(present)

    def __repr__(self):
        return 'Configuration(%s, %s, %s)' % (self.name, self.build, ','.join(self.hosts))


class Row:
    """One row of the matrix: a target, what of it runs, and how many times."""

    def __init__(self, configuration, label, target, kind, selection, repeat, shuffle):
        self.configuration = configuration
        self.label = label
        self.target = target
        self.kind = kind
        self.selection = selection
        self.repeat = repeat
        self.shuffle = shuffle

    @property
    def is_skip(self):
        return self.kind == 'skip'

    @property
    def is_pipeline(self):
        return self.label.startswith('pipeline.')

    @property
    def is_suite(self):
        return self.label.startswith('suite.')

    def __repr__(self):
        return 'Row(%s, %s, %s, %s)' % (self.configuration, self.label, self.target, self.kind)


def _parse_fields(rest):
    """Splits `key=value` fields from a record's tail.

    @param rest the remaining fields of a record.
    @return the fields as a dictionary.
    """
    fields = {}
    for field in rest:
        key, separator, value = field.partition('=')
        if not separator:
            raise MatrixError('%s is not a key=value field' % field)
        fields[key] = value
    return fields


def read_matrix(path=REGISTRY):
    """The configurations and rows the matrix states.

    @param path the matrix file.
    @return the configurations by name, and the rows in the order they are listed.
    """
    configurations = {}
    rows = []
    seen_labels = set()
    for number, line in enumerate(Path(path).read_text(encoding='utf-8').splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
        record, _, tail = stripped.partition(' ')
        tail = tail.strip()
        try:
            if record == 'configuration':
                name, _, rest = tail.partition(' ')
                fields = _parse_fields(rest.split())
                if name in configurations:
                    raise MatrixError('%s is declared twice' % name)
                if fields.get('build') not in BUILDS:
                    raise MatrixError('%s needs build=%s' % (name, '|'.join(BUILDS)))
                hosts = tuple(host for host in fields.get('hosts', '').split(',') if host)
                if not hosts or any(host not in HOSTS for host in hosts):
                    raise MatrixError('%s needs hosts= from %s' % (name, ','.join(HOSTS)))
                timeout = int(fields.get('timeout', '0'))
                if timeout <= 0:
                    raise MatrixError('%s needs a positive wall-clock bound; a row that hangs must '
                                      'end as a finding rather than run for ever' % name)
                configurations[name] = Configuration(name, fields['build'], hosts, timeout)
            elif record == 'option':
                name, _, rest = tail.partition(' ')
                if name not in configurations:
                    raise MatrixError('%s is not a declared configuration' % name)
                variable, separator, value = rest.strip().partition('=')
                if not separator or not variable:
                    raise MatrixError('%s is not VARIABLE=value' % rest.strip())
                configurations[name].options[variable] = value
            elif record == 'weakened':
                name, _, rest = tail.partition(' ')
                if name not in configurations:
                    raise MatrixError('%s is not a declared configuration' % name)
                token, _, reason = rest.strip().partition(' ')
                if token not in WEAKENING_TOKENS:
                    raise MatrixError('%s is not one of the options this campaign counts as a '
                                      'weakening' % token)
                if len(reason.strip()) < 20:
                    raise MatrixError('%s in %s needs a reason, not a word' % (token, name))
                configurations[name].weakened[token] = reason.strip()
            elif record == 'run':
                parts = tail.split()
                if len(parts) < 4:
                    raise MatrixError('a run needs a configuration, a label, a target and a selection')
                name, label, target = parts[0], parts[1], parts[2]
                if name not in configurations:
                    raise MatrixError('%s is not a declared configuration' % name)
                if not (label.startswith('suite.') or label.startswith('pipeline.')):
                    raise MatrixError('%s is neither a suite. nor a pipeline. label' % label)
                if (name, label) in seen_labels:
                    raise MatrixError('%s is listed twice in %s' % (label, name))
                seen_labels.add((name, label))
                selection_index = stripped.index(target, stripped.index(label)) + len(target)
                selection_text = stripped[selection_index:].strip()
                repeat, shuffle = 1, False
                if selection_text.startswith('skip='):
                    kind, selection = 'skip', selection_text[len('skip='):].strip()
                    if len(selection) < 20:
                        raise MatrixError('%s in %s is skipped without a reason' % (label, name))
                else:
                    remaining = selection_text.split()
                    head = remaining[0]
                    for field in remaining[1:]:
                        if field == 'shuffle':
                            shuffle = True
                        elif field.startswith('repeat='):
                            repeat = int(field[len('repeat='):])
                            if repeat < 1:
                                raise MatrixError('%s in %s repeats %d times' % (label, name, repeat))
                        else:
                            raise MatrixError('%s is not a field a run takes' % field)
                    if head == 'whole':
                        kind, selection = 'whole', ''
                    elif head.startswith('filter='):
                        kind, selection = 'filter', head[len('filter='):]
                        if not selection:
                            raise MatrixError('%s in %s has an empty filter' % (label, name))
                    else:
                        raise MatrixError('%s is not whole, filter= or skip=' % head)
                rows.append(Row(name, label, target, kind, selection, repeat, shuffle))
            else:
                raise MatrixError('%s is not a record this matrix has' % record)
        except MatrixError as error:
            raise MatrixError('line %d: %s' % (number, error)) from None
        except ValueError as error:
            raise MatrixError('line %d: %s' % (number, error)) from None

    if not configurations:
        raise MatrixError('the matrix declares no configuration, so the campaign would pass by '
                          'doing nothing')
    for name, configuration in configurations.items():
        if not any(row.configuration == name for row in rows):
            raise MatrixError('%s has no row, so it would pass by doing nothing' % name)
    return configurations, rows


# The benchmark layer's suite is not one of the engine's, though its name begins with the prefix theirs share.
NOT_ENGINE_SUITES = ('Test_EngineBenchCommon',)


def generated_suites(repo_root=REPO_ROOT):
    """Every `Test_Engine*` project the generated trees define but the benchmark layer's.

    Whether a tree exists is reported apart from what is in it, because the two mean opposite things:
    a repository nobody has generated yet has nothing to compare the matrix against, and a generated
    tree with no engine suite in it is the matrix being wrong.

    @param repo_root the repository root.
    @return the names, and whether any tree was read at all.
    """
    found = set()
    trees = []
    for relative, suffix in GENERATED_TEST_TREES:
        directory = Path(repo_root) / relative
        if not directory.is_dir():
            continue
        trees.append(str(relative))
        for entry in sorted(directory.iterdir()):
            name = entry.name[: -len(suffix)] if entry.name.endswith(suffix) else ''
            if entry.is_file() and name.startswith('Test_Engine') and name not in NOT_ENGINE_SUITES:
                found.add(name)
    return found, trees


def consumer_suites(repo_root=REPO_ROOT):
    """Every active consumer suite the target registry names, and the consumer libraries each links.

    A consumer suite is a test the numbered runners run - `runner product` - of class `engine`, which links the engine,
    or `none`.

    @param repo_root the repository root.
    @return the suites mapped to the consumer libraries they link; empty when the registry cannot be read.
    """
    path = Path(repo_root) / TARGET_REGISTRY
    if not path.is_file():
        return {}
    suites = {}
    current = None
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('target '):
            current = {'name': line.split()[1]}
            continue
        if current is None or not line.startswith('    ') or not line.strip():
            continue
        key, _, value = line.strip().partition(' ')
        if key in ('workspace', 'state', 'class', 'runner'):
            current[key] = value.strip()
        elif key == 'depends':
            current['depends'] = [part.strip() for part in value.split(',') if part.strip()]
        if (current.get('workspace') == 'tests' and current.get('state') == 'active'
                and current.get('runner') == 'product' and current.get('class') in ('engine', 'none')
                and 'depends' in current):
            suites[current['name']] = tuple(name for name in current['depends'] if name in CONSUMER_LIBRARIES)
    return suites


def suppression_files(configurations, repo_root=REPO_ROOT):
    """The suppression files the matrix's options name.

    @param configurations the configurations.
    @param repo_root the repository root.
    @return one (configuration name, path) per suppression, in configuration order.
    """
    named = []
    for name in sorted(configurations):
        for value in configurations[name].options.values():
            for part in value.split(':'):
                if part.startswith('suppressions='):
                    named.append((name, Path(repo_root) / part[len('suppressions='):]))
    return named


def resolve_suppressions(value, repo_root=REPO_ROOT):
    """An option's value with each suppression file it names given from the repository root.

    The matrix names a suppression file as a repository path, and a row runs in its executable's directory, where the
    sanitizer would look for that path and not find it.

    @param value an option's value, `name=value` settings separated by colons.
    @param repo_root the repository root.
    @return the value, each `suppressions=` setting naming its file by its absolute path.
    """
    return ':'.join('suppressions=%s' % (Path(repo_root) / part[len('suppressions='):])
                    if part.startswith('suppressions=') else part for part in value.split(':'))


def check_suppression_file(path):
    """Whether a suppression file suppresses only what this campaign permits.

    A suppression is a decision to stop seeing something. This campaign permits exactly one kind: a
    report from inside a library this engine did not compile, with a reason written above it. A
    suppression naming this engine is a defect somebody decided to stop seeing, which is the one
    thing the campaign may not contain.

    @param path the suppression file.
    @return the problems, as sentences; empty when the file is acceptable.
    """
    problems = []
    if not path.is_file():
        return ['%s is named by an option and does not exist' % path]
    reason = None
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), start=1):
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith('#'):
            if stripped[1:].strip().lower().startswith('reason:'):
                reason = stripped[1:].strip()[len('reason:'):].strip()
            continue
        kind, separator, pattern = stripped.partition(':')
        if not separator or kind not in SUPPRESSION_KINDS:
            problems.append('%s:%d: %s is not a suppression entry' % (path.name, number, stripped))
            continue
        if not reason or len(reason) < 20:
            problems.append('%s:%d: %s has no reason above it' % (path.name, number, stripped))
        for engine in ENGINE_NAMES:
            if engine in pattern:
                problems.append('%s:%d: %s names this engine; a finding in this engine is a defect '
                                'to fix, not one to suppress' % (path.name, number, stripped))
        reason = None
    return problems


def check(repo_root=REPO_ROOT, path=REGISTRY, stream=sys.stdout):
    """Whether the matrix describes a campaign that covers what it claims to cover.

    @param repo_root the repository root.
    @param path the matrix file.
    @param stream where to report.
    @return zero when it does, one otherwise.
    """
    try:
        configurations, rows = read_matrix(path)
    except (OSError, MatrixError) as error:
        print('the sanitizer matrix is not usable: %s' % error, file=stream)
        return 1

    generated, trees = generated_suites(repo_root)
    consumers = consumer_suites(repo_root)
    if trees:
        # A consumer suite the registry names is held only once a generated tree defines it, as the engine's are.
        defined = set()
        for relative, suffix in GENERATED_TEST_TREES:
            directory = Path(repo_root) / relative
            if directory.is_dir():
                defined.update(entry.name[: -len(suffix)] for entry in directory.iterdir()
                               if entry.is_file() and entry.name.endswith(suffix))
        generated |= {name for name in consumers if name in defined}
    problems = []
    if not trees:
        print('no generated test workspace was found; the matrix alone was checked', file=stream)
    else:
        print('generated trees read: %s' % ', '.join(trees), file=stream)

    for name in sorted(configurations):
        configuration = configurations[name]
        configuration_rows = [row for row in rows if row.configuration == name]
        covered = {}
        for row in configuration_rows:
            if row.is_suite:
                if row.target in covered:
                    problems.append('%s covers %s twice' % (name, row.target))
                covered[row.target] = row
        for suite in sorted(generated - set(covered)):
            problems.append('%s is generated but %s does not say whether it runs' % (suite, name))
        for suite in sorted(set(covered) - generated):
            if trees and not covered[suite].is_skip:
                problems.append('%s runs %s, which no generated project defines' % (name, suite))
        if not any(row.is_pipeline for row in configuration_rows):
            problems.append('%s has no focused pipeline row' % name)
        if not any(not row.is_skip for row in configuration_rows):
            problems.append('%s skips everything, so it would pass by doing nothing' % name)

        present = set(configuration.present_weakening_tokens())
        for token in sorted(present - set(configuration.weakened)):
            problems.append('%s sets %s and does not say why' % (name, token))
        for token in sorted(set(configuration.weakened) - present):
            problems.append('%s explains %s, which its options do not set' % (name, token))

    suppressions = suppression_files(configurations, repo_root)
    for name, suppression in suppressions:
        for problem in check_suppression_file(suppression):
            problems.append('%s: %s' % (name, problem))

    for problem in problems:
        print(problem, file=stream)

    for name in sorted(configurations):
        configuration = configurations[name]
        configuration_rows = [row for row in rows if row.configuration == name]
        skipped = [row for row in configuration_rows if row.is_skip]
        pipelines = [row for row in configuration_rows if row.is_pipeline]
        print('%-8s %-8s hosts %-16s %3d row(s), %d skipped, %d pipeline(s), %d s per row'
              % (name, configuration.build, ','.join(configuration.hosts), len(configuration_rows),
                 len(skipped), len(pipelines), configuration.timeout), file=stream)
    if not suppressions:
        print('no configuration names a suppression file, so nothing is suppressed', file=stream)
    return 1 if problems else 0


def host_name():
    """Which of the matrix's hosts this machine is.

    @return `windows` or `linux`.
    """
    return 'windows' if os.name == 'nt' else 'linux'


def binary_directory(repo_root, config):
    """Where the built targets are.

    @param repo_root the repository root.
    @param config `release` or `debug`.
    @return the directory, whether or not it exists.
    """
    toolchain = 'msvc' if os.name == 'nt' else 'clang'
    return Path(repo_root) / 'bin' / toolchain / config


def binary_path(directory, name, config):
    """One target's executable.

    @param directory where the built targets are.
    @param name the project name.
    @param config `release` or `debug`, which decides the debug suffix.
    @return the path, whether or not it exists.
    """
    suffix = '_d' if config == 'debug' else ''
    return directory / (name + suffix + ('.exe' if os.name == 'nt' else ''))


def engine_library_path(directory, config):
    """The engine library a suite in that directory loads.

    @param directory where the built targets are.
    @param config `release` or `debug`.
    @return the path, whether or not it exists.
    """
    suffix = '_d' if config == 'debug' else ''
    if os.name == 'nt':
        return directory / ('AfterNightEngine' + suffix + '.dll')
    return directory / ('libAfterNightEngine' + suffix + '.so')


def instrumentation_markers(build, host, is_library):
    """What an instrumented binary of that build contains.

    The markers are the sanitizer runtime's own entry points, which instrumented code has to name to
    reach: the address sanitizer's reporting calls, the undefined-behaviour sanitizer's handlers, and
    ThreadSanitizer's access hooks. On Windows the runtime is a separate library, so the import of it
    is the marker.

    @param build `address` or `thread`.
    @param host `windows` or `linux`.
    @param is_library whether this is the engine library rather than a test executable.
    @return the byte sequences all of which must be present.
    """
    if host == 'windows':
        # The one instrumented build this toolchain has. A thread configuration never reaches here:
        # the matrix does not give it this host and the generator refuses to produce one.
        return (b'clang_rt.asan',)
    if build == 'thread':
        return (b'__tsan_',)
    if is_library:
        # The undefined-behaviour half is only required of the engine: a test executable that happens
        # to contain nothing the sanitizer instruments would otherwise look uninstrumented.
        return (b'__asan_', b'__ubsan_handle')
    return (b'__asan_',)


def library_path(directory, name, config):
    """A shared library a suite in that directory loads, by its project name.

    @param directory where the built targets are.
    @param name the library's project name.
    @param config `release` or `debug`.
    @return the path, whether or not it exists.
    """
    suffix = '_d' if config == 'debug' else ''
    if os.name == 'nt':
        return directory / (name + suffix + '.dll')
    return directory / ('lib' + name + suffix + '.so')


def missing_instrumentation(path, build, host, is_library):
    """Which markers an artifact does not carry.

    @param path the artifact.
    @param build `address` or `thread`.
    @param host `windows` or `linux`.
    @param is_library whether this is the engine library.
    @return the missing markers, decoded; empty when the artifact is instrumented.
    """
    content = Path(path).read_bytes()
    return [marker.decode('ascii') for marker in instrumentation_markers(build, host, is_library)
            if marker not in content]


def instrumented_tbb(environment):
    """The instrumented oneTBB a thread row needs, if one is reachable.

    ThreadSanitizer proves a happens-before edge by watching the code that establishes it, and this
    engine's edges are established inside oneTBB. Without an instrumented copy first on the library
    path, a thread row reports races that are the library's synchronisation being invisible.

    @param environment the environment the row would run in.
    @return the library that would be loaded, or None.
    """
    for directory in environment.get('LD_LIBRARY_PATH', '').split(os.pathsep):
        if not directory:
            continue
        candidates = sorted(Path(directory).glob('libtbb.so*')) if Path(directory).is_dir() else []
        for candidate in candidates:
            if candidate.is_file():
                return candidate if b'__tsan_' in candidate.read_bytes() else None
    return None


def findings_in(output):
    """The sanitizer reports in a row's output.

    @param output what the row printed.
    @return one entry per line that opens a report.
    """
    found = []
    for line in output.splitlines():
        for marker in FINDING_MARKERS:
            if marker in line:
                found.append(line.strip())
                break
    return found


def tests_run(output):
    """How many tests a row ran, when it is a test framework binary at all.

    @param output what the row printed.
    @return the count, or None when the binary reports no count.
    """
    counts = [int(match) for match in TESTS_RAN.findall(output)]
    return max(counts) if counts else None


def asan_runtime_directory(explicit=None):
    """Where the address sanitizer's runtime library is on this host.

    Microsoft's instrumented binaries import it by name and it is not on the ordinary path, so a row
    started without it fails to load with an error that says nothing about sanitizers.

    @param explicit a directory given on the command line, if any.
    @return the directory, or None.
    """
    if explicit:
        return Path(explicit)
    if os.name != 'nt':
        return None
    tools = os.environ.get('VCToolsInstallDir')
    if tools:
        candidate = Path(tools) / 'bin' / 'Hostx64' / 'x64'
        if (candidate / 'clang_rt.asan_dynamic-x86_64.dll').is_file():
            return candidate
    for root in (Path(os.environ.get('ProgramFiles', r'C:\Program Files')),
                 Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))):
        pattern = 'Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll'
        matches = sorted(root.glob(pattern))
        if matches:
            return matches[-1].parent
    return None


def run_row(row, configuration, directory, config, storage_root, output_directory, runtime_directory,
            stream=sys.stdout, linked=(), repo_root=REPO_ROOT):
    """Runs one row and decides what it reported.

    @param row the row.
    @param configuration the configuration it belongs to.
    @param directory where the built targets are.
    @param config `release` or `debug`.
    @param storage_root the test-owned storage this campaign may write in.
    @param output_directory where the row's whole output is kept.
    @param runtime_directory the sanitizer runtime directory to put on the path, or None.
    @param stream where to report.
    @param linked the consumer libraries the row's suite links, each of which must be instrumented beside it.
    @param repo_root the repository root, which the suppression files the options name are read from.
    @return `ok`, `skipped` or a sentence saying what went wrong.
    """
    if row.is_skip:
        print('%-8s %-24s %-26s skipped' % (row.configuration, row.label, row.target), file=stream)
        return 'skipped'

    host = host_name()
    executable = binary_path(directory, row.target, config)
    if not executable.is_file():
        return '%s is not built' % executable

    library = engine_library_path(directory, config)
    if not library.is_file():
        return '%s is not beside it' % library
    artifacts = [(executable, False), (library, True)]
    # A consumer suite exercises the consumer libraries it links as much as the engine; each must be instrumented too.
    for name in linked:
        linked = library_path(directory, name, config)
        if not linked.is_file():
            return '%s is not beside it' % linked
        artifacts.append((linked, True))
    for artifact, is_library in artifacts:
        missing = missing_instrumentation(artifact, configuration.build, host, is_library)
        if missing:
            return ('%s carries no %s instrumentation (no %s in it); regenerate with '
                    'AFTERNIGHT_SANITIZE=%s and rebuild'
                    % (artifact.name, configuration.build, ', '.join(missing), configuration.build))

    environment = dict(os.environ)
    environment.update({name: resolve_suppressions(value, repo_root) for name, value in configuration.options.items()})
    own_storage = Path(storage_root) / row.configuration / row.label
    own_storage.mkdir(parents=True, exist_ok=True)
    environment['AFTERNIGHT_TEST_STORAGE_ROOT'] = str(own_storage)
    if runtime_directory is not None:
        environment['PATH'] = str(runtime_directory) + os.pathsep + environment.get('PATH', '')
    if configuration.build == 'thread':
        tbb = instrumented_tbb(environment)
        if tbb is None:
            return ('no instrumented oneTBB is first on LD_LIBRARY_PATH; build one with '
                    'build/scripts/linux/build_tsan_onetbb.sh, or this row reports the library '
                    'rather than the engine')

    command = [str(executable)]
    if row.kind == 'filter':
        command.append('--gtest_filter=' + row.selection)
    if row.repeat > 1:
        command.append('--gtest_repeat=%d' % row.repeat)
    if row.shuffle:
        command.append('--gtest_shuffle')

    started = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=str(executable.parent), env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
                                   timeout=configuration.timeout)
        output = completed.stdout.decode('utf-8', errors='replace')
        code = completed.returncode
        timed_out = False
    except subprocess.TimeoutExpired as expired:
        output = (expired.stdout or b'').decode('utf-8', errors='replace')
        code = None
        timed_out = True
    elapsed = time.monotonic() - started

    log = Path(output_directory) / row.configuration / (row.label + '.log')
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(output, encoding='utf-8')

    count = tests_run(output)
    findings = findings_in(output)
    if timed_out:
        verdict = 'did not finish within %d s; a row that hangs is a finding' % configuration.timeout
    elif findings:
        verdict = '%d sanitizer report(s), the first being: %s' % (len(findings), findings[0])
    elif code != 0:
        verdict = 'exit %d' % code
    elif count == 0:
        verdict = 'ran no test at all, so it reported nothing about anything'
    else:
        verdict = 'ok'

    print('%-8s %-24s %-26s %-8s %6s tests %7.1f s'
          % (row.configuration, row.label, row.target, 'ok' if verdict == 'ok' else 'FAILED',
             '-' if count is None else count, elapsed), file=stream)
    if verdict != 'ok':
        print('    %s' % verdict, file=stream)
        print('    its whole output is in %s' % log, file=stream)
        for line in findings[:8]:
            print('    %s' % line, file=stream)
    return verdict


def main(argv=None, stream=sys.stdout):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--matrix', default=str(REGISTRY))
    parser.add_argument('--config', default='release', choices=('release', 'debug'))
    parser.add_argument('--configuration', default=None,
                        help='the instrumented configuration to run; required unless --check')
    parser.add_argument('--label', action='append', default=[],
                        help='run only this row; may be given more than once')
    parser.add_argument('--storage-root', default=None)
    parser.add_argument('--output', default=None, help='where every row\'s whole output is kept')
    parser.add_argument('--asan-runtime-dir', default=None,
                        help='where clang_rt.asan_dynamic-x86_64.dll is, when it is not found')
    parser.add_argument('--check', action='store_true',
                        help='read the matrix and the generated projects, and run nothing')
    args = parser.parse_args(argv)

    if args.check:
        return check(Path(args.repo_root), Path(args.matrix), stream)

    try:
        configurations, rows = read_matrix(Path(args.matrix))
    except (OSError, MatrixError) as error:
        print('the sanitizer matrix is not usable: %s' % error, file=stream)
        return 2

    if args.configuration is None:
        print('name the configuration to run with --configuration; this host can run %s'
              % ', '.join(sorted(name for name, configuration in configurations.items()
                                 if configuration.runs_on(host_name()))), file=stream)
        return 2
    if args.configuration not in configurations:
        print('%s is not a configuration this matrix declares' % args.configuration, file=stream)
        return 2

    configuration = configurations[args.configuration]
    host = host_name()
    if not configuration.runs_on(host):
        print('%s does not run on %s; the matrix says %s'
              % (configuration.name, host, ','.join(configuration.hosts)), file=stream)
        return 2

    selected = [row for row in rows if row.configuration == configuration.name]
    if args.label:
        wanted = set(args.label)
        unknown = sorted(wanted - {row.label for row in selected})
        if unknown:
            print('not in %s: %s' % (configuration.name, ', '.join(unknown)), file=stream)
            return 2
        selected = [row for row in selected if row.label in wanted]

    directory = binary_directory(Path(args.repo_root), args.config)
    storage_root = (Path(args.storage_root) if args.storage_root
                    else Path(args.repo_root) / 'obj' / 'sanitizer-storage')
    output_directory = (Path(args.output) if args.output
                        else Path(args.repo_root) / 'obj' / 'sanitizer-campaign')
    # A previous campaign's storage is removed rather than reused: a row that read a file an earlier
    # run left behind would not be running against the state it describes.
    shutil.rmtree(storage_root, ignore_errors=True)
    storage_root.mkdir(parents=True, exist_ok=True)
    output_directory.mkdir(parents=True, exist_ok=True)

    runtime_directory = None
    if host == 'windows' and configuration.build == 'address':
        runtime_directory = asan_runtime_directory(args.asan_runtime_dir)
        if runtime_directory is None:
            print('the address sanitizer runtime was not found; name its directory with '
                  '--asan-runtime-dir or the rows will not start', file=stream)
            return 2

    print('campaign: %s on %s, %s, %d row(s), %s'
          % (configuration.name, host, args.config, len(selected),
             ' '.join('%s=%s' % item for item in sorted(configuration.options.items()))),
          file=stream)
    print('platform: %s' % platform.platform(), file=stream)

    failures = []
    skipped = 0
    consumers = consumer_suites(Path(args.repo_root))
    for row in selected:
        verdict = run_row(row, configuration, directory, args.config, storage_root, output_directory,
                          runtime_directory, stream, consumers.get(row.target, ()), args.repo_root)
        if verdict == 'skipped':
            skipped += 1
        elif verdict != 'ok':
            failures.append((row.label, verdict))

    for row in selected:
        if row.is_skip:
            print('not run: %s %s - %s' % (row.label, row.target, row.selection), file=stream)

    if failures:
        for label, verdict in failures:
            print('%s: %s' % (label, verdict), file=stream)
        print('%d of %d row(s) did not come back clean' % (len(failures), len(selected) - skipped),
              file=stream)
        return 1
    print('every row of %s ran instrumented and reported nothing: %d run, %d not run'
          % (configuration.name, len(selected) - skipped, skipped), file=stream)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Run the fuzz campaign - the engine's targets and the consumer's - to the budget its registry states.

The campaign is one command rather than six, because the thing that has to be reproducible is the
whole run: which targets exist, which budget each was given, which seed, and what every one of them
reported. A run of four of the six targets that said "the fuzz targets pass" would be telling the
truth about the four.

What this does:

1. reads `fuzz_targets.txt`, which is the registry of targets and their budgets;
2. refuses a registry that names a target the generated projects do not define, or a target the
   projects define and the registry does not name - a target nobody runs is worse than one that does
   not exist, because it looks like coverage;
3. runs each target's binary with its budget, its own storage root and its own artifact directory;
   and
4. reports one line per target and fails when any of them did not come back.

A target fails by crashing. There is no other signal, because a coverage-guided fuzzer recognises no
other signal, and the standalone driver is deliberately held to the same contract. So a nonzero exit
from a target is a finding, and the reproducer is in the artifact directory beside a seed and an
iteration index that regenerate it exactly.

Usage:
    python build/scripts/tools/run_fuzz_campaign.py [--config release] [--seed 1] [--scale 1.0]
    python build/scripts/tools/run_fuzz_campaign.py --check
    python build/scripts/tools/run_fuzz_campaign.py --dump-corpus <dir>
    python build/scripts/tools/run_fuzz_campaign.py --libfuzzer --corpus <dir> [--libfuzzer-seconds 900]

`--check` reads the registry and the generated projects and runs nothing, which is what a build-graph
check wants. `--scale` multiplies every budget, so a smoke run before a real campaign costs seconds
rather than minutes; a scaled run says so in its report and is not the campaign.

`--libfuzzer` runs the instrumented half, a tree generated with `AFTERNIGHT_FUZZ=1` on Clang: each target
under libFuzzer over the seeds `--dump-corpus` wrote from an ordinary build, for fifteen minutes unless
`--libfuzzer-seconds` says otherwise. Before it runs anything it refuses a target that is not linked with
libFuzzer and a library of this product beside the targets that carries no coverage counter - a target in
front of an uninstrumented library mutates blind, keeps a corpus of one byte and reads as clean - and it
fails a run that loaded no counter but its own or that libFuzzer itself reports blind. Each target's output
goes to `libfuzzer.log` beside its artifacts and is read as it arrives, never held: a library that warns on
every input writes gigabytes in a target's fifteen minutes, and a target started by a runner that held them
would inherit its peak resident size, which libFuzzer reads as its own and stops on before its first input.

Tests: build/scripts/tools/run_fuzz_campaign_test.py
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
REGISTRY = Path(__file__).resolve().parent / 'fuzz_targets.txt'

# What the instrumented half reads off the binaries before it runs anything. A target linked with libFuzzer defines
# libFuzzer's driver entry point, which the standalone driver does not; a library of the libFuzzer generation registers
# its coverage counters and carries the address sanitizer.
LIBFUZZER_ENTRY = b'LLVMFuzzerRunDriver'
COVERAGE_MARKERS = (b'__sanitizer_cov_8bit_counters_init', b'__asan_')

# One target's wall-clock budget under libFuzzer unless --libfuzzer-seconds gives another, in seconds.
LIBFUZZER_SECONDS = 900

# What a libFuzzer run prints that the runner decides it by: the modules whose counters registered, libFuzzer's own
# warning that it found nothing it could tell apart, and the final statistics `-print_final_stats=1` asks for.
LOADED_MODULES = re.compile(r'INFO: Loaded (\d+) modules?\s+\((\d+) inline 8-bit counters\)')
BLIND_WARNING = 'Is the code instrumented for coverage?'
NEW_UNITS = re.compile(r'stat::new_units_added:\s+(\d+)')
EXECUTED_UNITS = re.compile(r'stat::number_of_executed_units:\s+(\d+)')
# How many of a target's last lines a failed run reports, where a sanitizer's report and libFuzzer's ending are.
FAILURE_TAIL_LINES = 60

# How a fuzz target is named: the engine's subjects, and the consumer's - the integration's durable documents and the
# extension protocol - which the same harness and driver run.
TARGET_PREFIXES = ('Fuzz_Engine', 'Fuzz_Consumer')

# Where a generator writes the test workspace, and how a project file is named in each. Both front
# ends are read, because the registry has to agree with whichever tree this host generated.
GENERATED_TEST_TREES = (
    (Path('build') / 'vs2026-msvc' / 'tests', '.vcxproj'),
    (Path('build') / 'vs2026-clang' / 'tests', '.vcxproj'),
    (Path('build') / 'make-linux' / 'tests', '.make'),
)


class RegistryError(Exception):
    """The registry does not describe a campaign that can be run."""


class Target:
    """One registered target: its project name and the budget it is run to."""

    def __init__(self, name, iterations, seconds):
        self.name = name
        self.iterations = iterations
        self.seconds = seconds

    def scaled(self, scale):
        """This target's budget multiplied, with a floor of one so a scale never empties a run.

        @param scale what to multiply by.
        @return a new target.
        """
        return Target(self.name, max(1, int(self.iterations * scale)), max(1, int(self.seconds * scale)))

    def __repr__(self):
        return 'Target(%s, %d, %d)' % (self.name, self.iterations, self.seconds)


def read_registry(path=REGISTRY):
    """The targets and budgets the registry states.

    @param path the registry file.
    @return the targets, in the order they are listed.
    """
    targets = []
    seen = set()
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
        fields = stripped.split()
        name = fields[0]
        if not name.startswith(TARGET_PREFIXES):
            raise RegistryError('line %d: %s is not a fuzz target name' % (number, name))
        if name in seen:
            raise RegistryError('line %d: %s is listed twice' % (number, name))
        seen.add(name)
        budget = {'iterations': None, 'seconds': None}
        for field in fields[1:]:
            key, separator, value = field.partition('=')
            if not separator or key not in budget:
                raise RegistryError('line %d: %s is not a budget' % (number, field))
            try:
                budget[key] = int(value)
            except ValueError:
                raise RegistryError('line %d: %s is not a whole number' % (number, field)) from None
        if budget['iterations'] is None or budget['seconds'] is None:
            raise RegistryError('line %d: %s needs both an iteration and a wall-clock budget' % (number, name))
        if budget['iterations'] <= 0 or budget['seconds'] <= 0:
            raise RegistryError('line %d: %s has a budget of nothing' % (number, name))
        targets.append(Target(name, budget['iterations'], budget['seconds']))
    if not targets:
        raise RegistryError('the registry names no target, so the campaign would pass by doing nothing')
    return targets


def generated_targets(repo_root=REPO_ROOT):
    """Every `Fuzz_Engine*` and `Fuzz_Consumer*` project the generated trees define.

    Whether a tree exists is reported apart from what is in it, because the two mean opposite things:
    a repository nobody has generated yet has nothing to compare the registry against, and a generated
    tree with no fuzz project in it is the registry being wrong.

    @param repo_root the repository root.
    @return the names mapped to the trees they were found in, and whether any tree was read at all.
    """
    found = {}
    trees = []
    for relative, suffix in GENERATED_TEST_TREES:
        directory = Path(repo_root) / relative
        if not directory.is_dir():
            continue
        trees.append(str(relative))
        for entry in sorted(directory.iterdir()):
            if entry.is_file() and entry.name.endswith(suffix) and entry.name.startswith(TARGET_PREFIXES):
                found.setdefault(entry.name[: -len(suffix)], []).append(str(relative))
    return found, trees


def check(repo_root=REPO_ROOT, path=REGISTRY, stream=sys.stdout):
    """Whether the registry and the generated projects describe the same set of targets.

    @param repo_root the repository root.
    @param path the registry file.
    @param stream where to report.
    @return zero when they agree, one otherwise.
    """
    try:
        targets = read_registry(path)
    except (OSError, RegistryError) as error:
        print('the fuzz registry is not usable: %s' % error, file=stream)
        return 1

    registered = {target.name for target in targets}
    generated, trees = generated_targets(repo_root)
    problems = []
    if not trees:
        print('no generated test workspace was found; the registry alone was checked', file=stream)
    else:
        print('generated trees read: %s' % ', '.join(trees), file=stream)
        for name in sorted(registered - set(generated)):
            problems.append('%s is registered but no generated project defines it' % name)
        for name in sorted(set(generated) - registered):
            problems.append('%s is generated but the registry does not run it' % name)

    for problem in problems:
        print(problem, file=stream)
    for target in targets:
        print('%-22s %8d iterations, %4d s' % (target.name, target.iterations, target.seconds), file=stream)
    return 1 if problems else 0


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


def run_target(target, directory, config, seed, storage_root, artifacts, extra=(), stream=sys.stdout):
    """Runs one target to its budget.

    @param target the target and its budget.
    @param directory where the built targets are.
    @param config `release` or `debug`.
    @param seed the generator's seed.
    @param storage_root the test-owned storage this target may write in.
    @param artifacts where a reproducer is written.
    @param extra further arguments, for the corpus dump.
    @param stream where to report.
    @return the target's exit code, or 127 when it is not there.
    """
    executable = binary_path(directory, target.name, config)
    if not executable.is_file():
        print('%-22s MISSING  %s' % (target.name, executable), file=stream)
        return 127

    own_storage = Path(storage_root) / target.name
    own_storage.mkdir(parents=True, exist_ok=True)
    own_artifacts = Path(artifacts) / target.name
    own_artifacts.mkdir(parents=True, exist_ok=True)

    command = [str(executable), '--seed', str(seed), '--iterations', str(target.iterations),
               '--max-seconds', str(target.seconds), '--artifacts', str(own_artifacts)]
    command.extend(extra)
    environment = dict(os.environ)
    environment['AFTERNIGHT_TEST_STORAGE_ROOT'] = str(own_storage)

    started = time.monotonic()
    completed = subprocess.run(command, cwd=str(executable.parent), env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    elapsed = time.monotonic() - started
    output = completed.stdout.decode('utf-8', errors='replace')
    print(output.rstrip(), file=stream)
    print('%-22s %s  %6.1f s' % (target.name, 'ok     ' if completed.returncode == 0 else
                                 'FAILED ' + str(completed.returncode), elapsed), file=stream)
    return completed.returncode


def product_libraries(directory, config):
    """Every shared library of this product beside the targets, in order.

    The libFuzzer generation instruments every one of them, so every one is read rather than only those a target
    links: a library left over from an ordinary build is exactly the blind spot the instrumented half exists to close.

    @param directory where the built targets are.
    @param config `release` or `debug`, which decides the debug suffix.
    @return the paths.
    """
    found = Path(directory).glob('AfterNight*.dll' if os.name == 'nt' else 'libAfterNight*.so')
    return sorted(path for path in found if path.stem.endswith('_d') == (config == 'debug'))


def libfuzzer_build_problems(targets, directory, config, corpus):
    """What stops the instrumented half from running at all.

    @param targets the targets to run.
    @param directory where the built targets are.
    @param config `release` or `debug`.
    @param corpus the directory the ordinary build's `--dump-corpus` wrote the seeds into.
    @return the problems, each naming what to do; empty when the run may start.
    """
    problems = []
    libraries = product_libraries(directory, config)
    if not libraries:
        problems.append('no library of this product is beside the targets in %s, so nothing says what the targets '
                        'would be guided by' % directory)
    for library in libraries:
        data = library.read_bytes()
        missing = [marker.decode('ascii') for marker in COVERAGE_MARKERS if marker not in data]
        if missing:
            problems.append('%s carries no coverage instrumentation (no %s in it); regenerate with AFTERNIGHT_FUZZ=1 '
                            'and rebuild it' % (library.name, ', '.join(missing)))
    for target in targets:
        executable = binary_path(directory, target.name, config)
        if not executable.is_file():
            problems.append('%s is missing: %s' % (target.name, executable))
        elif LIBFUZZER_ENTRY not in executable.read_bytes():
            problems.append('%s is not linked with libFuzzer (no %s in it); regenerate with AFTERNIGHT_FUZZ=1 and '
                            'rebuild it' % (target.name, LIBFUZZER_ENTRY.decode('ascii')))
        seeds = Path(corpus) / target.name
        if not seeds.is_dir() or not any(seeds.iterdir()):
            problems.append('no seeds for %s under %s; write them from an ordinary build with --dump-corpus'
                            % (target.name, corpus))
    return problems


def run_libfuzzer_target(target, seconds, directory, config, seed, corpus, storage_root, artifacts,
                         stream=sys.stdout):
    """Runs one target under libFuzzer and decides it.

    @param target the target.
    @param seconds its wall-clock budget.
    @param directory where the built targets are.
    @param config `release` or `debug`.
    @param seed libFuzzer's seed.
    @param corpus the seeds' directory; the target's own subdirectory is its corpus and grows.
    @param storage_root the test-owned storage this target may write in.
    @param artifacts where libFuzzer writes a crashing or hanging input.
    @param stream where to report.
    @return the reason the run failed, or None when it passed.
    """
    executable = binary_path(directory, target.name, config)
    own_storage = Path(storage_root) / target.name
    own_storage.mkdir(parents=True, exist_ok=True)
    own_artifacts = Path(artifacts) / target.name
    own_artifacts.mkdir(parents=True, exist_ok=True)

    command = [str(executable), str(Path(corpus) / target.name), '-max_total_time=%d' % seconds,
               '-seed=%d' % seed, '-print_final_stats=1', '-artifact_prefix=%s%s' % (own_artifacts, os.sep)]
    environment = dict(os.environ)
    environment['AFTERNIGHT_TEST_STORAGE_ROOT'] = str(own_storage)
    log_path = own_artifacts / 'libfuzzer.log'
    print('%-22s %s' % (target.name, ' '.join(command)), file=stream)

    # Read as it arrives and kept only as figures and a tail, so the runner's size never follows the target's output.
    modules, added, executed, blind = 0, None, None, False
    tail = collections.deque(maxlen=FAILURE_TAIL_LINES)
    started = time.monotonic()
    with open(log_path, 'wb') as log:
        process = subprocess.Popen(command, cwd=str(executable.parent), env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for raw in process.stdout:
            log.write(raw)
            line = raw.decode('utf-8', errors='replace').rstrip()
            tail.append(line)
            loaded = LOADED_MODULES.search(line)
            if loaded:
                modules = int(loaded.group(1))
                print(line, file=stream)
            elif NEW_UNITS.search(line):
                added = NEW_UNITS.search(line).group(1)
            elif EXECUTED_UNITS.search(line):
                executed = EXECUTED_UNITS.search(line).group(1)
            blind = blind or BLIND_WARNING in line
        returncode = process.wait()
    elapsed = time.monotonic() - started

    summary = '%d modules, %s new units, %s executions' % (modules, added or '?', executed or '?')
    print('%-22s output in %s' % (target.name, log_path), file=stream)
    if returncode != 0:
        print('\n'.join(tail), file=stream)
        reason = 'did not come back: exit %d; its input is under %s' % (returncode, own_artifacts)
    elif blind or modules < 2:
        # The target's own counters register as one module; a library whose counters reached libFuzzer adds one more.
        reason = ('ran blind: libFuzzer saw the counters of %d module(s) and the target is one of them, so nothing it '
                  'hands an input to guided it' % modules)
    else:
        reason = None
    print('%-22s %s  %s, %6.1f s' % (target.name, 'ok     ' if reason is None else 'FAILED ', summary, elapsed),
          file=stream)
    return reason


def run_libfuzzer(targets, args, stream):
    """The instrumented half, start to finish.

    @param targets the targets to run.
    @param args the parsed command line.
    @param stream where to report.
    @return the exit code: 0 when every target ran and saw its libraries, 1 when one did not, 2 when none could start.
    """
    directory = binary_directory(Path(args.repo_root), args.config)
    problems = libfuzzer_build_problems(targets, directory, args.config, args.corpus)
    if problems:
        for problem in problems:
            print(problem, file=stream)
        print('the instrumented half was not run', file=stream)
        return 2

    seconds = max(1, int(args.libfuzzer_seconds * args.scale))
    storage_root = Path(args.storage_root) if args.storage_root else Path(args.repo_root) / 'obj' / 'fuzz-storage'
    artifacts = Path(args.artifacts) if args.artifacts else Path(args.repo_root) / 'obj' / 'fuzz-artifacts'
    shutil.rmtree(storage_root, ignore_errors=True)
    storage_root.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)

    print('libFuzzer campaign: %d target(s), %d s each, seed %d, %s' % (len(targets), seconds, args.seed, args.config),
          file=stream)
    if seconds != args.libfuzzer_seconds:
        print('this is a scaled run and is not the campaign', file=stream)
    failures = []
    for target in targets:
        reason = run_libfuzzer_target(target, seconds, directory, args.config, args.seed, args.corpus, storage_root,
                                      artifacts, stream)
        if reason is not None:
            failures.append((target.name, reason))
    if failures:
        for name, reason in failures:
            print('%s %s' % (name, reason), file=stream)
        return 1
    print('every registered target ran its budget under libFuzzer, guided by the libraries it reaches', file=stream)
    return 0


def main(argv=None, stream=sys.stdout):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--registry', default=str(REGISTRY))
    parser.add_argument('--config', default='release', choices=('release', 'debug'))
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--scale', type=float, default=1.0,
                        help='multiply every budget; a scaled run is a smoke test, not the campaign')
    parser.add_argument('--target', action='append', default=[],
                        help='run only this target; may be given more than once')
    parser.add_argument('--storage-root', default=None)
    parser.add_argument('--artifacts', default=None)
    parser.add_argument('--dump-corpus', default=None,
                        help='write every target\'s seeds under this directory and run nothing')
    parser.add_argument('--check', action='store_true',
                        help='read the registry and the generated projects, and run nothing')
    parser.add_argument('--libfuzzer', action='store_true',
                        help='run the instrumented half: every target under libFuzzer, in a tree generated with '
                             'AFTERNIGHT_FUZZ=1')
    parser.add_argument('--corpus', default=None,
                        help='with --libfuzzer, the directory --dump-corpus wrote the seeds into')
    parser.add_argument('--libfuzzer-seconds', type=int, default=LIBFUZZER_SECONDS,
                        help='with --libfuzzer, each target\'s wall-clock budget (default: fifteen minutes)')
    args = parser.parse_args(argv)

    if args.check:
        return check(Path(args.repo_root), Path(args.registry), stream)

    try:
        targets = read_registry(Path(args.registry))
    except (OSError, RegistryError) as error:
        print('the fuzz registry is not usable: %s' % error, file=stream)
        return 2

    if args.target:
        wanted = set(args.target)
        unknown = sorted(wanted - {target.name for target in targets})
        if unknown:
            print('not registered: %s' % ', '.join(unknown), file=stream)
            return 2
        targets = [target for target in targets if target.name in wanted]

    if args.libfuzzer:
        if not args.corpus:
            print('--libfuzzer needs --corpus: the seeds --dump-corpus wrote from an ordinary build', file=stream)
            return 2
        return run_libfuzzer(targets, args, stream)

    directory = binary_directory(Path(args.repo_root), args.config)
    storage_root = Path(args.storage_root) if args.storage_root else Path(args.repo_root) / 'obj' / 'fuzz-storage'
    artifacts = Path(args.artifacts) if args.artifacts else Path(args.repo_root) / 'obj' / 'fuzz-artifacts'
    # A previous campaign's storage is removed rather than reused: a target that read a file an
    # earlier run left behind would not be reading the input it was given.
    shutil.rmtree(storage_root, ignore_errors=True)
    storage_root.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)

    print('campaign: %d target(s), seed %d, %s, scale %g' % (len(targets), args.seed, args.config, args.scale),
          file=stream)
    if args.scale != 1.0:
        print('this is a scaled run and is not the campaign', file=stream)

    failures = []
    for target in targets:
        scaled = target.scaled(args.scale)
        extra = []
        if args.dump_corpus:
            # One directory per target, because the seeds are a target's own and a shared directory
            # would leave whichever ran last.
            extra = ['--dump-corpus', str(Path(args.dump_corpus) / target.name)]
            scaled = Target(target.name, 1, 1)
        code = run_target(scaled, directory, args.config, args.seed, storage_root, artifacts, extra, stream)
        if code != 0:
            failures.append((target.name, code))

    if failures:
        for name, code in failures:
            print('%s did not come back: exit %d; its reproducer is under %s'
                  % (name, code, artifacts / name), file=stream)
        return 1
    print('every registered target evaluated its whole budget and came back', file=stream)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

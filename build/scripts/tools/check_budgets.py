# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Measure and check the include and compile-time budgets of AfterNightEngine.

Compile time and header weight regress one include at a time and are never the reason anyone opens
a file, so by the point they are noticed the cheap fixes are gone. This records the numbers and
compares every change against them, rather than measuring them for the first time when they can no
longer be met cheaply.

Three things are measured, and they are different in kind:

* **Include counts** are exact and the same on every machine, because they are read out of the
  source rather than out of a compiler. They are budgeted with ceilings, and a ceiling that starts
  to bind is a design conversation, not a number to raise quietly.
* **Object sizes** are recorded only for the translation units that compile the same kernels once
  per instruction set, and only at the optimization level that ships. That is the number template
  and instruction-set bloat shows up in: how much object one more compiled target costs. It
  is measured by compiling the unit once per declared target subset and comparing, so the cost of
  a rung is a difference rather than an estimate, and it is per host because two toolchains do
  not emit the same amount of object for the same source.
* **Compile times** are wall-clock milliseconds and mean nothing across machines. They are recorded
  per host, and compared only against a baseline recorded on the same host. A run that has no
  baseline for its host says so and compares nothing, because a comparison against another machine's
  numbers is worse than no comparison at all. The preprocessed line count is recorded beside each
  time as the part a reader can reason about: it moves when a header gets heavier and not when the
  machine is busy. It is a per-host number all the same: it counts the text of a standard library,
  and two hosts do not ship the same one.

Usage:
    python build/scripts/tools/check_budgets.py measure [--host-id <id>] [--compiler <path>]
                                                        [--repetitions <n>] [--record] [--rebaseline]
    python build/scripts/tools/check_budgets.py check   [--host-id <id>] [--compiler <path>]

`measure` reports; `measure --record` adds missing numbers to the budget file, which needs a
--host-id for the compile half. Existing measurements are immutable unless `--rebaseline` is
explicitly supplied. `check` compares and exits 1 on a breach.

Tests: build/scripts/tools/check_budgets_test.py
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_include_layers as layers  # noqa: E402

REPO_ROOT = layers.REPO_ROOT

BUDGET_FILE = 'docs/dev/budgets.json'
BUDGET_SCHEMA = 1

PUBLIC_INCLUDE_ROOT = 'src/AfterNightEngine/include'
PRIVATE_INCLUDE_ROOT = 'src/AfterNightEngine/src'

# Where the engine's external dependencies are found, per platform. An implementation file that
# reaches one cannot be compiled without its root, and a measurement that skipped those files would
# quietly stop measuring them - the runtime, which reaches oneTBB, among them.
#
# Only roots that exist are passed, so a host with the library somewhere else, or in a system
# directory the compiler already searches, measures exactly as well. The public surface is measured
# without any of them, deliberately: a public header that needed an external include root would be a
# public header with a library in it, and the layering checker already refuses that.
EXTERNAL_INCLUDE_ROOTS = (
    'vcpkg/installed/x64-windows-raw/include',
    'vcpkg/installed/x64-linux-raw/include',
    'vcpkg/installed/x64-windows/include',
    'vcpkg/installed/x64-linux/include',
    'vcpkg/installed/x64-linux-release/include',
    # The codec libraries the dependency steps fetch and build themselves, OpenFITS and OpenXISF,
    # each installed where its build script puts it on Windows and on Linux.
    'openfits/build-msvc-install/include',
    'openfits/build-clang-install/include',
    'openxisf/build-msvc-install/include',
    'openxisf/build-clang-install/include',
    # The vendored tree, for the one library kept in the repository itself. The build gives the
    # engine the same root for the same reason.
    '3rdparty/src',
)

DEFAULT_REPETITIONS = 3


class Measurement:
    """One measured translation unit: how long it took, and how much text it produced."""

    def __init__(self, unit, milliseconds, preprocessed_lines):
        self.unit = unit
        self.milliseconds = milliseconds
        self.preprocessed_lines = preprocessed_lines


def load_budget(repo_root):
    path = repo_root / BUDGET_FILE
    if not path.is_file():
        raise FileNotFoundError('%s does not exist' % BUDGET_FILE)
    document = json.loads(path.read_bytes().decode('utf-8'))
    if document.get('schema') != BUDGET_SCHEMA:
        raise ValueError('%s: schema %r is not the %d this tool writes'
                         % (BUDGET_FILE, document.get('schema'), BUDGET_SCHEMA))
    return document


def save_budget(repo_root, document):
    """Write the budget file back, as CRLF UTF-8 like every other owned text file here."""
    text = json.dumps(document, indent=2, sort_keys=True) + '\n'
    path = repo_root / BUDGET_FILE
    path.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))


def public_headers(repo_root):
    """Every public header, keyed by the path an include names it with."""
    files, _ = layers.collect_files(repo_root)
    headers = {}
    for source in files:
        if source.public:
            below = source.relative.split(layers.PUBLIC_DIR + '/', 1)[-1]
            headers[layers.PUBLIC_INCLUDE_PREFIX + below] = source
    return headers


def private_sources(repo_root):
    """Every implementation translation unit, by repository-relative path."""
    files, _ = layers.collect_files(repo_root)
    return [source for source in files if not source.public and source.path.suffix == '.cpp']


def transitive_includes(header_key, headers):
    """Every public and standard header reachable from one public header, transitively.

    The public headers it reaches are its siblings, as the budget file counts them. Reachability is
    read from the include directives themselves, so the answer is exact, identical on every machine,
    and unaffected by which compiler or standard library is installed. That is the
    property that makes it worth budgeting: a wall-clock time tells you the machine was busy, and
    this tells you the header got heavier.
    """
    sibling = set()
    standard = set()
    external = set()
    pending = [header_key]
    seen = set()
    while pending:
        current = pending.pop()
        if current in seen or current not in headers:
            continue
        seen.add(current)
        for include in headers[current].includes:
            target = include.target
            if target.startswith(layers.PUBLIC_INCLUDE_PREFIX):
                if target != header_key:
                    sibling.add(target)
                pending.append(target)
            elif layers.is_standard_header(target):
                standard.add(target)
            else:
                external.add(target)
    return sibling, standard, external


def measure_includes(repo_root):
    """The include weight of each public header and of the surface as a whole."""
    headers = public_headers(repo_root)
    per_header = {}
    surface_standard = set()
    for key in sorted(headers):
        sibling, standard, external = transitive_includes(key, headers)
        per_header[key] = {
            'sibling': len(sibling),
            'standard': len(standard),
            'external': len(external),
        }
        surface_standard |= standard
    return {
        'publicHeaders': len(headers),
        'standardAcrossSurface': len(surface_standard),
        'perHeader': per_header,
    }


def check_includes(measured, limits):
    problems = []
    per_header_sibling = limits['maxSiblingIncludesPerPublicHeader']
    per_header_standard = limits['maxStandardIncludesPerPublicHeader']
    surface_standard = limits['maxStandardIncludesAcrossSurface']
    for key in sorted(measured['perHeader']):
        counts = measured['perHeader'][key]
        if counts['sibling'] > per_header_sibling:
            problems.append('%s pulls in %d public headers; the budget is %d'
                            % (key, counts['sibling'], per_header_sibling))
        if counts['standard'] > per_header_standard:
            problems.append('%s pulls in %d standard headers; the budget is %d'
                            % (key, counts['standard'], per_header_standard))
        if counts['external']:
            problems.append('%s pulls in %d header(s) that are neither public nor standard'
                            % (key, counts['external']))
    if measured['standardAcrossSurface'] > surface_standard:
        problems.append('the public surface pulls in %d distinct standard headers; the budget is %d'
                        % (measured['standardAcrossSurface'], surface_standard))
    return problems


def find_compiler(explicit=None, environment=None):
    """The C++ compiler to measure with, or None when there is none to use."""
    environment = os.environ if environment is None else environment
    for candidate in (explicit, environment.get('CXX')):
        if candidate:
            return shutil.which(candidate) or (candidate if os.path.isfile(candidate) else None)
    for name in ('clang++', 'g++', 'cl'):
        found = shutil.which(name)
        if found:
            return found
    return None


def is_msvc(compiler):
    return pathlib.Path(compiler).stem.lower() == 'cl'


def compiler_identity(compiler, runner=None):
    """What the compiler calls itself, which is what a recorded number should be filed under.

    Its path is not that: it is machine-specific, so recording one would put a local install
    directory into a file everybody shares and would not say which compiler produced the numbers.
    """
    execute = runner if runner is not None else _run
    command = [compiler] if is_msvc(compiler) else [compiler, '--version']
    code, text = execute(command, REPO_ROOT)
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return pathlib.Path(compiler).stem + (' (version not reported)' if code == 0 else ' (unknown)')



def compile_command(compiler, source, output, include_roots, extra_flags=(), defines=()):
    if is_msvc(compiler):
        command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/permissive-']
        command += list(extra_flags)
        command += ['/D' + define for define in defines]
        command += ['/I' + root for root in include_roots]
        return command + ['/c', str(source), '/Fo' + str(output)]
    command = [compiler, '-std=c++20', '-fsyntax-only' if output is None else '-c']
    command += list(extra_flags)
    command += ['-D' + define for define in defines]
    for root in include_roots:
        command += ['-I', root]
    if output is not None:
        command += ['-o', str(output)]
    return command + [str(source)]


def preprocess_command(compiler, source, include_roots, extra_flags=(), defines=()):
    if is_msvc(compiler):
        command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/permissive-']
        command += list(extra_flags)
        command += ['/D' + define for define in defines]
        command += ['/I' + root for root in include_roots]
        return command + ['/EP', str(source)]
    command = [compiler, '-std=c++20', '-E']
    command += list(extra_flags)
    command += ['-D' + define for define in defines]
    for root in include_roots:
        command += ['-I', root]
    return command + [str(source)]


def measure_unit(compiler, repo_root, source, include_roots, repetitions, runner=None,
                 extra_flags=(), defines=()):
    """One translation unit: the fastest of several compilations, its preprocessed size, its object.

    The fastest run is the statistic, not the average. Compilation noise only ever adds time - a
    scheduler slice lost to something else, a cold file cache - so the minimum is the closest thing
    to the cost of the work itself, and it is far more stable run to run than a mean.

    The object size is reported for every unit and budgeted only for the ones measured per
    instruction set, because it means two different things at two optimization levels and only the
    shipped one answers "what does one more compiled target cost".
    """
    execute = runner if runner is not None else _run
    with tempfile.TemporaryDirectory(prefix='an-budget-') as temporary:
        output = pathlib.Path(temporary) / (source.stem + '.obj')
        best = None
        object_bytes = 0
        for _ in range(repetitions):
            started = time.perf_counter()
            code, text = execute(
                compile_command(compiler, source, output, include_roots, extra_flags, defines),
                repo_root)
            elapsed = (time.perf_counter() - started) * 1000.0
            if code != 0:
                raise RuntimeError('compiling %s failed:\n%s' % (source, text))
            best = elapsed if best is None else min(best, elapsed)
            object_bytes = output.stat().st_size if output.is_file() else 0
        code, text = execute(
            preprocess_command(compiler, source, include_roots, extra_flags, defines), repo_root)
        if code != 0:
            raise RuntimeError('preprocessing %s failed:\n%s' % (source, text))
    return best, text.count('\n') + (0 if text.endswith('\n') or not text else 1), object_bytes


def _run(command, repo_root):
    completed = subprocess.run(command, cwd=str(repo_root), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, encoding='utf-8', errors='replace',
                               check=False)
    return completed.returncode, completed.stdout


def external_include_roots(repo_root):
    """The external include roots that exist on this host, in a stable order."""
    return [root for root in EXTERNAL_INCLUDE_ROOTS if (repo_root / root).is_dir()]


def compiler_family(compiler):
    """Which flag spellings this compiler takes, as the budget file names them."""
    return 'msvc' if is_msvc(compiler) else 'other'


def isa_unit_names(document):
    """Every translation unit the budget file measures once per instruction-set subset."""
    return tuple(entry['unit'] for entry in document.get('isa', {}).get('units', ()))


def baseline_isa_flags(compiler, isa_flags, unit, isa_units):
    """The flags one instruction-set unit needs to reach its intended baseline outside the build.

    These units are compiled by the build system with flags of their own, and they refuse to
    compile without them - deliberately, because a lost flag would otherwise produce a narrower
    baseline than the packaged floor and no diagnostic. This tool has no build system, so it
    reproduces the same flags from the budget file. Every other unit is measured exactly as before.
    """
    if not isa_flags or unit not in isa_units:
        return ()
    return tuple(isa_flags.get(compiler_family(compiler), ()))


def measure_compile(repo_root, compiler, repetitions, runner=None, isa_flags=None, isa_units=()):
    """Every measured translation unit: the public self-compile units and the implementation."""
    measurements = []
    self_compile = repo_root / layers.SELF_COMPILE_DIR
    for path in sorted(self_compile.glob('*.cpp')):
        milliseconds, lines, _ = measure_unit(compiler, repo_root, path, [PUBLIC_INCLUDE_ROOT],
                                              repetitions, runner)
        measurements.append(Measurement(path.relative_to(repo_root).as_posix(), milliseconds, lines))
    private_roots = [PUBLIC_INCLUDE_ROOT, PRIVATE_INCLUDE_ROOT] + external_include_roots(repo_root)
    for source in private_sources(repo_root):
        milliseconds, lines, _ = measure_unit(
            compiler, repo_root, source.path, private_roots, repetitions, runner,
            extra_flags=baseline_isa_flags(compiler, isa_flags, source.relative, isa_units),
            defines=raw_backend_defines(repo_root, source.relative))
        measurements.append(Measurement(source.relative, milliseconds, lines))
    return measurements


def raw_backend_defines(repo_root, unit):
    """Measure the RAW implementation selected by the same optional package binding as Premake."""
    if '/src/io/raw/' not in '/' + str(unit).replace('\\', '/') or os.environ.get('AFTERNIGHT_RAWSPEED') == '0':
        return ()
    for triplet in ('x64-windows-raw', 'x64-linux-raw'):
        header = repo_root / 'vcpkg/installed' / triplet / 'include/afternight-libraw/libraw/libraw.h'
        if header.is_file():
            return ('AFTERNIGHT_HAS_RAWSPEED',)
    return ()


class IsaMeasurement:
    """One translation unit compiled with one subset of the instruction sets it can carry."""

    def __init__(self, unit, label, milliseconds, object_bytes):
        self.unit = unit
        self.label = label
        self.milliseconds = milliseconds
        self.object_bytes = object_bytes


def isa_variant_key(unit, label):
    """The name one unit's one variant is recorded under. Total and reversible."""
    return unit + '|' + label


def measure_isa(repo_root, compiler, repetitions, document, runner=None):
    """Every declared unit, once per declared instruction-set subset, at shipping optimization.

    The subsets are expressed as macro definitions rather than as compiler switches on purpose: the
    instruction sets in these units are compiled by the vector abstraction rather than by an
    -m or /arch flag, so restricting them is a property of the source and not of the command line.
    Measuring both the floor alone and the whole ladder is what turns "the object grew" into "this
    rung costs this much", which is the question about bloat worth asking.
    """
    isa = document.get('isa')
    if not isa:
        return []
    family = compiler_family(compiler)
    flags = tuple(isa.get('optimizationFlags', {}).get(family, ()))
    flags += tuple(document['compile'].get('isaUnitFlags', {}).get(family, ()))
    roots = [PUBLIC_INCLUDE_ROOT, PRIVATE_INCLUDE_ROOT] + external_include_roots(repo_root)
    measurements = []
    for entry in isa['units']:
        source = repo_root / entry['unit']
        for variant in entry['variants']:
            milliseconds, _, object_bytes = measure_unit(
                compiler, repo_root, source, roots, repetitions, runner, extra_flags=flags,
                defines=tuple(variant.get('defines', ())))
            measurements.append(
                IsaMeasurement(entry['unit'], variant['label'], milliseconds, object_bytes))
    return measurements


def check_isa(measurements, baseline, allowances):
    """Compare each variant against the same host's recorded object size and compile time."""
    problems = []
    recorded = baseline.get('variants', {})
    object_allowance = allowances['objectSizeAllowance']
    wall_allowance = allowances['wallTimeAllowance']
    for measurement in measurements:
        key = isa_variant_key(measurement.unit, measurement.label)
        entry = recorded.get(key)
        if entry is None:
            problems.append('%s has no recorded baseline on this host; record one before comparing'
                            % key)
            continue
        if (entry['objectBytes'] > 0
                and measurement.object_bytes > entry['objectBytes'] * object_allowance):
            problems.append('%s emits %d bytes of object; the baseline is %d and the allowance '
                            'is %.2fx' % (key, measurement.object_bytes, entry['objectBytes'],
                                          object_allowance))
        if (entry['minMilliseconds'] > 0.0
                and measurement.milliseconds > entry['minMilliseconds'] * wall_allowance):
            problems.append('%s took %.0f ms; the baseline is %.0f ms and the allowance is %.2fx'
                            % (key, measurement.milliseconds, entry['minMilliseconds'],
                               wall_allowance))
    return problems


def report_isa(measurements, out):
    """The per-instruction-set table, and what each extra subset cost over the one before it."""
    if not measurements:
        return
    print('object size per compiled instruction-set subset, at shipping optimization:', file=out)
    previous = {}
    for measurement in measurements:
        delta = ''
        if measurement.unit in previous:
            difference = measurement.object_bytes - previous[measurement.unit]
            delta = '  (%s%s bytes over the subset above)' % ('+' if difference >= 0 else '-',
                                                              format(abs(difference), ','))
        previous[measurement.unit] = measurement.object_bytes
        print('  %-64s %-10s %12s bytes, %7.0f ms%s'
              % (measurement.unit, measurement.label, format(measurement.object_bytes, ','),
                 measurement.milliseconds, delta), file=out)


def check_compile(measurements, baseline, allowances):
    """Compare against a baseline from the same host, with a separate allowance for each number.

    Wall time and preprocessed lines need different tolerances because they are noisy in different
    amounts. A short compilation on a busy machine varies by tens of per cent from run to run; the
    preprocessed line count for a given toolchain does not vary at all, so an allowance loose enough
    to absorb timing noise would let a header double in weight unnoticed.
    """
    problems = []
    recorded = baseline['units']
    wall_allowance = allowances['wallTimeAllowance']
    line_allowance = allowances['preprocessedLineAllowance']
    for measurement in measurements:
        if measurement.unit not in recorded:
            problems.append('%s has no recorded baseline on this host; record one before comparing'
                            % measurement.unit)
            continue
        entry = recorded[measurement.unit]
        if (entry['minMilliseconds'] > 0.0
                and measurement.milliseconds > entry['minMilliseconds'] * wall_allowance):
            problems.append('%s took %.0f ms; the baseline is %.0f ms and the allowance is %.2fx'
                            % (measurement.unit, measurement.milliseconds,
                               entry['minMilliseconds'], wall_allowance))
        if (entry['preprocessedLines'] > 0
                and measurement.preprocessed_lines > entry['preprocessedLines'] * line_allowance):
            problems.append('%s preprocesses to %d lines; the baseline is %d and the allowance '
                            'is %.2fx' % (measurement.unit, measurement.preprocessed_lines,
                                          entry['preprocessedLines'], line_allowance))
    return problems


def report_includes(measured, limits, out):
    print('include weight of the %d public header(s):' % measured['publicHeaders'], file=out)
    for key in sorted(measured['perHeader']):
        counts = measured['perHeader'][key]
        print('  %-52s %2d public, %2d standard' % (key, counts['sibling'], counts['standard']),
              file=out)
    print('  %-52s %2d distinct standard headers (budget %d)'
          % ('across the whole surface', measured['standardAcrossSurface'],
             limits['maxStandardIncludesAcrossSurface']), file=out)


def report_compile(measurements, compiler, out):
    print('compile time with %s, fastest of the repetitions:' % compiler, file=out)
    for measurement in measurements:
        print('  %-64s %7.0f ms, %7d preprocessed lines'
                  % (measurement.unit, measurement.milliseconds, measurement.preprocessed_lines),
              file=out)


def record_compile_baseline(existing, compiler, measurements, rebaseline=False):
    """Add missing compile units without silently refreshing retained evidence."""
    if existing is not None and not rebaseline and existing.get('compiler') != compiler:
        raise ValueError('the recorded compiler differs; use --rebaseline to replace this host baseline')
    units = {} if existing is None or rebaseline else dict(existing.get('units', {}))
    added = 0
    for measurement in measurements:
        if not rebaseline and measurement.unit in units:
            continue
        units[measurement.unit] = {
            'minMilliseconds': round(measurement.milliseconds, 1),
            'preprocessedLines': measurement.preprocessed_lines,
        }
        added += 1
    return {'compiler': compiler, 'units': units}, added


def record_isa_baseline(existing, compiler, measurements, rebaseline=False):
    """Add missing ISA variants without silently refreshing retained evidence."""
    if existing is not None and not rebaseline and existing.get('compiler') != compiler:
        raise ValueError('the recorded compiler differs; use --rebaseline to replace this host baseline')
    variants = {} if existing is None or rebaseline else dict(existing.get('variants', {}))
    added = 0
    for measurement in measurements:
        key = isa_variant_key(measurement.unit, measurement.label)
        if not rebaseline and key in variants:
            continue
        variants[key] = {
            'minMilliseconds': round(measurement.milliseconds, 1),
            'objectBytes': measurement.object_bytes,
        }
        added += 1
    return {'compiler': compiler, 'variants': variants}, added


def main(argv=None, stream=None, runner=None, environment=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('command', choices=('measure', 'check'))
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    parser.add_argument('--host-id', default=None,
                        help='the machine these compile times describe; without it, only the '
                             'include budgets are answered')
    parser.add_argument('--compiler', default=None, help='C++ compiler; defaults to CXX, then PATH')
    parser.add_argument('--includes-only', action='store_true',
                        help='explicitly skip compile-time measurement and check only include budgets')
    parser.add_argument('--repetitions', type=int, default=DEFAULT_REPETITIONS)
    parser.add_argument('--record', action='store_true',
                        help='add measurements that have no retained baseline to the budget file')
    parser.add_argument('--rebaseline', action='store_true',
                        help='with --record, explicitly replace retained host measurements')
    arguments = parser.parse_args(argv)
    if arguments.rebaseline and not arguments.record:
        parser.error('--rebaseline requires --record')
    out = stream if stream is not None else sys.stdout

    repo_root = pathlib.Path(arguments.repo_root).resolve()
    document = load_budget(repo_root)

    measured = measure_includes(repo_root)
    report_includes(measured, document['includes'], out)
    problems = check_includes(measured, document['includes'])

    compiler = None if arguments.includes_only else find_compiler(arguments.compiler, environment)
    measurements = []
    isa_measurements = []
    if arguments.includes_only:
        print('include-only mode requested; compile-time and object-size budgets were not '
              'measured.', file=out)
    elif compiler is None:
        print('no C++ compiler was found, so compile time was not measured. Name one with '
              '--compiler or CXX.', file=out)
        problems.append('no C++ compiler was found; use --includes-only only when compile budgets '
                        'are intentionally out of scope')
    else:
        try:
            measurements = measure_compile(repo_root, compiler, max(1, arguments.repetitions),
                                           runner, document['compile'].get('isaUnitFlags'),
                                           isa_unit_names(document))
            isa_measurements = measure_isa(repo_root, compiler, max(1, arguments.repetitions),
                                           document, runner)
        except RuntimeError as failure:
            # A compiler that cannot build these translation units is a finding about the engine or
            # about the toolchain, and either way it is worth one clear line rather than a
            # traceback that reads as a broken script.
            print(str(failure), file=out)
            print('compile time was not measured because a translation unit did not build.',
                  file=out)
            return 1

        report_compile(measurements, compiler_identity(compiler, runner), out)
        report_isa(isa_measurements, out)
        if arguments.host_id is None:
            print('no --host-id was given, so these times were not compared with anything. Wall '
                  'time is only meaningful against a baseline from the same machine, and so is '
                  'the object size beside it.', file=out)
        else:
            hosts = document['compile']['hosts']
            baseline = hosts.get(arguments.host_id)
            if baseline is None:
                problems.append('no compile-time baseline is recorded for host %s; measure with '
                                '--record to establish one' % arguments.host_id)
            else:
                problems.extend(check_compile(measurements, baseline, document['compile']))
            isa_hosts = document.get('isa', {}).get('hosts', {})
            isa_baseline = isa_hosts.get(arguments.host_id)
            if isa_measurements and isa_baseline is None:
                problems.append('no per-instruction-set baseline is recorded for host %s; measure '
                                'with --record to establish one' % arguments.host_id)
            elif isa_measurements:
                problems.extend(check_isa(isa_measurements, isa_baseline, document['isa']))

    if arguments.command == 'measure' and arguments.record:
        document['includes']['measured'] = measured
        if measurements and arguments.host_id:
            identity = compiler_identity(compiler, runner)
            existing = document['compile']['hosts'].get(arguments.host_id)
            try:
                recorded, added = record_compile_baseline(existing, identity, measurements, arguments.rebaseline)
            except ValueError as failure:
                print(str(failure), file=out)
                return 1
            document['compile']['hosts'][arguments.host_id] = recorded
            action = 'replaced' if arguments.rebaseline else 'added'
            print('%s %d compile-unit baseline(s); retained %d' %
                  (action, added, len(recorded['units']) - added), file=out)
        elif measurements:
            print('the compile times were not recorded: --record needs a --host-id to file them '
                  'under.', file=out)
        if isa_measurements and arguments.host_id:
            identity = compiler_identity(compiler, runner)
            existing = document['isa']['hosts'].get(arguments.host_id)
            try:
                recorded, added = record_isa_baseline(existing, identity, isa_measurements, arguments.rebaseline)
            except ValueError as failure:
                print(str(failure), file=out)
                return 1
            document['isa']['hosts'][arguments.host_id] = recorded
            action = 'replaced' if arguments.rebaseline else 'added'
            print('%s %d ISA baseline(s); retained %d' %
                  (action, added, len(recorded['variants']) - added), file=out)
        save_budget(repo_root, document)
        print('recorded into %s' % BUDGET_FILE, file=out)
        return 0

    if arguments.command == 'check' and problems:
        for problem in problems:
            print(problem, file=out)
        print('%d budget problem(s)' % len(problems), file=out)
        return 1
    if problems:
        for problem in problems:
            print(problem, file=out)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

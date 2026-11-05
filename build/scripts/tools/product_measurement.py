#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The product measurement: archive a product build, run the paired protocol, compare, and check.

It compares the product's ImageIntegration workflow in two deployments - a baseline and a candidate, such as a build
of the commit a change starts from and a build of the change - on the same pinned dataset, in separate processes and
output roots, and never against a baseline rebuilt from changed sources. That needs three things this tool provides:

- `archive` copies a product build of a clean commit into a directory of its own: exactly what the Windows or Linux
  packager would ship, plus the measurement driver, every library it loads and the first-party symbols, with a
  manifest of every file's SHA-256, the commit, the toolchain the binaries were linked by, the vcpkg checkout and the
  pinned configuration. `verify-archive` holds an archive to its manifest.
- `run` launches the driver `AfterNightWorkflowDriver` from a baseline and a candidate deployment in randomized
  pairs - each pair one run of each side in a seeded random order - after common warm-ups whose results are
  discarded, for one workflow, full or Lite, and one case: `cold` evicts every input from the operating system's cache
  before each run, `warm` leaves them cached by the warm-ups, `incremental` primes each side's own output once and then
  alternates a setting of the light stack alone - the full process's high rejection sigma, Lite's weighting - so that
  every measured run re-stacks and reuses everything before it, and `cancel` requests cancellation at a pinned point and keeps the latency. Every run starts from a fresh output
  root except the incremental case's; each side keeps its own. The children run with only their deployment and the
  system directories on the loader path, and the host's load is read before each pair.
- `compare` applies the paired decision rules to the pairs - an exact one-sided paired sign test at the
  configured significance and a no-regression ratio of 1.0 for a noisy series, the median ratio for a quiet one - to
  the wall time from the accepted request to the finished signal and to the resident peak, and holds the outputs to
  their contract before a timing is accepted. A quiet series is a regression only past the host's resolution: twice
  the largest excursion of a median ratio from 1 that the host's recorded baseline, the product against itself,
  showed, because below it two identical products differ by chance; a host with no recorded baseline is decided at
  1.0. A second batch is analysed together with the first. The cancellation case's latency is reported, never decided
  with throughput.

`run` and `campaign` measure one of two paths. `--path driver`, the default, launches the workflow driver above;
`--path application` launches each deployment's application and drives it through its own windows with
`product_application_driver.ps1` on Windows and `product_application_driver_linux.py` on Linux - the Process menu, the
integration window, its folder dialogs, its progress dialog - so the region is the one a person sees and the resident
peak includes Qt, Python and the codecs. The application path stages the pinned frame set as hard links under
`--frames`, one folder per role, which is what the windows add, and gives every run a storage root of its own. Each path
is decided against its own resolution: a recorded baseline states the path it measured.

`campaign` runs a host's pinned series at the host's pairs and refuses one whose estimate - each side's run seconds
the host states and what each path adds per run - exceeds the budget, which is under an hour; `run` refuses more pairs
or warm-ups than the protocol unless asked.

`check`, in the common check list, holds `build/scripts/tools/product_measurement.json` - the pinned dataset, the
workflows and cases, the protocol, the output contract, the campaigns and the hosts - to the protocol this tool
states, and every recorded baseline it lists to the dataset it pins, to a commit of this repository and to the
resolution its records give, each record a file of this repository.

Usage:
    python build/scripts/tools/product_measurement.py archive --destination <new dir> --host-id <host>
                                                             [--configuration release] [--allow-dirty]
    python build/scripts/tools/product_measurement.py verify-archive <archive dir>
    python build/scripts/tools/product_measurement.py run --baseline <dir> --candidate <dir> --workflow full|lite
                                                         --case cold|warm|incremental|cancel --host-id <host>
                                                         --corpus-root <dir> --output <new dir> [--pairs N]
                                                         [--warmups N] [--seed N]
                                                         [--keep-outputs] [--over-budget]
                                                         [--path driver|application] [--frames <dir>]
    python build/scripts/tools/product_measurement.py campaign --baseline <dir> --candidate <dir> --host-id <host>
                                                              --corpus-root <dir> --output <new dir> [--work <dir>]
                                                              [--path driver|application] [--frames <dir>]
                                                              [--dry-run]
    python build/scripts/tools/product_measurement.py compare <run dir> [<second batch dir>] [--output <file>]
    python build/scripts/tools/product_measurement.py check [--repo-root <dir>]

Tests: build/scripts/tools/product_measurement_test.py
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import importlib.util
import json
import math
import os
import pathlib
import platform
import random
import shutil
import statistics
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
CONFIGURATION = 'build/scripts/tools/product_measurement.json'
DRIVER = 'AfterNightWorkflowDriver'
# The two paths a run takes through the product: the workflow driver, or the application's own windows.
PATHS = ('driver', 'application')
# The script that drives a deployment's application through its windows, per platform.
APPLICATION_DRIVER = {'windows': 'build/scripts/tools/product_application_driver.ps1',
                      'linux': 'build/scripts/tools/product_application_driver_linux.py'}
# The Linux driver runs under the system's Python, which carries PyGObject and the AT-SPI client library; the Python
# running this tool is the project's, which carries numpy for the comparison and neither of those.
SYSTEM_PYTHON = '/usr/bin/python3'
# The folder each role's frames are staged in for the application's windows to add, which the windows read as the role.
FRAME_FOLDERS = {'lights': 'LIGHTS', 'flats': 'FLAT', 'darks': 'DARK', 'bias': 'BIAS'}
WORKFLOWS = ('full', 'lite')
CASES = ('cold', 'warm', 'incremental', 'cancel')
SIDES = ('baseline', 'candidate')
# The application each packager ships. The AppImage packager copies it beside the libraries by a rule of its own and
# excludes it from the copy of everything else, so its payload rule alone would leave it out.
APPLICATION = {'windows': 'AfterNight.exe', 'linux': 'AfterNight'}
ARCHIVE_MANIFEST = 'archive-manifest.json'

# The protocol's values, which the pinned configuration is held to: three measured pairs per host and case and one
# extra batch of the same size for an inconclusive result, no discarded warm-up, the no-regression ratio, the factor
# a host's resolution is taken at, the dispersion past which a series is noisy, the sign test's significance, and how
# often the resident set is sampled.
PROTOCOL = {'pairs': 3, 'extraBatch': 3, 'warmups': 0, 'noRegressionRatio': 1.0, 'resolutionFactor': 2.0,
            'noiseThreshold': 0.05, 'pairedSignificance': 0.05, 'samplingIntervalMs': 10}
# The metrics a comparison decides, and so the ones a host's resolution is stated for.
METRICS = ('wallTime', 'residentPeak')


class MeasurementError(Exception):
    """The measurement cannot be made as asked."""


def host_platform() -> str:
    return 'windows' if os.name == 'nt' else 'linux'


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(8 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def read_configuration(repo_root: pathlib.Path = REPO_ROOT) -> dict:
    return json.loads((pathlib.Path(repo_root) / CONFIGURATION).read_text(encoding='utf-8'))


def _git(repo_root: pathlib.Path, *arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(['git', '-C', str(repo_root), *arguments], capture_output=True, text=True, check=False)


# ---- The pinned configuration and the recorded baselines.

def configuration_findings(configuration: dict) -> list[str]:
    problems = []
    dataset = configuration.get('dataset') or {}
    for key in ('frameSet', 'corpusManifest', 'corpusManifestSha256', 'identityDigest', 'frames', 'bytes', 'roles'):
        if key not in dataset:
            problems.append(f'the pinned dataset states no {key}')
    for key in ('corpusManifestSha256', 'identityDigest'):
        value = str(dataset.get(key, ''))
        if value and (len(value) != 64 or any(ch not in '0123456789abcdef' for ch in value)):
            problems.append(f'the pinned dataset {key} is not a SHA-256')
    roles = dataset.get('roles') or {}
    if roles and sum(roles.values()) != dataset.get('frames'):
        problems.append('the pinned dataset roles do not add up to its frame count')
    workflows = configuration.get('workflows') or {}
    if sorted(workflows) != sorted(WORKFLOWS):
        problems.append(f'the workflows must be exactly {", ".join(WORKFLOWS)}')
    cases = configuration.get('cases') or {}
    if sorted(cases) != sorted(CASES):
        problems.append(f'the cases must be exactly {", ".join(CASES)}')
    cancel = cases.get('cancel') or {}
    if not isinstance(cancel.get('cancelAfterMs'), int) or cancel.get('cancelAfterMs', 0) <= 0:
        problems.append('the cancel case states no cancelAfterMs')
    protocol = configuration.get('protocol') or {}
    for key, value in PROTOCOL.items():
        if protocol.get(key) != value:
            problems.append(f'the protocol {key} is {protocol.get(key)!r}, not the {value!r} this tool states')
    if not isinstance(protocol.get('seed'), int):
        problems.append('the protocol states no seed')
    hosts = configuration.get('hosts') or {}
    for host in ('bench-windows', 'bench-linux'):
        if host not in hosts:
            problems.append(f'no host entry for {host}')
    return problems + output_contract_findings(configuration) + campaign_findings(configuration)


# ---- The campaign's budget.
#
# A campaign is the series a host runs for one measurement: a workflow and a case each, at the host's number of pairs.
# Its length is estimated from the seconds a run of each side and kind takes on the host, which the configuration
# states, and a campaign whose estimate exceeds the budget is refused before a run starts, so no campaign is planned
# longer than the maintainer agreed to give one.

RUN_KINDS = ('fresh', 'incremental', 'cancel')
# What the runner adds per run outside the driver: starting the child, reading the results, comparing the images.
RUN_OVERHEAD_SECONDS = 5.0
# What the application path adds per run outside its region: starting the application, opening the window, adding the
# frames through its dialogs, stopping it and hashing and comparing its images - about 30 seconds on bench-windows.
APPLICATION_RUN_OVERHEAD_SECONDS = 35.0
# What evicting the pinned frame set from the cache before a cold run costs, as measured.
EVICTION_SECONDS = 2.0


def run_kind(case: str) -> str:
    return {'cold': 'fresh', 'warm': 'fresh', 'incremental': 'incremental', 'cancel': 'cancel'}[case]


def series_runs(case: str, pairs: int, warmups: int) -> list[tuple[str, str]]:
    """(side, kind) of every run a series makes: an incremental case's two primes, the warm-up rounds, the pairs."""
    runs = [(side, 'fresh') for side in SIDES] if case == 'incremental' else []
    runs += [(side, run_kind(case)) for _ in range(warmups + pairs) for side in SIDES]
    return runs


def campaign_estimate(configuration: dict, host: str, path: str = 'driver') -> dict:
    """The seconds each series of a host's campaign is expected to take by a path, and their total."""
    campaign = (configuration.get('campaign') or {}).get('hosts', {}).get(host) or {}
    seconds = ((configuration.get('hosts') or {}).get(host) or {}).get('runSeconds') or {}
    warmups = configuration['protocol']['warmups']
    overhead = APPLICATION_RUN_OVERHEAD_SECONDS if path == 'application' else RUN_OVERHEAD_SECONDS
    estimate = {}
    for series in campaign.get('series') or []:
        _, case = series.split('-', 1)
        total = 0.0
        for side, kind in series_runs(case, campaign.get('pairs', 0), warmups):
            total += float((seconds.get(side) or {}).get(kind, 0.0)) + overhead
            total += EVICTION_SECONDS if case == 'cold' else 0.0
        estimate[series] = round(total, 1)
    return {'path': path, 'series': estimate, 'totalSeconds': round(sum(estimate.values()), 1)}


def campaign_findings(configuration: dict) -> list[str]:
    """Every host states a campaign the budget holds: known series, at least two pairs and no more than the
    protocol's, the seconds each side's runs take, and an estimate inside the budget."""
    campaign = configuration.get('campaign')
    if not isinstance(campaign, dict):
        return ['the configuration states no campaign']
    problems = []
    budget = campaign.get('budgetMinutes')
    if not isinstance(budget, (int, float)) or not 0 < budget <= 60:
        problems.append('the campaign budget is not a number of minutes up to an hour')
        budget = 0
    protocol_pairs = (configuration.get('protocol') or {}).get('pairs', PROTOCOL['pairs'])
    for host in sorted((configuration.get('hosts') or {})):
        entry = (campaign.get('hosts') or {}).get(host)
        if not isinstance(entry, dict):
            problems.append(f'the campaign states nothing for {host}')
            continue
        pairs = entry.get('pairs')
        if not isinstance(pairs, int) or not 2 <= pairs <= protocol_pairs:
            problems.append(f'the {host} campaign runs {pairs!r} pairs, not two to the protocol\'s {protocol_pairs}')
        for series in entry.get('series') or []:
            workflow, _, case = series.partition('-')
            if workflow not in WORKFLOWS or case not in CASES:
                problems.append(f'the {host} campaign names {series}, which is no workflow and case')
        if not entry.get('series'):
            problems.append(f'the {host} campaign runs no series')
        seconds = ((configuration.get('hosts') or {}).get(host) or {}).get('runSeconds') or {}
        for side in SIDES:
            for kind in RUN_KINDS:
                value = (seconds.get(side) or {}).get(kind)
                if not isinstance(value, (int, float)) or value <= 0:
                    problems.append(f'{host} states no {side} {kind} run seconds')
        if problems:
            continue
        for path in PATHS:
            total = campaign_estimate(configuration, host, path)['totalSeconds']
            if total > budget * 60:
                problems.append(f'the {host} campaign by the {path} path is estimated at {total / 60:.1f} minutes, '
                                f'over its budget of {budget} minutes')
    return problems


# The images a reference run publishes, each of which the output contract must bound across the sides.
PUBLISHED_ROLES = ('bias-master', 'dark-master', 'flat-master', 'light-master')
ACROSS_BOUNDS = ('maxAbsoluteLocationShiftInNoise', 'noiseRatio', 'maxMedianAbsoluteDifferenceInNoise',
                 'maxP99AbsoluteDifferenceInNoise', 'minCorrelation')


def output_contract_findings(configuration: dict) -> list[str]:
    """The output contract states a bound for every published role across the sides, and every tolerance it allows
    within a side is a count of units in the last place and a share of the samples."""
    contract = configuration.get('outputContract')
    if not isinstance(contract, dict):
        return ['the configuration states no output contract']
    problems = []
    for role, bound in sorted((contract.get('withinSide') or {}).items()):
        if not isinstance(bound.get('maxUlp'), int) or bound['maxUlp'] < 0:
            problems.append(f'the {role} maxUlp within a side is not a count of units in the last place')
        share = bound.get('maxDifferingShare')
        if not isinstance(share, (int, float)) or not 0.0 <= share <= 1.0:
            problems.append(f'the {role} maxDifferingShare within a side is not a share of the samples')
    across = contract.get('acrossSides') or {}
    for role in PUBLISHED_ROLES:
        if role not in across:
            problems.append(f'the output contract states no accepted difference for {role}')
    for role, bound in sorted(across.items()):
        for key in ACROSS_BOUNDS:
            if key not in bound:
                problems.append(f'the {role} bound states no {key}')
        ratio = bound.get('noiseRatio')
        if 'noiseRatio' in bound and not (isinstance(ratio, list) and len(ratio) == 2 and ratio[0] <= 1.0 <= ratio[1]):
            problems.append(f'the {role} noiseRatio is not a range around one')
        if bound.get('grid', 'same') not in ('same', 'any'):
            problems.append(f'the {role} grid is neither same nor any')
    return problems


def _recorded_text(repo_root: pathlib.Path, reference: str) -> tuple[str | None, str | None]:
    """A baseline's evidence, a file of this repository: its text, or a problem when it is not there."""
    path = pathlib.Path(repo_root) / reference
    if not path.is_file():
        return None, f'{reference} does not exist'
    return path.read_text(encoding='utf-8'), None


def baseline_findings(repo_root: pathlib.Path, configuration: dict) -> list[str]:
    """Every recorded baseline names the pinned dataset, a commit of this repository that HEAD descends from, and its
    evidence - the archive's manifest and the comparison records it was measured in, each a file of this repository -
    and states the resolution those records give."""
    problems = []
    dataset = configuration.get('dataset') or {}
    head = _git(repo_root, 'rev-parse', 'HEAD').stdout.strip()
    for entry in configuration.get('baselines') or []:
        label = f'the {entry.get("host")} {entry.get("path", "driver")} baseline'
        for key in ('host', 'commit', 'archiveManifest', 'records'):
            if key not in entry:
                problems.append(f'{label} states no {key}')
        if entry.get('path', 'driver') not in PATHS:
            problems.append(f'{label} names a path that is neither {" nor ".join(PATHS)}')
        commit = entry.get('commit', '')
        if commit and _git(repo_root, 'cat-file', '-e', f'{commit}^{{commit}}').returncode != 0:
            problems.append(f'{label} names {commit}, which is not a commit of this repository')
        elif commit and head and _git(repo_root, 'merge-base', '--is-ancestor', commit, head).returncode != 0:
            problems.append(f'{label} names {commit}, which is not an ancestor of HEAD')
        if entry.get('archiveManifest'):
            text, problem = _recorded_text(repo_root, entry['archiveManifest'])
            if problem:
                problems.append(f'{label}: {problem}')
            else:
                manifest = json.loads(text)
                if manifest.get('commit') != entry.get('commit', ''):
                    problems.append(f'{label}: the archive manifest names another commit')
                if (manifest.get('dataset') or {}).get('identityDigest') != dataset.get('identityDigest'):
                    problems.append(f'{label}: the archive pins another dataset')
        comparisons = []
        for record in entry.get('records') or []:
            text, problem = _recorded_text(repo_root, record)
            if problem:
                problems.append(f'{label}: {problem}')
                continue
            comparison = json.loads(text)
            comparisons.append(comparison)
            if comparison.get('datasetIdentity') != dataset.get('identityDigest'):
                problems.append(f'{label}: {record} measured another dataset')
            if comparison.get('pairs', 0) < PROTOCOL['pairs']:
                problems.append(f'{label}: {record} has fewer than {PROTOCOL["pairs"]} pairs')
        stated = entry.get('resolution')
        if not isinstance(stated, dict):
            problems.append(f'{label} states no resolution')
        else:
            derived = baseline_resolution(comparisons, PROTOCOL['resolutionFactor'])
            for metric in METRICS:
                if metric not in derived:
                    continue
                if not isinstance(stated.get(metric), (int, float)) or abs(stated[metric] - derived[metric]) > 1e-9:
                    problems.append(f'{label} states a {metric} resolution of {stated.get(metric)!r}; its records give '
                                    f'{derived[metric]:g}')
    return problems


def baseline_resolution(comparisons: list[dict], factor: float) -> dict:
    """A host's resolution per metric: the ratio below which a quiet difference is its measurement's noise.

    It is `factor` times the largest excursion of a median ratio from 1 in the host's baseline - the product measured
    against itself - to six decimal places. The excursion is read in both directions, because the noise that moves an
    identical product's median above its twin's moves it below as often.
    """
    resolution = {}
    for metric in METRICS:
        ratios = [comparison[metric]['medianRatio'] for comparison in comparisons
                  if isinstance(comparison.get(metric), dict)
                  and isinstance(comparison[metric].get('medianRatio'), (int, float))]
        if ratios:
            resolution[metric] = round(1.0 + factor * max(abs(ratio - 1.0) for ratio in ratios), 6)
    return resolution


def host_resolution(configuration: dict, host: str, path: str = 'driver') -> dict:
    """The resolution a host's recorded baseline of a path states, with where it came from; 1.0 when it has none.

    A baseline that names no path measured the driver's."""
    limit = configuration['protocol']['noRegressionRatio']
    for entry in configuration.get('baselines') or []:
        if entry.get('host') == host and entry.get('path', 'driver') == path \
                and isinstance(entry.get('resolution'), dict):
            return {**{metric: entry['resolution'].get(metric, limit) for metric in METRICS},
                    'source': f'the {host} {path} baseline at {str(entry.get("commit", ""))[:9]}, '
                              f'{configuration["protocol"]["resolutionFactor"]:g} times its largest excursion'}
    return {**{metric: limit for metric in METRICS},
            'source': f'no recorded {path} baseline for {host}, so the no-regression ratio'}


def check(repo_root: pathlib.Path = REPO_ROOT) -> list[str]:
    path = pathlib.Path(repo_root) / CONFIGURATION
    if not path.is_file():
        return [f'{CONFIGURATION} does not exist']
    try:
        configuration = json.loads(path.read_text(encoding='utf-8'))
    except json.JSONDecodeError as error:
        return [f'{CONFIGURATION} is not JSON: {error}']
    return configuration_findings(configuration) + baseline_findings(pathlib.Path(repo_root), configuration)


# ---- The archive.

def pe_linker_version(path: pathlib.Path) -> str | None:
    """The linker version a PE binary records in its optional header, or None for another format."""
    try:
        with open(path, 'rb') as handle:
            header = handle.read(4096)
        if header[:2] != b'MZ':
            return None
        offset = struct.unpack_from('<I', header, 0x3C)[0]
        if header[offset:offset + 4] != b'PE\0\0':
            return None
        major, minor = header[offset + 24 + 2], header[offset + 24 + 3]
        return f'{major}.{minor}'
    except (OSError, struct.error, IndexError):
        return None


def elf_comment_strings(path: pathlib.Path) -> list[str]:
    """The strings an ELF binary's `.comment` section holds - the compilers its objects were built by - or an empty
    list for another format."""
    try:
        with open(path, 'rb') as handle:
            header = handle.read(64)
            if len(header) < 64 or header[:4] != b'\x7fELF' or header[4] != 2 or header[5] != 1:
                return []
            table_offset = struct.unpack_from('<Q', header, 0x28)[0]
            entry_size, count, names_index = struct.unpack_from('<HHH', header, 0x3A)
            handle.seek(table_offset)
            table = handle.read(entry_size * count)

            def section(index: int) -> tuple[int, int, int]:
                name, _, _, _, offset, size = struct.unpack_from('<IIQQQQ', table, index * entry_size)
                return name, offset, size

            _, names_offset, names_size = section(names_index)
            handle.seek(names_offset)
            names = handle.read(names_size)
            for index in range(count):
                name, offset, size = section(index)
                if names[name:names.index(b'\0', name)] == b'.comment':
                    handle.seek(offset)
                    return [text.decode('utf-8', 'replace') for text in handle.read(size).split(b'\0') if text]
    except (OSError, struct.error, ValueError):
        return []
    return []


def first_party(name: str) -> bool:
    """Whether a deployed file is one this repository builds: the product's binaries and the runtime backend."""
    return name.startswith(('AfterNight', 'libAfterNight', 'an_', 'liban_'))


def binary_toolchain(paths: list[pathlib.Path]) -> dict:
    """What the first-party binaries say built them: the linker a PE header records, the compilers an ELF comment
    section names."""
    linkers, compilers = set(), set()
    for path in paths:
        if not first_party(path.name) or path.suffix.lower() in ('.pdb', '.debug'):
            continue
        if path.suffix.lower() in ('.exe', '.dll'):
            version = pe_linker_version(path)
            if version:
                linkers.add(version)
        else:
            compilers.update(elf_comment_strings(path))
    return {'linker': sorted(linkers), 'compilers': sorted(compilers)}


def qt_version(common_lua: str) -> str:
    """The Qt version `afternight-common.lua` assigns, the one place the build states it."""
    import re
    match = re.search(r'^\s*(?:local\s+)?(?:\w+\.)?QT_VERSION\s*=\s*"([^"]+)"', common_lua, re.MULTILINE)
    return match.group(1) if match else ''


def payload_rule(repo_root: pathlib.Path, platform_name: str):
    """The packager's rule for this platform, as check_installer_payload.py reconstructs it."""
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import check_installer_payload as payload  # noqa: E402 - the reconstruction the payload check uses
    relative = payload.WINDOWS_PACKAGER if platform_name == 'windows' else payload.LINUX_PACKAGER
    parse = payload.parse_windows_packager if platform_name == 'windows' else payload.parse_linux_packager
    rule = parse((pathlib.Path(repo_root) / relative).read_text(encoding='utf-8'))
    if rule is None:
        raise MeasurementError(f'{relative} has no payload rule')
    return rule


def import_closure(start: pathlib.Path, binaries: pathlib.Path, read_imports, platform_name: str) -> list[pathlib.Path]:
    """Every library of the deployment directory that `start` loads, directly or through another, each read once.

    A name is matched as the platform's loader matches it - without regard to case on Windows - against the directory
    and, on Linux, its `lib` subdirectory; a name the directory does not hold is the system's.
    """
    def key(name: str) -> str:
        return name.lower() if platform_name == 'windows' else name
    index: dict[str, pathlib.Path] = {}
    for base in (binaries, binaries / 'lib'):
        if base.is_dir():
            for path in sorted(base.iterdir()):
                if path.is_file():
                    index.setdefault(key(path.name), path)
    reached: dict[str, pathlib.Path] = {}
    frontier = [start]
    while frontier:
        current = frontier.pop()
        if key(current.name) in reached:
            continue
        reached[key(current.name)] = current
        for name in read_imports(current) or ():
            found = index.get(key(name))
            if found is not None and key(found.name) not in reached:
                frontier.append(found)
    return sorted(path for path in reached.values() if path != start)


def engine_library(names) -> str | None:
    """The engine's library among a set of deployed file names, by its file name, or None when it is not there."""
    for name in sorted(pathlib.PurePath(name).name for name in names):
        lowered = name.lower()
        if lowered.startswith(('afternightengine.', 'libafternightengine.')) \
                and not lowered.endswith(('.pdb', '.debug')):
            return name
    return None


def binary_imports(path: pathlib.Path):
    """The libraries a binary names for its loader: read from a PE's import tables in Python, from an ELF's needed
    entries through llvm-readobj."""
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    if host_platform() == 'windows':
        import deploy_raw_deps  # noqa: E402 - the deployment's own PE import reader
        try:
            return sorted(deploy_raw_deps.pe_imports(path))
        except ValueError:
            return []
    import active_targets  # noqa: E402 - the active set's reader of the same tables
    import binary_imports  # noqa: E402
    reader = active_targets.find_reader(None)
    if reader is None:
        raise MeasurementError('no llvm-readobj on this host, so the driver\'s import closure cannot be read')
    return binary_imports.read_imports(reader, path) or []


def archive_selection(binaries: pathlib.Path, rule, platform_name: str, read_imports=None) -> list[pathlib.Path]:
    """What the archive holds: the packager's payload with its application, the driver and every library it loads
    from the deployment, and the first-party symbols of all of them.

    The driver's import closure is read only when a reader is given. The packager's payload is what the application
    needs, and the driver loads libraries the payload may leave out; the archive needs them for the driver to run.
    """
    driver_names = {DRIVER + ('.exe' if platform_name == 'windows' else ''), DRIVER + '.pdb', DRIVER + '.debug'}
    application = binaries / APPLICATION[platform_name]
    selected = []
    for path in sorted(binaries.rglob('*')):
        if not path.is_file():
            continue
        if path.name in driver_names or path == application or not rule.is_excluded(path.name):
            selected.append(path)
    driver = binaries / (DRIVER + ('.exe' if platform_name == 'windows' else ''))
    if read_imports is not None and driver.is_file():
        for path in import_closure(driver, binaries, read_imports, platform_name):
            if path not in selected:
                selected.append(path)
    # A PDB is named for its binary's stem, a Linux symbol file for its whole name: libAfterNightControls.so.debug.
    for path in list(selected):
        if not first_party(path.name) or path.suffix.lower() in ('.pdb', '.debug'):
            continue
        symbols = path.with_name(path.stem + '.pdb' if platform_name == 'windows' else path.name + '.debug')
        if symbols.is_file() and symbols not in selected:
            selected.append(symbols)
    return selected


def create_archive(repo_root: pathlib.Path, destination: pathlib.Path, host_id: str, configuration_name: str = 'release',
                   allow_dirty: bool = False) -> dict:
    repo_root = pathlib.Path(repo_root)
    destination = pathlib.Path(destination)
    platform_name = host_platform()
    toolchain = 'msvc' if platform_name == 'windows' else 'clang'
    binaries = repo_root / 'bin' / toolchain / configuration_name
    driver = binaries / (DRIVER + ('.exe' if platform_name == 'windows' else ''))
    if not driver.is_file():
        raise MeasurementError(f'{driver} does not exist: build the driver before archiving')
    status = _git(repo_root, 'status', '--porcelain', '--untracked-files=no').stdout.strip()
    if status and not allow_dirty:
        raise MeasurementError('the checkout has uncommitted changes; an archive is made from a clean commit')
    if destination.exists() and any(destination.iterdir()):
        raise MeasurementError(f'{destination} exists and is not empty')
    configuration = read_configuration(repo_root)
    rule = payload_rule(repo_root, platform_name)
    selected = archive_selection(binaries, rule, platform_name, binary_imports)
    engine = engine_library(path.name for path in import_closure(driver, binaries, binary_imports, platform_name))
    files = []
    for path in selected:
        relative = path.relative_to(binaries)
        target = destination / 'bin' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        files.append({'path': 'bin/' + relative.as_posix(), 'bytes': target.stat().st_size, 'sha256': file_sha256(target)})
    shutil.copy2(repo_root / CONFIGURATION, destination / pathlib.Path(CONFIGURATION).name)
    commit = _git(repo_root, 'rev-parse', 'HEAD').stdout.strip()
    built_by = binary_toolchain(selected)
    qt = qt_version((repo_root / 'build/premake/afternight-common.lua').read_text(encoding='utf-8'))
    manifest = {
        'schema': 1,
        'kind': 'workflow driver deployment',
        'engine': engine,
        'commit': commit,
        'clean': not status,
        'host': host_id,
        'platform': platform_name,
        'configuration': configuration_name,
        'created': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
        'toolchain': {'linker': built_by['linker'], 'compilers': built_by['compilers'], 'qt': qt,
                      'python': platform.python_version(),
                      'vcpkg': _git(repo_root / 'vcpkg', 'rev-parse', 'HEAD').stdout.strip()},
        'dataset': configuration['dataset'],
        'configurationSha256': file_sha256(repo_root / CONFIGURATION),
        'selection': ('the packager payload of ' + ('installer/win_installer.iss' if platform_name == 'windows'
                                                    else 'installer/linux_packager.sh') +
                      ', the measurement driver and every library it loads from the deployment, and the symbols of '
                      'the first-party binaries'),
        'reproduce': ['git checkout --detach ' + commit, 'the numbered 01 to 05 scripts in Release',
                      'msbuild tests/AfterNightTest.slnx /t:AfterNightWorkflowDriver (make on Linux)',
                      'python build/scripts/tools/product_measurement.py archive --destination <dir> --host-id <host>'],
        'files': files,
        'totalBytes': sum(entry['bytes'] for entry in files),
    }
    (destination / ARCHIVE_MANIFEST).write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def verify_archive(directory: pathlib.Path) -> list[str]:
    directory = pathlib.Path(directory)
    manifest_path = directory / ARCHIVE_MANIFEST
    if not manifest_path.is_file():
        return [f'{manifest_path} does not exist']
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    problems = []
    recorded = {entry['path']: entry for entry in manifest.get('files', [])}
    for relative, entry in sorted(recorded.items()):
        path = directory / relative
        if not path.is_file():
            problems.append(f'{relative} is missing')
        elif path.stat().st_size != entry['bytes'] or file_sha256(path) != entry['sha256']:
            problems.append(f'{relative} differs from its record')
    present = {path.relative_to(directory).as_posix() for path in (directory / 'bin').rglob('*') if path.is_file()} \
        if (directory / 'bin').is_dir() else set()
    for extra in sorted(present - set(recorded)):
        problems.append(f'{extra} is not in the manifest')
    return problems


# ---- The paired run.

def build_schedule(pairs: int, seed: int) -> list[tuple[str, str]]:
    """One pair per round, its two sides in a seeded random order: never a block of one side."""
    generator = random.Random(seed)
    schedule = []
    for _ in range(pairs):
        order = list(SIDES)
        generator.shuffle(order)
        schedule.append((order[0], order[1]))
    return schedule


def driver_path(directory: pathlib.Path, path: str = 'driver') -> pathlib.Path:
    """The binary a deployment runs by a path: its workflow driver, or its application."""
    name = DRIVER + ('.exe' if host_platform() == 'windows' else '') if path == 'driver' \
        else APPLICATION[host_platform()]
    for candidate in (pathlib.Path(directory) / 'bin' / name, pathlib.Path(directory) / name):
        if candidate.is_file():
            return candidate
    raise MeasurementError(f'no {name} under {directory}')


def require_numpy() -> None:
    """Refuses a run before its first pair when the comparison could not read its images."""
    if importlib.util.find_spec('numpy') is None:
        raise MeasurementError('the outputs are compared through numpy, which this Python does not have: '
                               'python -m pip install numpy')


def stage_frames(frame_set: dict, corpus_root: pathlib.Path, frames: pathlib.Path) -> dict[str, int]:
    """Stages the pinned frame set for the application's windows: each role's frames as hard links in the role's
    folder under `frames`, nothing else beside them, so a window that adds a folder adds exactly the role's frames and
    reads the corpus's own files. A link that is another file, or anything else under `frames`, is refused rather than
    replaced."""
    frames = pathlib.Path(frames)
    frames.mkdir(parents=True, exist_ok=True)
    counts = {}
    for role in frame_set['roles']:
        folder = FRAME_FOLDERS.get(role['role'])
        if folder is None:
            raise MeasurementError(f'the frame set has a role {role["role"]} no window adds')
        directory = frames / folder
        directory.mkdir(exist_ok=True)
        expected = set()
        for frame in role['frames']:
            source = pathlib.Path(corpus_root) / frame['relativePath']
            staged = directory / source.name
            expected.add(staged.name)
            if staged.exists():
                if not os.path.samefile(staged, source):
                    raise MeasurementError(f'{staged} is not {source}')
                continue
            try:
                os.link(source, staged)
            except OSError as error:
                raise MeasurementError(f'{source} cannot be linked under {frames}, which must be on the corpus\'s '
                                       f'volume: {error}') from error
        foreign = sorted(entry.name for entry in directory.iterdir() if entry.name not in expected)
        if foreign:
            raise MeasurementError(f'{directory} holds {", ".join(foreign)}, which the frame set does not')
        counts[folder] = len(expected)
    foreign = sorted(entry.name for entry in frames.iterdir() if entry.name not in counts)
    if foreign:
        raise MeasurementError(f'{frames} holds {", ".join(foreign)} beside the role folders')
    return counts


def application_command(script: pathlib.Path, application: pathlib.Path, workflow: str, frames: pathlib.Path,
                        output: pathlib.Path, storage: pathlib.Path, result: pathlib.Path, variant: int,
                        cancel_after_ms: int, sampling_interval_ms: int, identity: str) -> list[str]:
    """The command that drives one run through a deployment's application: Windows PowerShell, which carries the UI
    Automation the Windows driver uses, or the system's Python, which carries the AT-SPI client the Linux driver uses."""
    if host_platform() != 'windows':
        return [SYSTEM_PYTHON, str(script), '--application', str(application), '--workflow', workflow,
                '--frames', str(frames), '--output', str(output), '--storage', str(storage), '--result', str(result),
                '--variant', str(variant), '--cancel-after-ms', str(cancel_after_ms),
                '--sampling-interval-ms', str(sampling_interval_ms), '--dataset-identity', identity]
    system = os.environ.get('SystemRoot', r'C:\Windows')
    return [system + r'\System32\WindowsPowerShell\v1.0\powershell.exe', '-NoProfile', '-NonInteractive',
            '-ExecutionPolicy', 'Bypass', '-File', str(script), '-Application', str(application), '-Workflow', workflow,
            '-Frames', str(frames), '-Output', str(output), '-Storage', str(storage), '-Result', str(result),
            '-Variant', str(variant), '-CancelAfterMs', str(cancel_after_ms),
            '-SamplingIntervalMs', str(sampling_interval_ms), '-DatasetIdentity', identity]


def child_environment(binaries: pathlib.Path) -> dict:
    """Only the deployment and the system on the loader path, so no development output can satisfy a child."""
    env = dict(os.environ)
    for variable in ('AFTERNIGHT_TEST_STORAGE_ROOT', 'QT_PLUGIN_PATH'):
        env.pop(variable, None)
    if host_platform() == 'windows':
        system = os.environ.get('SystemRoot', r'C:\Windows')
        env['PATH'] = os.pathsep.join([str(binaries), system + r'\System32', system])
    else:
        env['LD_LIBRARY_PATH'] = os.pathsep.join([str(binaries), str(binaries / 'lib')])
    return env


def evict_from_cache(paths: list[pathlib.Path]) -> float:
    """Drops each file's pages from the operating system's cache; returns the seconds it took."""
    started = time.monotonic()
    if host_platform() == 'windows':
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        create = kernel32.CreateFileW
        create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                           wintypes.DWORD, wintypes.HANDLE]
        create.restype = wintypes.HANDLE
        close = kernel32.CloseHandle
        invalid = wintypes.HANDLE(-1).value
        for path in paths:
            # FILE_FLAG_NO_BUFFERING makes the cache manager drop the file's cached pages when it is opened.
            handle = create(str(path), 0x80000000, 0x1 | 0x2, None, 3, 0x20000000, None)
            if handle and handle != invalid:
                close(handle)
    else:
        for path in paths:
            descriptor = os.open(path, os.O_RDONLY)
            try:
                os.posix_fadvise(descriptor, 0, 0, os.POSIX_FADV_DONTNEED)
            finally:
                os.close(descriptor)
    return time.monotonic() - started


def host_busy_fraction(seconds: float = 1.0) -> float | None:
    """The share of all processors busy over an interval, read from the operating system."""
    def sample():
        if host_platform() == 'windows':
            import ctypes
            idle, kernel, user = (ctypes.c_ulonglong(), ctypes.c_ulonglong(), ctypes.c_ulonglong())
            if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
                return None
            return idle.value, kernel.value + user.value
        with open('/proc/stat', encoding='ascii') as handle:
            fields = [int(value) for value in handle.readline().split()[1:]]
        return fields[3] + (fields[4] if len(fields) > 4 else 0), sum(fields)
    first = sample()
    time.sleep(seconds)
    second = sample()
    if first is None or second is None or second[1] == first[1]:
        return None
    return 1.0 - (second[0] - first[0]) / (second[1] - first[1])


def process_cpu_seconds(process: subprocess.Popen) -> float | None:
    """The CPU time a finished child used, from its process handle on Windows."""
    if host_platform() != 'windows':
        return None
    import ctypes
    from ctypes import wintypes
    creation, exit_time, kernel, user = (wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME(),
                                         wintypes.FILETIME())
    handle = wintypes.HANDLE(int(process._handle))  # noqa: SLF001 - the handle Popen keeps for exactly this
    if not ctypes.windll.kernel32.GetProcessTimes(handle, ctypes.byref(creation), ctypes.byref(exit_time),
                                                  ctypes.byref(kernel), ctypes.byref(user)):
        return None

    def seconds(value):
        return ((value.dwHighDateTime << 32) | value.dwLowDateTime) / 1e7
    return seconds(kernel) + seconds(user)


def incremental_variant(measured_index: int) -> int:
    """The variant a side's measured incremental run uses: its prime is variant zero, so the first is one."""
    return (measured_index + 1) % 2


def run_one(command: list[str], log: pathlib.Path, env: dict, cwd: pathlib.Path) -> dict:
    started = time.monotonic()
    usage_before = None
    if host_platform() != 'windows':
        import resource
        usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    with open(log, 'wb') as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=env, cwd=str(cwd))
        exit_code = process.wait()
    wall = time.monotonic() - started
    cpu = process_cpu_seconds(process)
    if usage_before is not None:
        import resource
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu = (usage.ru_utime - usage_before.ru_utime) + (usage.ru_stime - usage_before.ru_stime)
    return {'exitCode': exit_code, 'processWallSeconds': round(wall, 3), 'cpuSeconds': cpu}


def summarize_result(result: dict) -> dict:
    """The fields of a driver result the comparison reads."""
    outcome = result.get('outcome') or {}
    timing = result.get('timing') or {}
    memory = result.get('memory') or {}
    preparation = result.get('preparation') or {}
    process = result.get('process') or {}
    outputs = {entry['role'] + ':' + pathlib.PurePosixPath(entry['path']).name: entry['sha256']
               for entry in result.get('outputs') or [] if entry.get('role') != 'report'}
    return {'success': outcome.get('success', False), 'cancelled': outcome.get('cancelled', False),
            'totalMs': timing.get('totalMs'), 'preparationMs': timing.get('preparationMs'),
            'executionMs': timing.get('executionMs'), 'residentPeakBytes': memory.get('residentPeakBytes'),
            'residentPeakSource': memory.get('residentPeakSource'),
            'idleBaselineBytes': memory.get('idleBaselineBytes'),
            'cancellationLatencyMs': outcome.get('cancellationLatencyMs'),
            'executionMode': preparation.get('executionMode'),
            'stages': [stage.get('name') for stage in timing.get('stages') or []],
            'datasetIdentity': (result.get('dataset') or {}).get('identityDigest'),
            'engine': result.get('engine'), 'outputs': outputs,
            'regionCpuSeconds': process.get('cpuSecondsInRegion'), 'completion': outcome.get('completion'),
            'error': outcome.get('error')}


def run_protocol(arguments) -> dict:
    configuration = read_configuration(arguments.repo_root)
    protocol = configuration['protocol']
    pairs = arguments.pairs if arguments.pairs is not None else protocol['pairs']
    warmups = arguments.warmups if arguments.warmups is not None else protocol['warmups']
    if (pairs > protocol['pairs'] or warmups > protocol['warmups']) and not arguments.over_budget:
        raise MeasurementError(f'{pairs} pairs and {warmups} warm-up rounds are more than the protocol\'s '
                               f'{protocol["pairs"]} and {protocol["warmups"]}; pass --over-budget to run them anyway')
    dataset = configuration['dataset']
    workflow, case = arguments.workflow, arguments.case
    path = getattr(arguments, 'path', 'driver')
    require_numpy()
    if path == 'application' and not getattr(arguments, 'frames', None):
        raise MeasurementError('the application path needs --frames, a directory on the corpus\'s volume')
    output = pathlib.Path(arguments.output)
    if output.exists() and any(output.iterdir()):
        raise MeasurementError(f'{output} exists and is not empty')
    drivers = {'baseline': driver_path(arguments.baseline, path), 'candidate': driver_path(arguments.candidate, path)}
    corpus_root = pathlib.Path(arguments.corpus_root)
    corpus_manifest = corpus_root / dataset['corpusManifest']
    if file_sha256(corpus_manifest) != dataset['corpusManifestSha256']:
        raise MeasurementError(f'{corpus_manifest} is not the pinned corpus manifest')
    frame_set = next(entry for entry in json.loads(corpus_manifest.read_text(encoding='utf-8'))['frameSets']
                     if entry['id'] == dataset['frameSet'])
    inputs = [corpus_root / frame['relativePath'] for role in frame_set['roles'] for frame in role['frames']]
    frames = pathlib.Path(arguments.frames) if path == 'application' else None
    staged = stage_frames(frame_set, corpus_root, frames) if frames is not None else None
    script = pathlib.Path(arguments.repo_root) / APPLICATION_DRIVER[host_platform()]
    (output / 'results').mkdir(parents=True)
    (output / 'logs').mkdir()
    seed = arguments.seed if arguments.seed is not None else protocol['seed']
    schedule = build_schedule(pairs, seed)
    work = pathlib.Path(arguments.work) if arguments.work else output / 'work'
    record = {'schema': 1, 'path': path, 'workflow': workflow, 'case': case, 'host': arguments.host_id, 'seed': seed,
              'pairsPlanned': pairs, 'warmups': warmups, 'datasetIdentity': dataset['identityDigest'],
              'baseline': str(pathlib.Path(arguments.baseline).resolve()),
              'candidate': str(pathlib.Path(arguments.candidate).resolve()),
              'driverSha256': {side: file_sha256(binary) for side, binary in drivers.items()},
              'applicationDriverSha256': file_sha256(script) if path == 'application' else None,
              'stagedFrames': staged,
              'maxParallelCores': arguments.max_parallel_cores, 'runs': [],
              'started': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds')}
    measured_count = {side: 0 for side in SIDES}
    counter = 0
    keeper = OutputKeeper(output / 'kept')

    def one(side: str, label: str, pair_index: int | None, variant: int, fresh: bool):
        nonlocal counter
        counter += 1
        run_root = work / side / ('incremental' if not fresh else f'run-{counter:03d}')
        if fresh and run_root.exists():
            shutil.rmtree(run_root)
        run_root.mkdir(parents=True, exist_ok=True)
        evicted = evict_from_cache(inputs) if case == 'cold' else None
        result_path = output / 'results' / f'{counter:03d}-{label}-{side}.json'
        log = output / 'logs' / f'{counter:03d}-{label}-{side}.log'
        cancel_after = configuration['cases']['cancel']['cancelAfterMs'] if case == 'cancel' else 0
        storage = run_root / f'storage-{counter:03d}'
        if path == 'application':
            # Every run starts the application from a storage root of its own, so no run reads another's session.
            env = dict(os.environ)
            env.pop('AFTERNIGHT_TEST_STORAGE_ROOT', None)
            command = application_command(script, drivers[side], workflow, frames, run_root / 'out', storage,
                                          result_path, variant, cancel_after, protocol['samplingIntervalMs'],
                                          dataset['identityDigest'])
            process = run_one(command, log, env, run_root)
        else:
            driver_arguments = ['--workflow', workflow, '--corpus-manifest', str(corpus_manifest), '--frame-set',
                                dataset['frameSet'], '--corpus-root', str(corpus_root), '--output',
                                str(run_root / 'out'), '--result', str(result_path), '--variant', str(variant),
                                '--sampling-interval-ms', str(protocol['samplingIntervalMs']),
                                '--max-parallel-cores', str(arguments.max_parallel_cores)]
            if case == 'cancel':
                driver_arguments += ['--cancel-after-ms', str(cancel_after)]
            process = run_one([str(drivers[side]), *driver_arguments], log, child_environment(drivers[side].parent),
                              drivers[side].parent)
        result = json.loads(result_path.read_text(encoding='utf-8-sig')) if result_path.is_file() else {}
        entry = {'index': counter, 'label': label, 'side': side, 'pair': pair_index, 'variant': variant,
                 'evictionSeconds': evicted, **process, **summarize_result(result)}
        if path == 'application':
            # The application's own processor time, not the driving script's.
            entry['cpuSeconds'] = (result.get('process') or {}).get('cpuSeconds')
            shutil.rmtree(storage, ignore_errors=True)
        if case != 'cancel':
            # Read before the run's output is deleted: its images against its side's first run's, outside any
            # measured region.
            entry.update(keeper.take(side, variant, counter, result, run_root / 'out'))
        record['runs'].append(entry)
        if fresh:
            shutil.rmtree(run_root, ignore_errors=True)
        (output / 'runs.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        print(f'{counter:03d} {label:>8} {side:>9}: exit {entry["exitCode"]}, {entry.get("totalMs") or 0:.0f} ms, '
              f'peak {(entry.get("residentPeakBytes") or 0) / 2 ** 30:.2f} GiB', flush=True)
        return entry

    fresh = case != 'incremental'
    if case == 'incremental':
        for side in SIDES:
            one(side, 'prime', None, 0, False)
    for round_index in range(warmups):
        for side in (SIDES if round_index % 2 == 0 else tuple(reversed(SIDES))):
            variant = incremental_variant(measured_count[side]) if case == 'incremental' else 0
            if case == 'incremental':
                measured_count[side] += 1
            one(side, f'warmup{round_index}', None, variant, fresh)
    for pair_index, order in enumerate(schedule):
        busy = host_busy_fraction()
        for side in order:
            variant = incremental_variant(measured_count[side]) if case == 'incremental' else 0
            if case == 'incremental':
                measured_count[side] += 1
            entry = one(side, f'pair{pair_index:02d}', pair_index, variant, fresh)
            entry['hostBusyBeforePair'] = busy
        (output / 'runs.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    if case != 'cancel':
        record['acrossSides'] = keeper.across()
        if not arguments.keep_outputs:
            shutil.rmtree(output / 'kept', ignore_errors=True)
    record['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds')
    (output / 'runs.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    comparison = compare([output], configuration)
    (output / 'comparison.json').write_text(json.dumps(comparison, indent=2) + '\n', encoding='utf-8')
    return comparison


# ---- The outputs.
#
# Two deployments need not publish the same bytes - a change may move the arithmetic - so an output is compared by its
# samples, against one of two contracts. Within a side, every run's output is compared with the same output of that
# side's first run: exact, or for a role the configuration bounds, within a stated number of units in the last place
# over a stated share of the samples. Across the sides, each side's first run's outputs are described against each
# other in the baseline's noise - the shift of the median, the ratio of the noise, the typical and the large
# difference, and the correlation - and held to the bounds the configuration states for each role. The samples are
# read here, with no engine.

FITS_BLOCK = 2880


def _fits_value(text: str):
    text = text.split('/', 1)[0].strip() if not text.lstrip().startswith("'") else text.strip()
    if text.startswith("'"):
        end = text.find("'", 1)
        while end != -1 and text[end + 1:end + 2] == "'":
            end = text.find("'", end + 2)
        return text[1:end if end != -1 else None].replace("''", "'").rstrip()
    if text in ('T', 'F'):
        return text == 'T'
    try:
        return int(text)
    except ValueError:
        pass
    try:
        return float(text.replace('D', 'E'))
    except ValueError:
        return text


def read_fits_image(path: pathlib.Path):
    """A FITS file's primary image as (planes, rows, columns) of float64 in its physical values, top row first, and its
    cards; an image a writer declared bottom-up is turned."""
    import numpy
    cards: dict = {}
    header_bytes = 0
    with open(path, 'rb') as handle:
        while True:
            block = handle.read(FITS_BLOCK)
            if len(block) < FITS_BLOCK:
                raise MeasurementError(f'{path} ends inside its header')
            header_bytes += FITS_BLOCK
            if header_bytes == FITS_BLOCK and not block.startswith(b'SIMPLE  ='):
                raise MeasurementError(f'{path} is not a FITS file')
            ended = False
            for offset in range(0, FITS_BLOCK, 80):
                card = block[offset:offset + 80].decode('latin-1')
                keyword = card[:8].strip()
                if keyword == 'END':
                    ended = True
                    break
                if card[8:10] == '= ' and keyword not in cards:
                    cards[keyword] = _fits_value(card[10:])
            if ended:
                break
        bitpix = cards.get('BITPIX')
        dtype = {8: 'u1', 16: '>i2', 32: '>i4', 64: '>i8', -32: '>f4', -64: '>f8'}.get(bitpix)
        axes = [cards.get(f'NAXIS{index}') for index in range(1, int(cards.get('NAXIS', 0)) + 1)]
        if dtype is None or len(axes) not in (2, 3) or any(not isinstance(length, int) for length in axes):
            raise MeasurementError(f'{path} holds no two- or three-axis image this reader knows')
        count = 1
        for length in axes:
            count *= length
        data = numpy.frombuffer(handle.read(count * abs(bitpix) // 8), dtype=dtype)
    if data.size != count:
        raise MeasurementError(f'{path} ends inside its samples')
    planes = axes[2] if len(axes) == 3 else 1
    samples = data.reshape(planes, axes[1], axes[0]).astype(numpy.float64)
    if float(cards.get('BSCALE', 1.0)) != 1.0:
        samples *= float(cards['BSCALE'])
    if float(cards.get('BZERO', 0.0)) != 0.0:
        samples += float(cards['BZERO'])
    if str(cards.get('ROWORDER', '')).upper() == 'BOTTOM-UP':
        samples = samples[:, ::-1, :]
    return samples, cards


def as_fractions(samples, cards: dict):
    """Samples as fractions of their representation's full scale: an unsigned integer image divided by its largest
    code, a floating one as it is stored."""
    bitpix = cards.get('BITPIX')
    if bitpix in (16, 32) and float(cards.get('BZERO', 0.0)) == float(2 ** (bitpix - 1)):
        return samples / float(2 ** bitpix - 1)
    if bitpix == 8:
        return samples / 255.0
    return samples


def _ordered_float32(values):
    """Float32 samples as integers ordered as the values are, so a difference counts the representable values between
    two samples - its units in the last place - and crosses zero without wrapping."""
    import numpy
    bits = numpy.ascontiguousarray(values, dtype=numpy.float32).view(numpy.int32).astype(numpy.int64)
    return numpy.where(bits < 0, -(bits & 0x7FFFFFFF), bits)


def sample_agreement(reference, other) -> dict:
    """How far one run's samples are from another's of the same product: how many differ and by how many units in the
    last place of Float32 at most. NaNs in one place agree."""
    import numpy
    reference = numpy.asarray(reference)
    other = numpy.asarray(other)
    if reference.shape != other.shape:
        return {'sameShape': False, 'identical': False, 'samples': int(other.size), 'differingSamples': None,
                'maxUlp': None, 'maxAbsoluteDifference': None}
    both_nan = numpy.isnan(reference) & numpy.isnan(other)
    differs = (reference != other) & ~both_nan
    count = int(numpy.count_nonzero(differs))
    max_ulp = 0
    max_abs = 0.0
    if count:
        distance = numpy.abs(_ordered_float32(reference[differs]) - _ordered_float32(other[differs]))
        max_ulp = int(distance.max())
        max_abs = float(numpy.nanmax(numpy.abs(reference[differs] - other[differs])))
    return {'sameShape': True, 'identical': count == 0, 'samples': int(reference.size), 'differingSamples': count,
            'maxUlp': max_ulp, 'maxAbsoluteDifference': max_abs}


def output_difference(baseline, candidate) -> dict:
    """One output of the two sides described in the baseline's noise, both in fractions of full scale.

    The location and the noise are each side's median and scaled median absolute deviation over its covered samples -
    finite and not exactly zero, which is what an aligned canvas holds where no frame reached. On one grid the
    samples are also compared where both are finite: the typical and the 99th-percentile absolute difference, and
    the correlation. Two grids leave those empty.
    """
    import numpy
    baseline = numpy.asarray(baseline, dtype=numpy.float64)
    candidate = numpy.asarray(candidate, dtype=numpy.float64)

    def described(samples):
        covered = samples[numpy.isfinite(samples) & (samples != 0.0)]
        if covered.size == 0:
            return 0, 0.0, 0.0
        median = float(numpy.median(covered))
        return int(covered.size), median, 1.4826 * float(numpy.median(numpy.abs(covered - median)))
    b_count, b_median, b_noise = described(baseline)
    c_count, c_median, c_noise = described(candidate)
    scale = b_noise if b_noise > 0.0 else 1.0
    statistics = {'sameShape': baseline.shape == candidate.shape, 'baselineShape': list(baseline.shape),
                  'candidateShape': list(candidate.shape), 'coveredSamples': [b_count, c_count],
                  'nonFinite': [int(numpy.count_nonzero(~numpy.isfinite(baseline))),
                                int(numpy.count_nonzero(~numpy.isfinite(candidate)))],
                  'baselineMedian': b_median, 'candidateMedian': c_median, 'baselineNoise': b_noise,
                  'candidateNoise': c_noise, 'locationShiftInNoise': (c_median - b_median) / scale,
                  'noiseRatio': (c_noise / b_noise) if b_noise > 0.0 else None,
                  'medianAbsoluteDifferenceInNoise': None, 'p99AbsoluteDifferenceInNoise': None, 'correlation': None}
    if not statistics['sameShape']:
        return statistics
    finite = numpy.isfinite(baseline) & numpy.isfinite(candidate)
    b = baseline[finite]
    c = candidate[finite]
    difference = numpy.abs(c - b)
    b_centred = b - float(b.mean())
    c_centred = c - float(c.mean())
    denominator = float(numpy.sqrt(numpy.dot(b_centred, b_centred) * numpy.dot(c_centred, c_centred)))
    statistics.update({'medianAbsoluteDifferenceInNoise': float(numpy.median(difference)) / scale,
                       'p99AbsoluteDifferenceInNoise': float(numpy.percentile(difference, 99.0)) / scale,
                       'correlation': (float(numpy.dot(b_centred, c_centred)) / denominator)
                       if denominator > 0.0 else None})
    return statistics


IMAGE_SUFFIXES = ('.fit', '.fits', '.fts')


def image_outputs(result: dict, output_root: pathlib.Path) -> list[tuple[str, pathlib.Path, str]]:
    """(key, path, digest) of every image a run published, keyed as `summarize_result` keys them."""
    found = []
    for entry in result.get('outputs') or []:
        path = pathlib.Path(output_root) / entry.get('path', '')
        if entry.get('role') == 'report' or path.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        found.append((entry['role'] + ':' + pathlib.PurePosixPath(entry['path']).name, path, entry.get('sha256', '')))
    return found


class OutputKeeper:
    """Keeps each side's first published images of each variant, compares every later run's with them, and describes
    the two sides' first images against each other.

    A run's own output directory is deleted after it, so a comparison reads its images before that; the first run's
    are copied under `directory` for the later ones to be read against.
    """

    def __init__(self, directory: pathlib.Path):
        self.directory = pathlib.Path(directory)
        self.references: dict[tuple[str, int], dict] = {}

    def take(self, side: str, variant: int, index: int, result: dict, output_root: pathlib.Path) -> dict:
        """The fields a run's entry gains: that it is its side's first, or how each image agrees with that first."""
        if not (result.get('outcome') or {}).get('success'):
            return {}
        outputs = image_outputs(result, output_root)
        reference = self.references.get((side, variant))
        if reference is None:
            files = {}
            for key, path, digest in outputs:
                kept = self.directory / side / f'variant{variant}' / path.name
                kept.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, kept)
                files[key] = (kept, digest)
            self.references[(side, variant)] = {'index': index, 'files': files}
            return {'outputReference': True}
        agreements = {}
        for key, path, digest in outputs:
            kept = reference['files'].get(key)
            if kept is None:
                agreements[key] = {'sameShape': False, 'identical': False, 'unmatched': True,
                                   'reference': reference['index']}
            elif kept[1] == digest:
                agreements[key] = {'sameShape': True, 'identical': True, 'samples': None, 'differingSamples': 0,
                                   'maxUlp': 0, 'maxAbsoluteDifference': 0.0, 'byDigest': True,
                                   'reference': reference['index']}
            else:
                agreement = sample_agreement(read_fits_image(kept[0])[0], read_fits_image(path)[0])
                agreements[key] = dict(agreement, reference=reference['index'])
        return {'outputAgreement': agreements}

    def across(self) -> dict:
        """{variantN: {baseline key: statistics}} for every variant both sides kept, each image matched by its key or,
        where its name differs, by the one image of its role on the other side."""
        compared = {}
        variants = sorted({variant for _, variant in self.references})
        for variant in variants:
            baseline = self.references.get(('baseline', variant))
            candidate = self.references.get(('candidate', variant))
            if baseline is None or candidate is None:
                continue
            outputs = {}
            for key, (path, _) in sorted(baseline['files'].items()):
                match = key if key in candidate['files'] else None
                if match is None:
                    same_role = [other for other in candidate['files'] if other.split(':', 1)[0] == key.split(':', 1)[0]]
                    match = same_role[0] if len(same_role) == 1 else None
                if match is None:
                    continue
                b_samples, b_cards = read_fits_image(path)
                c_samples, c_cards = read_fits_image(candidate['files'][match][0])
                statistics = output_difference(as_fractions(b_samples, b_cards), as_fractions(c_samples, c_cards))
                outputs[key] = dict(statistics, candidate=match)
            compared[f'variant{variant}'] = outputs
        return compared


def _role(key: str) -> str:
    return key.split(':', 1)[0]


def within_side_violation(role: str, agreement: dict, contract: dict) -> str | None:
    """Why a run's output is not its side's first run's, under the contract, or None when it is."""
    if not agreement.get('sameShape', False):
        return 'has another shape than its side\'s first run'
    if agreement.get('identical'):
        return None
    bound = (contract.get('withinSide') or {}).get(role)
    if bound is None:
        return f'differs from its side\'s first run in {agreement.get("differingSamples")} samples, and its role is exact'
    share = (agreement.get('differingSamples') or 0) / max(1, agreement.get('samples') or 0)
    if (agreement.get('maxUlp') or 0) > bound['maxUlp'] or share > bound['maxDifferingShare']:
        return (f'differs from its side\'s first run by up to {agreement.get("maxUlp")} units in the last place in '
                f'{agreement.get("differingSamples")} samples, past its bound')
    return None


def across_sides_violation(role: str, statistics: dict, contract: dict) -> str | None:
    """Why the two sides' outputs of a role are not within the accepted difference, or None when they are."""
    bound = (contract.get('acrossSides') or {}).get(role)
    if bound is None:
        return 'has no accepted difference stated for its role'
    same_grid = statistics.get('sameShape', False)
    if not same_grid and bound.get('grid') != 'any':
        return 'has another shape on each side'
    low, high = bound['noiseRatio']
    checks = [
        (abs(statistics.get('locationShiftInNoise') or 0.0) <= bound['maxAbsoluteLocationShiftInNoise'],
         f'its median moved by {statistics.get("locationShiftInNoise")} of the noise'),
        (statistics.get('noiseRatio') is not None and low <= statistics['noiseRatio'] <= high,
         f'its noise ratio is {statistics.get("noiseRatio")}'),
    ]
    if not same_grid:
        failed = [text for held, text in checks if not held]
        return '; '.join(failed) if failed else None
    checks += [
        ((statistics.get('medianAbsoluteDifferenceInNoise') or 0.0) <= bound['maxMedianAbsoluteDifferenceInNoise'],
         f'its typical difference is {statistics.get("medianAbsoluteDifferenceInNoise")} of the noise'),
        ((statistics.get('p99AbsoluteDifferenceInNoise') or 0.0) <= bound['maxP99AbsoluteDifferenceInNoise'],
         f'its 99th-percentile difference is {statistics.get("p99AbsoluteDifferenceInNoise")} of the noise'),
        (statistics.get('correlation') is not None and statistics['correlation'] >= bound['minCorrelation'],
         f'its correlation is {statistics.get("correlation")}'),
    ]
    failed = [text for held, text in checks if not held]
    return '; '.join(failed) if failed else None


def evaluate_outputs(records: list[dict], pairs: list[tuple[dict, dict]], contract: dict) -> dict:
    """Holds the measured pairs' outputs to their contract.

    A pair whose two sides published the same bytes holds exactly. Otherwise every run must agree with its side's
    first run - exactly, or within the bound the contract states for its role - and the two sides' first runs must be
    within the accepted difference of each role, which each record carries for every variant it measured; a role one
    side published and the other did not, or a comparison never made, does not hold.
    """
    differing = [index for index, (b, c) in enumerate(pairs) if b.get('outputs') != c.get('outputs')]
    report = {'contract': 'exact', 'pairsDiffering': differing, 'violations': [], 'withinSide': {}, 'acrossSides': {}}
    if not differing:
        report['held'] = True
        return report
    report['contract'] = 'accepted difference'
    violations = report['violations']
    for index in differing:
        b_roles = sorted(_role(key) for key in pairs[index][0].get('outputs') or {})
        c_roles = sorted(_role(key) for key in pairs[index][1].get('outputs') or {})
        if b_roles != c_roles:
            violations.append(f'pair {index}: the sides published {b_roles} and {c_roles}')
    for pair in pairs:
        for entry in pair:
            if entry.get('outputReference'):
                continue
            agreements = entry.get('outputAgreement')
            if agreements is None:
                violations.append(f'run {entry["index"]} was not compared with its side\'s first run')
                continue
            for key in entry.get('outputs') or {}:
                agreement = agreements.get(key)
                role = _role(key)
                summary = report['withinSide'].setdefault(entry['side'], {}).setdefault(
                    role, {'runs': 0, 'identical': 0, 'maxUlp': 0, 'maxDifferingShare': 0.0})
                if agreement is None:
                    violations.append(f'run {entry["index"]}: {key} was not compared with its side\'s first run')
                    continue
                summary['runs'] += 1
                summary['identical'] += 1 if agreement.get('identical') else 0
                summary['maxUlp'] = max(summary['maxUlp'], agreement.get('maxUlp') or 0)
                share = (agreement.get('differingSamples') or 0) / max(1, agreement.get('samples') or 0)
                summary['maxDifferingShare'] = max(summary['maxDifferingShare'], share)
                problem = within_side_violation(role, agreement, contract)
                if problem:
                    violations.append(f'run {entry["index"]}: {key} {problem}')
    across = {}
    for record in records:
        for variant, outputs in (record.get('acrossSides') or {}).items():
            across.setdefault(variant, {}).update(outputs)
    if not across:
        violations.append('the two sides\' outputs were never compared sample by sample')
    for variant, outputs in sorted(across.items()):
        for key, statistics in sorted(outputs.items()):
            report['acrossSides'].setdefault(variant, {})[key] = statistics
            problem = across_sides_violation(_role(key), statistics, contract)
            if problem:
                violations.append(f'{variant} {key}: {problem}')
    measured_roles = {_role(key) for pair in pairs for entry in pair for key in entry.get('outputs') or {}}
    compared_roles = {_role(key) for outputs in across.values() for key in outputs}
    for role in sorted(measured_roles - compared_roles):
        violations.append(f'{role} was published but not compared across the sides')
    report['held'] = not violations
    return report


# ---- The decision.

def binomial_upper_tail(trials: int, wins: int) -> float:
    """P(X >= wins) for X ~ Binomial(trials, 1/2), computed exactly."""
    return sum(math.comb(trials, count) for count in range(wins, trials + 1)) / (2 ** trials)


def coefficient_of_variation(values: list[float]) -> float:
    """The dispersion: the sample standard deviation over the mean, zero below two samples."""
    if len(values) < 2:
        return 0.0
    mean = sum(values) / len(values)
    if mean <= 0.0:
        return 0.0
    return statistics.stdev(values) / mean


def decide(baseline: list[float], candidate: list[float], ratio_limit: float, noise_threshold: float,
           significance: float, resolution: float | None = None) -> dict:
    """The rule for one metric of paired interleaved trials, lower being better.

    A quiet series - both sides' dispersion at or under the noise threshold - is decided by its medians: a regression
    when their ratio exceeds the host's resolution, the ratio below which a quiet difference is the measurement's
    noise, or the no-regression ratio when no resolution is given. A noisy one is decided by an exact one-sided paired
    sign test over the per-pair candidate-to-baseline ratios against the no-regression ratio; a noisy series the test
    cannot decide is inconclusive, never a pass.
    """
    quiet_limit = ratio_limit if resolution is None else max(ratio_limit, resolution)
    ratios = [c / b for b, c in zip(baseline, candidate) if b and b > 0]
    baseline_median = statistics.median(baseline) if baseline else 0.0
    candidate_median = statistics.median(candidate) if candidate else 0.0
    cv_baseline = coefficient_of_variation(baseline)
    cv_candidate = coefficient_of_variation(candidate)
    noisy = cv_baseline > noise_threshold or cv_candidate > noise_threshold
    within = sum(1 for ratio in ratios if ratio < ratio_limit)
    regressions = sum(1 for ratio in ratios if ratio > ratio_limit)
    decided = within + regressions
    if not ratios or baseline_median <= 0.0:
        decision = 'inconclusive'
        rule = 'no complete pair'
    elif not noisy:
        decision = 'regression' if candidate_median / baseline_median > quiet_limit else 'within'
        rule = 'median ratio, both series quiet'
    else:
        rule = 'paired sign test, a series noisy'
        if decided == 0:
            decision = 'within'
        elif within > regressions and binomial_upper_tail(decided, within) <= significance:
            decision = 'within'
        elif regressions > within and binomial_upper_tail(decided, regressions) <= significance:
            decision = 'regression'
        else:
            decision = 'inconclusive'
    return {'decision': decision, 'rule': rule, 'pairs': len(ratios), 'resolution': quiet_limit,
            'baselineMedian': baseline_median, 'candidateMedian': candidate_median,
            'medianRatio': (candidate_median / baseline_median) if baseline_median > 0 else None,
            'baselineCoefficientOfVariation': cv_baseline, 'candidateCoefficientOfVariation': cv_candidate,
            'noisy': noisy, 'pairsWithin': within, 'pairsRegressing': regressions,
            'baselineRange': [min(baseline), max(baseline)] if baseline else None,
            'candidateRange': [min(candidate), max(candidate)] if candidate else None,
            'ratios': ratios}


def measured_pairs(records: list[dict]) -> list[tuple[dict, dict]]:
    pairs = []
    for offset, record in enumerate(records):
        by_pair: dict[int, dict[str, dict]] = {}
        for run in record['runs']:
            if run.get('pair') is not None:
                by_pair.setdefault(run['pair'], {})[run['side']] = run
        for index in sorted(by_pair):
            sides = by_pair[index]
            if 'baseline' in sides and 'candidate' in sides:
                pairs.append((sides['baseline'], sides['candidate']))
    return pairs


# The stages a run executes only when it builds its calibration masters, which a run that reuses them skips.
MASTER_STAGES = ('BiasStacking', 'DarkStacking', 'FlatStacking')


def reused_before_the_stack(run: dict) -> bool:
    """Whether a run stacked its lights again and built no calibration master: what an incremental run is for.

    The full process plans the reuse and says so in its execution mode; Lite's direct preparation plans a full run and
    its executor reuses every artifact that is still valid, so the stages a run executed and the light master it
    published are the evidence both workflows give. A stack stage that published nothing found its stack still valid.
    """
    stages = run.get('stages') or []
    restacked = any(key.startswith('light-master:') for key in run.get('outputs') or {})
    return 'LightsStacking' in stages and restacked and not any(stage in MASTER_STAGES for stage in stages)


def compare(directories: list[pathlib.Path], configuration: dict | None = None) -> dict:
    configuration = configuration or read_configuration()
    protocol = configuration['protocol']
    records = [json.loads((pathlib.Path(directory) / 'runs.json').read_text(encoding='utf-8'))
               for directory in directories]
    first = records[0]
    for record in records[1:]:
        for key in ('workflow', 'case', 'host', 'datasetIdentity'):
            if record.get(key) != first.get(key):
                raise MeasurementError(f'the batches differ in {key}, so they cannot be analysed together')
        if record.get('path', 'driver') != first.get('path', 'driver'):
            raise MeasurementError('the batches differ in path, so they cannot be analysed together')
    pairs = measured_pairs(records)
    failures = [run['index'] for pair in pairs for run in pair
                if not (run.get('cancelled') if first['case'] == 'cancel' else run.get('success'))]
    path = first.get('path', 'driver')
    resolution = host_resolution(configuration, first['host'], path)
    report = {'schema': 1, 'path': path, 'workflow': first['workflow'], 'case': first['case'], 'host': first['host'],
              'datasetIdentity': first['datasetIdentity'], 'batches': len(records), 'pairs': len(pairs),
              'noRegressionRatio': protocol['noRegressionRatio'], 'resolution': resolution,
              'runsThatDidNotComplete': failures}
    limit = protocol['noRegressionRatio']
    if first['case'] == 'cancel':
        latency = decide([b.get('cancellationLatencyMs') or 0.0 for b, _ in pairs],
                         [c.get('cancellationLatencyMs') or 0.0 for _, c in pairs], limit,
                         protocol['noiseThreshold'], protocol['pairedSignificance'])
        report['cancellationLatencyMs'] = latency
        report['verdict'] = 'reported' if not failures else 'a run did not cancel'
        return report
    time_decision = decide([b['totalMs'] for b, _ in pairs], [c['totalMs'] for _, c in pairs], limit,
                           protocol['noiseThreshold'], protocol['pairedSignificance'], resolution['wallTime'])
    memory_decision = decide([b['residentPeakBytes'] for b, _ in pairs], [c['residentPeakBytes'] for _, c in pairs],
                             limit, protocol['noiseThreshold'], protocol['pairedSignificance'],
                             resolution['residentPeak'])
    outputs = evaluate_outputs(records, pairs, configuration.get('outputContract') or {})
    modes = sorted({run.get('executionMode') for pair in pairs for run in pair})
    report.update({'wallTime': time_decision, 'residentPeak': memory_decision, 'outputs': outputs,
                   'executionModes': modes,
                   'cpuSeconds': {side: [run.get('cpuSeconds') for pair in pairs for run in pair
                                         if run['side'] == side] for side in SIDES},
                   'regionCpuSeconds': {side: [run.get('regionCpuSeconds') for pair in pairs for run in pair
                                               if run['side'] == side] for side in SIDES}})
    if failures or not outputs['held']:
        verdict = 'refused: ' + ('a run did not complete' if failures else 'the outputs differ from their contract')
    elif first['case'] == 'incremental' and not all(reused_before_the_stack(run) for pair in pairs for run in pair):
        verdict = 'refused: a measured incremental run did not reuse everything before the light stack'
    elif 'regression' in (time_decision['decision'], memory_decision['decision']):
        verdict = 'regression'
    elif 'inconclusive' in (time_decision['decision'], memory_decision['decision']):
        verdict = 'inconclusive'
    else:
        verdict = 'no regression'
    report['verdict'] = verdict
    return report


def run_campaign(arguments) -> dict:
    """Runs a host's campaign: each series in turn at the host's pairs, every series' record under the output."""
    configuration = read_configuration(arguments.repo_root)
    problems = campaign_findings(configuration)
    if problems:
        raise MeasurementError('the campaign is not within its budget: ' + '; '.join(problems))
    entry = configuration['campaign']['hosts'].get(arguments.host_id)
    if entry is None:
        raise MeasurementError(f'the configuration states no campaign for {arguments.host_id}')
    path = getattr(arguments, 'path', 'driver')
    estimate = campaign_estimate(configuration, arguments.host_id, path)
    print(f'{arguments.host_id}: {len(entry["series"])} series by the {path} path at {entry["pairs"]} pairs, estimated '
          f'{estimate["totalSeconds"] / 60:.1f} of {configuration["campaign"]["budgetMinutes"]} minutes', flush=True)
    for series, seconds in estimate['series'].items():
        print(f'  {series}: {seconds / 60:.1f} minutes', flush=True)
    if arguments.dry_run:
        return {'estimate': estimate, 'series': {}}
    require_numpy()
    output = pathlib.Path(arguments.output)
    work = pathlib.Path(arguments.work) if arguments.work else output / 'work'
    verdicts = {}
    for series in entry['series']:
        workflow, case = series.split('-', 1)
        arguments.workflow, arguments.case = workflow, case
        arguments.output, arguments.work = output / series, work / series
        arguments.pairs, arguments.warmups, arguments.seed = entry['pairs'], None, None
        arguments.max_parallel_cores, arguments.keep_outputs, arguments.over_budget = 0, False, False
        started = time.monotonic()
        report = run_protocol(arguments)
        verdicts[series] = {'verdict': report['verdict'], 'minutes': round((time.monotonic() - started) / 60, 1)}
        print(f'{series}: {report["verdict"]}, {verdicts[series]["minutes"]} minutes', flush=True)
        shutil.rmtree(work / series, ignore_errors=True)
    summary = {'host': arguments.host_id, 'path': path, 'estimate': estimate, 'series': verdicts}
    (output / 'campaign.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    return summary


# ---- The command line.

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    archiving = commands.add_parser('archive')
    archiving.add_argument('--destination', type=pathlib.Path, required=True)
    archiving.add_argument('--host-id', required=True)
    archiving.add_argument('--configuration', choices=('release', 'debug'), default='release')
    archiving.add_argument('--allow-dirty', action='store_true')
    archiving.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    verifying = commands.add_parser('verify-archive')
    verifying.add_argument('directory', type=pathlib.Path)
    running = commands.add_parser('run')
    running.add_argument('--baseline', type=pathlib.Path, required=True)
    running.add_argument('--candidate', type=pathlib.Path, required=True)
    running.add_argument('--workflow', choices=WORKFLOWS, required=True)
    running.add_argument('--case', choices=CASES, required=True)
    running.add_argument('--host-id', required=True)
    running.add_argument('--corpus-root', type=pathlib.Path, required=True)
    running.add_argument('--output', type=pathlib.Path, required=True)
    running.add_argument('--work', type=pathlib.Path, help='where the runs write their outputs; under --output by default')
    running.add_argument('--pairs', type=int)
    running.add_argument('--warmups', type=int)
    running.add_argument('--seed', type=int)
    running.add_argument('--max-parallel-cores', type=int, default=0)
    running.add_argument('--keep-outputs', action='store_true',
                         help='keep each side\'s first published images under <output>/kept after the comparison')
    running.add_argument('--over-budget', action='store_true',
                         help='allow more pairs or warm-up rounds than the protocol states')
    campaigning = commands.add_parser('campaign')
    campaigning.add_argument('--baseline', type=pathlib.Path, required=True)
    campaigning.add_argument('--candidate', type=pathlib.Path, required=True)
    campaigning.add_argument('--host-id', required=True)
    campaigning.add_argument('--corpus-root', type=pathlib.Path, required=True)
    campaigning.add_argument('--output', type=pathlib.Path, required=True)
    campaigning.add_argument('--work', type=pathlib.Path)
    campaigning.add_argument('--dry-run', action='store_true', help='print the plan and its estimate only')
    for parser_ in (running, campaigning):
        parser_.add_argument('--path', choices=PATHS, default='driver',
                             help='the workflow driver, or the application driven through its own windows')
        parser_.add_argument('--frames', type=pathlib.Path,
                             help='the application path: where the frame set is staged, on the corpus\'s volume')
    campaigning.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    running.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    comparing = commands.add_parser('compare')
    comparing.add_argument('directories', type=pathlib.Path, nargs='+')
    comparing.add_argument('--output', type=pathlib.Path)
    checking = commands.add_parser('check')
    checking.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == 'check':
            problems = check(arguments.repo_root)
            for problem in problems:
                print(problem)
            if problems:
                print(f'{len(problems)} product measurement problem(s)')
                return 1
            configuration = read_configuration(arguments.repo_root)
            print(f'the product measurement is pinned: {configuration["dataset"]["frameSet"]}, '
                  f'{configuration["dataset"]["frames"]} frames, {len(CASES)} cases of {len(WORKFLOWS)} workflows; '
                  f'{len(configuration.get("baselines") or [])} recorded baseline(s) hold')
            return 0
        if arguments.command == 'archive':
            manifest = create_archive(arguments.repo_root, arguments.destination, arguments.host_id,
                                      arguments.configuration, arguments.allow_dirty)
            print(f'archived {len(manifest["files"])} files, {manifest["totalBytes"] / 2 ** 30:.2f} GiB, from '
                  f'{manifest["commit"][:9]} into {arguments.destination}')
            return 0
        if arguments.command == 'verify-archive':
            problems = verify_archive(arguments.directory)
            for problem in problems:
                print(problem)
            print('the archive holds every file of its manifest unchanged' if not problems
                  else f'{len(problems)} archive problem(s)')
            return 1 if problems else 0
        if arguments.command == 'campaign':
            run_campaign(arguments)
            return 0
        if arguments.command == 'run':
            report = run_protocol(arguments)
            print(f'{report["workflow"]} {report["case"]} by the {report["path"]} path: {report["verdict"]}')
            return 0
        report = compare(arguments.directories)
        if arguments.output:
            arguments.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({key: value for key, value in report.items() if key not in ('cpuSeconds',)}, indent=2))
        return 0
    except MeasurementError as error:
        print(f'refused: {error}')
        return 1


if __name__ == '__main__':
    sys.exit(main())

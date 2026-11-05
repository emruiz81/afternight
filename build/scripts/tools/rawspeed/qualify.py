# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hash-verified RAW corpus gate and paired, actual-engine benchmark. Missing evidence fails."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import queue
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

REPO = Path(__file__).resolve().parents[4]
MANIFEST = REPO / 'docs/dev/rawspeed-corpus.json'
GOLDENS = REPO / 'docs/dev/rawspeed-goldens.json'


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def manifest_identity():
    # Git checkouts use host-specific line endings; the manifest's values define its identity.
    value = json.loads(MANIFEST.read_text(encoding='utf-8'))
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode('utf-8')).hexdigest()


def provenance(directory):
    debug = directory.name.lower() == 'debug'
    suffix = '_d' if debug else ''
    if os.name == 'nt':
        d = 'd' if debug else ''
        names = [
            f'AfterNightEngine{suffix}.dll',
            f'raw_r{d}.dll', f'afternight_libraw_r{d}.dll', f'afternight_rawspeed{d}.dll']
    else:
        names = [
            f'libAfterNightEngine{suffix}.so']
    binaries = {name: digest(directory / name) for name in names if (directory / name).is_file()}
    paths = ['src/AfterNightEngine', 'build/premake', 'build/vcpkg', 'build/scripts/tools/rawspeed']
    patch = subprocess.check_output(['git', 'diff', '--binary', 'HEAD', '--', *paths], cwd=REPO)
    return {'baseRevision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
            'sourcePatchSha256': hashlib.sha256(patch).hexdigest(), 'sourcePaths': paths,
            'sourceWasDirty': bool(patch), 'engineAndRuntimeSha256': binaries,
            'privatePorts': {name: json.loads((REPO / 'build/vcpkg/overlay-ports' / name / 'vcpkg.json').read_text(encoding='utf-8'))
                             for name in ('afternight-libraw', 'afternight-rawspeed')}}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def corpus(args):
    mapping = json.loads(args.paths.read_text(encoding='utf-8')) if args.paths else {}
    fixtures = json.loads(MANIFEST.read_text(encoding='utf-8'))['fixtures']
    for fixture in fixtures:
        explicit = mapping.get(fixture['id']) or os.environ.get(fixture.get('environment', ''))
        candidates = [Path(explicit)] if explicit else []
        if args.root:
            candidates += [args.root / fixture['path'], args.root / fixture['filename']]
        path = next((p for p in candidates if p.is_file()), None)
        if path is None and args.download and args.root and fixture.get('source'):
            path = args.root / fixture['filename']
            path.parent.mkdir(parents=True, exist_ok=True)
            pending = path.with_suffix(path.suffix + '.pending')
            with urllib.request.urlopen(fixture['source'], timeout=120) as source, pending.open('wb') as output:
                shutil.copyfileobj(source, output)
            require(pending.stat().st_size == fixture['bytes'] and digest(pending) == fixture['sha256'],
                    f"Downloaded fixture identity mismatch: {fixture['id']}")
            pending.replace(path)
        require(path is not None, f"Missing required fixture {fixture['id']}: supply --root or --paths JSON")
        require(path.stat().st_size == fixture['bytes'] and digest(path) == fixture['sha256'],
                f"Fixture identity mismatch: {fixture['id']}")
        fixture['localPath'] = str(path.resolve())
    return fixtures


class Driver:
    def __init__(self, directory, name):
        self.binary = directory / (name + ('.exe' if os.name == 'nt' else ''))
        require(self.binary.is_file(), f'Missing required binary: {self.binary}')
        env = os.environ.copy()
        # Keep resolution inside the deployed tree. Never borrow experiment or vcpkg DLL directories.
        if os.name != 'nt':
            env['LD_LIBRARY_PATH'] = str(directory) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
        # Sanitizers can write more than a pipe buffer before exiting. A file cannot deadlock
        # the decoder while the controller waits for its JSON response.
        self.errors = tempfile.TemporaryFile(mode='w+t', encoding='utf-8')
        self.process = subprocess.Popen([str(self.binary)], cwd=directory, env=env,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.errors,
                                        text=True, encoding='utf-8')
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_output(self):
        try:
            for line in self.process.stdout:
                self.lines.put(line)
        finally:
            self.lines.put(None)

    def error_output(self):
        self.errors.seek(0)
        return self.errors.read()

    def __enter__(self):
        return self

    def __exit__(self, exception_type, *_):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=30)
        self.reader.join(timeout=5)
        errors = self.error_output()
        self.errors.close()
        if exception_type is None:
            require(self.process.returncode == 0, f'{self.binary.name} exited {self.process.returncode}: {errors}')

    def run(self, request):
        self.process.stdin.write(json.dumps(request) + '\n')
        self.process.stdin.flush()
        while True:
            try:
                line = self.lines.get(timeout=300)
            except queue.Empty:
                self.process.kill()
                self.process.wait(timeout=30)
                raise RuntimeError(f'{self.binary.name} produced no result within 300 seconds: {self.error_output()}')
            if not line:
                raise RuntimeError(f'{self.binary.name} exited without a result: {self.error_output()}')
            # A driver's ordinary logging may precede its JSON response; preserve it on the console.
            if not line.startswith('{'):
                print(line.rstrip(), flush=True)
                continue
            result = json.loads(line)
            require('error' not in result, f'{self.binary.name}: {result.get("error")}')
            return result


def exact_files(left, right):
    with Path(left).open('rb') as a, Path(right).open('rb') as b:
        while True:
            x, y = a.read(1024 * 1024), b.read(1024 * 1024)
            if x != y:
                return False
            if not x:
                return True


def good_oracle(result, accelerated=False):
    require(result.get('openCode') == 0 and result.get('unpackCode') == 0 and not result.get('corrupt'),
            f'Oracle refused required decode: {result}')
    if accelerated:
        require(result.get('outcome') == 1 and result.get('correctionRevision') == 1,
                'Requested oracle acceleration did not actually run the qualified correction')


def good_engine(result, selected=None):
    require(result.get('ok') and result.get('adoptable'), f'Engine decode failed: {result}')
    if selected:
        require(result['selected'] == selected, f'Actual backend mismatch: {result["selected"]} != {selected}')
    sequence = [event['sequence'] for event in result['traces']]
    require(sequence == sorted(set(sequence)), 'Trace sequence repeated or moved backwards')


SONY_ARW2 = 5  # AnRawSonyArw2 in the private LibRaw port's AnRawBackend.h.


def automatic_route(fixture, native):
    """The backend Automatic selects on this host: every accelerated mode on Windows, and every
    one but Sony ARW2 on Linux, whose timing did not meet the Automatic criterion there."""
    if not fixture['requiredAcceleration']:
        return 'native'
    return 'native' if os.name != 'nt' and native['family'] == SONY_ARW2 else 'rawspeed'


# What a golden records in place of an observer who is a third party's address. The stock oracle still compares the
# observer the engine reads (`compare_metadata`); the golden only declines to publish it.
WITHHELD = '<withheld>'


def pinned_metadata(actual, golden):
    """The engine's metadata as the golden records it: an observer the golden withholds is withheld here too."""
    if golden.get('artist') == WITHHELD and 'artist' in actual:
        return dict(actual, artist=WITHHELD)
    return actual


def compare_metadata(stock, engine):
    camera = engine['metadata']['camera']
    for field in camera:
        expected = stock['metadata'][field]
        if field == 'asShotApplied':
            expected = bool(expected)
        require(camera[field] == expected, f'Calibration field differs: {field}')
    require(engine['metadata']['artist'] == stock['metadata']['artist'], 'Observer differs')
    require(engine['metadata']['description'] == stock['metadata']['description'], 'Object differs')
    require(engine['width'] == stock['metadata']['width'] and engine['height'] == stock['metadata']['height'],
            'Visible extent differs')
    require(engine['descriptor']['pixelAspect'] == stock['metadata']['pixelAspect'], 'Pixel aspect differs')
    if 'cfaColors' in engine:
        filters = stock['metadata']['filters']
        expected = stock['metadata']['xtrans'] if filters == 9 else [
            [((filters >> ((y % 8) * 4 + (x % 2) * 2)) & 3) for x in range(6)] for y in range(6)]
        expected = [[1 if color == 3 else color for color in row] for row in expected]
        require(engine['cfaColors'] == expected, 'Visible CFA table or phase differs')


def qualify(args, fixtures):
    report = {'kind': 'functional', 'host': platform.node(), 'platform': platform.platform(), 'cases': []}
    golden_document = json.loads(GOLDENS.read_text(encoding='utf-8'))
    require(golden_document['manifestSha256'] == manifest_identity() and golden_document['correctionRevision'] == 1,
            'Golden corpus/revision changed: requalification is required')
    goldens = {row['id']: row for row in golden_document['cases']}
    require(set(goldens) == {row['id'] for row in fixtures}, 'Golden coverage must contain the complete corpus')
    triplet = 'x64-windows-raw' if os.name == 'nt' else 'x64-linux-raw'
    package = REPO / 'vcpkg/installed' / triplet
    if args.bin.name.lower() == 'debug':
        package /= 'debug'
    wrapper = package / 'tools/afternight-rawspeed' / ('AnRawSpeedTests.exe' if os.name == 'nt' else 'AnRawSpeedTests')
    require(wrapper.is_file(), f'Missing required wrapper tests: {wrapper}')
    wrapper_env = os.environ.copy(); wrapper_env['PATH'] = str(args.bin) + os.pathsep + wrapper_env.get('PATH', '')
    basic = subprocess.run([str(wrapper)], env=wrapper_env, capture_output=True, text=True, timeout=120)
    require(basic.returncode == 0, f'Wrapper tests failed: {basic.stdout} {basic.stderr}')
    report['wrapperTests'] = basic.stdout.strip()
    with tempfile.TemporaryDirectory(prefix='afternight-raw-qualification-') as temporary, ExitStack() as stack:
        directory = Path(temporary)
        stock = stack.enter_context(Driver(args.bin, 'RawStockOracle'))
        private = stack.enter_context(Driver(args.bin, 'RawPrivateOracle'))
        engine = stack.enter_context(Driver(args.bin, 'RawEngineBench'))
        for fixture in fixtures:
            path = fixture['localPath']
            row = {'id': fixture['id'], 'sourceSha256': fixture['sha256']}
            reference = stock.run({'path': path, 'dump': str(directory / 'stock')})
            good_oracle(reference)
            native = private.run({'path': path, 'dump': str(directory / 'private')})
            good_oracle(native)
            require(reference['metadata'] == native['metadata'], 'Stock/private metadata differs')
            for suffix in ('sensor', 'visible'):
                require(exact_files(directory / f'stock.{suffix}', directory / f'private.{suffix}'),
                        f'Stock/private {suffix} differs: {fixture["id"]}')
                row[suffix + 'Sha256'] = digest(directory / f'stock.{suffix}')
                require(row[suffix + 'Sha256'] == goldens[fixture['id']][suffix + 'Sha256'],
                        f'Independent oracle changed its pinned {suffix} golden: {fixture["id"]}')
            actual = engine.run({'path': path, 'preference': 'native', 'dump': str(directory / 'engine')})
            good_engine(actual, 'native')
            compare_metadata(reference, actual)
            require(pinned_metadata(actual['metadata'], goldens[fixture['id']]['metadata'])
                    == goldens[fixture['id']]['metadata'] and
                    actual['descriptorText'] == goldens[fixture['id']]['descriptorText'] and
                    actual.get('cfaColors') == goldens[fixture['id']].get('cfaColors'),
                    f'Pinned semantic golden differs: {fixture["id"]}')
            require(exact_files(directory / 'stock.visible', directory / 'engine'), f'Engine/reference differs: {fixture["id"]}')
            row['native'] = actual
            for preference in ('required', 'automatic'):
                candidate = engine.run({'path': path, 'preference': preference, 'dump': str(directory / 'engine')})
                if preference == 'required' and not fixture['requiredAcceleration']:
                    require(not candidate['ok'] and candidate['status'] == 'UnsupportedBackend' and candidate['engineLiveBytes'] == 0,
                            f'Unknown profile did not refuse RequireRawSpeed: {fixture["id"]}')
                    continue
                good_engine(candidate, 'rawspeed' if preference == 'required' else automatic_route(fixture, native))
                require(exact_files(directory / 'stock.visible', directory / 'engine'), 'Accelerated engine pixels differ')
                require(actual['metadata'] == candidate['metadata'] and actual['descriptorText'] == candidate['descriptorText'],
                        'Accelerated engine metadata/descriptor differs')
                row[preference] = candidate
            if fixture['requiredAcceleration']:
                accelerated = private.run({'path': path, 'accelerated': True, 'dump': str(directory / 'private')})
                good_oracle(accelerated, True)
                command = [str(wrapper), '--file', path, str(native['family']), str(native['rawWidth']),
                           str(native['rawHeight']), str(native['filters'])]
                if native['filters'] == 9:
                    command.append(''.join(str(v) for values in native['metadata']['xtransAbsolute'] for v in values))
                injected = subprocess.run(command, env=wrapper_env, capture_output=True, text=True, timeout=180)
                require(injected.returncode == 0, f'Real decoder allocation injection failed: {injected.stdout} {injected.stderr}')
                row['allocationInjection'] = injected.stdout.strip()
                require(reference['metadata'] == accelerated['metadata'], 'Accelerated sensor metadata differs')
                for suffix in ('sensor', 'visible'):
                    require(exact_files(directory / f'stock.{suffix}', directory / f'private.{suffix}'),
                            f'Accelerated {suffix} differs: {fixture["id"]}')
                events = [('rawspeed.unpack.checkpoint', 128), ('copy.checkpoint', 127), ('publication', 0)]
                if native['family'] in (2, 5):
                    events.append(('rawspeed.correction.checkpoint', 128))
                row['cancellation'] = []
                for event, after in events:
                    cancelled = engine.run({'path': path, 'preference': 'required', 'cancelEvent': event, 'cancelAfter': after})
                    require(not cancelled['ok'] and cancelled['status'] == 'Cancelled' and cancelled['engineLiveBytes'] == 0,
                            f'Mid-phase cancellation failed: {fixture["id"]} {event}')
                    require(cancelled['cancellationLatencyMs'] < args.cancellation_ms, 'Cancellation latency exceeded contract')
                    row['cancellation'].append({'phase': event, 'latencyMs': cancelled['cancellationLatencyMs']})
                deadline = engine.run({'path': path, 'preference': 'required', 'deadlineMs': 300,
                                       'deadlineEvent': 'rawspeed.unpack.checkpoint', 'cancelAfter': 128})
                require(not deadline['ok'] and deadline['status'] == 'DeadlineExceeded' and deadline['engineLiveBytes'] == 0 and
                        deadline.get('deadlineInjected') and deadline['deadlineLatencyMs'] < args.cancellation_ms,
                        'Deadline did not remain a terminal typed failure')
                row['deadlineLatencyMs'] = deadline['deadlineLatencyMs']
                toggled = engine.run({'path': path, 'adapterPreference': 'required', 'testRuntimeToggle': True})
                expected_route = row['automatic']['selected']
                reads = toggled['reads']
                require([r['selected'] for r in reads] == [expected_route, 'native', expected_route] and
                        all(r['ok'] and r['visibleHash'] == actual['visibleHash'] and
                            r['metadata'] == actual['metadata'] for r in reads),
                        'Runtime setting did not switch new reads in the same context')
                require(toggled['retainedBytes'] > 0 and toggled['engineLiveBytes'] == 0,
                        'Runtime toggle disturbed retained frame ownership')
                if expected_route == 'rawspeed':
                    require(not reads[0]['enabledAfterRead'], 'Setting was not changed during accelerated unpack')
                row['runtimeToggle'] = toggled
                disabled = engine.run({'path': path, 'adapterPreference': 'required', 'enableRawSpeed': False})
                good_engine(disabled, 'native')
                require(disabled['visibleHash'] == actual['visibleHash'], 'Runtime disable changed samples')
                refused = engine.run({'path': path, 'preference': 'required', 'engineMemoryBytes': 1024})
                require(not refused['ok'] and refused['engineLiveBytes'] == 0 and
                        not any(e['name'].startswith('attempt.') for e in refused['traces']), 'Output budget refused too late')
                memory = engine.run({'path': path, 'preference': 'required', 'memoryMiB': 1})
                require(not memory['ok'] and memory['engineLiveBytes'] == 0, 'Library guard ignored')
            no_metadata = engine.run({'path': path, 'metadata': False})
            good_engine(no_metadata)
            require('camera' not in no_metadata['metadata'] and no_metadata['visibleHash'] == actual['visibleHash'], 'Metadata omission changed samples')
            index = engine.run({'path': path, 'imageIndex': 999})
            require(not index['ok'] and index['engineLiveBytes'] == 0, 'Invalid image index published a frame')
            if actual['planes'] == 3:
                for mode in range(5):
                    native_wb = engine.run({'path': path, 'preference': 'native', 'whiteBalance': mode, 'multipliers': [2, 1, 3]})
                    auto_wb = engine.run({'path': path, 'whiteBalance': mode, 'multipliers': [2, 1, 3]})
                    good_engine(native_wb, 'native'); good_engine(auto_wb, 'native')
                    require(native_wb['visibleHash'] == auto_wb['visibleHash'] and native_wb['metadata'] == auto_wb['metadata'],
                            'Full-color white-balance fallback changed')
            row['exact'] = True
            report['cases'].append(row)
            args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            print(f"PASS {fixture['id']}: sensor, frame, metadata, preferences and runtime contracts", flush=True)
        # Same process, two independent contexts and decoder databases; repeated results remain deterministic.
        candidates = [f for f in fixtures if f['requiredAcceleration']]
        batch = engine.run({'batch': [{'path': f['localPath'], 'preference': 'required'} for f in candidates[:2]]})
        for fixture, result in zip(candidates, batch['results']):
            good_engine(result, 'rawspeed')
            expected = next(r for r in report['cases'] if r['id'] == fixture['id'])['native']
            require(result['visibleHash'] == expected['visibleHash'], 'Concurrent context changed pixels')
        report['concurrent'] = batch
        unicode_path = directory / 'cámara_日本語.CR2'
        shutil.copyfile(candidates[0]['localPath'], unicode_path)
        good_engine(engine.run({'path': str(unicode_path), 'preference': 'required'}), 'rawspeed')
        report['unicodePath'] = True
        malformed = directory / 'invalid.raw'
        for payload in (b'unknown camera data', Path(candidates[0]['localPath']).read_bytes()[:1024 * 1024]):
            malformed.write_bytes(payload)
            for preference in ('native', 'required', 'automatic'):
                result = engine.run({'path': str(malformed), 'preference': preference})
                require(not result['ok'] and result['engineLiveBytes'] == 0, 'Malformed/truncated input published a frame')
        report['malformed'] = True
    report['passed'] = True
    return report


def benchmark(args, fixtures):
    report = {'kind': 'timing', 'host': platform.node(), 'platform': platform.platform(),
              'cache': 'warm OS cache; fresh-process observations separately labelled, no OS eviction claimed',
              'seed': args.seed, 'rounds': args.rounds, 'threadsPerOperation': args.threads, 'cases': []}
    rng = random.Random(args.seed)
    if args.cold_cache:
        from cold_cache import evict
        require(os.name != 'nt', '--cold-cache requires Linux file residency verification')
        report['cache'] = 'file-cold: each fixture evicted and mincore-verified before each decode; code stays warm'
    def prepare(request):
        return evict(request['path']) if args.cold_cache else None
    with ExitStack() as stack:
        engine = stack.enter_context(Driver(args.bin, 'RawEngineBench'))
        for fixture in fixtures:
            routes = ['native', 'automatic']
            if fixture['requiredAcceleration']:
                routes.append('required')
            request = {'path': fixture['localPath'], 'threads': args.threads}
            row = {'id': fixture['id'], 'bytes': fixture['bytes'], 'trials': {route: [] for route in routes}, 'freshProcess': {}}
            for route in routes:
                with Driver(args.bin, 'RawEngineBench') as fresh:
                    residency = prepare(request)
                    value = fresh.run({**request, 'preference': route})
                    if residency: value['fileCache'] = residency
                    require(value.get('ok'), 'Fresh-process decode failed')
                    require(value['admittedThreads'] == args.threads, 'Fresh-process concurrency differs')
                    row['freshProcess'][route] = value
            for _ in range(0 if args.cold_cache else 2):
                for route in routes:
                    engine.run({**request, 'preference': route})
            for _ in range(args.rounds):
                order = routes.copy(); rng.shuffle(order)
                for route in order:
                    residency = prepare(request)
                    result = engine.run({**request, 'preference': route})
                    if residency: result['fileCache'] = residency
                    require(result.get('ok'), f'Benchmark decode failed: {fixture["id"]} {route}')
                    require(result['admittedThreads'] == args.threads, 'Benchmark concurrency differs')
                    if route == 'required':
                        require(result['selected'] == 'rawspeed', 'Benchmark silently fell back')
                    row['trials'][route].append(result)
            row['summary'] = {}
            for route, trials in row['trials'].items():
                times = [t['operationMs'] for t in trials]
                median = statistics.median(times)
                row['summary'][route] = {'medianMs': median, 'minMs': min(times), 'maxMs': max(times),
                                         'madMs': statistics.median(abs(t - median) for t in times)}
            if 'required' in routes:
                n, r = row['summary']['native'], row['summary']['required']
                row['automaticCandidate'] = r['medianMs'] <= n['medianMs'] * .9 and n['medianMs'] - r['medianMs'] > 3 * (n['madMs'] + r['madMs'])
            report['cases'].append(row)
            args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            print(f"TIMED {fixture['id']}: " + ', '.join(f'{k}={v["medianMs"]:.2f} ms' for k,v in row['summary'].items()), flush=True)
        accelerated = [f for f in fixtures if f['requiredAcceleration']]
        report['throughput'] = {}
        throughput_times = {'native': [], 'required': []}
        for _ in range(args.rounds):
            order = list(throughput_times); rng.shuffle(order)
            for route in order:
                total_ms = 0
                for offset in range(0, len(accelerated), args.width):
                    for fixture in accelerated[offset:offset + args.width]:
                        prepare({'path': fixture['localPath']})
                    started = time.perf_counter()
                    batch = engine.run({'batch': [{'path': f['localPath'], 'preference': route, 'threads': 1}
                                                   for f in accelerated[offset:offset + args.width]]})
                    total_ms += (time.perf_counter() - started) * 1000
                    for result in batch['results']:
                        good_engine(result, 'rawspeed' if route == 'required' else 'native')
                throughput_times[route].append(total_ms)
        for route, times in throughput_times.items():
            report['throughput'][route] = {'width': args.width, 'files': len(accelerated), 'wallMs': times,
                                           'filesPerSecond': len(accelerated) * 1000 / statistics.median(times)}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('verify', 'qualify', 'benchmark'))
    parser.add_argument('--root', type=Path)
    parser.add_argument('--paths', type=Path, help='Local JSON object mapping fixture IDs to literal paths')
    parser.add_argument('--download', action='store_true', help='Fetch only manifest-licensed public inputs into --root')
    parser.add_argument('--bin', type=Path, default=REPO / ('bin/msvc/release' if os.name == 'nt' else 'bin/clang/release'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=9)
    parser.add_argument('--seed', type=int, default=20260911)
    parser.add_argument('--threads', type=int, default=1)
    parser.add_argument('--width', type=int, default=2)
    parser.add_argument('--cancellation-ms', type=float, default=100)
    parser.add_argument('--only-qualified', action='store_true', help='Labelled timing rerun of all required-acceleration fixtures; still verifies the complete corpus')
    parser.add_argument('--cold-cache', action='store_true', help='Linux: evict only each fixture and require mincore-confirmed nonresidency before timing')
    args = parser.parse_args()
    require(not args.cold_cache or args.action == 'benchmark', '--cold-cache applies only to benchmark')
    require(args.rounds >= 3 and 1 <= args.width <= 16, 'At least three rounds and width 1..16 required')
    args.bin = args.bin.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fixtures = corpus(args)
    if args.only_qualified:
        require(args.action == 'benchmark', '--only-qualified is only a timing rerun, never a reduced corpus gate')
        fixtures = [f for f in fixtures if f['requiredAcceleration']]
    report = {'verified': len(fixtures)} if args.action == 'verify' else (
        qualify(args, fixtures) if args.action == 'qualify' else benchmark(args, fixtures))
    report['manifestSha256'] = manifest_identity()
    report['provenance'] = provenance(args.bin)
    report['binaries'] = {path.name: digest(path) for path in args.bin.glob('Raw*') if path.suffix.lower() in ('.exe', '') and path.is_file()}
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'{args.action}: PASS; evidence {args.output}')


if __name__ == '__main__':
    main()

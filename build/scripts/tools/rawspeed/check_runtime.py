# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Check private runtime identity, relocatability and test-only fault recovery on Windows/Linux."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from deploy_raw_deps import pe_imports
from qualify import Driver, digest, require, corpus


# A RAW or OpenMP runtime the engine itself names. The private packages are built static and with
# OpenMP off, so any of these in the engine's own dynamic section is the contract broken. The whole
# loaded closure is not that question: the distribution's FFmpeg reaches libgomp through libsoxr,
# which says nothing about RAW and is not something this engine links or can remove.
RAW_OR_OPENMP = re.compile(r'lib(?:raw|omp|gomp|iomp)')
NEEDED_LIBRARY = re.compile(r'\(NEEDED\).*\[([^\]]+)\]')


def unexpected_raw_dependencies(dynamic_section):
    """Every RAW or OpenMP shared library the engine names in its own dynamic section.

    @param dynamic_section the output of `readelf -d` for the engine.
    @return the offending library names, in the order they are named.
    """
    found = []
    for line in dynamic_section.splitlines():
        match = NEEDED_LIBRARY.search(line)
        if match and RAW_OR_OPENMP.search(match.group(1)):
            found.append(match.group(1))
    return found


def private_raw_symbols(symbols):
    """Inspect demangled owners; an engine function may take a private type as an argument."""
    result = []
    for line in symbols.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) != 3:
            continue
        name = fields[2]
        if re.fullmatch(r'(?:libraw_|jpeg_|png_|inflate|deflate|anRs|anLibRaw)[A-Za-z0-9_]*(?:@@?[^ ]+)?', name) or re.match(
            r'(?:(?:(?:non-virtual|virtual) thunk to|typeinfo(?: name)? for|vtable for|VTT for|guard variable for) )?'
            r'(?:LibRaw(?:_[A-Za-z0-9_]+)?(?:::|$)|rawspeed::)', name
        ):
            result.append(name)
    return result


def copy_closure(directory, target, names):
    pending = list(names)
    copied = set()
    while pending:
        name = pending.pop()
        if name.lower() in copied:
            continue
        path = directory / name
        require(path.is_file(), f'Missing deployed dependency {path}')
        shutil.copy2(path, target / name)
        copied.add(name.lower())
        pending.extend(dependency for dependency in pe_imports(path) if (directory / dependency).is_file())
    return copied


def check(args):
    suffix = '_d' if args.config == 'debug' else ''
    report = {'configuration': args.config, 'platform': sys.platform}
    if os.name == 'nt':
        private_suffix = 'd' if suffix else ''
        engine = f'AfterNightEngine{suffix}.dll'
        stock_raw = f'raw_r{private_suffix}.dll'
        private_packages = [f'afternight_libraw_r{private_suffix}.dll', f'afternight_rawspeed{private_suffix}.dll']
        libraries = [
            engine, stock_raw, *private_packages]
        with tempfile.TemporaryDirectory(prefix='afternight-raw-relocation-') as temporary:
            target = Path(temporary)
            closure = copy_closure(args.bin, target, [*libraries, 'RawEngineBench.exe', 'RawPrivateOracle.exe', 'RawRuntimeProbe.exe'])
            report['closure'] = {name: digest(target / name) for name in sorted(closure)}
            # Load in a fresh process, so imports cannot be accidentally satisfied by prior loaded modules.
            loaded = subprocess.run([str(target / 'RawRuntimeProbe.exe'), args.config], cwd=target,
                                    text=True, capture_output=True, timeout=60)
            require(loaded.returncode == 0, loaded.stderr)
            report['coLoad'] = loaded.stdout.strip()
            engine_imports = pe_imports(target / engine)
            require(private_packages[0].lower() in engine_imports and stock_raw.lower() not in engine_imports,
                    'the engine resolves RAW through the stock package')
            for name in private_packages:
                imports = pe_imports(target / name)
                require(not any(any(x in dependency for x in ('vcomp', 'libomp', 'iomp', 'gomp')) for dependency in imports),
                        'OpenMP imported by private package')
            if args.fault_library:
                fixtures = corpus(args)
                candidate = next(f for f in fixtures if f['requiredAcceleration'])
                request = {'path': candidate['localPath'], 'preference': 'automatic'}
                with Driver(target, 'RawEngineBench') as normal:
                    reference = normal.run({**request, 'preference': 'native'})
                    automatic = normal.run(request)
                require(automatic.get('selected') == 'rawspeed', 'Fault tests need a qualified Automatic profile')
                shutil.copyfile(args.fault_library, target / private_packages[1])
                report['faults'] = []
                try:
                    for status, recoverable in ((2, True), (3, True), (4, True), (7, True), (5, False), (1, False), (8, False)):
                        os.environ['AN_TEST_RAWSPEED_STATUS'] = str(status)
                        for preference in ('automatic', 'required'):
                            with Driver(target, 'RawEngineBench') as fault:
                                actual = fault.run({**request, 'preference': preference})
                            attempts = [e['name'] for e in actual['traces'] if e['name'].startswith('attempt.')]
                            if recoverable and preference == 'automatic':
                                require(actual.get('ok') and actual['selected'] == 'native' and
                                        actual['visibleHash'] == reference['visibleHash'] and actual['metadata'] == reference['metadata'] and
                                        attempts == ['attempt.rawspeed', 'attempt.native'], 'Recoverable failure did not reopen exactly once')
                            else:
                                require(not actual.get('ok') and actual['engineLiveBytes'] == 0 and attempts == ['attempt.rawspeed'],
                                        'Terminal/required failure retried or published pixels')
                            report['faults'].append({'status': status, 'preference': preference, 'attempts': attempts,
                                                     'result': actual['status']})
                finally:
                    os.environ.pop('AN_TEST_RAWSPEED_STATUS', None)
    else:
        engine = args.bin / f'libAfterNightEngine{suffix}.so'
        libraries = [
            engine]
        for library in libraries:
            require(library.is_file(), f'Missing engine {library}')
        symbols = subprocess.check_output(['nm', '-D', '--defined-only', '--demangle', str(engine)], text=True)
        escaped = private_raw_symbols(symbols)
        require(not escaped, f'Private archive symbols escaped the engine DSO: {escaped}')
        dependencies = subprocess.check_output(['ldd', str(engine)], text=True)
        dynamic = subprocess.check_output(['readelf', '-d', str(engine)], text=True)
        named = unexpected_raw_dependencies(dynamic)
        require('not found' not in dependencies, 'Unresolved shared dependency')
        require(not named, 'Unexpected shared RAW/OpenMP dependency: %s' % ', '.join(named))
        env = os.environ.copy(); env['LD_LIBRARY_PATH'] = str(args.bin)
        loaded = subprocess.run([str(args.bin / 'RawRuntimeProbe'), args.config], cwd=args.bin,
                                env=env, capture_output=True, text=True, timeout=60)
        require(loaded.returncode == 0, loaded.stderr)
        report['coLoad'] = loaded.stdout.strip(); report['dependencies'] = dependencies
        report['dynamicSection'] = dynamic
        report['privateSymbolsHidden'] = True
    report['passed'] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--config', choices=('release', 'debug'), required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--fault-library', type=Path)
    parser.add_argument('--paths', type=Path)
    parser.add_argument('--root', type=Path)
    parser.set_defaults(download=False)
    args = parser.parse_args(); args.bin = args.bin.resolve()
    result = check(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'Runtime identity, isolation and relocation: PASS; {args.output}')


if __name__ == '__main__':
    main()

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Verify licensed RAW identities before running the direct decoder-oracle suite locally."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST = REPO_ROOT / 'docs/dev/raw-codec-corpus.json'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def verify(root, records):
    """Return environment paths only after every expected identity and confined path matches."""
    root = Path(root).resolve()
    environment = {}
    for record in records:
        path = (root / record['path']).resolve()
        if not path.is_relative_to(root):
            raise ValueError('RAW fixture path escapes the corpus root')
        name = record['environment']
        if not name.startswith('AFTERNIGHT_TEST_RAW_') or name in environment:
            raise ValueError('invalid or duplicate RAW fixture environment name')
        if path.stat().st_size != record['bytes'] or digest(path) != record['sha256']:
            raise ValueError('RAW fixture identity mismatch: ' + record['path'])
        environment[name] = str(path)
    expected = {'AFTERNIGHT_TEST_RAW_' + suffix
                for suffix in ('CANON', 'CANON_CHANNEL_BLACK', 'NIKON', 'XPRO1', 'XT50', 'FOVEON')}
    if set(environment) != expected:
        raise ValueError('the RAW corpus must supply all six named camera files')
    return environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus-root', type=Path, required=True)
    parser.add_argument('--test-executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    records = json.loads(MANIFEST.read_text(encoding='utf-8'))['fixtures']
    try:
        environment = dict(os.environ, **verify(args.corpus_root, records))
        output = args.output.resolve()
        if output.is_relative_to(args.corpus_root.resolve()):
            raise ValueError('validation output must be outside the licensed input corpus')
        executable = args.test_executable.resolve(strict=True)
        output.mkdir(parents=True, exist_ok=True)
        environment['AFTERNIGHT_TEST_STORAGE_ROOT'] = str(output / 'storage')
        command = [str(executable), '--gtest_filter=LicensedCameras/RawCodecCorpusTest.*']
        with (output / 'tests.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(command, cwd=executable.parent, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT, check=False)
        report = {'testExecutable': str(executable), 'testSha256': digest(executable),
                  'fixtures': records, 'exitCode': result.returncode}
        (output / 'identity.json').write_bytes((json.dumps(report, indent=2) + '\n').replace('\n', '\r\n').encode())
        print((output / 'tests.log').read_text(encoding='utf-8'))
        return result.returncode
    except (OSError, ValueError) as error:
        print('RAW corpus validation refused: ' + str(error))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

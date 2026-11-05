# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Require the feature-disabled engine to retain every corpus output and refuse acceleration."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

from qualify import Driver, GOLDENS, corpus, digest, good_engine, manifest_identity, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--paths', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.set_defaults(download=False)
    args = parser.parse_args(); args.bin = args.bin.resolve()
    fixtures = corpus(args)
    document = json.loads(GOLDENS.read_text(encoding='utf-8'))
    require(document['manifestSha256'] == manifest_identity(), 'Golden manifest mismatch')
    goldens = {row['id']: row for row in document['cases']}
    report = {'configuration': args.bin.name, 'manifestSha256': manifest_identity(), 'cases': []}
    if os.name == 'nt':
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from deploy_raw_deps import pe_imports
        suffix = '_d' if args.bin.name.lower() == 'debug' else ''
        imports = pe_imports(args.bin / f'AfterNightEngine{suffix}.dll')
        require(any(name.startswith('raw_r') for name in imports) and
                not any(name.startswith('afternight_rawspeed') or name.startswith('afternight_libraw') for name in imports),
                'Feature-disabled engine still imports a private RAW package')
        report['imports'] = sorted(imports)
    with tempfile.TemporaryDirectory(prefix='an-raw-native-only-') as temporary, Driver(args.bin, 'RawEngineBench') as engine:
        dump = Path(temporary) / 'samples'
        for fixture in fixtures:
            for preference in ('native', 'automatic'):
                result = engine.run({'path': fixture['localPath'], 'preference': preference, 'dump': str(dump)})
                good_engine(result, 'native')
                golden = goldens[fixture['id']]
                require(digest(dump) == golden['visibleSha256'] and result['metadata'] == golden['metadata'] and
                        result['descriptorText'] == golden['descriptorText'], 'Feature-disabled native output changed')
            refused = engine.run({'path': fixture['localPath'], 'preference': 'required'})
            require(not refused['ok'] and refused['status'] == 'UnsupportedBackend' and refused['engineLiveBytes'] == 0,
                    'Feature-disabled RequireRawSpeed did not refuse without publishing')
            report['cases'].append({'id': fixture['id'], 'exact': True, 'required': refused['status']})
            print('PASS', fixture['id'], flush=True)
    report['passed'] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

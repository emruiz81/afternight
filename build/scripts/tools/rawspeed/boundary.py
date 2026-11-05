# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Run the complete corpus through isolated stock/private boundary binaries, including sanitizers."""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import subprocess
import tempfile

from qualify import Driver, GOLDENS, corpus, digest, exact_files, good_oracle, manifest_identity, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--wrapper', type=Path, required=True)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--paths', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.set_defaults(download=False)
    args = parser.parse_args()
    args.bin = args.bin.resolve()
    args.wrapper = args.wrapper.resolve()
    fixtures = corpus(args)
    goldens = json.loads(GOLDENS.read_text(encoding='utf-8'))
    require(goldens['manifestSha256'] == manifest_identity(), 'Golden manifest mismatch')
    expected = {row['id']: row for row in goldens['cases']}
    def wrapper(arguments):
        completed = subprocess.run([str(args.wrapper), *arguments], capture_output=True, text=True, timeout=180)
        require(completed.returncode == 0, completed.stdout + completed.stderr)
        return completed.stdout.strip()
    report = {'wrapper': wrapper([]), 'manifestSha256': manifest_identity(), 'cases': []}
    with tempfile.TemporaryDirectory(prefix='an-raw-boundary-') as temporary, ExitStack() as stack:
        work = Path(temporary)
        stock = stack.enter_context(Driver(args.bin, 'RawStockOracle'))
        private = stack.enter_context(Driver(args.bin, 'RawPrivateOracle'))
        for fixture in fixtures:
            reference = stock.run({'path': fixture['localPath'], 'dump': str(work / 'stock')})
            good_oracle(reference)
            for accelerated in ([False, True] if fixture['requiredAcceleration'] else [False]):
                actual = private.run({'path': fixture['localPath'], 'accelerated': accelerated, 'dump': str(work / 'private')})
                good_oracle(actual, accelerated)
                require(actual['metadata'] == reference['metadata'], 'Boundary metadata differs')
                for kind in ('sensor', 'visible'):
                    require(exact_files(work / ('stock.' + kind), work / ('private.' + kind)) and
                            digest(work / ('stock.' + kind)) == expected[fixture['id']][kind + 'Sha256'],
                            f'Boundary {kind} differs: {fixture["id"]}')
            row = {'id': fixture['id'], 'exact': True}
            if fixture['requiredAcceleration']:
                command = ['--file', fixture['localPath'], str(actual['family']), str(actual['rawWidth']),
                           str(actual['rawHeight']), str(actual['filters'])]
                if actual['filters'] == 9:
                    command.append(''.join(str(v) for values in actual['metadata']['xtransAbsolute'] for v in values))
                row['allocationInjection'] = wrapper(command)
            report['cases'].append(row)
            print('PASS', fixture['id'], flush=True)
    report['passed'] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

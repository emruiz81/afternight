# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Assert what a controller run claims about interleaving.

`randomInterleaving` is the one field in a run manifest that describes how a measurement was taken
rather than what it measured, and recording it without actually controlling execution order would be
an invalid run. That makes it worth checking from outside the tool that writes it:
a refusal that silently stops refusing looks exactly like a refusal that works.

Continuous integration runs the controller with --dry-run, where the honest answer is false. A
measurement run on a benchmark host expects true. Either way the expectation is stated on the
command line rather than inferred, so the check fails loudly when the two disagree.
"""

import argparse
import json
import os
import sys

MANIFESTS = ("baseline.json", "candidate.json")


def check_run(run_dir, expected, stream):
    """Return 0 when both manifests in `run_dir` carry the expected interleaving claim."""
    problems = []
    for name in MANIFESTS:
        path = os.path.join(run_dir, name)
        try:
            with open(path) as handle:
                manifest = json.load(handle)
        except (OSError, ValueError) as error:
            problems.append("%s: could not be read (%s)" % (name, error))
            continue
        if "randomInterleaving" not in manifest:
            problems.append("%s: has no randomInterleaving field" % name)
            continue
        actual = bool(manifest["randomInterleaving"])
        if actual != expected:
            problems.append("%s: claims randomInterleaving=%s, expected %s"
                            % (name, str(actual).lower(), str(expected).lower()))

    for problem in problems:
        stream.write(problem + "\n")
    if problems:
        return 1
    stream.write("both manifests report randomInterleaving=%s, as expected\n"
                 % str(expected).lower())
    return 0


def main(argv=None, stream=None):
    stream = stream or sys.stdout
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--run-dir", dest="run_dir", required=True)
    parser.add_argument("--expect", choices=("true", "false"), required=True)
    try:
        args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    except SystemExit:
        return 3
    return check_run(args.run_dir, args.expect == "true", stream)


if __name__ == "__main__":
    sys.exit(main())

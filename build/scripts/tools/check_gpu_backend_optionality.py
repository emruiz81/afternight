# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prove that the GPU backend stays opt-in and that the default toolchain installs no GPU SDK.

One property outranks the whole GPU proof: it must not leave a dependency behind. A contributor who
has never installed a GPU toolkit generates, builds, tests, deploys and packages the engine with every
check passing, and no GPU SDK becomes part of the documented toolchain.
That is a property of three files nobody reads while it holds, so it is read here:

1. neither dependency script - `02-install_msvc_deps.bat` nor `02-install_linux_deps.sh` - names a GPU
   SDK, and neither does a repository script either one calls. A comment is not an installation and
   is not read;
2. the opt-in installer that does install one puts it in a vcpkg triplet of its own, where no default
   project's include or library directory can reach it; and
3. both Premake workspaces generate the backend only when `AFTERNIGHT_OPENCL=1` asks for it, so a tree
   generated with nothing set contains no GPU target at all.

The generated graph's half of the same property - that no target but the backend reaches the SDK even
in an opted-in tree - is `check_build_graph.py`'s.

A GPU SDK is recognised by name, as a token rather than a substring, so a package that merely contains
the letters is not one.

Usage:
    python build/scripts/tools/check_gpu_backend_optionality.py [--repo-root <dir>]

Exits 0 when every rule holds, 1 otherwise, printing one line per problem.

Tests: build/scripts/tools/check_gpu_backend_optionality_test.py
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

MSVC_DEPENDENCY_SCRIPT = 'build/scripts/msvc/02-install_msvc_deps.bat'
LINUX_DEPENDENCY_SCRIPT = 'build/scripts/linux/02-install_linux_deps.sh'
DEPENDENCY_SCRIPTS = (MSVC_DEPENDENCY_SCRIPT, LINUX_DEPENDENCY_SCRIPT)

# The one script that installs a GPU SDK, and the triplet it must install into.
OPT_IN_INSTALLER = 'build/scripts/msvc/install_opencl_backend_deps.bat'
OPT_IN_TRIPLET = 'x64-windows-opencl'

# The line each workspace definition must carry. Spelled out whole, because the property is its
# polarity: `== "1"` is off unless asked for, where the adapters' `~= "0"` is on unless refused.
PREMAKE_DEFINITIONS = ('build/premake/afternight-pm5.lua', 'build/premake/afternight-test-pm5.lua')
OPT_IN_GATE = 'local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") == "1"'

# The names a GPU compute SDK or its loader goes by in a vcpkg port, a distribution package or a
# toolkit: OpenCL and its distribution loader, CUDA and its compiler and libraries, SYCL and oneAPI's
# compiler, ROCm and HIP, and oneAPI Level Zero.
GPU_SDK_TOKENS = ('cuda', 'cudnn', 'dpcpp', 'hip-sdk', 'hipsdk', 'level-zero', 'nvcc', 'nvrtc', 'ocl-icd',
                  'opencl', 'rocm', 'sycl')

_TOKEN = re.compile(r'(?<![A-Za-z0-9])(%s)(?![A-Za-z0-9])'
                    % '|'.join(re.escape(token) for token in sorted(GPU_SDK_TOKENS, key=len, reverse=True)),
                    re.IGNORECASE)
_CALLED_SCRIPT = re.compile(r'([^\s"\'/\\%]+\.(?:bat|sh))\b', re.IGNORECASE)
_VCPKG_INSTALL = re.compile(r'\bvcpkg(?:\.exe)?\s+install\b', re.IGNORECASE)
_TRIPLET = re.compile(r'--triplet[=\s]+([A-Za-z0-9_-]+)')


def is_comment(line, suffix):
    """Whether one script line is a comment in that script's language."""
    stripped = line.strip()
    if suffix == '.sh':
        return stripped.startswith('#')
    lowered = stripped.lower()
    return lowered.startswith('::') or lowered in ('rem', '@rem') or lowered.startswith(('rem ', '@rem '))


def statements(path):
    """Every non-comment line of a script, with its one-based number."""
    text = path.read_text(encoding='utf-8', errors='replace')
    for number, line in enumerate(text.splitlines(), start=1):
        if line.strip() and not is_comment(line, path.suffix.lower()):
            yield number, line


def gpu_sdk_tokens(line):
    """The GPU SDK names one line uses, lowercased, sorted and without repeats."""
    return sorted({match.group(1).lower() for match in _TOKEN.finditer(line)})


def called_scripts(path):
    """The repository scripts a script calls, resolved beside it, in the order they are first named."""
    called = []
    for _, line in statements(path):
        for match in _CALLED_SCRIPT.finditer(line.replace('%~dp0', '')):
            candidate = path.parent / match.group(1)
            if candidate.is_file() and candidate != path and candidate not in called:
                called.append(candidate)
    return called


def check_dependency_scripts(repo_root):
    problems = []
    for relative in DEPENDENCY_SCRIPTS:
        path = repo_root / relative
        if not path.is_file():
            problems.append('%s: not found' % relative)
            continue
        for number, line in statements(path):
            tokens = gpu_sdk_tokens(line)
            if tokens:
                problems.append('%s:%d names a GPU SDK (%s); the default toolchain installs none'
                                % (relative, number, ', '.join(tokens)))
        for called in called_scripts(path):
            called_relative = called.relative_to(repo_root).as_posix()
            for number, line in statements(called):
                tokens = gpu_sdk_tokens(line)
                if tokens:
                    problems.append('%s:%d names a GPU SDK (%s), and %s calls it; the default toolchain '
                                    'installs none' % (called_relative, number, ', '.join(tokens), relative))
    return problems


def check_opt_in_installer(repo_root):
    path = repo_root / OPT_IN_INSTALLER
    if not path.is_file():
        return ['%s: not found' % OPT_IN_INSTALLER]
    problems = []
    installs = 0
    for number, line in statements(path):
        if not _VCPKG_INSTALL.search(line):
            continue
        installs += 1
        triplet = _TRIPLET.search(line)
        if triplet is None:
            problems.append('%s:%d names no triplet, so it would install into the default one, where a '
                            'default project could reach it' % (OPT_IN_INSTALLER, number))
        elif triplet.group(1) != OPT_IN_TRIPLET:
            problems.append('%s:%d installs into %s rather than %s, where a default project could reach it'
                            % (OPT_IN_INSTALLER, number, triplet.group(1), OPT_IN_TRIPLET))
    if installs == 0:
        problems.append('%s: installs nothing, so the opt-in backend has no SDK to compile against'
                        % OPT_IN_INSTALLER)
    return problems


def check_premake_gates(repo_root):
    problems = []
    for relative in PREMAKE_DEFINITIONS:
        path = repo_root / relative
        if not path.is_file():
            problems.append('%s: not found' % relative)
            continue
        lines = {line.strip() for line in path.read_text(encoding='utf-8', errors='replace').splitlines()}
        if OPT_IN_GATE not in lines:
            problems.append('%s does not gate the GPU backend on AFTERNIGHT_OPENCL=1 with the line: %s'
                            % (relative, OPT_IN_GATE))
    return problems


def check(repo_root):
    return check_dependency_scripts(repo_root) + check_opt_in_installer(repo_root) + check_premake_gates(repo_root)


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT),
                        help='repository root to check; defaults to the one containing this script')
    arguments = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout

    problems = check(pathlib.Path(arguments.repo_root).resolve())
    for problem in problems:
        print(problem, file=out)
    if problems:
        print('%d GPU backend optionality problem(s)' % len(problems), file=out)
        return 1
    print('GPU backend optionality holds: no GPU SDK in %s, the opt-in installer uses %s, and both '
          'workspaces gate the backend on AFTERNIGHT_OPENCL=1' % (' or '.join(DEPENDENCY_SCRIPTS), OPT_IN_TRIPLET),
          file=out)
    return 0


if __name__ == '__main__':
    sys.exit(main())

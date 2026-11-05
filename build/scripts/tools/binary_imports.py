# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""What a built binary imports, read from the binary itself through a symbol reader.

The deploy, payload and safety checks each need the libraries a binary loads, which no build definition
states for a library loaded through another one. One reader serves them all: `llvm-readobj` prints a PE
file's import table and an ELF file's needed libraries in two shapes, and both are read here.

Tests: build/scripts/tools/binary_imports_test.py
"""

import glob
import os
import re
import shutil
import subprocess


def find_reader(tool=None):
    """llvm-readobj, the one named, or a versioned install of it on this host; None when there is none.

    GNU readelf is not a substitute: it has no table of needed libraries in either shape this reads."""
    if tool:
        return shutil.which(tool) or (tool if os.path.isfile(tool) else None)
    for name in ('llvm-readobj', 'readobj'):
        found = shutil.which(name)
        if found:
            return found
    versioned = sorted(glob.glob('/usr/bin/llvm-readobj-*') + glob.glob('/usr/lib/llvm-*/bin/llvm-readobj'),
                       key=lambda path: [int(part) for part in re.findall(r'\d+', path)], reverse=True)
    return versioned[0] if versioned else None


def imported_names(output):
    """The libraries one binary imports, from either shape a symbol reader prints."""
    names = []
    inside_needed = False
    for line in output.split('\n'):
        stripped = line.strip()
        if stripped.startswith('NeededLibraries ['):
            inside_needed = True
            continue
        if inside_needed:
            if stripped.startswith(']'):
                inside_needed = False
            elif stripped:
                names.append(stripped)
            continue
        if stripped.startswith('Name:'):
            names.append(stripped.split(':', 1)[1].strip())
    return names


def is_engine_module(name, engine):
    """Whether a library file name, in any platform's spelling, is `engine`'s."""
    stem = re.sub(r'\.(dll|so|dylib|lib|a)(\.\d+)*$', '', name, flags=re.IGNORECASE)
    stem = re.sub(r'^lib', '', stem)
    stem = re.sub(r'_d$', '', stem)
    return stem.lower() == engine.lower()


def read_imports(tool, path):
    """One binary's imports, asking the reader for whichever table its format has."""
    with path.open('rb') as handle:
        magic = handle.read(4)
    flag = '--coff-imports' if magic[:2] == b'MZ' else '--needed-libs'
    result = subprocess.run([tool, flag, str(path)], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        return None
    return imported_names(result.stdout)

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Deploy the private RAW runtime, refusing missing packages and conflicting DLL identities."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct

REPO = Path(__file__).resolve().parents[3]
# The private packages deployment names, each with its debug suffix to fill; check_installer_payload.py reads
# this to keep them out of the installers.
PRIVATE_LIBRARIES = ('afternight_libraw_r{suffix}.dll', 'afternight_rawspeed{suffix}.dll')


def pe_imports(path: Path) -> set[str]:
    """Read both ordinary and delay imports without depending on a compiler at deployment time."""
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError(f'Not a PE file: {path}')
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'Invalid PE signature: {path}')
    machine, sections = struct.unpack_from('<HH', data, pe + 4)
    if machine != 0x8664:
        raise ValueError(f'RAW runtime must be x64: {path}')
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError(f'RAW runtime must be PE32+: {path}')
    section_table = optional + optional_size

    def offset(rva: int) -> int:
        for index in range(sections):
            base = section_table + 40 * index
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from('<IIII', data, base + 8)
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                return raw_offset + rva - virtual_address
        raise ValueError(f'Invalid import RVA in {path}')

    def name(rva: int) -> str:
        start = offset(rva)
        return data[start:data.index(0, start)].decode('ascii').lower()

    imports: set[str] = set()
    for directory, stride, name_position in ((1, 20, 12), (13, 32, 4)):
        rva, length = struct.unpack_from('<II', data, optional + 112 + 8 * directory)
        if not rva:
            continue
        start = offset(rva)
        for position in range(start, start + length, stride):
            entry = data[position:position + stride]
            if not any(entry):
                break
            imports.add(name(struct.unpack_from('<I', entry, name_position)[0]))
    return imports


def deploy(configuration: str, toolchain: str, destination: Path | None = None) -> list[Path]:
    if os.environ.get('AFTERNIGHT_RAWSPEED') == '0':
        return []
    suffix = 'd' if configuration == 'debug' else ''
    subtree = 'debug/bin' if suffix else 'bin'
    source = REPO / 'vcpkg/installed/x64-windows-raw' / subtree
    # The ports' own triplet, as 02-install_msvc_deps.bat installed them.
    general = REPO / 'vcpkg/installed' / os.environ.get('AFTERNIGHT_VCPKG_TRIPLET', 'x64-windows') / subtree
    destination = destination or REPO / 'bin' / toolchain / configuration
    required = [library.format(suffix=suffix) for library in PRIVATE_LIBRARIES]
    pending = list(required)
    checked: dict[str, Path] = {}
    while pending:
        filename = pending.pop()
        if filename in checked:
            continue
        library = source / filename
        if not library.is_file():
            raise FileNotFoundError(f'Package-present build requires {library}; run install_raw_deps.bat')
        checked[filename] = library
        for dependency in pe_imports(library):
            if any(marker in dependency for marker in ('vcomp', 'libomp', 'iomp', 'gomp')):
                raise ValueError(f'OpenMP runtime {dependency} imported by {filename}')
            if dependency.startswith('afternight_'):
                pending.append(dependency)
            elif (source / dependency).is_file():
                candidate = source / dependency
                for other in (general / dependency, destination / dependency):
                    if other.exists() and hashlib.sha256(candidate.read_bytes()).digest() != hashlib.sha256(other.read_bytes()).digest():
                        raise ValueError(f'Conflicting shared dependency {dependency}: {candidate} and {other}')
                pending.append(dependency)
            elif dependency in ('raw_r.dll', 'raw_rd.dll'):
                raise ValueError('Private LibRaw unexpectedly imports the stock decoder')
            elif not (dependency.startswith(('api-ms-win-', 'ext-ms-win-', 'msvcp140', 'vcruntime140')) or
                      dependency in ('kernel32.dll', 'ws2_32.dll', 'ucrtbased.dll', 'ucrtbase.dll')):
                raise FileNotFoundError(f'Undeclared shared RAW dependency {dependency} imported by {filename}')
    # Validate the complete closure before changing the deployed directory.
    destination.mkdir(parents=True, exist_ok=True)
    for filename, library in checked.items():
        shutil.copy2(library, destination / filename)
    return [destination / name for name in sorted(checked)]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', choices=('debug', 'release'), required=True)
    parser.add_argument('--toolchain', choices=('msvc', 'clang'), default='msvc')
    parser.add_argument('--destination', type=Path)
    args = parser.parse_args()
    for path in deploy(args.config, args.toolchain, args.destination):
        print(f'Deployed {path.name}')


if __name__ == '__main__':
    main()

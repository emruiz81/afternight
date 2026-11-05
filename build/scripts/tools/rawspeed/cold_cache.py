# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Evict only a verified fixture's Linux file pages and prove they are nonresident."""
from __future__ import annotations

import ctypes
import os
from pathlib import Path
import sys


def evict(path):
    if not sys.platform.startswith('linux'):
        raise RuntimeError('Verified file-cold measurement requires the Linux benchmark host')
    path = Path(path)
    libc = ctypes.CDLL(None, use_errno=True)
    libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int,
                          ctypes.c_int, ctypes.c_long]
    libc.mmap.restype = ctypes.c_void_p
    libc.mincore.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
    libc.mincore.restype = ctypes.c_int
    libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    libc.munmap.restype = ctypes.c_int
    descriptor = os.open(path, os.O_RDONLY)
    try:
        size = os.fstat(descriptor).st_size
        if size == 0:
            raise RuntimeError('A cold-cache fixture must not be empty')
        os.fsync(descriptor)
        os.posix_fadvise(descriptor, 0, 0, os.POSIX_FADV_DONTNEED)
        # PROT_NONE does not fault any data in. MAP_SHARED lets mincore inspect file residency.
        address = libc.mmap(None, size, 0, 1, descriptor, 0)
        if address == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_errno(), 'mmap for file-cache verification')
        try:
            pages = (size + os.sysconf('SC_PAGE_SIZE') - 1) // os.sysconf('SC_PAGE_SIZE')
            residency = (ctypes.c_ubyte * pages)()
            if libc.mincore(address, size, residency):
                raise OSError(ctypes.get_errno(), 'mincore for file-cache verification')
            resident = sum(value & 1 for value in residency)
            if resident:
                raise RuntimeError(f'Cannot claim file-cold timing: {resident}/{pages} fixture pages remain resident')
            return {'method': 'fsync, POSIX_FADV_DONTNEED, mincore', 'pages': pages, 'residentPages': resident}
        finally:
            libc.munmap(address, size)
    finally:
        os.close(descriptor)

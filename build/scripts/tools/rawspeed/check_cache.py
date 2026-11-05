# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prove private-package cache restore and invalidation using a disposable compiler copy."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

REPO = Path(__file__).resolve().parents[4]
WORK = REPO / 'obj/raw-cache-proof'


def run(name, args, env, expected=0):
    result = subprocess.run(args, cwd=REPO / 'vcpkg', env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding='utf-8', errors='replace')
    (WORK / (name + '.log')).write_text(result.stdout, encoding='utf-8')
    if (result.returncode == 0) != (expected == 0):
        raise RuntimeError(f'Cache proof {name} returned {result.returncode}; see {WORK / (name + ".log")}')
    print(f'{name}: expected outcome observed', flush=True)
    return result.stdout


def main():
    if os.name != 'nt':
        raise SystemExit('This is the mixed MSVC/clang-cl cache proof; run check_raw_cache.bat on Windows')
    original = Path(os.environ['AN_RAW_CLANG_CL']).resolve()
    if not original.is_file(): raise RuntimeError('The pinned compiler is missing')
    WORK.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    executable = str(REPO / 'vcpkg/vcpkg.exe')
    install = [executable, 'install', 'afternight-libraw', '--vcpkg-root=.', '--triplet=x64-windows-raw',
               '--overlay-ports=../build/vcpkg/overlay-ports', '--overlay-triplets=../build/vcpkg/triplets', '--recurse']
    remove = [executable, 'remove', 'afternight-rawspeed:x64-windows-raw', '--recurse', '--vcpkg-root=.']
    wrapper = REPO / 'build/vcpkg/overlay-ports/afternight-rawspeed/AnRawSpeed.cpp'
    saved_wrapper = wrapper.read_bytes()
    copied = WORK / 'llvm/bin/clang-cl.exe'; copied.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(original, copied)
    resources = Path(subprocess.check_output([str(original), '-print-resource-dir'], text=True).strip())
    shutil.copytree(resources / 'include', WORK / 'llvm/lib/clang' / resources.name / 'include', dirs_exist_ok=True)
    report = {'compilerSha256': hashlib.sha256(original.read_bytes()).hexdigest(), 'steps': []}
    try:
        # A source build supplies the baseline even on a machine whose binary cache started empty.
        run('remove-before-source', remove, env)
        run('clean-source', install + ['--binarysource=clear'], env)
        # Populate this exact identity in the ordinary local binary cache, then prove restore.
        run('remove-before-cache', remove, env)
        run('cache-populate', install, env)
        run('remove-before-restore', remove, env)
        run('cache-restore', install + ['--only-binarycaching'], env)
        report['steps'].append('clean Debug/Release source build and cache-only restore')
        wrapper.write_bytes(saved_wrapper + b'\r\n// Disposable ABI invalidation probe.\r\n')
        run('remove-before-wrapper-change', remove, env)
        run('wrapper-cache-miss', install + ['--only-binarycaching'], env, expected=1)
        wrapper.write_bytes(saved_wrapper)
        run('wrapper-restored-cache-hit', install + ['--only-binarycaching'], env)
        report['steps'].append('wrapper content change misses; restoring content restores the cache hit')
        copied_env = dict(env, AN_RAW_CLANG_CL=str(copied))
        run('remove-before-copied-compiler', remove, copied_env)
        run('copied-compiler-baseline', install, copied_env)
        # PE loaders ignore an overlay. Compiler behaviour/version stays the same, but its bytes
        # change at the SAME path, isolating the additional actual-executable hash input.
        with copied.open('ab') as stream: stream.write(b'AfterNight disposable compiler ABI probe\0')
        report['changedCompilerSha256'] = hashlib.sha256(copied.read_bytes()).hexdigest()
        run('remove-before-compiler-change', remove, copied_env)
        run('compiler-cache-miss', install + ['--only-binarycaching'], copied_env, expected=1)
        shutil.copy2(original, copied)
        run('compiler-restored-cache-hit', install + ['--only-binarycaching'], copied_env)
        report['steps'].append('same-path compiler byte change misses; restoring bytes restores the cache hit')
    finally:
        wrapper.write_bytes(saved_wrapper)
        run('remove-before-production-restore', remove, env)
        run('production-restore', install, env)
    report['passed'] = True
    (WORK / 'proof.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Cache identity proof passed: {WORK / "proof.json"}')


if __name__ == '__main__': main()

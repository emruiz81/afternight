# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Build and run the SIMD candidate comparison.

Standalone on purpose: no Premake project, no place in either workspace, nothing that a product
build can reach. Each candidate is compiled to its own object so object size is comparable, and the
sizes are recorded alongside the timings because code size is one of the things being decided.

Usage:
    python build/spike/simd/run_spike.py [--compiler msvc|clang] [--out <csv>]

The platform is detected rather than selected. Windows offers both compilers the project ships on
there; Linux offers clang, which is the toolchain its build scripts use.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
IS_WINDOWS = os.name == "nt"
TRIPLET = "x64-windows" if IS_WINDOWS else "x64-linux"
VCPKG = ROOT / "vcpkg" / "installed" / TRIPLET
OBJ = HERE / "obj"
BIN = HERE / "bin"

CANDIDATE_SOURCES = [
    "Scalar.cpp",
    "Autovec.cpp",
    "Highway.cpp",
    "HighwayDynamic.cpp",
    "Xsimd.cpp",
    "Sleef.cpp",
    "OpenCv.cpp",
]
DRIVER_SOURCE = "Main.cpp"

WINDOWS_INCLUDES = [HERE, VCPKG / "include", VCPKG / "include" / "opencv4"]
WINDOWS_LIBS = ["hwy.lib", "sleef.lib", "opencv_core4.lib"]

# The ISA baseline that reproduces MSVC's /arch:AVX2 on a compiler that has no such switch: AVX2
# with the FMA, F16C, BMI, BMI2, LZCNT and MOVBE that switch implies, plus the AES and PCLMUL it
# also implies and which Highway's SSE4 baseline requires. Highway's AVX2 target sits on that SSE4
# baseline, and a missing flag does not warn - it silently selects a narrower target and every
# number the run produces is a comparison of vector widths wearing a compiler's name. The harness
# prints the target it actually selected, so the run is checked against that rather than against
# this list being right.
LINUX_ISA_FLAGS = [
    "-mavx2",
    "-mfma",
    "-mf16c",
    "-mbmi",
    "-mbmi2",
    "-mlzcnt",
    "-mmovbe",
    "-maes",
    "-mpclmul",
]


def vs_env():
    """Return the environment of a VS x64 developer prompt, without depending on vswhere."""
    vcvars = pathlib.Path(
        r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    )
    if not vcvars.exists():
        raise SystemExit(f"vcvars64.bat not found at {vcvars}")

    completed = subprocess.run(
        f'"{vcvars}" >nul 2>&1 && set',
        shell=True,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise SystemExit("failed to initialize the Visual Studio environment")

    env = {}
    for line in completed.stdout.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            env[key] = value
    return env


def tool(name, env):
    """Absolute path to a build tool. Windows resolves an executable against the parent process's
    PATH, not the one handed to subprocess, so passing a bare name here silently misses the
    developer environment we just built."""
    found = shutil.which(name, path=env.get("PATH", ""))
    if found is None:
        raise SystemExit(f"{name} not found in the Visual Studio environment")
    return found


def pkg_config(*arguments):
    """OpenCV is the one dependency the Linux product takes from the distribution rather than from
    vcpkg, so its paths are asked for rather than assumed."""
    completed = subprocess.run(["pkg-config", *arguments], capture_output=True, text=True)
    if completed.returncode != 0:
        raise SystemExit("pkg-config could not describe opencv4; is libopencv-dev installed?")
    return completed.stdout.split()


def linux_includes():
    return ["-I", str(HERE), "-I", str(VCPKG / "include")] + pkg_config("--cflags-only-I", "opencv4")


def compile_one(source, compiler, env):
    """Compile one translation unit and return (object path, size in bytes)."""
    if IS_WINDOWS:
        obj = OBJ / (pathlib.Path(source).stem + ".obj")
        includes = []
        for path in WINDOWS_INCLUDES:
            includes += ["/I", str(path)]

        if compiler == "msvc":
            cc = tool("cl.exe", env)
            flags = ["/nologo", "/c", "/std:c++20", "/O2", "/arch:AVX2", "/EHsc", "/GS-"]
        else:
            cc = tool("clang-cl.exe", env)
            # /arch:AVX2 is NOT equivalent between the two compilers, and the difference is silent.
            # MSVC's implies AES; clang-cl's does not. Highway's SSE4 baseline requires __AES__, its
            # AVX2 baseline requires the SSE4 baseline, so without -maes Highway falls all the way back
            # to SSSE3 and every kernel runs 128-bit while the MSVC build runs 256-bit. Nothing warns.
            # Comparing the compilers without this flag compares vector widths and calls the result a
            # compiler difference.
            flags = [
                "/nologo",
                "/c",
                "/std:c++20",
                "/O2",
                "/arch:AVX2",
                "-maes",
                "/EHsc",
                "-Wno-unused-command-line-argument",
            ]
        command = [cc] + flags + includes + [f"/Fo{obj}", str(HERE / source)]
    else:
        obj = OBJ / (pathlib.Path(source).stem + ".o")
        cc = compiler_path(compiler)
        # /GS- has no switch of its own here; -fno-stack-protector is the same request, and Ubuntu
        # enables the checks by default, so leaving it out would price a security feature as a
        # library difference.
        flags = ["-c", "-std=c++20", "-O2"] + LINUX_ISA_FLAGS + [
            "-fno-stack-protector",
            "-Wno-unused-command-line-argument",
        ]
        command = [cc] + flags + linux_includes() + ["-o", str(obj), str(HERE / source)]

    completed = subprocess.run(command, env=env, capture_output=True, text=True)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout + completed.stderr)
        raise SystemExit(f"compile failed: {source}")
    return obj, obj.stat().st_size


def compiler_path(compiler):
    name = {"clang": "clang++", "gcc": "g++"}[compiler]
    found = shutil.which(name)
    if found is None:
        raise SystemExit(f"{name} not found on PATH")
    return found


def link(objects, compiler, env):
    """Link the candidates and the driver into one executable and return its path."""
    if IS_WINDOWS:
        exe = BIN / f"simd_spike_{compiler}.exe"
        command = [tool("link.exe", env), "/nologo", f"/OUT:{exe}", f"/LIBPATH:{VCPKG / 'lib'}"] + [
            str(o) for o in objects
        ] + WINDOWS_LIBS
    else:
        exe = BIN / f"simd_spike_{compiler}"
        command = (
            [compiler_path(compiler), "-o", str(exe)]
            + [str(o) for o in objects]
            + [f"-L{VCPKG / 'lib'}", "-lhwy", "-lsleef"]
            + pkg_config("--libs-only-L", "opencv4")
            + ["-lopencv_core", "-lm"]
        )

    completed = subprocess.run(command, env=env, capture_output=True, text=True)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout + completed.stderr)
        raise SystemExit("link failed")
    return exe


def main():
    default_compiler = "msvc" if IS_WINDOWS else "clang"
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--compiler",
        choices=["msvc", "clang"] if IS_WINDOWS else ["clang", "gcc"],
        default=default_compiler,
    )
    parser.add_argument("--out", default=str(HERE / "results.csv"))
    parser.add_argument("--sizes-out", default=str(HERE / "objectsizes.csv"))
    parser.add_argument("--drift-out", default=str(HERE / "drift.csv"))
    # A laptop measures its sustained clocks, not its first-second clocks, so the measured run has
    # to start from thermal steady state rather than from whatever the build left behind. These two
    # options are how the run protocol is stated in the command rather than in someone's memory:
    # settle after the build, then burn one full pass whose output is discarded, then measure.
    parser.add_argument("--settle-seconds", type=float, default=0.0)
    parser.add_argument("--thermal-warmup-runs", type=int, default=0)
    args = parser.parse_args()

    OBJ.mkdir(parents=True, exist_ok=True)
    BIN.mkdir(parents=True, exist_ok=True)
    env = vs_env() if IS_WINDOWS else dict(os.environ)

    sizes = {}
    objects = []
    for source in CANDIDATE_SOURCES + [DRIVER_SOURCE]:
        obj, size = compile_one(source, args.compiler, env)
        objects.append(obj)
        sizes[pathlib.Path(source).stem] = size
        print(f"compiled {source:22} {size:>9} bytes")

    exe = link(objects, args.compiler, env)
    print(f"linked {exe}")

    if IS_WINDOWS:
        # The OpenCV and Highway DLLs have to sit next to the executable.
        for dll in (VCPKG / "bin").glob("*.dll"):
            target = BIN / dll.name
            if not target.exists():
                shutil.copy2(dll, target)

    # From the repository root, because the harness's own output paths are relative to it. The
    # warm-up passes write their drift table to a scratch path so a discarded run cannot leave a
    # recorded artifact behind.
    command = [str(exe), str(pathlib.Path(args.drift_out).resolve())]
    if args.settle_seconds > 0.0:
        print(f"settling for {args.settle_seconds:g}s")
        time.sleep(args.settle_seconds)
    for index in range(args.thermal_warmup_runs):
        print(f"thermal warm-up run {index + 1}/{args.thermal_warmup_runs} (discarded)")
        subprocess.run(
            [str(exe), str(BIN / "drift-warmup.csv")], cwd=ROOT, capture_output=True, text=True
        )
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    sys.stderr.write(completed.stderr)
    pathlib.Path(args.out).write_text(completed.stdout, encoding="utf-8", newline="\r\n")
    print(f"wrote {args.out} ({completed.returncode=})")

    with open(args.sizes_out, "w", encoding="utf-8", newline="\r\n") as handle:
        handle.write("candidate,object_bytes\n")
        for name, size in sizes.items():
            handle.write(f"{name},{size}\n")
    print(f"wrote {args.sizes_out}")

    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())

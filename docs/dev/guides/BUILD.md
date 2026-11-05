# Building

How the engine, its adapters and its suites are built on Windows and Linux, what the generation
switches change, and the invariants the build graph is held to. The repository-wide instructions -
dependency installation, the numbered scripts, the product - are in `AGENTS.md` and `CLAUDE.md`; this
page is the engine's part of them.

## Platforms and toolchains

| Platform | Toolchain | Project tree | Binaries |
|---|---|---|---|
| Windows 11, x86-64 | MSVC (Visual Studio 2026) | `build/vs2026-msvc` | `bin/msvc/<config>` |
| Ubuntu, x86-64 | Clang | `build/make-linux` | `bin/clang/<config>` |

A second Windows tree, `build/vs2026-clang`, builds the same sources with clang-cl into
`bin/clang-cl/<config>`. It is non-shipping and exists because it is the one place on Windows that
catches several classes of defect - an above-floor instruction set, Clang's diagnostics - before a
Linux build does.

Pull requests to `main` run `.github/workflows/ci.yml`: the Linux format check over the complete
changed C/C++ files under `src/`, and the Windows and Linux builds with every `Test_*` suite. The
checks and campaigns in [TESTING.md](TESTING.md) run locally.

## The short loop

Premake generates the projects; there is no CMake. Sources are globbed, so a new file needs the
project generation step again and nothing else:

```text
build\scripts\msvc\01-create_project_files.bat
build\scripts\msvc\build_afternightengine.bat release --with-tests
```

```text
build/scripts/linux/01-create_project_files.sh
build/scripts/linux/build_afternightengine.sh release --with-tests
```

The target-select script builds the engine, both adapters and - with `--with-tests` - the direct
suites, out of the generated graph and nothing else in the workspace. `--toolchain clang` selects the
clang-cl tree on Windows. A change to the engine alone does not need a full product build: the
isolation invariants below keep the product graph unaffected, and the full-product numbered chain
is what a change that touches the product builds.

Release is the per-change configuration. Debug is built for a change to a public layout, the ABI or
the build itself.

**After an interrupted MSVC build**, run `build\scripts\msvc\check_msbuild_dependency_tracking.bat
--fix` before trusting the next one. An object compiled against an older revision of a header links
silently beside the current one, and the symptom - a crash that appears when a struct grows and
vanishes when it shrinks - looks like a memory defect and is not one. The build scripts run the check
themselves.

## Generation switches

Each is an environment variable read when the projects are generated, not when they are built.

| Switch | Default | Effect |
|---|---|---|
| `AFTERNIGHT_PYTHON=0` | on | Builds the Python adapter without Python: it reports itself absent and refuses every call |
| `AFTERNIGHT_GMIC=0` | on | The same for the G'MIC adapter |
| `AFTERNIGHT_RAWSPEED=0` | on | Builds without the private RawSpeed decoder, leaving LibRaw's own |
| `AFTERNIGHT_OPENCL=1` | off | Adds the opt-in device backend and its one suite; needs the SDK installed first |
| `AFTERNIGHT_SANITIZE=address` | off | Instruments the engine, its suites, the benchmark layer and the product's libraries and suites into an object directory of their own, `obj/sanitize-<name>/` |
| `AFTERNIGHT_SANITIZE=thread` | off | The thread sanitizer; a Clang arrangement that refuses to generate on Windows |
| `AFTERNIGHT_FUZZ=1` | off | On Clang, the libFuzzer generation: every library and suite instrumented with coverage counters and the address and undefined-behaviour sanitizers into `obj/sanitize-<name>/` as `fuzzer`, and the fuzz targets linked with libFuzzer; it refuses to be asked for beside `AFTERNIGHT_SANITIZE` |

An instrumented tree writes to its own object directory so an ordinary build cannot mix with it.
Switching a tree in or out of a sanitizer needs the projects generated again and a rebuild; a suite
binary left from the other configuration still runs and reports on code that is not the tree's.

## Install, deploy and test switches

These are read when the scripts run.

| Switch | Default | Effect |
|---|---|---|
| `AFTERNIGHT_VCPKG_TRIPLET=x64-windows-release` | `x64-windows` | The triplet `build/scripts/msvc/02-install_msvc_deps.bat` installs the ports into and `build/scripts/msvc/04a-deploy_deps_release.bat` and `build/scripts/msvc/09a-build_afternight_tests_release.bat` deploy from; continuous integration names the release-only one, which has no Debug libraries |
| `AFTERNIGHT_SKIP_TESTS=<names>` | none | Executables `build/scripts/msvc/run_tests.bat` and `build/scripts/linux/run_tests.sh` leave out, separated by spaces |
| `AFTERNIGHT_TEST_RESULTS_DIR=<directory>` | none | Where the two runners write each executable's gtest JSON report |
| `AFTERNIGHT_SKIP_RECORDS=<ids>` | none | Behaviour records `Test_ProcessesSpatial` leaves out, by id and separated by spaces, each noted in its report; the Linux workflow names the one the hosted runners answer differently from the machine that captured it |

Each install script also runs one stage when asked: `02-install_msvc_deps.bat vcpkg` installs the vcpkg ports
and the private RAW packages and stops, and `02-install_linux_deps.sh system` installs the distribution packages
and `02-install_linux_deps.sh vcpkg` the ports and the private RAW packages. With no argument each installs
everything, and either fails at the first package or port it could not install.

## What the build graph is held to

`build/scripts/tools/check_build_graph.py` reads the generated projects - both the Microsoft ones and
the `make-linux` tree - and fails when:

- a project the target registry (`build/scripts/tools/consumer_targets.txt`) declares is not generated,
  or one is generated that it does not declare;
- a target reaches the engine, through its libraries, its headers or its sources, when its registry
  class excludes it - the engine-neutral benchmark and fixture layers, the Lite host, the protocol and
  platform libraries among them - or reaches more of the product than its rules allow;
- anything but `Test_EnginePythonAdapter` and `Test_EngineGmicAdapter` links an optional adapter, or the
  engine links an adapter or the library it adapts;
- a target other than the opt-in backend and its suite reaches a GPU SDK;
- two projects, or two generated trees, write the same binary, symbol file or object directory;
- a project is compiled at an instruction set above the packaged floor, or leaves implicit fused
  multiply-add on;
- any target on the engine's side of the workspace compiles through a precompiled header or a forced
  include.

Its companions read what a generated graph cannot see: `check_include_layers.py` (which layer may
include which library), `check_header_architecture.py` (header size and the empty
explicit-instantiation register) and `check_budgets.py` (include counts, compile time and
per-instruction-set object size per unit, per host, against `docs/dev/budgets.json`).

## Deployment and packaging

The build deploys the engine, both adapters and their runtime libraries beside the product in
`bin/<toolchain>/<config>`. Generating the projects removes what a target the registry no longer
builds left there and in the generated trees (`active_targets.py invalidate`): left in place, its
binary would pass for current. `check_deployed_runtimes.py --binaries <dir>` reads a deployed
directory against the generated graph: every binary the graph places there is present with
the symbol file its configuration produces, no two names differ only in case, and no binary of this
product is left there that no project produces any more. On Linux a binary that links Qt finds it
through one run path, `lib/` beside it, so the check also holds every Qt library a binary there needs
to be in `lib/` - Qt Test among them, which the deployment places there for the suites alone - and
every library a binary the product ships needs, and what that loads, to resolve in the deployment or
the system. The deployment step finds what to copy through the build's Qt for the same reason.

Nothing ships because it was built beside the product. Both installers copy the deployed directory
minus patterns, and `check_installer_payload.py` holds those patterns to everything that lands there
from outside the two solutions: the Python adapter, the opt-in backend, the benchmark runners, the
fuzz registry, the build record, the RAW qualification tools, the library parity suite's reference
libraries and, on Linux, Qt Test.

## Formatting and warnings

The engine builds with zero warnings under `/WX` on MSVC and `-Werror` on Clang, in both
configurations. clang-format 22 is the pinned formatter; `build\scripts\msvc\format_changed.bat` and
`build/scripts/format_changed.sh` format the changed lines, and the `check_clang_format` scripts check
them. Every file this engine owns uses CRLF line endings, except shell scripts that run on Linux.

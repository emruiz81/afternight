# Local validation

How a change to AfterNight is validated on the two platforms it ships on: the checks common to both, the
Windows and Linux commands, and the campaigns.

## Where work lands

- `main` is the development line, and work is tracked as GitHub issues. A pull request to `main` runs
  `.github/workflows/ci.yml`; the build, test and sanitizer workflows there run on demand.
- Validation runs locally. An agent runs the checks and suites the Windows host can run; when Linux
  evidence is required, the maintainer runs the Linux commands below on the Linux machine and supplies the
  result.
- Say what ran - the commands, the configurations, the suites and their counts, the checks, and anything
  that could not run - in the commit message or the pull request.

## Checks common to both platforms

Run the checks applicable to the changed surface from the repository root:

```text
python build/scripts/tools/check_include_layers.py
python build/scripts/tools/check_forbidden_constructs.py
python build/scripts/tools/check_process_state.py
python build/scripts/tools/check_public_documentation.py
python build/scripts/tools/check_engine_documentation.py
python build/scripts/tools/check_budgets.py check
python build/scripts/tools/check_build_graph.py
python build/scripts/tools/check_header_architecture.py
python build/scripts/tools/check_public_api.py
python build/scripts/tools/check_installer_payload.py
python build/scripts/tools/render_installer_artwork.py --check
python build/scripts/tools/check_deployed_runtimes.py --binaries bin/<toolchain>/<configuration>
python build/scripts/tools/check_gpu_backend_optionality.py
python build/scripts/tools/remove_gpu_proof.py
python build/scripts/tools/run_fuzz_campaign.py --check
python build/scripts/tools/run_sanitizer_campaign.py --check
python build/scripts/tools/run_fault_campaign.py --check
python build/scripts/tools/run_conformance_campaign.py --check
python build/scripts/tools/run_performance_campaign.py --check
python build/scripts/tools/check_dependency_provenance.py
python build/scripts/tools/check_safety_register.py
python build/scripts/tools/check_license_headers.py
python build/scripts/tools/check_markdown_links.py
python build/scripts/tools/generate_skimage_goldens.py check
python build/scripts/tools/check_consumer_state.py
python build/scripts/tools/check_consumer_layering.py
python build/scripts/tools/consumer_targets.py check
python build/scripts/tools/product_measurement.py check
```

Every check in that list but three answers from a checkout alone. `check_build_graph.py` and
`consumer_targets.py` read the projects `01-create_project_files` generates, and refuse to answer when no
generated tree is newer than the Premake definitions, the target registry and `isolated_sources.txt`.
`check_deployed_runtimes.py` answers from a built and deployed tree instead, so it is run once per configuration
after the deployment step and its counts are recorded; a directory that was never deployed is reported rather than
read as clean.

`check_budgets.py check` measures compile time with a C++ compiler and compares it with the host's recorded
baselines, so on a machine with none on `PATH` - this Windows host among them - it needs both
`--compiler <clang++>` (or `CXX`) and `--host-id bench-windows` or `--host-id bench-linux`, the host names
[the benchmarks guide](guides/BENCHMARKS.md) gives. Without a compiler it checks the include budgets alone and exits 1,
having measured nothing; without a host id it measures every unit and exits 0, but its last line says nothing was
compared, which is not a pass.

`check_license_headers.py` holds every source file AfterNight owns - C and C++ (`.h`, `.hpp`, `.cpp`, `.c`, `.inl`)
and Python - to its licence header: an `SPDX-License-Identifier` line and an `SPDX-FileCopyrightText` line before
its first line of code, stating `GPL-3.0-or-later`, or `Apache-2.0` on the extension protocol boundary, or the
licence a derived file's upstream grants. `--apply` inserts the header a file lacks and changes nothing else in it.
`check_markdown_links.py` reads every link of the developer documents - `docs/dev/`, the contributor guides, the
README and the repository's other root documents - and fails the one that leads to no file.

Run the Python test file paired with every changed script under `build/scripts/tools/`, and with every script
that imports one. Run clang-format on changed C/C++
files and clang-tidy over every changed `AfterNightEngine` source and header, and over every changed
`AfterNightPlatform` and `AfterNightConsumerRuntime` source and header with that library's own `.clang-tidy`. A check that cannot
run is recorded as unavailable; it is never replaced with a hosted job or described as passing.

A change that adds or removes an engine, adapter or backend source also reads the process-state
answer out of the built objects rather than only out of the source, once per library tree. Use the
Debug objects: the engine's Release objects are link-time code generation input rather than object
files, and a symbol tool refuses them:

```text
python build/scripts/tools/check_process_state.py --objects obj/AfterNightEngine/msvc/x64/Debug --symbol-tool <llvm-nm>
```

`llvm-nm.exe` ships beside `clang-format.exe` in the Visual Studio LLVM directory; on Linux the
toolchain's own `nm` reads that tree's `obj/AfterNightEngine/clang` the same way. The two modes are
meant to agree; record that they did.

## Windows validation

Generate projects and install dependencies through the numbered MSVC scripts before using the
target-select command. A dependency added since the last run needs `02-install_msvc_deps.bat`
again: the engine's vector kernels compile against the `highway[contrib]` port, and a tree
without it fails to compile one translation unit rather than quietly building a slower engine. Then run both
configurations when an ABI, build or layout change requires them:

```text
build\scripts\msvc\build_afternightengine.bat release --with-tests
build\scripts\msvc\build_afternightengine.bat debug --with-tests
```

With `AFTERNIGHT_TEST_STORAGE_ROOT` set to a unique temporary directory outside the production
AfterNight tree, run these from both `bin\msvc\release` and `bin\msvc\debug` as applicable:

```text
Test_EngineCore.exe
Test_EngineRuntime.exe
Test_EngineImage.exe
Test_EngineImageOps.exe
Test_EngineCalibration.exe
Test_EngineStars.exe
Test_EngineRegistration.exe
Test_EngineAstrometry.exe
Test_EngineColor.exe
Test_EngineStacking.exe
Test_EngineInterop.exe
Test_EngineKernels.exe
Test_EngineFaults.exe
Test_EngineFixtureBridge.exe
Test_EnginePublicHeaders.exe
Test_EngineConsumerProbes.exe
Test_EngineLibraryParity.exe
Test_EnginePythonAdapter.exe
Test_EngineGmicAdapter.exe
```

Run affected benchmark suites and the runner's dry-run in Release. Record the compiler version,
configuration, suite counts, checker results and benchmark bundle with the change.

### The functional conformance campaign

A change to what the registry covers runs the whole campaign rather than the affected part:

```text
python build/scripts/tools/run_conformance_campaign.py --output <evidence-directory> --config release
```

It holds `build/scripts/tools/conformance_matrix.txt` - every direct suite, and the cases that must run and pass
in them - to the generated tree in both directions, runs every registered suite, and reads each one's own report
rather than its exit code, because a suite that ran nothing exits zero. A suite registered `optional` runs when the
tree contains it and is reported as not generated when it does not. `--check` needs nothing built and is in the
common list above.

The campaign runs each suite in the environment it was started in, and reports by name every case a
suite skipped. Six of them are the licensed-camera cases, which are pointed at their files
rather than searching for them, so a session that does not export these six variables records six
skips instead of six comparisons against the independent decoder:

```text
AFTERNIGHT_TEST_RAW_CANON, AFTERNIGHT_TEST_RAW_NIKON, AFTERNIGHT_TEST_RAW_XPRO1,
AFTERNIGHT_TEST_RAW_XT50, AFTERNIGHT_TEST_RAW_FOVEON, AFTERNIGHT_TEST_RAW_CANON_CHANNEL_BLACK
```

The last one needs a file whose per-channel black levels are not all equal; the RAW qualification
evidence names which of the local fixtures those are. The paths are host-local and are not recorded
here or in the registry, for the reason a skip is never registered either: an environmental fact
belongs to the session, and what the campaign owes is to say which cases it cost.

### The optional adapters' two configurations

An optional adapter has to be validated in both of the configurations it claims to have, because the
absent one is a code path rather than an absence. The default build links the library the adapter
adapts; `AFTERNIGHT_PYTHON=0` and `AFTERNIGHT_GMIC=0` regenerate the project tree without one of
them, and that adapter then reports itself absent and refuses every call by name. A change that
touches an adapter runs its suite in both.

```text
set AFTERNIGHT_PYTHON=0
build\scripts\msvc\01-create_project_files.bat
build\scripts\msvc\build_afternightengine.bat release --with-tests
Test_EnginePythonAdapter.exe
set AFTERNIGHT_PYTHON=
build\scripts\msvc\01-create_project_files.bat
build\scripts\msvc\build_afternightengine.bat release --with-tests
Test_EnginePythonAdapter.exe
```

The G'MIC adapter is the same two runs with its own switch and its own suite, `AFTERNIGHT_GMIC=0`
and `Test_EngineGmicAdapter.exe`. It needs no installation to be stated, unlike the Python one: the
library carries its standard commands compiled into itself, so the enabled configuration's cases run
wherever `cgmic` is deployed beside the engine, and only the absent-path case skips there.

Both configurations pass with cases skipped rather than absent: the enabled build skips the
absent-path cases and the disabled build skips the interpreter cases, and in each one the passing
and skipped counts add up to the whole suite. Only the preprocessor define changes between them, so
a build system that compares timestamps rebuilds nothing - touch the adapter's sources, or clean its
object directory, between configurations, or the second run measures the first one's binary. A changed
link option alone does relink on Linux: every binary depends on its own makefile, which Premake rewrites
only when its text changes.

The interpreter cases need a Python installation and state where it is rather than searching for
one: `AFTERNIGHT_TEST_PYTHON_HOME` names it, and `PythonLib` beside the test executable is the
fallback,
which is where the deploy step puts one. A host with neither skips those cases and says so.

### Benchmarks and the performance campaign

How a measurement is taken and decided - the interleaving controller, the duration floor, the measurement hosts and
their thresholds, the performance and cancellation campaigns, and the reference dataset - is in
[the benchmarks guide](guides/BENCHMARKS.md). A change that claims an effect on speed or memory records there the
host, the controller's output and the decision; the performance registry's `--check` is in the common list above.

The derived inputs some rows read are prepared once, outside timing, and pinned by SHA-256 so both hosts time the same
bytes. The guided patch denoise's capture rows, for one, read a 512 x 512 crop at the centre of the Eagle and of the
Carina captures and both whole frames, every UInt16 code divided by `Float32(65535)`, from a manifest under the corpus
root that extends the raw-dump manifest:

```text
python build/scripts/tools/prepare_patch_denoise_inputs.py --corpus-root <corpus-root> --corpus-manifest <corpus-root>/RawDumps/corpus.json --out <corpus-root>/Prepared/patch-denoise-v1
```

### Transcendental providers and the backend conformance sweep

The vector transcendentals are compiled from the `highway[contrib]` port, so a tree whose
dependencies predate it fails to compile one translation unit rather than building a slower engine;
the dependency step above is what installs it.

`Test_EngineKernels` carries both halves of the evidence. `MathPrimitivesTest` measures the distance
between the compiled polynomials and the platform math library over a sweep that crosses the whole
Float32 exponent range and prints the worst distance it saw, in units in the last place, for the
exponential, the logarithm and two powers. Those printed numbers are the evidence a record of the run
quotes; a run that only says the case passed has recorded that the contract held and not what the
margin was.

`BackendConformanceTest` is the deterministic randomized differential sweep. It generates cases from
a fixed seed - shapes around every vector width, row padding, validity rules including a packed
mask, alias modes, thread counts and sample values including the exceptional ones - and runs each of
them on the reference backend and on every compiled backend this build contains and this host can
run. It reports how many backend runs it compared and how many cases both sides refused identically;
both numbers belong in the record of the run, because a sweep that compared nothing and a sweep that compared
everything print the same "OK" line otherwise. A failure names the subject, the case index and the
seed, and re-running the binary regenerates exactly that case.

The sweep is the same size on both hosts and the cases are identical, but what it covers is not:
`bench-linux` is the only measurement host whose build contains an AVX-512 rung, so it is the only
place where a compiled kernel above the packaged floor is compared with its oracle. A Windows run
that reports one candidate backend and a Linux run that reports two is the expected result rather
than a discrepancy.

## Linux validation supplied by the maintainer

The agent provides the exact applicable command list. The maintainer runs it on the Linux machine and
returns the output. The dependency step matters the same way it does on Windows and for
one more reason: `02-install_linux_deps.sh` installs the `highway[contrib]` port the vector kernels
compile against, and it is the only measurement host whose build contains an AVX-512 rung, so a run
here is the only place an optimized kernel above the packaged floor is compared with its oracle.
After the numbered Linux generator and dependency steps have been completed, the engine's ordinary
commands are:

```text
./build/scripts/linux/build_afternightengine.sh release --with-tests
./build/scripts/linux/build_afternightengine.sh debug --with-tests
```

Set `AFTERNIGHT_TEST_STORAGE_ROOT` to a unique `mktemp -d` directory, put
`bin/clang/<configuration>` first in `LD_LIBRARY_PATH`, and run the same direct suites from
each applicable configuration. The adapter configurations are the same two, with
`AFTERNIGHT_PYTHON=0 ./build/scripts/linux/01-create_project_files.sh` and
`AFTERNIGHT_GMIC=0 ./build/scripts/linux/01-create_project_files.sh` regenerating the disabled ones;
the touch-between-configurations caveat applies to the GNU Make front end for the same reason. Also
run this first-call case in a process of its own and reject an empty filter:

```text
Test_EngineRuntime --gtest_filter=CpuCapabilitiesTest.DetectionIsSafeToRaceOn
```

The Linux sanitizer campaigns are local, and they are generated rather than typed. Regenerate the
tree with the instrumentation, build it, and run the campaign, which applies the options and decides
every row:

```text
AFTERNIGHT_SANITIZE=address ./01-create_project_files.sh
./build_afternightengine.sh release --with-tests
python build/scripts/tools/run_sanitizer_campaign.py --configuration address
python build/scripts/tools/run_sanitizer_campaign.py --configuration leak
```

The generation applies `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=vptr` and
`-Wno-pass-failed`, and the matrix applies
`ASAN_OPTIONS=halt_on_error=1:print_stacktrace=1:detect_leaks=0:allocator_may_return_null=1` and
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`. `allocator_may_return_null=1` is required because
`Test_EngineRuntime` deliberately asks the platform allocator for an impossible size and checks that the
engine returns an allocation-free failure; without the option the sanitizer runtime aborts that
`nothrow` request instead of returning null, so the suite never reaches the behavior it is testing.
That request prints `WARNING: AddressSanitizer failed to allocate 0x8000000000000046 bytes` in the
runtime suite's output, which is the case working rather than a finding. The `leak` configuration is
the same build run again with `detect_leaks=1` and `build/scripts/tools/leak_suppressions.txt`, which
names what CPython, NumPy, pybind11 and PySide6 keep past a finalized interpreter and nothing of
AfterNight's. It does not print the suppressions it used: an extension host the suites start inherits the
options, and the cases read the host's output.

ThreadSanitizer is a second generation of the same tree:

```text
AFTERNIGHT_SANITIZE=thread ./01-create_project_files.sh
./build_afternightengine.sh release --with-tests
LD_LIBRARY_PATH=<instrumented-onetbb>:$LD_LIBRARY_PATH python build/scripts/tools/run_sanitizer_campaign.py --configuration thread
```

It applies `-fsanitize=thread -fno-omit-frame-pointer` and the matrix applies
`TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:allocator_may_return_null=1`. Before running it,
build the matching instrumented oneTBB with `build/scripts/linux/build_tsan_onetbb.sh` and put its
output directory first in `LD_LIBRARY_PATH`: the runner refuses every thread row without one, because
a sanitizer proves a happens-before edge by watching the code that establishes it and this engine's
edges are established inside that library. `Test_EngineCore` is skipped in that configuration because it
replaces global allocation functions and must still contain no thread-starting code, and the two
adapter suites and the library-parity suite are skipped because their concurrency crosses into
CPython, G'MIC and OpenCV, none of which is instrumented here. The matrix at
`build/scripts/tools/sanitizer_matrix.txt` states every row and every reason, and
`run_sanitizer_campaign.py --check` fails a suite that has left it.

libFuzzer is a third generation. Write each target's seeds from the ordinary
build first - the instrumented targets have no `--dump-corpus` - then regenerate, build and run the
instrumented half, fifteen minutes per target:

```text
python build/scripts/tools/run_fuzz_campaign.py --dump-corpus <scratch>/corpus
AFTERNIGHT_FUZZ=1 ./01-create_project_files.sh
./build_afternightengine.sh release --with-tests
./build_active_targets.sh release --tests
python build/scripts/tools/run_fuzz_campaign.py --libfuzzer --corpus <scratch>/corpus
```

The generation builds every library and suite with `-fsanitize=fuzzer-no-link,address,undefined`, the
fuzz targets with `-fsanitize=fuzzer,address,undefined`, and records `fuzzer+fuzzer` beside the
binaries; it refuses to be asked for beside `AFTERNIGHT_SANITIZE`. The runner refuses to start while a
library of this product beside the targets carries no coverage counter or a target is not linked with
libFuzzer, and fails a target that crashed, that registered no module but its own, or that libFuzzer
reports blind. Each target's output goes to `libfuzzer.log` beside its artifacts rather than into the
runner, which a later target would otherwise inherit the size of. Record each target's modules,
executions and new units, and any `crash-` or `timeout-` artifact; a finding is a defect of this
engine or product unless the stack says otherwise, and is repaired rather than suppressed.

Regenerate the ordinary tree afterwards, and rebuild: an instrumented binary is left in `bin/` by
any of these configurations, and the campaign's own instrumentation check is what tells the two apart. Both
generations write the same `bin/`, so make keeps a binary newer than the objects it would link: delete
the binaries a generation writes before building it over the other's, and look below `bin/` too.

## The private RAW dependencies

The private RAW dependencies - the RawSpeed build and the private LibRaw beside it - are a subtree of their
own. Follow the complete numbered platform order after a change to them; Windows deploys dependencies
before the main build and Linux deploys after it. The commands, the fixture layout, the native-only
rollback, the cache proof, the timing and the sanitizer boundary recipe are maintained together in
[`build/scripts/tools/rawspeed/README.md`](../../build/scripts/tools/rawspeed/README.md). The budget
checker measures the RAW units with the private RAW headers and their enabled definition, never the
feature-disabled branch. Windows evidence does not qualify Linux Automatic: Linux Automatic keeps Sony ARW2
native, and `qualify` asserts each input's Automatic route per host. RAW qualification uses its isolated
native loader probe and does not inherit a Python interpreter's already-loaded dependency DLLs.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Source of truth

[AGENTS.md](AGENTS.md) is the canonical, tool-agnostic contributor guide (build order, platform shells, coding standards,
theming rules, test-isolation policy, TDD policy, definition of done, and the rules the engine and the product keep).
Read it before non-trivial work; when this file and AGENTS.md disagree, AGENTS.md wins. This file is the quick
operational summary and the map.

Before changing an area, read its section of
[docs/dev/guides/PRODUCT_ARCHITECTURE.md](docs/dev/guides/PRODUCT_ARCHITECTURE.md) (the product) or the
engine guides in [docs/dev/guides/](docs/dev/guides/README.md). A design's reasons are written in the guide for its
area; there are no decision records. [docs/dev/README.md](docs/dev/README.md) indexes the developer documents.

This repository is public and its `main` is the development line; work is tracked as GitHub issues. Validation is
local ([docs/dev/LOCAL_VALIDATION.md](docs/dev/LOCAL_VALIDATION.md)): run what this Windows host can run, name
what is owed to the Linux host, and report what ran with its counts. A document that compares AfterNight with
another product, measures it against one or plans to exceed one is never written here - it belongs to the
maintainer's private notes, kept outside this repository; attribution of derived code stays here (AGENTS.md,
"Workflow").

## Build system

Premake5 generates native projects — **there is no CMake**. `tools/` holds the Premake binary.

- `build/premake/premake5.lua` — entry point; loads the other definitions
- `build/premake/afternight-common.lua` — `QT_VERSION` (currently 6.10.3) and shared paths; the single
  place to change the Qt version
- `build/premake/afternight-pm5.lua` — the main projects (the OpenCL proof backend only when
  `AFTERNIGHT_OPENCL=1` asks for it)
- `build/premake/afternight-test-pm5.lua` — the `Test_*` Google Test projects
- `build/premake/afternight-targets.lua` — reads the target registry `build/scripts/tools/consumer_targets.txt`;
  a **new project needs a record there** or generation stops, and an opt-in target is generated only when asked for
- Generated output: `build/vs2026-msvc/AfterNight.slnx`, `build/vs2026-msvc/tests/AfterNightTest.slnx`,
  `build/make-linux/`
- Sources are globbed (`src/AfterNightEngine/**`), so a **new file only needs `01-create_project_files.*`
  re-run** - except in an isolated test executable, which compiles only its list in
  `build/scripts/tools/isolated_sources.txt`, so a new file there needs its entry too.

The step order is mandatory on every platform: generate projects → install deps → build 3rdparty →
build main projects → deploy runtime deps. On MSVC the numbering is deliberately
`04*-deploy*` before `05*-build_afternight*`.

## Commands

### Windows MSVC (this repo's primary dev platform)

The `.bat` scripts self-bootstrap the VS environment through `_setup_vsdev_env.bat` (vswhere), so they
work from a plain shell; a VS 2026 Developer Command Prompt is still the documented environment.

```cmd
cd build\scripts\msvc
01-create_project_files.bat          REM regenerate after adding/removing source files
02-install_msvc_deps.bat             REM vcpkg from source: 30-60 min, do not interrupt
03a-build_3rdparty_libs_release.bat
04a-deploy_deps_release.bat
05a-build_afternight_release.bat     REM the usual iterate-on-C++ step
09a-build_afternight_tests_release.bat
run_tests.bat [release|debug]
```

`b`-suffixed scripts (`03b`, `04b`, `05b`, `09b`) are the debug equivalents; debug binaries carry a
`_d` suffix. `build_all_release.bat` / `build_all_debug.bat` run the whole chain, and
`build_active_targets.bat release --tests` builds the whole active set, suites included. The runners set
`AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST=1`, so a case that starts the full extension host fails rather than skips
when the host is not deployed beside the suites.

Build one project or one test without the full solution:

```cmd
msbuild build\vs2026-msvc\AfterNight.slnx /t:AfterNightPlatform /p:Configuration=Release;Platform=x64
msbuild build\vs2026-msvc\tests\AfterNightTest.slnx /t:Test_ControlsServices /p:Configuration=Release;Platform=x64
```

Run one `msbuild` per project: a `/t:` list has been seen to build only its first target and leave the
others' old binaries in place, so read the log for the recompiled sources before trusting a suite.

The engine has a shorter loop of its own — it builds the engine and its direct suites, and nothing else in the
workspace:

```cmd
build\scripts\msvc\build_afternightengine.bat release --with-tests
bin\msvc\release\Test_EngineCore.exe
bin\msvc\release\Test_EngineRuntime.exe
bin\msvc\release\Test_EngineImage.exe
bin\msvc\release\Test_EngineInterop.exe
bin\msvc\release\Test_EngineKernels.exe
bin\msvc\release\Test_EngineFixtureBridge.exe
bin\msvc\release\Test_EnginePublicHeaders.exe
```

`build/scripts/linux/build_afternightengine.sh` is the Linux equivalent. An engine-only change
does not need a full-product build: the isolation invariants keep the product graph unaffected and
`check_build_graph.py` fails the change when they stop holding. Debug is a build/ABI/layout tool here, not a
per-change one.

`01-create_project_files` removes the staged outputs of a target the registry records as retired
(`active_targets.py invalidate`).

### Linux (Clang)

```bash
cd build/scripts/linux
./01-create_project_files.sh
./02-install_linux_deps.sh
./03a-build_3rdparty_libs_release.sh
./04a-build_afternight_release.sh
./05a-deploy_all_deps_release.sh
./08a-build_afternight_tests_release.sh
./run_tests.sh
```

### Running

Binaries land in `bin/<toolchain>/<config>/` where toolchain is `msvc` or `clang`
(Linux output is `bin/clang/`, **not** `bin/gcc/`).

- `bin\msvc\release\AfterNight.exe`, or `./bin/clang/release/run_AfterNight.sh` on Linux (sets lib paths)
- `bin/<toolchain>/<config>/AfterNightCLI[.exe] [options] <input> <output>` - the command-line tool;
  `--help` prints its options and exit codes

### Running a single test

```cmd
bin\msvc\release\Test_ImageIntegration.exe --gtest_filter=*Manifest*
bin\msvc\release\Test_EngineImage.exe --gtest_list_tests
```

Set `AFTERNIGHT_TEST_STORAGE_ROOT` to a throwaway directory before running a test executable by hand —
`run_tests.bat`/`run_tests.sh` create a unique one per executable, and without it Qt-based suites can
write into the production `%LOCALAPPDATA%\AfterNight\` tree, which the test-isolation policy forbids.

### Checks

The common check list is in [docs/dev/LOCAL_VALIDATION.md](docs/dev/LOCAL_VALIDATION.md); run the ones a
change could affect, and the `*_test.py` paired with every changed script under `build/scripts/tools/`.
`check_build_graph.py` and `consumer_targets.py check` read the generated projects and refuse to answer when none
is newer than the Premake definitions and the registry - regenerate first.

### Build integrity (MSVC)

An interrupted MSVC build can leave an object compiled against an older revision of a header while
the rest of the binary uses the current one — two layouts of one struct in one binary, no warning.
The `05*`/`09*` build scripts run this automatically; run it by hand after killing a build:

```cmd
build\scripts\msvc\check_msbuild_dependency_tracking.bat --fix
```

Symptom to recognise: a crash or wrong value that appears when you grow a struct and vanishes when
you revert it. ASan will not find it — instrumenting forces the rebuild that repairs it. See
`docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md`.

### Formatting (mandatory before handing off any C/C++ change)

```cmd
build\scripts\msvc\check_clang_format.bat origin/main
build\scripts\msvc\format_changed.bat origin/main
```

```bash
build/scripts/check_clang_format.sh origin/main
build/scripts/format_changed.sh origin/main
```

Linux/CI checks and formats complete changed C/C++ files under `src/`; the MSVC helper checks and
formats changed lines while preserving CRLF. Use the repository helpers so external-derived engine
sources remain excluded and formatting scope stays tied to the current change.

Linux CI is pinned to `clang-format-22` (installed by `build/scripts/linux/install_clang_format.sh`
from apt.llvm.org), matching the `clang-format.exe` that Visual Studio 2026 bundles, so the MSVC and
Linux helpers agree. Majors are **not** interchangeable — clang-format 18 and 22 break lines
differently, so a file formatted by one can fail the other's check. Version 22 is not required for
local validation: use the available repository wrapper and report the formatter version. A pass with
another version is a valid local result, but does not claim exact Linux/CI version parity; CI is
authoritative if versions disagree. Set `CLANG_FORMAT_BIN` when an explicit local formatter path is
needed.

## Architecture

### Module layering (link order is the dependency order)

```
AfterNightPlatform (shared lib, an::platform)        <- logger, logging config, crash handler; no engine, no Qt
AfterNightEngine (shared lib, an)                    <- the image engine; no Qt
AfterNightConsumerRuntime (shared lib, an::consumer) <- Engine + Platform: session, operation scope, image asset; no Qt or Python
AfterNightExtensionProtocol (shared lib)             <- engine-free, Apache-2.0 boundary
AfterNightControls (shared lib, an::ui)              <- ConsumerRuntime + Engine + Platform + Protocol + Qt + KDDockWidgets
AfterNightProcesses (shared lib, an::ui::processes)  <- Controls + ConsumerRuntime + Engine + GmicAdapter + Platform + Protocol
AfterNightApp (exe)                                  <- Processes + Controls + ConsumerRuntime + Engine + Platform + Protocol
```

`AfterNightCLI` links ConsumerRuntime, the engine and Platform. `AfterNightExtensionHost` links the processes and
Controls libraries, ConsumerRuntime, the engine and the interpreter (GPL, full engine access) and loads
`AfterNightExtensionRuntimeBackend` by name, borrowing the session it owns; `AfterNightExtensionHostLite` links
**only** the Protocol library and must never pull in Engine/Controls/Processes/KDDockWidgets/Platform headers — that
separation is the licensing boundary described in `docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md`, selected per
extension by `sdk_backend` in `extension.json` (`runtime` → full host, `protocol`/`rpc` → lite host).

### AfterNightEngine (`src/AfterNightEngine/`)

The engine every product target links, in `namespace an`, PascalCase. Public headers live under
`include/AfterNightEngine/<Area>/` and are included as `<AfterNightEngine/Area/Name.h>` (areas `Core`, `Image`,
`Interop`, `Runtime`, `Algorithms`, `IO`); implementation headers under `src/<layer>/` as `"layer/Name.h"`. Expected
failures are returned as `Status`/`Result<T>`; the engine owns no globals (a caller owns an `EngineContext`); every
non-trivial algorithm takes an `ExecutionContext`; parallel work goes only through `Runtime/TaskScheduler.h`; memory
comes from the context; an image's meaning is stated in its `ImageDescriptor` and metadata, never inferred from a C++
type. `src/AfterNightEnginePythonAdapter/` and `src/AfterNightEngineGmicAdapter/` are separate optional libraries
(`AFTERNIGHT_PYTHON=0`, `AFTERNIGHT_GMIC=0` build them absent). Its version started at 1.0.0 and its ABI
generation at 1 (`Core/Version.h`). The rules are AGENTS.md's "The engine"; the guides
start at `docs/dev/PUBLIC_API_CONTRACT.md` and `docs/dev/guides/ARCHITECTURE.md`; the API reference is
generated by `python build/scripts/tools/check_public_documentation.py --output-dir <dir>`.

### AfterNightPlatform and AfterNightConsumerRuntime

`src/AfterNightPlatform/` (`an::platform`, `<AfterNightPlatform/Name.h>`) holds the logger and its `LOG_*` macros, the
logging configuration, the Crashpad handler, `Sha256.h` and the text, path and memory helpers; it links no engine and
no Qt. `src/AfterNightConsumerRuntime/` (`an::consumer`, `<AfterNightConsumerRuntime/Name.h>`) is what a consumer of the
engine owns: `EngineSession` (the one engine context, admission of every operation), `OperationScope`, `ImageAsset`
under read and write leases with an `AssetRevision`, and `ConsumerMemory`; it is Qt-free and Python-free.

### AfterNightCLI (`src/AfterNightCLI/`)

One image read, stretched by the display presentation and written with an explicit `OutputIntent`, every step an
admitted `OperationScope`; `CommandLine.h`'s `kOptions` declares every option. `Test_AfterNightCLI` runs its units and
its binary.

### AfterNightControls (`src/AfterNightControls/`)

Reusable Qt layer, `namespace an::ui`:

- `session/` — the engine bridge: `IEngineSessionProvider`, `ImageDocument`, `RequestTracker`/`CommitTicket`,
  `ExecutionScope`, `ImageReads`
- `theme/` — `AnDesignTokens` singleton, `AnProxyStyle` (all standard Qt widget painting),
  `KddwProxyStyle`, `AnimationUtils`
- `controls/` — `ImageViewControl`, `ImageProcessControl` → `ImageRTPreviewControl` →
  `TypedProcessControl<ParamsT>`, the history, sequences, drawing tools, dockable controls, display modules
  (astrometry overlays, grids)
- `app/` — application services and coordinators that the app and processes reach through the
  `controls/IAppContext.h` interfaces (`IDialogService`, `IViewFactory`, `IIconProvider`,
  `IGlobalSettingsProvider`, `ITempFileService`, `IEngineSessionProvider`, `IRawOpenPolicyService`, …), plus
  `ProcessRegistry`, `ExtensionManager`, `ExtensionHostManager`, `PlateSolveService`, `ThemeManager`,
  `ApplicationStorage.h`, and the file workflows (`FileWorkflowRunner`)
- `components/`, `widgets/`, `dialogs/` — reusable UI (`AnCardWidget`, `AnInfoBanner`, …)

### AfterNightProcesses (`src/AfterNightProcesses/`)

Every user-facing process. The repeating pattern per process is three files:
`XProcess.h/.cpp` (control logic, derives from `an::ui::TypedProcessControl<XParams>`) and
`XProcessUiBuilder.h` (an `an::ui::processes::XProcessUiElements` struct with `setupUi()` — the
hand-maintained replacement for `.ui` files). `TypedProcessControl` is required for any process with
execution parameters; a body - `executeTypedProcessImpl(source, destination, run, params, scope)` - reads a
`ProcessSource`, writes a `ProcessDestination` and reads no widget. `app/ProcessRegistryBuiltins.cpp` registers
descriptors into `ProcessRegistry`, and `app/ProcessLauncher` opens them. Each process family has its suite,
`Test_Processes<Family>`.

`processes/ImageIntegration/` is the large multi-stage stacking workflow: `model/` (frames, grouping, calibration
matching, `IntegrationError`), `workflow/` (manifest build/diff/hash, `ExecutionPlanner`, `WorkflowExecutor`,
progress, artifacts, cancellation) with `workflow/stages/` (Bias, Dark, Flat, LightsCalibration, LightsRegistration,
LightsAlignment, LightsNormalization, LightsStacking, LightsDrizzle), `persistence/` (SQLite project state, autosave,
presets), `reporting/` (the report model, `buildReportModel`, and the document written from it; read
`docs/dev/IMAGE_INTEGRATION_REPORT.md` first), and `tabs/`, `widgets/`, `controls/`, `dialogs/` for UI. `lite/` and
`ImageIntegrationLiteProcess.*` are the simplified front end only — shared behavior belongs in the full
model/workflow helpers and should be reused by Lite. Every generated image goes through `publishWorkflowImage`
(`workflow/WorkflowArtifactStamp.h`), and reuse is decided from the identity stamped in its header, never by re-reading
a payload: read `docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md` before touching artifact validation or output
writing.

### AfterNightApp (`src/AfterNightApp/`)

Thin shell: `AfterNightApp` (QMainWindow) composes coordinators that live in
`AfterNightControls/app/` (`UiBootstrapCoordinator`, `ViewCreationCoordinator`,
`WindowLayoutCoordinator`, `DialogOrchestrationCoordinator`, `PersistenceCoordinator`,
`AppLifecycleCoordinator`, …), and `AfterNightAppContext` implements the `IAppContext` service
interfaces. Put new logic in a coordinator or service, not in `AfterNightApp.cpp`. `ApplicationSession` is the engine
session `main.cpp` owns, and `shutDownApplication` the order the application shuts down in.

### The extension hosts and the runtime backend

`src/AfterNightExtensionRuntimeBackend/` is the module the full host loads by name for a `runtime` extension: it owns
the session the host borrows through a capsule. `src/AfterNightExtensionHost/` opens no session of its own and is
heartbeat-supervised by the application; `src/AfterNightExtensionHostLite/` links only `src/AfterNightExtensionProtocol/`.

### Tests (`src/AfterNightTests/`)

One directory and one `Test_*` executable per area: the engine's direct suites (`Test_EngineCore`, `Test_EngineImage`,
`Test_EngineRuntime`, …), the consumer suites (`Test_Platform`, `Test_ConsumerRuntime`, `Test_AfterNightCLI`, the
`Test_Controls*` suites, the `Test_Processes*` families, `Test_ImageIntegration`, `Test_ProcessFramework`,
`Test_AfterNightApp`, `Test_ExtensionHostNative`, `Test_ExtensionRuntimeBackend`, `Test_ThemeSystem`,
`Test_Persistence`, …), the `Fuzz_*` targets and the fixture executables. Test inputs come from the fixture layers
under `src/AfterNightTests/fixtures/` (`common/`, `harness/`, `engine/`, held by `Test_FixtureNeutral` and
`Test_FixtureEngine`); the expectations under `fixtures/common/expectations/` are recorded reference expectations
that cannot be captured again, and a checked-in record is never regenerated ("The fixture layers" in
[docs/dev/guides/TESTING.md](docs/dev/guides/TESTING.md)).

## Conventions that bite

- **`(std::max)(a, b)` / `(std::min)(a, b)`** with parentheses around the name, everywhere under
  `src/` — otherwise the Windows SDK `min`/`max` macros expand the call. `SourceGuards.NoRawStdMinMaxCallsInSrc`
  in `Test_ImageIntegration` reads every C/C++ file under `src/`, so run it for a change anywhere there.
- **Verdigris, not moc**: use `W_OBJECT`, `W_SIGNAL`, `W_SLOT` instead of `Q_OBJECT`.
- **State what new samples start as.** An `ImageStorage` is created with a stated `StorageInitialization`, never an
  assumed one: ask for zeroed storage when a buffer is accumulated into or only partly covered. Uninitialised pixels
  once produced a comet stack wrong in ~50% of runs with a bit-identical wrong value, reproducible only with the heap
  deliberately poisoned.
- **A FITS header is complete before its payload.** The engine's FITS writer
  (`src/AfterNightEngine/src/io/fits/FitsWrite.cpp`) hands OpenFITS the whole header - the scaling cards among the
  structural ones the library writes from the image's description - before the first sample, so the payload is
  written once, in order, and never moved; a card added after the data would move the entire payload (258 ms per
  178 MB frame, measured when CFITSIO wrote the files). No card is ever written late, and a test that truncates a
  FITS file to its header must locate the `END` card instead of assuming one 2880-byte block.
- **Work runs on the thread pool and lands on the GUI thread.** The GUI thread never waits for a worker, an admission
  or the history; a result is shown only when its `CommitTicket` still commits; a run's other images are leased before
  it is admitted. See AGENTS.md, "Threads, requests and publication".
- **Record an edit before the view shows it, and never wait for the history on the GUI thread.** A history step is
  a copy taken under a lease when it is registered, admitted against the session's pending-snapshot allowance and
  written as an image snapshot; a restore is a new asset. An edit of a view goes through `ImageViewControl::editImage`,
  whose worker runs the history's `editPreparation` (what it replaces written, its copy admitted) before the GUI thread
  records it with `registerEditBeforePublishing` and shows it only when that returns true - Apply Stretch
  (`controls/AutostretchApply.h`) is the model; `registerShownState` is for a state a process already showed. Never
  write the shown image in place: other threads read it.
- **No QSS.** `setStyleSheet()` is forbidden project-wide. No hardcoded colors/spacing/radii/fonts —
  resolve through `an::ui::tokens()`, and call it *inside* `paintEvent()` (never cache it in the
  constructor) so theme switches apply. Read `docs/dev/THEME_SYSTEM.md` before touching UI.
- **Naming**: PascalCase is the convention for all new code — PascalCase types and enumerators,
  camelCase methods/locals, `m_` members, `k`-prefixed constants, PascalCase files. Add no new snake_case public API
  anywhere. A library with its own `.clang-tidy` (the engine, its adapters, the platform and consumer runtime
  libraries, the CLI, Controls' `session/`) enforces the table. New framework/control classes go in `namespace an::ui`,
  new process UI builders in `namespace an::ui::processes`. The engine lives in `namespace an`. Do not
  retro-namespace existing classes.
- **No planning-artifact references in code.** No reference to a unit of *work* under `src/` or in
  the Premake files — comments and test failure messages included. That covers work-unit IDs of any
  shape (`Task 1.3`, `Phase 2`, `Step 3 of the redesign`, `Milestone 2`, `Iteration 5`, `Sprint 14`,
  `Wave 1`, `Epic`), document coordinates (`section 10.7`, `§8.13`,
  `Appendix C`, plan filenames), registry IDs (`C06`, `decision 3.5.18`, `ADR-12`, `REQ-7`), and
  tracker handles (issue/PR numbers, ticket keys, branch names). **The ban is on the reference, not
  the vocabulary**: `LightsStackingStage`, a "phase" of a pipeline, a loop "iteration", and
  `// Step 1:` narrating a function's own algorithm are all fine; "removed as part of Task 4.2" is
  not. Test: if archiving the plan would make the sentence meaningless, it does not belong in code.
  State the reason, not the citation, and for forward references say what is missing rather than
  which work unit will add it. Tooling that parses a plan document as input (`build/scripts/tools/`) is exempt.
  See AGENTS.md.
- **Logging**: the `LOG_*` macros of `<AfterNightPlatform/Logging.h>` (`LOG_ERROR`/`LOG_WARNING`/`LOG_INFO`/
  `LOG_DEBUG`/`LOG_TRACE`); never print to stdout/stderr. The logger's controls are `an::platform::`
  functions (`isLoggingInitialized`, `initializeLogging`, …).
- **Parallelism**: in the engine, through `Runtime/TaskScheduler.h` under the operation's grant
  (`tbb::global_control` is refused); in the product, heavy work runs as engine operations inside an admitted
  `OperationScope`. Never touch UI from workers.
- **Extension host**: `AfterNightExtensionHost` is heartbeat-supervised by the main app — never block
  its event loop (no `QDialog::exec()`, no static `QFileDialog::get*()`, no synchronous waits).
- **Line endings**: every AfterNight-owned text file uses CRLF, even when created or edited on Linux.
  Linux-exclusive files may use LF; files under `build/scripts/linux/**` and shell scripts executed on
  Linux must use LF. Preserve upstream endings under `3rdparty/**` and in Git submodules. Mixed line
  endings are forbidden. See the mandatory policy in `AGENTS.md` for the current Linux script paths.
- **Licence headers**: every `.h`, `.hpp`, `.cpp`, `.c`, `.inl` and `.py` AfterNight owns opens with
  `// SPDX-License-Identifier: GPL-3.0-or-later` and `// SPDX-FileCopyrightText: <year> <author>` (`#` in Python) -
  `Apache-2.0` on the protocol boundary (`src/AfterNightExtensionProtocol/`, `src/AfterNightExtensionHostLite/` and
  the protocol half of `python/modules/afternight/`), and after the upstream notice in a derived file.
  `check_license_headers.py` holds it; `--apply` inserts a missing header.
- **Generic controls**: app-side controls built to serve an extension's UI must stay generic — no
  class, file, or object names derived from a specific extension or process.
- **Tests must never write into the production storage tree** (`%LOCALAPPDATA%\AfterNight\` and the
  Linux equivalents). Own a `QTemporaryDir` / temp root, set `AFTERNIGHT_TEST_STORAGE_ROOT` before
  constructing `QApplication`, resolve paths via `app/ApplicationStorage.h`, and clean up on both
  success and failure.
- **Interface settings are a file, never the registry.** Product code reads and writes them through
  `an::ui::applicationSettings()` (`app/ApplicationStorage.h`), an INI file under the application's local data; a
  default `QSettings` or one named by organization and application is the native store - the registry on Windows -
  which no test storage root redirects.
- **A new static, namespace-scope object or singleton in product code** needs an entry in
  `build/scripts/tools/consumer_state.txt` (`check_consumer_state.py`); in the engine, one in
  `check_process_state.py`'s table.
- **Credit every derived work in the file itself.** Code copied, translated or ported from another project carries in
  its module comment, in the header *and* the implementation: the upstream work and version with a URL, the authors,
  the upstream copyright line verbatim, the licence and that the file is a derived work distributed under it, and
  what is upstream's versus AfterNight's. The notice travels with the logic when the code moves or is rewritten. Check
  compatibility first: no GPL-2.0-only, no source without a licence, nothing GPL behind the Apache-2.0 protocol
  boundary. Beside the file: a `docs/dev/*_PROVENANCE.md` note; for engine code a `ported` record in
  `build/scripts/tools/dependencies.txt` whose marker is the copyright line, which `check_dependency_provenance.py`
  enforces; and the About dialog's licence credit, held by `Test_ControlsDialogs`. Licence and copyright text is not a
  planning reference. See AGENTS.md, "Credit for derived code".
- **TDD is mandatory** for new features (failing test first), and bug fixes need a regression test.

## Documentation

[docs/dev/README.md](docs/dev/README.md) indexes the developer notes in `docs/dev/` (theme system, crash
symbolication, extension host modes, ImageIntegration artifact validation, SPCC and catalog maintenance, provenance
notes, user-guide authoring). The engine's guides and the product guide are in
`docs/dev/guides/`, which also hold each area's design reasons. End-user docs live in
`docs/user-guide/` (MkDocs with the guide's own theme) and build with
`build\scripts\msvc\build_user_guide_docs.bat` or the platform equivalent.

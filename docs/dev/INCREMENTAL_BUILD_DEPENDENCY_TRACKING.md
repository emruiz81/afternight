# MSVC incremental builds can silently link stale objects

*Written 2026-08-18.*

## The short version

An MSVC incremental build can leave an object file compiled against an **older revision of a
header** while every other object in the same binary uses the current one. Nothing warns. If that
header declares a struct, the binary holds two different layouts of it, and code reads fields at
the wrong offsets: wrong values, or an access violation with no usable stack.

This is not theoretical. It is what produced the `FrameQualityMetrics` "crash" that stood as an
open risk for three days and led to a real architectural workaround built around a bug that does
not exist.

Two guards now exist:

- `build/scripts/msvc/check_msbuild_dependency_tracking.bat` detects the condition and, with
  `--fix`, deletes the affected objects. The MSVC build scripts - `05a`, `05b`, `09a`, `09b`,
  `build_active_targets.bat` and `build_afternightengine.bat` - call it with `--fix` before every
  build, so it self-heals.
- `FrameQuality.h` pins its struct layout in a `#pragma detect_mismatch` tag, so a mixed-layout
  link fails with `LNK2038` rather than misbehaving at runtime. The engine guards its own ABI
  differently: it states its generation in `AfterNightEngine/Core/Version.h`
  (`kEngineAbiGeneration`) and reports a mismatched caller through `engineCompatibility()` and
  `engineMatchesHeaders()`.

## What actually happened

`docs/dev/FRAME_WEIGHTING_INPUTS.md` and `FrameQuality.h` carried a rule: never add fields to
`FrameQualityMetrics`. Adding even 24 bytes of padding that nothing reads turned five (in fact
seven) `RegistrationTabTest` frame-exclusion tests into `0xC0000005`. Field order was irrelevant;
only the size mattered. The same padding on `RegistrationMetricRecord` was harmless. It looked
exactly like a latent memory bug sensitive to allocation layout.

It was reproduced, then falsified:

| Experiment | Result |
| --- | --- |
| Padding + incremental build | 6 access violations, 1 wrong value |
| Padding + ASan build (fresh obj dir) | all tests pass, no ASan report |
| Padding + plain build into a **fresh obj dir** | all tests pass |

The third row is the answer. Nothing about the padding is wrong; the incremental build was.

At the time `Test_ImageIntegration` compiled `RegistrationTab.cpp` itself (it now links
`AfterNightProcesses`), and that source includes `processes/ImageIntegration/model/FrameQuality.h`
on line 9. After editing the header, MSBuild
recompiled 169 objects — and not `RegistrationTab.obj`. The test executable therefore contained
`test_registration_tab.obj` and `IntegrationDataModel.obj` built with a 104-byte struct and
`RegistrationTab.obj` built with the 80-byte one. Every metrics field the tab read after `stars`
came from the wrong offset.

## Why MSBuild missed it

MSBuild does not parse includes. It replays the read log that `FileTracker` recorded when the
object was last compiled, in `<IntDir>/<name>.tlog/CL.read.*.tlog`, and recompiles when any file
in that list is newer than the object.

`RegistrationTab.cpp`'s entry listed 618 files. A clean rebuild of the same source records 1118 —
and the 618 are an **exact ordered prefix** of the 1118. The log had been cut off mid-write while
the object survived, so from then on MSBuild compared the object against a dependency set that
stopped before any of the project's own headers. `stdafx.hpp` was the only file under `src/` it
still knew about.

Sixteen objects in that one intermediate directory were in this state, in two clusters seven
seconds wide (2026-08-15 21:09 and 23:14) — the set that was in flight when something cut a build
short, on a 32-thread machine building with `/m` and `/MP`. They stayed that way for three days.
Because the untracked set includes the engine headers, every one of those objects had also missed the
changes to the stacking, registration, local normalization and drizzle headers of the engine of the
time — the entire no-data-contract work. `Test_ImageIntegration` was reporting green from a
binary that mixed three days of header revisions.

## A second route to the same skew

The tracking log is not the only way in. While verifying the layout tag, restoring
`FrameQuality.h` from a copy set its modification time *backwards* - older than objects already
built from a newer revision of it. MSBuild's dependency list was complete and correct; the
timestamp comparison simply said "up to date" and nothing recompiled. The binary ended up mixing an
80-byte and a 104-byte view of the struct exactly as before.

Anything that rewrites a source with an older timestamp does this: restoring from a backup,
`Copy-Item` or `robocopy` preserving the source's time, some archive extractions, checking out an
older revision of one file. `check_msbuild_dependency_tracking` cannot see it - every recorded
dependency is present - which is why the layout tag is a second layer rather than a second belt on
the same trousers. It caught this one at the link, unprompted:

```text
RegistrationTab.obj : error LNK2038: mismatch detected for 'FrameQualityMetricsLayout':
    value '80/8' doesn't match value '104/8' in ImageIntegrationProcess.obj
```

If you suspect it, `touch` the header - or rebuild from clean - rather than trusting the timestamps.

## The check

```cmd
build\scripts\msvc\check_msbuild_dependency_tracking.bat            REM report only
build\scripts\msvc\check_msbuild_dependency_tracking.bat --fix      REM delete the stale objects
python build\scripts\msvc\check_msbuild_dependency_tracking_test.py REM its own test suite
```

It walks every `.tlog` directory under `obj/`. For each source it follows the in-repository headers
reachable through includes written outside any preprocessor conditional and requires each one to
appear in that source's recorded read set — the compiler cannot have skipped an unconditional
include, so a missing one is proof the log is short. `--fix` deletes the object, which is all it
takes: MSBuild recompiles anything whose output is gone, and writes a complete log that time.

Deliberate silences, so a finding is always real rather than merely suspicious:

- Includes inside `#if`/`#ifdef` are ignored — the compiler may genuinely not have read them.
- An include that resolves outside `src/` and `3rdparty/src/` is ignored; `<kddockwidgets/core/Group.h>`
  must not be matched against `3rdparty/src/cfitsio/group.h` on the basename alone.
- Where one spelling can name several files in the tree — the test suites shadow headers with
  mocks — the read log itself decides which was opened, and the walk follows that one.
- Objects compiled with `/Yu` inherit the reads recorded for the source that builds the PCH,
  because a change there rebuilds the PCH and therefore every consumer.
- A source with no object file is not a finding: nothing stale can reach the link.

The cost of that caution is recall — a truncation that dropped only system headers is invisible.
The check found 17 stale objects in this checkout on first run, including one in
`Test_Persistence` that had gone unnoticed entirely, and zero findings against a known-good
intermediate directory.

## What this means day to day

- **After interrupting a build (Ctrl-C, a killed IDE, a reboot mid-compile), run the check.** The
  build scripts do it for you; a bare `msbuild` invocation does not.
- **After restoring a source from anywhere other than an editor, check its timestamp.** A file
  written back with an older modification time is invisible to the check and to MSBuild alike.
- **A crash that appears when you change a struct, and disappears when you revert it, is a build
  artefact until proven otherwise.** Rebuild into a clean intermediate directory before spending a
  day in a debugger — and before designing around it.
- **Do not use ASan to rule this class out.** Instrumenting requires a full rebuild, which
  silently repairs the very condition you are chasing. Here ASan came back clean and, read
  naively, would have said "no memory bug, cause unknown".
- The Linux build compiles with `-MD -MP`, so the compiler emits each object's
  dependency file itself, next to the object, rather than a separate tracker reconstructing it
  after the fact. They are not exposed to this failure mode; it is MSVC-only.

## Anti-regression

- Existing: the `RegistrationTabTest` frame-exclusion tests still double as the sensitive
  detector for a mixed-layout `FrameQualityMetrics`.
- `FrameQualityMetricsLayout.*` in `unit/test_frame_quality.cpp` pins the size, alignment and key
  field offsets that the linker tag is built from, so changing the struct without updating the tag
  fails both compilation and the suite.
- `check_msbuild_dependency_tracking_test.py` covers the detector itself: log parsing, the
  truncated-log finding, `--fix`, transitive headers, and each of the silences listed above.

Both guards were verified end to end against a real build rather than reasoned about: growing the
struct without updating the tag stops at `error C2338` with the message telling you which two
numbers to change, and an object left behind at the old layout stops at the `LNK2038` quoted above
instead of reaching the test runner.

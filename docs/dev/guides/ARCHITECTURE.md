# Architecture

How AfterNightEngine is put together: the libraries, the layers inside the engine, what owns the
resources an operation uses, and what happens between a caller asking for an operation and getting
its answer. Every section ends at the header that states the contract in full.

## What it is, and what it is beside

AfterNightEngine is the image engine the product runs on: every product target links it except the platform
library, the extension protocol library and the Lite extension host, which are held to reaching no engine.

Four libraries make up the engine's side of the workspace:

| Library | Role | Links |
|---|---|---|
| `AfterNightEngine` | The engine: images, runtime, every algorithm, codecs and sequences | oneTBB, the libraries its interop and codec layers wrap |
| `AfterNightEnginePythonAdapter` | Optional: runs a caller's Python on engine images | the engine, and Python when built with it |
| `AfterNightEngineGmicAdapter` | Optional: runs G'MIC pipelines on engine images | the engine, and G'MIC when built with it |
| The opt-in device backend | Non-shipping proof that the device seam can be implemented on a real GPU | the engine and a GPU SDK, only in a tree generated with it |

An optional adapter is a separate binary so that a consumer that never uses Python or G'MIC never
acquires either. Built without its library (`AFTERNIGHT_PYTHON=0`, `AFTERNIGHT_GMIC=0`), an adapter
still builds, reports itself absent and refuses every call by name. The engine links neither adapter.
See [DEPENDENCIES.md](DEPENDENCIES.md) and [BACKENDS.md](BACKENDS.md).

## Layers

The published surface is six areas under `src/AfterNightEngine/include/AfterNightEngine/`,
each included as `<AfterNightEngine/<Area>/<Name>.h>`:

| Area | Holds |
|---|---|
| `Core` | Version and ABI checks, `Status` and `Result`, `ErrorCode`, checked sizes and layouts, limits, the exception boundary, the dependency report |
| `Runtime` | `EngineContext`, `ExecutionContext`, the task scheduler, memory resources and scratch arenas, the three report sinks, CPU detection and backend dispatch, device residency and events |
| `Image` | The image model: descriptors, sample types, storage, views, images, metadata, validity, conversion |
| `Interop` | The external-buffer description, OpenCV, FFTW and GSL buffer descriptions that never include those libraries, and the persisted star-profile records |
| `IO` | Codecs, the codec registry, frame sources and sinks, sequences, streaming, output transactions |
| `Algorithms` | Every processing operation, one directory per domain, and the kernel contracts in `Algorithms/Kernel` |

The implementation under `src/AfterNightEngine/src/` is layered by what it may depend on, and
included as `"layer/Name.h"`:

| Layer | Holds | External libraries it may include |
|---|---|---|
| `core`, `image` | The foundations behind the `Core` and `Image` areas | none |
| `runtime`, `memory` | Contexts, scheduling, allocation | oneTBB |
| `kernels/reference` | The scalar reference kernels, which define each kernel family's answer | none |
| `backends/cpu` | The compiled per-instruction-set kernels behind the dispatch tables | the vector library, Highway |
| `backends/gpu` | Reserved for a shipping GPU backend; empty today | a GPU SDK - and no other layer may |
| `interop/<library>` | The one place a wrapped library's headers appear | that library alone |
| `algorithms/<domain>` | The operations | none: a wrapped library is reached through `interop` |
| `io/<codec>` | Codecs and sequences | the codec libraries and platform headers; FFmpeg only below `io/sequences/ffmpeg` |

Three rules hold across every layer, and `build/scripts/tools/check_include_layers.py` fails the
change that breaks one: a published header includes no external library and no private header; a
layer includes only the libraries it declares; nothing includes another first-party component, so
the engine stays usable without the product. Two more keep the include graph honest: no header
declares more than 600 lines of code unless it is a private header registered with its reason and
included by one translation unit - the median selection networks are the only one - and no target on
this side compiles through a precompiled header or a forced include (`check_header_architecture.py`,
`check_build_graph.py`). Documentation does not count toward the 600 lines because every public
symbol must carry it, so a limit on physical lines would ration the documentation; and a precompiled
header or a forced include would bring in headers that no include line names, where every include
check reads include lines.

## What owns what

The engine keeps no globals. Thread pools, caches, limits and registries - what a library usually
keeps in statics - are owned by an object the caller constructs.

**`EngineContext`** (`Runtime/EngineContext.h`) owns the thread budget, the memory resource, the
limits and the codec registry. There is no instance accessor and no process-wide default, so two
contexts coexist and share nothing. Its maximum concurrency is fixed at construction, because oneTBB
fixes a task arena's parameters once the arena is initialised; changing it means quiescing the work
in flight and building a replacement with `EngineContext::createReplacement`. Everything taken from
a context holds an `EngineContextLease`,
and a context with a lease outstanding refuses to shut down - which is how a leaked image or
execution is found. `check_process_state.py` holds the whole side of the workspace to this: every
object with process lifetime is in its table, and there is no category for a cache, a registry, a
setting or a logger. A rule with no inventory behind it holds only until someone adds a cache, and a
process-wide cache couples callers that never agreed to share it - the first thing to break when two
contexts run side by side.

**`ExecutionContext`** (`Runtime/ExecutionContext.h`) is one operation's policy and control:
cancellation through a `std::stop_token`, an optional deadline, borrowed progress, diagnostic and
trace sinks, a borrowed scratch arena, a backend preference, and a concurrency request that may only
narrow what the engine context allows. It takes its share of the context's threads for its whole
life - `admittedConcurrency()` is that share, which is not the ceiling `maxConcurrency()` states - so a
workflow that wants two operations in flight narrows `ExecutionOptions::maxConcurrency` for each.
Every non-trivial operation takes one; `checkCancellation()` is the checkpoint it calls.

**Memory** comes from the context. `Runtime/MemoryResource.h` is the context's aligned allocator: a
budget refusal is a returned failure carrying the requested, available and limit bytes, and live,
peak and denied counters are always on. A caller may supply its own resource in
`EngineContextOptions::memoryResource`, and the context's resource then takes its bytes from it while
keeping the budget and the counters. `Runtime/ScratchArena.h` is a bump allocator one execution owns;
`reset()` and `rewind()` return nothing to the allocator and its destructor returns everything.

**Threads** are lent, never capped globally. `Runtime/TaskScheduler.h` gives each operation a
`task_arena` of exactly its admitted width, so a nested parallel loop - including one inside an
adopted algorithm - inherits the width without being told. `parallelFor` and `parallelReduce` run on
exactly the granted workers. `chooseGrainSize` depends on the concurrency and
`deterministicChunkCount` does not, which is why a reduction split by it gives the same answer at any
thread count. Resizing oneTBB for the whole process is refused anywhere in the tree by
`check_forbidden_constructs.py`: a process-wide limit set for one operation would silently constrain
unrelated work, the application's own threads included, and it can be neither scoped nor nested,
where an arena can be both.

## How one operation runs

Operations follow one shape, and the shape is the same for a statistic and for a stack:

1. **Describe the request** as a value: `StackRequest`, `BackgroundSampleRequest`, `PsfFitRequest`.
   A request holds numbers and choices, never memory.
2. **Plan it** where planning is not trivial: `makeStackModel` checks the request and `planStack`
   decides strategy, memory, passes and read costs. A plan is reviewable before anything is
   allocated, and a plan that does not fit the budget is refused before any memory is taken.
3. **Execute it** with an `ExecutionContext`, borrowed inputs and destinations: `executeStack`,
   `computeImageBasicStatistics`, `removeImageAutoBackground`. The execution reports progress,
   diagnostics and traces through its sinks, and checks cancellation at bounded checkpoints.
4. **Record what happened**: an operation that changes an image's meaning returns or accepts the
   `MetadataTransition` that explains it (`stackTransition`), and `Image::transition` is the only door
   to new metadata.

**Failures are returned.** `Core/Status.h`'s `Status` for an operation that returns nothing and
`Core/Result.h`'s `Result<T>` for one that produces a value; both are `[[nodiscard]]`. A caller
branches on `ErrorCode` (`Core/ErrorCode.h`), on `errorCategory`, or on a typed context field, never
on a message. An operation that fails leaves no partial result: a destination is either what the
operation promised or untouched, and a written file is whole or absent with nothing beside it.
Exceptions are for defects; another library's exception is translated at its adapter boundary by
`Core/ExceptionBoundary.h`. [PUBLIC_API_CONTRACT.md](../PUBLIC_API_CONTRACT.md) states the rules.

**Reports share one order.** Progress, diagnostics and traces from one operation carry one
saturating sequence and the `ExecutionContext::operationId()`; a progress report that would move
backwards is dropped and counted, and `operationCounters()` says how many of each were delivered and
discarded. Callbacks are serialized per operation, so a sink shared by two executions must be
thread-safe. An operation reports progress when its work is not one pass over its input - an
iteration, a patch search, a fit - and says nothing when it is, because the caller counts frames. A
composing operation maps a nested one into a share of its own total through the private
`runtime/ProgressRange.h`.

**Which code ran is asked, not assumed.** `Runtime/BackendDispatch.h` resolves an operation's backend
and instruction set and reports what the selected table declares; see [BACKENDS.md](BACKENDS.md).

## The binary interface

The ABI is version-locked in-tree: the engine and everything that links it are built from one
checkout, and nothing promises one commit's binary to another commit's caller. What the version
machinery catches is the failure that arrangement does produce - a translation unit compiled against
one revision of a header linked beside objects compiled against the next. `Core/Version.h` states
the version triple and `kEngineAbiGeneration`; `engineMatchesHeaders()` is inline, so it compares the
caller's constants with the library's, and a mismatch is fatal.

The published surface is frozen as a record: `build/scripts/tools/public_api.txt` holds every
published header's declaration digest, every exported declaration, every public type with its data
members in order, and every enumeration. `check_public_api.py` fails a change to any of them until
the version has moved, and the ABI generation too when an already-compiled object would be wrong
about a layout or signature. Member order is recorded because a layout is what an already-compiled
object relies on and no export table shows it; comments are removed before anything is digested, so
the documentation can be rewritten without touching the record.

## Where the rest is written down

| Topic | Read |
|---|---|
| What an image is, and how its samples and meaning are kept apart | [NUMERIC_VALIDITY_METADATA.md](NUMERIC_VALIDITY_METADATA.md) |
| Instruction sets, kernels, and the device seam | [BACKENDS.md](BACKENDS.md) |
| Which libraries, where, and at which version | [DEPENDENCIES.md](DEPENDENCIES.md) |
| Building, testing and measuring | [BUILD.md](BUILD.md), [TESTING.md](TESTING.md), [BENCHMARKS.md](BENCHMARKS.md) |
| How the product uses the engine | [PRODUCT_ARCHITECTURE.md](PRODUCT_ARCHITECTURE.md) |

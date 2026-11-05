# AfterNightEngine: the public API contract

What a consumer has to know before it writes its first line against this engine: who owns what, how
a failure reaches it, and how it asks what the build and the machine can do.

This is not the API reference - that is generated from the headers, each of which documents its own
declarations in full. This is the part that is true of every header and is
therefore written down nowhere in particular: the four ownership shapes, the one error model, and
the queries a caller makes before it configures anything.

Everything here is reached through `<AfterNightEngine/<Area>/<Name>.h>` and nothing else. A
public header includes no external library, so compiling against this engine acquires no third-party
toolchain; the optional adapters in `Interop/` describe another library's buffers in their own field
types without including it.

---

## 0. The first call

```cpp
#include <AfterNightEngine/Core/Version.h>

if (!an::engineMatchesHeaders())
{
    // The loaded library was built from different public headers. Nothing else in this
    // surface may be trusted: a struct this caller passes may not have the layout the
    // library reads. Stop here.
}
```

This is a version-locked in-tree C++ ABI. The engine and everything that links it are built from one
checkout by one set of scripts, no packager ships a header, and nothing promises that one commit's
binary can serve another commit's caller. What the version machinery is for is the failure that
arrangement really does produce: a translation unit compiled against one revision of a public header,
linked beside objects that used the next one. It is the cheapest interface to write and the fastest at
run time: an all-opaque API or a C boundary would buy a binary compatibility no consumer needs, at the
price of an allocation and an indirection on every value record or a hand-maintained C surface.

`engineMatchesHeaders()` is defined inline, so the constants it compares are the *caller's*.
`engineCompatibility()` distinguishes the two failures and reports the ABI one first, because a
layout disagreement makes even a useful version diagnostic unsafe to act past. Both are fatal.

`Core/Version.h` states what moves each number. The interface generation moves when an existing
public layout or exported signature changes; the minor version moves when the surface gains
something. `build/scripts/tools/check_public_api.py` decides which of those a change was and fails
until the right number has moved.

---

## 1. Ownership is visible in the type

Four shapes, and every public type is one of them. Nothing in this surface is a raw pointer whose
ownership a caller has to infer, and nothing returns a `const` smart pointer.

### An owner

Move-only, releases what it holds in its destructor, and copying is either deleted or a named call
that says what it costs.

| Type | Owns |
|---|---|
| `Runtime/EngineContext.h`'s `EngineContext` | the thread budget, the memory resource, the limits - what a library would otherwise keep in process-wide statics |
| `Image/ImageStorage.h`'s `ImageStorage<SampleT>`, `ImageStorageBytes` | the samples, counted against the context's budget |
| `Image/Image.h`'s `Image<SampleT>` | storage, description, metadata and two generations |
| `Image/ValidityMask.h`'s `ValidityMask` | the packed per-pixel mask |
| `Runtime/ScratchArena.h`'s `ScratchArena` | the blocks one execution bumps through |

`Image<SampleT>` has a deleted copy constructor and a `clone()` that says so: a frame is expensive
and sharing one is a decision a call site should have to write. A default-constructed owner holds
nothing, which is also what a move and a `release()` leave behind; `valid()` is how to ask, and every
accessor answers for that state rather than requiring the question to have been asked first.

### A borrowed view

A pointer and a validated shape, valid for exactly as long as the thing it points into. It allocates
nothing, owns nothing, and is cheap to copy.

- `Image/ImageView.h`'s `ImageView<SampleT>` and `Image/PlaneView.h`'s `PlaneView<SampleT>` pair a
  typed pointer with a layout whose element size, buffer size and base alignment were checked once.
  Constness lives in the sample type, as it does in `std::span`: `ImageView<const float>` is the
  read-only one.
- `Image/ValidityMaskView.h`'s `ValidityMaskView` is the mask's borrowed form.
- The per-operation aggregates - `PointwiseImageView`, `StackSourceBand`, the kernel views - borrow a
  layout **for the length of one call**. Their `const ImageLayout*` must point at a named object that
  outlives the call, never at a member of a temporary.
- The frame views that name a frame across a sequence of calls - `CalibrationFrameView`,
  `StarFrameView`, `NormalizationFrameView` - borrow the samples and **hold** the layout and the
  description by value, so the only lifetime a caller keeps for them is the buffer's.

`viewsOverlap` reports aliasing conservatively; an operation that refuses an aliasing destination
says so by name.

### An operation handle

Represents permission to do something, counts itself while it exists, and is not copyable.

- `Runtime/ExecutionContext.h`'s `ExecutionContext` is one operation's policy and control. It holds
  an `EngineContextLease`, so the engine context it came from cannot be shut down under it, and it
  takes a share of that context's thread budget for its whole life - which is why a workflow gives
  each operation its own scope and a caller that wants two in flight narrows
  `ExecutionOptions::maxConcurrency` for each.
- `Image/Image.h`'s `ImageWriteAccess<SampleT>` is write permission to an image's samples. Obtaining
  one moves the image's pixel generation whether or not a sample changes.
- `Runtime/DeviceEvent.h`'s events and `Algorithms/Kernel/Device.h`'s work handles are deferred
  completion, completed exactly once from any thread.

### An opaque implementation state

A public type may name an incomplete type when that keeps an allocator, a scheduler, a mutex or an
external library's type out of the published surface. `EngineContextState`, `OperationArena` and
`OperationTelemetryState` are incomplete to a consumer and stay that way. This is a layering rule,
not a compatibility promise: it does not make a header change binary-compatible, because nothing
here does.

### What a caller must outlive

| The caller keeps alive | For as long as |
|---|---|
| the `EngineContext` | any image, execution, storage or lease taken from it exists - it refuses to shut down otherwise, and that refusal is how a leak is found |
| the samples a view borrows | every call that receives the view |
| a `ProgressSink`, `DiagnosticSink` or `TraceSink` | the execution that names it |
| a frame source, codec, or processor it implements | every call that reads through it |
| a `MemoryResource` it supplies in `EngineContextOptions` | the context that takes its bytes from it |

Strings in a `TraceEvent` are borrowed for the callback only. Callbacks are serialised per operation
and across no others, so a sink shared by two executions must be thread-safe.

---

## 2. Failures are returned; exceptions are for defects

**If a correct caller can provoke it with valid input, it is a returned failure.** A file that does
not exist, a parameter out of range, a memory budget exceeded, a cancelled operation - these are
outcomes, not surprises, and reporting them by exception makes every caller's error path invisible at
exactly the call sites where a reviewer needs to see it.

```cpp
an::Result<an::Image<float>> image = an::Image<float>::create(engine, descriptor, arrangement, init);
if (!image.hasValue())
{
    switch (image.status().code()) { /* ... */ }
}
```

- `Core/Status.h`'s `Status` for an operation that returns nothing; `Core/Result.h`'s `Result<T>` for
  one that produces a value. Both are `[[nodiscard]]`, so ignoring a failure is a compiler
  diagnostic rather than a review finding. Both are shaped like `std::expected`, which this project
  cannot use until it moves to C++23.
- **Branch on `ErrorCode`, on `errorCategory(code)`, or on a typed context field. Never on a
  message.** A message explains, to a person, and is free to change in any commit. `Core/ErrorCode.h`
  carries the closed enumeration, its category, its domain and its text; `StatusDetail` and
  `StatusContextField` carry what a caller may need to act on - the parameter's name, the requested
  and available bytes, the frame that refused.
- A failure carries its **cause**, so a stack's refusal names the read that produced it. The engine
  never names the *stage* a consumer called it from: it has no stages. A workflow wraps a stage's
  failure in one of its own and keeps the engine's as the cause.
- **Cancellation is a returned failure too.** `ExecutionContext::checkCancellation()` is the
  checkpoint, and `isCancellation(code)` recognises the result.
- An operation that fails produces **no partial result**: a destination is either what the operation
  promised or untouched, and a written file is whole or absent with nothing beside it.

Exceptions remain for programming errors and for conditions with no useful local handling. They may
cross the boundary, because one compiler and one runtime are on both sides of it. An external
library's exception is translated at its adapter boundary through `Core/ExceptionBoundary.h` -
`evaluateAcrossBoundary` and `callAcrossBoundary` - and reaches the caller as an
`ErrorCode::ExternalFailure` naming the library. **A consumer implementing a callback the engine
calls back into - a frame source, a codec, a processor - should expect its own exceptions to be
translated the same way rather than to unwind through the engine.**

---

## 3. Asking what the build and the machine can do

Every one of these is answerable **before** a context exists, which is the point of them: they are
what a caller configures a context from.

| Question | Header |
|---|---|
| What version and interface generation is loaded; how many threads the host has; whether this binary contains the GPU backend | `Runtime/EngineCapabilities.h` - `engineCapabilities()` |
| Which instruction sets the processor supports **and the operating system has enabled** | `Runtime/CpuCapabilities.h` |
| Which backend and instruction set an operation actually ran on | `Runtime/BackendDispatch.h`, and `ExecutionContext::backend()` after the call |
| Which third-party libraries this build carries, at which version its headers declared and which the loaded copy reports | `Core/DependencyReport.h` - `engineDependencyCount()`, `engineDependencies()` |
| The ceilings on extents, strides and allocations | `Core/EngineLimits.h` - `defaultEngineLimits()` |

Two of these have a shape worth stating plainly.

**Forcing a backend needs two things to be true.** The host must be able to run it *and* this build
must contain it. A request that fails either is `ErrorCode::UnsupportedBackend` when no fallback is
permitted, and is quietly unconstrained when one is. `engineCapabilities().gpuBackend` false means
the module is not in the binary at all, which is a different answer from a host with no device - and
no build of this repository contains one today, so `ComputeBackend::Gpu` is always that answer. GPU
work reaches a device through the separately compiled seam in `Algorithms/Kernel/Device.h`, which
that enumerator does not select.

**A dependency has two versions and either may be empty.** What the headers declared when the engine
was compiled, and what the loaded copy reports at run time, are different facts on a shared library,
and empty is an answer.

There is no strict/reproducible/fast mode selector. There is one numeric contract: a reduction whose
result feeds a decision gives the same answer at any thread count. `ReductionDeterminism::All` extends
that to reductions that are only reported, by admitting the operation on exactly one worker whatever
concurrency it asked for; it costs throughput, travels with the execution and is recorded in a stack
plan.

---

## 4. The three objects a consumer builds

```cpp
an::Result<an::EngineContext> engine = an::EngineContext::create({.maxConcurrency = 8u});
```

**`EngineContext`** owns what a library would otherwise keep in process-wide statics. There is no instance
accessor and no process-wide default, so two contexts coexist and share nothing. Maximum concurrency
is fixed at construction: changing it means quiescing outstanding work and building a replacement
through `createReplacement`, not reconfiguring one in place. A context with any work in flight
refuses to shut down.

**`ExecutionContext`** is one operation's: cancellation through a `std::stop_token`, an optional
deadline, borrowed progress, diagnostic and trace sinks, a borrowed scratch arena, a backend
preference, and a concurrency request that may only *narrow* the context's. `admittedConcurrency()`
is the share actually granted, which is not `maxConcurrency()` - the ceiling the caller asked for.
Progress, diagnostics and traces from one operation share one order and carry its `operationId()`;
`operationCounters()` reports how many of each were delivered and discarded.

**Memory** comes from the context. A budget refusal is a returned failure carrying the requested,
available and limit bytes; live, peak and denied counters are always on. `ScratchArena` bumps within
blocks it keeps for the arena's life, belongs to one execution, and is not thread-safe. A caller may
supply its own `MemoryResource` in `EngineContextOptions`, and the context's own resource then takes
its admitted bytes from it while keeping the budget, the alignment contract and the counters.

**Which operations report progress.** An operation reports when its work is not one pass over its
input - iterative, patch-search, layered and fitted operations, in a fixed total its last report
reaches. A pointwise chain, a conversion, a calibration or a warp of one frame reports nothing,
because the caller counts frames. A workflow's progress is the workflow's: the engine has no
workflows, and no amount of per-operation progress adds up to one without the consumer saying how
many stages it has.

---

## 5. What the recorded surface promises, and what it does not

**It promises** that the published surface is written down, that a change to it fails a check until
someone has read the change and moved the number that describes it, and that every name the headers
declare exported is a name the built library exports - and that the library exports nothing else of
its own. The last clause has a platform in it. On Windows the export table holds only what a
decorated declaration put there. On Linux the libraries are built with `-fvisibility=hidden` so that
the same is true of this engine's code, but a dynamic symbol table also holds what the C++ standard
library and other libraries make visible by their own decision - namespace `std` is default-visible
because the identity of everything in it has to be one thing across a process - and those are read
and counted rather than compared. What a consumer can rely on is the same on both: every declared
name is there, and nothing of this engine's that was not declared.

Four types are exported for their identity rather than for a declaration. `Runtime/MemoryResource.h`'s
`MemoryResource`, `IO/ImageCodec.h`'s `ImageCodec` and `IO/FrameSource.h`'s `FrameSourceBackend` are
the types a consumer implements, and `Algorithms/Drizzle/DrizzleStackSource.h`'s
`DrizzledStackFrameSource` is one a consumer constructs and moves; all four carry
`AFTERNIGHTENGINE_INTERFACE` so that their vtable and type information are reachable from the code
that derives from, constructs or moves them. The macro is the visibility attribute where the ABI
needs it and nothing on Windows, where the compiler emits both beside the derived class already.

**It does not promise** binary compatibility across commits. There is no C shim, no stability
guarantee and no SDK: every consumer is built from this checkout with this engine, and the version
check in §0 exists so that a stale one is *detected*, not tolerated. If third-party native code is
ever meant to link this engine, that is a new decision and a real body of work.

**It does not cover the documentation**, which is required of every public declaration and checked
separately, nor the implementation behind the handles, nor the opt-in GPU proof backend, which no
default build contains.

**It does not mean the surface stops growing.** An addition - a header, a type, an overload, an
enumerator at the end of an enumeration - moves the minor version and leaves the interface generation
alone, because an object compiled before it is still right about everything it knows; a member added
to a public record, a changed exported signature or a renumbered enumerator moves the generation as
well; a change that leaves every declaration and record as it was moves the patch version.
`Core/Version.h` states the current numbers.

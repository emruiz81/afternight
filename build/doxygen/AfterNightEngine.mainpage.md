# AfterNightEngine API reference {#mainpage}

This is the generated reference for the published surface of the AfterNight image engine: every
header a consumer may include, every declaration in it, and each header's own account of what it is
for. It is generated from the headers and nothing else, so what it says is what the code says.

Three libraries publish a surface, each with its own include root:

| Library | Include as | What it is |
|---|---|---|
| The engine | `<AfterNightEngine/<Area>/<Name>.h>` | Images, their meaning and their memory; the runtime that owns threads, memory and cancellation; every algorithm; codecs and sequences |
| The Python adapter | `<AfterNightEnginePythonAdapter/<Name>.h>` | An optional library that runs a caller's Python on engine images. It reports itself absent in a build without Python |
| The G'MIC adapter | `<AfterNightEngineGmicAdapter/<Name>.h>` | An optional library that runs G'MIC pipelines on engine images. It reports itself absent in a build without G'MIC |

A tree generated with the opt-in device backend publishes a fourth surface for it; it is a
non-shipping proof of the device seam in `Algorithms/Kernel/DeviceBackend.h` and nothing links it
but its own suite.

## Where to start

Read these before anything else. Each is short, and each answers a question every consumer has.

1. **`Core/Version.h`** - the first call. `an::engineMatchesHeaders()` says whether the loaded library
   was built from the headers this caller compiled against. If it was not, nothing else here may be
   trusted.
2. **`Runtime/EngineContext.h`** - the owner of everything the engine keeps: the thread budget, the
   memory resource and the limits. There is no global engine; a caller constructs one, and two can
   coexist.
3. **`Runtime/ExecutionContext.h`** - one operation's policy and control: cancellation, a deadline,
   progress, diagnostics and traces, and the share of the context's threads it runs on. Every
   non-trivial operation takes one.
4. **`Core/Status.h` and `Core/Result.h`** - how every expected failure is reported. Branch on
   `an::ErrorCode` (`Core/ErrorCode.h`), never on a message.
5. **`Image/Image.h`** - an owned image: its samples, what they mean, and what has been done to them.
   `Image/ImageDescriptor.h` states the meaning, `Image/ImageView.h` borrows the samples.

## The areas

| Area | Holds |
|---|---|
| `Core` | Version and ABI checks, `Status` and `Result`, error codes, checked geometry and layouts, limits, the exception boundary and the dependency report |
| `Runtime` | The engine and execution contexts, the task scheduler, memory resources and scratch arenas, progress, diagnostic and trace sinks, CPU capability detection and backend dispatch, device residency and events |
| `Image` | Descriptors, sample types and traits, storage, views, images, metadata and provenance, validity, and the conversion policy and its operations |
| `Interop` | Descriptions of other libraries' buffers - OpenCV, FFTW, GSL - that never include those libraries |
| `IO` | The codec registry and its codecs (FITS, XISF, TIFF, PNG, JPEG, RAW, the developer dump), frame sources and sinks, sequences and streaming |
| `Algorithms` | Every processing operation, one directory per domain, and the kernel contracts in `Algorithms/Kernel` that the CPU backend and the device seam implement |

## The guides

The reference says what each declaration does. The guides say how the parts fit, and why, and they
live in `docs/dev/guides/`:

| Guide | Answers |
|---|---|
| `ARCHITECTURE.md` | How the engine is layered, what owns what, and how one operation flows through it |
| `NUMERIC_VALIDITY_METADATA.md` | Sample types, precision, conversion, validity and no-data, metadata and provenance |
| `BACKENDS.md` | CPU dispatch and instruction sets, the kernel contracts, and the device seam |
| `DEPENDENCIES.md` | Which libraries the engine uses, where each is allowed to appear, and how versions are reported |
| `BUILD.md` | Building the engine, its adapters and its suites on Windows and Linux, and the generation switches |
| `TESTING.md` | The direct suites, the fixture layers and their reference expectations, the campaigns and the checks, and what each one proves |
| `BENCHMARKS.md` | The benchmark runner, the scenario contract, and how a comparison is decided |

`docs/dev/PUBLIC_API_CONTRACT.md` is the one-page statement of what is true of every header:
the ownership shapes, the error model, and the capability queries.

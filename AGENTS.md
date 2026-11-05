# AfterNight – Agent Guide

This document is the canonical, tool-agnostic guide for automated coding agents and human contributors working in the AfterNight repository across Windows (MSVC) and Linux.

It is intended for any agentic workflow (GitHub Copilot, Codex, Cursor, Claude Code, internal bots, etc.). Tool-specific instruction files (for example, GitHub Copilot’s repository instructions) should point here instead of duplicating these rules.

## Scope and mission

- Primary goal: implement features and fixes in C++20/Qt6 projects: the engine (`AfterNightEngine`, a shared library), the platform and consumer runtime libraries, AfterNightControls and AfterNightProcesses, AfterNightApp, AfterNightCLI, the extension hosts and runtime backend, and their unit tests.
- Success criteria: builds complete in required order on target platforms; tests pass; binaries run with deployed dependencies; changes follow code style and are minimal, well-structured, and performant.
- Stop criteria: all acceptance checks pass (build + tests + deploy), or you are blocked by missing external prerequisites that you have documented.

## Source of truth

- If multiple instruction documents exist, treat this file as the source of truth for build order, testing, coding standards, and “definition of done”.
- If a tool-specific file conflicts with this guide, follow this guide and consider updating the tool-specific file to reference this one.
- This file states the rules. How the parts fit, and why they are built the way they are, is in the guides under `docs/dev/guides/`: the engine's, and how the product uses the engine, area by area, in `docs/dev/guides/PRODUCT_ARCHITECTURE.md`. A design's reasons are written in the guide for its area; there are no decision records. `docs/dev/README.md` indexes the developer documentation.

## Repository overview

- Language: C++20 (C for some deps)
- Build system: Premake5 generates native projects (Makefiles, VS2026)
- UI: Qt 6 + KDDockWidgets. The authoritative Qt version is `QT_VERSION` in `build/premake/afternight-common.lua` (currently 6.10.3).
- Tests: Google Test
- Deps of the product: oneTBB, Highway, CFITSIO, libtiff, libpng, libjpeg-turbo, LibRaw and a private RawSpeed build, OpenXISF, FFmpeg, Exiv2, FFTW (single precision), GSL (one function), nlohmann-json, G'MIC (through its adapter), Crashpad (MSVC/Linux), Qt's SQLite driver, and Python 3.14.7 for the extension hosts and runtime backend; `build/scripts/tools/dependencies.txt` is the register. OpenCV, the public LibRaw and, on Windows, Eigen serve only the library parity suite as reference libraries.
- Platforms: Linux (Clang), Windows MSVC

Key build scripts and configs live under `build/`:
- `build/premake/*.lua`: premake definitions and shared constants (Qt version, paths)
- `build/scripts/<platform>/`: platform build automation (create, install deps, build, deploy)
- `build/scripts/tools/`: the checks, registries and campaign runners that hold the tree.

Module layering (link order is the dependency order):

```
AfterNightPlatform (shared lib, an::platform)        <- logger, logging config, crash handler; no engine, no Qt
AfterNightEngine (shared lib, an)                    <- the image engine; no Qt
AfterNightConsumerRuntime (shared lib, an::consumer) <- Engine + Platform: session, operation scope, image asset; no Qt or Python
AfterNightExtensionProtocol (shared lib)             <- engine-free, Apache-2.0 boundary
AfterNightControls (shared lib, an::ui)              <- ConsumerRuntime + Engine + Platform + Protocol + Qt + KDDockWidgets
AfterNightProcesses (shared lib, an::ui::processes)  <- Controls + ConsumerRuntime + Engine + GmicAdapter + Platform + Protocol
AfterNightApp (exe)                                  <- Processes + Controls + ConsumerRuntime + Engine + Platform + Protocol
```

`AfterNightCLI` links ConsumerRuntime, the engine and Platform. `AfterNightExtensionHost`, the full host, links Processes, Controls, ConsumerRuntime, the engine and the Python interpreter, and loads `AfterNightExtensionRuntimeBackend` by name; `AfterNightExtensionHostLite` links only the protocol library. That separation is the licensing boundary described in `docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md`.

## Workflow

- This repository is public, and its `main` is the development line; work is tracked as its GitHub issues. `.github/workflows/ci.yml` runs on a pull request to `main` - the format check, and the Linux build and tests; the Windows build runs on a push to `main`, on a release, and on a pull request that changes the build or carries the `ci:windows` label. The test and sanitizer workflows run on demand.
- **Comparisons with other products are not written here.** A document that compares AfterNight with another product, measures AfterNight against one or plans to exceed one belongs to the maintainer's private notes, which are kept outside this repository; nothing here cites them. What stays here is attribution - the credit, the licence and the provenance of derived code ("Credit for derived code" below) - and compatibility naming, which says which file format or behaviour of another program a feature reproduces. The development record before the first public commit is private too (`HISTORY.md`).
- Validation runs locally, as `docs/dev/LOCAL_VALIDATION.md` describes: an agent runs what the Windows host can run, and the maintainer runs the Linux commands and supplies the result. Say what ran - the commands, the suites and their counts - in the commit message or the pull request. A check that cannot run is reported as unavailable, never described as passing.

## The engine (`src/AfterNightEngine`)

`AfterNightEngine`, in `namespace an`, is the engine every product target links. `docs/dev/guides/ARCHITECTURE.md` is how it fits together; these are the rules a change to it keeps.

### Layout, layering and documentation

- **Two roots, two include forms.** A public header lives under `include/AfterNightEngine/<Area>/` and is included as `<AfterNightEngine/Area/Name.h>`; an implementation header lives under `src/<layer>/` and is included as `"layer/Name.h"`. The two forms mean different things and are not interchangeable.
- **Layers depend downwards.** The areas are `Core`, `Image`, `Interop`, `Runtime`, `Algorithms`, `IO`; the implementation layers are `core`, `image`, `runtime`, `memory`, `kernels/reference`, `backends/cpu`, `backends/gpu`, `interop/<library>`, `algorithms/<domain>`, `io/<codec>`, `ported`. A public header includes no external library and no private header; a layer includes only the libraries it declares; an adapter under `interop/<library>` adapts that one library; a GPU SDK header appears only under `backends/gpu`; and nothing includes another first-party component. `check_include_layers.py` holds it.
- **An optional adapter is a separate library, and optionality is a build property.** The engine links no Python runtime and no G'MIC library. `src/AfterNightEnginePythonAdapter` and `src/AfterNightEngineGmicAdapter` each link the engine's published surface and one external library, and an adapter's own public surface carries no header of that library, so a consumer compiles against it without that toolchain. Only its own suite links the Python adapter; the G'MIC adapter is linked by its suite, `AfterNightProcesses` and `Test_ProcessesSpatial`; `check_build_graph.py` holds both. `AFTERNIGHT_PYTHON=0` and `AFTERNIGHT_GMIC=0` regenerate an adapter without its library - it then reports itself absent and refuses every call by name - and both configurations are built and tested. Only a preprocessor define changes between them, so touch the adapter's sources between configurations or nothing rebuilds.
- **Every public header has a self-compile unit** under `src/AfterNightTests/Test_EnginePublicHeaders/SelfCompile/`, named after the header path with the separators removed - `Core/Version.h` becomes `CoreVersion.cpp` - which includes that header twice and nothing else.
- **Every public declaration is documented**, in the `///` and `///<` form with `@`-prefixed commands and no `@brief`: the first sentence is the brief, because Doxygen's AUTOBRIEF is on. Document what a caller cannot infer - ownership, lifetime, units and ranges, thread safety, cancellation, error results, numeric guarantees - and `@param`/`@return` for each one that exists. `check_public_documentation.py` runs Doxygen and fails a declaration or return value that carries nothing; whether what is there says any of the above is for review.
- **Every file says what it is for, and a private header is documented like a public one.** Each file of the engine, its adapters and the opt-in backend opens with a module comment before its first line of code - a published header's is a `/// @file` block, which puts it on the header's page of the reference - and every declaration in a private header is documented (`build/doxygen/AfterNightEngineInternal.doxyfile`, without the per-parameter rule). No comment or literal there names a work unit, a plan coordinate, a registry number or a tracker handle; no `TODO` or `FIXME` stands in for a stated limitation; and a path a comment cites exists. `check_engine_documentation.py` holds all of it, and the guides under `docs/dev/guides/` too. A comment that disagrees with the code is corrected to the code; a code defect found by reading is recorded, not rewritten into prose.
- **A header carries no implementation the engine did not decide to put there.** `check_header_architecture.py` holds every header of the engine, both adapters and the opt-in backend to 600 declaration lines - lines that are not blank, a comment or a preprocessor directive, so the mandatory documentation never trips it. A header over that is registered, and an entry has to be private, have one reason and be included by exactly one translation unit; the register is read in both directions, so an entry cannot outlive its reason. The register of explicit instantiations is empty: per-type work lives in private per-instruction-set units behind a dispatch table. `check_build_graph.py` refuses a precompiled header or a forced include on the engine's side of the workspace.

### Errors, state and execution

- **Expected failures are returned, not thrown.** An operation that returns nothing returns `Core/Status.h`'s `Status`; one that produces a value returns `Core/Result.h`'s `Result<T>`. Both are `[[nodiscard]]`. If a correct caller can provoke it with valid input, it is a returned failure. Exceptions remain for defects, and an exception from an external library is translated at its adapter boundary through `Core/ExceptionBoundary.h` - none may cross a kernel, a thread entry, a callback or a codec boundary untranslated. A caller branches on `ErrorCode`, `ErrorCategory` or a typed context field; a message is written for a person and is never parsed. A new `ErrorCode` goes into `errorCategory` and both text tables, before `Internal`, which stays last.
- **The engine owns no globals; a caller owns an `EngineContext`.** `Runtime/EngineContext.h` has no instance accessor and no process-wide default, so two contexts with different settings coexist. Its maximum concurrency is fixed at construction: no kernel, codec or workflow creates a parallelism limit of its own, and changing the maximum means quiescing the outstanding work and building a replacement through `EngineContext::createReplacement`, never reconfiguring one in place. `EngineContextLease` counts the work in flight, and a context that still has any refuses to shut down. What the build and the host offer is `Runtime/EngineCapabilities.h`, answerable before any context exists.
- **Every object with process lifetime is declared, with a category and a reason.** `check_process_state.py` finds every declaration in the engine's four libraries whose storage outlives every call, execution and context, and refuses one its table does not list. A new one is an `identity` counter (which must be atomic), a `constant` (which must be const), `library-state` (only in an interop or adapter directory, for a lock or one-time initialization a linked library makes process-wide) or an `adapter-seat` (only in an optional adapter, for a library that exists once per process). There is no category for a cache, a registry, a setting or a logger. `--objects` reads the same answer out of the built objects.
- **Every non-trivial algorithm takes an `ExecutionContext`.** `Runtime/ExecutionContext.h` is one operation's policy and its permission to run: cancellation through a `std::stop_token` - never a raw pointer and never a bool - an optional deadline, the caller's progress and diagnostic sinks, a borrowed scratch arena, a backend preference, and a concurrency request that may narrow the engine context's maximum and never widen it. It holds a lease, so a context with an operation in flight cannot be shut down. `checkCancellation()` is what a checkpoint calls: allocation-free, and it tells `Cancelled` from `DeadlineExceeded`. The engine writes to no stream and no log - it reports through `Runtime/ProgressSink.h`, `Runtime/DiagnosticSink.h` and `Runtime/TraceSink.h`, all `noexcept`, or says nothing. There is no strict, reproducible or fast mode: the engine has one numeric contract, and `Runtime/ExecutionPolicy.h` carries only what genuinely varies per operation.
- **Everything one operation reports carries one order, and the execution context assigns it.** Progress, diagnostics and traces of one operation share a sequence that starts at one, never decreases and saturates rather than wraps; different operations are told apart by `ExecutionContext::operationId()`. A progress report that would move backwards is counted and not delivered, and `ExecutionContext::operationCounters()` says how many of each kind were delivered and discarded. Every string in a `TraceEvent` is borrowed for the call, so a sink that keeps one copies it. One operation's callbacks are serialized with each other and with no other operation's, so a sink handed to two executions must be safe for concurrent calls.
- **An operation reports progress when its work is not one pass over its input.** An iterative restoration, a patch search, a decomposition into layers or a model fitted by rejection reports in a total that never changes during the call and that its last report reaches; a pointwise chain, a conversion, a calibration or a warp of one frame reports nothing, because its caller counts frames. An operation that composes another maps the nested reports into a share of its own total through the private `runtime/ProgressRange.h`, sized so the nested count still moves the bar. `OperationProgressTest` in `Test_EngineImageOps` holds every reporting operation to that contract. `Test_EngineConsumerProbes` holds the three consumer shapes the public surface is judged against - a command-line one-shot, a file-backed integration pipeline and an asynchronous preview.
- **Parallel work goes through `Runtime/TaskScheduler.h`, and nothing caps parallelism for the process.** `tbb::global_control` and `tbb::task_scheduler_init` are refused anywhere in the tree by `check_forbidden_constructs.py`: both resize the pool for the whole application. An engine context owns a budget of threads and lends part of it to each operation, which runs in a `task_arena` of exactly that width, so the grants outstanding in one context never exceed its maximum and two contexts share nothing; a nested `tbb::parallel_for`, in adopted code too, inherits the operation's width without being told. `parallelFor` and `parallelReduce` run on exactly as many workers as the operation was granted. A loop's grain may follow the machine (`chooseGrainSize` takes a concurrency); a reduction's partition may not (`deterministicChunkCount` does not), so a reduction gives the same answer at any thread count. `ExecutionContext::admittedConcurrency()` is the share an operation was granted, not `maxConcurrency()`, the ceiling its caller stated.
- **Memory comes from the context, and scratch dies with the execution.** `Runtime/MemoryResource.h` is the context's persistent allocator: alignment is a parameter and is delivered, a refusal is a returned failure carrying the requested, available and limit bytes, and live, peak and denied bytes are always counted. `Runtime/ScratchArena.h` keeps its blocks for the life of the arena - `reset()` and `rewind()` return nothing to the allocator, and the destructor returns everything - and an arena is not thread-safe: one belongs to one execution. `EngineContextOptions::memoryResource` puts a caller's resource underneath the context's while the budget, the alignment contract and the counters stay the context's, which is how a test refuses an allocation at a chosen boundary without the engine being able to tell it from a machine that has run out.
- **Which implementation ran is asked of the dispatch table, not asserted.** `Runtime/CpuCapabilities.h` detects once and immutably, gating AVX and AVX-512 on the operating system's state as well as the feature bit. `Runtime/BackendDispatch.h` resolves an operation's backend and instruction set and reports what the selected table's own compiled code says it is. Forcing a set needs this host to run it and this build to contain it; a request that fails either is `ErrorCode::UnsupportedBackend` when no fallback is permitted and unconstrained when one is.
- **The binary interface is version-locked in-tree, and a mismatch is detectable rather than tolerated.** Nothing ships a header of this engine, so `Core/Version.h` makes no compatibility promise; it exists for an object compiled against one revision of a public header linked beside objects that used the next. The header states the version and `kEngineAbiGeneration` as compile-time constants, the library reports what it was built from, and `engineCompatibility()` and `engineMatchesHeaders()` evaluate the caller's constants, so a stale translation unit is caught. An ABI mismatch outranks a version mismatch, and either is fatal. The version started at 1.0.0 and the generation at 1; the generation moves when an existing public layout or exported signature changes, not when a header or a type is added. Public value and event records keep visible standard-layout members; implementation services stay incomplete types behind handles. `check_public_api.py` holds the published surface to `build/scripts/tools/public_api.txt`, and `--history <base>` holds every commit since a base to the same two tiers.

### The image model

- **What an image is, is stated rather than inferred from a C++ type.** `Image/ImageDescriptor.h` carries the extent, `Image/SampleType.h`'s representation and the byte order a boundary reads it in, `Image/ChannelSemantics.h`'s channels, colour basis and numeric domain, `Image/CfaPattern.h`'s filter array and the phase a crop moves, the coordinate origin and the pixel aspect. It refuses the combinations that cannot describe one image - Lab channels in a display RGB basis, a physically scaled value in an integer sample with no scale, a filter array over three colour channels - each as a named `Image/DescriptorError.h` reason. It carries meaning, not memory: `Core/ImageLayout.h` owns where the samples are and `Image/StorageArrangement.h` names what a layout arranges. There is no setter: read `toRequest()`, state the change and build a new descriptor. `Image/DescriptorText.h` is one lossless line per descriptor, for fixtures and failure messages.
- **An image's memory is owned, counted and released; reaching it is checked once, outside the loop.** `Image/ImageStorage.h` takes its bytes from the context's memory resource and holds a lease on the context, so a context with an image outstanding refuses to shut down. What its samples start as is a stated `StorageInitialization`: ask for zeroed storage when a buffer is accumulated into or only partly covered, because uninitialized samples once produced a comet stack wrong in half its runs. A refusal carries the request, what was available and the limit; `reshape` re-describes retained capacity and never reaches the allocator. All eight resident types share one untyped `ImageStorageBytes`, and `Image/SampleTraits.h` is the closed two-way table between a `SampleType` and its C++ type, proved at compile time to agree with `Image/SampleType.h`. `Image/PlaneView.h` and `Image/ImageView.h` check the element size, the buffer size and the base alignment once; a row is then a `std::span` that stops before the padding. A plane view and an image view are distinct types on purpose, constness lives in the sample type as it does in `std::span`, and `viewsOverlap` answers aliasing conservatively rather than deciding the rule.
- **An image is its samples, what they are, and what has been done to them.** `Image/Image.h` holds owned storage, a descriptor, an `ImageMetadata` and two counters, and has no algorithm methods and no implicit conversion of any kind - not between sample types, not to a view, not to its storage. Reading is free; `write()` returns a handle and moves the pixel generation whether or not a sample changes. The semantic generation moves when the description, the processing meaning or the astrometric solution changes, and for nothing else; a composite cache key belongs to whatever owns the cache. `transition` is the only door to a new description or new metadata, and it requires the lineage entry that explains the change.
- **Float32 is the default arithmetic type, and Float64 is reached by name.** `Image/ComputePrecision.h` is the policy: an operation computing in double states which of six approved reasons applies, and Float32 is refused where the storage and the output promise need more - the UInt32 and Int32 case. `WorkingImage<Precision>` and `createWorkingImage` are how an operation chooses its working representation instead of inheriting one.
- **A boundary frame is described, not reinterpreted.** `Image/RawFrame.h` carries a decoded frame with its representation, byte order, geometry and meaning stated. `Image/RawSampleFormat.h` is a width and a class, and whether it is one of the eight is derived from a closed table, so a 64-bit integer frame is carried and refused entry rather than narrowed. `Image/SampleBuffer.h` says who frees the bytes: engine memory, counted and the only kind an image adopts with no copy; mapped or decoder-owned bytes, released through a lifetime token; and borrowed bytes, released by nobody. `checkAdoptable` names which of three things blocks an adoption.
- **Metadata is six typed objects that travel as one and change through one door.** `Image/ImageMetadata.h` carries `AcquisitionMetadata`, `ImageSemantics`, `AstrometricSolution`, `Provenance`, `CodecMetadata` and an optional `OutputIntent`, absent until something decides how the image is written, so a source fact never becomes an output decision by inheritance. An algorithm returns a `MetadataTransition`, which has no field for the acquisition record and must carry the lineage entry for any change; `changesMeaning` names the two objects that move the semantic generation. Provenance is append-only, bounded and never part of a computation or a cache key.
- **Which samples mean something is stated, never inferred from the pixels.** `Image/ValidityDescriptor.h` is one of five rules - all valid, non-finite invalid, exact zero is no data, an explicit packed mask, or a stated closed range - carried in the processing meaning, so a change of validity moves the semantic generation. The default is all valid and no rule is ever acquired; the exact-zero rule is a compatibility convention a source asks for by name, because a black pixel is data. `Image/ValidityMask.h` is the packed mask: one bit per pixel per plane, set meaning valid, the bits past a row's last pixel held at zero by every write, owned and budgeted, and travelling beside the image rather than inside it. Every operation declares what it does with validity - preserve, combine, regenerate or reject - and `applyValidityPolicy` honours it: two claims that disagree cannot be preserved, combining them answers with the mask rule, and a reduction over nothing reports `ErrorCode::EmptySelection`.
- **A conversion is five named decisions, and none of them is a cast.** `Image/ConversionPolicy.h` states rounding (to nearest, ties to even), range (saturate the real value before converting), non-finite mapping (NaN to zero, the infinities to the destination's extremes), domain mapping (integer and floating full scales correspond, so the integer round trip is the identity) and the signed convention (two's complement unless a FITS or TIFF boundary asks for offset binary). `Image/SampleConversion.h` is the scalar matrix behind them, computed in `double`, which is exact for all six integer representations, and it counts the non-finite and clamped samples it met. `Image/ImageConversion.h` applies the same rules to whole images on the operation's threads, refuses an aliasing destination, and keeps repacking and byte order as separate calls; forcing `ComputeBackend::Reference` runs the scalar matrix and the automatic backend the compiled one, which is what makes the differential sweep a comparison.

### Operations the product calls rather than reimplements

The product computes none of these answers itself; a copy in the product is a defect to remove. How each area calls them is in `docs/dev/guides/PRODUCT_ARCHITECTURE.md`.

- **What a viewer paints:** `Algorithms/Presentation/DisplayPresentation.h`. `presentImage` presents every sample; `presentImageRealtimePreview` runs only the subsampling preview plan, and the production executor refuses an analysis a preview measured. Hand back the `DisplayPresentationAnalysis` a call returned instead of measuring again; the display constants are `kDisplayShadowsClipping` and `kDisplayTargetBackground`.
- **One channel or colour component:** `Algorithms/Color/ChannelScope.h`'s closed `ChannelScope` enumeration drives the process selector, the viewer's channel display and the curves; `applyChannelScope` extracts the plane, runs the caller's `ChannelScopeOperation` and recomposes, and `extractChannelScope` writes the plane alone. Labels stay in Controls, mapped onto the enumeration in one table. The comparison view's difference is `Algorithms/Pointwise/ImageDifference.h`, and the one switch from a `SampleType` to its C++ type is `dispatchSampleType` in `Image/SampleTraits.h`; `check_forbidden_constructs.py` refuses a second definition.
- **A mask's overlay and a drawing tool's numbers:** `Algorithms/Masks/MaskOverlay.h`'s `renderMaskOverlay`, `Algorithms/Spatial/DrawingPrimitives.h` (the brush tip, dab placement and stamp, and the shape feather) and `Algorithms/Pointwise/LayerBlend.h`'s five blend modes. The tint colours, the canvas, the stroke points, the undo patches and the shapes stay in the tools.
- **Combinations and external tools:** `composeBlend` (`Algorithms/Pointwise/BlendComposition.h`) takes the whole blend request - both transfers, the pre-filter, the kept lightness, one of the twenty-seven modes in the product's menu order and the opacity - so a blend is never modulated again by its caller; `combineLrgb` (`Algorithms/Color/LrgbCombination.h`); `recombineStarMask` (`Algorithms/Stars/StarMaskRecombination.h`); `prepareExternalToolHandoff` and `restoreExternalToolAnswer` (`Algorithms/Pointwise/ExternalToolHandoff.h`) are the arithmetic around StarNet and DeepSNR, and the program and its file stay in the application.
- **The stacking stages' own arithmetic:** `splitCometAperture`, `renderCometMask` and `blendCometComposite` (`Algorithms/Stacking/CometComposite.h`) around `executeStack`; `countRejectionMaps` (`Algorithms/Stacking/StackQuality.h`); `planDrizzleCanvas` and `drizzleLocalNormalizationShape` (`Algorithms/Drizzle/DrizzleCanvas.h`); `StackWeighting::Exposure` and `StackWeighting::SignalToNoise`.
- **Angles as text and the plate solve's hints:** `Algorithms/Astrometry/Sexagesimal.h`; `readPointingHint`, `readOpticalHints`, `resolvePlateScaleHint`, `julianEpochYear` and `sphericalMeanDirection` (`Algorithms/Astrometry/AstrometryHints.h`); `parseFitsHeaderCards` (`IO/FitsHeaderCards.h`) for a solver's header-only `.wcs`; and `detectStarsByChannelPolicy` (`Algorithms/Stars/StarDetectionChannels.h`). `parseWcsCards` reads an `EQUINOX` without `RADESYS` as FK5 and refuses it, so a caller that ran an ICRS solver states `RADESYS = 'ICRS'`.
- **A frame's kind, a combined image's metadata and a history step:** `readFrameTypeCards` and `resolveFrameType` (`IO/FrameTypeCards.h`); `combinedImageMetadata` (`IO/CombinedMetadata.h`), which builds a combination's whole metadata and is not a transition; and `writeImageSnapshot`, `probeImageSnapshot` and `readImageSnapshot` (`IO/ImageSnapshot.h`), one checked file published whole or not at all. The developer dump is not a history format.
- **A read under a named RAW policy:** `probeImage`, `decodeImage` and `decodeImageRegion` (`IO/CodecIO.h`) take a `const CodecRegistry&` after the execution and select from it alone, with no fallback to the context's. A full-colour sensor's balance is the named registry's `RawCodecOptions::fullColorWhiteBalance`; a mosaic's is `resolveCfaWhiteBalance` and `applyCfaChannelGains`; auto-rotate is `planOrientation`.

### Interop, devices and the benchmark bridge

- **An adapter describes another library's buffer; it never converts one, and it never includes it.** `Interop/ExternalBuffer.h` is what a bridge is told: what the engine's samples mean, what the library will treat them as, which side owns the bytes, how long the borrow lasts and whether the borrower may write. A difference between the two numeric domains is refused by name, and the caller converts through `Image/ImageConversion.h`. `Interop/OpenCvAdapter.h`, `Interop/FftwAdapter.h` and `Interop/GslAdapter.h` describe each library's own structures in its own field types without including it, so a consumer that never uses a library never acquires its headers; a library is included and linked only through its own layer, `interop/<library>` or `io/<codec>`. Zero-copy is decided rather than asserted - one plane shares with a `cv::Mat` and a planar colour image does not - and a refused bridge still yields the foreign buffer's engine layout, so the copy that follows is explicit and visible in a profile.
- **A device is reached through a checked facade, and nothing crosses to it implicitly.** `Algorithms/Kernel/DeviceBackend.h` is the seam a backend implements, and `Algorithms/Kernel/Device.h` checks every submission before a backend sees it. `Runtime/DeviceResource.h` states host, pinned, shared or device residency: a kernel binds shared or device memory only, device memory is never mapped, and every crossing is a declared, counted transfer. `Runtime/DeviceEvent.h` is deferred completion, and a wait given an execution stops the wait, never the work. Work that reads what unfinished work writes, or writes what it reads, depends on that work directly or transitively, or it is refused as `DeviceFailure::UndeclaredDependency` - no backend's queue order is relied on. `Algorithms/Kernel/DeviceChain.h` plans a chain of device operations, with every hand-over and transfer, before anything is allocated. `MockDeviceBackend` in `Test_EngineKernels` is the contract's proof. See `docs/dev/guides/BACKENDS.md`.
- **The GPU proof is opt-in, non-shipping and leaves no dependency behind.** `AfterNightEngineOpenClBackend` implements the device seam through OpenCL - a fused pointwise program, a three-by-three stencil and a resident chain - and only a tree generated with `AFTERNIGHT_OPENCL=1`, after `build\scripts\msvc\install_opencl_backend_deps.bat` has installed its SDK into the `x64-windows-opencl` triplet, contains it or its suite, `Test_EngineOpenClBackend`, the one target that may link it. No GPU SDK may appear in either `02-install_*` script or reach any other target (`check_gpu_backend_optionality.py`, `check_build_graph.py`), and `AFTERNIGHT_TEST_REQUIRE_OPENCL=1` makes the suite fail rather than skip without a GPU. `remove_gpu_proof.py` checks that nothing outside its manifest starts naming the proof, and `--apply` deletes it, keeping the seam and `MockDeviceBackend`; this paragraph is the prose it leaves to a person.
- **The benchmark fixture bridge translates and refuses; it never repairs.** `src/AfterNightBench/engine/FixtureBridge.h` is the only place a benchmark type and an engine type meet: a 64-bit integer fixture is refused rather than narrowed, a payload in the other byte order rather than swapped, and a stated stride is kept exactly. It supplies the meaning a benchmark descriptor does not carry, with one default for every representation, and lives in the runner rather than the engine.

### Checks, campaigns and registers

Every check named here is in the common check list of `docs/dev/LOCAL_VALIDATION.md`, which says how and when each runs.

- **A fault is injected at every place a subject has.** `Test_EngineFaults` and `run_fault_campaign.py` count the places each scenario's subject can be made to fail and fail it at each in turn; every injected run answers with a typed failure, gives every block back, comes back inside its bound, and leaves a destination holding a whole file or no file. `fault_points.txt` states a floor of injection points per scenario. `docs/dev/guides/TESTING.md` describes the four campaigns.
- **Fuzzing and the sanitizers are generated rather than typed, and a clean run has to have covered something.** The fuzz targets are registered with their budgets in `fuzz_targets.txt` and run by `run_fuzz_campaign.py`: on Clang `AFTERNIGHT_FUZZ=1` instruments every library and suite for libFuzzer, and elsewhere the targets replay and mutate their seeds through `FuzzEngine/FuzzDriver.cpp`. `AFTERNIGHT_SANITIZE=address` and `AFTERNIGHT_SANITIZE=thread` regenerate an instrumented tree into `obj/sanitize-<name>/`; `sanitizer_matrix.txt` states the configurations and their rows, and `run_sanitizer_campaign.py` refuses a row whose executable or engine library lacks the sanitizer runtime. No suppression may name this engine: the one suppression file, `leak_suppressions.txt`, names what CPython, NumPy, pybind11 and PySide6 keep past a finalized interpreter.
- **"Every suite passes" is a registry.** `conformance_matrix.txt` names every direct engine suite and the cases it nominates, and `run_conformance_campaign.py` runs them and reads each suite's own report rather than its exit code, because a suite that ran nothing exits zero.
- **Every library the engine keeps is on one register, and the engine reports its own versions.** `dependencies.txt` records, per third-party library and per ported subsystem, the version, source, licence, upstream, security channel, build and deploy owners and boundary, and `check_dependency_provenance.py` joins each claim to the include lines, the generated link lines, the install and deploy scripts, the engine's own report and the suite that pins it. `Core/DependencyReport.h` carries two versions per library - the one its headers declared and the one the loaded copy reports - because on a shared library those are different facts. A version is read with a bounded scan, and `std::regex` is refused inside the engine. See `docs/dev/guides/DEPENDENCIES.md`.
- **No critical or high defect is left open, and that is read rather than asserted.** `safety_register.txt` reviews every library the shipped binaries load, at the version whose advisories were read, and records every finding with a severity stating what it can do in this engine. `check_safety_register.py` fails a critical or high finding that is open or accepted, a repair that names no existing test or pinned release, an unanswered advisory and a review of a version `dependencies.txt` no longer pins; `--binaries` refuses a loaded module no review names. FFmpeg and Expat are pinned ahead of the vcpkg checkout in `build/vcpkg/overlay-ports`; move a pin with `vcpkg upgrade <port> --no-dry-run --overlay-ports=..\build\vcpkg\overlay-ports`, not `install`.
- **Nothing reaches an installer because it was built beside the product.** Both packagers copy the deployed `bin` directory minus patterns, so a file written there ships unless something excludes it. `check_installer_payload.py` holds them to what every source writes there - the Python adapter and the opt-in backend, the benchmark runners, the fuzz registry's targets, the library parity suite's reference libraries, the RAW qualification project's install rules and the private RAW packages `deploy_raw_deps.py` declares - reading each from its source. A new tool, record or runtime that lands in `bin` from outside the two solutions needs its source read by that check, not only a pattern in the packagers.
- **The budgets and the refused spellings are checks too.** `check_budgets.py` holds the include and compile-time budgets: its include ceilings are host-independent, and it compares compile times only against a baseline recorded on the same machine, named with `--host-id`. `check_forbidden_constructs.py` refuses the spellings this engine decided against.

Build just the engine and its direct suites with `build\scripts\msvc\build_afternightengine.bat` or `build/scripts/linux/build_afternightengine.sh`; both take `release|debug` and `--with-tests`. An engine-only change does not need a full-product build: the product graph is unaffected, and `check_build_graph.py` fails the change when that stops holding.

## The product on the engine

How each area of the product uses the engine - the viewer, the history, files, the plate solve, the processes, the ImageIntegration workflow, the application and the extension hosts - and why, is `docs/dev/guides/PRODUCT_ARCHITECTURE.md`. These are the rules every area keeps.

### Libraries and boundaries

- **The logger, its configuration and the crash handler belong to `AfterNightPlatform`, not to an engine.** `src/AfterNightPlatform` is a Qt-free, engine-free shared library in `namespace an::platform`, included as `<AfterNightPlatform/Name.h>`, with its own `.clang-tidy` for the naming table; `Test_Platform` is its suite. The Lite host and the protocol library do not link it, and `check_build_graph.py` holds both directions. Its `Text.h`, `FileSystem.h` and `SystemMemory.h` are the text, path and memory helpers the product uses. A file digest is `Sha256.h`'s `sha256OfFile` or `Sha256`, which use the x86 SHA instructions where the processor has them - not `QCryptographicHash`, a sixth of their speed over a master or a catalog.
- **A consumer owns its engine through `AfterNightConsumerRuntime`.** `src/AfterNightConsumerRuntime` is a Qt-free, Python-free shared library in `namespace an::consumer`, linking the engine's published surface and the platform library and nothing else, with its own `.clang-tidy`; `Test_ConsumerRuntime` is its suite. `EngineSession` owns the one engine context and admits every operation: hand a session to whatever needs one - there is no default and no locator - take a `SessionHold` for work that runs no operation, and run work in an `OperationScope` from `admit` (on a worker; it waits in a bounded queue) or `tryAdmit` (on any thread; it never waits). End a scope before the next operation, pass `execution()` down to nested work, and never call a helper that admits its own operation from inside an admitted scope. Keep images as `ImageAsset`s, read and written only under an `AssetReadLease` or the `AssetWriteLease`; key derived results on an `AssetRevision`; reserve consumer memory from the session's `ConsumerMemory` before allocating it. Settings are requested and take effect at the next construction. Never build an `ExecutionContext` on a hold's context directly: the session's admission is only as good as its use.
- **No library includes one above it.** `AfterNightControls` includes, links and builds after nothing of `AfterNightProcesses`, the application or the full host, and `AfterNightProcesses` nothing of the last two. `check_consumer_layering.py` reads this from the sources and `check_build_graph.py` from the generated projects. A helper both libraries need lives in Controls, beside `app/AppContextHelpers.h` and `app/ProcessControlEvent.h`.
- **Controls reaches the engine through its session bridge.** `src/AfterNightControls/session/`, in `namespace an::ui` with its own `.clang-tidy`: `IEngineSessionProvider` lends the process's session, and Controls never opens one or builds an engine context. A UI interface carries an `ImageDocument`, read under the current asset's lease and written through `ImageDocumentWriter`, whose `replace` is the only way a document's image changes shape or representation. `controls/IAppContext.h` declares the engine and bridge types it names rather than including them, so a unit that calls or implements such a member includes what it uses.
- **One process, one session.** The application owns its session (`ApplicationSession`, opened by `AfterNightApp::initialize` once the settings store has been read and before anything reads an image), and nothing else in the application opens one. The full host opens none: it borrows the runtime backend's through the backend's session capsule before any native window exists. The runtime backend's module seat owns the backend's, and it refuses to shut down while a capsule, an asset, an operation or a hold remains. Teardown runs in a stated order - admission closed, windows destroyed, the thread pool drained and what it posted back delivered, the session shut down and checked, then the logger and the crash handler (`shutDownApplication`, `shutDownHostRuntime`).
- **Every object with process lifetime in the product is registered.** `check_consumer_state.py` holds the product's libraries and executables to `build/scripts/tools/consumer_state.txt`: a new static, namespace-scope object or singleton needs an entry naming its owner, lifetime, synchronization and reason, and no entry may hold an engine image or context except the runtime backend's module seat.
- **New application logic goes in a coordinator or a service**, under `src/AfterNightControls/app/`, not in `AfterNightApp.cpp`.

### Threads, requests and publication

- **Work runs on the thread pool and lands on the GUI thread.** A read, an edit, a write, a presentation, a histogram count, a measurement or a star detection runs on a worker and publishes on the GUI thread. The GUI thread never waits for a worker, an admission or the history. A file workflow asks its questions and makes its views on the GUI thread, and none of the application's entry points makes a worker wait for the GUI thread; a window closes without waiting for its work.
- **A result is published only while it is still wanted.** A request takes a `CommitTicket` from a `RequestTracker` (`issueUnbound()` for a file read), and its result is shown only when `CommitTicket::verdict` on the GUI thread says `Commit`; a newer request supersedes and stops the one before. A preview drains to the latest request, and a preview never holds an Apply back.
- **A run is issued from a snapshot taken on the GUI thread.** Its parameters, the selected channel, the modulation, the mask, the preview area and every other image it reads are captured when it is issued - the other images leased before the run is admitted, one lease each in order of identity (`session/ImageReads.h`) - and a body reads no widget. Background work a dialog starts takes its read lease before its admission (`admitDialogWork`), because an Apply in place holds the writer while it waits to be admitted.
- **Record an edit before the view shows it, and never write the shown image in place.** An edit of a view goes through `ImageViewControl::editImage`, whose worker runs the history's `editPreparation` - the state it replaces written, room for its copy admitted - before the GUI thread records it with `registerEditBeforePublishing`, and the edit is shown only when that returns true. `registerShownState` is for a state a process already showed. A history step is a copy taken under a lease and admitted against the session's pending-snapshot allowance before it is made; a restore is a new asset. Apply Stretch (`controls/AutostretchApply.h`) is the model. Other threads read the shown image, so it is never written in place.
- **A sequence is processed under one admission and published whole.** Never admit again inside a frame loop. A sequence run publishes a whole sequence or nothing, and a sequence writer publishes its container only when `finish` succeeds.
- **Identity is a revision, never bytes or pointers.** Know an image by its `AssetRevision`: never hash pixel bytes or compare image pointers to decide whether it changed, and apply an answer computed from one revision only while the document still holds that revision.
- **Every failure reaches the user by name.** A failure is said in a message titled as the action or process and written to the log - never swallowed by an empty `catch` - and only a stop the user asked for is quiet. An input a process cannot take (a mosaic a container cannot hold, a grey image given to a colour-only process, an image below a tool's minimum size) is refused by name before anything is written.
- **A process works in the units the product has always used.** An integer image is read as fractions of full scale - an unsigned one from zero, a signed one counted from its smallest value, in Float64 for the 32-bit integers - and the result is narrowed back to the nearest code in the image's representation, keeping its validity and cards. A result whose transfer is no longer linear is recorded nonlinear in its processing-state cards and lineage.

### Tests of the product

- **Every registered action is tested through its body.** A new registered or menu action, or a process that changes framework, arrives with a case that runs its body through its window as the menu runs it: its family's suite (`Test_Processes<Family>`), `ProcessRegistryTest`, which launches every registered action for real, and `ApplicationMenu` in `Test_AfterNightApp`. Observe a launch through the window it opened (`openedProcessWindows` and `ClosesOpenedWindows` in `Test_ProcessesShared/ProcessesSharedTestSupport.h`).
- **A suite links the library it tests.** Every Controls and Processes suite compiles its own cases and links its library, so a class or free function a case calls carries `AFTERNIGHTCONTROLS_API` or `AFTERNIGHTPROCESSES_API`, and a suite's list in `build/scripts/tools/isolated_sources.txt` names no production unit and no stand-in. `Test_ControlsSeams` is the one exception: the freed-memory quarantine sees only the classes compiled into its executable, so the close-during-work cases compile the closures they exercise there, and every other Controls case goes in the suite of what it tests.
- **A test image is built from the neutral fixtures.** `src/AfterNightTests/fixtures/common/` describes an image in stored units, writes and reads neutral FITS files and states independent statistics, linking no engine; `fixtures/engine/` turns a description into the engine's image (`FixtureImage`, `fixtures/TestImageGenerators.h`); `fixtures/harness/` owns the storage root, the temporary directory, the logger, the crash handler and, in `QtTestHarness.h`, a headless `QApplication`; `Test_FixtureNeutral` and `Test_FixtureEngine` hold the layers. The expectations under `fixtures/common/expectations/` are recorded reference expectations that cannot be captured again: a checked-in record is never regenerated, and a description that changes gets a new record of its own. See "The fixture layers" in `docs/dev/guides/TESTING.md`.
- **Drive the application through its own window.** `Test_AfterNightApp/ApplicationDriving.h`'s `RealWindowCase` makes the main window as the application does; a menu action is triggered by its object name, a modal dialog is answered from the event loop it runs (`answerWhenShown`), and a file is chosen by typing its name into the application's own file dialog. Both runners set `AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST=1`, so a case that starts the full extension host fails rather than skips when the host is not deployed beside the suites.
- **The engine's campaigns reach the product.** `Test_ConsumerFaults` sweeps the engine's fault harness over the product's paths, held to `fault_points.txt` beside `Test_EngineFaults` (`suite=` names the executable); `Fuzz_ConsumerDocuments` and `Fuzz_ConsumerProtocol` read the integration's documents and the extension protocol; and an `AFTERNIGHT_SANITIZE` tree instruments every consumer library and suite. A product reader reads a CBOR value only once Qt reports no parse error, and the protocol refuses a map that repeats a key. A project that links a static library built without the sanitizer - the platform library, Crashpad's - states `applyEngineSanitizer({ unannotatedContainers = true })` with its reason. ThreadSanitizer runs the Qt-free suites; a lifetime it cannot schedule has a deterministic case that orders its threads with latches, never sleeps.

### Targets and the registry

- **What is generated is a registry.** `build/scripts/tools/consumer_targets.txt` declares every project the Premake definitions generate: its engine class (`none`, or `engine` when its link closure holds the engine), its state (`active` or `opt-in`), what it links and compiles, its fixture and the package profiles that ship it. `build/premake/afternight-targets.lua` reads it: a project with no record stops generation, and an opt-in target is generated only when asked for. A new project gets its record, and both runners' suite lists (`run_tests.bat`, `run_tests.sh`) its entry, in the change that adds it. An isolated test executable (`addIsolatedTestProject`) compiles exactly its list in `build/scripts/tools/isolated_sources.txt`, so a new file in one needs its entry there too. `consumer_targets.py check` holds the tree and the runners to the registry, `check_installer_payload.py` derives both packagers' lists from `package_profiles.txt`, and `build_active_targets` and `deploy_active_targets` under `build/scripts/msvc` and `build/scripts/linux` build and deploy the active set.
- **The product is measured through its own workflow.** `AfterNightWorkflowDriver` runs the ImageIntegration workflow over the pinned reference set in one engine session, through the product's own import and the shared preparation in `processes/ImageIntegration/workflow/WorkflowPreparation.h`; a front end that prepares a workflow calls that preparation rather than writing its own. `build/scripts/tools/product_measurement.py` archives a deployment, runs the paired protocol and compares outputs and timings, and `--path application` drives each deployment's own windows instead; `product_measurement.py check` holds `build/scripts/tools/product_measurement.json`.

## Critical build rules (mandatory order)

Always follow the numbered script sequence for the target platform. The invariant order is:
1) Create project files (Premake5)
2) Install dependencies (Qt, toolchains, packages)
3) Build 3rd party libraries (cgmic)
4) Run the platform build/deploy phases exactly where its scripts place them:
   - Linux: build main projects (Engine, CLI, UI), then deploy dependencies (Qt, Python, DLLs/SOs) to `bin/*/<config>`
   - MSVC: deploy external/vcpkg dependencies with `04*`, then build AfterNight with `05*`
5) Run acceptance validation from the deployed `bin/*/<config>` tree when runtime checks are required

Never attempt any main-project build before dependency installation. Do not run Make/MSBuild directly unless the generator step has been done.

MSVC script naming currently uses `04*-deploy*` and `05*-build_afternight*`. This is intentional; follow the script order exactly as provided in `build/scripts/msvc`.

## Platform build playbooks

### Windows – MSVC (Developer Command Prompt required)

Run from a Visual Studio Developer Command Prompt (not PowerShell/CMD without VS env):

```cmd
cd build\scripts\msvc
01-create_project_files.bat
02-install_msvc_deps.bat          REM Can take 30–60 minutes (vcpkg from source)
03a-build_3rdparty_libs_release.bat
04a-deploy_deps_release.bat        REM Copies vcpkg DLLs to bin/msvc/release
05a-build_afternight_release.bat
```

For debug builds, use the corresponding `b` scripts if present (e.g., `03b`, `04b`, `05b`). `build_active_targets.bat release --tests` builds the whole active set, suites included.

### Linux – Clang

```bash
cd build/scripts/linux
./01-create_project_files.sh
./02-install_linux_deps.sh         # Installs apt packages + Qt + Python
./03a-build_3rdparty_libs_release.sh
./04a-build_afternight_release.sh
./05a-deploy_all_deps_release.sh
```

Notes:
- Scripts already choose `-j` parallelism conservatively.
- Full first builds can take 20–60 minutes depending on platform.

## Running and testing

- Binaries are placed under `bin/<toolchain>/<config>/`, where the toolchain is `msvc` or `clang`.
- UI app:
  - Linux: use `run_AfterNight.sh` to ensure library paths are set
  - Windows: launch `AfterNight.exe` directly from `bin/msvc/release`
- Command-line tool: `AfterNightCLI[.exe] [options] <input> <output>`; `--help` prints its options and exit codes.

### Unit tests

Tests live under `src/AfterNightTests/`, one directory and one `Test_*` executable per area: the engine's direct suites (`Test_EngineCore`, `Test_EngineImage`, `Test_EngineRuntime`, …), the platform and consumer runtime suites, the Controls suites (`Test_Controls*`), the process families (`Test_Processes*`), `Test_ImageIntegration`, `Test_ProcessFramework`, `Test_AfterNightApp`, the extension host and runtime backend suites, and the fixture executables.

- Linux:
  ```bash
  cd build/scripts/linux
  ./08a-build_afternight_tests_release.sh
  ./08b-build_afternight_tests_debug.sh
  ./run_tests.sh
  ```
- Windows MSVC (Developer Command Prompt):
  ```cmd
  cd build\scripts\msvc
  09a-build_afternight_tests_release.bat
  09b-build_afternight_tests_debug.bat
  run_tests.bat
  ```

Run individual tests from `bin/<toolchain>/<config>/` with `Test_*` naming, with `AFTERNIGHT_TEST_STORAGE_ROOT` set to a throwaway directory (see "Test and temporary data isolation" below); the runners create a unique one per executable.

## Day-to-day changes

- Modifying C++ sources:
  1. Edit files under `src/`
  2. Run the mandatory clang-format validation for both Linux/CI and MSVC when the shell/tooling is available (see below)
  3. Re-run only the build step for your platform (`05a-*` on MSVC, `04a-*` on Linux)
  4. Test by running binaries from `bin/*/release` (or `debug`)

- Adding new source files:
  1. Place `.cpp`/`.h` in the appropriate `src/` subtree
  2. Premake globs already include new files (e.g., Engine: `src/AfterNightEngine/**`); an isolated test executable compiles only its list in `build/scripts/tools/isolated_sources.txt`, so a new file in one needs its entry there
  3. Re-run `01-create_project_files.*` to regenerate
  4. Build as normal

- Changing build configuration:
  1. Edit `build/premake/afternight-*.lua`
  2. Re-run `01-create_project_files.*`
  3. Do a clean build then rebuild (use provided clean/build scripts)

- Updating Qt:
  1. Update `QT_VERSION` in `build/premake/afternight-common.lua`; this is the source of truth for scripts and docs
  2. Re-run dependency install script for your platform
  3. Regenerate projects and rebuild all

## Coding standards and style (C++20)

Follow the repository’s `.clang-format`, `.editorconfig`, and `.clang-tidy`. Key conventions:
- Formatting workflow for agents and contributors:
  - **Mandatory for every assigned code task:** any agent that changes C/C++ files under `src/` must run clang-format validation before final handoff. Do this even for tiny edits and test-only changes.
  - Prefer the repository helpers over ad-hoc `clang-format` invocations. Linux/CI checks and formats complete changed C/C++ files so CRLF validation is deterministic; the MSVC helper applies equivalent changed-line validation while preserving CRLF.
  - Linux/CI authoritative check: `build/scripts/check_clang_format.sh origin/main`.
  - Linux/CI fix command: `build/scripts/format_changed.sh origin/main`.
  - On Linux, `build/scripts/linux/install_clang_format.sh` installs `clang-format-22` from apt.llvm.org. CI's format job calls it; run it once on a Linux machine that formats or checks changes.
  - `clang-format-22` is the pinned Linux/CI major version, chosen to match the `clang-format.exe` bundled with Visual Studio 2026 so Windows and Linux agree on line breaking. Formatter majors are not interchangeable: clang-format 18 and 22 produce different output for the same file, so a check that passes under one can fail under the other.
  - Version 22 is not a universal prerequisite for local validation. Use another available version when it is unavailable, and report the version used. Do not skip formatting validation merely because the pinned version is absent.
  - MSVC check: `build\scripts\msvc\check_clang_format.bat origin/main`.
  - MSVC fix command: `build\scripts\msvc\format_changed.bat origin/main`.
  - On Windows, the MSVC wrappers use the `clang-format.exe` and `git-clang-format` bundled with Visual Studio's **C++ Clang tools for Windows**, which is the version 22 that CI pins. They also support standalone LLVM and the `CLANG_FORMAT_BIN` override.
  - A successful check with another formatter version is valid local validation, but it is not a claim of exact Linux/CI version parity. If different versions disagree, the pinned CI result is authoritative; document the local version and discrepancy.
  - If `origin/main` is unavailable (shallow clone, detached checkout, fork, or alternate upstream), fetch it or pass an explicit merge-base/ref to the helper scripts. If no upstream base exists, let the helpers fall back to `HEAD~1` and document the base used in the final handoff.
  - The MSVC wrappers load the Visual Studio developer environment, locate `clang-format.exe`, and check or format only changed C/C++ lines under `src/`. Install Visual Studio’s “C++ Clang tools for Windows” component or standalone LLVM if they fail to locate the tool.
  - If the current machine cannot run one of the required platform checks, run every available check and explicitly document the missing shell/tooling in the final handoff. Never omit formatting validation silently.

### Line endings (mandatory)

- Every AfterNight-owned text file must use CRLF (`\r\n`), regardless of its format or the operating system on which it is created or edited. This includes C and C++ sources and headers, Python, JSON, Lua, batch files, Markdown, configuration files, scripts, and every other textual format.
- Linux-exclusive files may use LF (`\n`). Files under `build/scripts/linux/**` are Linux-exclusive, and shell scripts executed on Linux must use LF even when they live elsewhere in the repository. Current shared locations include `build/premake/configure_linux.sh`, `build/scripts/_build_user_guide_docs.sh`, `build/scripts/check_clang_format.sh`, `build/scripts/format_changed.sh`, `build/scripts/python/setup_python_linux.sh`, and `installer/linux_packager.sh`.
- Files under `3rdparty/**` and files owned by Git submodules are excluded from AfterNight's line-ending policy. Preserve their upstream line endings.
- Mixed line endings are forbidden. Before handoff, verify every newly created or edited AfterNight-owned text file and normalize it to the required line ending. Editing from Linux does not change the CRLF default.

- Prefer modern C++: range-for, `enum class`, `override/final`, `=delete`, `constexpr` where trivial
- Memory: avoid raw new/delete; use RAII and smart pointers
- Parameters: pass non-trivial types by `const&`; use `std::string_view`/`std::span` for non-owning
- Exceptions: avoid in hot paths; use consistent error handling per module
- `noexcept` when obvious; `[[nodiscard]]` for important return values
- Includes: `#pragma once`; include own header first, then STL, third-party, then project headers, grouped by blank lines; include what you use
- Naming:
  - **PascalCase is the repository convention for all new code.** AfterNightApp, AfterNightControls,
    AfterNightProcesses, and `AfterNightEngine` all use the same family:

    | Element | Form | Example |
    |---|---|---|
    | classes, structs, `enum class`, aliases, concepts | PascalCase | `ImageStorage`, `AnCardWidget` |
    | enumerators | PascalCase | `SampleType::Float32` |
    | methods and free functions | camelCase | `createImage`, `refreshTheme` |
    | data members | `m_` + camelCase | `m_headerLabel` |
    | locals and parameters | camelCase | `rowBytes` |
    | compile-time constants | `k` + PascalCase | `kMaxImageBytes` |
    | namespaces | lowercase | `an`, `an::ui`, `an::ui::processes` |
    | macros | UPPER_SNAKE | `AFTERNIGHT_API` |
    | files | PascalCase after the primary type | `ImageStorage.h`, `AnCardWidget.cpp` |

  - **No snake_case public API is added anywhere in the repository.**
  - Vendored and ported code under `src/**/internal/` and `3rdparty/` keeps its upstream style.
  - The table is enforced rather than reviewed where a library carries its own `.clang-tidy` - `AfterNightEngine`, its adapters and the opt-in backend, `AfterNightPlatform`, `AfterNightConsumerRuntime`, `AfterNightCLI` and Controls' `session/` - which configures `readability-identifier-naming` to exactly the table above. Run the local static analysis over every changed source and header with the clang-tidy Visual Studio bundles, or `clang-tidy-22` from `build/scripts/linux/install_clang_tidy.sh`:

    ```
    clang-tidy --config-file=src/AfterNightEngine/.clang-tidy <changed sources and headers> --
        -std=c++20 -x c++ -Isrc/AfterNightEngine/include -Isrc/AfterNightEngine/src
    ```

    Name the headers as well as the sources, which is what `-x c++` is for. Much of the engine is
    header-only, and a header no translation unit includes is never parsed: reached through sources
    alone, a name that breaks the table above passes silently.

- **No planning-artifact references in code.** A reference to a unit of *work* must not appear
  anywhere under `src/` or in the Premake definitions. This covers comments and test failure
  messages, not just identifiers. The banned forms include:
  - Work-unit IDs of any shape: `Task 1.3`, `Phase 2`, `Plan 3`, `Step 4 of the redesign`,
    `Stage 2 of the rollout`, `Milestone 2`, `Iteration 5`, `Sprint 14`, `Wave 1`, `Round 2`,
    `Epic 7`, `Workstream B`, `Backlog item 12`.
  - Document coordinates: `section 10.7`, `§8.13`, `Appendix C`, `Table 4`, `step 3 of the plan`,
    and plan document filenames.
  - Registry IDs owned by a planning document: contract IDs (`C06`), decision and ADR IDs
    (`decision 3.5.18`, `ADR-12`), requirement IDs (`REQ-7`).
  - External tracker handles: issue and PR numbers, ticket keys, branch names.
  - **The ban is on the reference, not on the vocabulary.** `LightsStackingStage`, "the vertical
    phase streams the image once", `// Step 1: log in` narrating a function's own algorithm, and a
    loop "iteration" all describe the code and are fine. "Removed as part of Task 4.2" and
    "deferred to Phase 3" describe a schedule and are not. The test: **if archiving the planning
    document would make the sentence meaningless, it does not belong in the code.**
  - They name the process that produced the code, not the code. Plans get archived, sections get
    renumbered, and work units close, so the reference decays into a citation of nothing while
    still reading as authoritative to the next person.
  - Write the reason instead. "A fast-math build invalidates the numeric contract" survives the
    document that first decided it and can be acted on by a reader who has never seen that
    document; "contract C06 prohibits this" cannot.
  - **Forward references are the same defect.** Say what is missing, not which work unit will add
    it: "the deterministic generators are implemented separately", never "implemented in Phase 6".
  - The only exemption is temporary scaffolding that never reaches `main` — a `TODO` on a working
    branch, a scratch file. Anything merged is permanent and must stand on its own.
  - Tooling whose *input* is a planning document (`build/scripts/tools/`) necessarily names the
    sections it parses. That is a dependency on an input format, not a citation, and it lives
    outside `src/`.
  - Traceability belongs in the commit message and `docs/dev/` — both are versioned alongside the
    plan and age with it.
- Structure: prefer early returns; use `switch` over long `if/else`; prefer standard algorithms; use structured bindings and `auto` judiciously; keep functions short
- Implementation details in `.cpp` should use anonymous namespaces (avoid `static` globals)
- Namespace convention: new framework/control classes go in `namespace an::ui`; new process UI-builder structs go in `namespace an::ui::processes`. Do **not** retroactively namespace existing classes — only new and extracted code.
- Native application controls created to serve extension process UIs must still be generic AfterNight controls. Do not name app-side classes, ParamDef types, object names, files, or helpers after a specific extension or process; make the control reusable and let extensions provide process-specific labels/configuration through schema.
- The `TypedProcessControl<ParamsT>` template (in `controls/TypedProcessControl.h`) is the required base for all processes that use execution parameters. Inherit from it instead of `ImageRTPreviewControl` directly.
- **Windows MIN/MAX macro conflict**: always write `(std::max)(...)` and `(std::min)(...)` (with the extra parentheses around the function name) instead of `std::max(...)` / `std::min(...)`. This prevents the Windows SDK `min`/`max` macros from incorrectly expanding the calls. `SourceGuards.NoRawStdMinMaxCallsInSrc` in `Test_ImageIntegration` enforces it across every C and C++ file under `src/`, so a change anywhere under `src/` runs that case (`Test_ImageIntegration --gtest_filter=SourceGuards.*`) whatever other suites it affects.

### Licence headers (mandatory)

- Every source file AfterNight owns - `.h`, `.hpp`, `.cpp`, `.c`, `.inl` and `.py` - states its licence and its copyright in two SPDX lines before its first line of code. A new file opens with them:

  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  // SPDX-FileCopyrightText: <year> <author>
  ```

  Python writes them with `#`, after a shebang or an encoding line where the file has one. `3rdparty/` and the submodules keep their upstream notices and are not held to this.
- The licence is `GPL-3.0-or-later`, except on the extension protocol boundary, which is `Apache-2.0` and is exactly `src/AfterNightExtensionProtocol/`, `src/AfterNightExtensionHostLite/` and the protocol half of `python/modules/afternight/`: `__init__.py`, `lite.py`, `protocol.py`, `sequences.py` and `ui_protocol.py`. Nothing under the GPL is included, imported or derived there.
- A file derived from another project keeps its upstream notice where it is and states the licence the derived work is distributed under - the upstream's, such as `GPL-3.0-only` for a work licensed under version 3 alone, or an expression such as `GPL-3.0-or-later AND BSD-3-Clause` when the file joins AfterNight's code to permissively licensed code. The SPDX lines follow the notice when the file opens with one.
- `check_license_headers.py`, in the common check list, holds every file to its header and its licence; `--apply` inserts a missing header and refuses to change anything else in the file.

### Qt specifics

- Use Verdigris macros (`W_OBJECT`, `W_SIGNAL`, `W_SLOT`, …) instead of `Q_OBJECT`
- Prefer new signal/slot syntax (functor or pointer-to-member)
- Use `QStringLiteral` for string constants; `tr()` for user-facing text
- Do not block the UI thread; use signals/slots, `QFuture`/QtConcurrent, or worker threads
- Follow Qt ownership patterns (`QPointer`, `QObject` parents) consistent with surrounding code

### Extension host responsiveness

- `AfterNightExtensionHost` is heartbeat-supervised by the main app. Never block its main Qt event loop from native extension-window UI callbacks.
- Avoid `QDialog::exec()`, static `QFileDialog::get*()` helpers, long CPU/IO work, and synchronous waits that do not continue servicing the control channel.
- Use async dialogs (`open()` plus `finished`/`accepted`/`rejected` signals), worker threads, helper processes, or the normal execute/preview pipeline so open dialogs and slow operations do not trigger missed heartbeat warnings.

### UI theming and styling

All visual styling is governed by the **theme system** documented in `docs/dev/THEME_SYSTEM.md`. Read that file before creating or modifying any UI widget. Key rules (enforced project-wide):

- **No QSS.** `setStyleSheet()` is forbidden everywhere — no exceptions.
- **No hardcoded colors, spacing, radii, or fonts.** Always resolve values through `an::ui::tokens()` (design tokens singleton).
- Colors: `tokens().colors.*`, Spacing: `tokens().spacing.*`, Radii: `tokens().radius.*`, Fonts: `tokens().typography.*`.
- Standard Qt widgets are styled via `AnProxyStyle` (`theme/AnProxyStyle.h/.cpp`); do not override their painting in widget code.
- Custom/composite widgets use `paintEvent()` with design tokens — call `an::ui::tokens()` inside `paintEvent()` (never cache in constructor) so theme switches apply automatically.
- Use `AnimationUtils::duration()` for every animation duration; never hardcode.
- Use dynamic properties (`an` prefix + PascalCase, e.g. `anButtonVariant`, `anFrameRadius`) for visual variants instead of subclassing.
- Reuse existing components from `src/AfterNightControls/components/` (`AnCardWidget`, `AnInfoBanner`, `AnCollapsibleSection`, etc.) before building new ones.
- Responsiveness: use Qt layout managers exclusively; no absolute positioning; test at 1080p and 720p.

### Logging and errors

- Use the macros from `<AfterNightPlatform/Logging.h>` (`LOG_ERROR`, `LOG_WARNING`, `LOG_INFO`, `LOG_DEBUG`, `LOG_TRACE`); control the logger through its `an::platform::` functions
- Do not print to stdout/stderr directly
- Keep error returns consistent with the module; avoid exceptions in hot paths

### Dependencies

- Prefer existing linked libraries before adding new ones
- If you must add a library, justify it in code comments/PR and update platform-specific install scripts and premake accordingly; regenerate, rebuild 3rdparty, rebuild main, deploy
- Keep dependency surface minimal; prefer permissive licenses
- A library the engine uses goes on `build/scripts/tools/dependencies.txt` and, once it ships, on `safety_register.txt` (see "Checks, campaigns and registers")

### Credit for derived code (mandatory)

AfterNight honours the work it builds on, whatever its licence. Code **derived** from another project carries
that project's credit and licence in the file itself.
Derived means copied, translated from another language (a Python or PixInsight script, a C library), or written by
following its source closely - however much the code was restructured. A method re-implemented from a publication
or a specification alone is not derived code; cite the publication anyway.

Before porting anything:

- Identify the exact upstream: project, files, version or commit, authors, the licence the *file* declares
  (a file-level SPDX line that disagrees with the repository's licence file is taken as controlling, and both
  are recorded) and the hashes of the snapshot you read.
- Check compatibility with AfterNight's GPL-3.0-or-later. GPL-3.0, GPL-3.0-or-later, LGPL and permissive licences
  are fine. GPL-2.0-only, and anything with an extra restriction (non-commercial, no derivatives, one host
  application only), is not: stop and ask the maintainer. Source with no licence text grants nothing - re-derive
  the method from its publication instead (the `da3d-guided-shrinkage` record in
  `build/scripts/tools/dependencies.txt` is the model).
- Nothing derived from GPL code may reach the Apache-2.0 boundary: `AfterNightExtensionProtocol`,
  `AfterNightExtensionHostLite` and the protocol-only SDK modules.
- Never carry upstream assets or host APIs: icons, bitmaps, UI layouts, generated resources, PixInsight/PJSR or
  Pleiades interfaces. Draw native controls through the theme system.

Every derived file - header **and** implementation - says, in its module comment (the `/// @file` block of a
published header, so the credit reaches the generated reference):

- the upstream work and its version, its project and where to find it (a URL);
- the authors, and the upstream copyright line **verbatim** (`Copyright (C) 2023, 2024 Mike Cranfield`) - never
  paraphrased, never dropped;
- the licence as upstream states it, and that the file is a derived work distributed under that licence;
- what is upstream's (the method, the formulas, the constants) and what is AfterNight's (the restatement, the
  validity handling, the scheduling), so a reader can tell them apart;
- where the provenance note is.

**A notice travels with the logic, not with the file.** When derived code is moved, split, merged into a shared
helper or rewritten, every file that now carries the derived logic carries the notice; a rewrite that keeps the
formulas is still a derived work.

Beside the file:

- **A provenance note**, `docs/dev/<NAME>_PROVENANCE.md`: the snapshot (location, version or commit, hashes), the
  authors, the licence, what was translated and what was written independently.
  `docs/dev/AUTO_GRADIENT_REMOVAL_PROVENANCE.md` and `docs/dev/NBCOLORMAPPER_PROVENANCE.md` are the models.
- **For the engine, its adapters and the opt-in backend, a `ported` record** in
  `build/scripts/tools/dependencies.txt`. Its `marker` is the upstream copyright line, or the authors' names when
  upstream states no copyright line; its `relation` is `adapted` when code or translated logic is carried and
  `re-derived` only when nothing but a publication was used; an `adapted` record joins the pinned set in
  `check_dependency_provenance_test.py`. `check_dependency_provenance.py`, in the common check list, then fails a
  listed file that loses the marker, and any engine file carrying a licence identifier or a dated copyright line
  that no record claims.
- **User-visible credit.** A derived process gets a paragraph in the About dialog's licence page
  (`AboutDialog::getLicenseText`, `src/AfterNightControls/controls/AboutDialog.cpp`): it is a derived work of the
  upstream work by its authors, the copyright line, the licence, and that it is distributed under that licence.
  Extend `Test_ControlsDialogs`' `TheLicenceCreditsEachDerivedProcessAndDescribesTheOriginalOneAsItIsNow` for it.
  Its user-guide page's references section names the upstream work and author as well.
- A process window built on derived logic credits the upstream work in its header too.
- Original code that resembles a known project says that it is original and what it does not include
  (`docs/dev/NBRGB_COMBINATION_PROVENANCE.md` and the About dialog's NBRGB paragraph), and keeps saying only what
  is true when the code changes underneath it.

Credit is not a planning reference: licence identifiers, copyright lines, authors' and projects' names and
upstream URLs belong in `src/`, and the planning-artifact rule above does not apply to them.

### Performance and parallelism

- Minimize allocations/copies; reserve capacities; use cache-friendly layouts.
- In the engine, parallel work goes through `Runtime/TaskScheduler.h` under the operation's grant (see "Errors, state and execution"); in the product, heavy work runs as engine operations inside an admitted `OperationScope`. Avoid UI access from workers.
- A loop written for a compiler to keep in vector lanes is not a vectorized loop on MSVC: one host can lose what the other wins. Check a hot engine loop with `/Qvec-report:2`, and give one MSVC leaves scalar a compiled run in `SimdKernelTables.cpp`, held to the loop by a case on every table. A compiled run keeps its counters in its own locals and returns them; a lambda that takes them by reference beside a counter store leaves them in memory.
- A pointwise step the compiled run has no lane body for sends its whole program through the evaluator a pixel at a time: a new step kind gets a lane body in `SimdKernelTables.cpp` or a direct path in `PointwiseExecution.cpp`, held to the reference backend bit for bit.
- A validity rule the fast paths turn away costs every caller that states it. A scan that adds a fast path for the all-valid case states which rules it serves - a rule that keeps one closed interval reads as the value window it keeps (`foldIntervalRule`) - and a case holds a rule to the window or mask stating the same samples.
- A stage that walks its frames reads ahead and writes behind through `workflow/stages/StageFramePipeline.h` (`pipelineWidths`, `ReadAhead`, `WriteBehind`) rather than threads of its own.
- An engine context with no memory budget returns a block of a mebibyte or more to the platform on a thread of its own (`BudgetedMemoryResource::kDeferredReleaseBytes`): its counters move at once and the process's resident set a little later.
- See `docs/dev/guides/BACKENDS.md`.

### Tests

- Add/adjust Google Test cases for new logic under `src/AfterNightTests/`
- Keep tests short, deterministic, and focused (happy path plus a couple edge cases)
- A suite that replaces the global allocation functions must replace every form that can hand it a pointer - plain, sized, array **and nothrow** - because memory from `operator new(size, nothrow)` is released by the plain `operator delete`. A partial replacement is invisible on an ordinary build, where the library's nothrow form forwards to the replaced throwing one, and aborts under a sanitizer, which replaces the whole set itself. A form may be left alone only while both halves of its pair are left alone, as the aligned forms are. See `src/AfterNightTests/Test_EngineCore/AllocationCounter.cpp`
- A suite that replaces the global allocation functions cannot be built with ThreadSanitizer. That sanitizer's C++ runtime defines the same set, strongly, and is linked with `--whole-archive`, so the link fails on duplicate symbols; AddressSanitizer leaves the replacement to the program and is unaffected. Keep such a suite free of worker threads, so leaving it out of a ThreadSanitizer build costs no coverage, and put a threaded case in a suite without the counter. `build/scripts/tools/sanitizer_matrix.txt` states that exclusion and its reason, and `run_sanitizer_campaign.py --check` fails a suite that has left the matrix or a skip that has lost its reason
- A case that races on a value computed once per process only tests that while it is the first thing in its process to ask. Any earlier call - from an earlier case in the same binary, or from the case itself taking a reference before it starts its threads - settles the value on one thread, and the racing case then reads a constant and passes over the defect it is named after. Measured: with the immutable static behind `cpuCapabilities()` replaced by a flag beside a value, the whole suite passes and ThreadSanitizer reports nothing; the same case run in a process of its own reports the race. Take the reference after the threads have joined, gate them so they arrive together, and give the case a run of its own as the sanitizer matrix does; a row whose filter matched no test fails, because a clean line from a run of nothing is the failure mode of every sanitizer campaign
- ThreadSanitizer needs an instrumented oneTBB, which `build/scripts/linux/build_tsan_onetbb.sh` builds from the version the installed headers declare. The distribution's library is not instrumented, so the happens-before edges oneTBB's scheduler establishes are invisible and everything a parallel region hands between threads is reported as racy - 156 reports on this engine's first instrumented run, most of them naming its own files, none of them a defect. Do not answer that with a suppression file: ThreadSanitizer matches a suppression against every frame of a report, and an engine race inside a parallel loop body always has oneTBB frames below it, so a suppression wide enough to quiet the noise also hides what the job is for. The campaign refuses a suppression naming this engine outright, and the one suppression file, the leak configuration's `leak_suppressions.txt`, names only what CPython, NumPy, pybind11 and PySide6 keep past a finalized interpreter

#### Test and temporary data isolation (mandatory)

- Tests must never write test data or any temporary data into AfterNight's production storage tree. This includes `%LOCALAPPDATA%\AfterNight\` on Windows and the corresponding AfterNight application-data, cache, configuration, log, or state directories on Linux and macOS.
- The prohibition covers every artifact type, including databases, settings, catalogs, presets, caches, logs, shared buffers, extension environments, downloaded packages, workflow outputs, and intermediate files. Directories such as `%LOCALAPPDATA%\AfterNight\Test_ImageIntegration` and `%LOCALAPPDATA%\AfterNight\Test_ProcessFramework` must never be created.
- Every test executable or fixture that writes files must own a unique temporary root outside the production storage tree. Prefer `QTemporaryDir` for Qt tests and an RAII-managed directory below `std::filesystem::temp_directory_path()` for non-Qt tests. Do not use a shared fixed directory that can collide across parallel runs.
- Qt test executables that exercise application storage must set `AFTERNIGHT_TEST_STORAGE_ROOT` to their owned temporary root before constructing `QApplication`, application singletons, storage services, or child processes. Resolve application-local and cache paths through `app/ApplicationStorage.h`; do not call `QStandardPaths::AppLocalDataLocation`, `AppDataLocation`, or `CacheLocation` directly from code that can write test artifacts.
- The product's interface settings - window sizes, panel layouts, recent lists, preferences - are the INI file `an::ui::applicationSettings()` opens (`app/ApplicationStorage.h`), under the application's local data. Never construct a default `QSettings` or one named by organization and application in product code: that is the platform's native store, the registry on Windows, which no test storage root redirects.
- Redirect `QSettings` user-scope storage into the same owned temporary root. Any spawned extension host, runtime backend, Python process, or other child process must inherit the test-storage override or receive an explicit temporary path.
- Destroy applications, services, mapped buffers, databases, and child processes before removing the temporary root. Cleanup must be attempted on both successful and failed test runs, and the test executable must return failure if its owned temporary directory cannot be removed.
- Test teardown must never delete or sweep AfterNight's production storage tree. Cleanup targets must be resolved and verified as descendants of the test-owned temporary root before recursive removal.
- Add regression coverage for storage-path routing whenever a test gains a new persistent or temporary artifact. Validation should confirm that the resolved path is below the owned temporary root, the production AfterNight storage path was not created or modified, and no test-owned temporary directory remains after normal process exit.
- Retaining test artifacts for diagnostics must be explicit and opt-in. The retained location must remain outside the production AfterNight storage tree and must be reported clearly to the user or CI logs.

### TDD policy (mandatory for every new feature)

- Every new feature starts with at least one failing automated test that captures expected behavior.
- Follow Red -> Green -> Refactor:
  1. Write a failing test first.
  2. Implement the minimal production change to make it pass.
  3. Refactor while keeping all tests green.
- New features are not complete without tests in the same change.
- Every bug fix must include a regression test when technically feasible.
- If automation is temporarily infeasible (e.g., UI-only edge case), document why, add a follow-up test task before merge, and include another validation artifact where possible: a focused widget smoke test, screenshot/manual smoke notes, or a narrowly scoped regression note.

## Agent operating procedure (AOP)

- Planning:
  - Read the user request fully; extract explicit and implicit requirements
  - Maintain a compact TODO list; work one item at a time
- Editing:
  - Apply small, targeted patches; avoid reformatting unrelated code
  - Match existing naming and style; include what you use
  - Apply the mandatory line-ending policy above to every newly created or edited text file; never leave mixed line endings
  - Prefer internal helpers in anonymous namespaces; keep functions single-purpose
- Validation (green-before-done):
  - For every C/C++ code task, run clang-format validation before build/test handoff:
    - Linux/CI: `build/scripts/check_clang_format.sh origin/main`
    - MSVC: `build\scripts\msvc\check_clang_format.bat origin/main`
    - Fix with `build/scripts/format_changed.sh origin/main` or `build\scripts\msvc\format_changed.bat origin/main`, then re-run the check.
  - If `origin/main` is not resolvable, fetch it or pass the same explicit fallback base/ref to the check and fix helpers; document the base used.
  - Build/test the target platform(s) and configurations whose behavior could be affected; run both Release and Debug when code or build behavior changes and the required shell/tooling is available.
  - Run the checks of the common check list (`docs/dev/LOCAL_VALIDATION.md`) that the change could affect, and the Python test file paired with every changed script under `build/scripts/tools/`.
  - For documentation-only changes, run the relevant documentation validation when available; a C++ build is not required unless the docs change build scripts or generated inputs.
  - Run all unit tests; for UI changes, do a quick manual smoke test
  - Iterate up to three times on build/test failures; summarize root cause if still failing
- Safety:
  - Do not introduce new network calls or secrets
  - Follow the documented build order and platform shell requirements
- Documentation:
  - Update comments and minimal README snippets when behavior changes
  - A change to how an area works updates the guide or rule that describes it in the same change; a design's reasons are written in the guide for its area

## Common build issues and fixes

- "Qt not found": run the platform `02-install_*_deps.*` script to install the Qt version defined by `QT_VERSION` in `build/premake/afternight-common.lua` into `Qt6/`
- "Missing library: libAfterNightEngine.*": violated build order; ensure Engine is built before CLI/UI
- "make: *** No rule to make target": regenerate via `01-create_project_files.*`
- "cannot find -lQt6Core": run deploy step `05a-deploy_all_deps_*` to copy Qt libs into `bin`
- Python import errors at runtime: ensure deploy step copied Python runtime into `bin/*/release/PythonLib/`
- vcpkg slow on MSVC: `02-install_msvc_deps.bat` legitimately takes 30–60 minutes; don’t interrupt
- Crash or wrong values that appear when you change a struct and vanish when you revert it: suspect a stale object before suspecting the code. An interrupted MSVC build can leave objects compiled against an older header revision, mixing two layouts of one type into a single binary. Run `build\scripts\msvc\check_msbuild_dependency_tracking.bat --fix` (the `05*`/`09*` build scripts already do) and see `docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md`. Do not reach for ASan first: instrumenting forces the full rebuild that repairs the condition, so it reports nothing
- `check_build_graph.py` or `consumer_targets.py` refusing to answer with "no current generated tree": the generated projects are older than the Premake definitions or the registry; re-run `01-create_project_files.*`

## Crash dump analysis

AfterNight uses Crashpad on Linux and Windows MSVC builds. See `docs/dev/CRASH_SYMBOLICATION.md` for the full guide. Quick reference for agents:

### Dump locations

- Linux: `~/.local/share/AfterNight/CrashDumps/pending/*.dmp`
- Windows MSVC: `%LOCALAPPDATA%\AfterNight\CrashDumps\reports\*.dmp` (Crashpad's Windows database keeps its reports there; there is no `pending` folder)

### Linux — LLDB (recommended)

Release builds produce split debug sidecars (`*.debug`) next to the stripped binaries in `bin/clang/<config>/`. LLDB reads Crashpad minidumps natively:

```bash
# Find the latest dump
ls -t ~/.local/share/AfterNight/CrashDumps/pending/*.dmp | head -1

# Open it in LLDB (LLDB follows gnu-debuglink to .debug files automatically)
lldb -c <dump-path>.dmp bin/clang/release/AfterNight

# Inside LLDB:
(lldb) bt                  # backtrace of crashing thread
(lldb) bt all               # all threads
(lldb) frame select 2       # inspect a specific frame
(lldb) frame variable       # show locals
(lldb) source list          # show source around current frame
```

If `libAfterNightEngine.so` frames are unsymbolicated, add the module - and `libAfterNightPlatform.so`, which
holds the logger and the crash handler, when their frames are:
```
(lldb) target modules add bin/clang/release/libAfterNightEngine.so
(lldb) target modules add bin/clang/release/libAfterNightPlatform.so
```

### Windows — Visual Studio

Open the `.dmp` file in Visual Studio, click "Debug with Native Only", and point at the `bin\msvc\release\` folder containing the matching `.pdb` files. The `.pdb` files must come from the exact same build.

### Key rule

Debug symbols (`.debug` on Linux, `.pdb` on Windows) must match the exact binary that crashed. A different build — even without code changes — produces non-matching symbols.

## Directory quick reference

- Source: `src/<Project>/`, one directory per project of the module layering above; tests in `src/AfterNightTests/`, benchmarks in `src/AfterNightBench/`
- ImageIntegrationProcess: `src/AfterNightProcesses/processes/ImageIntegration/**`
- Engine: `src/AfterNightEngine/{include,src}` and its checks in `build/scripts/tools/`
- Build scripts: `build/scripts/*/*`
- Premake: `build/premake/*.lua`
- Generated projects: `build/make-*`, `build/vs2026-*`
- Output binaries: `bin/<toolchain>/<config>/`
- Third-party: `3rdparty/{src,lib,bin}`
- Developer documentation: `docs/dev/` (index: `docs/dev/README.md`); user guide: `docs/user-guide/`
- CI workflows: `.github/workflows/*.yml`

## Installer builds

- Windows (Inno Setup): ensure deploy has populated `bin/msvc/release`; build the user guide with `build\scripts\msvc\build_user_guide_docs.bat`; install Inno Setup 6.2+; run `iscc installer\win_installer.iss /DMSVC`. Local/manual builds bundle `docs/user-guide/site/` when it exists. Release builds must pass `/DREQUIRE_DOCS` so missing offline docs fail loudly. Use `/DADMIN=1` for elevation.
- Linux AppImage: complete release build and deploy; build the user guide with `build/scripts/linux/build_user_guide_docs.sh`; run `installer/linux_packager.sh` (downloads `appimagetool` if needed). Local/manual builds bundle `docs/user-guide/site/` when it exists. Release builds must run with `REQUIRE_DOCS_SITE=1` so missing offline docs fail loudly. Output: `installer/AfterNight-<version>-x86_64.AppImage`.

## User documentation system

End-user documentation lives in `docs/user-guide/` and is built with MkDocs Material.

### Structure
- Source: `docs/user-guide/docs/en/` (Markdown files)
- Config: `docs/user-guide/mkdocs.yml`
- Direct dependencies: `docs/user-guide/requirements.in`
- Locked dependencies: `docs/user-guide/requirements.txt`
- Output: `docs/user-guide/site/` (gitignored)

### Building locally

Prefer the platform helper so the locked documentation environment is reused:

```cmd
build\scripts\msvc\build_user_guide_docs.bat
```

```bash
build/scripts/linux/build_user_guide_docs.sh
```

Direct MkDocs commands are also valid after installing the locked dependencies:

```bash
pip install --require-hashes -r docs/user-guide/requirements.txt
mkdocs serve -f docs/user-guide/mkdocs.yml
mkdocs build -f docs/user-guide/mkdocs.yml --strict
```

### Adding/editing pages
- Edit Markdown files under `docs/user-guide/docs/en/`
- Add new pages to the `nav:` section in `docs/user-guide/mkdocs.yml`
- Place images in `docs/user-guide/docs/en/assets/images/`
- Follow the page templates under `docs/user-guide/templates/` and the authoring rules in `docs/dev/USER_GUIDE_AUTHORING.md`
- Update `docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md` whenever page status, reviewed sources, screenshot state, diagrams, or strict-build validation changes
- Track requested screenshots in `docs/user-guide/SCREENSHOT_REQUEST_MANIFEST.md`; commit real images only with matching entries in `docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md`

### CI
- A workflow that calls one reusable workflow more than once must key their concurrency groups apart. A reusable workflow inherits its caller's context, so two calls in one run see the same `github.workflow` and the same ref; with `cancel-in-progress` they share a group and the second cancels the first, taking the job that waited on it. The run then reports `cancelled` rather than `failure`, with no failing step anywhere, so the missing half reads as somebody pressing stop. `cache-linux.yml` and `cache-windows.yml` take a `variant` input for this; pass whatever distinguishes the calls. When a workflow runs several modes of one job, check that every mode reported rather than that nothing failed
- `.github/workflows/docs.yml` builds, validates, and optionally deploys documentation
- Release builds call the docs workflow once and pass the uploaded HTML artifact into platform packaging jobs
- Release installer jobs download the built HTML artifact for offline bundling
- Release installer jobs require the docs artifact (`/DREQUIRE_DOCS` on Windows, `REQUIRE_DOCS_SITE=1` on Linux)
- Manual/non-release installer builds bundle docs only when `docs/user-guide/site/` already exists
- GitHub Pages deployment is opt-in (`deploy_pages: true`), not automatic during release packaging

### In-app help
- `Help -> User Guide` opens bundled offline docs first from the installed app's docs location
- If bundled docs are unavailable, the app falls back to the published online guide
- Set `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE=1` for offline-policy smoke tests; with that policy enabled, missing bundled docs should show a clear warning instead of opening the web guide

### i18n (future)
- Current source is English-only at `docs/user-guide/docs/en/`
- Future translation support migrates `docs_dir` from `docs/en` to `docs` and uses directory-based `en/`, `es/`, `fr/`
- Plugin: `mkdocs-static-i18n` (listed but commented out in `requirements.in`)
- Untranslated pages fall back to English only after that future migration is implemented

## Acceptance checklist

- Build PASS on target platform(s) and configuration(s) affected by the change; run both Release and Debug where applicable and feasible
- Deploy step run; app/CLI start without missing libraries
- Unit tests PASS; no new compiler warnings introduced
- The checks of the common check list the change could affect PASS, or are named as unavailable
- Changes adhere to style and performance guidance; minimal diffs
- Code derived from another project carries its credit and licence in every file that holds the derived logic,
  with its provenance note, its `ported` record for engine code and its About dialog credit (see "Credit for
  derived code")
- Every new source file opens with its SPDX licence and copyright lines, and `check_license_headers.py` passes (see
  "Licence headers")

## ImageIntegrationProcess architecture snapshot (current)

Routing guidance:
- Use `src/AfterNightProcesses/processes/ImageIntegration/**` for maintained full ImageIntegrationProcess work: model, workflow, persistence, reporting, tabs, widgets, controls, and dialogs.
- Use `src/AfterNightProcesses/processes/ImageIntegrationLiteProcess.*` and `src/AfterNightProcesses/processes/ImageIntegration/lite/**` only for the simplified Lite front end, its launch behavior, or Lite-specific UX. Shared behavior should usually live in the full ImageIntegration model/workflow helpers and be reused by Lite.

- Entry/UI shell: `ImageIntegrationProcess.*` coordinates tabs, dialogs, and workflow execution.
- Domain model: `model/` owns frame metadata, grouping, validation, calibration matching, and integration errors.
- Workflow core: `workflow/` provides manifest generation/diffing, execution planning, progress/control, artifact tracking, and cancellation. A run's stages work in the session `WorkflowExecutionOptions::session` names, through the `WorkflowStageServices` it binds: each stage admits its operations at the run's width and changes nothing process-wide.
- Pipeline stages: `workflow/stages/` contains stage implementations (`Bias`, `Dark`, `Flat`, `LightsCalibration`, `LightsRegistration`, `LightsAlignment`, `LightsNormalization`, `LightsStacking`, `LightsDrizzle`).
- Reporting: `reporting/` generates the final HTML/Markdown workflow report via `WorkflowReportBuilder`.
- Persistence: `persistence/` manages SQLite-backed project state, autosave, presets, and schema. A store reads the schema it writes and refuses any other without writing to it.
- UI composition: `tabs/`, `widgets/`, `controls/`, `dialogs/`, and `ui_models/` encapsulate presentation and interaction logic.

### Artifact writing and incremental reuse

Read `docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md` before changing artifact validation, incremental reuse, or anything that writes a generated image. Two rules govern the whole scheme:

- **Generated files are complete by construction.** Every generated image goes through the engine's output transaction (`IO/OutputTransaction.h`): the payload is written to a private pending sibling, proved to hold every byte, and renamed into place only then, so a crash, a full disk, or a failing save leaves the previous output untouched instead of half-replaced; `sweepPendingOutputs` (`IO/PendingOutput.h`) reclaims what an interrupted run left behind. Never write a generated artifact directly to its destination name.
- **Reuse is decided from identity, not content.** Every generated image carries the manifest artifact identity it satisfies in its own `ANARTID` header card (`an::ArtifactIdentity`, `IO/ArtifactIdentity.h`), and `ArtifactTracker::isArtifactValid()` and the planner's `ExecutionPreparationSnapshot` answer from the platform file identity or that header — never by re-reading a payload; a row with no stamp to compare is recomputed. Content digests remain for the bounded non-image documents (normalization coefficients, drizzle descriptors, `workflow/DocumentPublication.h`) and for final-master archive provenance, taken once after the write commits; no reuse decision compares one.

When adding a stage that writes an artifact, publish it through `publishWorkflowImage` with the identity `workflowArtifactIdentity` makes of the manifest fingerprint (`workflow/WorkflowArtifactStamp.h`); the output transaction takes the identity it is given and withdraws any identity card the caller's intent carried, so a write never inherits its input's. A stage that changes what an artifact family computes moves that family's revision (`workflow/ArtifactSchema.h`), so artifacts computed before the change are recomputed once.

### SIP distortion correction

SIP (Simple Imaging Polynomial) distortion correction is handled by the engine when a frame's cards carry A/B/AP/BP polynomial coefficients:

- **Engine layer**: `parseWcsCards` (`Algorithms/Astrometry/WcsCards.h`) reads the cards into a `WcsSolution` whose `distortion` is a `SipDistortion` (`Algorithms/Astrometry/SipDistortion.h`) in engine pixel coordinates, the first pixel's centre at (0.5, 0.5); the forward polynomials undistort star positions before matching, and the inverse ones - or, when AP/BP are absent, a Newton solve of the forward model, the route reported with the answer - warp images during alignment. `Algorithms/Registration/RegistrationTransform.h` and `RegistrationPlan.h` take the source distortion beside the transform.
- **Integration points**: `workflow/stages/RegistrationFrames.h` probes each light's own solution and distortion, and `warpRegistrationFrame` undoes the distortion before sampling the transform; `planRegistrationOutputPlane` and `registrationOutputPlaneSolution` drop a distortion the alignment undoes from the plane's solution; the drizzle descriptors (`Algorithms/Drizzle/DrizzleMapDescriptor.h`) carry it into the drizzled stack.
- **Reporting**: the alignment stage counts the frames it corrected into `AlignmentGroupResult::sipCorrectedCount`, which flows through `WorkflowExecutor::Result` to `WorkflowReportBuilder`, rendering a SIP Distortion Correction callout in the final report when any frame had SIP correction.
- **Conventions**: a FITS solution counts pixels from one, with the first pixel's centre at one; the engine counts from zero, with that centre at one half. `parseWcsCards` subtracts the half pixel on the way in and the card writer adds it back on the way out, and nothing else moves it, so a reference pixel and the distortion that shares it are moved exactly once (`Algorithms/Astrometry/WcsCards.h`).

The maintained full implementation and the Lite front end coexist under `src/AfterNightProcesses/processes/`; route changes according to the guidance above.

## Error handling quick reference (ImageIntegrationProcess)

The ImageIntegrationProcess workflow uses structured error types defined in `model/IntegrationError.h`; a stage reports an engine failure through `integrationErrorFromEngine` (`model/IntegrationEngineError.h`):

**Error categories:**
- **IO errors (1xx):** `FileNotFound`, `FileNotReadable`, `FileNotWritable`, `DirectoryNotFound`, `DirectoryNotWritable`, `DiskFull`, `FileLocked`, `InvalidFileFormat`
- **Validation errors (2xx):** `MissingRequiredFrames`, `IncompatibleDimensions`, `IncompatibleChannels`, `InvalidConfiguration`, `EmptyFrameList`, `InsufficientFrames`, `InvalidOutputPath`
- **Processing errors (3xx):** `StackingFailed`, `RegistrationFailed`, `CalibrationFailed`, `NormalizationFailed`, `DrizzleFailed`, `MemoryExhausted`, `InternalError`
- **Cancellation (4xx):** `Cancelled`, `Timeout`

**Usage:**
```cpp
// Create error with factory method
IntegrationError error = IntegrationError::fileNotFound(path);

// Check error properties
if (error.isRecoverable()) { /* skip file, continue */ }
if (error.isCancellation()) { /* user cancelled */ }

// Format for display
QString logMsg = error.toLogString();   // For logging (includes error code)
QString userMsg = error.toUserString(); // For UI display (user-friendly)
```

**Recoverable errors:** `FileNotFound`, `FileNotReadable`, `InvalidFileFormat` allow workflow to continue with remaining files.

**Performance benchmarks (targets):**
- GroupingEngine: <10ms (100 frames), <100ms (1000 frames), <500ms (10000 frames)
- ManifestBuilder: <50ms (simple), <200ms (complex), <500ms (5000 lights)
- CalibrationMatcher: <50ms (100 groups), <200ms (500 groups)
- Model serialization: <500ms (10000 frames)

## Trust these instructions

These procedures have been validated against the existing build process. Prefer them over ad-hoc steps. If you encounter a gap, document it here for future updates.

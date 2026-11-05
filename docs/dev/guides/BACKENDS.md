# Backends

Where the engine's arithmetic actually runs: the scalar reference that defines every answer, the
compiled CPU kernels selected at run time, and the device seam a GPU backend implements. The rule
across all three is the same - which implementation ran is a question the engine answers, and a
faster path must agree with the reference inside a stated bound.

## Three levels, one answer

| Level | Where | What it is for |
|---|---|---|
| Reference | `src/AfterNightEngine/src/kernels/reference/` | The scalar definition of each kernel family's result. Every compiled or device path is tested against it. |
| CPU | `src/AfterNightEngine/src/backends/cpu/` | Compiled kernels, vectorized with Highway, behind a dispatch table resolved once per operation |
| Device | `Algorithms/Kernel/DeviceBackend.h`, implemented outside the engine | A checked seam for a separately compiled GPU backend |

A caller chooses with `ExecutionOptions`: `ComputeBackend` is `Automatic`, `Reference`, `Cpu` or `Gpu`,
and `BackendFallback` says whether an unavailable choice may fall back. Forcing `Reference` runs the
scalar kernels and leaving the choice automatic runs the compiled ones, which is what makes every
differential test in the direct suites a comparison between two implementations rather than one
implementation with itself. No build contains a `Gpu` backend today, so asking for one is refused or
unconstrained by the fallback rule; GPU work goes through the device seam below instead.

## The CPU

**The product floor is AVX2**, for the whole product and not only the engine: every engine-linked
target and the in-tree third-party projects are compiled for it, and the user guide's system
requirements state it. The engine reaches above it by dispatching at run time, from one binary,
between AVX2 and one AVX-512 target - Highway's AVX3, which the engine names
`InstructionSet::Avx512Skylake` - and a kernel opts into per-target compilation on measured benefit,
because each target costs object size and compile time in every unit that carries it. Highway marks
AVX-512 broken under MSVC, so a Windows build compiles the AVX2 rung only and a Clang build both;
`Runtime/BackendDispatch.h` reports what a build contains. The floor is not lower because the product
does not target older processors, and lowering it would mean recompiling every target and third-party
project for them; it is not higher because requiring AVX-512 would exclude current consumer
processors. Dispatching inside one binary costs no measurable time, survives a change of hardware, and
lets one process test each target against the other.

**Detection happens once and is immutable** (`Runtime/CpuCapabilities.h`). AVX and AVX-512 are
reported only when the operating system has enabled their register state as well as when the
processor advertises them, because an instruction set the OS does not save across a context switch
is one the engine cannot use. `InstructionSet` names the sets.

**Dispatch reports what it chose** (`Runtime/BackendDispatch.h`). It resolves an operation's backend
and instruction set and reports what the selected table's compiled code declares; after the call,
`ExecutionContext::backend()` says the same. Forcing an instruction set needs the host to run it *and*
the build to contain it. A request failing either is `ErrorCode::UnsupportedBackend` when no fallback
is permitted and quietly unconstrained when one is.

**How a compiled kernel is written.** A kernel family's per-row body is written once, in a shared
`.inl` included both inside Highway's per-target namespace and by the algorithm unit, so the compiled
and the fallback paths are the same source. The per-target units in `backends/cpu` fill the dispatch
tables (`backends/cpu/SimdKernelTables.h`, `backends/cpu/CpuKernelTable.h`); an algorithm never names
an instruction set. Each per-target unit has its own compile-time and object-size baseline in
`docs/dev/budgets.json`, measured by `check_budgets.py`, which is what keeps the per-target
multiplication visible. The kernels are written with Highway rather than left to the compiler's
auto-vectorization because MSVC leaves many of them scalar - one conversion measured eighteen times
slower than its Highway form - while Highway's code performs within a few per cent across compilers;
and of the libraries considered, it alone compiles per target for run-time dispatch and expresses
every kernel.

**Approximations have a named bound.** The transcendental functions a pointwise program can raise to
use one of the providers `MathProvider` names, and `mathUlpBound` in `Algorithms/Kernel/MathKernel.h`
states the error a caller accepts by using the vector tier. The tier accepts about five units in the
last place because a one-unit vector library is about three times slower, for accuracy that tone
curves and display transforms do not need; a power's error grows with its argument, so its bound is
derived from the argument rather than fixed.

## Kernel contracts

`Algorithms/Kernel/` publishes the contract every kernel family meets, whichever level implements it.
`Algorithms/Kernel/KernelContract.h` is the vocabulary: `KernelFamily` and `KernelOperation` say what
the kernel is; the alias, border, channel, stride, validity, scratch, cancellation and parallel
policies say what it accepts; `KernelAccuracyMetric` says how an answer is compared with the
reference. Each family has its own header - pointwise, reduction, histogram and order, stencil,
resample and sampling, gather and scatter, transfer - which declares its parameters and its scalar
reference entry point (`referenceSumKernel`, `referenceHistogramKernel`, `referenceResampleKernel`
and the rest), so a test or a backend compares against the same code the engine treats as the
definition. `Algorithms/Kernel/ReferenceKernel.h` holds the values the families share.

A **fused pointwise program** (`Algorithms/Pointwise/PointwiseProgram.h`) is how chains of per-sample
steps - scale, bias, clamp, gamma, curves, masks, blends - run as one pass over memory rather than
one pass per step. `Algorithms/Pointwise/PointwiseProgramKernel.h` reads a plan off a program so a
device can run the whole chain as one kernel.

## The device seam

A GPU is reached through a checked facade, and nothing crosses to it implicitly.

- **`Algorithms/Kernel/DeviceBackend.h`** is the seam a separately compiled backend implements: its
  capabilities as `KernelContract`s plus run-to-run determinism (`KernelDeterminism`), opaque
  allocations, host mappings, prepared kernels, and `DeviceWork` completed exactly once from any thread.
- **`Algorithms/Kernel/Device.h`** checks every submission before a backend sees it. A prepared kernel
  states the device memory it keeps between runs beside the scratch one run needs, and both count
  against the device's memory limit.
- **`Runtime/DeviceResource.h`** states residency - `Host`, `Pinned`, `Shared` or `Device` - and every
  crossing between them is a declared, counted transfer. A kernel binds shared or device memory only,
  and device memory is never mapped.
- **`Runtime/DeviceEvent.h`** is deferred completion. Work that reads what unfinished work writes, or
  writes what it reads, must depend on that work, or the submission is refused as
  `DeviceFailure::UndeclaredDependency`: no backend's queue order is relied on. A failed dependency is
  its dependent's cause, and falling back to the host is explicit and reported as a warning.
- **`Algorithms/Kernel/DeviceChain.h`** plans two or more device operations whose intermediate results
  never visit the host: the caller states which buffers are inputs, outputs or intermediates, and the
  plan answers with residency, uploads, downloads and the transfers the hand-overs avoid, before any
  memory is allocated.

`MockDeviceBackend`, in `Test_EngineKernels`, implements the seam in memory and is the contract's proof
in every build. Dependencies are declared rather than read from queue order because GPU interfaces
disagree about it - a CUDA stream keeps submission order and a SYCL queue promises none - and binding
host memory is refused rather than staged, because a planner that keeps data on the device has to be
able to prove that no hidden copy happened.

### The GPU proof

`src/AfterNightEngineOpenClBackend` implements the seam through OpenCL to prove it can be
implemented on a real device. It is opt-in and non-shipping: only a tree generated with
`AFTERNIGHT_OPENCL=1`, after `build\scripts\msvc\install_opencl_backend_deps.bat`, contains it or its
suite, `Test_EngineOpenClBackend`, which is the one target that may link it. No GPU SDK appears in either
dependency installer or reaches any other target, and its published surface names nothing of the SDK.
It lowers fused pointwise programs, a three-by-three stencil and chained residency, matching the host
reference bit for bit except at a power, which stays inside `mathUlpBound`.

The proof uses OpenCL because it builds from headers and an import library, compiles its kernels in
the driver and deploys nothing, where CUDA needs a multi-gigabyte toolkit and a redistributed
runtime: no shipping build uses a GPU, so the proof must leave no dependency behind. It is removable
by command, because a written list of its parts would go stale:
`python build/scripts/tools/remove_gpu_proof.py` checks that nothing outside its manifest names the
proof, and `--apply` deletes the backend, its suite, its installer and triplet and the checks that
guarded the opt-in, keeping the seam, `MockDeviceBackend` and the rule that no target may reach a GPU
SDK.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A GPU behind the device seam, through OpenCL.
///
/// This is a proof and nothing more: a real device implementing `Algorithms/Kernel/DeviceBackend.h`,
/// so that the contract a mock satisfies is shown to be one a GPU satisfies too. It is non-shipping,
/// opt-in, off by default and built on Windows only. A tree generated without AFTERNIGHT_OPENCL=1
/// contains no project for it, no installer carries it, and deleting it leaves the engine exactly as
/// it was - the seam it implements is the part that stays.
///
/// Nothing of OpenCL crosses this header. A caller sees the engine's own vocabulary - a `DeviceBackend`
/// to hand to `Device::create` - and a report of what the operating system's OpenCL loader can reach,
/// stated in plain values, so it can decide whether to create a backend without compiling against an
/// SDK.
///
/// The runtime is the one the graphics driver installed. This library imports the loader lazily and
/// loads it from the operating system's directory before it calls into it, so on a machine with no
/// OpenCL at all it still loads, reports that, and refuses to create a backend rather than failing to
/// start.
///
/// What a device over this backend offers:
///
/// - host, pinned and device memory. Pinned memory is a page-locked host buffer the driver transfers to
///   and from without staging it. No shared memory is offered: a discrete card has no memory both sides
///   address, and a backend that emulated it would hide exactly the copies the contract makes visible;
/// - three kernels, all narrowed to all-valid input and all needing the device's double precision.
///   One is the affine pointwise operation `Algorithms/Kernel/PointwiseKernel.h` defines, over one
///   Float32 plane whose rows may be padded. It computes each sample through Float64 in separate
///   statements with contraction switched off, which is the scalar reference's arithmetic, so the
///   result is the reference's bit for bit. Another is a fused pointwise program,
///   `Algorithms/Pointwise/PointwiseProgramKernel.h`, lowered into one OpenCL kernel when it is prepared:
///   scale, bias, clamp, gamma, mask, blend and sampled-curve steps over Float32 planar images, reproducing
///   the scalar evaluator bit for bit except at a power step, which is held to the engine's transcendental
///   tier. It needs double precision to round a curve's quotient, because the device's own division is
///   not the nearest quotient. Any other step is refused by name when the kernel is prepared, with
///   `ErrorCode::UnsupportedBackend`. The third is the direct three-by-three stencil
///   `Algorithms/Kernel/StencilKernel.h` defines, over one Float32 plane with a clamped or constant
///   border: each output's resolved neighbourhood goes through nine samples of device-owned scratch
///   and is accumulated in Float64 in the scalar reference's order, matching it bit for bit; and
/// - asynchronous completion on one worker thread the backend owns. It performs a piece of work's steps
///   in order, observes cancellation between them, and records each step's duration from the driver's
///   own event profiling, so transfer time and kernel time are the device's figures rather than a clock
///   read around a call.

#pragma once

#include <AfterNightEngineOpenClBackend/Api.h>

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace an
{

/// What the operating system's OpenCL loader can reach from this process.
enum class OpenClRuntimeState : std::uint8_t
{
    /// No OpenCL loader is installed in the operating system's directory, so nothing was asked. A
    /// graphics driver installs one; an SDK does not.
    LoaderNotInstalled,

    /// The loader answered, and no platform it reported has a GPU device.
    NoGpuDevice,

    /// At least one GPU device was found. Whether each can back a device is stated per device.
    ///
    /// Deliberately the last enumerator.
    GpuDevicesFound
};

/// The state's own name, for a diagnostic or a report.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param state the state to name. A value outside the enumeration is named
///        `"UnknownOpenClRuntimeState"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINEOPENCLBACKEND_API const char* openClRuntimeStateName(OpenClRuntimeState state) noexcept;

/// One GPU device an OpenCL platform reports, described in plain values.
struct OpenClGpuDevice
{
    /// The platform's name as its driver reports it.
    std::string platformName{};

    /// The platform's version string, which begins `OpenCL <major>.<minor>`.
    std::string platformVersion{};

    /// The device's product name.
    std::string deviceName{};

    /// The device's OpenCL version string, which begins `OpenCL <major>.<minor>`.
    std::string deviceVersion{};

    /// The driver's own version string.
    std::string driverVersion{};

    /// The OpenCL major version the device supports, read from `deviceVersion`; zero when that string
    /// does not state one.
    std::uint32_t versionMajor = 0u;

    /// The OpenCL minor version the device supports, read from `deviceVersion`.
    std::uint32_t versionMinor = 0u;

    /// How much memory the device has, in bytes.
    std::uint64_t globalMemoryBytes = 0u;

    /// The largest single buffer the device allocates, in bytes.
    std::uint64_t maximumAllocationBytes = 0u;

    /// Whether the device computes in double precision, which every one of the backend's kernels
    /// requires: a device without it is created with no kernels to offer.
    bool doublePrecision = false;

    /// Whether the device and the host share one memory. False for a discrete card.
    bool hostUnifiedMemory = false;

    /// Whether this backend can create a device over it: OpenCL 1.2 or later.
    bool meetsApiFloor = false;
};

/// What `openClRuntimeReport` found.
struct OpenClRuntimeReport
{
    /// What the loader could reach.
    OpenClRuntimeState state = OpenClRuntimeState::LoaderNotInstalled;

    /// The absolute path of the loader this process loaded, as UTF-8; empty when none was.
    std::string loaderPath{};

    /// Every GPU device of every platform, in platform order and then device order. A device's
    /// position here is what `OpenClBackendOptions::deviceIndex` names.
    std::vector<OpenClGpuDevice> devices{};
};

/// Asks the operating system's OpenCL loader what it can reach, without creating a context.
///
/// The first call in a process loads the loader and keeps it loaded. Thread-safe. Allocates the report.
///
/// @return the report. An absent loader, and a platform whose queries fail, are reported as what they
///         are rather than as a failure: a machine with no GPU is an ordinary answer to this question.
[[nodiscard]] AFTERNIGHTENGINEOPENCLBACKEND_API OpenClRuntimeReport openClRuntimeReport();

/// What a caller chooses about the device a backend is created over.
struct OpenClBackendOptions
{
    /// The device, as its position in `OpenClRuntimeReport::devices`.
    std::uint32_t deviceIndex = 0u;

    /// The most bytes of device memory, scratch included, the device may hold at once. Zero states the
    /// device's own memory size; anything larger than that is refused.
    std::uint64_t memoryLimitBytes = 0u;
};

/// The contract the backend's affine kernel satisfies.
///
/// The scalar reference's contract, narrowed where this kernel is narrower: all-valid input only, and
/// cancellation observed between a piece of work's steps rather than within the kernel.
///
/// Thread-safe. Allocation-free apart from a failure it has to describe.
///
/// @return the contract, or `ErrorCode::Internal` when the engine's kernel registry holds no reference
///         affine operation to narrow.
[[nodiscard]] AFTERNIGHTENGINEOPENCLBACKEND_API Result<KernelContract> openClAffineContract();

/// Creates a backend over one GPU device, ready to hand to `Device::create`.
///
/// Creates the device's context and its command queues and starts the backend's worker thread. A
/// kernel's program is compiled when the kernel is prepared: the affine and stencil programs once per
/// backend, a pointwise program for every preparation, since its source states the program. A device
/// without double precision is created with no kernels to offer. Thread-safe.
///
/// @param options which device, and how much of its memory. Copied.
/// @return the backend; or `ErrorCode::Unsupported` when no OpenCL loader is installed, when no GPU
///         device is found, or when the chosen device is below OpenCL 1.2; `ErrorCode::OutOfRange`
///         carrying the requested index and the device count for an index past the last device;
///         `ErrorCode::InvalidArgument` for a memory limit above the device's memory; or
///         `ErrorCode::ExternalFailure` carrying the OpenCL status as `ContextKey::LibraryCode` when the
///         driver refuses the context or a queue. Each of those names `"OpenCL"` as
///         `ContextKey::Library`. `ErrorCode::AllocationFailed` when the host runs out of memory, and
///         the `openClAffineContract` failure for a double-precision device, carry no library field.
[[nodiscard]] AFTERNIGHTENGINEOPENCLBACKEND_API Result<std::shared_ptr<DeviceBackend>>
createOpenClBackend(const OpenClBackendOptions& options = {});

} // namespace an

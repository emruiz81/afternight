// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A device backend, checked.
///
/// `Device` is how the engine and its callers use a backend. It creates resources, selects and
/// prepares kernels, and accepts work, and every one of those is checked before the backend is asked,
/// so the rules below hold for every backend rather than for each one that remembered them.
///
/// **Transfer is explicit.** A kernel binds shared or device memory and nothing else. Host and pinned
/// data reaches a kernel through a transfer the submission declares, and a submission that binds host
/// memory is refused rather than quietly staged - so every copy between the two sides is a transfer the
/// device counts, and a planner that keeps a resource on the device across two operations can prove it
/// did.
///
/// **Dependency is explicit.** Work that reads a resource which accepted, unfinished work writes -
/// or writes one such work reads - must depend on that work, directly or through its own dependencies.
/// No backend is relied on to run work in the order it was submitted, which is what keeps the contract
/// expressible on a device whose queues are not ordered.
///
/// **Fallback is explicit and diagnosed.** Whether a kernel runs on the device is asked of
/// `selectKernel`, which answers the device or the host and why. An unsupported kernel falls back to the
/// host only when the execution permits it, and the fallback is reported to the execution's diagnostic
/// sink as a warning; when it does not permit it, the answer is a refusal.
///
/// **Completion is exactly once and never lost.** The backend completes work through the work object;
/// a second completion is ignored and counted, work a backend drops is completed as abandoned, and
/// dependent work is released on the completing thread with no thread of the engine's own. Stopping an
/// execution finishes its waiting work at once, and running work observes the stop at the backend's
/// checkpoints.
///
/// **Lifetime is counted.** Resources, mappings, prepared kernels and unfinished work each keep the
/// device alive, and a device with any of them outstanding refuses to shut down. A device handle
/// destroyed early is a defect whose answer is a late release, not a use-after-free.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceError.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included, because every member below only borrows one for the call.
class ExecutionContext;

/// One copy a submission declares, between two resources of the same device.
///
/// The resources are borrowed for the `Device::submit` call only; accepted work keeps what it needs.
struct DeviceTransfer
{
    /// The resource read from. Must hold a resource of the submitting device.
    const DeviceResource* source = nullptr;

    /// The first byte read, from the start of the source.
    std::uint64_t sourceOffset = 0u;

    /// The resource written to. Must hold a resource of the submitting device.
    const DeviceResource* destination = nullptr;

    /// The first byte written, from the start of the destination.
    std::uint64_t destinationOffset = 0u;

    /// How many bytes are copied. Must be nonzero, and both ranges must lie inside their resources.
    std::uint64_t bytes = 0u;
};

/// One resource a submission binds to its kernel.
struct DeviceBinding
{
    /// The bound resource, borrowed for the `Device::submit` call. Must be shared or device memory of
    /// the submitting device.
    const DeviceResource* resource = nullptr;

    /// What the kernel does with it. Must equal the kernel's requirement for this binding.
    DeviceAccess access = DeviceAccess::Read;
};

/// The engine's record of one prepared kernel, defined inside the engine.
class PreparedKernelRecord;

/// A kernel a device prepared, ready to be named by submissions.
///
/// Returned by `Device::prepareKernel`. Copyable: copies name one prepared kernel, and the device counts
/// the kernel as outstanding until the last copy is gone.
class PreparedDeviceKernel
{
public:
    /// A handle that names nothing. `valid()` is false.
    PreparedDeviceKernel() noexcept = default;

    /// Whether this handle names a prepared kernel.
    ///
    /// @return false for a default-constructed handle.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_record != nullptr;
    }

    /// The identity of the device that prepared the kernel.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the device's identity, or `0` for a handle that names nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t deviceId() const noexcept;

    /// The prepared kernel, for reading its plan and requirements.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the kernel, valid while a copy of this handle exists; null for a handle that names
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const DeviceKernel* kernel() const noexcept;

private:
    /// The prepared-kernel record is what makes a handle, so it is also what may build one.
    friend class PreparedKernelRecord;

    /// Names a record the device has already built.
    ///
    /// @param record the record. Never null on this path.
    explicit PreparedDeviceKernel(std::shared_ptr<PreparedKernelRecord> record) noexcept : m_record(std::move(record))
    {
    }

    /// The prepared kernel's record, or null.
    std::shared_ptr<PreparedKernelRecord> m_record;
};

/// Everything one piece of work declares.
///
/// A plain aggregate of borrowed spans, valid for the `Device::submit` call only. The steps run in the
/// order they are listed: the transfers before, the kernel, the transfers after.
struct DeviceSubmission
{
    /// Copies performed before the kernel - typically host to device.
    std::span<const DeviceTransfer> transfersBefore{};

    /// The kernel to run, or null for work that only transfers.
    const PreparedDeviceKernel* kernel = nullptr;

    /// The kernel's resources, in its binding order. Must be empty when there is no kernel.
    std::span<const DeviceBinding> bindings{};

    /// Copies performed after the kernel - typically device to host.
    std::span<const DeviceTransfer> transfersAfter{};

    /// Work that must succeed before this work may start. Events of any device are accepted.
    std::span<const DeviceEvent> dependencies{};
};

/// Where a kernel will run.
enum class DeviceDispatch : std::uint8_t
{
    /// On the device, through a prepared kernel.
    Device,

    /// On the host, through the engine's CPU backends.
    Host
};

/// Why a kernel will run where `DeviceKernelSelection::dispatch` says.
enum class DeviceDispatchReason : std::uint8_t
{
    /// The device's capabilities cover the plan.
    KernelSupported,

    /// The execution is constrained to a host backend - `ComputeBackend::Reference` or
    /// `ComputeBackend::Cpu` - so the device is not used, whatever it supports.
    ExecutionConstrainedToHost,

    /// The device's capabilities do not cover the plan, and the execution permits falling back to the
    /// host.
    KernelNotSupported
};

/// The answer to where one kernel plan runs.
struct DeviceKernelSelection
{
    /// The device or the host.
    DeviceDispatch dispatch = DeviceDispatch::Host;

    /// Why.
    DeviceDispatchReason reason = DeviceDispatchReason::ExecutionConstrainedToHost;

    /// Whether the device's kernel reproduces its bits across runs. Meaningful only when `dispatch` is
    /// `DeviceDispatch::Device`.
    KernelDeterminism determinism = KernelDeterminism::Reproducible;
};

/// How many transfers of one kind a device performed, and how many bytes they moved.
struct DeviceTransferTally
{
    /// How many transfers.
    std::uint64_t transfers = 0u;

    /// How many bytes they copied between them.
    std::uint64_t bytes = 0u;
};

/// What a device has done, for a planner, a benchmark or a diagnostic.
///
/// A plain value read out under one lock, so its fields are consistent with each other. Transfers are
/// counted when a backend records them as performed, and classified by residency: a transfer into
/// device memory from anything else is an upload, one out of device memory into anything else is a
/// download, one between two device resources is a device copy, and one with no device-memory end is a
/// host copy.
struct DeviceCounters
{
    /// Submissions accepted.
    std::uint64_t acceptedWork = 0u;

    /// Submissions refused before acceptance.
    std::uint64_t refusedSubmissions = 0u;

    /// Accepted work that has not finished.
    std::uint64_t pendingWork = 0u;

    /// Work that finished successfully.
    std::uint64_t succeededWork = 0u;

    /// Work that finished with a failure other than a stop.
    std::uint64_t failedWork = 0u;

    /// Work that finished `Cancelled` or `DeadlineExceeded`.
    std::uint64_t cancelledWork = 0u;

    /// Transfers into device memory.
    DeviceTransferTally uploads{};

    /// Transfers out of device memory.
    DeviceTransferTally downloads{};

    /// Transfers between two device resources.
    DeviceTransferTally deviceCopies{};

    /// Transfers with no device-memory end.
    DeviceTransferTally hostCopies{};

    /// Kernels backends recorded as run.
    std::uint64_t kernelRuns = 0u;

    /// Resources whose memory has not been returned, including released ones still used by work.
    std::uint64_t liveResources = 0u;

    /// Mappings that have not ended.
    std::uint64_t liveMappings = 0u;

    /// Prepared kernels still named by a handle.
    std::uint64_t liveKernels = 0u;

    /// Shared and device memory, scratch and prepared kernels included, which is what
    /// `DeviceCapabilities::memoryLimitBytes` bounds. A prepared kernel that holds device memory counts as
    /// one allocation of `DeviceKernel::residentBytes` bytes while a handle names it. A refusal by that
    /// limit is counted as denied.
    MemoryCounters deviceMemory{};

    /// Host and pinned memory.
    MemoryCounters hostMemory{};

    /// Backend defects the engine contained: completions after the first, abandoned work, incomplete
    /// reports, and work taken by `enqueue` that returned a failure anyway.
    std::uint64_t backendDefects = 0u;
};

/// The engine's record of one device, defined inside the engine.
class DeviceState;

/// One device backend, used through the contract's checks.
///
/// Built through `create`. Move-only, because a copy would be a second owner of the backend. The
/// members are safe to call from several threads at once, except `shutdown`, which changes what the
/// handle owns and must not race with any other call on the same handle.
class Device
{
public:
    /// A device that holds nothing. `valid()` is false.
    Device() noexcept = default;

    /// Releases this handle's reference to the device.
    ///
    /// Destroying a device that still has resources, mappings, prepared kernels or work outstanding is a
    /// defect in the caller; its answer is a late release - the backend is kept until the last of them
    /// is gone - rather than a use-after-free. `shutdown` is the checked form.
    ~Device() = default;

    /// Takes over another handle's device, leaving it holding nothing.
    ///
    /// @param other the handle to take over. Left as a default-constructed one.
    Device(Device&& other) noexcept = default;

    /// Releases this handle's device and takes over another's, under the destructor's rule.
    ///
    /// @param other the handle to take over. Left as a default-constructed one.
    /// @return this handle.
    Device& operator=(Device&& other) noexcept = default;

    /// Copying is deleted: a copy would be a second owner of the backend.
    Device(const Device&) = delete;

    /// Copying is deleted. See the copy constructor.
    Device& operator=(const Device&) = delete;

    /// Builds a device from a backend, after validating what the backend says it offers.
    ///
    /// Thread-safe: devices share nothing. Allocates the device's record and its copy of the
    /// capabilities.
    ///
    /// @param backend the backend, whose ownership is shared with the device. Must not be null.
    /// @return the device; or `ErrorCode::InvalidArgument` for a null backend,
    ///         `ErrorCode::InvalidArgument` with `DeviceFailure::InvalidCapabilities` for an incoherent
    ///         capability report, or the translated failure of a backend that threw while reporting.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<Device> create(std::shared_ptr<DeviceBackend> backend);

    /// Whether this handle holds a device.
    ///
    /// @return false for a default-constructed handle, one that was moved from, and one that was shut
    ///         down.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// This device's identity, unique among every device this process creates and never reused.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the identity, or `0` for a handle that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t id() const noexcept;

    /// What the backend offers, as validated when the device was created.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the capabilities, valid while this handle holds the device; null for a handle that holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const DeviceCapabilities* capabilities() const noexcept;

    /// Creates one resource.
    ///
    /// The description is validated and the bytes are counted against the memory limit before the
    /// backend is asked.
    ///
    /// Thread-safe. Allocates the resource's record.
    ///
    /// @param descriptor what to create. Copied.
    /// @return the resource, or a failure: `ErrorCode::NotInitialized` with `DeviceFailure::DeviceShutDown`,
    ///         `ErrorCode::InvalidArgument` with `DeviceFailure::InvalidDescriptor`,
    ///         `ErrorCode::Unsupported` with `DeviceFailure::ResidencyNotOffered`,
    ///         `ErrorCode::LimitExceeded` with `DeviceFailure::ResourceTooLarge` carrying the requested
    ///         value and the limit, `ErrorCode::MemoryBudgetExceeded` carrying the requested, available and
    ///         limit bytes, or the backend's own failure as the cause of `DeviceFailure::BackendFailure`.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DeviceResource>
    createResource(const DeviceResourceDescriptor& descriptor) const;

    /// Where one kernel plan runs, and why.
    ///
    /// An execution constrained to `ComputeBackend::Reference` or `ComputeBackend::Cpu` runs on the
    /// host. Otherwise the plan runs on the device when a kernel the device advertises supports it, as
    /// `kernelContractSupports` decides. A plan the device does not support falls back to the host only
    /// when the execution permits fallback and is not constrained to `ComputeBackend::Gpu`; the fallback is
    /// reported to the execution's diagnostic sink at `DiagnosticSeverity::Warning`. Every other answer is
    /// reported at `DiagnosticSeverity::Debug`.
    ///
    /// Thread-safe with respect to `execution`, which is only read. Allocation-free apart from a failure
    /// it has to describe.
    ///
    /// @param execution the operation the kernel belongs to. Must hold something.
    /// @param plan the plan to place.
    /// @return the selection; or `ErrorCode::NotInitialized` for a device or execution that holds
    ///         nothing, or `ErrorCode::UnsupportedBackend` with `DeviceFailure::KernelNotSupported` for an
    ///         unsupported plan the execution may not fall back from.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DeviceKernelSelection> selectKernel(const ExecutionContext& execution,
                                                                                  const KernelPlanKey& plan) const;

    /// Prepares a kernel the device supports.
    ///
    /// Never falls back: a caller that may run on the host asks `selectKernel` first. The kernel the
    /// backend returns is checked against the request and against the device's limits, and the device
    /// memory it holds - `DeviceKernel::residentBytes` - is counted against the memory limit until the
    /// last handle naming it is gone. A kernel the limit does not admit is released before the refusal
    /// returns, so a refused preparation leaves the device holding exactly what it held before.
    ///
    /// Thread-safe. Allocates the kernel's record, and whatever the backend allocates.
    ///
    /// @param execution the operation preparing the kernel, whose cancellation is checked first.
    /// @param request the plan, layouts and arguments. Borrowed for the call.
    /// @return the prepared kernel; or `ErrorCode::NotInitialized`, `ErrorCode::Cancelled`,
    ///         `ErrorCode::DeadlineExceeded`, `ErrorCode::UnsupportedBackend` with
    ///         `DeviceFailure::KernelNotSupported`, the backend's own failure as the cause of
    ///         `DeviceFailure::BackendFailure` under the backend's code, `ErrorCode::Internal` with
    ///         `DeviceFailure::InvalidKernel` for a kernel that disagrees with the request, or
    ///         `ErrorCode::MemoryBudgetExceeded` carrying the requested, available and limit bytes for a
    ///         kernel whose device memory the limit does not admit.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<PreparedDeviceKernel>
    prepareKernel(const ExecutionContext& execution, const DeviceKernelRequest& request) const;

    /// Accepts one piece of work, or refuses it with nothing started.
    ///
    /// Every check runs before anything is accepted, in a fixed order: the device and the execution, the
    /// execution's cancellation, the submission's shape, the kernel and its bindings, the transfers, the
    /// dependencies, the memory limit for the kernel's scratch, and last, under the device's lock, the
    /// resources' mappings and pending use. Scratch allocated for a submission that is then refused is
    /// returned before the refusal. An accepted submission returns its event at once; the work runs
    /// when its dependencies have succeeded, under the stop token and deadline `execution` carries.
    ///
    /// Thread-safe. Allocates the work's record and event, and the kernel's scratch through the backend.
    ///
    /// @param execution the operation the work belongs to. Only its policy is kept, never the reference.
    /// @param submission what the work declares. Borrowed for the call.
    /// @return the work's event; or a refusal: `ErrorCode::NotInitialized`, with
    ///         `DeviceFailure::DeviceShutDown` for a device handle that holds nothing and without a
    ///         device failure for an execution that holds nothing; `ErrorCode::Cancelled` or
    ///         `ErrorCode::DeadlineExceeded`; `ErrorCode::InvalidArgument` with
    ///         `DeviceFailure::EmptySubmission`, `InvalidDependency`, `InvalidKernel` or
    ///         `BindingMismatch`; `ErrorCode::LimitExceeded` with `DeviceFailure::SubmissionTooLarge`;
    ///         `ErrorCode::InvalidState` with `DeviceFailure::ResourceReleased` or
    ///         `UndeclaredDependency`; `ErrorCode::IncompatibleArguments` with
    ///         `DeviceFailure::ForeignDevice` or `NotDeviceReachable`; `ErrorCode::OutOfRange` with
    ///         `DeviceFailure::TransferOutOfRange`; `ErrorCode::Aliasing` with
    ///         `DeviceFailure::TransferOverlap` or `BindingAliased`; `ErrorCode::Busy` with
    ///         `DeviceFailure::ResourceMapped`; `ErrorCode::MemoryBudgetExceeded`; or the backend's
    ///         failure to allocate scratch as the cause of `DeviceFailure::BackendFailure`.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DeviceEvent> submit(const ExecutionContext& execution,
                                                                  const DeviceSubmission& submission) const;

    /// What this device has done.
    ///
    /// Thread-safe and allocation-free. The values are consistent with each other and may be out of date
    /// the moment they are read.
    ///
    /// @return the counters, or all zeroes for a handle that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API DeviceCounters counters() const noexcept;

    /// Releases the backend, and says whether it could.
    ///
    /// Refuses while any resource, mapping, prepared kernel or unfinished work of the device is
    /// outstanding, leaving the device exactly as it was. Idempotent: shutting down a handle that holds
    /// nothing succeeds.
    ///
    /// Not thread-safe with respect to this handle. Allocates only the failure it reports.
    ///
    /// @return success, or `ErrorCode::Busy` with `DeviceFailure::DeviceInUse`.
    [[nodiscard]] AFTERNIGHTENGINE_API Status shutdown();

private:
    /// Takes the record the factory has already built.
    ///
    /// @param state the device's record. Never null on this path.
    explicit Device(std::shared_ptr<DeviceState> state) noexcept : m_state(std::move(state)) {}

    /// The device's record, or null. Shared with resources, mappings, prepared kernels and unfinished
    /// work, so the backend outlives a handle destroyed while any of them is outstanding.
    std::shared_ptr<DeviceState> m_state;
};

} // namespace an

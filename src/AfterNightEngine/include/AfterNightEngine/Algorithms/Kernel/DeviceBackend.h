// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a device backend implements, and what it is handed.
///
/// The engine keeps the contract and a backend supplies the device. A backend is whatever turns the
/// contract into real work - a GPU through whichever SDK a build chose, or a deliberately fake device in
/// a test - and it is compiled separately from everything that uses it. This header is the whole of
/// what one implements, which is why it is plain C++ with no SDK type, macro or handle anywhere in it:
/// the seam is the part of the engine that survives when any particular backend is deleted.
///
/// The division of labour is deliberate. A backend does the four things only it can do: say what it
/// offers, allocate and map its memory, prepare kernels, and run work. Everything that must hold for
/// every backend belongs to the engine's `Device` - which resources work may bind, which transfers are
/// in range, which dependencies have to be declared, when work waits and when it is refused, how often
/// a piece of work may complete, and what it all costs. A backend therefore receives only work that has
/// passed every rule and whose every dependency has already succeeded, and it reports completion
/// through the work object rather than by returning - so a backend that finishes later, on its own
/// thread, is the ordinary case rather than an extension of a synchronous one.
///
/// Kernel capability is the kernel registry's vocabulary. A backend advertises the `KernelContract`
/// each of its kernels satisfies - representation, arrangement, stride, alias, validity and accuracy
/// against the scalar oracle - and, beside it, whether repeated runs reproduce their bits. A device can
/// meet an accuracy contract without being run-to-run deterministic, and a planner needs to know which.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace an
{

/// Whether repeated runs of a device kernel reproduce their bits.
///
/// Separate from the accuracy contract on purpose. Accuracy says how far a result may be from the
/// scalar oracle; this says whether two runs of the same kernel over the same input on the same device
/// agree with each other.
enum class KernelDeterminism : std::uint8_t
{
    /// Every run over the same input on this device writes the same bits, whatever else is running.
    Reproducible,

    /// Runs may differ from each other, each within the kernel's accuracy contract - for instance
    /// because a reduction's order depends on how the device scheduled it.
    WithinAccuracy
};

/// The determinism's own name, for a diagnostic or a report.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param determinism the value to name. A value outside the enumeration is named
///        `"UnknownKernelDeterminism"`.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* kernelDeterminismName(KernelDeterminism determinism) noexcept;

/// One kernel a device can run.
struct DeviceKernelSupport
{
    /// The operation and the capabilities this device's implementation of it satisfies. Its accuracy
    /// is stated against the operation's scalar oracle, exactly as a CPU registration's is.
    KernelContract contract{};

    /// Whether repeated runs reproduce their bits.
    KernelDeterminism determinism = KernelDeterminism::Reproducible;
};

/// What a device backend offers, reported once when a `Device` is created from it.
///
/// A backend must report the same value for its whole life: the engine validates it once and plans
/// against the copy it kept.
struct DeviceCapabilities
{
    /// A name for a person reading a diagnostic - `"mock"`, or a device's product name. Must not be
    /// empty. Never parsed.
    std::string name{};

    /// Whether the backend creates host-residency resources.
    bool hostResidency = false;

    /// Whether the backend creates pinned-residency resources.
    bool pinnedResidency = false;

    /// Whether the backend creates shared-residency resources.
    bool sharedResidency = false;

    /// Whether the backend creates device-residency resources. At least one of this and
    /// `sharedResidency` must be true, or no kernel could bind anything.
    bool deviceResidency = false;

    /// Whether work may finish after `DeviceBackend::enqueue` has returned. False states that every
    /// piece of work is complete by then, which a caller may rely on and the engine does not.
    bool asynchronous = true;

    /// The most bytes of shared and device memory - scratch and prepared kernels included - the device
    /// may hold at once. Must be nonzero. The engine refuses a resource or scratch above it before asking
    /// the backend, and a prepared kernel above it as soon as the backend reports what the kernel holds.
    std::uint64_t memoryLimitBytes = 0u;

    /// The most bytes one resource may hold. Must be nonzero.
    std::uint64_t maximumResourceBytes = 0u;

    /// The widest alignment one resource may ask for. Must be a power of two.
    std::uint64_t maximumAlignmentBytes = 0u;

    /// The most bindings one kernel may declare. Must be nonzero.
    std::uint32_t maximumBindings = 0u;

    /// The most transfers one submission may declare, before and after its kernel together. Must be
    /// nonzero.
    std::uint32_t maximumTransfers = 0u;

    /// The most dependencies one submission may declare. Zero states that work never waits for other
    /// work on this device.
    std::uint32_t maximumDependencies = 0u;

    /// Every kernel the device can run. May be empty for a device that only holds and moves memory.
    std::vector<DeviceKernelSupport> kernels{};
};

/// Whether a device's capabilities include a residency.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param capabilities what the device reported.
/// @param residency the residency to look for.
/// @return true when the device creates resources of that residency; false for a value outside the
///         enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API bool offersResidency(const DeviceCapabilities& capabilities,
                                                        ResourceResidency residency) noexcept;

/// A backend's name for one of its allocations.
///
/// Opaque to the engine, which stores it, hands it back, and compares it with nothing. What it means -
/// an index, a device pointer, a buffer object - is the backend's to decide.
struct DeviceAllocation
{
    /// The backend's handle. Zero is never a live allocation.
    std::uint64_t handle = 0u;

    /// Two allocations are the same when their handles are.
    ///
    /// @param left one allocation.
    /// @param right the other.
    /// @return true when both name the same handle.
    [[nodiscard]] friend constexpr bool operator==(const DeviceAllocation& left,
                                                   const DeviceAllocation& right) noexcept = default;
};

/// What one binding of a prepared kernel needs from the resource bound to it.
struct DeviceBindingRequirement
{
    /// What the kernel does with the resource. The submission's binding must state the same access.
    DeviceAccess access = DeviceAccess::Read;

    /// The fewest bytes the resource may hold. Must be nonzero.
    std::uint64_t minimumBytes = 0u;

    /// The alignment the resource's first byte must satisfy. Must be a power of two.
    std::uint64_t alignment = 1u;

    /// The layout the kernel reads or writes, when it has one. A bound resource that states a layout
    /// must state this one.
    std::optional<ImageLayout> layout{};
};

/// A kernel a backend has prepared, ready to be bound to resources and submitted.
///
/// Preparing is where a device does its expensive, once-per-plan work - compiling or looking up a
/// program - so that submitting it many times costs only the binding. Implemented by a backend, held by
/// the engine, and handed back to the same backend inside work; a backend may therefore rely on every
/// kernel it receives being one it prepared.
///
/// Immutable once prepared, and safe to use from several threads at once.
class DeviceKernel
{
public:
    /// Virtual, so a kernel held by base pointer is destroyed as what it is.
    virtual ~DeviceKernel() = default;

    /// The plan this kernel was prepared for.
    ///
    /// @return the plan, which must equal the one in the request that prepared it.
    [[nodiscard]] virtual KernelPlanKey plan() const noexcept = 0;

    /// What each binding needs, in the order a submission must bind them.
    ///
    /// @return the requirements, valid for the kernel's life. At least one, and no more than the
    ///         device's `maximumBindings`.
    [[nodiscard]] virtual std::span<const DeviceBindingRequirement> bindings() const noexcept = 0;

    /// How many bytes of device scratch one run needs.
    ///
    /// The engine allocates the scratch when work using this kernel is accepted, counts it against the
    /// device's memory limit, hands it to the backend with the work, and returns it when the work
    /// finishes.
    ///
    /// @return the bytes, or zero for a kernel that needs none.
    [[nodiscard]] virtual std::uint64_t scratchBytes() const noexcept = 0;

    /// How many bytes of device memory the prepared kernel itself holds, for as long as it lives.
    ///
    /// What the backend placed on the device when it prepared the kernel and keeps there between runs -
    /// the tables and constants a program reads that no binding supplies - as opposed to what one run
    /// needs, which is `scratchBytes`. A compiled program's code is the driver's and is not counted.
    ///
    /// The engine reads it once, when the kernel is prepared, and counts it against the device's memory
    /// limit from then until the last handle naming the kernel is gone. A kernel the limit does not
    /// admit is released at once and its preparation refused, so the answer must be what the kernel
    /// actually holds: reporting less hides memory from every later check, and there is no default,
    /// because a backend that never considered the question is exactly the one that would report zero.
    ///
    /// @return the bytes, or zero for a kernel that holds none. Must not change for the kernel's life.
    [[nodiscard]] virtual std::uint64_t residentBytes() const noexcept = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    DeviceKernel() = default;

    /// Protected so a kernel is never copied through a base pointer and sliced.
    ///
    /// @param other the kernel to copy from.
    DeviceKernel(const DeviceKernel& other) = default;

    /// Protected for the same reason as the copy constructor.
    ///
    /// @param other the kernel to copy from.
    /// @return this kernel.
    DeviceKernel& operator=(const DeviceKernel& other) = default;
};

/// What a caller asks a device to prepare.
///
/// Everything in it is borrowed for the call; a backend that keeps something copies it.
struct DeviceKernelRequest
{
    /// The operation and buffer-policy choices. The device's capabilities must cover it.
    KernelPlanKey plan{};

    /// The layout each binding will read or write, in the kernel's binding order. How many there are
    /// is the operation's to define.
    std::span<const ImageLayout> bindingLayouts{};

    /// The operation's own arguments - coefficients, radii - as the bytes of the trivially copyable
    /// structure the operation defines for them. `deviceKernelArguments` builds the span.
    std::span<const std::byte> arguments{};
};

/// The bytes of an operation's argument structure, for `DeviceKernelRequest::arguments`.
///
/// Inline and header-only, like every template in the published surface.
///
/// @tparam ArgumentsT the operation's argument structure, which must be trivially copyable so that its
///         bytes are the whole of its value.
/// @param arguments the structure. Borrowed: it must outlive the span.
/// @return a span over its object representation.
template <typename ArgumentsT>
    requires std::is_trivially_copyable_v<ArgumentsT>
[[nodiscard]] std::span<const std::byte> deviceKernelArguments(const ArgumentsT& arguments) noexcept
{
    return std::as_bytes(std::span<const ArgumentsT, 1u>(&arguments, 1u));
}

/// One transfer a backend performs, resolved to its allocations.
struct DeviceTransferStep
{
    /// The allocation the bytes are read from.
    DeviceAllocation source{};

    /// Where the source's bytes live.
    ResourceResidency sourceResidency = ResourceResidency::Host;

    /// The first byte read, from the start of the source.
    std::uint64_t sourceOffset = 0u;

    /// The allocation the bytes are written to.
    DeviceAllocation destination{};

    /// Where the destination's bytes live.
    ResourceResidency destinationResidency = ResourceResidency::Device;

    /// The first byte written, from the start of the destination.
    std::uint64_t destinationOffset = 0u;

    /// How many bytes are copied. Never zero, and both ranges were checked to lie inside their
    /// resources and not to overlap.
    std::uint64_t bytes = 0u;
};

/// One resource bound to a kernel, resolved to its allocation.
struct DeviceBindingStep
{
    /// The bound allocation.
    DeviceAllocation allocation{};

    /// Where its bytes live: shared or device memory.
    ResourceResidency residency = ResourceResidency::Device;

    /// What the kernel does with it, which equals its requirement's access.
    DeviceAccess access = DeviceAccess::Read;

    /// How many bytes the resource holds, at least the requirement's minimum.
    std::uint64_t bytes = 0u;
};

/// The engine's record of one piece of work while a backend holds it, defined inside the engine.
class DeviceWorkRecord;

/// One piece of accepted work whose dependencies have all succeeded, handed to a backend to run.
///
/// The backend performs, in order, the transfers before, the kernel, and the transfers after, and then
/// completes the work exactly once. It records each transfer and the kernel as it performs them, so
/// that the work's timing separates the two and the device's counters count what was actually done;
/// a success that did not record every declared step is turned into an engine-detected failure.
///
/// Move-only: the work is in one place at a time. A work object destroyed before `complete` was called
/// completes the work as `ErrorCode::Internal` with `DeviceFailure::WorkAbandoned`, so a backend defect
/// ends the wait of whoever depends on the work rather than leaving it forever.
///
/// The accessors are safe to call from any thread. `markRunning`, the recording calls and `complete`
/// must not be called concurrently on one work object; a backend runs one piece of work at a time on
/// whichever thread it likes.
class DeviceWork
{
public:
    /// A work object that holds nothing. `valid()` is false.
    DeviceWork() noexcept = default;

    /// Completes the work as abandoned if it holds work that was never completed.
    AFTERNIGHTENGINE_API ~DeviceWork();

    /// Takes over another work object's work, leaving it holding nothing.
    ///
    /// @param other the work to take over. Left as a default-constructed one.
    AFTERNIGHTENGINE_API DeviceWork(DeviceWork&& other) noexcept;

    /// Completes this object's work as abandoned if it was never completed, then takes over another's.
    ///
    /// @param other the work to take over. Left as a default-constructed one.
    /// @return this work object.
    AFTERNIGHTENGINE_API DeviceWork& operator=(DeviceWork&& other) noexcept;

    /// Copying is deleted: the work is completed exactly once, from one place.
    DeviceWork(const DeviceWork&) = delete;

    /// Copying is deleted. See the copy constructor.
    DeviceWork& operator=(const DeviceWork&) = delete;

    /// Whether this object holds work.
    ///
    /// @return false for a default-constructed object and one that was moved from.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_record != nullptr;
    }

    /// The identity of the event that observes this work.
    ///
    /// @return the event's identity, or `0` for an object that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t eventId() const noexcept;

    /// The transfers to perform before the kernel, in order.
    ///
    /// @return the steps, valid while this object holds the work; empty for an object that holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const DeviceTransferStep> transfersBefore() const noexcept;

    /// The kernel to run, prepared earlier by this backend.
    ///
    /// @return the kernel, valid while this object holds the work; null when the work runs none.
    [[nodiscard]] AFTERNIGHTENGINE_API const DeviceKernel* kernel() const noexcept;

    /// The resources bound to the kernel, in the kernel's binding order.
    ///
    /// @return the bindings, valid while this object holds the work; empty when the work runs no
    ///         kernel.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const DeviceBindingStep> bindings() const noexcept;

    /// The device scratch the engine allocated for the kernel.
    ///
    /// @return the allocation, of `DeviceKernel::scratchBytes()` bytes in device memory when the
    ///         backend offers it and shared memory otherwise; no value when the kernel needs none.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<DeviceAllocation> scratch() const noexcept;

    /// The transfers to perform after the kernel, in order.
    ///
    /// @return the steps, valid while this object holds the work; empty for an object that holds
    ///         nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const DeviceTransferStep> transfersAfter() const noexcept;

    /// What a backend calls at a checkpoint: before starting and between steps.
    ///
    /// Observes the stop token and the deadline of the execution the work was submitted under.
    /// Allocation-free.
    ///
    /// @return success, `ErrorCode::Cancelled`, `ErrorCode::DeadlineExceeded`, or
    ///         `ErrorCode::NotInitialized` for an object that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API Status checkCancellation() const noexcept;

    /// Records that the backend has started the work, which moves its event to
    /// `DeviceWorkPhase::Running`.
    ///
    /// Calling it twice records the first start. A backend that never calls it has its start taken to
    /// be the moment it completed the work. Allocation-free.
    AFTERNIGHTENGINE_API void markRunning() noexcept;

    /// Records that the next declared transfer has been performed, and what it took.
    ///
    /// Transfers are recorded in the order they were declared, before ones first. Allocation-free.
    ///
    /// @param nanoseconds how long the transfer took on the backend's clock.
    AFTERNIGHTENGINE_API void recordTransfer(std::uint64_t nanoseconds) noexcept;

    /// Records that the kernel has run, and what it took.
    ///
    /// Allocation-free.
    ///
    /// @param nanoseconds how long the kernel took on the backend's clock.
    AFTERNIGHTENGINE_API void recordKernel(std::uint64_t nanoseconds) noexcept;

    /// Gives the work its outcome.
    ///
    /// Exactly once. A second call is ignored and counted as a backend defect on the device's
    /// counters. A success is accepted only when every declared transfer and the kernel were recorded;
    /// otherwise the work fails with `ErrorCode::Internal` and `DeviceFailure::IncompleteReport`. A
    /// failure is kept as the backend reported it.
    ///
    /// Completing the work may run later work that was waiting for it, on the calling thread, before
    /// this call returns - which is why a backend must accept `DeviceBackend::enqueue` from inside a
    /// call to `complete`.
    ///
    /// @param outcome success, or the failure that stopped the work.
    AFTERNIGHTENGINE_API void complete(Status outcome) noexcept;

private:
    /// The work record is what makes a work object, so it is also what may build one.
    friend class DeviceWorkRecord;

    /// Holds a record the device has already built and validated.
    ///
    /// @param record the record. Never null on this path.
    explicit DeviceWork(std::shared_ptr<DeviceWorkRecord> record) noexcept;

    /// The work's record, or null.
    std::shared_ptr<DeviceWorkRecord> m_record;
};

/// What a device backend implements.
///
/// Implemented by the code that owns a device - an opt-in module, or a test - and handed to
/// `Device::create`, which keeps it until the device shuts down. The engine calls it only with requests
/// that have passed the contract's checks, as each member states, so an implementation checks what is
/// its own to check and nothing else.
///
/// Every member except the destructor must be safe to call from several threads at once, and
/// `enqueue` must also be safe to call from inside `DeviceWork::complete` on the backend's own thread.
/// A member that throws has the exception translated at the boundary; it is still a defect to throw.
class DeviceBackend
{
public:
    /// Virtual, so a backend held by base pointer is destroyed as what it is. Called only when no
    /// resource, mapping, prepared kernel or work of the device remains.
    ///
    /// It may run on any thread - including one of this backend's own, while that thread completes the
    /// last piece of work of a device whose handle and resources are already gone. A backend with
    /// threads of its own therefore lets the state those threads use outlive this object, rather than
    /// joining the thread the destructor is running on.
    virtual ~DeviceBackend() = default;

    /// What this backend offers. Called once, when a `Device` is created.
    ///
    /// @return the capabilities, which must not change for the backend's life.
    [[nodiscard]] virtual DeviceCapabilities capabilities() const = 0;

    /// Allocates one resource.
    ///
    /// Called only with a validated description whose residency this backend offers and whose size and
    /// alignment are within its limits, after the engine has counted it against the memory limit.
    ///
    /// @param descriptor what to allocate. Borrowed for the call.
    /// @return the allocation, whose handle is nonzero; or the backend's failure, typically
    ///         `ErrorCode::AllocationFailed`.
    [[nodiscard]] virtual Result<DeviceAllocation> allocate(const DeviceResourceDescriptor& descriptor) = 0;

    /// Returns one allocation.
    ///
    /// Called exactly once per successful `allocate`, with the same description, when nothing uses the
    /// allocation any more.
    ///
    /// @param allocation what `allocate` returned.
    /// @param descriptor what it was allocated with.
    virtual void release(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept = 0;

    /// Makes a host-addressable allocation's bytes available to the host.
    ///
    /// Called only for host, pinned or shared memory, only while no accepted work uses the allocation in
    /// a way the access conflicts with, and never twice without an `unmap` between.
    ///
    /// @param allocation the allocation to map.
    /// @param descriptor what it was allocated with.
    /// @param access what the host will do with the bytes.
    /// @return the first byte, valid until `unmap`, never null; or the backend's failure.
    [[nodiscard]] virtual Result<std::byte*> map(DeviceAllocation allocation,
                                                 const DeviceResourceDescriptor& descriptor, DeviceAccess access) = 0;

    /// Ends a mapping.
    ///
    /// Called exactly once per successful `map`.
    ///
    /// @param allocation the mapped allocation.
    /// @param descriptor what it was allocated with.
    virtual void unmap(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept = 0;

    /// Prepares a kernel.
    ///
    /// Called only for a plan this backend's capabilities cover. The request's layouts and arguments are
    /// the operation's to validate, and a refusal of them is an ordinary returned failure. The device
    /// memory the kernel holds is counted only once it is returned, through `DeviceKernel::residentBytes`,
    /// so a backend whose kernels hold a great deal refuses what it cannot place rather than relying on
    /// the engine to have refused it first.
    ///
    /// @param request what to prepare. Borrowed for the call.
    /// @return the kernel, which must report the request's plan; or the backend's failure.
    [[nodiscard]] virtual Result<std::shared_ptr<const DeviceKernel>>
    prepareKernel(const DeviceKernelRequest& request) = 0;

    /// Takes one piece of work to run.
    ///
    /// Called only with work whose dependencies have all succeeded. Returning success means the backend
    /// took the work - moved from `work` - and will complete it, now or later, on any thread. Returning a
    /// failure without taking the work means the engine completes it with that failure as its cause; a
    /// backend that took the work and still returns a failure has the failure counted as a defect, and
    /// remains responsible for completing the work.
    ///
    /// @param work the work to run.
    /// @return success when the work was taken, or why it was not.
    [[nodiscard]] virtual Status enqueue(DeviceWork&& work) = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    DeviceBackend() = default;

    /// Protected so a backend is never copied through a base pointer and sliced.
    ///
    /// @param other the backend to copy from.
    DeviceBackend(const DeviceBackend& other) = default;

    /// Protected for the same reason as the copy constructor.
    ///
    /// @param other the backend to copy from.
    /// @return this backend.
    DeviceBackend& operator=(const DeviceBackend& other) = default;
};

} // namespace an

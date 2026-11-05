// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the device facade's own records: a device, a prepared kernel, and one piece of
// work.
//
// `Device` in the published surface is a handle, and this is what it holds. There are three records
// because three things have lifetimes of their own: the device, which lives while anything of it is
// outstanding; a prepared kernel, which the backend made and which must be gone before the backend is;
// and a piece of work, which lives from acceptance until its outcome is known and is shared between the
// device, the backend holding it and the dependencies that will release it.
//
// The work record is where the contract's concurrency is resolved, so its rules are stated once here
// rather than at each use:
//
// - a record's stage only moves forwards - registering, waiting, handed off, finished - and only under
//   the record's own lock;
// - exactly one path finishes a record, whichever gets there first: the backend completing it, the
//   backend dropping it, a dependency failing, a stop request while it waits, or the backend refusing to
//   take it. Every later attempt sees it finished and does nothing, or counts a backend defect;
// - no lock is held while the backend is called or while an event's continuations run, so a backend
//   that completes work inline, or a continuation that releases more work, cannot deadlock the device;
// - a record finished while waiting is retired rather than destroyed on the spot, because the thread
//   finishing it may be running inside that record's own stop callback; and
// - a record holds its device only until it finishes, so a finished record kept by a backend never keeps
//   that backend alive.
//
// Lock order: the device's lock may be held while an event's lock is taken. A record's lock and the
// device's lock are never held together.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>

#include "runtime/DeviceEventState.h"
#include "runtime/DeviceResourceState.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace an
{

class DeviceState;
class DeviceWorkRecord;

/// Finishes a waiting piece of work when its execution is asked to stop.
struct DeviceStopHook
{
    /// The work, held weakly: the hook never keeps it alive.
    std::weak_ptr<DeviceWorkRecord> record;

    /// Called by the stop source, on the thread that requested the stop.
    void operator()() const noexcept;
};

/// One prepared kernel, counted by its device - with the device memory it holds - while any handle names it.
class PreparedKernelRecord
{
public:
    /// Records a kernel the backend prepared and the device admitted.
    ///
    /// @param owner the device, kept alive while the kernel is.
    /// @param prepared the backend's kernel.
    /// @param residentBytes the device memory the kernel reported holding, already counted by the device. Kept
    ///        rather than asked again, so the device counts out exactly what it counted in.
    PreparedKernelRecord(std::shared_ptr<DeviceState> owner, std::shared_ptr<const DeviceKernel> prepared,
                         std::uint64_t residentBytes) noexcept;

    /// Releases the backend's kernel, then tells the device it is gone and what it held.
    ~PreparedKernelRecord();

    PreparedKernelRecord(const PreparedKernelRecord&) = delete;
    PreparedKernelRecord& operator=(const PreparedKernelRecord&) = delete;
    PreparedKernelRecord(PreparedKernelRecord&&) = delete;
    PreparedKernelRecord& operator=(PreparedKernelRecord&&) = delete;

    /// @return the device that prepared the kernel.
    [[nodiscard]] const std::shared_ptr<DeviceState>& device() const noexcept
    {
        return m_device;
    }

    /// @return the backend's kernel.
    [[nodiscard]] const DeviceKernel& kernel() const noexcept
    {
        return *m_kernel;
    }

    /// Builds the public handle over a record.
    ///
    /// @param record the record.
    /// @return a handle naming it.
    [[nodiscard]] static PreparedDeviceKernel makeHandle(std::shared_ptr<PreparedKernelRecord> record) noexcept
    {
        return PreparedDeviceKernel(std::move(record));
    }

    /// The record behind a public handle.
    ///
    /// @param handle the handle.
    /// @return its record, which is null for a handle that names nothing.
    [[nodiscard]] static const std::shared_ptr<PreparedKernelRecord>& share(const PreparedDeviceKernel& handle) noexcept
    {
        return handle.m_record;
    }

private:
    std::shared_ptr<DeviceState> m_device;
    std::shared_ptr<const DeviceKernel> m_kernel;
    std::uint64_t m_residentBytes;
};

/// How far a piece of work has got, as its device tracks it.
enum class DeviceWorkStage : std::uint8_t
{
    /// Accepted, and still registering with its dependencies and its stop token.
    Registering,

    /// Waiting for one or more dependencies.
    Waiting,

    /// Handed to the backend, which completes it.
    HandedOff,

    /// Given its outcome.
    Finished
};

/// One piece of accepted work.
class DeviceWorkRecord final : public DeviceEventContinuation, public std::enable_shared_from_this<DeviceWorkRecord>
{
public:
    /// Records work the device has accepted.
    ///
    /// @param owner the device, held until the work finishes.
    /// @param observed the event the caller holds.
    /// @param token the submitting execution's stop token.
    /// @param expiry the submitting execution's deadline.
    /// @param dependencyCount how many dependencies the work declared.
    DeviceWorkRecord(const std::shared_ptr<DeviceState>& owner, std::shared_ptr<DeviceEventState> observed,
                     std::stop_token token, std::optional<std::chrono::steady_clock::time_point> expiry,
                     std::size_t dependencyCount) noexcept;

    ~DeviceWorkRecord() override;

    DeviceWorkRecord(const DeviceWorkRecord&) = delete;
    DeviceWorkRecord& operator=(const DeviceWorkRecord&) = delete;
    DeviceWorkRecord(DeviceWorkRecord&&) = delete;
    DeviceWorkRecord& operator=(DeviceWorkRecord&&) = delete;

    /// Counts one dependency as finished and moves the work on when that was the last, or a failure.
    ///
    /// @param dependency the dependency that finished.
    void onEventFinished(const DeviceEventState& dependency) noexcept override;

    /// @return success, or the stop or deadline the submitting execution has reached.
    [[nodiscard]] Status checkCancellation() const noexcept;

    /// @return how many transfers the work declared, before and after its kernel together.
    [[nodiscard]] std::uint32_t declaredTransfers() const noexcept
    {
        return static_cast<std::uint32_t>(transfersBefore.size() + transfersAfter.size());
    }

    /// Builds the object a backend is handed.
    ///
    /// @param record the record.
    /// @return a work object holding it.
    [[nodiscard]] static DeviceWork makeWork(std::shared_ptr<DeviceWorkRecord> record) noexcept
    {
        return DeviceWork(std::move(record));
    }

    /// The record behind a work object.
    ///
    /// @param work the work object.
    /// @return its record, which is null for one that holds nothing.
    [[nodiscard]] static const std::shared_ptr<DeviceWorkRecord>& share(const DeviceWork& work) noexcept
    {
        return work.m_record;
    }

    /// The event the caller holds. Fixed at acceptance.
    const std::shared_ptr<DeviceEventState> event;

    /// The submitting execution's stop token. Fixed at acceptance.
    const std::stop_token stopToken;

    /// The submitting execution's deadline. Fixed at acceptance.
    const std::optional<std::chrono::steady_clock::time_point> deadline;

    /// The device, observed without keeping it alive, for counting defects after the work finished.
    const std::weak_ptr<DeviceState> deviceObserver;

    /// The resolved transfers before the kernel. Written before the work is handed off.
    std::vector<DeviceTransferStep> transfersBefore;

    /// The resolved transfers after the kernel. Written before the work is handed off.
    std::vector<DeviceTransferStep> transfersAfter;

    /// The resolved bindings. Written before the work is handed off.
    std::vector<DeviceBindingStep> bindings;

    /// The kernel's scratch, when it needs any. Written before the work is handed off.
    std::optional<DeviceAllocation> scratch;

    /// What the scratch was allocated as.
    DeviceResourceDescriptor scratchDescriptor{};

    /// The prepared kernel, held until the work finishes.
    std::shared_ptr<PreparedKernelRecord> kernel;

    /// Every resource the work uses, held until the work finishes.
    std::vector<std::shared_ptr<DeviceResourceState>> resources;

    /// Guards the members below.
    mutable std::mutex mutex;

    /// The device, held until the work finishes.
    std::shared_ptr<DeviceState> device;

    /// How far the work has got.
    DeviceWorkStage stage = DeviceWorkStage::Registering;

    /// Dependencies not yet finished, plus one while the work is registering.
    std::size_t remainingDependencies = 1u;

    /// The first dependency outcome that was not a success.
    std::optional<Status> dependencyFailure;

    /// Whether the stop token fired while the work was registering or waiting.
    bool stopRequested = false;

    /// The stop registration. Written once while registering; destroyed at hand-off or with the record.
    std::unique_ptr<std::stop_callback<DeviceStopHook>> stopHook;
};

/// One device: its backend, what the backend offers, and everything the device counts.
class DeviceState final : public DeviceResourceOwner, public std::enable_shared_from_this<DeviceState>
{
public:
    /// Records a device over a backend whose capabilities were validated.
    ///
    /// @param identity the device's process-unique identity.
    /// @param implementation the backend.
    /// @param offered the validated capabilities.
    DeviceState(std::uint64_t identity, std::shared_ptr<DeviceBackend> implementation,
                DeviceCapabilities offered) noexcept;

    ~DeviceState() override;

    DeviceState(const DeviceState&) = delete;
    DeviceState& operator=(const DeviceState&) = delete;
    DeviceState(DeviceState&&) = delete;
    DeviceState& operator=(DeviceState&&) = delete;

    /// @return the device's identity.
    [[nodiscard]] std::uint64_t id() const noexcept
    {
        return m_id;
    }

    /// @return the validated capabilities.
    [[nodiscard]] const DeviceCapabilities& capabilities() const noexcept
    {
        return m_capabilities;
    }

    /// See `Device::createResource`.
    ///
    /// @param descriptor what to create.
    /// @return the resource or the refusal.
    [[nodiscard]] Result<DeviceResource> createResource(const DeviceResourceDescriptor& descriptor);

    /// See `DeviceResource::map`.
    ///
    /// @param resource the resource's record.
    /// @param access what the host will do.
    /// @return the mapping or the refusal.
    [[nodiscard]] Result<DeviceMapping> mapResource(const std::shared_ptr<DeviceResourceState>& resource,
                                                    DeviceAccess access) override;

    /// Ends a mapping.
    ///
    /// @param resource the mapped resource's record.
    void unmapResource(DeviceResourceState& resource) noexcept override;

    /// Returns a resource's memory.
    ///
    /// @param resource the record being destroyed.
    void releaseResource(DeviceResourceState& resource) noexcept override;

    /// See `Device::selectKernel`.
    ///
    /// @param execution the operation.
    /// @param plan the plan to place.
    /// @return the selection or the refusal.
    [[nodiscard]] Result<DeviceKernelSelection> selectKernel(const ExecutionContext& execution,
                                                             const KernelPlanKey& plan) const;

    /// See `Device::prepareKernel`.
    ///
    /// @param execution the operation.
    /// @param request what to prepare.
    /// @return the prepared kernel or the refusal.
    [[nodiscard]] Result<PreparedDeviceKernel> prepareKernel(const ExecutionContext& execution,
                                                             const DeviceKernelRequest& request);

    /// See `Device::submit`.
    ///
    /// @param execution the operation.
    /// @param submission what the work declares.
    /// @return the event or the refusal.
    [[nodiscard]] Result<DeviceEvent> submit(const ExecutionContext& execution, const DeviceSubmission& submission);

    /// @return a consistent copy of the counters.
    [[nodiscard]] DeviceCounters counters() const noexcept;

    /// See `Device::shutdown`.
    ///
    /// @return success, or the refusal while anything is outstanding.
    [[nodiscard]] Status shutdown();

    /// Counts one prepared kernel as released, and the device memory it held as returned.
    ///
    /// @param residentBytes what the kernel held, exactly as it was counted when it was admitted.
    void kernelReleased(std::uint64_t residentBytes) noexcept;

    /// Counts one backend defect the device contained.
    void countBackendDefect() noexcept;

    /// Accepts a backend's completion, refusing a success whose report is incomplete.
    ///
    /// @param record the work.
    /// @param outcome what the backend reported.
    void complete(const std::shared_ptr<DeviceWorkRecord>& record, Status outcome) noexcept;

    /// Gives a piece of work its outcome, once.
    ///
    /// @param record the work.
    /// @param outcome its outcome.
    void finish(const std::shared_ptr<DeviceWorkRecord>& record, Status outcome) noexcept;

    /// Moves a registered or waiting piece of work on, if it can be.
    ///
    /// @param record the work.
    static void advance(const std::shared_ptr<DeviceWorkRecord>& record) noexcept;

    /// Records a stop request for a piece of work, finishing it when it is waiting.
    ///
    /// @param record the work.
    static void stop(const std::shared_ptr<DeviceWorkRecord>& record) noexcept;

private:
    void handOff(const std::shared_ptr<DeviceWorkRecord>& record) noexcept;
    [[nodiscard]] Result<DeviceAllocation> allocate(const DeviceResourceDescriptor& descriptor, const char* operation);
    void releaseAllocation(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept;
    [[nodiscard]] Status checkDescriptor(const DeviceResourceDescriptor& descriptor, const char* operation) const;
    [[nodiscard]] const DeviceKernelSupport* findSupport(const KernelPlanKey& plan) const noexcept;
    [[nodiscard]] ExternalBoundary boundary(const char* operation) const noexcept;
    [[nodiscard]] Status refuse(Status refusal) noexcept;
    void sweepRetired() noexcept;

    std::shared_ptr<DeviceBackend> m_backend;
    DeviceCapabilities m_capabilities;
    std::uint64_t m_id;

    mutable std::mutex m_mutex;
    DeviceCounters m_counters{};
    std::uint64_t m_lastResourceId = 0u;
    std::uint64_t m_lastEventId = 0u;
    std::vector<std::shared_ptr<DeviceWorkRecord>> m_waiting;
    std::vector<std::shared_ptr<DeviceWorkRecord>> m_retired;
};

} // namespace an

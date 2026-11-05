// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - what one backend owns, shared with its worker thread and its kernels.
//
// A backend's state lives here rather than in the backend object, for the reason the device seam states:
// the last reference to a device can be released on one of the backend's own threads, when its worker
// completes the last piece of work of a device whose handle is already gone. The backend object is then
// destroyed on that worker, which cannot join itself - it detaches instead, and the worker keeps this core
// alive until it has left the loop it is in. A prepared kernel keeps the core too, so the context its
// program was built in outlives it.

#pragma once

#include "OpenClHeader.h"

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

/// The OpenCL device backend's implementation: the runtime loader, the backend's shared core, the
/// device backend itself and its kernels. Nothing in it is part of any published surface.
namespace an::opencl
{

/// The clock a step's duration falls back to when the driver reports no profiling information.
using Clock = std::chrono::steady_clock;

/// One allocation the backend holds.
struct Allocation
{
    /// Where its bytes live.
    ResourceResidency residency = ResourceResidency::Host;

    /// How many bytes it holds.
    std::uint64_t bytes = 0u;

    /// The alignment it was allocated with.
    std::uint64_t alignment = 1u;

    /// The buffer object, for pinned and device memory; null for host memory.
    cl_mem buffer = nullptr;

    /// The first host byte, for host memory and for pinned memory, which stays mapped for its whole life;
    /// null for device memory.
    std::byte* host = nullptr;

    /// Whether the engine currently has it mapped.
    bool mapped = false;
};

/// What one backend owns.
struct BackendCore
{
    BackendCore() = default;

    /// Releases every allocation, the affine and stencil programs, both queues and the context.
    ~BackendCore();

    BackendCore(const BackendCore&) = delete;
    BackendCore& operator=(const BackendCore&) = delete;

    /// The context the device's objects belong to.
    cl_context context = nullptr;

    /// The device.
    cl_device_id device = nullptr;

    /// The queue the worker runs every step on, with profiling on so each command reports its own duration.
    cl_command_queue workQueue = nullptr;

    /// The queue pinned memory is mapped and unmapped on, from whichever thread allocates or releases it.
    cl_command_queue mapQueue = nullptr;

    /// What the backend reported, kept for its whole life.
    DeviceCapabilities capabilities{};

    /// The affine kernel's contract, when the device computes in double precision.
    std::optional<KernelContract> affineContract{};

    /// Guards the allocation table.
    std::mutex allocationMutex;

    /// Every allocation, by the handle the engine was given.
    std::unordered_map<std::uint64_t, Allocation> allocations;

    /// The next handle; zero is never one.
    std::uint64_t nextHandle = 1u;

    /// Guards building the affine and stencil programs; a pointwise program kernel builds its own.
    std::mutex programMutex;

    /// The affine kernel's program, built when a kernel is first prepared.
    cl_program affineProgram = nullptr;

    /// The direct stencil's program, built when a stencil is first prepared.
    cl_program stencilProgram = nullptr;

    /// Guards the work queue and the stop flag.
    std::mutex workMutex;

    /// Signalled when work arrives or the backend stops.
    std::condition_variable workChanged;

    /// Work taken and not yet started.
    std::deque<DeviceWork> queuedWork;

    /// Set when the backend is destroyed.
    bool stopping = false;

    /// A copy of one allocation's record.
    ///
    /// @param allocation the handle.
    /// @return the record, or no value for a handle the backend does not hold.
    [[nodiscard]] std::optional<Allocation> findAllocation(DeviceAllocation allocation);
};

/// Returns one allocation's memory: unmaps and releases a pinned buffer, releases a device buffer, and
/// frees host memory.
///
/// @param core the backend the allocation belongs to.
/// @param allocation its record.
void releaseAllocation(BackendCore& core, const Allocation& allocation) noexcept;

/// How long ago `started` was, in nanoseconds.
///
/// @param started a point on the steady clock.
/// @return the nanoseconds since.
[[nodiscard]] std::uint64_t nanosecondsSince(Clock::time_point started) noexcept;

/// Waits for one command, checks that it succeeded, and reads how long the device spent on it.
///
/// Takes ownership of the event and releases it.
///
/// @param event the command's event.
/// @param call the OpenCL function that enqueued it, for a failure's message.
/// @param started when the call was made, for a duration when the driver reports no profiling
///        information.
/// @return the nanoseconds the command ran, or the failure it ended with.
[[nodiscard]] Result<std::uint64_t> finishCommand(cl_event event, const char* call, Clock::time_point started);

/// The driver's build log for a program, for a failure's message.
///
/// @param program the program whose build failed.
/// @param device the device it was built for.
/// @return the log, or `"no build log"` when the driver reports none.
[[nodiscard]] std::string programBuildLog(cl_program program, cl_device_id device);

} // namespace an::opencl

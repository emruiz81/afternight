// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - one OpenCL GPU as a device backend.
//
// The backend does the four things the seam leaves to a backend - reports what it offers, allocates and
// maps its memory, prepares its kernels, and runs work - and nothing the engine's `Device` already checks.
// It receives only work that passed every rule and whose dependencies all succeeded, and it runs that
// work on one worker thread of its own, a step at a time, completing each piece exactly once.

#pragma once

#include "OpenClBackendCore.h"
#include "OpenClRuntime.h"

#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace an::opencl
{

/// One OpenCL GPU behind the device seam.
class OpenClDeviceBackend final : public DeviceBackend
{
public:
    /// Creates the device's context and queues and starts the worker.
    ///
    /// @param device the device, from `enumerateGpuDevices`.
    /// @param memoryLimitBytes the device memory the backend reports it may hold at once.
    /// @return the backend; `ErrorCode::ExternalFailure` when the driver refuses the context or a queue;
    ///         or, on a device with double precision, the `openClAffineContract` failure.
    [[nodiscard]] static Result<std::shared_ptr<DeviceBackend>> create(const GpuDeviceHandle& device,
                                                                       std::uint64_t memoryLimitBytes);

    /// Stops the worker: joins it, or detaches it when this runs on the worker itself.
    ~OpenClDeviceBackend() override;

    OpenClDeviceBackend(const OpenClDeviceBackend&) = delete;
    OpenClDeviceBackend& operator=(const OpenClDeviceBackend&) = delete;

    /// What the device offers, fixed when the backend was created: host, pinned and device memory but
    /// no shared memory, asynchronous completion, the device's limits, and - on a device with double
    /// precision - the affine, stencil and pointwise program kernels.
    ///
    /// @return a copy of the capabilities.
    [[nodiscard]] DeviceCapabilities capabilities() const override;

    /// Allocates one resource: host memory from the aligned heap, pinned memory as a driver buffer in
    /// page-locked host memory mapped once for its whole life, device memory as a driver buffer. Host
    /// and pinned memory start zeroed.
    ///
    /// @param descriptor what to allocate.
    /// @return the allocation; `ErrorCode::AllocationFailed` when the host or the driver refuses the
    ///         memory, or the driver maps pinned memory at an address not aligned as asked; or
    ///         `ErrorCode::Unsupported` for shared memory, which this backend never offers.
    [[nodiscard]] Result<DeviceAllocation> allocate(const DeviceResourceDescriptor& descriptor) override;

    /// Returns an allocation's memory and forgets its handle; a handle the backend does not hold is
    /// ignored.
    ///
    /// @param allocation what `allocate` returned.
    /// @param descriptor what it was allocated with; unused, because the backend keeps its own record.
    void release(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept override;

    /// Hands the host the address of host or pinned memory. Nothing is enqueued: pinned memory has
    /// been mapped since it was allocated.
    ///
    /// @param allocation the allocation to map.
    /// @param descriptor what it was allocated with; unused.
    /// @param access what the host will do; unused, because the mapping allows both.
    /// @return the first byte; `ErrorCode::IncompatibleArguments` for device memory, which is never
    ///         mapped; `ErrorCode::InvalidState` for an allocation already mapped; or
    ///         `ErrorCode::Internal` for a handle the backend does not hold.
    [[nodiscard]] Result<std::byte*> map(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor,
                                         DeviceAccess access) override;

    /// Marks an allocation unmapped. Nothing is enqueued, for the reason `map` enqueues nothing.
    ///
    /// @param allocation the mapped allocation.
    /// @param descriptor what it was allocated with; unused.
    void unmap(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept override;

    /// Prepares the stencil or the pointwise program kernel when the plan names one, and the affine
    /// kernel for any other plan, which refuses a plan its contract does not cover.
    ///
    /// @param request what to prepare.
    /// @return the kernel, or the preparing function's failure.
    [[nodiscard]] Result<std::shared_ptr<const DeviceKernel>>
    prepareKernel(const DeviceKernelRequest& request) override;

    /// Queues the work for the worker thread and returns at once. Safe from inside
    /// `DeviceWork::complete` on the worker, because the queue's lock is never held while work runs.
    ///
    /// @param work the work to run, taken in every case.
    /// @return success.
    [[nodiscard]] Status enqueue(DeviceWork&& work) override;

private:
    /// Takes a core whose context and queues exist, and starts the worker over it.
    explicit OpenClDeviceBackend(std::shared_ptr<BackendCore> core);

    std::shared_ptr<BackendCore> m_core;
    std::thread m_worker;
};

} // namespace an::opencl

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - one OpenCL GPU as a device backend. See OpenClDeviceBackend.h.

#include "OpenClDeviceBackend.h"

#include "OpenClAffineKernel.h"
#include "OpenClKernel.h"
#include "OpenClPointwiseProgramKernel.h"
#include "OpenClStencil3x3Kernel.h"

#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <array>
#include <cstring>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace an::opencl
{

namespace
{

constexpr std::uint64_t kMaximumAlignmentBytes = 4096u;
/// Every source of the widest pointwise program, and its destination.
constexpr std::uint32_t kMaximumBindings = kMaxPointwiseSources + 1u;
constexpr std::uint32_t kMaximumTransfers = 16u;
constexpr std::uint32_t kMaximumDependencies = 16u;

/// Copies one declared transfer's bytes the way their two residencies require, and reports how long it
/// took.
[[nodiscard]] Result<std::uint64_t> performTransfer(BackendCore& core, const DeviceTransferStep& step)
{
    const std::optional<Allocation> source = core.findAllocation(step.source);
    const std::optional<Allocation> destination = core.findAllocation(step.destination);
    if (!source.has_value() || !destination.has_value())
    {
        return backendFailure(ErrorCode::Internal, "the OpenCL device was handed an allocation it does not hold");
    }
    const auto sourceOffset = static_cast<std::size_t>(step.sourceOffset);
    const auto destinationOffset = static_cast<std::size_t>(step.destinationOffset);
    const auto bytes = static_cast<std::size_t>(step.bytes);
    const bool fromDevice = source->residency == ResourceResidency::Device;
    const bool toDevice = destination->residency == ResourceResidency::Device;
    const Clock::time_point started = Clock::now();

    if (!fromDevice && !toDevice)
    {
        std::memmove(destination->host + destinationOffset, source->host + sourceOffset, bytes);
        return nanosecondsSince(started);
    }

    cl_event event = nullptr;
    cl_int status = CL_SUCCESS;
    const char* call = nullptr;
    if (fromDevice && toDevice)
    {
        call = "clEnqueueCopyBuffer";
        status = clEnqueueCopyBuffer(core.workQueue, source->buffer, destination->buffer, sourceOffset,
                                     destinationOffset, bytes, 0u, nullptr, &event);
    }
    else if (toDevice)
    {
        call = "clEnqueueWriteBuffer";
        status = clEnqueueWriteBuffer(core.workQueue, destination->buffer, CL_TRUE, destinationOffset, bytes,
                                      source->host + sourceOffset, 0u, nullptr, &event);
    }
    else
    {
        call = "clEnqueueReadBuffer";
        status = clEnqueueReadBuffer(core.workQueue, source->buffer, CL_TRUE, sourceOffset, bytes,
                                     destination->host + destinationOffset, 0u, nullptr, &event);
    }
    if (status != CL_SUCCESS)
    {
        if (event != nullptr)
        {
            (void)clReleaseEvent(event);
        }
        return callFailure(ErrorCode::ExternalFailure, call, status);
    }
    return finishCommand(event, call, started);
}

/// Performs one ordered list of transfers, observing cancellation before each.
[[nodiscard]] Status performTransfers(BackendCore& core, DeviceWork& work, std::span<const DeviceTransferStep> steps)
{
    for (const DeviceTransferStep& step : steps)
    {
        Status status = work.checkCancellation();
        if (status.failed())
        {
            return status;
        }
        const Result<std::uint64_t> performed = performTransfer(core, step);
        if (!performed.hasValue())
        {
            return performed.status();
        }
        work.recordTransfer(*performed);
    }
    return Status::success();
}

/// Runs one piece of work's steps in order and says how it ended, without completing it.
[[nodiscard]] Status runWork(BackendCore& core, DeviceWork& work) noexcept
{
    try
    {
        Status status = performTransfers(core, work, work.transfersBefore());
        if (status.failed())
        {
            return status;
        }
        if (const DeviceKernel* const kernel = work.kernel(); kernel != nullptr)
        {
            status = work.checkCancellation();
            if (status.failed())
            {
                return status;
            }
            // Every kernel this backend receives is one it prepared, which the seam guarantees.
            const Result<std::uint64_t> ran =
                static_cast<const OpenClKernel&>(*kernel).run(work.bindings(), work.scratch());
            if (!ran.hasValue())
            {
                return ran.status();
            }
            work.recordKernel(*ran);
        }
        return performTransfers(core, work, work.transfersAfter());
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    catch (...)
    {
        return Status::failure(ErrorCode::Internal);
    }
}

void execute(BackendCore& core, DeviceWork work)
{
    work.markRunning();
    Status outcome = runWork(core, work);
    // Completing can release the device's last reference, and so destroy the backend, on this thread.
    // Nothing after this line touches the backend object; the caller holds the core.
    work.complete(std::move(outcome));
}

void runWorker(const std::shared_ptr<BackendCore>& core)
{
    std::unique_lock<std::mutex> lock(core->workMutex);
    for (;;)
    {
        core->workChanged.wait(lock, [&core] { return core->stopping || !core->queuedWork.empty(); });
        if (core->stopping)
        {
            return;
        }
        DeviceWork work = std::move(core->queuedWork.front());
        core->queuedWork.pop_front();
        lock.unlock();
        execute(*core, std::move(work));
        lock.lock();
    }
}

} // namespace

Result<std::shared_ptr<DeviceBackend>> OpenClDeviceBackend::create(const GpuDeviceHandle& device,
                                                                   std::uint64_t memoryLimitBytes)
{
    auto core = std::make_shared<BackendCore>();
    core->device = device.device;

    cl_int status = CL_SUCCESS;
    const std::array<cl_context_properties, 3u> properties{CL_CONTEXT_PLATFORM,
                                                           reinterpret_cast<cl_context_properties>(device.platform), 0};
    core->context = clCreateContext(properties.data(), 1u, &device.device, nullptr, nullptr, &status);
    if (status != CL_SUCCESS || core->context == nullptr)
    {
        return callFailure(ErrorCode::ExternalFailure, "clCreateContext", status);
    }
    core->workQueue = clCreateCommandQueue(core->context, device.device, CL_QUEUE_PROFILING_ENABLE, &status);
    if (status != CL_SUCCESS || core->workQueue == nullptr)
    {
        return callFailure(ErrorCode::ExternalFailure, "clCreateCommandQueue", status);
    }
    core->mapQueue = clCreateCommandQueue(core->context, device.device, 0u, &status);
    if (status != CL_SUCCESS || core->mapQueue == nullptr)
    {
        return callFailure(ErrorCode::ExternalFailure, "clCreateCommandQueue", status);
    }

    if (device.description.doublePrecision)
    {
        const Result<KernelContract> contract = openClAffineContract();
        if (!contract.hasValue())
        {
            return contract.status();
        }
        core->affineContract = *contract;
    }

    DeviceCapabilities& capabilities = core->capabilities;
    capabilities.name = "OpenCL " + device.description.deviceName;
    capabilities.hostResidency = true;
    capabilities.pinnedResidency = true;
    capabilities.sharedResidency = false;
    capabilities.deviceResidency = true;
    capabilities.asynchronous = true;
    capabilities.memoryLimitBytes = memoryLimitBytes;
    capabilities.maximumResourceBytes = device.description.maximumAllocationBytes;
    capabilities.maximumAlignmentBytes = kMaximumAlignmentBytes;
    capabilities.maximumBindings = kMaximumBindings;
    capabilities.maximumTransfers = kMaximumTransfers;
    capabilities.maximumDependencies = kMaximumDependencies;
    if (core->affineContract.has_value())
    {
        capabilities.kernels.push_back(
            DeviceKernelSupport{.contract = *core->affineContract, .determinism = KernelDeterminism::Reproducible});
        capabilities.kernels.push_back(DeviceKernelSupport{.contract = stencil3x3KernelContract(),
                                                           .determinism = KernelDeterminism::Reproducible});
    }
    // The program kernel computes in Float32, and rounds a curve's quotient through double precision.
    if (device.description.doublePrecision)
    {
        for (const KernelContract& contract : pointwiseProgramContracts())
        {
            capabilities.kernels.push_back(
                DeviceKernelSupport{.contract = contract, .determinism = KernelDeterminism::Reproducible});
        }
    }

    return std::shared_ptr<DeviceBackend>(new OpenClDeviceBackend(std::move(core)));
}

OpenClDeviceBackend::OpenClDeviceBackend(std::shared_ptr<BackendCore> core) : m_core(std::move(core))
{
    m_worker = std::thread([shared = m_core] { runWorker(shared); });
}

OpenClDeviceBackend::~OpenClDeviceBackend()
{
    {
        const std::lock_guard<std::mutex> held(m_core->workMutex);
        m_core->stopping = true;
    }
    m_core->workChanged.notify_all();
    if (m_worker.joinable())
    {
        // Destroyed on its own worker when that worker released the device's last reference: joining
        // would wait for itself, so it detaches, and the worker - which holds the core - leaves its loop
        // without touching this object again.
        if (m_worker.get_id() == std::this_thread::get_id())
        {
            m_worker.detach();
        }
        else
        {
            m_worker.join();
        }
    }
}

DeviceCapabilities OpenClDeviceBackend::capabilities() const
{
    return m_core->capabilities;
}

Result<DeviceAllocation> OpenClDeviceBackend::allocate(const DeviceResourceDescriptor& descriptor)
{
    Allocation allocation{
        .residency = descriptor.residency, .bytes = descriptor.bytes, .alignment = descriptor.alignment};
    const auto bytes = static_cast<std::size_t>(descriptor.bytes);
    cl_int status = CL_SUCCESS;
    switch (descriptor.residency)
    {
    case ResourceResidency::Host:
        allocation.host = static_cast<std::byte*>(
            ::operator new(bytes, std::align_val_t{static_cast<std::size_t>(descriptor.alignment)}, std::nothrow));
        if (allocation.host == nullptr)
        {
            return backendFailure(ErrorCode::AllocationFailed, "the host refused the memory for a host resource");
        }
        std::memset(allocation.host, 0, bytes);
        break;
    case ResourceResidency::Pinned: {
        // A buffer the driver allocates in page-locked host memory, mapped once for its whole life: the
        // mapped address is what the host reads and writes, and what a transfer to or from the device
        // copies without staging.
        allocation.buffer =
            clCreateBuffer(m_core->context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, bytes, nullptr, &status);
        if (status != CL_SUCCESS || allocation.buffer == nullptr)
        {
            return callFailure(ErrorCode::AllocationFailed, "clCreateBuffer", status);
        }
        void* const mapped = clEnqueueMapBuffer(m_core->mapQueue, allocation.buffer, CL_TRUE,
                                                CL_MAP_READ | CL_MAP_WRITE, 0u, bytes, 0u, nullptr, nullptr, &status);
        if (status != CL_SUCCESS || mapped == nullptr)
        {
            (void)clReleaseMemObject(allocation.buffer);
            return callFailure(ErrorCode::AllocationFailed, "clEnqueueMapBuffer", status);
        }
        allocation.host = static_cast<std::byte*>(mapped);
        if (reinterpret_cast<std::uintptr_t>(mapped) % descriptor.alignment != 0u)
        {
            releaseAllocation(*m_core, allocation);
            return backendFailure(ErrorCode::AllocationFailed,
                                  "the driver mapped pinned memory at an address that is not aligned to " +
                                      std::to_string(descriptor.alignment) + " bytes");
        }
        std::memset(allocation.host, 0, bytes);
        break;
    }
    case ResourceResidency::Device:
        allocation.buffer = clCreateBuffer(m_core->context, CL_MEM_READ_WRITE, bytes, nullptr, &status);
        if (status != CL_SUCCESS || allocation.buffer == nullptr)
        {
            return callFailure(ErrorCode::AllocationFailed, "clCreateBuffer", status);
        }
        break;
    case ResourceResidency::Shared:
        return backendFailure(ErrorCode::Unsupported, "this OpenCL device offers no shared memory");
    }

    try
    {
        const std::lock_guard<std::mutex> held(m_core->allocationMutex);
        const std::uint64_t handle = m_core->nextHandle++;
        m_core->allocations.emplace(handle, allocation);
        return DeviceAllocation{.handle = handle};
    }
    catch (...)
    {
        releaseAllocation(*m_core, allocation);
        throw;
    }
}

void OpenClDeviceBackend::release(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept
{
    (void)descriptor;
    std::optional<Allocation> removed;
    {
        const std::lock_guard<std::mutex> held(m_core->allocationMutex);
        const auto found = m_core->allocations.find(allocation.handle);
        if (found == m_core->allocations.end())
        {
            return;
        }
        removed = found->second;
        m_core->allocations.erase(found);
    }
    releaseAllocation(*m_core, *removed);
}

Result<std::byte*> OpenClDeviceBackend::map(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor,
                                            DeviceAccess access)
{
    (void)descriptor;
    (void)access;
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    const auto found = m_core->allocations.find(allocation.handle);
    if (found == m_core->allocations.end())
    {
        return backendFailure(ErrorCode::Internal, "the device mapped an allocation the OpenCL backend does not hold");
    }
    if (found->second.host == nullptr)
    {
        return backendFailure(ErrorCode::IncompatibleArguments, "the OpenCL backend never maps device memory");
    }
    if (found->second.mapped)
    {
        return backendFailure(ErrorCode::InvalidState, "the device mapped an allocation twice");
    }
    found->second.mapped = true;
    return found->second.host;
}

void OpenClDeviceBackend::unmap(DeviceAllocation allocation, const DeviceResourceDescriptor& descriptor) noexcept
{
    (void)descriptor;
    const std::lock_guard<std::mutex> held(m_core->allocationMutex);
    const auto found = m_core->allocations.find(allocation.handle);
    if (found != m_core->allocations.end())
    {
        found->second.mapped = false;
    }
}

Result<std::shared_ptr<const DeviceKernel>> OpenClDeviceBackend::prepareKernel(const DeviceKernelRequest& request)
{
    if (request.plan.id == kStencil3x3KernelId)
    {
        return prepareStencil3x3Kernel(m_core, request);
    }
    if (request.plan.id == kPointwiseProgramKernelId)
    {
        return preparePointwiseProgramKernel(m_core, request);
    }
    return prepareAffineKernel(m_core, request);
}

Status OpenClDeviceBackend::enqueue(DeviceWork&& work)
{
    // Safe from inside `DeviceWork::complete` on the worker: the work lock is never held while work runs.
    const std::shared_ptr<BackendCore> core = m_core;
    {
        const std::lock_guard<std::mutex> held(core->workMutex);
        core->queuedWork.push_back(std::move(work));
    }
    core->workChanged.notify_one();
    return Status::success();
}

} // namespace an::opencl

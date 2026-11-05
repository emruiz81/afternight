// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Kernel/Device.h>

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/DeviceError.h>

#include "kernels/reference/DeviceState.h"

#include <atomic>
#include <new>
#include <optional>
#include <utility>

// The public face of a device: the factory that validates what a backend says it offers, and the thin
// members that hand every call to the device's record. What a device does is in DeviceState.cpp.

namespace an
{

namespace
{

/// The identity for the next device this process creates.
///
/// A counter rather than an address, so an identity is never reused by a later device - which is what a
/// resource or an event naming its device by identity depends on. It owns nothing: when every device is
/// gone, what is left is a number.
///
/// @return an identity no other device has held, counting from one.
[[nodiscard]] std::uint64_t nextDeviceId() noexcept
{
    static std::atomic<std::uint64_t> counter{0u};
    return counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

[[nodiscard]] Status noDevice(const char* operation)
{
    return deviceFailure(ErrorCode::NotInitialized, DeviceFailure::DeviceShutDown,
                         StatusDetail{.message = "the device handle holds nothing", .operation = operation});
}

/// Whether a backend's capability report describes a device work can be planned against.
[[nodiscard]] Status checkCapabilities(const DeviceCapabilities& capabilities)
{
    const auto incoherent = [](const char* message, const char* parameter) {
        return deviceFailure(ErrorCode::InvalidArgument, DeviceFailure::InvalidCapabilities,
                             StatusDetail{.message = message,
                                          .operation = "Device::create",
                                          .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
    };
    if (capabilities.name.empty())
    {
        return incoherent("a device backend must name itself", "name");
    }
    if (!capabilities.hostResidency && !capabilities.pinnedResidency && !capabilities.sharedResidency &&
        !capabilities.deviceResidency)
    {
        return incoherent("a device backend must create at least one residency", "residencies");
    }
    if (!capabilities.sharedResidency && !capabilities.deviceResidency)
    {
        return incoherent("a device backend must create memory a kernel can bind: shared or device memory",
                          "residencies");
    }
    if (capabilities.memoryLimitBytes == 0u)
    {
        return incoherent("a device's memory limit must be nonzero", "memoryLimitBytes");
    }
    if (capabilities.maximumResourceBytes == 0u)
    {
        return incoherent("a device's largest resource must be nonzero", "maximumResourceBytes");
    }
    if (!isPowerOfTwo(capabilities.maximumAlignmentBytes))
    {
        return incoherent("a device's widest alignment must be a power of two", "maximumAlignmentBytes");
    }
    if (capabilities.maximumBindings == 0u)
    {
        return incoherent("a device must accept at least one binding", "maximumBindings");
    }
    if (capabilities.maximumTransfers == 0u)
    {
        return incoherent("a device must accept at least one transfer", "maximumTransfers");
    }
    for (const DeviceKernelSupport& support : capabilities.kernels)
    {
        if (!isKernelId(support.contract.id))
        {
            return incoherent("a device kernel names no known operation", "kernels");
        }
        if (support.determinism != KernelDeterminism::Reproducible &&
            support.determinism != KernelDeterminism::WithinAccuracy)
        {
            return incoherent("a device kernel states a determinism outside the enumeration", "kernels");
        }
    }
    return Status::success();
}

} // namespace

Result<Device> Device::create(std::shared_ptr<DeviceBackend> backend)
{
    constexpr const char* kOperation = "Device::create";
    if (backend == nullptr)
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = "a device needs a backend",
                                            .operation = kOperation,
                                            .domain = ErrorDomain::Runtime,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "backend")}});
    }
    try
    {
        std::optional<DeviceCapabilities> reported;
        const Status thrown = callAcrossBoundary(
            ExternalBoundary{.library = "device backend", .operation = kOperation, .domain = ErrorDomain::Runtime},
            [&backend, &reported] { reported.emplace(backend->capabilities()); });
        if (thrown.failed())
        {
            return deviceFailure(
                thrown.code(), DeviceFailure::BackendFailure,
                StatusDetail{.message = "the backend threw while reporting its capabilities", .operation = kOperation},
                thrown);
        }
        Status incoherent = checkCapabilities(*reported);
        if (incoherent.failed())
        {
            return incoherent;
        }
        return Device(std::make_shared<DeviceState>(nextDeviceId(), std::move(backend), std::move(*reported)));
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

std::uint64_t Device::id() const noexcept
{
    return m_state != nullptr ? m_state->id() : 0u;
}

const DeviceCapabilities* Device::capabilities() const noexcept
{
    return m_state != nullptr ? &m_state->capabilities() : nullptr;
}

Result<DeviceResource> Device::createResource(const DeviceResourceDescriptor& descriptor) const
{
    if (m_state == nullptr)
    {
        return noDevice("Device::createResource");
    }
    try
    {
        return m_state->createResource(descriptor);
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<DeviceKernelSelection> Device::selectKernel(const ExecutionContext& execution, const KernelPlanKey& plan) const
{
    if (m_state == nullptr)
    {
        return noDevice("Device::selectKernel");
    }
    try
    {
        return m_state->selectKernel(execution, plan);
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<PreparedDeviceKernel> Device::prepareKernel(const ExecutionContext& execution,
                                                   const DeviceKernelRequest& request) const
{
    if (m_state == nullptr)
    {
        return noDevice("Device::prepareKernel");
    }
    try
    {
        return m_state->prepareKernel(execution, request);
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

Result<DeviceEvent> Device::submit(const ExecutionContext& execution, const DeviceSubmission& submission) const
{
    if (m_state == nullptr)
    {
        return noDevice("Device::submit");
    }
    try
    {
        return m_state->submit(execution, submission);
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

DeviceCounters Device::counters() const noexcept
{
    return m_state != nullptr ? m_state->counters() : DeviceCounters{};
}

Status Device::shutdown()
{
    if (m_state == nullptr)
    {
        return Status::success();
    }
    Status refused = m_state->shutdown();
    if (refused.failed())
    {
        return refused;
    }
    m_state.reset();
    return Status::success();
}

std::uint64_t PreparedDeviceKernel::deviceId() const noexcept
{
    return m_record != nullptr ? m_record->device()->id() : 0u;
}

const DeviceKernel* PreparedDeviceKernel::kernel() const noexcept
{
    return m_record != nullptr ? &m_record->kernel() : nullptr;
}

} // namespace an

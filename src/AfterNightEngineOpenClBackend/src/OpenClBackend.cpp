// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - the published entry points: the runtime report, the kernel's
// contract, and creating a backend. See OpenClBackend.h.

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>

#include "OpenClDeviceBackend.h"
#include "OpenClRuntime.h"

#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <new>
#include <string>
#include <utility>
#include <vector>

namespace an
{

const char* openClRuntimeStateName(OpenClRuntimeState state) noexcept
{
    switch (state)
    {
    case OpenClRuntimeState::LoaderNotInstalled:
        return "LoaderNotInstalled";
    case OpenClRuntimeState::NoGpuDevice:
        return "NoGpuDevice";
    case OpenClRuntimeState::GpuDevicesFound:
        return "GpuDevicesFound";
    }
    return "UnknownOpenClRuntimeState";
}

OpenClRuntimeReport openClRuntimeReport()
{
    OpenClRuntimeReport report;
    if (!opencl::loadSystemLoader())
    {
        return report;
    }
    report.loaderPath = opencl::systemLoaderPath();
    for (opencl::GpuDeviceHandle& device : opencl::enumerateGpuDevices())
    {
        report.devices.push_back(std::move(device.description));
    }
    report.state = report.devices.empty() ? OpenClRuntimeState::NoGpuDevice : OpenClRuntimeState::GpuDevicesFound;
    return report;
}

Result<KernelContract> openClAffineContract()
{
    for (const KernelRegistration& registration : kernelRegistry())
    {
        if (registration.contract.id.operation == KernelOperation::PointwiseAffine &&
            registration.backend == ComputeBackend::Reference)
        {
            KernelContract contract = registration.contract;
            // The device kernel has no validity rule and no mask, and a stop is observed between a piece
            // of work's steps rather than within the kernel; everything else is the reference's.
            contract.capabilities.validity = KernelValidityCapability::AllValidOnly;
            contract.capabilities.cancellation = KernelCancellationPolicy::None;
            return contract;
        }
    }
    return Status::failure(ErrorCode::Internal, "the engine's kernel registry holds no reference affine operation");
}

Result<std::shared_ptr<DeviceBackend>> createOpenClBackend(const OpenClBackendOptions& options)
{
    try
    {
        if (!opencl::loadSystemLoader())
        {
            return opencl::backendFailure(ErrorCode::Unsupported,
                                          "no OpenCL loader is installed in the operating system's directory; a "
                                          "graphics driver installs one");
        }
        const std::vector<opencl::GpuDeviceHandle> devices = opencl::enumerateGpuDevices();
        if (devices.empty())
        {
            return opencl::backendFailure(ErrorCode::Unsupported, "the OpenCL loader reports no GPU device");
        }
        if (options.deviceIndex >= devices.size())
        {
            return opencl::backendFailure(ErrorCode::OutOfRange,
                                          "the requested OpenCL device index is past the last GPU device",
                                          {StatusContextField::ofCount(ContextKey::Requested, options.deviceIndex),
                                           StatusContextField::ofCount(ContextKey::Available, devices.size())});
        }
        const opencl::GpuDeviceHandle& device = devices[options.deviceIndex];
        if (!device.description.meetsApiFloor)
        {
            return opencl::backendFailure(ErrorCode::Unsupported, device.description.deviceName + " reports " +
                                                                      device.description.deviceVersion +
                                                                      ", below the OpenCL 1.2 this backend calls");
        }
        const std::uint64_t deviceMemory = device.description.globalMemoryBytes;
        if (options.memoryLimitBytes > deviceMemory)
        {
            return opencl::backendFailure(ErrorCode::InvalidArgument,
                                          "the memory limit is above the device's own memory",
                                          {StatusContextField::ofCount(ContextKey::Requested, options.memoryLimitBytes),
                                           StatusContextField::ofCount(ContextKey::Limit, deviceMemory)});
        }
        return opencl::OpenClDeviceBackend::create(device, options.memoryLimitBytes == 0u ? deviceMemory
                                                                                          : options.memoryLimitBytes);
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - the state one backend owns. See OpenClBackendCore.h.

#include "OpenClBackendCore.h"

#include "OpenClRuntime.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <cstring>
#include <new>
#include <string>

namespace an::opencl
{

BackendCore::~BackendCore()
{
    for (const auto& entry : allocations)
    {
        releaseAllocation(*this, entry.second);
    }
    allocations.clear();
    if (affineProgram != nullptr)
    {
        (void)clReleaseProgram(affineProgram);
    }
    if (stencilProgram != nullptr)
    {
        (void)clReleaseProgram(stencilProgram);
    }
    if (workQueue != nullptr)
    {
        (void)clFinish(workQueue);
        (void)clReleaseCommandQueue(workQueue);
    }
    if (mapQueue != nullptr)
    {
        (void)clFinish(mapQueue);
        (void)clReleaseCommandQueue(mapQueue);
    }
    if (context != nullptr)
    {
        (void)clReleaseContext(context);
    }
}

std::optional<Allocation> BackendCore::findAllocation(DeviceAllocation allocation)
{
    const std::lock_guard<std::mutex> held(allocationMutex);
    const auto found = allocations.find(allocation.handle);
    if (found == allocations.end())
    {
        return std::nullopt;
    }
    return found->second;
}

void releaseAllocation(BackendCore& core, const Allocation& allocation) noexcept
{
    if (allocation.residency == ResourceResidency::Pinned && allocation.buffer != nullptr &&
        allocation.host != nullptr && core.mapQueue != nullptr)
    {
        (void)clEnqueueUnmapMemObject(core.mapQueue, allocation.buffer, allocation.host, 0u, nullptr, nullptr);
        (void)clFinish(core.mapQueue);
    }
    if (allocation.buffer != nullptr)
    {
        (void)clReleaseMemObject(allocation.buffer);
    }
    if (allocation.residency == ResourceResidency::Host && allocation.host != nullptr)
    {
        ::operator delete(static_cast<void*>(allocation.host),
                          std::align_val_t{static_cast<std::size_t>(allocation.alignment)});
    }
}

std::uint64_t nanosecondsSince(Clock::time_point started) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
}

Result<std::uint64_t> finishCommand(cl_event event, const char* call, Clock::time_point started)
{
    const cl_int waited = clWaitForEvents(1u, &event);
    cl_int executed = CL_COMPLETE;
    const cl_int queried =
        clGetEventInfo(event, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(executed), &executed, nullptr);
    cl_ulong start = 0u;
    cl_ulong end = 0u;
    const bool profiled =
        clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(start), &start, nullptr) == CL_SUCCESS &&
        clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(end), &end, nullptr) == CL_SUCCESS &&
        end >= start;
    (void)clReleaseEvent(event);

    if (queried == CL_SUCCESS && executed < 0)
    {
        return callFailure(ErrorCode::ExternalFailure, call, executed);
    }
    if (waited != CL_SUCCESS)
    {
        return callFailure(ErrorCode::ExternalFailure, "clWaitForEvents", waited);
    }
    if (queried != CL_SUCCESS)
    {
        return callFailure(ErrorCode::ExternalFailure, "clGetEventInfo", queried);
    }
    return profiled ? static_cast<std::uint64_t>(end - start) : nanosecondsSince(started);
}

std::string programBuildLog(cl_program program, cl_device_id device)
{
    std::size_t size = 0u;
    if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0u, nullptr, &size) != CL_SUCCESS || size == 0u)
    {
        return "no build log";
    }
    std::string log(size, '\0');
    if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, size, log.data(), nullptr) != CL_SUCCESS)
    {
        return "no build log";
    }
    log.resize(std::strlen(log.c_str()));
    return log;
}

} // namespace an::opencl

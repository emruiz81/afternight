// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - loading the operating system's OpenCL loader and asking it what it
// reaches. See OpenClRuntime.h.

#include "OpenClRuntime.h"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>

#include <charconv>
#include <string_view>
#include <system_error>
#include <utility>

namespace an::opencl
{

namespace
{

/// The loader module: loaded on first use, from the operating system's directory only, and never
/// unloaded.
///
/// A function-local static rather than a member of anything, on purpose. A loaded library is a property
/// of the process, and every delay-loaded import this backend makes resolves against this one module, so
/// nothing narrower than the process could own it.
[[nodiscard]] HMODULE loaderModule() noexcept
{
    static const HMODULE kModule = LoadLibraryExW(L"OpenCL.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return kModule;
}

[[nodiscard]] std::string utf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    const auto length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string converted(static_cast<std::size_t>(size), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, text.data(), length, converted.data(), size, nullptr, nullptr);
    return converted;
}

/// One text property of a platform, without its terminator; empty when the query fails.
[[nodiscard]] std::string platformText(cl_platform_id platform, cl_platform_info info)
{
    std::size_t size = 0u;
    if (clGetPlatformInfo(platform, info, 0u, nullptr, &size) != CL_SUCCESS || size == 0u)
    {
        return {};
    }
    std::string value(size, '\0');
    if (clGetPlatformInfo(platform, info, size, value.data(), nullptr) != CL_SUCCESS)
    {
        return {};
    }
    value.resize(std::string_view(value.c_str()).size());
    return value;
}

/// One text property of a device, without its terminator; empty when the query fails.
[[nodiscard]] std::string deviceText(cl_device_id device, cl_device_info info)
{
    std::size_t size = 0u;
    if (clGetDeviceInfo(device, info, 0u, nullptr, &size) != CL_SUCCESS || size == 0u)
    {
        return {};
    }
    std::string value(size, '\0');
    if (clGetDeviceInfo(device, info, size, value.data(), nullptr) != CL_SUCCESS)
    {
        return {};
    }
    value.resize(std::string_view(value.c_str()).size());
    return value;
}

/// One fixed-size property of a device.
template <typename ValueT>
[[nodiscard]] bool deviceValue(cl_device_id device, cl_device_info info, ValueT& value) noexcept
{
    return clGetDeviceInfo(device, info, sizeof(ValueT), &value, nullptr) == CL_SUCCESS;
}

/// Reads `OpenCL <major>.<minor>` from the start of a version string, leaving zeroes when it is not
/// there.
void readVersion(std::string_view text, std::uint32_t& major, std::uint32_t& minor) noexcept
{
    constexpr std::string_view kPrefix = "OpenCL ";
    major = 0u;
    minor = 0u;
    if (!text.starts_with(kPrefix))
    {
        return;
    }
    text.remove_prefix(kPrefix.size());
    const char* const end = text.data() + text.size();
    std::uint32_t parsedMajor = 0u;
    const std::from_chars_result majorRead = std::from_chars(text.data(), end, parsedMajor);
    if (majorRead.ec != std::errc{} || majorRead.ptr == end || *majorRead.ptr != '.')
    {
        return;
    }
    std::uint32_t parsedMinor = 0u;
    const std::from_chars_result minorRead = std::from_chars(majorRead.ptr + 1, end, parsedMinor);
    if (minorRead.ec != std::errc{})
    {
        return;
    }
    major = parsedMajor;
    minor = parsedMinor;
}

[[nodiscard]] OpenClGpuDevice describe(cl_platform_id platform, cl_device_id device)
{
    OpenClGpuDevice description;
    description.platformName = platformText(platform, CL_PLATFORM_NAME);
    description.platformVersion = platformText(platform, CL_PLATFORM_VERSION);
    description.deviceName = deviceText(device, CL_DEVICE_NAME);
    description.deviceVersion = deviceText(device, CL_DEVICE_VERSION);
    description.driverVersion = deviceText(device, CL_DRIVER_VERSION);
    readVersion(description.deviceVersion, description.versionMajor, description.versionMinor);

    cl_ulong memory = 0u;
    if (deviceValue(device, CL_DEVICE_GLOBAL_MEM_SIZE, memory))
    {
        description.globalMemoryBytes = memory;
    }
    cl_ulong largest = 0u;
    if (deviceValue(device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, largest))
    {
        description.maximumAllocationBytes = largest;
    }

    // OpenCL 1.2 states double precision as a floating-point configuration; an earlier device can only
    // say so through its extension list.
    cl_device_fp_config doubleConfiguration = 0u;
    description.doublePrecision =
        (deviceValue(device, CL_DEVICE_DOUBLE_FP_CONFIG, doubleConfiguration) && doubleConfiguration != 0u) ||
        deviceText(device, CL_DEVICE_EXTENSIONS).find("cl_khr_fp64") != std::string::npos;

    cl_bool unified = CL_FALSE;
    description.hostUnifiedMemory = deviceValue(device, CL_DEVICE_HOST_UNIFIED_MEMORY, unified) && unified == CL_TRUE;
    description.meetsApiFloor =
        description.versionMajor > 1u || (description.versionMajor == 1u && description.versionMinor >= 2u);
    return description;
}

} // namespace

bool loadSystemLoader() noexcept
{
    return loaderModule() != nullptr;
}

std::string systemLoaderPath()
{
    const HMODULE module = loaderModule();
    if (module == nullptr)
    {
        return {};
    }
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD written = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (written == 0u)
        {
            return {};
        }
        if (written < path.size())
        {
            path.resize(written);
            return utf8(path);
        }
        path.resize(path.size() * 2u);
    }
}

std::vector<GpuDeviceHandle> enumerateGpuDevices()
{
    std::vector<GpuDeviceHandle> found;
    if (!loadSystemLoader())
    {
        return found;
    }
    cl_uint platformCount = 0u;
    if (clGetPlatformIDs(0u, nullptr, &platformCount) != CL_SUCCESS || platformCount == 0u)
    {
        return found;
    }
    std::vector<cl_platform_id> platforms(platformCount);
    if (clGetPlatformIDs(platformCount, platforms.data(), nullptr) != CL_SUCCESS)
    {
        return found;
    }
    for (const cl_platform_id platform : platforms)
    {
        cl_uint deviceCount = 0u;
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0u, nullptr, &deviceCount) != CL_SUCCESS || deviceCount == 0u)
        {
            continue;
        }
        std::vector<cl_device_id> devices(deviceCount);
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, deviceCount, devices.data(), nullptr) != CL_SUCCESS)
        {
            continue;
        }
        for (const cl_device_id device : devices)
        {
            found.push_back(
                GpuDeviceHandle{.platform = platform, .device = device, .description = describe(platform, device)});
        }
    }
    return found;
}

const char* statusName(cl_int status) noexcept
{
#define AN_OPENCL_STATUS_NAME(name) \
    case name:                      \
        return #name;
    switch (status)
    {
        AN_OPENCL_STATUS_NAME(CL_SUCCESS)
        AN_OPENCL_STATUS_NAME(CL_DEVICE_NOT_FOUND)
        AN_OPENCL_STATUS_NAME(CL_DEVICE_NOT_AVAILABLE)
        AN_OPENCL_STATUS_NAME(CL_COMPILER_NOT_AVAILABLE)
        AN_OPENCL_STATUS_NAME(CL_MEM_OBJECT_ALLOCATION_FAILURE)
        AN_OPENCL_STATUS_NAME(CL_OUT_OF_RESOURCES)
        AN_OPENCL_STATUS_NAME(CL_OUT_OF_HOST_MEMORY)
        AN_OPENCL_STATUS_NAME(CL_PROFILING_INFO_NOT_AVAILABLE)
        AN_OPENCL_STATUS_NAME(CL_MEM_COPY_OVERLAP)
        AN_OPENCL_STATUS_NAME(CL_IMAGE_FORMAT_MISMATCH)
        AN_OPENCL_STATUS_NAME(CL_IMAGE_FORMAT_NOT_SUPPORTED)
        AN_OPENCL_STATUS_NAME(CL_BUILD_PROGRAM_FAILURE)
        AN_OPENCL_STATUS_NAME(CL_MAP_FAILURE)
        AN_OPENCL_STATUS_NAME(CL_MISALIGNED_SUB_BUFFER_OFFSET)
        AN_OPENCL_STATUS_NAME(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
        AN_OPENCL_STATUS_NAME(CL_COMPILE_PROGRAM_FAILURE)
        AN_OPENCL_STATUS_NAME(CL_LINKER_NOT_AVAILABLE)
        AN_OPENCL_STATUS_NAME(CL_LINK_PROGRAM_FAILURE)
        AN_OPENCL_STATUS_NAME(CL_DEVICE_PARTITION_FAILED)
        AN_OPENCL_STATUS_NAME(CL_KERNEL_ARG_INFO_NOT_AVAILABLE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_VALUE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_DEVICE_TYPE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_PLATFORM)
        AN_OPENCL_STATUS_NAME(CL_INVALID_DEVICE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_CONTEXT)
        AN_OPENCL_STATUS_NAME(CL_INVALID_QUEUE_PROPERTIES)
        AN_OPENCL_STATUS_NAME(CL_INVALID_COMMAND_QUEUE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_HOST_PTR)
        AN_OPENCL_STATUS_NAME(CL_INVALID_MEM_OBJECT)
        AN_OPENCL_STATUS_NAME(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR)
        AN_OPENCL_STATUS_NAME(CL_INVALID_IMAGE_SIZE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_SAMPLER)
        AN_OPENCL_STATUS_NAME(CL_INVALID_BINARY)
        AN_OPENCL_STATUS_NAME(CL_INVALID_BUILD_OPTIONS)
        AN_OPENCL_STATUS_NAME(CL_INVALID_PROGRAM)
        AN_OPENCL_STATUS_NAME(CL_INVALID_PROGRAM_EXECUTABLE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_KERNEL_NAME)
        AN_OPENCL_STATUS_NAME(CL_INVALID_KERNEL_DEFINITION)
        AN_OPENCL_STATUS_NAME(CL_INVALID_KERNEL)
        AN_OPENCL_STATUS_NAME(CL_INVALID_ARG_INDEX)
        AN_OPENCL_STATUS_NAME(CL_INVALID_ARG_VALUE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_ARG_SIZE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_KERNEL_ARGS)
        AN_OPENCL_STATUS_NAME(CL_INVALID_WORK_DIMENSION)
        AN_OPENCL_STATUS_NAME(CL_INVALID_WORK_GROUP_SIZE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_WORK_ITEM_SIZE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_GLOBAL_OFFSET)
        AN_OPENCL_STATUS_NAME(CL_INVALID_EVENT_WAIT_LIST)
        AN_OPENCL_STATUS_NAME(CL_INVALID_EVENT)
        AN_OPENCL_STATUS_NAME(CL_INVALID_OPERATION)
        AN_OPENCL_STATUS_NAME(CL_INVALID_GL_OBJECT)
        AN_OPENCL_STATUS_NAME(CL_INVALID_BUFFER_SIZE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_MIP_LEVEL)
        AN_OPENCL_STATUS_NAME(CL_INVALID_GLOBAL_WORK_SIZE)
        AN_OPENCL_STATUS_NAME(CL_INVALID_PROPERTY)
        AN_OPENCL_STATUS_NAME(CL_INVALID_IMAGE_DESCRIPTOR)
        AN_OPENCL_STATUS_NAME(CL_INVALID_COMPILER_OPTIONS)
        AN_OPENCL_STATUS_NAME(CL_INVALID_LINKER_OPTIONS)
        AN_OPENCL_STATUS_NAME(CL_INVALID_DEVICE_PARTITION_COUNT)
    case -1001:
        // CL_PLATFORM_NOT_FOUND_KHR, which the ICD loader returns when no driver registered a platform.
        return "CL_PLATFORM_NOT_FOUND_KHR";
    default:
        return "UnknownOpenClStatus";
    }
#undef AN_OPENCL_STATUS_NAME
}

Status backendFailure(ErrorCode code, std::string message, std::vector<StatusContextField> context)
{
    StatusDetail detail;
    detail.message = std::move(message);
    detail.context = std::move(context);
    detail.context.push_back(StatusContextField::ofText(ContextKey::Library, "OpenCL"));
    return Status::failure(code, std::move(detail));
}

Status callFailure(ErrorCode code, const char* call, cl_int status)
{
    return backendFailure(code, std::string(call) + " returned " + statusName(status),
                          {StatusContextField::ofNumber(ContextKey::LibraryCode, status)});
}

} // namespace an::opencl

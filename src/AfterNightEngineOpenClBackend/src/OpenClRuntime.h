// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - the operating system's OpenCL loader, reached once and deliberately.
//
// Every call into OpenCL starts here. The loader is delay-loaded, so nothing of it is resolved until
// something calls it, and `loadSystemLoader` is what makes calling it safe: it loads OpenCL.dll from the
// operating system's directory, and nothing in this backend calls the loader unless that succeeded. A
// loader deployed beside an executable is never the one used - a graphics driver installs the loader and
// registers itself with it, and a copy from an SDK would stand between the two.

#pragma once

#include "OpenClHeader.h"

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <string>
#include <vector>

namespace an::opencl
{

/// Loads the operating system's OpenCL loader, once for the process, and keeps it loaded.
///
/// A loaded library belongs to the process whatever holds its handle, and the delay-load helper resolves
/// this backend's imports against the module loaded here, which is why it is never unloaded. Thread-safe.
///
/// @return true when the loader is loaded and may be called.
[[nodiscard]] bool loadSystemLoader() noexcept;

/// The absolute path of the loaded loader, as UTF-8.
///
/// @return the path, or empty when no loader is loaded.
[[nodiscard]] std::string systemLoaderPath();

/// One GPU device, with the handles creating a context over it needs.
struct GpuDeviceHandle
{
    /// The platform the device belongs to.
    cl_platform_id platform = nullptr;

    /// The device.
    cl_device_id device = nullptr;

    /// What the device reports about itself.
    OpenClGpuDevice description{};
};

/// Every GPU device of every platform, in platform order and then device order.
///
/// Requires `loadSystemLoader()` to have succeeded; returns nothing otherwise. A platform whose devices
/// cannot be listed is skipped rather than reported as a failure.
///
/// @return the devices.
[[nodiscard]] std::vector<GpuDeviceHandle> enumerateGpuDevices();

/// The name of an OpenCL status code, for a message a person reads.
///
/// @param status the code.
/// @return a string literal; `"UnknownOpenClStatus"` for a code this backend does not name.
[[nodiscard]] const char* statusName(cl_int status) noexcept;

/// A failure this backend decided, naming OpenCL as the library.
///
/// @param code what kind of failure it is.
/// @param message what happened, for a person.
/// @param context further typed fields, to which the library's name is appended.
/// @return the failure.
[[nodiscard]] Status backendFailure(ErrorCode code, std::string message, std::vector<StatusContextField> context = {});

/// A failure the driver returned from one call, carrying its status as `ContextKey::LibraryCode`.
///
/// @param code what kind of failure it is.
/// @param call the OpenCL function that returned it.
/// @param status what it returned.
/// @return the failure.
[[nodiscard]] Status callFailure(ErrorCode code, const char* call, cl_int status);

} // namespace an::opencl

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - the one place the OpenCL headers are included.
//
// The target version is stated before the headers rather than taken from their default, which is the
// newest API they describe. This backend calls nothing past OpenCL 1.2, so a call to a later function
// does not compile, and a 1.2 function a later version deprecated - `clCreateCommandQueue` among them -
// is not reported as deprecated either. Only this backend's implementation includes this header; its
// published surface names nothing of OpenCL.

#pragma once

#if !defined(CL_TARGET_OPENCL_VERSION)
#define CL_TARGET_OPENCL_VERSION 120
#endif

#include <CL/cl.h>

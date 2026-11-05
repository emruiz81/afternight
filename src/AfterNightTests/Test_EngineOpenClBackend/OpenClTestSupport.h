// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Shared support for the OpenCL backend's suite: finding a device to test, and the fixtures the
// device-contract cases need.
//
// The fixtures are a deliberate copy of the few helpers Test_EngineKernels' mock support provides, rather
// than a file shared with that suite. This suite exists only in a tree generated with
// AFTERNIGHT_OPENCL=1 and is deleted with the backend, while the mock's suite has to build identically
// whether or not this one exists.

#pragma once

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DeviceError.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace an::test
{

/// A backend over the first OpenCL GPU this host has that the backend can use, or why there is none.
struct OpenClDeviceUnderTest
{
    /// The backend, or null when there is no usable device.
    std::shared_ptr<DeviceBackend> backend{};

    /// The device's position in the runtime report.
    std::uint32_t index = 0u;

    /// What the runtime reported about the device.
    OpenClGpuDevice description{};

    /// Why there is no backend, for the skip or failure message.
    std::string unavailable{};
};

/// Whether AFTERNIGHT_TEST_REQUIRE_OPENCL=1 says this host must have a usable OpenCL GPU.
[[nodiscard]] bool openClRequired();

/// Finds the first device that reaches OpenCL 1.2 with double precision and creates a backend over it.
[[nodiscard]] OpenClDeviceUnderTest openClDeviceUnderTest();

/// Declares `variable` as the device under test, and skips the case - or fails it on a host that is
/// required to have one - when there is none.
#define AN_OPENCL_DEVICE_OR_SKIP(variable)                                              \
    ::an::test::OpenClDeviceUnderTest variable = ::an::test::openClDeviceUnderTest();   \
    if (variable.backend == nullptr)                                                    \
    {                                                                                   \
        if (::an::test::openClRequired())                                               \
        {                                                                               \
            FAIL() << "AFTERNIGHT_TEST_REQUIRE_OPENCL=1, and " << variable.unavailable; \
        }                                                                               \
        GTEST_SKIP() << variable.unavailable;                                           \
    }

/// The canonical plan of the backend's affine kernel.
[[nodiscard]] KernelPlanKey openClAffinePlan();

/// An engine context with room for several concurrent executions.
[[nodiscard]] EngineContext makeEngine();

/// An execution of one thread unless `options` states otherwise.
[[nodiscard]] ExecutionContext makeExecution(const EngineContext& engine, ExecutionOptions options = {});

/// A device over a backend, expected to be accepted.
[[nodiscard]] Device makeDevice(std::shared_ptr<DeviceBackend> backend);

/// A single-plane Float32 layout, with rows padded to `rowAlignmentBytes`.
[[nodiscard]] ImageLayout floatPlaneLayout(std::uint32_t width, std::uint32_t height,
                                           std::uint64_t rowAlignmentBytes = 1u);

/// A description of a resource that holds exactly one plane of `layout`.
[[nodiscard]] DeviceResourceDescriptor planeDescriptor(const ImageLayout& layout, ResourceResidency residency);

/// A resource, expected to be created.
[[nodiscard]] DeviceResource makeResource(const Device& device, const DeviceResourceDescriptor& descriptor);

/// Deterministic row-major samples, finite and far from Float32's range limits, starting with -0.
[[nodiscard]] std::vector<float> patternSamples(std::uint32_t width, std::uint32_t height, std::uint32_t seed);

/// Writes row-major samples into a host-addressable resource at `layout`'s row offsets.
void writeFloatPlane(const DeviceResource& resource, const ImageLayout& layout, std::span<const float> samples);

/// Reads a host-addressable resource's samples at `layout`'s row offsets, row-major.
[[nodiscard]] std::vector<float> readFloatPlane(const DeviceResource& resource, const ImageLayout& layout);

/// The scalar reference's answer for the affine operation over row-major samples.
[[nodiscard]] std::vector<float> referenceAffine(std::uint32_t width, std::uint32_t height,
                                                 std::span<const float> samples,
                                                 const AffineKernelParameters& parameters);

/// Row-major samples whose affine results round differently in single and in double precision: full
/// 24-bit mantissas across forty binary magnitudes and both signs, starting with -0.
///
/// The ordinary pattern's samples are small integers scaled by powers of two, over which an exact binary
/// coefficient keeps every intermediate value exact in Float32 - so a bit-for-bit comparison over them
/// cannot tell Float32 arithmetic from Float64.
[[nodiscard]] std::vector<float> roundingSensitiveSamples(std::uint32_t width, std::uint32_t height,
                                                          std::uint32_t seed);

/// The affine operation rounded in single precision at every step, which the reference is not.
[[nodiscard]] std::vector<float> singlePrecisionAffine(std::span<const float> samples,
                                                       const AffineKernelParameters& parameters);

/// Prepares the backend's affine kernel over two layouts, expected to succeed.
[[nodiscard]] PreparedDeviceKernel prepareAffine(const Device& device, const ExecutionContext& execution,
                                                 const ImageLayout& input, const ImageLayout& output,
                                                 const AffineKernelParameters& parameters);

/// How many samples of two equally long runs differ bit for bit; the longer run's length when the
/// lengths differ.
[[nodiscard]] std::size_t differingSamples(std::span<const float> left, std::span<const float> right);

/// Whether a failure carries this code and this device reason.
[[nodiscard]] bool refusedFor(const Status& status, ErrorCode code, DeviceFailure reason);

} // namespace an::test

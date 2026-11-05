// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Support for the OpenCL backend's suite. See OpenClTestSupport.h for why these fixtures are a copy.

#include "OpenClTestSupport.h"

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

namespace an::test
{

bool openClRequired()
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0u;
    const auto copied = _dupenv_s(&value, &length, "AFTERNIGHT_TEST_REQUIRE_OPENCL");
    const bool required = copied == 0 && value != nullptr && std::string_view(value) == "1";
    std::free(value);
    return required;
#else
    const char* const value = std::getenv("AFTERNIGHT_TEST_REQUIRE_OPENCL");
    return value != nullptr && std::string_view(value) == "1";
#endif
}

OpenClDeviceUnderTest openClDeviceUnderTest()
{
    OpenClDeviceUnderTest subject;
    const OpenClRuntimeReport report = openClRuntimeReport();
    if (report.state != OpenClRuntimeState::GpuDevicesFound)
    {
        subject.unavailable = std::string("the OpenCL runtime reports ") + openClRuntimeStateName(report.state);
        return subject;
    }
    for (std::size_t position = 0u; position < report.devices.size(); ++position)
    {
        const OpenClGpuDevice& device = report.devices[position];
        if (!device.meetsApiFloor || !device.doublePrecision)
        {
            continue;
        }
        const auto index = static_cast<std::uint32_t>(position);
        Result<std::shared_ptr<DeviceBackend>> made = createOpenClBackend(OpenClBackendOptions{.deviceIndex = index});
        if (!made.hasValue())
        {
            subject.unavailable =
                "creating a backend over " + device.deviceName + " failed: " + std::string(made.status().message());
            return subject;
        }
        subject.backend = std::move(*made);
        subject.index = index;
        subject.description = device;
        return subject;
    }
    subject.unavailable = "no OpenCL GPU device reaches OpenCL 1.2 with double precision";
    return subject;
}

KernelPlanKey openClAffinePlan()
{
    const Result<KernelContract> contract = openClAffineContract();
    EXPECT_TRUE(contract.hasValue()) << contract.status().message();
    return contract.hasValue() ? kernelPlanFor(*contract) : KernelPlanKey{};
}

EngineContext makeEngine()
{
    EngineContextOptions options;
    options.maxConcurrency = 16u;
    Result<EngineContext> made = EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : EngineContext{};
}

ExecutionContext makeExecution(const EngineContext& engine, ExecutionOptions options)
{
    if (options.maxConcurrency == kUseContextConcurrency)
    {
        options.maxConcurrency = 1u;
    }
    Result<ExecutionContext> made = ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : ExecutionContext{};
}

Device makeDevice(std::shared_ptr<DeviceBackend> backend)
{
    Result<Device> made = Device::create(std::move(backend));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : Device{};
}

ImageLayout floatPlaneLayout(std::uint32_t width, std::uint32_t height, std::uint64_t rowAlignmentBytes)
{
    const EngineLimits limits = defaultEngineLimits();
    const std::optional<ImageExtent> extent = ImageExtent::create(width, height, 1u, limits);
    const ImageLayoutRequest request{.extent = extent.value(),
                                     .elementSizeBytes = sizeof(float),
                                     .samplesPerPixel = 1u,
                                     .planeCount = 1u,
                                     .rowAlignmentBytes = rowAlignmentBytes};
    return ImageLayout::create(request, limits).value();
}

DeviceResourceDescriptor planeDescriptor(const ImageLayout& layout, ResourceResidency residency)
{
    return DeviceResourceDescriptor{
        .bytes = layout.totalBytes(), .alignment = 64u, .residency = residency, .layout = layout};
}

DeviceResource makeResource(const Device& device, const DeviceResourceDescriptor& descriptor)
{
    Result<DeviceResource> made = device.createResource(descriptor);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : DeviceResource{};
}

std::vector<float> patternSamples(std::uint32_t width, std::uint32_t height, std::uint32_t seed)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    std::uint32_t state = seed * 2654435761u + 1u;
    for (float& sample : samples)
    {
        state = state * 1664525u + 1013904223u;
        const auto whole = static_cast<float>(static_cast<std::int32_t>(state % 20001u) - 10000);
        sample = std::ldexp(whole, -static_cast<int>(state >> 28u));
    }
    if (!samples.empty())
    {
        samples.front() = -0.0f;
    }
    return samples;
}

void writeFloatPlane(const DeviceResource& resource, const ImageLayout& layout, std::span<const float> samples)
{
    Result<DeviceMapping> mapping = resource.map(DeviceAccess::Write);
    ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
    const std::span<std::byte> bytes = mapping->writableBytes();
    const std::uint32_t width = layout.extent().width();
    for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
    {
        std::memcpy(bytes.data() + *layout.rowByteOffset(0u, y), samples.data() + static_cast<std::size_t>(y) * width,
                    std::size_t{width} * sizeof(float));
    }
}

std::vector<float> roundingSensitiveSamples(std::uint32_t width, std::uint32_t height, std::uint32_t seed)
{
    std::vector<float> samples(static_cast<std::size_t>(width) * height);
    std::uint64_t state = std::uint64_t{seed} * 0x9E3779B97F4A7C15ull + 1u;
    for (float& sample : samples)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        // A 24-bit mantissa with its top bit set, so every bit a Float32 has is in use.
        const auto mantissa = static_cast<float>((state >> 40u) | (std::uint64_t{1u} << 23u));
        const int exponent = static_cast<int>((state >> 16u) % 40u) - 40;
        const float magnitude = std::ldexp(mantissa, exponent);
        sample = (state & 1u) != 0u ? -magnitude : magnitude;
    }
    if (!samples.empty())
    {
        samples.front() = -0.0f;
    }
    return samples;
}

std::vector<float> singlePrecisionAffine(std::span<const float> samples, const AffineKernelParameters& parameters)
{
    const auto scale = static_cast<float>(parameters.scale);
    const auto bias = static_cast<float>(parameters.bias);
    std::vector<float> results(samples.size());
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const float scaled = samples[index] * scale;
        results[index] = scaled + bias;
    }
    return results;
}

std::vector<float> readFloatPlane(const DeviceResource& resource, const ImageLayout& layout)
{
    const std::uint32_t width = layout.extent().width();
    std::vector<float> samples(static_cast<std::size_t>(width) * layout.extent().height());
    Result<DeviceMapping> mapping = resource.map(DeviceAccess::Read);
    EXPECT_TRUE(mapping.hasValue()) << mapping.status().message();
    if (!mapping.hasValue())
    {
        return samples;
    }
    const std::span<const std::byte> bytes = mapping->bytes();
    for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
    {
        std::memcpy(samples.data() + static_cast<std::size_t>(y) * width, bytes.data() + *layout.rowByteOffset(0u, y),
                    std::size_t{width} * sizeof(float));
    }
    return samples;
}

std::vector<float> referenceAffine(std::uint32_t width, std::uint32_t height, std::span<const float> samples,
                                   const AffineKernelParameters& parameters)
{
    const EngineContext engine = makeEngine();
    const ExecutionContext execution = makeExecution(engine);
    std::vector<float> input(samples.begin(), samples.end());
    std::vector<float> output(input.size(), 0.0f);
    const std::uint64_t stride = std::uint64_t{width} * sizeof(float);
    const std::optional<PlaneView<const float>> source =
        PlaneView<const float>::create(std::span<const float>(input), width, height, 1u, stride);
    const std::optional<PlaneView<float>> destination =
        PlaneView<float>::create(std::span<float>(output), width, height, 1u, stride);
    EXPECT_TRUE(source.has_value() && destination.has_value());
    if (!source.has_value() || !destination.has_value())
    {
        return output;
    }
    const Result<KernelRunResult> run =
        referencePointwiseAffineKernel(execution, *source, *destination, parameters, KernelValidityView{});
    EXPECT_TRUE(run.hasValue()) << run.status().message();
    return output;
}

PreparedDeviceKernel prepareAffine(const Device& device, const ExecutionContext& execution, const ImageLayout& input,
                                   const ImageLayout& output, const AffineKernelParameters& parameters)
{
    const std::array<ImageLayout, 2u> layouts{input, output};
    Result<PreparedDeviceKernel> prepared =
        device.prepareKernel(execution, DeviceKernelRequest{.plan = openClAffinePlan(),
                                                            .bindingLayouts = layouts,
                                                            .arguments = deviceKernelArguments(parameters)});
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    return prepared.hasValue() ? *prepared : PreparedDeviceKernel{};
}

std::size_t differingSamples(std::span<const float> left, std::span<const float> right)
{
    if (left.size() != right.size())
    {
        return (std::max)(left.size(), right.size());
    }
    std::size_t differing = 0u;
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        differing += std::bit_cast<std::uint32_t>(left[index]) != std::bit_cast<std::uint32_t>(right[index]) ? 1u : 0u;
    }
    return differing;
}

bool refusedFor(const Status& status, ErrorCode code, DeviceFailure reason)
{
    const std::optional<DeviceFailure> carried = deviceFailureReason(status);
    return status.code() == code && carried.has_value() && *carried == reason;
}

} // namespace an::test

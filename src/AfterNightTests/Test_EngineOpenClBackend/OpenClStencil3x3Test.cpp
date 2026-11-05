// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The one non-pointwise proof: a direct convolution on the GPU, with a border-resolved
// one-pixel halo in device-owned scratch and explicit upload, kernel and download dependencies.

#include "OpenClTestSupport.h"

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace
{

[[nodiscard]] an::ValidityDescriptor validityRule(an::ValidityRule rule)
{
    const std::optional<an::ValidityDescriptor> made =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = rule});
    EXPECT_TRUE(made.has_value());
    return made.value();
}

[[nodiscard]] std::vector<float> hostStencil(const an::ExecutionContext& execution, std::uint32_t width,
                                             std::uint32_t height, std::span<const float> samples,
                                             const an::Stencil3x3KernelParameters& parameters,
                                             const an::KernelValidityView& validity = {})
{
    std::vector<float> input(samples.begin(), samples.end());
    std::vector<float> output(input.size(), -12345.0f);
    const std::uint64_t stride = std::uint64_t{width} * sizeof(float);
    const std::optional<an::PlaneView<const float>> source =
        an::PlaneView<const float>::create(input, width, height, 1u, stride);
    const std::optional<an::PlaneView<float>> destination =
        an::PlaneView<float>::create(output, width, height, 1u, stride);
    EXPECT_TRUE(source.has_value() && destination.has_value());
    if (source.has_value() && destination.has_value())
    {
        const an::Result<an::KernelRunResult> ran =
            an::referenceStencil3x3Kernel(execution, *source, *destination, parameters, validity);
        EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    }
    return output;
}

[[nodiscard]] an::PreparedDeviceKernel prepareStencil(const an::Device& device, const an::ExecutionContext& execution,
                                                      const an::ImageLayout& input, const an::ImageLayout& output,
                                                      const an::Stencil3x3KernelParameters& parameters)
{
    const std::array layouts{input, output};
    const an::Result<an::PreparedDeviceKernel> prepared =
        device.prepareKernel(execution, an::DeviceKernelRequest{.plan = an::stencil3x3KernelPlan(parameters),
                                                                .bindingLayouts = layouts,
                                                                .arguments = an::deviceKernelArguments(parameters)});
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    return prepared.hasValue() ? *prepared : an::PreparedDeviceKernel{};
}

class OpenClStencil3x3Test : public ::testing::TestWithParam<an::KernelBorderMode>
{
};

TEST_P(OpenClStencil3x3Test, BorderHaloScratchAndAsyncDependenciesMatchTheScalarReferenceExactly)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    constexpr std::uint32_t kWidth = 67u;
    constexpr std::uint32_t kHeight = 43u;
    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients = {0.1, -0.2, 0.3, -0.4, 1.5, -0.6, 0.7, -0.8, 0.9};
    parameters.border = GetParam();
    parameters.constantBorder = -0.375f;

    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);
    ASSERT_NE(device.capabilities(), nullptr);
    EXPECT_TRUE(device.capabilities()->asynchronous);

    const an::ImageLayout inputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 64u);
    const an::ImageLayout outputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 128u);
    const std::vector<float> samples = an::test::roundingSensitiveSamples(kWidth, kHeight, 89u);
    const an::KernelPlanKey plan = an::stencil3x3KernelPlan(parameters);
    const an::Result<an::DeviceKernelSelection> selected = device.selectKernel(execution, plan);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->dispatch, an::DeviceDispatch::Device);
    EXPECT_EQ(selected->reason, an::DeviceDispatchReason::KernelSupported);

    const an::PreparedDeviceKernel kernel = prepareStencil(device, execution, inputLayout, outputLayout, parameters);
    ASSERT_NE(kernel.kernel(), nullptr);
    const std::uint64_t scratchBytes = std::uint64_t{kWidth} * kHeight * 9u * sizeof(float);
    EXPECT_EQ(kernel.kernel()->scratchBytes(), scratchBytes);
    EXPECT_EQ(kernel.kernel()->residentBytes(), 0u);

    const an::DeviceResource hostInput =
        an::test::makeResource(device, an::test::planeDescriptor(inputLayout, an::ResourceResidency::Pinned));
    const an::DeviceResource deviceInput =
        an::test::makeResource(device, an::test::planeDescriptor(inputLayout, an::ResourceResidency::Device));
    const an::DeviceResource deviceOutput =
        an::test::makeResource(device, an::test::planeDescriptor(outputLayout, an::ResourceResidency::Device));
    const an::DeviceResource hostOutput =
        an::test::makeResource(device, an::test::planeDescriptor(outputLayout, an::ResourceResidency::Pinned));
    an::test::writeFloatPlane(hostInput, inputLayout, samples);

    const std::array uploadStep{
        an::DeviceTransfer{.source = &hostInput, .destination = &deviceInput, .bytes = inputLayout.totalBytes()}};
    const an::Result<an::DeviceEvent> upload =
        device.submit(execution, an::DeviceSubmission{.transfersBefore = uploadStep});
    ASSERT_TRUE(upload.hasValue()) << upload.status().message();

    const std::array bindings{an::DeviceBinding{.resource = &deviceInput, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceOutput, .access = an::DeviceAccess::Write}};
    const std::array uploadDependency{*upload};
    const an::Result<an::DeviceEvent> filtered = device.submit(
        execution, an::DeviceSubmission{.kernel = &kernel, .bindings = bindings, .dependencies = uploadDependency});
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();

    const std::array downloadStep{
        an::DeviceTransfer{.source = &deviceOutput, .destination = &hostOutput, .bytes = outputLayout.totalBytes()}};
    const std::array filterDependency{*filtered};
    const an::Result<an::DeviceEvent> download = device.submit(
        execution, an::DeviceSubmission{.transfersBefore = downloadStep, .dependencies = filterDependency});
    ASSERT_TRUE(download.hasValue()) << download.status().message();
    ASSERT_TRUE(download->wait().ok());
    ASSERT_TRUE(upload->outcome().has_value());
    ASSERT_TRUE(filtered->outcome().has_value());
    EXPECT_TRUE(upload->outcome()->ok());
    EXPECT_TRUE(filtered->outcome()->ok());

    EXPECT_EQ(an::test::differingSamples(an::test::readFloatPlane(hostOutput, outputLayout),
                                         hostStencil(execution, kWidth, kHeight, samples, parameters)),
              0u);
    EXPECT_EQ(upload->timing().transfersRecorded, 1u);
    EXPECT_TRUE(filtered->timing().kernelRecorded);
    EXPECT_EQ(download->timing().transfersRecorded, 1u);

    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 1u);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.kernelRuns, 1u);
    EXPECT_EQ(counters.succeededWork, 3u);
    EXPECT_EQ(counters.pendingWork, 0u);
    EXPECT_EQ(counters.backendDefects, 0u);
    EXPECT_GE(counters.deviceMemory.peakBytes, inputLayout.totalBytes() + outputLayout.totalBytes() + scratchBytes);
}

INSTANTIATE_TEST_SUITE_P(Borders, OpenClStencil3x3Test,
                         ::testing::Values(an::KernelBorderMode::Clamp, an::KernelBorderMode::Constant));

TEST(OpenClStencil3x3FallbackTest, AValidityRuleTheDeviceDoesNotCarryRunsOnTheHost)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 3u;
    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients.fill(1.0 / 9.0);
    parameters.border = an::KernelBorderMode::Clamp;

    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);
    an::KernelPlanKey plan = an::stencil3x3KernelPlan(parameters);
    plan.validity = an::ValidityRule::FiniteOnly;
    const an::Result<an::DeviceKernelSelection> selected = device.selectKernel(execution, plan);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->dispatch, an::DeviceDispatch::Host);
    EXPECT_EQ(selected->reason, an::DeviceDispatchReason::KernelNotSupported);

    std::vector<float> samples = an::test::patternSamples(kWidth, kHeight, 97u);
    samples[7] = (std::numeric_limits<float>::quiet_NaN)();
    const std::vector<float> output =
        hostStencil(execution, kWidth, kHeight, samples, parameters,
                    an::KernelValidityView{.descriptor = validityRule(an::ValidityRule::FiniteOnly)});
    EXPECT_EQ(output[7], -12345.0f);
    EXPECT_NE(output.front(), -12345.0f);
    EXPECT_EQ(device.counters().kernelRuns, 0u);
}

} // namespace

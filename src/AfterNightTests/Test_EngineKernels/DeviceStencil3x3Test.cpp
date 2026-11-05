// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The direct stencil through the checked device facade, against the mock backend in both completion
// modes. This is the always-built half of the proof; the opt-in suite runs the same operation on OpenCL.

#include "MockDeviceBackend.h"

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
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

class DeviceStencil3x3Test : public ::testing::TestWithParam<bool>
{
};

TEST_P(DeviceStencil3x3Test, BorderHaloScratchAndDependenciesProduceTheReferenceResult)
{
    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 19u;
    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients = {0.1, -0.2, 0.3, -0.4, 1.5, -0.6, 0.7, -0.8, 0.9};
    parameters.border = an::KernelBorderMode::Constant;
    parameters.constantBorder = -0.375f;

    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    an::test::MockDeviceOptions options;
    options.asynchronous = GetParam();
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(options);
    const an::Device device = an::test::makeMockDevice(backend);
    const an::ImageLayout inputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 64u);
    const an::ImageLayout outputLayout = an::test::floatPlaneLayout(kWidth, kHeight, 128u);
    const std::vector<float> samples = an::test::patternSamples(kWidth, kHeight, 71u);

    const an::Result<an::DeviceKernelSelection> selected =
        device.selectKernel(execution, an::test::mockStencil3x3Plan(parameters));
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->dispatch, an::DeviceDispatch::Device);
    EXPECT_EQ(selected->reason, an::DeviceDispatchReason::KernelSupported);

    const an::PreparedDeviceKernel kernel =
        an::test::prepareStencil3x3(device, execution, inputLayout, outputLayout, parameters);
    ASSERT_NE(kernel.kernel(), nullptr);
    const std::uint64_t scratchBytes = std::uint64_t{kWidth} * kHeight * 9u * sizeof(float);
    EXPECT_EQ(kernel.kernel()->scratchBytes(), scratchBytes);

    const an::DeviceResource hostInput =
        an::test::makeResource(device, an::test::planeDescriptor(inputLayout, an::ResourceResidency::Host));
    const an::DeviceResource deviceInput =
        an::test::makeResource(device, an::test::planeDescriptor(inputLayout, an::ResourceResidency::Device));
    const an::DeviceResource deviceOutput =
        an::test::makeResource(device, an::test::planeDescriptor(outputLayout, an::ResourceResidency::Device));
    const an::DeviceResource hostOutput =
        an::test::makeResource(device, an::test::planeDescriptor(outputLayout, an::ResourceResidency::Host));
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

    EXPECT_TRUE(an::test::sameBits(an::test::readFloatPlane(hostOutput, outputLayout),
                                   hostStencil(execution, kWidth, kHeight, samples, parameters)));
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

INSTANTIATE_TEST_SUITE_P(MockCompletion, DeviceStencil3x3Test, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return std::string(info.param ? "Asynchronous" : "Synchronous");
                         });

TEST(DeviceStencil3x3FallbackTest, UnsupportedValidityRunsThroughTheHostReference)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 3u;
    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients.fill(1.0 / 9.0);
    parameters.border = an::KernelBorderMode::Clamp;

    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    const auto backend = std::make_shared<an::test::MockDeviceBackend>();
    const an::Device device = an::test::makeMockDevice(backend);

    an::KernelPlanKey plan = an::test::mockStencil3x3Plan(parameters);
    plan.validity = an::ValidityRule::FiniteOnly;
    const an::Result<an::DeviceKernelSelection> selected = device.selectKernel(execution, plan);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->dispatch, an::DeviceDispatch::Host);
    EXPECT_EQ(selected->reason, an::DeviceDispatchReason::KernelNotSupported);

    std::vector<float> samples = an::test::patternSamples(kWidth, kHeight, 79u);
    samples[7] = (std::numeric_limits<float>::quiet_NaN)();
    const std::vector<float> output =
        hostStencil(execution, kWidth, kHeight, samples, parameters,
                    an::KernelValidityView{.descriptor = validityRule(an::ValidityRule::FiniteOnly)});
    EXPECT_EQ(output[7], -12345.0f);
    EXPECT_NE(output.front(), -12345.0f);
    EXPECT_EQ(device.counters().kernelRuns, 0u);
}

} // namespace

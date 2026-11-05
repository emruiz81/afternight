// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A fused pointwise program submitted through the checked device facade.
//
// The GPU proof's shape of work, in the configuration every local build has: a chain prepared from the
// plan read off it, carried to the backend by address, bound to one device resource per source and one for
// the destination, and brought back through a declared transfer. The mock evaluates each pixel with the
// scalar evaluator and narrows it as the host executor does, so its result is the host reference's bit for
// bit and a difference is a defect in the seam rather than in arithmetic. The same chain on a GPU is the
// opt-in OpenCL suite's to prove.

#include "MockDeviceBackend.h"

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Runtime/DeviceError.h>
#include <AfterNightEngine/Runtime/DeviceEvent.h>
#include <AfterNightEngine/Runtime/DeviceResource.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// A value no step of these chains writes, so padding a kernel touched is visible.
constexpr float kPadding = -12345.0f;

/// A planar Float32 layout with padded rows and, when asked, whole padding rows between planes.
[[nodiscard]] an::ImageLayout planarFloatLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                                std::uint32_t rowPaddingSamples, std::uint32_t planePaddingRows)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = std::uint64_t{width + rowPaddingSamples} * sizeof(float);
    const std::uint64_t planeStride = rowStride * (std::uint64_t{height} + planePaddingRows);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, planeStride, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// Plane-major, row-major samples placed at a layout's offsets, in a buffer the size of the whole layout.
[[nodiscard]] std::vector<float> laidOut(const an::ImageLayout& layout, std::span<const float> samples)
{
    std::vector<float> buffer(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), kPadding);
    const std::uint32_t width = layout.extent().width();
    std::size_t next = 0u;
    for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            const auto row = static_cast<std::size_t>(*layout.rowByteOffset(plane, y) / sizeof(float));
            std::copy_n(samples.begin() + static_cast<std::ptrdiff_t>(next), width,
                        buffer.begin() + static_cast<std::ptrdiff_t>(row));
            next += width;
        }
    }
    return buffer;
}

/// The plane-major, row-major samples of a buffer laid out as `layout`.
[[nodiscard]] std::vector<float> samplesOf(const an::ImageLayout& layout, std::span<const float> buffer)
{
    const std::uint32_t width = layout.extent().width();
    std::vector<float> samples;
    samples.reserve(std::size_t{width} * layout.extent().height() * layout.planeCount());
    for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            const auto row = static_cast<std::size_t>(*layout.rowByteOffset(plane, y) / sizeof(float));
            samples.insert(samples.end(), buffer.begin() + static_cast<std::ptrdiff_t>(row),
                           buffer.begin() + static_cast<std::ptrdiff_t>(row + width));
        }
    }
    return samples;
}

void writeImage(const an::DeviceResource& resource, const an::ImageLayout& layout, std::span<const float> samples)
{
    const std::vector<float> buffer = laidOut(layout, samples);
    an::Result<an::DeviceMapping> mapping = resource.map(an::DeviceAccess::Write);
    ASSERT_TRUE(mapping.hasValue()) << mapping.status().message();
    std::memcpy(mapping->writableBytes().data(), buffer.data(), buffer.size() * sizeof(float));
}

[[nodiscard]] std::vector<float> readImage(const an::DeviceResource& resource, const an::ImageLayout& layout)
{
    std::vector<float> buffer(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)));
    an::Result<an::DeviceMapping> mapping = resource.map(an::DeviceAccess::Read);
    EXPECT_TRUE(mapping.hasValue()) << mapping.status().message();
    if (mapping.hasValue())
    {
        std::memcpy(buffer.data(), mapping->bytes().data(), buffer.size() * sizeof(float));
    }
    return samplesOf(layout, buffer);
}

/// Samples over and past the normalized interval, each with a full mantissa, led by both zeros, one, a
/// not-a-number and both infinities.
[[nodiscard]] std::vector<float> imageSamples(std::size_t count, std::uint64_t seed)
{
    std::vector<float> samples(count);
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ull + 1u;
    for (float& sample : samples)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        sample = static_cast<float>(std::ldexp(static_cast<double>(state >> 40u), -24) * 1.3 - 0.1);
    }
    constexpr std::array<float, 6u> kSpecial{-0.0f,
                                             0.0f,
                                             1.0f,
                                             std::numeric_limits<float>::quiet_NaN(),
                                             std::numeric_limits<float>::infinity(),
                                             -std::numeric_limits<float>::infinity()};
    std::copy_n(kSpecial.begin(), (std::min)(kSpecial.size(), samples.size()), samples.begin());
    return samples;
}

/// Mask weights in `[0, 1]`, each with a full mantissa, led by an exact zero and an exact one.
[[nodiscard]] std::vector<float> maskSamples(std::size_t count, std::uint64_t seed)
{
    std::vector<float> samples(count);
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ull + 7u;
    for (float& sample : samples)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        sample = static_cast<float>(std::ldexp(static_cast<double>(state >> 40u), -24));
    }
    if (samples.size() >= 2u)
    {
        samples[0] = 0.0f;
        samples[1] = 1.0f;
    }
    return samples;
}

/// Brightness, a clamp, a gamma encode, a sampled tone curve and a mask: the chain the GPU proof runs.
[[nodiscard]] an::PointwiseProgram representativeChain(std::uint32_t channels)
{
    constexpr std::array<double, 17u> kCurve{0.0,  0.03, 0.08, 0.15, 0.23, 0.32, 0.41, 0.5, 0.58,
                                             0.66, 0.73, 0.8,  0.86, 0.91, 0.95, 0.98, 1.0};
    an::PointwiseProgramBuilder builder;
    const std::uint32_t table = builder.addTable(kCurve);
    builder.channels(channels)
        .sources(2u)
        .readingDomain(an::NumericDomain::NormalizedLinear)
        .writingDomain(an::NumericDomain::EncodedNonlinear)
        .append(an::pointwiseScale(1.1))
        .append(an::pointwiseBias(-0.0125))
        .append(an::pointwiseClamp(0.0, 1.0))
        .append(an::pointwiseGamma(1.0 / 2.2))
        .append(an::pointwiseSampledCurve(table, 0.0, 1.0))
        .append(an::pointwiseMask(1u));
    an::Result<an::PointwiseProgram> built = builder.build();
    EXPECT_TRUE(built.hasValue()) << built.status().message();
    return std::move(*built);
}

/// The host reference: the executor forced to the scalar backend, over buffers laid out as the device's.
[[nodiscard]] std::vector<float> referenceResult(const an::PointwiseProgram& program,
                                                 std::span<const an::ImageLayout> sourceLayouts,
                                                 std::span<const std::vector<float>> sources,
                                                 const an::ImageLayout& destinationLayout)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Reference;
    options.backendFallback = an::BackendFallback::Forbidden;
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine, options);
    std::vector<std::vector<float>> buffers;
    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        buffers.push_back(laidOut(sourceLayouts[index], sources[index]));
    }
    std::vector<an::PointwiseImageView> views;
    for (std::size_t index = 0u; index < buffers.size(); ++index)
    {
        views.push_back(an::PointwiseImageView{.samples = buffers[index].data(), .layout = &sourceLayouts[index]});
    }
    std::vector<float> destination(static_cast<std::size_t>(destinationLayout.totalBytes() / sizeof(float)), kPadding);
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, program, views,
        an::PointwiseMutableImageView{.samples = destination.data(), .layout = &destinationLayout},
        an::KernelValidityView{});
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    return samplesOf(destinationLayout, destination);
}

[[nodiscard]] std::size_t differingBits(std::span<const float> left, std::span<const float> right)
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

[[nodiscard]] bool refusedFor(const an::Status& status, an::ErrorCode code, an::DeviceFailure reason)
{
    const std::optional<an::DeviceFailure> carried = an::deviceFailureReason(status);
    return status.code() == code && carried.has_value() && *carried == reason;
}

[[nodiscard]] an::DeviceResourceDescriptor describe(const an::ImageLayout& layout, an::ResourceResidency residency)
{
    return an::DeviceResourceDescriptor{
        .bytes = layout.totalBytes(), .alignment = 64u, .residency = residency, .layout = layout};
}

class DevicePointwiseProgramTest : public ::testing::TestWithParam<bool>
{
};

TEST_P(DevicePointwiseProgramTest, AProgramPreparedFromItsPlanRunsOnTheDeviceExactlyAsTheHostReferenceRunsIt)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    an::test::MockDeviceOptions options;
    options.asynchronous = GetParam();
    const auto backend = std::make_shared<an::test::MockDeviceBackend>(options);
    const an::Device device = an::test::makeMockDevice(backend);

    // Three strides: padded rows and a padding row between planes for the image, wider row padding for the
    // mask, and a contiguous destination - so every binding's own offsets are honoured.
    constexpr std::uint32_t kWidth = 29u;
    constexpr std::uint32_t kHeight = 13u;
    constexpr std::uint32_t kChannels = 2u;
    const an::ImageLayout imageLayout = planarFloatLayout(kWidth, kHeight, kChannels, 3u, 1u);
    const an::ImageLayout maskLayout = planarFloatLayout(kWidth, kHeight, kChannels, 5u, 0u);
    const an::ImageLayout outputLayout = planarFloatLayout(kWidth, kHeight, kChannels, 0u, 0u);
    const std::size_t count = std::size_t{kWidth} * kHeight * kChannels;
    const std::vector<float> image = imageSamples(count, 3u);
    const std::vector<float> mask = maskSamples(count, 4u);
    const an::PointwiseProgram program = representativeChain(kChannels);
    const an::KernelPlanKey plan = an::pointwiseProgramKernelPlan(program);

    const an::Result<an::DeviceKernelSelection> selected = device.selectKernel(execution, plan);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->dispatch, an::DeviceDispatch::Device);
    EXPECT_EQ(selected->reason, an::DeviceDispatchReason::KernelSupported);

    const std::array layouts{imageLayout, maskLayout, outputLayout};
    const an::PointwiseProgramKernelArguments arguments{.program = &program};
    const an::Result<an::PreparedDeviceKernel> kernel = device.prepareKernel(
        execution, an::DeviceKernelRequest{
                       .plan = plan, .bindingLayouts = layouts, .arguments = an::deviceKernelArguments(arguments)});
    ASSERT_TRUE(kernel.hasValue()) << kernel.status().message();

    const an::DeviceResource hostImage =
        an::test::makeResource(device, describe(imageLayout, an::ResourceResidency::Host));
    const an::DeviceResource hostMask =
        an::test::makeResource(device, describe(maskLayout, an::ResourceResidency::Host));
    const an::DeviceResource deviceImage =
        an::test::makeResource(device, describe(imageLayout, an::ResourceResidency::Device));
    const an::DeviceResource deviceMask =
        an::test::makeResource(device, describe(maskLayout, an::ResourceResidency::Device));
    const an::DeviceResource deviceOutput =
        an::test::makeResource(device, describe(outputLayout, an::ResourceResidency::Device));
    const an::DeviceResource hostOutput =
        an::test::makeResource(device, describe(outputLayout, an::ResourceResidency::Host));
    writeImage(hostImage, imageLayout, image);
    writeImage(hostMask, maskLayout, mask);

    const std::array before{
        an::DeviceTransfer{.source = &hostImage, .destination = &deviceImage, .bytes = imageLayout.totalBytes()},
        an::DeviceTransfer{.source = &hostMask, .destination = &deviceMask, .bytes = maskLayout.totalBytes()}};
    const std::array bindings{an::DeviceBinding{.resource = &deviceImage, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceMask, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceOutput, .access = an::DeviceAccess::Write}};
    const std::array after{
        an::DeviceTransfer{.source = &deviceOutput, .destination = &hostOutput, .bytes = outputLayout.totalBytes()}};
    const an::Result<an::DeviceEvent> event = device.submit(
        execution, an::DeviceSubmission{
                       .transfersBefore = before, .kernel = &*kernel, .bindings = bindings, .transfersAfter = after});
    ASSERT_TRUE(event.hasValue()) << event.status().message();
    const an::Status outcome = event->wait();
    ASSERT_TRUE(outcome.ok()) << outcome.message();

    const std::array sourceLayouts{imageLayout, maskLayout};
    const std::array sources{image, mask};
    const std::vector<float> expected = referenceResult(program, sourceLayouts, sources, outputLayout);
    EXPECT_EQ(differingBits(readImage(hostOutput, outputLayout), expected), 0u);

    const an::DeviceWorkTiming timing = event->timing();
    EXPECT_EQ(timing.transfersRecorded, 3u);
    EXPECT_TRUE(timing.kernelRecorded);
    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.uploads.transfers, 2u);
    EXPECT_EQ(counters.downloads.transfers, 1u);
    EXPECT_EQ(counters.kernelRuns, 1u);
    EXPECT_EQ(counters.succeededWork, 1u);
    EXPECT_EQ(counters.backendDefects, 0u);
}

INSTANTIATE_TEST_SUITE_P(MockCompletion, DevicePointwiseProgramTest, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return std::string(info.param ? "Asynchronous" : "Synchronous");
                         });

TEST(DevicePointwiseProgramPlacementTest, APlanPlacesAProgramAndPreparationRefusesWhatThePlanCannotSay)
{
    const an::EngineContext engine = an::test::makeDeviceTestEngine();
    const an::ExecutionContext execution = an::test::makeDeviceTestExecution(engine);
    const auto backend = std::make_shared<an::test::MockDeviceBackend>();
    const an::Device device = an::test::makeMockDevice(backend);

    const an::PointwiseProgram program = representativeChain(1u);
    const an::KernelPlanKey plan = an::pointwiseProgramKernelPlan(program);
    const an::ImageLayout layout = planarFloatLayout(8u, 4u, 1u, 0u, 0u);
    const std::array layouts{layout, layout, layout};
    const auto prepare = [&](const an::KernelPlanKey& planned, std::span<const an::ImageLayout> bindingLayouts,
                             const an::PointwiseProgramKernelArguments& arguments) {
        return device.prepareKernel(execution,
                                    an::DeviceKernelRequest{.plan = planned,
                                                            .bindingLayouts = bindingLayouts,
                                                            .arguments = an::deviceKernelArguments(arguments)});
    };

    // A program in a representation the device advertises nothing for is placed on the host, and says why.
    an::PointwiseProgramBuilder wideBuilder;
    wideBuilder.readingSamples(an::SampleType::Float64)
        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64)
        .append(an::pointwiseScale(0.5));
    const an::Result<an::PointwiseProgram> wide = wideBuilder.build();
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    const an::Result<an::DeviceKernelSelection> onHost =
        device.selectKernel(execution, an::pointwiseProgramKernelPlan(*wide));
    ASSERT_TRUE(onHost.hasValue()) << onHost.status().message();
    EXPECT_EQ(onHost->dispatch, an::DeviceDispatch::Host);
    EXPECT_EQ(onHost->reason, an::DeviceDispatchReason::KernelNotSupported);

    // A plan cannot say that this program's power step rules exactness out, so an exact plan is placed on the
    // device and refused when the kernel is prepared, with the code a caller that may run on the host
    // branches on.
    an::KernelPlanKey exact = plan;
    exact.accuracy = an::KernelAccuracyContract{};
    const an::Result<an::DeviceKernelSelection> placedExact = device.selectKernel(execution, exact);
    ASSERT_TRUE(placedExact.hasValue()) << placedExact.status().message();
    EXPECT_EQ(placedExact->dispatch, an::DeviceDispatch::Device);
    const an::Result<an::PreparedDeviceKernel> refusedExact =
        prepare(exact, layouts, an::PointwiseProgramKernelArguments{.program = &program});
    ASSERT_FALSE(refusedExact.hasValue());
    EXPECT_TRUE(refusedFor(refusedExact.status(), an::ErrorCode::UnsupportedBackend, an::DeviceFailure::BackendFailure))
        << refusedExact.status().message();

    const an::Result<an::PreparedDeviceKernel> noProgram =
        prepare(plan, layouts, an::PointwiseProgramKernelArguments{});
    ASSERT_FALSE(noProgram.hasValue());
    EXPECT_TRUE(refusedFor(noProgram.status(), an::ErrorCode::InvalidArgument, an::DeviceFailure::BackendFailure))
        << noProgram.status().message();

    const an::Result<an::PreparedDeviceKernel> noDestination =
        prepare(plan, std::span<const an::ImageLayout>(layouts).first(2u),
                an::PointwiseProgramKernelArguments{.program = &program});
    ASSERT_FALSE(noDestination.hasValue());
    EXPECT_TRUE(refusedFor(noDestination.status(), an::ErrorCode::InvalidArgument, an::DeviceFailure::BackendFailure))
        << noDestination.status().message();

    const an::Result<an::PreparedDeviceKernel> prepared =
        prepare(plan, layouts, an::PointwiseProgramKernelArguments{.program = &program});
    ASSERT_TRUE(prepared.hasValue()) << prepared.status().message();
    ASSERT_NE(prepared->kernel(), nullptr);
    EXPECT_EQ(prepared->kernel()->plan(), plan);
    EXPECT_EQ(prepared->kernel()->bindings().size(), 3u);
    EXPECT_EQ(device.counters().liveKernels, 1u);

    // The program's one seventeen-entry table is what the kernel keeps between runs, and nothing else here holds
    // device memory, so the device's count is exactly that table - and none of the refused preparations left any.
    constexpr std::uint64_t kTableBytes = 17u * sizeof(double);
    EXPECT_EQ(prepared->kernel()->residentBytes(), kTableBytes);
    const an::DeviceCounters counters = device.counters();
    EXPECT_EQ(counters.deviceMemory.liveBytes, kTableBytes);
    EXPECT_EQ(counters.deviceMemory.liveAllocations, 1u);
    EXPECT_EQ(counters.deviceMemory.deniedAllocations, 0u);
}

} // namespace

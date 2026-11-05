// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A fused pointwise program on a GPU, through the checked device facade.
//
// One representative chain - a brightness change, a clamp, a gamma encode, a sampled tone curve and a mask -
// is prepared from the plan read off it, lowered by the backend into one OpenCL kernel, and run over device
// memory reached only through declared transfers. What it is held to is the accuracy its plan states: every
// step reproduces the scalar evaluator bit for bit except the power step, whose distance from the
// evaluator's power stays inside the engine's accepted transcendental bound. The cases prove the two halves
// apart - the fused kernel against the evaluator given the device's own power, and the device's power
// against the evaluator's - over samples on which a kernel computing in another width, or contracting a
// multiply into an add, would differ.
//
// Transfer time is read from the device's events apart from kernel time, and the frame-scale case prints
// both beside the host executor's time. Neither is a performance target, and a device slower than the host
// is an outcome to record rather than a failure.
//
// Every case skips by name on a host with no usable OpenCL GPU.

#include "OpenClTestSupport.h"

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/DeviceBackend.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
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
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

/// A value no step of these chains writes, so padding a kernel touched is visible.
constexpr float kPadding = -12345.0f;

/// The gamma the representative chain encodes with.
constexpr double kGammaExponent = 1.0 / 2.2;

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
/// not-a-number, both infinities and a subnormal.
[[nodiscard]] std::vector<float> imageSamples(std::size_t count, std::uint64_t seed)
{
    std::vector<float> samples(count);
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ull + 1u;
    for (float& sample : samples)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        sample = static_cast<float>(std::ldexp(static_cast<double>(state >> 40u), -24) * 1.3 - 0.1);
    }
    constexpr std::array<float, 7u> kSpecial{-0.0f,
                                             0.0f,
                                             1.0f,
                                             std::numeric_limits<float>::quiet_NaN(),
                                             std::numeric_limits<float>::infinity(),
                                             -std::numeric_limits<float>::infinity(),
                                             std::numeric_limits<float>::denorm_min()};
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

/// An S-shaped tone curve over encoded values, planned by the engine's own curve planner.
[[nodiscard]] an::Result<an::CurveProgramPlan> toneCurve(std::uint32_t channels)
{
    constexpr std::array<an::CurveControlPoint, 5u> kPoints{
        an::CurveControlPoint{.input = 0.0, .output = 0.0}, an::CurveControlPoint{.input = 0.2, .output = 0.12},
        an::CurveControlPoint{.input = 0.5, .output = 0.56}, an::CurveControlPoint{.input = 0.8, .output = 0.9},
        an::CurveControlPoint{.input = 1.0, .output = 1.0}};
    return an::buildCurveProgram(an::TonePlanShape{.sampleType = an::SampleType::Float32,
                                                   .inputDomain = an::NumericDomain::EncodedNonlinear,
                                                   .outputDomain = an::NumericDomain::EncodedNonlinear,
                                                   .channelCount = channels},
                                 an::CurveProgramRequest{.points = kPoints,
                                                         .interpolation = an::CurveInterpolation::MonotoneCubic,
                                                         .approximation = an::CurveApproximation::SampledLinear,
                                                         .tableEntries = 4096u,
                                                         .domainPolicy = an::CurveDomainPolicy::Clamp});
}

/// Appends another program's steps, carrying its tables across.
void appendSteps(an::PointwiseProgramBuilder& builder, const an::PointwiseProgram& program)
{
    std::vector<std::uint32_t> tables;
    for (std::uint32_t index = 0u; index < program.tableCount(); ++index)
    {
        tables.push_back(builder.addTable(program.table(index)));
    }
    for (an::PointwiseOp step : program.steps())
    {
        if (step.kind == an::PointwiseOpKind::Lut || step.kind == an::PointwiseOpKind::ChannelMatrix ||
            step.kind == an::PointwiseOpKind::SampledCurve)
        {
            step.table = tables[step.table];
        }
        builder.append(step);
    }
}

[[nodiscard]] an::PointwiseProgram built(const an::PointwiseProgramBuilder& builder)
{
    an::Result<an::PointwiseProgram> program = builder.build();
    EXPECT_TRUE(program.hasValue()) << program.status().message();
    return std::move(*program);
}

/// The brightness change and clamp the chain starts with, over one source.
void appendBrightness(an::PointwiseProgramBuilder& builder)
{
    builder.append(an::pointwiseScale(1.1)).append(an::pointwiseBias(-0.0125)).append(an::pointwiseClamp(0.0, 1.0));
}

/// The representative chain: brightness, clamp, gamma encode, tone curve, and a mask from the second source.
[[nodiscard]] an::PointwiseProgram representativeChain(std::uint32_t channels, const an::PointwiseProgram& curve)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(channels)
        .sources(2u)
        .readingDomain(an::NumericDomain::NormalizedLinear)
        .writingDomain(an::NumericDomain::EncodedNonlinear);
    appendBrightness(builder);
    builder.append(an::pointwiseGamma(kGammaExponent));
    appendSteps(builder, curve);
    builder.append(an::pointwiseMask(1u));
    return built(builder);
}

/// The chain's first four steps: everything up to and including the power.
[[nodiscard]] an::PointwiseProgram throughThePower(std::uint32_t channels)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(channels)
        .readingDomain(an::NumericDomain::NormalizedLinear)
        .writingDomain(an::NumericDomain::EncodedNonlinear);
    appendBrightness(builder);
    builder.append(an::pointwiseGamma(kGammaExponent));
    return built(builder);
}

/// The chain's first three steps: the power's base.
[[nodiscard]] an::PointwiseProgram beforeThePower(std::uint32_t channels)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(channels);
    appendBrightness(builder);
    return built(builder);
}

/// The chain's last steps: the tone curve and the mask, reading encoded values.
[[nodiscard]] an::PointwiseProgram afterThePower(std::uint32_t channels, const an::PointwiseProgram& curve)
{
    an::PointwiseProgramBuilder builder;
    builder.channels(channels).sources(2u).readingDomain(an::NumericDomain::EncodedNonlinear);
    appendSteps(builder, curve);
    builder.append(an::pointwiseMask(1u));
    return built(builder);
}

[[nodiscard]] an::DeviceResourceDescriptor describe(const an::ImageLayout& layout, an::ResourceResidency residency)
{
    return an::DeviceResourceDescriptor{
        .bytes = layout.totalBytes(), .alignment = 64u, .residency = residency, .layout = layout};
}

/// What one program's run on the device produced.
struct DeviceRun
{
    std::vector<float> samples{};
    an::DeviceWorkTiming timing{};
};

/// Prepares a program from its own plan and runs it once: pinned memory up, the kernel, pinned memory down.
[[nodiscard]] DeviceRun runOnDevice(const an::Device& device, const an::ExecutionContext& execution,
                                    const an::PointwiseProgram& program, std::span<const an::ImageLayout> sourceLayouts,
                                    std::span<const std::vector<float>> sources,
                                    const an::ImageLayout& destinationLayout)
{
    DeviceRun run;
    std::vector<an::ImageLayout> layouts(sourceLayouts.begin(), sourceLayouts.end());
    layouts.push_back(destinationLayout);
    const an::PointwiseProgramKernelArguments arguments{.program = &program};
    const an::Result<an::PreparedDeviceKernel> kernel =
        device.prepareKernel(execution, an::DeviceKernelRequest{.plan = an::pointwiseProgramKernelPlan(program),
                                                                .bindingLayouts = layouts,
                                                                .arguments = an::deviceKernelArguments(arguments)});
    EXPECT_TRUE(kernel.hasValue()) << kernel.status().message();
    if (!kernel.hasValue())
    {
        return run;
    }

    std::vector<an::DeviceResource> hostSources;
    std::vector<an::DeviceResource> deviceSources;
    hostSources.reserve(sources.size());
    deviceSources.reserve(sources.size());
    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        hostSources.push_back(
            an::test::makeResource(device, describe(sourceLayouts[index], an::ResourceResidency::Pinned)));
        deviceSources.push_back(
            an::test::makeResource(device, describe(sourceLayouts[index], an::ResourceResidency::Device)));
        writeImage(hostSources.back(), sourceLayouts[index], sources[index]);
    }
    const an::DeviceResource deviceDestination =
        an::test::makeResource(device, describe(destinationLayout, an::ResourceResidency::Device));
    const an::DeviceResource hostDestination =
        an::test::makeResource(device, describe(destinationLayout, an::ResourceResidency::Pinned));

    std::vector<an::DeviceTransfer> before;
    std::vector<an::DeviceBinding> bindings;
    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        before.push_back(an::DeviceTransfer{.source = &hostSources[index],
                                            .destination = &deviceSources[index],
                                            .bytes = sourceLayouts[index].totalBytes()});
        bindings.push_back(an::DeviceBinding{.resource = &deviceSources[index], .access = an::DeviceAccess::Read});
    }
    bindings.push_back(an::DeviceBinding{.resource = &deviceDestination, .access = an::DeviceAccess::Write});
    const std::array after{an::DeviceTransfer{
        .source = &deviceDestination, .destination = &hostDestination, .bytes = destinationLayout.totalBytes()}};
    const an::Result<an::DeviceEvent> event = device.submit(
        execution, an::DeviceSubmission{
                       .transfersBefore = before, .kernel = &*kernel, .bindings = bindings, .transfersAfter = after});
    EXPECT_TRUE(event.hasValue()) << event.status().message();
    if (!event.hasValue())
    {
        return run;
    }
    const an::Status outcome = event->wait();
    EXPECT_TRUE(outcome.ok()) << outcome.message();
    run.samples = readImage(hostDestination, destinationLayout);
    run.timing = event->timing();
    return run;
}

/// The host reference: the executor forced to the scalar backend, over buffers laid out as the device's.
[[nodiscard]] std::vector<float> referenceResult(const an::EngineContext& engine, std::uint32_t concurrency,
                                                 const an::PointwiseProgram& program,
                                                 std::span<const an::ImageLayout> sourceLayouts,
                                                 std::span<const std::vector<float>> sources,
                                                 const an::ImageLayout& destinationLayout)
{
    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Reference;
    options.backendFallback = an::BackendFallback::Forbidden;
    options.maxConcurrency = concurrency;
    const an::ExecutionContext execution = an::test::makeExecution(engine, options);
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

[[nodiscard]] bool sameBits(float left, float right) noexcept
{
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}

/// How many units in the last place of `reference` separate it from `value`.
[[nodiscard]] double ulpDistance(float value, float reference) noexcept
{
    const float magnitude = std::fabs(reference);
    const float spacing = std::nextafter(magnitude, std::numeric_limits<float>::infinity()) - magnitude;
    return std::fabs(static_cast<double>(value) - static_cast<double>(reference)) / static_cast<double>(spacing);
}

[[nodiscard]] double medianOf(std::vector<double> values)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2u];
}

[[nodiscard]] double millisecondsOf(std::uint64_t nanoseconds) noexcept
{
    return static_cast<double>(nanoseconds) / 1.0e6;
}

TEST(OpenClPointwiseProgramTest, TheRepresentativeChainMatchesTheScalarReferenceWithinTheAccuracyItsPlanStates)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    // An extent no work-group size divides, three planes, and a different stride for every binding: padded
    // rows and a padding row between planes for the image, wider row padding for the mask, and two padding
    // rows between the destination's planes.
    constexpr std::uint32_t kWidth = 1021u;
    constexpr std::uint32_t kHeight = 769u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout imageLayout = planarFloatLayout(kWidth, kHeight, kChannels, 3u, 1u);
    const an::ImageLayout maskLayout = planarFloatLayout(kWidth, kHeight, kChannels, 11u, 0u);
    const an::ImageLayout outputLayout = planarFloatLayout(kWidth, kHeight, kChannels, 0u, 2u);
    const std::size_t count = std::size_t{kWidth} * kHeight * kChannels;
    const std::vector<float> image = imageSamples(count, 11u);
    const std::vector<float> mask = maskSamples(count, 12u);

    const an::Result<an::CurveProgramPlan> curve = toneCurve(kChannels);
    ASSERT_TRUE(curve.hasValue()) << curve.status().message();
    const an::PointwiseProgram chain = representativeChain(kChannels, curve->program());
    const an::KernelPlanKey plan = an::pointwiseProgramKernelPlan(chain);
    EXPECT_EQ(plan.accuracy, an::mathFunctionAccuracy(an::MathFunction::Pow));
    const an::Result<an::DeviceKernelSelection> selected = device.selectKernel(execution, plan);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_EQ(selected->dispatch, an::DeviceDispatch::Device);
    EXPECT_EQ(selected->reason, an::DeviceDispatchReason::KernelSupported);

    // The comparisons below can fail. Over these samples the chain's first multiply and add, rounded once in
    // double precision or fused into one multiply-add, give different bits from the evaluator's two Float32
    // roundings in a large share of them, so a kernel doing either could not have matched it.
    std::size_t widerDifferences = 0u;
    std::size_t contractedDifferences = 0u;
    const auto scale = static_cast<float>(1.1);
    const auto bias = static_cast<float>(-0.0125);
    for (const float sample : image)
    {
        if (!std::isfinite(sample))
        {
            continue;
        }
        const float scaled = sample * scale;
        const float stepwise = scaled + bias;
        widerDifferences +=
            sameBits(static_cast<float>(static_cast<double>(sample) * 1.1 - 0.0125), stepwise) ? 0u : 1u;
        contractedDifferences += sameBits(std::fma(sample, scale, bias), stepwise) ? 0u : 1u;
    }
    EXPECT_GT(widerDifferences, count / 10u);
    EXPECT_GT(contractedDifferences, count / 20u);
    std::cout << "over " << count << " samples, double-precision brightness would differ in " << widerDifferences
              << " and a contracted one in " << contractedDifferences << '\n';

    const std::array imageLayouts{imageLayout};
    const std::array imageSources{image};
    const std::array bothLayouts{imageLayout, maskLayout};
    const std::array bothSources{image, mask};

    const DeviceRun fused = runOnDevice(device, execution, chain, bothLayouts, bothSources, outputLayout);
    ASSERT_EQ(fused.samples.size(), count);
    EXPECT_EQ(fused.timing.transfersRecorded, 3u);
    EXPECT_TRUE(fused.timing.kernelRecorded);
    const an::DeviceCounters afterChain = device.counters();
    EXPECT_EQ(afterChain.uploads.transfers, 2u);
    EXPECT_EQ(afterChain.downloads.transfers, 1u);
    EXPECT_EQ(afterChain.kernelRuns, 1u);
    EXPECT_EQ(afterChain.backendDefects, 0u);
    std::cout << kWidth << 'x' << kHeight << 'x' << kChannels << " padded chain on " << subject.description.deviceName
              << ": transfers " << fused.timing.transferNanoseconds << " ns, kernel " << fused.timing.kernelNanoseconds
              << " ns, running " << fused.timing.runningNanoseconds << " ns\n";

    // The power's base is arithmetic, and the device reproduces it exactly.
    const an::PointwiseProgram base = beforeThePower(kChannels);
    const std::vector<float> referenceBase =
        referenceResult(engine, 8u, base, imageLayouts, imageSources, outputLayout);
    const DeviceRun deviceBase = runOnDevice(device, execution, base, imageLayouts, imageSources, outputLayout);
    ASSERT_EQ(deviceBase.samples.size(), count);
    std::size_t baseDifferences = 0u;
    for (std::size_t index = 0u; index < count; ++index)
    {
        baseDifferences += sameBits(deviceBase.samples[index], referenceBase[index]) ? 0u : 1u;
    }
    EXPECT_EQ(baseDifferences, 0u);

    // One half: the device's power against the evaluator's, held to the engine's accepted bound for that base.
    const an::PointwiseProgram power = throughThePower(kChannels);
    const std::vector<float> referencePower =
        referenceResult(engine, 8u, power, imageLayouts, imageSources, outputLayout);
    const DeviceRun devicePower = runOnDevice(device, execution, power, imageLayouts, imageSources, outputLayout);
    ASSERT_EQ(devicePower.samples.size(), count);
    std::size_t powerDifferences = 0u;
    std::size_t outsideBound = 0u;
    double worstUlps = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const float actual = devicePower.samples[index];
        const float expected = referencePower[index];
        if (sameBits(actual, expected))
        {
            continue;
        }
        ++powerDifferences;
        const double bound = an::mathUlpBound(an::MathFunction::Pow, referenceBase[index], kGammaExponent);
        const bool comparable = std::isfinite(actual) && std::isnormal(expected) && bound > 0.0;
        const double distance = comparable ? ulpDistance(actual, expected) : std::numeric_limits<double>::infinity();
        worstUlps = (std::max)(worstUlps, distance);
        outsideBound += distance <= bound ? 0u : 1u;
    }
    EXPECT_EQ(outsideBound, 0u);
    std::cout << "the device's power differs from the evaluator's in " << powerDifferences << " of " << count
              << " samples, by at most " << worstUlps << " units in the last place\n";

    // The other half: given the device's own power, everything after it is the evaluator's bit for bit.
    const an::PointwiseProgram tail = afterThePower(kChannels, curve->program());
    const std::array tailLayouts{outputLayout, maskLayout};
    const std::array tailSources{devicePower.samples, mask};
    const std::vector<float> composed = referenceResult(engine, 8u, tail, tailLayouts, tailSources, outputLayout);
    std::size_t composedDifferences = 0u;
    for (std::size_t index = 0u; index < count; ++index)
    {
        composedDifferences += sameBits(fused.samples[index], composed[index]) ? 0u : 1u;
    }
    EXPECT_EQ(composedDifferences, 0u);

    // End to end, a sample may differ from the reference only where the power did.
    const std::vector<float> reference = referenceResult(engine, 8u, chain, bothLayouts, bothSources, outputLayout);
    std::size_t chainDifferences = 0u;
    std::size_t differingWherePowerAgreed = 0u;
    double worstAbsolute = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        if (sameBits(fused.samples[index], reference[index]))
        {
            continue;
        }
        ++chainDifferences;
        differingWherePowerAgreed += sameBits(devicePower.samples[index], referencePower[index]) ? 1u : 0u;
        if (std::isfinite(fused.samples[index]) && std::isfinite(reference[index]))
        {
            worstAbsolute = (std::max)(worstAbsolute, std::fabs(static_cast<double>(fused.samples[index]) -
                                                                static_cast<double>(reference[index])));
        }
    }
    EXPECT_EQ(differingWherePowerAgreed, 0u);
    std::cout << "the fused chain differs from the reference in " << chainDifferences << " of " << count
              << " samples, by at most " << worstAbsolute << '\n';
}

TEST(OpenClPointwiseProgramTest, EveryStepTheDeviceCoversMatchesTheScalarReferenceBitForBit)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    // Tables no binary fraction represents, with a repeated pair so the equal-neighbours branch runs.
    std::vector<double> curveEntries(17u);
    for (std::size_t index = 0u; index < curveEntries.size(); ++index)
    {
        curveEntries[index] = 0.5 + 0.45 * std::sin(0.7 * static_cast<double>(index)) + 0.1 / 3.0;
    }
    curveEntries[9] = curveEntries[8];
    const std::array<double, 2u> stepEntries{0.2, 0.7};

    struct Chain
    {
        std::uint32_t channels = 1u;
        std::uint32_t sources = 1u;
        std::vector<an::PointwiseOp> steps{};
    };
    // Every table index below is the builder's: the long table first, then the two-entry one.
    const std::vector<Chain> chains{
        Chain{.channels = 1u,
              .sources = 1u,
              .steps = {an::pointwiseScale(0.1), an::pointwiseBias(1.0 / 3.0), an::pointwiseClamp(-0.05, 0.95)}},
        Chain{.channels = 3u,
              .sources = 2u,
              .steps = {an::pointwiseMask(1u), an::pointwiseBlend(1u, 0.3), an::pointwiseScale(-0.7),
                        an::pointwiseBias(0.2)}},
        Chain{.channels = 2u,
              .sources = 3u,
              .steps = {an::pointwiseBlend(2u, 1.0), an::pointwiseMask(0u), an::pointwiseBlend(0u, 0.0),
                        an::pointwiseClamp(0.0, 1.0)}},
        Chain{.channels = 1u,
              .sources = 1u,
              .steps = {an::pointwiseSampledCurve(0u, -0.25, 1.5, an::PointwiseCurveSampling::Nearest,
                                                  an::PointwiseCurveDomainPolicy::Clamp)}},
        Chain{.channels = 1u,
              .sources = 1u,
              .steps = {an::pointwiseSampledCurve(0u, -0.25, 1.5, an::PointwiseCurveSampling::Linear,
                                                  an::PointwiseCurveDomainPolicy::LinearExtrapolation)}},
        Chain{
            .channels = 3u, .sources = 1u, .steps = {an::pointwiseSampledCurve(1u, 0.2, 0.7), an::pointwiseScale(3.0)}},
        Chain{.channels = 2u,
              .sources = 2u,
              .steps = {an::pointwiseSampledCurve(0u, 0.1, 0.9, an::PointwiseCurveSampling::Nearest,
                                                  an::PointwiseCurveDomainPolicy::LinearExtrapolation),
                        an::pointwiseMask(1u)}},
        Chain{.channels = 1u,
              .sources = 3u,
              .steps = {an::pointwiseScale(1.0 / 3.0), an::pointwiseBlend(1u, 0.1), an::pointwiseMask(2u),
                        an::pointwiseBias(-1.0 / 3.0), an::pointwiseSampledCurve(0u, -1.0, 1.0),
                        an::pointwiseBlend(0u, 0.9), an::pointwiseClamp(-2.0, 2.0)}},
        // Steps a compiler that reassociates would fold - repeated scales and biases, a repeated mask, a
        // quotient multiplied by a constant over a domain whose width is not a power of two - and that the
        // evaluator rounds one at a time.
        Chain{.channels = 1u,
              .sources = 1u,
              .steps = {an::pointwiseScale(0.1), an::pointwiseScale(-0.7), an::pointwiseBias(1.0 / 3.0),
                        an::pointwiseBias(-0.2), an::pointwiseScale(1.0 / 3.0)}},
        Chain{.channels = 2u,
              .sources = 2u,
              .steps = {an::pointwiseMask(1u), an::pointwiseMask(1u), an::pointwiseBlend(1u, 0.3),
                        an::pointwiseBlend(1u, 0.6)}},
        Chain{.channels = 1u,
              .sources = 1u,
              .steps = {an::pointwiseSampledCurve(0u, -0.25, 1.5), an::pointwiseScale(0.7), an::pointwiseScale(1.3)}}};

    std::set<an::PointwiseOpKind> exercised;
    constexpr std::uint32_t kWidth = 61u;
    constexpr std::uint32_t kHeight = 37u;
    for (std::size_t chainIndex = 0u; chainIndex < chains.size(); ++chainIndex)
    {
        const Chain& described = chains[chainIndex];
        an::PointwiseProgramBuilder builder;
        builder.channels(described.channels).sources(described.sources);
        (void)builder.addTable(curveEntries);
        (void)builder.addTable(stepEntries);
        for (const an::PointwiseOp& step : described.steps)
        {
            builder.append(step);
            exercised.insert(step.kind);
        }
        const an::PointwiseProgram program = built(builder);

        std::vector<an::ImageLayout> sourceLayouts;
        std::vector<std::vector<float>> sources;
        const std::size_t count = std::size_t{kWidth} * kHeight * described.channels;
        for (std::uint32_t source = 0u; source < described.sources; ++source)
        {
            sourceLayouts.push_back(planarFloatLayout(kWidth, kHeight, described.channels, 2u * source + 1u, source));
            sources.push_back(source == 0u ? imageSamples(count, 100u + chainIndex)
                                           : maskSamples(count, 200u + chainIndex * 10u + source));
        }
        const an::ImageLayout destinationLayout = planarFloatLayout(kWidth, kHeight, described.channels, 5u, 1u);

        const DeviceRun ran = runOnDevice(device, execution, program, sourceLayouts, sources, destinationLayout);
        const std::vector<float> expected =
            referenceResult(engine, 4u, program, sourceLayouts, sources, destinationLayout);
        ASSERT_EQ(ran.samples.size(), expected.size()) << "chain " << chainIndex;
        std::size_t differences = 0u;
        for (std::size_t index = 0u; index < expected.size(); ++index)
        {
            differences += sameBits(ran.samples[index], expected[index]) ? 0u : 1u;
        }
        EXPECT_EQ(differences, 0u) << "chain " << chainIndex;
    }
    const std::set<an::PointwiseOpKind> covered{an::PointwiseOpKind::Scale, an::PointwiseOpKind::Bias,
                                                an::PointwiseOpKind::Clamp, an::PointwiseOpKind::Mask,
                                                an::PointwiseOpKind::Blend, an::PointwiseOpKind::SampledCurve};
    EXPECT_EQ(exercised, covered);
}

TEST(OpenClPointwiseProgramTest, ACurvesQuotientIsTheNearestFloat32WhereTheDevicesOwnDivisionIsNot)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    // A two-entry table from zero to one, read linearly and extrapolated from a domain starting at zero, writes
    // its quotient unchanged, so these results are the curve's division itself. Six is a width no binary
    // fraction inverts; three quarters carries the largest inputs past the largest finite Float32; and a half
    // turns half the largest finite value into exactly the largest, the one quotient below the threshold of
    // infinity that is not below the largest finite value.
    constexpr std::array<double, 2u> kIdentity{0.0, 1.0};
    const float largest = std::numeric_limits<float>::max();
    const float tiniest = std::numeric_limits<float>::denorm_min();
    struct Width
    {
        double high = 1.0;
        bool reciprocalInexact = true;
        std::vector<float> inputs{};
    };
    std::vector<Width> widths{Width{.high = 6.0}, Width{.high = 0.75}, Width{.high = 0.5, .reciprocalInexact = false}};
    {
        float value = largest * 0.5f;
        for (int step = 0; step < 3; ++step)
        {
            value = std::nextafter(value, 0.0f);
        }
        for (int step = 0; step < 7; ++step)
        {
            widths[2].inputs.push_back(value);
            widths[2].inputs.push_back(-value);
            value = std::nextafter(value, std::numeric_limits<float>::infinity());
        }
    }

    const std::vector<float> spread = maskSamples(4093u, 31u);
    for (const float unit : spread)
    {
        widths[0].inputs.push_back(unit * 6.0f);
        widths[1].inputs.push_back(unit * 0.75f);
    }
    // Quotients exactly halfway between two subnormals, which round to the even one: 3, 9 and 15 of the
    // smallest subnormal over six are 0.5, 1.5 and 2.5 of it.
    for (const float multiple : {3.0f, 9.0f, 15.0f})
    {
        widths[0].inputs.push_back(multiple * tiniest);
        widths[0].inputs.push_back(-multiple * tiniest);
    }
    // Quotients at the largest finite Float32 and at the threshold past which one rounds to infinity.
    for (const float centre :
         {static_cast<float>(0.75 * static_cast<double>(largest)), static_cast<float>(0.75 * 0x1.ffffffp+127), largest})
    {
        float value = centre;
        for (int step = 0; step < 3; ++step)
        {
            value = std::nextafter(value, 0.0f);
        }
        for (int step = 0; step < 7; ++step)
        {
            widths[1].inputs.push_back(value);
            widths[1].inputs.push_back(-value);
            value = std::nextafter(value, std::numeric_limits<float>::infinity());
        }
    }

    for (const Width& width : widths)
    {
        // The inputs discriminate: dividing by multiplying with a rounded reciprocal gives other bits for some.
        const auto divisor = static_cast<float>(width.high);
        const auto reciprocal = static_cast<float>(1.0 / width.high);
        std::size_t reciprocalDifferences = 0u;
        for (const float input : width.inputs)
        {
            reciprocalDifferences += sameBits(input * reciprocal, input / divisor) ? 0u : 1u;
        }
        if (width.reciprocalInexact)
        {
            EXPECT_GT(reciprocalDifferences, 0u) << "width " << width.high;
        }

        an::PointwiseProgramBuilder builder;
        const std::uint32_t table = builder.addTable(kIdentity);
        builder.readingDomain(an::NumericDomain::PhysicalScaled)
            .append(an::pointwiseSampledCurve(table, 0.0, width.high, an::PointwiseCurveSampling::Linear,
                                              an::PointwiseCurveDomainPolicy::LinearExtrapolation));
        const an::PointwiseProgram program = built(builder);
        const an::ImageLayout layout =
            planarFloatLayout(static_cast<std::uint32_t>(width.inputs.size()), 1u, 1u, 0u, 0u);
        const std::array layouts{layout};
        const std::array sources{width.inputs};
        const DeviceRun ran = runOnDevice(device, execution, program, layouts, sources, layout);
        const std::vector<float> expected = referenceResult(engine, 4u, program, layouts, sources, layout);
        ASSERT_EQ(ran.samples.size(), expected.size());
        std::size_t differences = 0u;
        for (std::size_t index = 0u; index < expected.size(); ++index)
        {
            differences += sameBits(ran.samples[index], expected[index]) ? 0u : 1u;
        }
        EXPECT_EQ(differences, 0u) << "width " << width.high;
        std::cout << "over a curve " << width.high << " wide, a reciprocal-multiplying division would differ in "
                  << reciprocalDifferences << " of " << width.inputs.size() << " quotients\n";
    }
}

TEST(OpenClPointwiseProgramTest, APlanPlacesAProgramOnTheDeviceAndPreparationRefusesTheStepsTheDeviceDoesNotCover)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::Result<an::CurveProgramPlan> curve = toneCurve(1u);
    ASSERT_TRUE(curve.hasValue()) << curve.status().message();
    const an::PointwiseProgram chain = representativeChain(1u, curve->program());
    const an::KernelPlanKey plan = an::pointwiseProgramKernelPlan(chain);
    const an::ImageLayout layout = planarFloatLayout(16u, 8u, 1u, 0u, 0u);
    const std::array layouts{layout, layout, layout};

    const an::Result<an::DeviceKernelSelection> onDevice = device.selectKernel(execution, plan);
    ASSERT_TRUE(onDevice.hasValue()) << onDevice.status().message();
    EXPECT_EQ(onDevice->dispatch, an::DeviceDispatch::Device);

    // What the device's contracts cannot run is placed on the host: a validity rule, and a width.
    an::KernelPlanKey finite = plan;
    finite.validity = an::ValidityRule::FiniteOnly;
    const an::Result<an::DeviceKernelSelection> validityOnHost = device.selectKernel(execution, finite);
    ASSERT_TRUE(validityOnHost.hasValue()) << validityOnHost.status().message();
    EXPECT_EQ(validityOnHost->dispatch, an::DeviceDispatch::Host);
    EXPECT_EQ(validityOnHost->reason, an::DeviceDispatchReason::KernelNotSupported);
    an::PointwiseProgramBuilder wideBuilder;
    wideBuilder.readingSamples(an::SampleType::Float64)
        .computingIn(an::ComputePrecision::Float64, an::Float64Justification::StoredInFloat64)
        .append(an::pointwiseScale(0.5));
    const an::Result<an::DeviceKernelSelection> widthOnHost =
        device.selectKernel(execution, an::pointwiseProgramKernelPlan(built(wideBuilder)));
    ASSERT_TRUE(widthOnHost.hasValue()) << widthOnHost.status().message();
    EXPECT_EQ(widthOnHost->dispatch, an::DeviceDispatch::Host);

    const auto prepare = [&](const an::PointwiseProgram* program, const an::KernelPlanKey& planned,
                             std::span<const an::ImageLayout> bindingLayouts) {
        const an::PointwiseProgramKernelArguments arguments{.program = program};
        return device.prepareKernel(execution,
                                    an::DeviceKernelRequest{.plan = planned,
                                                            .bindingLayouts = bindingLayouts,
                                                            .arguments = an::deviceKernelArguments(arguments)});
    };
    const auto expectRefused = [](const an::Result<an::PreparedDeviceKernel>& prepared, an::ErrorCode code) {
        ASSERT_FALSE(prepared.hasValue());
        const std::optional<an::DeviceFailure> reason = an::deviceFailureReason(prepared.status());
        EXPECT_EQ(prepared.status().code(), code) << prepared.status().message();
        EXPECT_TRUE(reason.has_value() && *reason == an::DeviceFailure::BackendFailure) << prepared.status().message();
    };

    // A plan names no steps, so a program holding one the device does not lower is placed on the device and
    // refused when its kernel is prepared, with the code a caller that may run on the host branches on.
    const auto singleStep = [&](const an::PointwiseOp& step, std::uint32_t sources) {
        an::PointwiseProgramBuilder builder;
        builder.sources(sources).append(step);
        return built(builder);
    };
    for (const an::PointwiseProgram& uncovered :
         {singleStep(an::pointwiseAdd(1u, 0.5), 2u), singleStep(an::pointwiseMinimum(1u), 2u),
          singleStep(an::pointwiseCompare(1u, an::PointwiseComparison::Less), 2u)})
    {
        const an::KernelPlanKey uncoveredPlan = an::pointwiseProgramKernelPlan(uncovered);
        const an::Result<an::DeviceKernelSelection> placed = device.selectKernel(execution, uncoveredPlan);
        ASSERT_TRUE(placed.hasValue()) << placed.status().message();
        EXPECT_EQ(placed->dispatch, an::DeviceDispatch::Device);
        expectRefused(prepare(&uncovered, uncoveredPlan, layouts), an::ErrorCode::UnsupportedBackend);
    }

    an::PointwiseProgramBuilder contracted;
    contracted.allowingContraction().append(an::pointwiseScale(0.1)).append(an::pointwiseBias(0.2));
    const an::PointwiseProgram contractedProgram = built(contracted);
    const std::array twoLayouts{layout, layout};
    expectRefused(prepare(&contractedProgram, an::pointwiseProgramKernelPlan(contractedProgram), twoLayouts),
                  an::ErrorCode::UnsupportedBackend);

    constexpr std::array<double, 4u> kEntries{0.0, 0.2, 0.7, 1.0};
    an::PointwiseProgramBuilder rejecting;
    const std::uint32_t table = rejecting.addTable(kEntries);
    rejecting.append(an::pointwiseSampledCurve(table, 0.0, 1.0, an::PointwiseCurveSampling::Linear,
                                               an::PointwiseCurveDomainPolicy::Reject));
    const an::PointwiseProgram rejectingProgram = built(rejecting);
    expectRefused(prepare(&rejectingProgram, an::pointwiseProgramKernelPlan(rejectingProgram), twoLayouts),
                  an::ErrorCode::UnsupportedBackend);

    // The plan cannot say that a power step rules exactness out either.
    an::KernelPlanKey exact = plan;
    exact.accuracy = an::KernelAccuracyContract{};
    const an::Result<an::DeviceKernelSelection> exactPlaced = device.selectKernel(execution, exact);
    ASSERT_TRUE(exactPlaced.hasValue()) << exactPlaced.status().message();
    EXPECT_EQ(exactPlaced->dispatch, an::DeviceDispatch::Device);
    expectRefused(prepare(&chain, exact, layouts), an::ErrorCode::UnsupportedBackend);

    // A request that describes another program, or none.
    const an::ImageLayout twoPlanes = planarFloatLayout(16u, 8u, 2u, 0u, 0u);
    const std::array mismatched{layout, twoPlanes, layout};
    expectRefused(prepare(&chain, plan, mismatched), an::ErrorCode::IncompatibleArguments);
    expectRefused(prepare(nullptr, plan, layouts), an::ErrorCode::InvalidArgument);

    EXPECT_EQ(device.counters().liveKernels, 0u);
}

TEST(OpenClPointwiseProgramTest, AProgramsTablesAreDeviceMemoryTheDeviceCountsForAsLongAsTheKernelLives)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    const an::EngineContext engine = an::test::makeEngine();
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    // The tone curve's table is written into the kernel's source as a constant array, so the device keeps it
    // for the kernel's whole life: one Float32 per entry, whatever the images the kernel runs over.
    const an::Result<an::CurveProgramPlan> curve = toneCurve(1u);
    ASSERT_TRUE(curve.hasValue()) << curve.status().message();
    const an::PointwiseProgram chain = representativeChain(1u, curve->program());
    std::uint64_t entries = 0u;
    for (std::uint32_t index = 0u; index < chain.tableCount(); ++index)
    {
        entries += chain.table(index).size();
    }
    ASSERT_GT(entries, 0u);
    const std::uint64_t tableBytes = entries * sizeof(float);

    const an::ImageLayout layout = planarFloatLayout(64u, 32u, 1u, 0u, 0u);
    const std::array layouts{layout, layout, layout};
    const an::PointwiseProgramKernelArguments arguments{.program = &chain};
    const an::DeviceCounters before = device.counters();
    {
        const an::Result<an::PreparedDeviceKernel> kernel =
            device.prepareKernel(execution, an::DeviceKernelRequest{.plan = an::pointwiseProgramKernelPlan(chain),
                                                                    .bindingLayouts = layouts,
                                                                    .arguments = an::deviceKernelArguments(arguments)});
        ASSERT_TRUE(kernel.hasValue()) << kernel.status().message();
        EXPECT_EQ(kernel->kernel()->residentBytes(), tableBytes);
        const an::DeviceCounters prepared = device.counters();
        EXPECT_EQ(prepared.deviceMemory.liveBytes, before.deviceMemory.liveBytes + tableBytes);
        EXPECT_EQ(prepared.deviceMemory.liveAllocations, before.deviceMemory.liveAllocations + 1u);
        EXPECT_EQ(prepared.liveKernels, before.liveKernels + 1u);
    }
    const an::DeviceCounters released = device.counters();
    EXPECT_EQ(released.deviceMemory.liveBytes, before.deviceMemory.liveBytes);
    EXPECT_EQ(released.deviceMemory.liveAllocations, before.deviceMemory.liveAllocations);
    EXPECT_EQ(released.liveKernels, before.liveKernels);
    std::cout << "a " << entries << "-entry tone curve holds " << tableBytes << " bytes on "
              << subject.description.deviceName << " while its kernel lives\n";
}

TEST(OpenClPointwiseProgramTest, AtFrameScaleTransferTimeIsReportedApartFromKernelTimeBesideTheHostExecutor)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);
    constexpr std::uint32_t kWidth = 6000u;
    constexpr std::uint32_t kHeight = 4000u;
    constexpr std::size_t kWarmups = 2u;
    constexpr std::size_t kRepetitions = 7u;
    const std::uint32_t hostThreads = (std::max)(1u, (std::min)(std::thread::hardware_concurrency(), 64u));

    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = hostThreads + 1u;
    an::Result<an::EngineContext> madeEngine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(madeEngine.hasValue()) << madeEngine.status().message();
    const an::EngineContext engine = std::move(*madeEngine);
    const an::ExecutionContext execution = an::test::makeExecution(engine);
    const an::Device device = an::test::makeDevice(subject.backend);

    const an::ImageLayout layout = planarFloatLayout(kWidth, kHeight, 1u, 0u, 0u);
    const std::size_t count = std::size_t{kWidth} * kHeight;
    const std::vector<float> image = imageSamples(count, 21u);
    const std::vector<float> mask = maskSamples(count, 22u);
    const an::Result<an::CurveProgramPlan> curve = toneCurve(1u);
    ASSERT_TRUE(curve.hasValue()) << curve.status().message();
    const an::PointwiseProgram chain = representativeChain(1u, curve->program());

    const std::array layouts{layout, layout, layout};
    const an::PointwiseProgramKernelArguments arguments{.program = &chain};
    const Clock::time_point prepareStart = Clock::now();
    const an::Result<an::PreparedDeviceKernel> kernel =
        device.prepareKernel(execution, an::DeviceKernelRequest{.plan = an::pointwiseProgramKernelPlan(chain),
                                                                .bindingLayouts = layouts,
                                                                .arguments = an::deviceKernelArguments(arguments)});
    const double prepareMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - prepareStart).count();
    ASSERT_TRUE(kernel.hasValue()) << kernel.status().message();

    const an::DeviceResource hostImage =
        an::test::makeResource(device, describe(layout, an::ResourceResidency::Pinned));
    const an::DeviceResource hostMask = an::test::makeResource(device, describe(layout, an::ResourceResidency::Pinned));
    const an::DeviceResource deviceImage =
        an::test::makeResource(device, describe(layout, an::ResourceResidency::Device));
    const an::DeviceResource deviceMask =
        an::test::makeResource(device, describe(layout, an::ResourceResidency::Device));
    const an::DeviceResource deviceOutput =
        an::test::makeResource(device, describe(layout, an::ResourceResidency::Device));
    const an::DeviceResource hostOutput =
        an::test::makeResource(device, describe(layout, an::ResourceResidency::Pinned));
    writeImage(hostImage, layout, image);
    writeImage(hostMask, layout, mask);

    // Three pieces of work, each depending on the one before, so each event times one kind of step.
    const std::uint64_t bytes = layout.totalBytes();
    const std::array uploads{an::DeviceTransfer{.source = &hostImage, .destination = &deviceImage, .bytes = bytes},
                             an::DeviceTransfer{.source = &hostMask, .destination = &deviceMask, .bytes = bytes}};
    const std::array bindings{an::DeviceBinding{.resource = &deviceImage, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceMask, .access = an::DeviceAccess::Read},
                              an::DeviceBinding{.resource = &deviceOutput, .access = an::DeviceAccess::Write}};
    const std::array download{an::DeviceTransfer{.source = &deviceOutput, .destination = &hostOutput, .bytes = bytes}};
    std::vector<double> uploadMilliseconds;
    std::vector<double> kernelMilliseconds;
    std::vector<double> downloadMilliseconds;
    std::vector<double> roundTripMilliseconds;
    for (std::size_t repetition = 0u; repetition < kWarmups + kRepetitions; ++repetition)
    {
        const Clock::time_point start = Clock::now();
        const an::Result<an::DeviceEvent> uploaded =
            device.submit(execution, an::DeviceSubmission{.transfersBefore = uploads});
        ASSERT_TRUE(uploaded.hasValue()) << uploaded.status().message();
        const std::array afterUpload{*uploaded};
        const an::Result<an::DeviceEvent> ran = device.submit(
            execution, an::DeviceSubmission{.kernel = &*kernel, .bindings = bindings, .dependencies = afterUpload});
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        const std::array afterKernel{*ran};
        const an::Result<an::DeviceEvent> downloaded =
            device.submit(execution, an::DeviceSubmission{.transfersBefore = download, .dependencies = afterKernel});
        ASSERT_TRUE(downloaded.hasValue()) << downloaded.status().message();
        const an::Status outcome = downloaded->wait();
        const double roundTrip = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        ASSERT_TRUE(outcome.ok()) << outcome.message();
        ASSERT_TRUE(uploaded->wait().ok());
        ASSERT_TRUE(ran->wait().ok());
        if (repetition < kWarmups)
        {
            continue;
        }
        uploadMilliseconds.push_back(millisecondsOf(uploaded->timing().transferNanoseconds));
        kernelMilliseconds.push_back(millisecondsOf(ran->timing().kernelNanoseconds));
        downloadMilliseconds.push_back(millisecondsOf(downloaded->timing().transferNanoseconds));
        roundTripMilliseconds.push_back(roundTrip);
    }
    const std::vector<float> deviceResult = readImage(hostOutput, layout);

    // The device's result, checked the way the representative case checks it: the evaluator's tail over the
    // device's own power.
    const std::array imageLayouts{layout};
    const std::array imageSources{image};
    const DeviceRun devicePower =
        runOnDevice(device, execution, throughThePower(1u), imageLayouts, imageSources, layout);
    const std::array tailLayouts{layout, layout};
    const std::array tailSources{devicePower.samples, mask};
    const std::vector<float> composed =
        referenceResult(engine, hostThreads, afterThePower(1u, curve->program()), tailLayouts, tailSources, layout);
    ASSERT_EQ(deviceResult.size(), composed.size());
    std::size_t differences = 0u;
    for (std::size_t index = 0u; index < composed.size(); ++index)
    {
        differences += sameBits(deviceResult[index], composed[index]) ? 0u : 1u;
    }
    EXPECT_EQ(differences, 0u);

    // The host executor over the same frame, at every admitted thread and at one.
    const std::array<an::PointwiseImageView, 2u> hostViews{
        an::PointwiseImageView{.samples = image.data(), .layout = &layout},
        an::PointwiseImageView{.samples = mask.data(), .layout = &layout}};
    std::vector<float> hostResult(count, 0.0f);
    const auto timeHost = [&](std::uint32_t threads) {
        an::ExecutionOptions options;
        options.maxConcurrency = threads;
        const an::ExecutionContext hostExecution = an::test::makeExecution(engine, options);
        std::vector<double> milliseconds;
        for (std::size_t repetition = 0u; repetition < kWarmups + kRepetitions; ++repetition)
        {
            const Clock::time_point start = Clock::now();
            const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
                hostExecution, chain, hostViews,
                an::PointwiseMutableImageView{.samples = hostResult.data(), .layout = &layout},
                an::KernelValidityView{});
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            EXPECT_TRUE(ran.hasValue()) << ran.status().message();
            if (repetition >= kWarmups)
            {
                milliseconds.push_back(elapsed);
            }
        }
        return medianOf(milliseconds);
    };
    const double hostAllThreads = timeHost(hostThreads);
    const double hostOneThread = timeHost(1u);

    std::cout << kWidth << 'x' << kHeight << " Float32 frame, representative chain on "
              << subject.description.deviceName << ", medians of " << kRepetitions << ": prepare "
              << prepareMilliseconds << " ms (once), upload " << medianOf(uploadMilliseconds) << " ms, kernel "
              << medianOf(kernelMilliseconds) << " ms, download " << medianOf(downloadMilliseconds)
              << " ms, submit to result " << medianOf(roundTripMilliseconds) << " ms\n";
    std::cout << "host executor over the same frame: " << hostAllThreads << " ms at " << hostThreads << " threads, "
              << hostOneThread << " ms at one thread\n";
}

} // namespace

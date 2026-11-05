// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the display presentation: the pipeline from an image to what a viewer paints.
//
// The oracles are written from the definitions rather than read from the engine: an exact median and
// median absolute deviation from a sorted copy, the automatic stretch's balance and the midtones transfer
// from their formulas, the colour balance from its measurement rules, and a byte from the engine's own
// published conversion of one sample, which is the rule the presentation states it follows.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Color/ColorAdjustment.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

/// The consistency constant of a normal distribution: one over the inverse of its cumulative distribution at 0.75.
constexpr double kMadToSigma = 1.4826022185056018;

[[nodiscard]] an::ImageDescriptor describe(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           an::ChannelSchema schema,
                                           an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const std::uint32_t channels =
        schema == an::ChannelSchema::Mono ? 1u : (schema == an::ChannelSchema::Rgb ? 3u : 4u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = type,
                                                               .channelSchema = schema,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = domain,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::ImageLayout layoutFrom(const an::ImageLayoutRequest& request)
{
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::DisplayPresentationPlan plan(const an::DisplayPresentationRequest& request)
{
    an::Result<an::DisplayPresentationPlan> made = an::planDisplayPresentation(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

[[nodiscard]] an::RealtimePreviewDisplayPresentationPlan previewPlan(const an::DisplayPresentationRequest& request,
                                                                     std::uint32_t factor)
{
    an::Result<an::RealtimePreviewDisplayPresentationPlan> made =
        an::planRealtimePreviewDisplayPresentation(request, factor);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// A planar Float32 colour frame: a dim, noisy background per channel and a few bright samples.
[[nodiscard]] std::vector<float> skyFrame(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                          std::uint64_t seed)
{
    Sequence sequence{seed};
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::vector<float> samples(pixels * channels, 0.0f);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::size_t index = 0u; index < pixels; ++index)
        {
            float value = 0.04f + 0.015f * static_cast<float>(channel) + 0.03f * sequence.next();
            if (index % 23u == 7u)
            {
                value = 0.55f + 0.4f * sequence.next();
            }
            samples[channel * pixels + index] = value;
        }
    }
    return samples;
}

[[nodiscard]] double exactMedian(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t count = values.size();
    return (values[(count - 1u) / 2u] + values[count / 2u]) / 2.0;
}

struct Location
{
    double median = 0.0;
    double deviation = 0.0;
};

[[nodiscard]] Location exactLocation(const std::vector<double>& values)
{
    const double median = exactMedian(values);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (const double value : values)
    {
        deviations.push_back(std::abs(value - median));
    }
    return Location{.median = median, .deviation = exactMedian(deviations)};
}

/// The midtones transfer, written from its definition.
[[nodiscard]] double midtonesTransfer(double value, double midtones, double shadows, double highlights)
{
    if (value <= shadows)
    {
        return 0.0;
    }
    if (value >= highlights)
    {
        return 1.0;
    }
    const double x = (value - shadows) / (highlights - shadows);
    return ((midtones - 1.0) * x) / ((2.0 * midtones - 1.0) * x - midtones);
}

struct Transfer
{
    double midtones = 0.5;
    double shadows = 0.0;
    double highlights = 1.0;
};

/// The automatic stretch's balance for channels whose medians sit below one half, from its definition.
[[nodiscard]] Transfer autoStretchOf(const std::vector<Location>& locations, double clipping, double target)
{
    double shadows = 0.0;
    double median = 0.0;
    for (const Location& location : locations)
    {
        const double sigma = location.deviation == 0.0 ? 0.001 : location.deviation * kMadToSigma;
        shadows += location.median + clipping * sigma;
        median += location.median;
    }
    shadows = (std::max)(shadows / static_cast<double>(locations.size()), 0.0);
    median /= static_cast<double>(locations.size());
    return Transfer{
        .midtones = midtonesTransfer(median - shadows, target, 0.0, 1.0), .shadows = shadows, .highlights = 1.0};
}

[[nodiscard]] std::vector<double> channelValues(const std::vector<float>& samples, std::size_t pixels,
                                                std::uint32_t channel)
{
    std::vector<double> values;
    for (std::size_t index = 0u; index < pixels; ++index)
    {
        const float value = samples[channel * pixels + index];
        if (std::isfinite(value))
        {
            values.push_back(value);
        }
    }
    return values;
}

[[nodiscard]] std::uint8_t engineByte(float value)
{
    return an::convertSample<float, std::uint8_t>(value, an::ConversionPolicy{}).value;
}

/// A source over a planar Float32 vector.
struct FloatSource
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<float> samples;

    [[nodiscard]] an::DisplayPresentationSource view(const an::KernelValidityView& validity = {}) const
    {
        return an::DisplayPresentationSource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = validity};
    }
};

[[nodiscard]] FloatSource floatSource(std::uint32_t width, std::uint32_t height, an::ChannelSchema schema,
                                      std::vector<float> samples)
{
    const an::ImageDescriptor descriptor = describe(an::SampleType::Float32, width, height, schema);
    const an::ImageLayout layout =
        planarLayout(an::SampleType::Float32, width, height, descriptor.extent().channelCount());
    return FloatSource{descriptor, layout, std::move(samples)};
}

/// Presents into a planar Float32 working image and returns it with the result.
struct WorkingOutput
{
    an::DisplayPresentationResult result;
    std::vector<float> samples;
};

[[nodiscard]] WorkingOutput presentWorking(const an::ExecutionContext& execution,
                                           an::DisplayPresentationRequest request,
                                           const an::DisplayPresentationSource& source,
                                           const an::DisplayPresentationAnalysis* analysis = nullptr)
{
    request.output = an::PresentationOutput::WorkingFloat32;
    const an::DisplayPresentationPlan made = plan(request);
    const an::Result<an::ImageLayoutRequest> layoutRequest = an::displayPresentationLayout(made, *source.descriptor);
    EXPECT_TRUE(layoutRequest.hasValue()) << layoutRequest.status().message();
    const an::ImageLayout layout = layoutFrom(*layoutRequest);
    std::vector<float> samples(layout.totalBytes() / sizeof(float), -7.0f);
    const an::Result<an::DisplayPresentationResult> result =
        an::presentImage(execution, made, source, samples.data(), layout, analysis);
    EXPECT_TRUE(result.hasValue()) << result.status().message();
    // Drop the row padding so callers can index the planes tightly.
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    std::vector<float> tight;
    for (std::uint32_t channel = 0u; channel < layout.extent().channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto offset = static_cast<std::size_t>(
                (channel * layout.planeStrideBytes() + static_cast<std::uint64_t>(y) * layout.rowStrideBytes()) /
                sizeof(float));
            tight.insert(tight.end(), samples.begin() + static_cast<std::ptrdiff_t>(offset),
                         samples.begin() + static_cast<std::ptrdiff_t>(offset + width));
        }
    }
    return WorkingOutput{result.hasValue() ? *result : an::DisplayPresentationResult{}, std::move(tight)};
}

/// Presents into tightly packed bytes of the plan's output, and returns them.
struct ByteOutput
{
    an::DisplayPresentationResult result;
    an::ImageLayout layout;
    std::vector<std::uint8_t> bytes;
};

[[nodiscard]] ByteOutput presentBytes(const an::ExecutionContext& execution, const an::DisplayPresentationPlan& made,
                                      const an::DisplayPresentationSource& source,
                                      const an::DisplayPresentationAnalysis* analysis = nullptr)
{
    an::Result<an::ImageLayoutRequest> layoutRequest = an::displayPresentationLayout(made, *source.descriptor);
    EXPECT_TRUE(layoutRequest.hasValue());
    layoutRequest->rowAlignmentBytes = 1u;
    const an::ImageLayout layout = layoutFrom(*layoutRequest);
    std::vector<std::uint8_t> bytes(layout.totalBytes(), 0xCDu);
    const an::Result<an::DisplayPresentationResult> result =
        an::presentImage(execution, made, source, bytes.data(), layout, analysis);
    EXPECT_TRUE(result.hasValue()) << result.status().message();
    return ByteOutput{result.hasValue() ? *result : an::DisplayPresentationResult{}, layout, std::move(bytes)};
}

} // namespace

TEST(DisplayPresentationTest, PlanningRefusesEveryMalformedField)
{
    EXPECT_TRUE(an::planDisplayPresentation({}).hasValue());
    const auto refused = [](const an::DisplayPresentationRequest& request, an::ErrorCode code) {
        const an::Result<an::DisplayPresentationPlan> made = an::planDisplayPresentation(request);
        EXPECT_FALSE(made.hasValue());
        if (!made.hasValue())
        {
            EXPECT_EQ(made.status().code(), code) << made.status().message();
        }
    };
    an::DisplayPresentationRequest request;
    request.promotion = static_cast<an::PresentationPromotion>(2u);
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.stretch = static_cast<an::PresentationStretch>(2u);
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.output = static_cast<an::PresentationOutput>(4u);
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.invalidPixels = static_cast<an::PresentationInvalidPixels>(2u);
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.saturationBackgroundLocation = static_cast<an::SaturationBackgroundLocation>(2u);
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.neutralizationBackground = {0.2, 0.1};
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.calibrationWhite = {0.0, 1.5};
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.calibrationBackground = {-0.1, 0.1};
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.shadowsClipping = std::numeric_limits<double>::quiet_NaN();
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.targetBackground = 1.25;
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.saturation = std::numeric_limits<double>::infinity();
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.saturationBackgroundFactor = std::numeric_limits<double>::quiet_NaN();
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.brightness = 1.5;
    refused(request, an::ErrorCode::InvalidArgument);
    request = {};
    request.contrast = 0.0;
    refused(request, an::ErrorCode::InvalidArgument);

    const an::Result<an::RealtimePreviewDisplayPresentationPlan> zero =
        an::planRealtimePreviewDisplayPresentation({}, 0u);
    ASSERT_FALSE(zero.hasValue());
    EXPECT_EQ(zero.status().code(), an::ErrorCode::OutOfRange);
    const an::Result<an::RealtimePreviewDisplayPresentationPlan> wide =
        an::planRealtimePreviewDisplayPresentation({}, an::kMaxRealtimePreviewSubsampleFactor + 1u);
    ASSERT_FALSE(wide.hasValue());
    EXPECT_EQ(wide.status().code(), an::ErrorCode::OutOfRange);
    request = {};
    request.contrast = -1.0;
    const an::Result<an::RealtimePreviewDisplayPresentationPlan> malformed =
        an::planRealtimePreviewDisplayPresentation(request, 2u);
    ASSERT_FALSE(malformed.hasValue());
    EXPECT_EQ(malformed.status().code(), an::ErrorCode::InvalidArgument);
    const an::RealtimePreviewDisplayPresentationPlan accepted =
        previewPlan(an::DisplayPresentationRequest{}, an::kMaxRealtimePreviewSubsampleFactor);
    EXPECT_EQ(accepted.subsampleFactor(), an::kMaxRealtimePreviewSubsampleFactor);
}

TEST(DisplayPresentationTest, TheLayoutStatesEachOutputsShape)
{
    const an::ImageDescriptor mono = describe(an::SampleType::UInt16, 10u, 7u, an::ChannelSchema::Mono);
    const an::ImageDescriptor rgb = describe(an::SampleType::Float32, 10u, 7u, an::ChannelSchema::Rgb);
    const an::ImageDescriptor rgba = describe(an::SampleType::UInt8, 10u, 7u, an::ChannelSchema::Rgba);
    struct Expected
    {
        const an::ImageDescriptor* source;
        an::PresentationOutput output;
        std::uint32_t channels;
        std::uint32_t elementBytes;
        std::uint32_t samplesPerPixel;
        std::uint32_t planes;
    };
    const std::array<Expected, 8u> cases{{
        {&mono, an::PresentationOutput::Interleaved8, 1u, 1u, 1u, 1u},
        {&rgb, an::PresentationOutput::Interleaved8, 3u, 1u, 3u, 1u},
        {&rgba, an::PresentationOutput::Interleaved8, 4u, 1u, 4u, 1u},
        {&mono, an::PresentationOutput::Rgba8, 4u, 1u, 4u, 1u},
        {&mono, an::PresentationOutput::Bgra8, 4u, 1u, 4u, 1u},
        {&rgb, an::PresentationOutput::Bgra8, 4u, 1u, 4u, 1u},
        {&rgb, an::PresentationOutput::WorkingFloat32, 3u, 4u, 1u, 3u},
        {&rgba, an::PresentationOutput::WorkingFloat32, 4u, 4u, 1u, 4u},
    }};
    for (const Expected& expected : cases)
    {
        an::DisplayPresentationRequest request;
        request.output = expected.output;
        const an::Result<an::ImageLayoutRequest> layout =
            an::displayPresentationLayout(plan(request), *expected.source);
        ASSERT_TRUE(layout.hasValue()) << layout.status().message();
        EXPECT_EQ(layout->extent.width(), 10u);
        EXPECT_EQ(layout->extent.height(), 7u);
        EXPECT_EQ(layout->extent.channelCount(), expected.channels);
        EXPECT_EQ(layout->elementSizeBytes, expected.elementBytes);
        EXPECT_EQ(layout->samplesPerPixel, expected.samplesPerPixel);
        EXPECT_EQ(layout->planeCount, expected.planes);
        EXPECT_EQ(layout->rowAlignmentBytes, expected.elementBytes == 1u ? 4u : 64u);
        EXPECT_TRUE(an::ImageLayout::create(*layout, an::defaultEngineLimits()).has_value());
    }
    const an::Result<an::ImageLayoutRequest> subsampled =
        an::realtimePreviewDisplayPresentationLayout(previewPlan({}, 3u), mono);
    ASSERT_TRUE(subsampled.hasValue());
    EXPECT_EQ(subsampled->extent.width(), 4u);
    EXPECT_EQ(subsampled->extent.height(), 3u);

    // A colour encoding other than RGB is converted before it is presented.
    an::ImageDescriptorRequest tristimulus = rgb.toRequest();
    tristimulus.channelSchema = an::ChannelSchema::Xyz;
    tristimulus.colorSpace = an::ColorSpace::CieXyz;
    const std::optional<an::ImageDescriptor> xyz = an::ImageDescriptor::create(tristimulus);
    ASSERT_TRUE(xyz.has_value());
    const an::Result<an::ImageLayoutRequest> refused = an::displayPresentationLayout(plan({}), *xyz);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(DisplayPresentationTest, ALinearPresentationOfEveryUInt16CodeIsTheEngineConversionToBytes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = describe(an::SampleType::UInt16, 256u, 256u, an::ChannelSchema::Mono);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, 256u, 256u);
    std::vector<std::uint16_t> codes(65536u);
    for (std::size_t code = 0u; code < codes.size(); ++code)
    {
        codes[code] = static_cast<std::uint16_t>(code);
    }
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Linear;
    const ByteOutput output = presentBytes(
        execution, plan(request),
        an::DisplayPresentationSource{.descriptor = &descriptor, .samples = codes.data(), .layout = &layout});
    ASSERT_EQ(output.bytes.size(), codes.size());
    for (std::size_t code = 0u; code < codes.size(); ++code)
    {
        const std::uint8_t expected =
            an::convertSample<std::uint16_t, std::uint8_t>(codes[code], an::ConversionPolicy{}).value;
        ASSERT_EQ(output.bytes[code], expected) << "code " << code;
    }
    EXPECT_EQ(output.result.invalidPixels, 0u);
    EXPECT_EQ(output.result.width, 256u);
    EXPECT_FALSE(output.result.analysis.measured);
    for (const an::MidtonesTransferParameters& transfer : output.result.transfer)
    {
        EXPECT_EQ(transfer.midtones, 0.5);
        EXPECT_EQ(transfer.shadows, 0.0);
        EXPECT_EQ(transfer.highlights, 1.0);
    }
}

namespace
{

/// Presents every code a narrow representation holds, through every subsample factor from one to nine, on the
/// reference backend and on each instruction set this host runs, and holds each byte to the engine's conversion. The
/// presented samples count through every code; the others hold a pattern a wrong stride would show. Each source row
/// ends at its last presented sample, so a row that read a whole vector past it would read past the image.
template <typename SampleT> void expectEveryStrideToPresentEveryCode()
{
    constexpr std::uint32_t kOutputWidth = 257u;
    constexpr std::uint32_t kOutputHeight = 256u;
    constexpr std::uint32_t kCodes = 1u << (8u * sizeof(SampleT));
    const an::SampleType type = an::ResidentSampleTraits<SampleT>::kSampleType;
    const an::EngineContext engine = makeEngine();
    std::vector<std::pair<const char*, an::ExecutionOptions>> executions;
    an::ExecutionOptions reference;
    reference.backend = an::ComputeBackend::Reference;
    executions.emplace_back("reference", reference);
    executions.emplace_back("automatic", an::ExecutionOptions{});
    for (const auto& [name, set] :
         {std::pair{"AVX2", an::InstructionSet::Avx2}, std::pair{"AVX-512", an::InstructionSet::Avx512Skylake}})
    {
        if (!an::instructionSetIsCompiledIn(set) || !an::supportsInstructionSet(an::cpuCapabilities(), set))
        {
            continue;
        }
        an::ExecutionOptions forced;
        forced.backend = an::ComputeBackend::Cpu;
        forced.instructionSet = set;
        forced.backendFallback = an::BackendFallback::Forbidden;
        executions.emplace_back(name, forced);
    }
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Linear;
    request.output = an::PresentationOutput::Interleaved8;
    for (std::uint32_t factor = 1u; factor <= 9u; ++factor)
    {
        const std::uint32_t width = (kOutputWidth - 1u) * factor + 1u;
        const std::uint32_t height = (kOutputHeight - 1u) * factor + 1u;
        const an::ImageDescriptor descriptor = describe(type, width, height, an::ChannelSchema::Mono);
        const an::ImageLayout layout = planarLayout(type, width, height);
        std::vector<SampleT> samples(static_cast<std::size_t>(width) * height, static_cast<SampleT>(0xA5A5u));
        for (std::uint32_t y = 0u; y < kOutputHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kOutputWidth; ++x)
            {
                samples[static_cast<std::size_t>(y) * factor * width + static_cast<std::size_t>(x) * factor] =
                    static_cast<SampleT>((y * kOutputWidth + x) % kCodes);
            }
        }
        const an::DisplayPresentationSource source{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout};
        const an::RealtimePreviewDisplayPresentationPlan made = previewPlan(request, factor);
        an::Result<an::ImageLayoutRequest> layoutRequest =
            an::realtimePreviewDisplayPresentationLayout(made, descriptor);
        ASSERT_TRUE(layoutRequest.hasValue()) << layoutRequest.status().message();
        layoutRequest->rowAlignmentBytes = 1u;
        const an::ImageLayout destinationLayout = layoutFrom(*layoutRequest);
        ASSERT_EQ(destinationLayout.extent().width(), kOutputWidth);
        ASSERT_EQ(destinationLayout.extent().height(), kOutputHeight);
        for (const auto& [name, options] : executions)
        {
            an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
            ASSERT_TRUE(execution.hasValue()) << name << ": " << execution.status().message();
            std::vector<std::uint8_t> bytes(destinationLayout.totalBytes(), 0xCDu);
            const an::Result<an::DisplayPresentationResult> result =
                an::presentImageRealtimePreview(*execution, made, source, bytes.data(), destinationLayout);
            ASSERT_TRUE(result.hasValue()) << name << ": " << result.status().message();
            std::size_t wrong = 0u;
            std::size_t first = bytes.size();
            for (std::size_t index = 0u; index < bytes.size(); ++index)
            {
                const auto code = static_cast<SampleT>(index % kCodes);
                const std::uint8_t expected =
                    an::convertSample<SampleT, std::uint8_t>(code, an::ConversionPolicy{}).value;
                if (bytes[index] != expected)
                {
                    first = (std::min)(first, index);
                    ++wrong;
                }
            }
            EXPECT_EQ(wrong, 0u) << name << " at factor " << factor << ", first at output sample " << first;
        }
    }
}

} // namespace

TEST(DisplayPresentationTest, EveryStrideOfAPreviewPresentsEverySixteenBitCodeOnEveryInstructionSet)
{
    expectEveryStrideToPresentEveryCode<std::uint16_t>();
}

TEST(DisplayPresentationTest, EveryStrideOfAPreviewPresentsEveryEightBitCodeOnEveryInstructionSet)
{
    expectEveryStrideToPresentEveryCode<std::uint8_t>();
}

TEST(DisplayPresentationTest, AFloatSampleBecomesTheByteTheEngineConversionGivesIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // Every tie between two codes, a step either side of each, the ends, and values outside the interval.
    std::vector<float> values;
    for (int code = 0; code < 255; ++code)
    {
        const float tie = static_cast<float>((static_cast<double>(code) + 0.5) / 255.0);
        values.push_back(tie);
        values.push_back(std::nextafter(tie, 0.0f));
        values.push_back(std::nextafter(tie, 1.0f));
        values.push_back(static_cast<float>(code) / 255.0f);
    }
    for (const float extra : {-0.5f, -0.0f, 0.0f, 1.0f, 1.0000001f, 7.0f})
    {
        values.push_back(extra);
    }
    Sequence sequence{91u};
    while (values.size() < 4096u)
    {
        values.push_back(sequence.next() * 1.2f - 0.1f);
    }
    const auto width = static_cast<std::uint32_t>(values.size());
    const FloatSource source = floatSource(width, 1u, an::ChannelSchema::Mono, values);
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Linear;
    const ByteOutput output = presentBytes(execution, plan(request), source.view());
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        ASSERT_EQ(output.bytes[x], engineByte(values[x])) << "value " << values[x];
    }
}

TEST(DisplayPresentationTest, ALinearByteOutputIsTheConversionOfItsWorkingImage)
{
    constexpr std::uint32_t kWidth = 11u;
    constexpr std::uint32_t kHeight = 7u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // An interleaved colour source with a non-finite sample, behind a mask that rejects one pixel's green.
    const an::ImageDescriptor descriptor = describe(an::SampleType::Float32, kWidth, kHeight, an::ChannelSchema::Rgb);
    const an::ImageLayout layout = interleavedLayout(an::SampleType::Float32, kWidth, kHeight, 3u);
    std::vector<float> samples = skyFrame(kWidth, kHeight, 3u, 17u);
    samples[5] = std::numeric_limits<float>::quiet_NaN();
    samples[40] = 1.7f;
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 3u, an::defaultEngineLimits());
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(2u) * kHeight * 3u, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, *maskExtent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    ASSERT_TRUE(writable->setValid(1u, 4u, 2u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const std::optional<an::ValidityDescriptor> masked =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(masked.has_value());
    const an::DisplayPresentationSource source{.descriptor = &descriptor,
                                               .samples = samples.data(),
                                               .layout = &layout,
                                               .validity =
                                                   an::KernelValidityView{.descriptor = *masked, .mask = &mask}};

    for (const std::uint32_t factor : {1u, 2u})
    {
        for (const an::PresentationPromotion promotion :
             {an::PresentationPromotion::Gradient, an::PresentationPromotion::Quantized16})
        {
            an::DisplayPresentationRequest request;
            request.stretch = an::PresentationStretch::Linear;
            request.promotion = promotion;
            request.output = an::PresentationOutput::WorkingFloat32;
            const an::RealtimePreviewDisplayPresentationPlan workingPlan = previewPlan(request, factor);
            const an::Result<an::ImageLayoutRequest> workingRequest =
                an::realtimePreviewDisplayPresentationLayout(workingPlan, descriptor);
            ASSERT_TRUE(workingRequest.hasValue());
            const an::ImageLayout workingLayout = layoutFrom(*workingRequest);
            std::vector<float> working(workingLayout.totalBytes() / sizeof(float));
            ASSERT_TRUE(an::presentImageRealtimePreview(execution, workingPlan, source, working.data(), workingLayout)
                            .hasValue());
            const std::uint32_t width = workingLayout.extent().width();
            const std::uint32_t height = workingLayout.extent().height();
            const auto workingAt = [&](std::uint32_t channel, std::uint32_t x, std::uint32_t y) {
                return working[(channel * workingLayout.planeStrideBytes() + y * workingLayout.rowStrideBytes()) /
                                   sizeof(float) +
                               x];
            };

            for (const an::PresentationOutput output :
                 {an::PresentationOutput::Interleaved8, an::PresentationOutput::Rgba8})
            {
                request.output = output;
                const an::RealtimePreviewDisplayPresentationPlan bytePlan = previewPlan(request, factor);
                an::Result<an::ImageLayoutRequest> byteRequest =
                    an::realtimePreviewDisplayPresentationLayout(bytePlan, descriptor);
                ASSERT_TRUE(byteRequest.hasValue());
                const an::ImageLayout byteLayout = layoutFrom(*byteRequest);
                std::vector<std::uint8_t> bytes(byteLayout.totalBytes(), 0xCDu);
                const an::Result<an::DisplayPresentationResult> presented =
                    an::presentImageRealtimePreview(execution, bytePlan, source, bytes.data(), byteLayout);
                ASSERT_TRUE(presented.hasValue()) << presented.status().message();
                std::uint64_t invalid = 0u;
                for (std::uint32_t y = 0u; y < height; ++y)
                {
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        bool pixelInvalid = false;
                        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            const float value = workingAt(channel, x, y);
                            pixelInvalid = pixelInvalid || std::isnan(value);
                            const std::size_t at = y * byteLayout.rowStrideBytes() +
                                                   static_cast<std::size_t>(x) * byteLayout.samplesPerPixel() + channel;
                            ASSERT_EQ(bytes[at], engineByte(value)) << factor << " " << x << " " << y << " " << channel;
                        }
                        invalid += pixelInvalid ? 1u : 0u;
                        if (output == an::PresentationOutput::Rgba8)
                        {
                            EXPECT_EQ(bytes[y * byteLayout.rowStrideBytes() + static_cast<std::size_t>(x) * 4u + 3u],
                                      255u);
                        }
                    }
                }
                EXPECT_EQ(presented->invalidPixels, invalid);
                EXPECT_EQ(presented->width, width);
            }
        }
    }
}

namespace
{

/// Every code of a narrow integer representation, presented linearly as bytes and as a working image: the bytes are
/// the engine's conversion of the working image, under both promotions and both byte outputs.
template <typename SampleT> void expectEveryCodeAsItsWorkingImageConverts()
{
    constexpr std::uint32_t kCodes = 1u << (8u * sizeof(SampleT));
    constexpr std::uint32_t kWidth = kCodes >= 256u ? 256u : kCodes;
    constexpr std::uint32_t kHeight = kCodes / kWidth;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const an::ImageDescriptor descriptor = describe(type, kWidth, kHeight, an::ChannelSchema::Mono);
    const an::ImageLayout layout = planarLayout(type, kWidth, kHeight);
    std::vector<SampleT> codes(kCodes);
    for (std::uint32_t index = 0u; index < kCodes; ++index)
    {
        codes[index] = static_cast<SampleT>((std::numeric_limits<SampleT>::min)() + static_cast<std::int64_t>(index));
    }
    const an::DisplayPresentationSource source{.descriptor = &descriptor, .samples = codes.data(), .layout = &layout};
    for (const an::PresentationPromotion promotion :
         {an::PresentationPromotion::Gradient, an::PresentationPromotion::Quantized16})
    {
        an::DisplayPresentationRequest request;
        request.stretch = an::PresentationStretch::Linear;
        request.promotion = promotion;
        const WorkingOutput working = presentWorking(execution, request, source);
        for (const an::PresentationOutput output :
             {an::PresentationOutput::Interleaved8, an::PresentationOutput::Rgba8})
        {
            request.output = output;
            const ByteOutput bytes = presentBytes(execution, plan(request), source);
            const std::uint32_t stride = output == an::PresentationOutput::Rgba8 ? 4u : 1u;
            for (std::uint32_t index = 0u; index < kCodes; ++index)
            {
                ASSERT_EQ(bytes.bytes[index * stride], engineByte(working.samples[index]))
                    << "code " << static_cast<std::int64_t>(codes[index]);
            }
        }
    }
}

} // namespace

TEST(DisplayPresentationTest, EveryEightAndSixteenBitCodeIsTheConversionOfItsWorkingImage)
{
    expectEveryCodeAsItsWorkingImageConverts<std::uint8_t>();
    expectEveryCodeAsItsWorkingImageConverts<std::int8_t>();
    expectEveryCodeAsItsWorkingImageConverts<std::uint16_t>();
    expectEveryCodeAsItsWorkingImageConverts<std::int16_t>();
}

TEST(DisplayPresentationTest, AnUnlinkedStretchIsEachChannelsMidtonesTransferOfItsExactStatistics)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource source = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 5u));

    const WorkingOutput working = presentWorking(execution, an::DisplayPresentationRequest{}, source.view());
    ASSERT_TRUE(working.result.analysis.measured);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const Location location = exactLocation(channelValues(source.samples, kPixels, channel));
        EXPECT_NEAR(working.result.analysis.medians[channel], location.median, 1e-7);
        EXPECT_NEAR(working.result.analysis.medianAbsoluteDeviations[channel], location.deviation, 1e-7);
        EXPECT_EQ(working.result.analysis.measuredSamples[channel], kPixels);
        const Transfer transfer = autoStretchOf({location}, an::kDisplayShadowsClipping, an::kDisplayTargetBackground);
        EXPECT_NEAR(working.result.transfer[channel].midtones, transfer.midtones, 1e-9);
        EXPECT_NEAR(working.result.transfer[channel].shadows, transfer.shadows, 1e-9);
        for (std::size_t index = 0u; index < kPixels; ++index)
        {
            const double expected =
                midtonesTransfer(source.samples[channel * kPixels + index], transfer.midtones, transfer.shadows, 1.0);
            ASSERT_NEAR(working.samples[channel * kPixels + index], expected, 2e-6) << channel << " " << index;
        }
    }
    EXPECT_NE(working.result.transfer[0].midtones, working.result.transfer[2].midtones);

    // The byte output is the engine's conversion of that working image.
    const ByteOutput bytes = presentBytes(execution, plan({}), source.view());
    for (std::size_t index = 0u; index < kPixels; ++index)
    {
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            ASSERT_EQ(bytes.bytes[index * 3u + channel], engineByte(working.samples[channel * kPixels + index]));
        }
    }
}

TEST(DisplayPresentationTest, ALinkedStretchGivesEveryChannelTheTransferOfAllOfThem)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource source = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 8u));
    an::DisplayPresentationRequest request;
    request.linked = true;
    request.shadowsClipping = -2.0;
    request.targetBackground = 0.2;
    const WorkingOutput working = presentWorking(execution, request, source.view());
    std::vector<Location> locations;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        locations.push_back(exactLocation(channelValues(source.samples, kPixels, channel)));
    }
    const Transfer transfer = autoStretchOf(locations, -2.0, 0.2);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(working.result.transfer[channel].midtones, transfer.midtones, 1e-9);
        EXPECT_NEAR(working.result.transfer[channel].shadows, transfer.shadows, 1e-9);
        for (std::size_t index = 0u; index < kPixels; ++index)
        {
            const double expected =
                midtonesTransfer(source.samples[channel * kPixels + index], transfer.midtones, transfer.shadows, 1.0);
            ASSERT_NEAR(working.samples[channel * kPixels + index], expected, 2e-6);
        }
    }
}

TEST(DisplayPresentationTest, RgbaOutputRepeatsGreyAndCarriesOnlyASourcesOwnAlpha)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Linear;
    request.output = an::PresentationOutput::Rgba8;

    const FloatSource grey = floatSource(4u, 1u, an::ChannelSchema::Mono, {0.0f, 0.25f, 0.5f, 1.0f});
    const ByteOutput mono = presentBytes(execution, plan(request), grey.view());
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        const std::uint8_t expected = engineByte(grey.samples[x]);
        EXPECT_EQ(mono.bytes[x * 4u + 0u], expected);
        EXPECT_EQ(mono.bytes[x * 4u + 1u], expected);
        EXPECT_EQ(mono.bytes[x * 4u + 2u], expected);
        EXPECT_EQ(mono.bytes[x * 4u + 3u], 255u);
    }

    // RGBA: the colour is stretched, the alpha is carried as it is.
    const FloatSource withAlpha =
        floatSource(2u, 1u, an::ChannelSchema::Rgba, {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.25f, 0.75f});
    an::DisplayPresentationRequest stretched;
    stretched.output = an::PresentationOutput::Rgba8;
    const ByteOutput rgba = presentBytes(execution, plan(stretched), withAlpha.view());
    EXPECT_EQ(rgba.bytes[3], engineByte(0.25f));
    EXPECT_EQ(rgba.bytes[7], engineByte(0.75f));
    EXPECT_EQ(rgba.result.analysis.colorChannels, 3u);
    const WorkingOutput working = presentWorking(execution, stretched, withAlpha.view());
    EXPECT_EQ(working.samples[6], 0.25f);
    EXPECT_EQ(working.samples[7], 0.75f);
}

namespace
{

/// A planar source of one representation, for the byte-order comparisons.
template <typename SampleT> struct TypedSource
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;

    [[nodiscard]] an::DisplayPresentationSource view() const
    {
        return an::DisplayPresentationSource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = {}};
    }
};

/// A planar source whose samples come from a deterministic sequence spread over the representation's full scale.
template <typename SampleT>
[[nodiscard]] TypedSource<SampleT> sequenceSource(std::uint32_t width, std::uint32_t height, an::ChannelSchema schema,
                                                  std::uint64_t seed)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const an::ImageDescriptor descriptor = describe(type, width, height, schema);
    const std::uint32_t channels = descriptor.extent().channelCount();
    Sequence sequence{seed};
    std::vector<SampleT> samples(static_cast<std::size_t>(width) * height * channels);
    for (SampleT& sample : samples)
    {
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            sample = static_cast<SampleT>(0.02f + 0.3f * sequence.next());
        }
        else
        {
            sample = static_cast<SampleT>(sequence.next() * static_cast<float>((std::numeric_limits<SampleT>::max)()));
        }
    }
    return TypedSource<SampleT>{descriptor, planarLayout(type, width, height, channels), std::move(samples)};
}

/// Presents through the preview plan into tightly packed bytes of its output.
[[nodiscard]] std::vector<std::uint8_t> presentPreviewBytes(const an::ExecutionContext& execution,
                                                            const an::DisplayPresentationRequest& request,
                                                            std::uint32_t factor,
                                                            const an::DisplayPresentationSource& source)
{
    const an::RealtimePreviewDisplayPresentationPlan made = previewPlan(request, factor);
    an::Result<an::ImageLayoutRequest> layoutRequest =
        an::realtimePreviewDisplayPresentationLayout(made, *source.descriptor);
    EXPECT_TRUE(layoutRequest.hasValue());
    layoutRequest->rowAlignmentBytes = 1u;
    const an::ImageLayout layout = layoutFrom(*layoutRequest);
    std::vector<std::uint8_t> bytes(layout.totalBytes(), 0xCDu);
    const an::Result<an::DisplayPresentationResult> result =
        an::presentImageRealtimePreview(execution, made, source, bytes.data(), layout);
    EXPECT_TRUE(result.hasValue()) << result.status().message();
    return bytes;
}

/// Expects every four bytes of a blue-first output to be the red-first output's with red and blue exchanged.
void expectRedAndBlueExchanged(const std::vector<std::uint8_t>& rgba, const std::vector<std::uint8_t>& bgra,
                               const char* what)
{
    ASSERT_EQ(rgba.size(), bgra.size()) << what;
    for (std::size_t pixel = 0u; pixel * 4u < rgba.size(); ++pixel)
    {
        ASSERT_EQ(bgra[pixel * 4u + 0u], rgba[pixel * 4u + 2u]) << what << ", pixel " << pixel;
        ASSERT_EQ(bgra[pixel * 4u + 1u], rgba[pixel * 4u + 1u]) << what << ", pixel " << pixel;
        ASSERT_EQ(bgra[pixel * 4u + 2u], rgba[pixel * 4u + 0u]) << what << ", pixel " << pixel;
        ASSERT_EQ(bgra[pixel * 4u + 3u], rgba[pixel * 4u + 3u]) << what << ", pixel " << pixel;
    }
}

} // namespace

// Blue, green, red and alpha bytes are what a toolkit paints without converting them. An opaque pixel is the red-first
// output in the other order on every path a presentation takes - the direct one for a narrow code, a grey source and a
// float, the one that packs a stretched working image, and the preview's subsampled one; a pixel with alpha carries its
// colour premultiplied, so no colour byte stands above its alpha byte.
TEST(DisplayPresentationTest, BgraOutputIsTheRgbaBytesInTheOtherOrderWithTheColourPremultipliedByAlpha)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::DisplayPresentationRequest linear;
    linear.stretch = an::PresentationStretch::Linear;
    const an::DisplayPresentationRequest automatic{};
    const auto compare = [&](const an::DisplayPresentationRequest& request, const an::DisplayPresentationSource& source,
                             std::uint32_t factor, const char* what) {
        an::DisplayPresentationRequest red = request;
        red.output = an::PresentationOutput::Rgba8;
        an::DisplayPresentationRequest blue = request;
        blue.output = an::PresentationOutput::Bgra8;
        if (factor == 1u)
        {
            expectRedAndBlueExchanged(presentBytes(execution, plan(red), source).bytes,
                                      presentBytes(execution, plan(blue), source).bytes, what);
        }
        else
        {
            expectRedAndBlueExchanged(presentPreviewBytes(execution, red, factor, source),
                                      presentPreviewBytes(execution, blue, factor, source), what);
        }
    };

    const TypedSource<std::uint16_t> codes = sequenceSource<std::uint16_t>(7u, 5u, an::ChannelSchema::Rgb, 3u);
    const TypedSource<std::uint16_t> greyCodes = sequenceSource<std::uint16_t>(7u, 5u, an::ChannelSchema::Mono, 4u);
    const TypedSource<std::int16_t> signedCodes = sequenceSource<std::int16_t>(7u, 5u, an::ChannelSchema::Rgb, 6u);
    const TypedSource<float> floats = sequenceSource<float>(7u, 5u, an::ChannelSchema::Rgb, 5u);
    compare(linear, codes.view(), 1u, "sixteen-bit codes, linear");
    compare(linear, greyCodes.view(), 1u, "sixteen-bit grey, linear");
    compare(linear, signedCodes.view(), 1u, "signed sixteen-bit codes, linear");
    compare(linear, floats.view(), 1u, "floats, linear");
    compare(automatic, codes.view(), 1u, "sixteen-bit codes, stretched");
    compare(automatic, floats.view(), 1u, "floats, stretched");
    compare(linear, codes.view(), 2u, "sixteen-bit codes, every second sample");
    compare(automatic, floats.view(), 3u, "floats, stretched, every third sample");

    // An invalid sample is black and opaque, or every byte zero when it is to be transparent, in either order.
    std::vector<float> holes = floats.samples;
    holes[2] = std::numeric_limits<float>::quiet_NaN();
    holes[35u + 9u] = std::numeric_limits<float>::infinity();
    const TypedSource<float> withHoles{floats.descriptor, floats.layout, holes};
    an::DisplayPresentationRequest transparent;
    transparent.invalidPixels = an::PresentationInvalidPixels::Transparent;
    compare(linear, withHoles.view(), 1u, "floats with invalid samples, black");
    compare(transparent, withHoles.view(), 1u, "floats with invalid samples, transparent");
    transparent.output = an::PresentationOutput::Bgra8;
    const ByteOutput cleared = presentBytes(execution, plan(transparent), withHoles.view());
    for (std::uint32_t byte = 0u; byte < 4u; ++byte)
    {
        EXPECT_EQ(cleared.bytes[2u * 4u + byte], 0u);
        EXPECT_EQ(cleared.bytes[9u * 4u + byte], 0u);
    }

    // A source's own alpha: each colour is its working sample times the alpha, then a byte.
    const FloatSource withAlpha = floatSource(
        3u, 1u, an::ChannelSchema::Rgba, {0.9f, 0.2f, 0.6f, 0.4f, 0.5f, 1.0f, 0.3f, 0.7f, 0.05f, 1.0f, 0.5f, 0.0f});
    for (const an::DisplayPresentationRequest& request : {linear, automatic})
    {
        an::DisplayPresentationRequest blue = request;
        blue.output = an::PresentationOutput::Bgra8;
        const ByteOutput bytes = presentBytes(execution, plan(blue), withAlpha.view());
        const WorkingOutput working = presentWorking(execution, request, withAlpha.view());
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            const float alpha = working.samples[9u + x];
            const std::uint8_t* pixel = bytes.bytes.data() + static_cast<std::size_t>(x) * 4u;
            EXPECT_EQ(pixel[0], engineByte(working.samples[6u + x] * alpha)) << x;
            EXPECT_EQ(pixel[1], engineByte(working.samples[3u + x] * alpha)) << x;
            EXPECT_EQ(pixel[2], engineByte(working.samples[x] * alpha)) << x;
            EXPECT_EQ(pixel[3], engineByte(alpha)) << x;
            for (std::uint32_t colour = 0u; colour < 3u; ++colour)
            {
                EXPECT_LE(pixel[colour], pixel[3]) << "a premultiplied colour above its alpha, pixel " << x;
            }
        }
    }
}

TEST(DisplayPresentationTest, AnInvalidSampleIsNeverMeasuredAndIsShownBlackOrTransparent)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> samples = skyFrame(kWidth, kHeight, 3u, 12u);
    // Red alone is invalid at pixel 3; every channel at pixel 10; green is infinite at pixel 20.
    samples[3] = std::numeric_limits<float>::quiet_NaN();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        samples[channel * kPixels + 10u] = std::numeric_limits<float>::quiet_NaN();
    }
    samples[kPixels + 20u] = std::numeric_limits<float>::infinity();
    const FloatSource source = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, samples);

    const WorkingOutput working = presentWorking(execution, an::DisplayPresentationRequest{}, source.view());
    EXPECT_EQ(working.result.invalidPixels, 3u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::vector<double> valid;
        for (std::size_t index = 0u; index < kPixels; ++index)
        {
            const float value = samples[channel * kPixels + index];
            if (std::isfinite(value))
            {
                valid.push_back(value);
            }
        }
        const Location location = exactLocation(valid);
        EXPECT_NEAR(working.result.analysis.medians[channel], location.median, 1e-7);
        EXPECT_EQ(working.result.analysis.measuredSamples[channel], valid.size());
        const Transfer transfer = autoStretchOf({location}, an::kDisplayShadowsClipping, an::kDisplayTargetBackground);
        // A pixel with an invalid red still has its green and blue stretched.
        if (channel != 0u)
        {
            EXPECT_NEAR(working.samples[channel * kPixels + 3u],
                        midtonesTransfer(samples[channel * kPixels + 3u], transfer.midtones, transfer.shadows, 1.0),
                        2e-6);
        }
    }
    EXPECT_TRUE(std::isnan(working.samples[3]));
    EXPECT_TRUE(std::isnan(working.samples[kPixels + 20u]));

    const ByteOutput black = presentBytes(execution, plan({}), source.view());
    EXPECT_EQ(black.bytes[3u * 3u + 0u], 0u);
    EXPECT_EQ(black.bytes[3u * 3u + 1u], engineByte(working.samples[kPixels + 3u]));
    EXPECT_EQ(black.bytes[10u * 3u + 2u], 0u);
    EXPECT_EQ(black.result.invalidPixels, 3u);

    an::DisplayPresentationRequest transparent;
    transparent.output = an::PresentationOutput::Rgba8;
    transparent.invalidPixels = an::PresentationInvalidPixels::Transparent;
    const ByteOutput rgba = presentBytes(execution, plan(transparent), source.view());
    for (const std::size_t pixel : {std::size_t{3u}, std::size_t{10u}, std::size_t{20u}})
    {
        for (std::uint32_t byte = 0u; byte < 4u; ++byte)
        {
            EXPECT_EQ(rgba.bytes[pixel * 4u + byte], 0u) << pixel;
        }
    }
    EXPECT_EQ(rgba.bytes[4u * 4u + 3u], 255u);
    transparent.invalidPixels = an::PresentationInvalidPixels::Black;
    const ByteOutput opaque = presentBytes(execution, plan(transparent), source.view());
    EXPECT_EQ(opaque.bytes[3u * 4u + 0u], 0u);
    EXPECT_EQ(opaque.bytes[3u * 4u + 3u], 255u);
}

TEST(DisplayPresentationTest, AMaskAndAZeroRuleSelectTheSamplesAPresentationShows)
{
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 5u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ImageDescriptor descriptor = describe(an::SampleType::UInt16, kWidth, kHeight, an::ChannelSchema::Mono);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<std::uint16_t> codes(static_cast<std::size_t>(kWidth) * kHeight, 30000u);
    codes[4] = 0u;
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Linear;

    const std::optional<an::ValidityDescriptor> zero =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zero.has_value());
    const ByteOutput zeroRule =
        presentBytes(execution, plan(request),
                     an::DisplayPresentationSource{.descriptor = &descriptor,
                                                   .samples = codes.data(),
                                                   .layout = &layout,
                                                   .validity = an::KernelValidityView{.descriptor = *zero}});
    EXPECT_EQ(zeroRule.result.invalidPixels, 1u);

    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(2u) * kHeight, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, *maskExtent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    ASSERT_TRUE(writable->setValid(0u, 7u, 2u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const std::optional<an::ValidityDescriptor> masked =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(masked.has_value());
    const ByteOutput maskRule = presentBytes(
        execution, plan(request),
        an::DisplayPresentationSource{.descriptor = &descriptor,
                                      .samples = codes.data(),
                                      .layout = &layout,
                                      .validity = an::KernelValidityView{.descriptor = *masked, .mask = &mask}});
    EXPECT_EQ(maskRule.result.invalidPixels, 1u);
    EXPECT_EQ(maskRule.bytes[2u * kWidth + 7u], 0u);
    const std::uint8_t level = an::convertSample<std::uint16_t, std::uint8_t>(30000u, an::ConversionPolicy{}).value;
    EXPECT_EQ(maskRule.bytes[0], level);

    // A mask the rule does not read, and a rule with no mask, are refused.
    std::vector<std::uint8_t> destination(static_cast<std::size_t>(kWidth) * kHeight);
    const an::ImageLayout destinationLayout = planarLayout(an::SampleType::UInt8, kWidth, kHeight);
    const an::Result<an::DisplayPresentationResult> unused =
        an::presentImage(execution, plan(request),
                         an::DisplayPresentationSource{.descriptor = &descriptor,
                                                       .samples = codes.data(),
                                                       .layout = &layout,
                                                       .validity = an::KernelValidityView{.mask = &mask}},
                         destination.data(), destinationLayout);
    ASSERT_FALSE(unused.hasValue());
    EXPECT_EQ(unused.status().code(), an::ErrorCode::InvalidArgument);
    const an::Result<an::DisplayPresentationResult> missing =
        an::presentImage(execution, plan(request),
                         an::DisplayPresentationSource{.descriptor = &descriptor,
                                                       .samples = codes.data(),
                                                       .layout = &layout,
                                                       .validity = an::KernelValidityView{.descriptor = *masked}},
                         destination.data(), destinationLayout);
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DisplayPresentationTest, NeutralizationMovesEveryBackgroundOntoTheirMean)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource source = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 21u));
    an::DisplayPresentationRequest request;
    request.neutralize = true;
    request.neutralizationBackground = {0.0, 0.1};
    request.stretch = an::PresentationStretch::Linear;
    const WorkingOutput working = presentWorking(execution, request, source.view());
    ASSERT_TRUE(working.result.analysis.neutralized);
    std::array<double, 3u> levels{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::vector<double> inside;
        for (const double value : channelValues(source.samples, kPixels, channel))
        {
            if (value >= 0.0 && value <= 0.1)
            {
                inside.push_back(value);
            }
        }
        levels[channel] = exactMedian(inside);
    }
    const double pedestal = (levels[0] + levels[1] + levels[2]) / 3.0;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const double offset = pedestal - levels[channel];
        EXPECT_NEAR(working.result.analysis.neutralizationOffsets[channel], offset, 1e-7);
        for (std::size_t index = 0u; index < kPixels; ++index)
        {
            const double expected = std::clamp(source.samples[channel * kPixels + index] + offset, 0.0, 1.0);
            ASSERT_NEAR(working.samples[channel * kPixels + index], expected, 1e-6);
        }
    }
    EXPECT_FALSE(working.result.analysis.calibrated);
}

TEST(DisplayPresentationTest, CalibrationScalesEachChannelAboutItsBackground)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource source = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 34u));
    an::DisplayPresentationRequest request;
    request.calibrate = true;
    request.calibrationBackground = {0.0, 0.1};
    request.calibrationWhite = {0.0, 0.9};
    request.stretch = an::PresentationStretch::Linear;
    const WorkingOutput working = presentWorking(execution, request, source.view());
    ASSERT_TRUE(working.result.analysis.calibrated);
    std::array<double, 3u> above{};
    std::array<double, 3u> background{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::vector<double> sky;
        double whiteSum = 0.0;
        std::size_t whiteCount = 0u;
        for (const double value : channelValues(source.samples, kPixels, channel))
        {
            if (value >= 0.0 && value <= 0.1)
            {
                sky.push_back(value);
            }
            if (value >= 0.0 && value <= 0.9)
            {
                whiteSum += value;
                ++whiteCount;
            }
        }
        background[channel] = exactMedian(sky);
        above[channel] = std::abs(whiteSum / static_cast<double>(whiteCount) - background[channel]);
    }
    const double strongest = (std::max)({above[0], above[1], above[2]});
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const double gain = strongest / above[channel];
        const double offset = background[channel] * (1.0 - gain);
        EXPECT_NEAR(working.result.analysis.calibrationGains[channel], gain, 1e-6);
        EXPECT_NEAR(working.result.analysis.calibrationOffsets[channel], offset, 1e-7);
        for (std::size_t index = 0u; index < kPixels; ++index)
        {
            const double expected = std::clamp(source.samples[channel * kPixels + index] * gain + offset, 0.0, 1.0);
            ASSERT_NEAR(working.samples[channel * kPixels + index], expected, 2e-6);
        }
    }
}

TEST(DisplayPresentationTest, ABalanceIsRefusedOnGreyAndWhenItsIntervalHoldsNothing)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource grey = floatSource(3u, 3u, an::ChannelSchema::Mono, std::vector<float>(9u, 0.05f));
    std::vector<std::uint8_t> bytes(9u);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt8, 3u, 3u);
    an::DisplayPresentationRequest request;
    request.neutralize = true;
    const an::Result<an::DisplayPresentationResult> refused =
        an::presentImage(execution, plan(request), grey.view(), bytes.data(), layout);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);

    const FloatSource bright = floatSource(3u, 3u, an::ChannelSchema::Rgb, std::vector<float>(27u, 0.6f));
    std::vector<std::uint8_t> colour(27u);
    const an::ImageLayout colourLayout = interleavedLayout(an::SampleType::UInt8, 3u, 3u, 3u);
    const an::Result<an::DisplayPresentationResult> empty =
        an::presentImage(execution, plan(request), bright.view(), colour.data(), colourLayout);
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);

    an::DisplayPresentationRequest flat;
    flat.calibrate = true;
    flat.calibrationBackground = {0.0, 1.0};
    const an::Result<an::DisplayPresentationResult> noWhite =
        an::presentImage(execution, plan(flat), bright.view(), colour.data(), colourLayout);
    ASSERT_FALSE(noWhite.hasValue());
    EXPECT_EQ(noWhite.status().code(), an::ErrorCode::InvalidData);
}

TEST(DisplayPresentationTest, SaturationIsTheColourOperationOnTheStretchedImage)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource source = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 55u));
    const WorkingOutput stretched = presentWorking(execution, an::DisplayPresentationRequest{}, source.view());

    an::DisplayPresentationRequest request;
    request.saturation = 0.6;
    request.saturationBackgroundFactor = 0.8;
    request.saturationBackgroundLocation = an::SaturationBackgroundLocation::Mean;
    const WorkingOutput saturated = presentWorking(execution, request, source.view());

    // The same two published colour operations, called on the stretched image directly.
    std::vector<float> expected = stretched.samples;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);
    const an::Result<double> background = an::deriveSaturationEnhancementBackground(
        execution, an::SampleType::Float32, expected.data(), layout, 0.8, an::SaturationBackgroundLocation::Mean);
    ASSERT_TRUE(background.hasValue());
    EXPECT_NEAR(saturated.result.saturationBackground, std::clamp(*background, 0.0, 1.0), 1e-12);
    ASSERT_TRUE(
        an::enhanceImageSaturation(
            execution, an::SampleType::Float32, expected.data(), layout, expected.data(), layout,
            an::SaturationEnhancementParameters{.coefficient = 0.6, .background = std::clamp(*background, 0.0, 1.0)})
            .hasValue());
    ASSERT_EQ(saturated.samples.size(), expected.size());
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        ASSERT_EQ(saturated.samples[index], expected[index]) << index;
    }

    // Grey has no saturation to enhance.
    const FloatSource grey = floatSource(kWidth, kHeight, an::ChannelSchema::Mono, skyFrame(kWidth, kHeight, 1u, 55u));
    const WorkingOutput greyOutput = presentWorking(execution, request, grey.view());
    EXPECT_EQ(greyOutput.result.saturationBackground, 0.0);
}

TEST(DisplayPresentationTest, BrightnessThenContrastEachClampToTheUnitInterval)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> ramp{0.0f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f, 1.4f};
    const FloatSource source = floatSource(8u, 1u, an::ChannelSchema::Mono, ramp);
    for (const std::array<double, 2u> setting :
         {std::array<double, 2u>{0.2, 1.0}, std::array<double, 2u>{-0.15, 1.6}, std::array<double, 2u>{0.0, 0.4}})
    {
        an::DisplayPresentationRequest request;
        request.stretch = an::PresentationStretch::Linear;
        request.brightness = setting[0];
        request.contrast = setting[1];
        const WorkingOutput working = presentWorking(execution, request, source.view());
        for (std::size_t index = 0u; index < ramp.size(); ++index)
        {
            const double bright = std::clamp(static_cast<double>(ramp[index]) + setting[0], 0.0, 1.0);
            const double expected = std::clamp((bright - 0.5) * setting[1] + 0.5, 0.0, 1.0);
            EXPECT_NEAR(working.samples[index], expected, 1e-6) << setting[0] << " " << setting[1] << " " << index;
        }
    }
}

TEST(DisplayPresentationTest, AnAnalysisIsReusedOnlyWhereItWasTakenUnderTheSameDecisions)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 9u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource whole = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 61u));
    const FloatSource other = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 62u));
    an::DisplayPresentationRequest request;
    request.neutralize = true;
    const an::DisplayPresentationPlan made = plan(request);
    const an::Result<an::DisplayPresentationAnalysis> analysis =
        an::analyzeDisplayPresentation(execution, made, whole.view());
    ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
    EXPECT_TRUE(analysis->measured);
    EXPECT_TRUE(analysis->neutralized);
    EXPECT_FALSE(analysis->realtimePreview);

    // The whole image's analysis presents another image exactly as the whole image is presented.
    const ByteOutput direct = presentBytes(execution, made, whole.view());
    const ByteOutput reused = presentBytes(execution, made, whole.view(), &*analysis);
    EXPECT_TRUE(reused.result.analysisReused);
    EXPECT_EQ(direct.bytes, reused.bytes);
    const ByteOutput through = presentBytes(execution, made, other.view(), &*analysis);
    const ByteOutput own = presentBytes(execution, made, other.view());
    EXPECT_NE(through.bytes, own.bytes);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(through.result.transfer[channel].midtones, direct.result.transfer[channel].midtones);
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kWidth) * kHeight * 3u);
    const an::ImageLayout layout = interleavedLayout(an::SampleType::UInt8, kWidth, kHeight, 3u);
    const auto refusedWith = [&](const an::DisplayPresentationAnalysis& candidate, an::ErrorCode code) {
        const an::Result<an::DisplayPresentationResult> refused =
            an::presentImage(execution, made, whole.view(), bytes.data(), layout, &candidate);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), code) << refused.status().message();
    };
    an::DisplayPresentationAnalysis candidate = *analysis;
    candidate.realtimePreview = true;
    refusedWith(candidate, an::ErrorCode::IncompatibleArguments);
    candidate = *analysis;
    candidate.promotion = an::PresentationPromotion::Quantized16;
    refusedWith(candidate, an::ErrorCode::IncompatibleArguments);
    candidate = *analysis;
    candidate.neutralized = false;
    refusedWith(candidate, an::ErrorCode::IncompatibleArguments);
    candidate = *analysis;
    candidate.colorChannels = 1u;
    refusedWith(candidate, an::ErrorCode::IncompatibleArguments);
    candidate = *analysis;
    candidate.measured = false;
    refusedWith(candidate, an::ErrorCode::IncompatibleArguments);
    candidate = *analysis;
    candidate.medianAbsoluteDeviations[1] = -1.0;
    refusedWith(candidate, an::ErrorCode::InvalidArgument);

    // A preview accepts its own analysis and a production one.
    const an::RealtimePreviewDisplayPresentationPlan preview = previewPlan(request, 2u);
    const an::Result<an::ImageLayoutRequest> previewRequest =
        an::realtimePreviewDisplayPresentationLayout(preview, whole.descriptor);
    ASSERT_TRUE(previewRequest.hasValue());
    const an::ImageLayout previewLayout = layoutFrom(*previewRequest);
    std::vector<std::uint8_t> previewBytes(previewLayout.totalBytes());
    const an::Result<an::DisplayPresentationResult> previewed = an::presentImageRealtimePreview(
        execution, preview, whole.view(), previewBytes.data(), previewLayout, &*analysis);
    ASSERT_TRUE(previewed.hasValue()) << previewed.status().message();
    EXPECT_TRUE(previewed->analysisReused);
    candidate = *analysis;
    candidate.realtimePreview = true;
    EXPECT_TRUE(an::presentImageRealtimePreview(execution, preview, whole.view(), previewBytes.data(), previewLayout,
                                                &candidate)
                    .hasValue());
}

TEST(DisplayPresentationTest, AReusedAnalysisStretchesEachRowAsItPromotesItAndWritesWhatTwoPassesWrite)
{
    // Over a prepared analysis that balances and adjusts nothing the stretch is the only step after the promotion, so
    // each row is stretched as it is promoted. What that writes is what a measured call writes in two passes - for
    // grey, colour and colour with alpha, Float32 and sixteen-bit codes, as a working image, as bytes and as a
    // preview, on the compiled and the reference backends - and a source holding one invalid sample, whose stretch
    // the whole image decides, takes the two passes and writes it too.
    constexpr std::uint32_t kWidth = 157u;
    constexpr std::uint32_t kHeight = 61u;
    const an::EngineContext engine = makeEngine();
    const FloatSource grey = floatSource(kWidth, kHeight, an::ChannelSchema::Mono, skyFrame(kWidth, kHeight, 1u, 91u));
    const FloatSource colour = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 92u));
    const FloatSource alpha = floatSource(kWidth, kHeight, an::ChannelSchema::Rgba, skyFrame(kWidth, kHeight, 4u, 93u));
    std::vector<float> holed = skyFrame(kWidth, kHeight, 3u, 94u);
    holed[kWidth * 37u + 11u] = std::numeric_limits<float>::quiet_NaN();
    const FloatSource invalid = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, std::move(holed));
    const an::ImageDescriptor codeDescriptor =
        describe(an::SampleType::UInt16, kWidth, kHeight, an::ChannelSchema::Mono);
    const an::ImageLayout codeLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    std::vector<std::uint16_t> codes;
    for (const float sample : skyFrame(kWidth, kHeight, 1u, 95u))
    {
        codes.push_back(static_cast<std::uint16_t>(std::clamp(sample, 0.0f, 1.0f) * 65535.0f));
    }
    const std::array<an::DisplayPresentationSource, 5u> sources{
        grey.view(), colour.view(), alpha.view(), invalid.view(),
        an::DisplayPresentationSource{.descriptor = &codeDescriptor, .samples = codes.data(), .layout = &codeLayout}};
    const auto bits = [](const std::vector<float>& samples) {
        std::vector<std::uint32_t> patterns;
        for (const float sample : samples)
        {
            patterns.push_back(std::bit_cast<std::uint32_t>(sample));
        }
        return patterns;
    };
    for (const an::ComputeBackend backend : {an::ComputeBackend::Automatic, an::ComputeBackend::Reference})
    {
        SCOPED_TRACE(static_cast<int>(backend));
        const an::ExecutionContext execution = makeExecution(engine, 3u, backend);
        for (std::size_t index = 0u; index < sources.size(); ++index)
        {
            SCOPED_TRACE(index);
            const an::DisplayPresentationSource& source = sources[index];
            const an::DisplayPresentationRequest request;
            const WorkingOutput measured = presentWorking(execution, request, source);
            const WorkingOutput reused = presentWorking(execution, request, source, &measured.result.analysis);
            EXPECT_TRUE(reused.result.analysisReused);
            EXPECT_EQ(bits(reused.samples), bits(measured.samples));
            EXPECT_EQ(reused.result.invalidPixels, measured.result.invalidPixels);
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                EXPECT_EQ(reused.result.transfer[channel].midtones, measured.result.transfer[channel].midtones);
                EXPECT_EQ(reused.result.transfer[channel].shadows, measured.result.transfer[channel].shadows);
            }

            const an::DisplayPresentationPlan made = plan(request);
            const ByteOutput measuredBytes = presentBytes(execution, made, source);
            const ByteOutput reusedBytes = presentBytes(execution, made, source, &measuredBytes.result.analysis);
            EXPECT_EQ(reusedBytes.bytes, measuredBytes.bytes);

            const an::RealtimePreviewDisplayPresentationPlan preview = previewPlan(request, 2u);
            const an::Result<an::ImageLayoutRequest> previewRequest =
                an::realtimePreviewDisplayPresentationLayout(preview, *source.descriptor);
            ASSERT_TRUE(previewRequest.hasValue()) << previewRequest.status().message();
            const an::ImageLayout previewLayout = layoutFrom(*previewRequest);
            std::vector<std::uint8_t> previewMeasured(previewLayout.totalBytes(), 0xCDu);
            const an::Result<an::DisplayPresentationResult> previewed = an::presentImageRealtimePreview(
                execution, preview, source, previewMeasured.data(), previewLayout, nullptr);
            ASSERT_TRUE(previewed.hasValue()) << previewed.status().message();
            std::vector<std::uint8_t> previewReused(previewLayout.totalBytes(), 0xCDu);
            const an::Result<an::DisplayPresentationResult> again = an::presentImageRealtimePreview(
                execution, preview, source, previewReused.data(), previewLayout, &previewed->analysis);
            ASSERT_TRUE(again.hasValue()) << again.status().message();
            EXPECT_EQ(previewReused, previewMeasured);
        }
    }

    // A working image too large for the caches is written with streaming stores, and holds the same samples.
    constexpr std::uint32_t kLargeWidth = 2048u;
    constexpr std::uint32_t kLargeHeight = 1400u;
    const FloatSource large =
        floatSource(kLargeWidth, kLargeHeight, an::ChannelSchema::Rgb, skyFrame(kLargeWidth, kLargeHeight, 3u, 96u));
    ASSERT_GE(large.samples.size() * sizeof(float), std::size_t{32u} << 20u);
    const an::ExecutionContext wide = makeExecution(engine, 4u);
    const WorkingOutput measured = presentWorking(wide, an::DisplayPresentationRequest{}, large.view());
    const WorkingOutput reused =
        presentWorking(wide, an::DisplayPresentationRequest{}, large.view(), &measured.result.analysis);
    EXPECT_TRUE(reused.result.analysisReused);
    EXPECT_EQ(bits(reused.samples), bits(measured.samples));
}

TEST(DisplayPresentationTest, APreviewPresentsAndMeasuresEveryNthSampleOfEveryNthRow)
{
    constexpr std::uint32_t kWidth = 10u;
    constexpr std::uint32_t kHeight = 7u;
    constexpr std::uint32_t kFactor = 3u;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource source =
        floatSource(kWidth, kHeight, an::ChannelSchema::Mono, skyFrame(kWidth, kHeight, 1u, 77u));
    const an::RealtimePreviewDisplayPresentationPlan preview = previewPlan({}, kFactor);
    const an::Result<an::ImageLayoutRequest> layoutRequest =
        an::realtimePreviewDisplayPresentationLayout(preview, source.descriptor);
    ASSERT_TRUE(layoutRequest.hasValue());
    const an::ImageLayout layout = layoutFrom(*layoutRequest);
    std::vector<std::uint8_t> bytes(layout.totalBytes(), 0xCDu);
    const an::Result<an::DisplayPresentationResult> result =
        an::presentImageRealtimePreview(execution, preview, source.view(), bytes.data(), layout);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(result->width, 4u);
    EXPECT_EQ(result->height, 3u);
    EXPECT_TRUE(result->analysis.realtimePreview);

    std::vector<double> presented;
    for (std::uint32_t y = 0u; y < kHeight; y += kFactor)
    {
        for (std::uint32_t x = 0u; x < kWidth; x += kFactor)
        {
            presented.push_back(source.samples[static_cast<std::size_t>(y) * kWidth + x]);
        }
    }
    const Location location = exactLocation(presented);
    EXPECT_NEAR(result->analysis.medians[0], location.median, 1e-7);
    EXPECT_EQ(result->analysis.measuredSamples[0], presented.size());
    const Transfer transfer = autoStretchOf({location}, an::kDisplayShadowsClipping, an::kDisplayTargetBackground);
    std::size_t index = 0u;
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            const double expected = midtonesTransfer(presented[index++], transfer.midtones, transfer.shadows, 1.0);
            const std::uint8_t byte = bytes[y * layout.rowStrideBytes() + x];
            EXPECT_LE(std::abs(static_cast<int>(byte) - static_cast<int>(engineByte(static_cast<float>(expected)))), 1);
        }
    }

    // The production plan refuses the preview's analysis.
    std::vector<std::uint8_t> full(static_cast<std::size_t>(kWidth) * kHeight);
    const an::ImageLayout fullLayout = planarLayout(an::SampleType::UInt8, kWidth, kHeight);
    const an::Result<an::DisplayPresentationResult> refused =
        an::presentImage(execution, plan({}), source.view(), full.data(), fullLayout, &result->analysis);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(DisplayPresentationTest, Quantized16HoldsAFloatSampleToSixteenBitLevels)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> values{0.0f, 0.1234567f, 0.5f, 0.999999f, 1.3f, -0.2f};
    const FloatSource source = floatSource(6u, 1u, an::ChannelSchema::Mono, values);
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Linear;
    request.promotion = an::PresentationPromotion::Quantized16;
    const WorkingOutput working = presentWorking(execution, request, source.view());
    for (std::size_t index = 0u; index < values.size(); ++index)
    {
        const double clamped = std::clamp(static_cast<double>(values[index]), 0.0, 1.0);
        const double level = std::nearbyint(clamped * 65535.0) / 65535.0;
        EXPECT_EQ(working.samples[index], static_cast<float>(level)) << index;
    }
    EXPECT_EQ(working.result.analysis.promotion, an::PresentationPromotion::Quantized16);

    // A sixteen-bit source has nothing between its levels to lose.
    const an::ImageDescriptor descriptor = describe(an::SampleType::UInt16, 4u, 1u, an::ChannelSchema::Mono);
    const an::ImageLayout layout = planarLayout(an::SampleType::UInt16, 4u, 1u);
    const std::vector<std::uint16_t> codes{0u, 1u, 40000u, 65535u};
    const an::DisplayPresentationSource codesSource{
        .descriptor = &descriptor, .samples = codes.data(), .layout = &layout};
    const WorkingOutput quantized = presentWorking(execution, request, codesSource);
    request.promotion = an::PresentationPromotion::Gradient;
    const WorkingOutput gradient = presentWorking(execution, request, codesSource);
    EXPECT_EQ(quantized.samples, gradient.samples);
}

TEST(DisplayPresentationTest, AnIntegerIsPresentedAtItsFullScaleAndAFloatingQuantityIsRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::DisplayPresentationRequest request;
    request.stretch = an::PresentationStretch::Linear;

    const an::ImageDescriptor signedDescriptor = describe(an::SampleType::Int16, 4u, 1u, an::ChannelSchema::Mono);
    const an::ImageLayout signedLayout = planarLayout(an::SampleType::Int16, 4u, 1u);
    const std::vector<std::int16_t> signedCodes{-32768, -1, 16384, 32767};
    const ByteOutput signedBytes =
        presentBytes(execution, plan(request),
                     an::DisplayPresentationSource{
                         .descriptor = &signedDescriptor, .samples = signedCodes.data(), .layout = &signedLayout});
    EXPECT_EQ(signedBytes.bytes[0], 0u);
    EXPECT_EQ(signedBytes.bytes[1], 0u);
    EXPECT_EQ(signedBytes.bytes[2], engineByte(an::convertSample<std::int16_t, float>(16384, {}).value));
    EXPECT_EQ(signedBytes.bytes[3], 255u);

    const an::ImageDescriptor sensor =
        describe(an::SampleType::UInt16, 2u, 1u, an::ChannelSchema::Mono, an::NumericDomain::RawSensorCode);
    const an::ImageLayout sensorLayout = planarLayout(an::SampleType::UInt16, 2u, 1u);
    const std::vector<std::uint16_t> sensorCodes{32768u, 65535u};
    const ByteOutput sensorBytes = presentBytes(
        execution, plan(request),
        an::DisplayPresentationSource{.descriptor = &sensor, .samples = sensorCodes.data(), .layout = &sensorLayout});
    EXPECT_EQ(sensorBytes.bytes[1], 255u);

    for (const an::NumericDomain domain : {an::NumericDomain::PhysicalScaled, an::NumericDomain::RawSensorCode})
    {
        const an::ImageDescriptor physical = describe(an::SampleType::Float32, 2u, 1u, an::ChannelSchema::Mono, domain);
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 2u, 1u);
        const std::vector<float> values{100.0f, 2000.0f};
        std::vector<std::uint8_t> bytes(2u);
        const an::ImageLayout byteLayout = planarLayout(an::SampleType::UInt8, 2u, 1u);
        const an::Result<an::DisplayPresentationResult> refused = an::presentImage(
            execution, plan(request),
            an::DisplayPresentationSource{.descriptor = &physical, .samples = values.data(), .layout = &layout},
            bytes.data(), byteLayout);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
    }
}

TEST(DisplayPresentationTest, ASampleOutsideTheUnitIntervalIsPresentedAndMeasuredAsTheDisplayHoldsIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // Signed samples mostly below zero: the stretch measures them as black, and presents without a refusal.
    const an::ImageDescriptor descriptor = describe(an::SampleType::Int16, 9u, 1u, an::ChannelSchema::Mono);
    const an::ImageLayout layout = planarLayout(an::SampleType::Int16, 9u, 1u);
    const std::vector<std::int16_t> codes{-30000, -20000, -9000, -100, -50, 50, 3000, 9000, 32767};
    const WorkingOutput working = presentWorking(
        execution, an::DisplayPresentationRequest{},
        an::DisplayPresentationSource{.descriptor = &descriptor, .samples = codes.data(), .layout = &layout});
    ASSERT_TRUE(working.result.analysis.measured);
    EXPECT_EQ(working.result.analysis.medians[0], 0.0);
    for (const float sample : working.samples)
    {
        EXPECT_GE(sample, 0.0f);
        EXPECT_LE(sample, 1.0f);
    }
    EXPECT_EQ(working.samples[8], 1.0f);

    // Floats above one are white under a linear presentation, and the working image says so.
    const FloatSource bright = floatSource(3u, 1u, an::ChannelSchema::Mono, {0.5f, 1.0f, 4.0f});
    an::DisplayPresentationRequest linear;
    linear.stretch = an::PresentationStretch::Linear;
    const WorkingOutput held = presentWorking(execution, linear, bright.view());
    EXPECT_EQ(held.samples[2], 1.0f);
}

TEST(DisplayPresentationTest, AnAnalysisAloneMeasuresAFloatSourceWhereItLiesAsTheDisplayHoldsIt)
{
    // An analysis alone over a Float32 source that balances nothing promotes no working image: the statistics read
    // the stored samples held to the unit interval, with a non-finite sample and one the source's rule rejects left
    // out, which is the promotion's own arithmetic. So over colour and grey sources holding samples below zero, above
    // one, not a number and infinite, under the all-valid, finite-only and range rules, the analysis is the one a
    // presentation that promotes measures, bit for bit, at one worker and at eight, and it is the exact statistics of
    // the held samples; a masked source and a quantized promotion still measure through the working image, and
    // agree the same way.
    constexpr std::uint32_t kWidth = 61u;
    constexpr std::uint32_t kHeight = 47u;
    constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;
    const an::EngineContext engine = makeEngine(8u);
    const auto plant = [](std::vector<float> samples, std::uint32_t channels) {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            float* const plane = samples.data() + static_cast<std::size_t>(channel) * kPixels;
            plane[3u + channel] = -0.3f;
            plane[17u + channel] = 1.7f;
            plane[29u + channel] = std::numeric_limits<float>::quiet_NaN();
            plane[41u + channel] = std::numeric_limits<float>::infinity();
            plane[53u + channel] = -std::numeric_limits<float>::infinity();
            plane[67u + channel] = -0.0f;
            plane[71u + channel] = 1.0f;
            plane[79u + channel] = 0.0f;
        }
        return samples;
    };
    const FloatSource colour =
        floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, plant(skyFrame(kWidth, kHeight, 3u, 71u), 3u));
    const FloatSource grey =
        floatSource(kWidth, kHeight, an::ChannelSchema::Mono, plant(skyFrame(kWidth, kHeight, 1u, 72u), 1u));
    an::ValidityDescriptorRequest finiteRequest;
    finiteRequest.rule = an::ValidityRule::FiniteOnly;
    an::ValidityDescriptorRequest rangeRequest;
    rangeRequest.rule = an::ValidityRule::DomainRange;
    rangeRequest.lowestValid = 0.01;
    rangeRequest.highestValid = 0.9;
    const std::array<an::ValidityDescriptor, 3u> rules{an::ValidityDescriptor{},
                                                       *an::ValidityDescriptor::create(finiteRequest),
                                                       *an::ValidityDescriptor::create(rangeRequest)};
    const auto sameBits = [](double left, double right) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    };
    const auto expectSameAnalysis = [&](const an::DisplayPresentationAnalysis& direct,
                                        const an::DisplayPresentationAnalysis& promoted) {
        EXPECT_TRUE(direct.measured);
        EXPECT_TRUE(promoted.measured);
        EXPECT_EQ(direct.colorChannels, promoted.colorChannels);
        EXPECT_EQ(direct.promotion, promoted.promotion);
        EXPECT_EQ(direct.measuredSamples, promoted.measuredSamples);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_TRUE(sameBits(direct.medians[channel], promoted.medians[channel])) << channel;
            EXPECT_TRUE(sameBits(direct.medianAbsoluteDeviations[channel], promoted.medianAbsoluteDeviations[channel]))
                << channel;
        }
    };

    for (const std::uint32_t workers : {8u, 1u})
    {
        SCOPED_TRACE(workers);
        const an::ExecutionContext execution = makeExecution(engine, workers);
        for (const FloatSource* source : {&colour, &grey})
        {
            const std::uint32_t channels = source->descriptor.extent().channelCount();
            SCOPED_TRACE(channels);
            for (const an::ValidityDescriptor& rule : rules)
            {
                SCOPED_TRACE(static_cast<int>(rule.rule()));
                const an::DisplayPresentationSource view = source->view(an::KernelValidityView{.descriptor = rule});
                an::DisplayPresentationRequest request;
                const an::Result<an::DisplayPresentationAnalysis> direct =
                    an::analyzeDisplayPresentation(execution, plan(request), view);
                ASSERT_TRUE(direct.hasValue()) << direct.status().message();
                const WorkingOutput promoted = presentWorking(execution, request, view);
                expectSameAnalysis(*direct, promoted.result.analysis);
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    std::vector<double> held;
                    for (std::size_t index = 0u; index < kPixels; ++index)
                    {
                        const float stored = source->samples[channel * kPixels + index];
                        const bool admitted = rule.rule() == an::ValidityRule::DomainRange
                                                  ? stored >= 0.01f && stored <= 0.9f
                                                  : std::isfinite(stored);
                        if (admitted)
                        {
                            held.push_back(std::clamp(static_cast<double>(stored), 0.0, 1.0));
                        }
                    }
                    const Location location = exactLocation(held);
                    EXPECT_EQ(direct->measuredSamples[channel], held.size());
                    EXPECT_NEAR(direct->medians[channel], location.median, 1e-12);
                    EXPECT_NEAR(direct->medianAbsoluteDeviations[channel], location.deviation, 1e-12);
                }
            }
        }
    }

    // A mask, and the quantized promotion, measure the working image as before; the answers still agree.
    const an::ExecutionContext execution = makeExecution(engine, 8u);
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(kWidth, kHeight, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(maskExtent.has_value());
    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(kHeight) * ((kWidth + 7u) / 8u), 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    ASSERT_TRUE(writableMask.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            ASSERT_TRUE(writableMask->setValid(0u, x, y, (x + y) % 5u != 0u));
        }
    }
    an::ValidityDescriptorRequest maskRequest;
    maskRequest.rule = an::ValidityRule::ExplicitMask;
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    const an::DisplayPresentationSource masked =
        colour.view(an::KernelValidityView{.descriptor = *an::ValidityDescriptor::create(maskRequest), .mask = &mask});
    an::DisplayPresentationRequest request;
    const an::Result<an::DisplayPresentationAnalysis> ofMasked =
        an::analyzeDisplayPresentation(execution, plan(request), masked);
    ASSERT_TRUE(ofMasked.hasValue()) << ofMasked.status().message();
    expectSameAnalysis(*ofMasked, presentWorking(execution, request, masked).result.analysis);
    request.promotion = an::PresentationPromotion::Quantized16;
    const an::Result<an::DisplayPresentationAnalysis> ofQuantized =
        an::analyzeDisplayPresentation(execution, plan(request), colour.view());
    ASSERT_TRUE(ofQuantized.hasValue()) << ofQuantized.status().message();
    expectSameAnalysis(*ofQuantized, presentWorking(execution, request, colour.view()).result.analysis);
}

namespace
{

/// The quantile the engine publishes over an already sorted oracle: the sample at the lower rank plus the fraction of
/// the way to the one above it.
[[nodiscard]] double publishedQuantile(const std::vector<double>& sorted, double quantile)
{
    const double position = quantile * static_cast<double>(sorted.size() - 1u);
    const double floored = std::floor(position);
    const auto lower = static_cast<std::size_t>(floored);
    const double fraction = position - floored;
    return fraction == 0.0 ? sorted[lower] : sorted[lower] + fraction * (sorted[lower + 1u] - sorted[lower]);
}

/// One code of a sky: most of them near a level a few hundredths up the representation's positive range, some
/// anywhere in it, and both of its ends - below zero for a signed one - present.
template <typename SampleT> [[nodiscard]] SampleT skyCode(Sequence& sequence)
{
    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
    const double draw = static_cast<double>(sequence.next());
    double value = 0.0;
    if (draw < 0.01)
        value = lowest;
    else if (draw < 0.02)
        value = highest;
    else if (draw < 0.1)
        value = lowest + (highest - lowest) * static_cast<double>(sequence.next());
    else
        value = highest * (0.04 + 0.01 * static_cast<double>(sequence.next() + sequence.next() - 1.0f));
    return static_cast<SampleT>(std::clamp(std::round(value), lowest, highest));
}

/// An eight- or sixteen-bit frame and the Float32 frame a presentation promotes it to: each sample its fraction of full
/// scale held to the unit interval, and a quiet NaN where the frame's rule or mask leaves it out.
template <typename SampleT> struct CodedFrame
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;
    FloatSource promoted;
};

template <typename SampleT>
[[nodiscard]] CodedFrame<SampleT> codedFrame(std::uint32_t width, std::uint32_t height, an::ChannelSchema schema,
                                             bool interleaved, const an::KernelValidityView& validity,
                                             std::uint64_t seed)
{
    const an::ImageDescriptor descriptor = describe(an::kSampleTypeOf<SampleT>, width, height, schema);
    const std::uint32_t channels = descriptor.extent().channelCount();
    const an::ImageLayout layout = interleaved ? interleavedLayout(an::kSampleTypeOf<SampleT>, width, height, channels)
                                               : planarLayout(an::kSampleTypeOf<SampleT>, width, height, channels, 3u);
    std::vector<SampleT> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(SampleT)), SampleT{0});
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::vector<float> promoted(pixels * channels, 0.0f);
    Sequence sequence{seed};
    const bool perChannel = validity.mask != nullptr && validity.mask->planeCount() == channels && channels > 1u;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::uint32_t plane = channel / layout.samplesPerPixel();
                const std::optional<std::uint64_t> offset =
                    layout.sampleByteOffset(plane, y, x, channel % layout.samplesPerPixel());
                EXPECT_TRUE(offset.has_value());
                const SampleT code = skyCode<SampleT>(sequence);
                samples[static_cast<std::size_t>(*offset / sizeof(SampleT))] = code;
                bool valid = an::sampleIsValidByValue(validity.descriptor, code);
                if (validity.mask != nullptr)
                    valid =
                        valid && validity.mask->test(perChannel ? channel : validity.maskPlane, x, y).value_or(false);
                const float value =
                    std::clamp(an::convertSample<SampleT, float>(code, an::ConversionPolicy{}).value, 0.0f, 1.0f);
                promoted[channel * pixels + static_cast<std::size_t>(y) * width + x] =
                    valid ? value : std::numeric_limits<float>::quiet_NaN();
            }
        }
    }
    return CodedFrame<SampleT>{descriptor, layout, std::move(samples),
                               floatSource(width, height, schema, std::move(promoted))};
}

/// Checks that an eight- or sixteen-bit frame is measured, and presented, as the Float32 frame it promotes to is.
template <typename SampleT> void expectCodesMeasuredAsTheirPromotion(std::uint64_t seed)
{
    constexpr std::uint32_t kWidth = 83u;
    constexpr std::uint32_t kHeight = 59u;
    const an::EngineContext engine = makeEngine(8u);
    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
    an::ValidityDescriptorRequest rangeRequest;
    rangeRequest.rule = an::ValidityRule::DomainRange;
    rangeRequest.lowestValid = lowest + 0.02 * (highest - lowest);
    rangeRequest.highestValid = highest - 0.05 * (highest - lowest);
    an::ValidityDescriptorRequest maskRequest;
    maskRequest.rule = an::ValidityRule::ExplicitMask;
    an::ValidityDescriptorRequest finiteRequest;
    finiteRequest.rule = an::ValidityRule::FiniteOnly;
    const an::ValidityDescriptor finiteOnly = *an::ValidityDescriptor::create(finiteRequest);
    // A mask of one plane shared by every channel, and one of a plane per channel, each leaving out its own pixels.
    const auto maskOf = [&](std::uint32_t planes, std::vector<std::uint8_t>& bytes) {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(kWidth, kHeight, planes, an::defaultEngineLimits());
        EXPECT_TRUE(extent.has_value());
        bytes.assign(static_cast<std::size_t>(planes) * kHeight * ((kWidth + 7u) / 8u), 0u);
        std::optional<an::ValidityMaskView<std::uint8_t>> mask =
            an::ValidityMaskView<std::uint8_t>::createTight(bytes, *extent);
        EXPECT_TRUE(mask.has_value());
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
            for (std::uint32_t y = 0u; y < kHeight; ++y)
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                    EXPECT_TRUE(mask->setValid(plane, x, y, (x * 3u + y + plane * 5u) % 7u != 0u));
        return mask->constView();
    };
    const auto expectSameAnalysis = [](const an::DisplayPresentationAnalysis& coded,
                                       const an::DisplayPresentationAnalysis& promoted) {
        EXPECT_TRUE(coded.measured);
        EXPECT_EQ(coded.colorChannels, promoted.colorChannels);
        EXPECT_EQ(coded.promotion, promoted.promotion);
        EXPECT_EQ(coded.measuredSamples, promoted.measuredSamples);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_EQ(std::bit_cast<std::uint64_t>(coded.medians[channel]),
                      std::bit_cast<std::uint64_t>(promoted.medians[channel]))
                << channel;
            EXPECT_EQ(std::bit_cast<std::uint64_t>(coded.medianAbsoluteDeviations[channel]),
                      std::bit_cast<std::uint64_t>(promoted.medianAbsoluteDeviations[channel]))
                << channel;
        }
    };

    for (const an::ChannelSchema schema : {an::ChannelSchema::Mono, an::ChannelSchema::Rgb, an::ChannelSchema::Rgba})
    {
        const std::uint32_t channels =
            schema == an::ChannelSchema::Mono ? 1u : (schema == an::ChannelSchema::Rgb ? 3u : 4u);
        std::vector<std::uint8_t> sharedBytes;
        std::vector<std::uint8_t> perChannelBytes;
        const an::ValidityMaskView<const std::uint8_t> shared = maskOf(1u, sharedBytes);
        const an::ValidityMaskView<const std::uint8_t> perChannel = maskOf(channels, perChannelBytes);
        const an::ValidityDescriptor masked = *an::ValidityDescriptor::create(maskRequest);
        const std::vector<std::pair<const char*, an::KernelValidityView>> readings{
            {"all valid", an::KernelValidityView{}},
            {"a range", an::KernelValidityView{.descriptor = *an::ValidityDescriptor::create(rangeRequest)}},
            {"a shared mask", an::KernelValidityView{.descriptor = masked, .mask = &shared}},
            {"a mask per channel", an::KernelValidityView{.descriptor = masked, .mask = &perChannel}}};
        for (const bool interleaved : {false, true})
        {
            if (interleaved && channels == 1u)
                continue;
            for (const auto& [name, validity] : readings)
            {
                SCOPED_TRACE(::testing::Message()
                             << an::sampleTypeName(an::kSampleTypeOf<SampleT>) << ", " << channels << " channels, "
                             << (interleaved ? "interleaved, " : "planar, ") << name);
                const CodedFrame<SampleT> frame =
                    codedFrame<SampleT>(kWidth, kHeight, schema, interleaved, validity, seed);
                const an::DisplayPresentationSource coded{.descriptor = &frame.descriptor,
                                                          .samples = frame.samples.data(),
                                                          .layout = &frame.layout,
                                                          .validity = validity};
                const an::DisplayPresentationSource promoted =
                    frame.promoted.view(an::KernelValidityView{.descriptor = finiteOnly});
                for (const std::uint32_t workers : {8u, 1u})
                {
                    SCOPED_TRACE(workers);
                    const an::ExecutionContext execution = makeExecution(engine, workers);
                    an::DisplayPresentationRequest request;
                    const an::Result<an::DisplayPresentationAnalysis> ofCodes =
                        an::analyzeDisplayPresentation(execution, plan(request), coded);
                    ASSERT_TRUE(ofCodes.hasValue()) << ofCodes.status().message();
                    const an::Result<an::DisplayPresentationAnalysis> ofPromoted =
                        an::analyzeDisplayPresentation(execution, plan(request), promoted);
                    ASSERT_TRUE(ofPromoted.hasValue()) << ofPromoted.status().message();
                    expectSameAnalysis(*ofCodes, *ofPromoted);

                    // Presented with no analysis given, the frame is measured the same way and shows the same working
                    // samples as its promotion does.
                    const WorkingOutput codedWorking = presentWorking(execution, request, coded);
                    const WorkingOutput promotedWorking = presentWorking(execution, request, promoted);
                    expectSameAnalysis(codedWorking.result.analysis, promotedWorking.result.analysis);
                    EXPECT_FALSE(codedWorking.result.analysisReused);
                    ASSERT_EQ(codedWorking.samples.size(), promotedWorking.samples.size());
                    std::size_t differing = 0u;
                    for (std::size_t index = 0u; index < codedWorking.samples.size(); ++index)
                    {
                        const float left = codedWorking.samples[index];
                        const float right = promotedWorking.samples[index];
                        const bool same = std::isnan(left) ? std::isnan(right)
                                                           : std::bit_cast<std::uint32_t>(left) ==
                                                                 std::bit_cast<std::uint32_t>(right);
                        differing += same ? 0u : 1u;
                    }
                    EXPECT_EQ(differing, 0u);

                    // Both are the exact statistics of the promoted samples the frame's rule and mask admit.
                    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
                    for (std::uint32_t channel = 0u; channel < (std::min)(channels, 3u); ++channel)
                    {
                        std::vector<double> values;
                        for (std::size_t index = 0u; index < pixels; ++index)
                        {
                            const float value = frame.promoted.samples[channel * pixels + index];
                            if (!std::isnan(value))
                                values.push_back(static_cast<double>(value));
                        }
                        std::sort(values.begin(), values.end());
                        ASSERT_FALSE(values.empty());
                        const double median = publishedQuantile(values, 0.5);
                        std::vector<double> deviations;
                        for (const double value : values)
                            deviations.push_back(std::abs(value - median));
                        std::sort(deviations.begin(), deviations.end());
                        EXPECT_EQ(ofCodes->measuredSamples[channel], values.size());
                        EXPECT_EQ(std::bit_cast<std::uint64_t>(ofCodes->medians[channel]),
                                  std::bit_cast<std::uint64_t>(median));
                        EXPECT_EQ(std::bit_cast<std::uint64_t>(ofCodes->medianAbsoluteDeviations[channel]),
                                  std::bit_cast<std::uint64_t>(publishedQuantile(deviations, 0.5)));
                    }
                }
            }
        }
    }
}

} // namespace

TEST(DisplayPresentationTest, AnEightOrSixteenBitFrameIsMeasuredFromItsCodesAsItsPromotionWouldBe)
{
    // The gradient promotion of an eight- or sixteen-bit sample is a function of its code alone, so a presentation
    // measures such a frame from how often each of its codes occurs, each code promoted once, rather than from a
    // promoted copy. The medians, the deviations and the counts are the promoted frame's bit for bit, and so is every
    // working sample a presentation that measures shows - in grey, colour and colour with alpha, planar and
    // interleaved, under every rule and a mask shared by the channels or of one plane each, at one worker and at
    // eight - and both are the exact statistics of the promoted samples.
    expectCodesMeasuredAsTheirPromotion<std::uint16_t>(0x9e3779b97f4a7c15ull);
    expectCodesMeasuredAsTheirPromotion<std::int16_t>(0x243f6a8885a308d3ull);
    expectCodesMeasuredAsTheirPromotion<std::uint8_t>(0x5851f42d4c957f2dull);
    expectCodesMeasuredAsTheirPromotion<std::int8_t>(0x14057b7ef767814full);
}

TEST(DisplayPresentationTest, AnAnalysisOfASixteenBitFrameTakesNoWorkingImage)
{
    // Measured from its codes, a sixteen-bit frame's analysis holds a table of how often each code occurs, not the
    // Float32 working image a promotion fills - here eighteen megabytes - and it is still the promoted frame's.
    constexpr std::uint32_t kWidth = 1536u;
    constexpr std::uint32_t kHeight = 1024u;
    const CodedFrame<std::uint16_t> frame =
        codedFrame<std::uint16_t>(kWidth, kHeight, an::ChannelSchema::Rgb, false, an::KernelValidityView{}, 0xa5u);
    const an::EngineContext engine = makeEngine(4u);
    const an::ExecutionContext execution = makeExecution(engine, 4u);
    const an::Result<an::DisplayPresentationAnalysis> analysis = an::analyzeDisplayPresentation(
        execution, plan({}),
        an::DisplayPresentationSource{
            .descriptor = &frame.descriptor, .samples = frame.samples.data(), .layout = &frame.layout});
    ASSERT_TRUE(analysis.hasValue()) << analysis.status().message();
    const std::uint64_t working = static_cast<std::uint64_t>(kWidth) * kHeight * 3u * sizeof(float);
    EXPECT_LT(engine.memoryResource()->counters().peakBytes, working / 8u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);

    an::ValidityDescriptorRequest finiteRequest;
    finiteRequest.rule = an::ValidityRule::FiniteOnly;
    const an::Result<an::DisplayPresentationAnalysis> promoted = an::analyzeDisplayPresentation(
        execution, plan({}),
        frame.promoted.view(an::KernelValidityView{.descriptor = *an::ValidityDescriptor::create(finiteRequest)}));
    ASSERT_TRUE(promoted.hasValue()) << promoted.status().message();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(std::bit_cast<std::uint64_t>(analysis->medians[channel]),
                  std::bit_cast<std::uint64_t>(promoted->medians[channel]));
        EXPECT_EQ(std::bit_cast<std::uint64_t>(analysis->medianAbsoluteDeviations[channel]),
                  std::bit_cast<std::uint64_t>(promoted->medianAbsoluteDeviations[channel]));
    }
}

TEST(DisplayPresentationTest, ADestinationIsCheckedBeforeAnythingIsWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<float> samples = skyFrame(6u, 4u, 3u, 3u);
    const FloatSource source = floatSource(6u, 4u, an::ChannelSchema::Rgb, samples);
    const an::DisplayPresentationPlan made = plan({});
    std::vector<std::uint8_t> bytes(6u * 4u * 4u, 0xCDu);

    const an::ImageLayout planarBytes = planarLayout(an::SampleType::UInt8, 6u, 4u, 3u);
    const an::Result<an::DisplayPresentationResult> planar =
        an::presentImage(execution, made, source.view(), bytes.data(), planarBytes);
    ASSERT_FALSE(planar.hasValue());
    EXPECT_EQ(planar.status().code(), an::ErrorCode::IncompatibleArguments);
    const an::ImageLayout narrow = interleavedLayout(an::SampleType::UInt8, 5u, 4u, 3u);
    const an::Result<an::DisplayPresentationResult> wrongExtent =
        an::presentImage(execution, made, source.view(), bytes.data(), narrow);
    ASSERT_FALSE(wrongExtent.hasValue());
    EXPECT_EQ(wrongExtent.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t byte) { return byte == 0xCDu; }));

    const an::ImageLayout interleaved = interleavedLayout(an::SampleType::UInt8, 6u, 4u, 3u);
    const an::Result<an::DisplayPresentationResult> null =
        an::presentImage(execution, made, source.view(), nullptr, interleaved);
    ASSERT_FALSE(null.hasValue());
    EXPECT_EQ(null.status().code(), an::ErrorCode::InvalidArgument);
    const an::Result<an::DisplayPresentationResult> overlapping =
        an::presentImage(execution, made, source.view(), const_cast<float*>(source.samples.data()), interleaved);
    ASSERT_FALSE(overlapping.hasValue());
    EXPECT_EQ(overlapping.status().code(), an::ErrorCode::IncompatibleArguments);

    an::DisplayPresentationSource missing = source.view();
    missing.layout = nullptr;
    const an::Result<an::DisplayPresentationResult> noLayout =
        an::presentImage(execution, made, missing, bytes.data(), interleaved);
    ASSERT_FALSE(noLayout.hasValue());
    EXPECT_EQ(noLayout.status().code(), an::ErrorCode::InvalidArgument);
    const an::ImageLayout wrongLayout = planarLayout(an::SampleType::Float32, 6u, 4u, 1u);
    an::DisplayPresentationSource mismatched = source.view();
    mismatched.layout = &wrongLayout;
    const an::Result<an::DisplayPresentationResult> notDescribed =
        an::presentImage(execution, made, mismatched, bytes.data(), interleaved);
    ASSERT_FALSE(notDescribed.hasValue());
    EXPECT_EQ(notDescribed.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DisplayPresentationTest, TheAnswerDoesNotDependOnHowManyThreadsRan)
{
    constexpr std::uint32_t kWidth = 301u;
    constexpr std::uint32_t kHeight = 207u;
    const an::EngineContext engine = makeEngine(8u);
    const FloatSource source = floatSource(kWidth, kHeight, an::ChannelSchema::Rgb, skyFrame(kWidth, kHeight, 3u, 99u));
    an::DisplayPresentationRequest request;
    request.neutralize = true;
    request.saturation = 0.4;
    request.brightness = 0.05;
    request.contrast = 1.2;
    request.output = an::PresentationOutput::Rgba8;
    const an::DisplayPresentationPlan made = plan(request);
    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::ExecutionContext several = makeExecution(engine, 8u);
    const ByteOutput one = presentBytes(single, made, source.view());
    const ByteOutput eight = presentBytes(several, made, source.view());
    EXPECT_EQ(one.bytes, eight.bytes);
    EXPECT_EQ(one.result.analysis.medians, eight.result.analysis.medians);
}

TEST(DisplayPresentationTest, AStoppedExecutionAndARefusedBudgetAreReturnedFailures)
{
    const FloatSource source = floatSource(64u, 64u, an::ChannelSchema::Rgb, skyFrame(64u, 64u, 3u, 4u));
    const an::DisplayPresentationPlan made = plan({});
    std::vector<std::uint8_t> bytes(64u * 64u * 3u);
    const an::ImageLayout layout = interleavedLayout(an::SampleType::UInt8, 64u, 64u, 3u);

    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    const an::Result<an::ExecutionContext> stopped = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(stopped.hasValue());
    const an::Result<an::DisplayPresentationResult> cancelled =
        an::presentImage(*stopped, made, source.view(), bytes.data(), layout);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);

    an::EngineContextOptions small;
    small.maxConcurrency = 2u;
    small.limits.memoryBudgetBytes = 4096u;
    an::Result<an::EngineContext> tight = an::EngineContext::create(small);
    ASSERT_TRUE(tight.hasValue()) << tight.status().message();
    const an::ExecutionContext execution = makeExecution(*tight, 2u);
    const an::Result<an::DisplayPresentationResult> refused =
        an::presentImage(execution, made, source.view(), bytes.data(), layout);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST(DisplayPresentationTest, AnImageWithNothingValidIsShownBlackWithoutAFailure)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const FloatSource source =
        floatSource(4u, 3u, an::ChannelSchema::Rgb, std::vector<float>(36u, std::numeric_limits<float>::quiet_NaN()));
    an::DisplayPresentationRequest request;
    request.saturation = 0.5;
    request.brightness = 0.1;
    const ByteOutput output = presentBytes(execution, plan(request), source.view());
    EXPECT_TRUE(std::all_of(output.bytes.begin(), output.bytes.end(), [](std::uint8_t byte) { return byte == 0u; }));
    EXPECT_EQ(output.result.invalidPixels, 12u);
    EXPECT_EQ(output.result.analysis.measuredSamples[0], 0u);
    EXPECT_EQ(output.result.transfer[0].midtones, 0.5);
}

TEST(DisplayPresentationTest, TheEffectsDeclareDisplayData)
{
    static_assert(an::checkOperationEffect(an::kDisplayPresentationEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kRealtimePreviewDisplayPresentationEffect) ==
                  an::OperationEffectError::None);
    EXPECT_EQ(an::kDisplayPresentationEffect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(an::kDisplayPresentationEffect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(an::kDisplayPresentationEffect.solution, an::SolutionEffect::Preserves);
    EXPECT_EQ(an::kRealtimePreviewDisplayPresentationEffect.solution, an::SolutionEffect::Invalidates);
    EXPECT_EQ(an::kDisplayPresentationEffect.validity.policy, an::ValidityPolicy::Regenerate);
    EXPECT_EQ(an::kDisplayPresentationEffect.validity.produced.rule(), an::ValidityRule::FiniteOnly);
}

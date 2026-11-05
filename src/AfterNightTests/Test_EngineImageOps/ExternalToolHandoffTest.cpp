// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the external-tool hand-off: what an image becomes on its way to a tool that reads an image
// file, and what the tool's answer becomes on the way back.
//
// Oracles are written from the header's definitions: an integer sample a fraction of full scale, a floating image
// mapped from the unit interval widened to its smallest and largest valid samples, the automatic transfer derived
// from the mapped image's statistics by the published derivation, the handed sample written once by the engine's
// conversion; on the way back, the inverse of the transfer - checked against the published bisection inverse rather
// than restated - and the range's offset and span for an image or its span alone for a star layer.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Pointwise/ExternalToolHandoff.h>
#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <type_traits>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

[[nodiscard]] an::ImageDescriptor describe(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t channels,
                                           an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = type,
                                   .channelSchema = channels == 1u   ? an::ChannelSchema::Mono
                                                    : channels == 4u ? an::ChannelSchema::Rgba
                                                                     : an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = domain,
                                   .cfa = {},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// Samples of one representation in a layout, with a descriptor, owning their storage.
template <typename SampleT> struct Owned
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;

    [[nodiscard]] an::ExternalToolSource source(an::KernelValidityView validity = {}) const
    {
        return an::ExternalToolSource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = validity};
    }

    [[nodiscard]] SampleT& at(std::uint32_t c, std::uint32_t x, std::uint32_t y)
    {
        const std::optional<std::uint64_t> offset =
            layout.sampleByteOffset(c / layout.samplesPerPixel(), y, x, c % layout.samplesPerPixel());
        return samples[static_cast<std::size_t>(*offset / sizeof(SampleT))];
    }

    [[nodiscard]] SampleT at(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        const std::optional<std::uint64_t> offset =
            layout.sampleByteOffset(c / layout.samplesPerPixel(), y, x, c % layout.samplesPerPixel());
        return samples[static_cast<std::size_t>(*offset / sizeof(SampleT))];
    }
};

/// An empty image of a representation, planar or interleaved.
template <typename SampleT>
[[nodiscard]] Owned<SampleT> emptyImage(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                        bool interleaved = false,
                                        an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const an::ImageLayout layout =
        interleaved ? interleavedLayout(type, width, height, channels) : planarLayout(type, width, height, channels);
    return Owned<SampleT>{describe(type, width, height, channels, domain), layout,
                          std::vector<SampleT>(layout.totalBytes() / sizeof(SampleT), SampleT{})};
}

/// An image of random samples over `[low, high]`; an integer image over its representation's whole range.
template <typename SampleT>
[[nodiscard]] Owned<SampleT>
randomImage(std::uint32_t width, std::uint32_t height, std::uint32_t channels, std::uint64_t seed, double low = 0.0,
            double high = 1.0, bool interleaved = false, an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    Owned<SampleT> image = emptyImage<SampleT>(width, height, channels, interleaved, domain);
    Sequence sequence{seed};
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double unit = static_cast<double>(sequence.next());
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    image.at(c, x, y) = static_cast<SampleT>(low + unit * (high - low));
                }
                else
                {
                    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::min)());
                    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
                    image.at(c, x, y) = static_cast<SampleT>(std::floor(lowest + unit * (highest - lowest)));
                }
            }
        }
    }
    return image;
}

/// A sample as a fraction of full scale, by the engine's own conversion.
template <typename SampleT> [[nodiscard]] double fraction(SampleT value)
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<double>(value);
    }
    else
    {
        return an::convertSample<SampleT, double>(value, an::ConversionPolicy{}).value;
    }
}

/// A value written once into a representation by the engine's conversion: nearest code, ties to even, saturating.
template <typename SampleT> [[nodiscard]] SampleT narrowed(double value)
{
    return an::convertSample<double, SampleT>(value, an::ConversionPolicy{}).value;
}

[[nodiscard]] an::ExternalToolHandoffPlan plan(const an::ExternalToolHandoffRequest& request = {})
{
    const an::Result<an::ExternalToolHandoffPlan> made = an::planExternalToolHandoff(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// The handed image of a plan's format.
template <typename HandedT>
[[nodiscard]] Owned<HandedT> handedFor(const an::ExternalToolHandoffPlan& made, const an::ImageDescriptor& source)
{
    const an::Result<an::ImageLayoutRequest> request = an::externalToolHandoffLayout(made, source);
    EXPECT_TRUE(request.hasValue()) << request.status().message();
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    const an::ImageDescriptor descriptor = describe(an::kSampleTypeOf<HandedT>, source.extent().width(),
                                                    source.extent().height(), source.extent().channelCount());
    return Owned<HandedT>{descriptor, *layout, std::vector<HandedT>(layout->totalBytes() / sizeof(HandedT), HandedT{})};
}

template <typename SourceT, typename HandedT>
[[nodiscard]] an::ExternalToolHandoff prepare(const an::ExecutionContext& execution,
                                              const an::ExternalToolHandoffPlan& made, const Owned<SourceT>& source,
                                              Owned<HandedT>& handed, an::KernelValidityView validity = {})
{
    const an::Result<an::ExternalToolHandoff> result =
        an::prepareExternalToolHandoff(execution, made, source.source(validity), handed.samples.data(), handed.layout);
    EXPECT_TRUE(result.hasValue()) << result.status().message();
    return result.hasValue() ? *result : an::ExternalToolHandoff{};
}

template <typename AnswerT, typename ResultT>
void restore(const an::ExecutionContext& execution, const an::ExternalToolHandoffPlan& made,
             const an::ExternalToolHandoff& handoff, an::ExternalToolLayer layer, const Owned<AnswerT>& answer,
             Owned<ResultT>& result)
{
    const an::Result<an::ExternalToolRestoreResult> restored = an::restoreExternalToolAnswer(
        execution, made, handoff, layer, answer.source(), result.samples.data(), result.layout);
    ASSERT_TRUE(restored.hasValue()) << restored.status().message();
    EXPECT_EQ(restored->run.outputsWritten,
              static_cast<std::uint64_t>(handoff.width) * handoff.height * handoff.channels);
}

/// Each channel's automatic transfer for a mapped image, through the published statistics and derivation.
[[nodiscard]] std::array<an::MidtonesTransferParameters, 3u> derivedTransfers(const an::ExecutionContext& execution,
                                                                              const Owned<float>& mapped, bool linked)
{
    const std::uint32_t channels = mapped.descriptor.extent().channelCount();
    std::vector<an::StretchChannelStatistics> statistics;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const an::Result<an::RobustStatistics> measured = an::computeImageRobustStatistics(
            execution, an::SampleType::Float32, mapped.samples.data(), mapped.layout, channel,
            an::StatisticsSelection{}, an::RobustStatisticsRequest{.medianAbsoluteDeviation = true});
        EXPECT_TRUE(measured.hasValue()) << measured.status().message();
        statistics.push_back(
            {.median = measured->median, .medianAbsoluteDeviation = measured->medianAbsoluteDeviation});
    }
    std::array<an::MidtonesTransferParameters, 3u> transfer{};
    if (linked)
    {
        const an::Result<an::MidtonesTransferParameters> one = an::deriveLinkedAutoStretch(statistics, -2.8, 0.25);
        EXPECT_TRUE(one.hasValue());
        transfer.fill(*one);
        return transfer;
    }
    EXPECT_TRUE(an::deriveUnlinkedAutoStretch(statistics, -2.8, 0.25,
                                              std::span<an::MidtonesTransferParameters>(transfer.data(), channels))
                    .ok());
    for (std::uint32_t channel = channels; channel < 3u; ++channel)
    {
        transfer[channel] = transfer[channels - 1u];
    }
    return transfer;
}

/// The inverse of a transfer, by the published bisection.
[[nodiscard]] double inverseOf(double value, const an::MidtonesTransferParameters& transfer)
{
    const an::Result<double> inverse = an::evaluateInverseMidtonesTransfer(value, transfer);
    EXPECT_TRUE(inverse.hasValue());
    return *inverse;
}

[[nodiscard]] bool closeFloat(double actual, double expected)
{
    return std::abs(actual - expected) <= 2.0e-7 * (std::max)(1.0, std::abs(expected));
}

} // namespace

TEST(ExternalToolHandoffTest, ThePlanAndTheLayoutStateWhatTheToolIsHanded)
{
    EXPECT_EQ(an::planExternalToolHandoff({.format = static_cast<an::ExternalToolFormat>(2u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planExternalToolHandoff({.preStretch = static_cast<an::ExternalToolPreStretch>(3u)}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planExternalToolHandoff({.shadowsClipping = std::numeric_limits<double>::infinity()}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::planExternalToolHandoff({.targetBackground = 1.5}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(an::isExternalToolLayer(an::ExternalToolLayer::StarLayer));
    EXPECT_FALSE(an::isExternalToolLayer(static_cast<an::ExternalToolLayer>(2u)));

    const an::ExternalToolHandoffPlan sixteen = plan();
    EXPECT_EQ(sixteen.request().format, an::ExternalToolFormat::UInt16);
    EXPECT_EQ(sixteen.request().preStretch, an::ExternalToolPreStretch::None);
    const an::ImageDescriptor source = describe(an::SampleType::Float32, 9u, 4u, 3u);
    const an::Result<an::ImageLayoutRequest> codes = an::externalToolHandoffLayout(sixteen, source);
    ASSERT_TRUE(codes.hasValue());
    EXPECT_EQ(codes->elementSizeBytes, 2u);
    EXPECT_EQ(codes->planeCount, 3u);
    EXPECT_EQ(codes->samplesPerPixel, 1u);
    const an::Result<an::ImageLayoutRequest> floats =
        an::externalToolHandoffLayout(plan({.format = an::ExternalToolFormat::Float32}), source);
    ASSERT_TRUE(floats.hasValue());
    EXPECT_EQ(floats->elementSizeBytes, 4u);
    EXPECT_EQ(an::externalToolHandoffLayout(sixteen, describe(an::SampleType::Float32, 9u, 4u, 4u)).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

// An integer image reaches the tool as its fraction of full scale, whatever its numeric domain says, and comes back
// as the codes it went as.
TEST(ExternalToolHandoffTest, AnIntegerImageIsHandedAtFullScaleAndComesBackAsItWent)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ExternalToolHandoffPlan made = plan();
    const Owned<std::uint16_t> codes =
        randomImage<std::uint16_t>(13u, 5u, 3u, 7u, 0.0, 1.0, false, an::NumericDomain::RawSensorCode);
    Owned<std::uint16_t> handed = handedFor<std::uint16_t>(made, codes.descriptor);
    const an::ExternalToolHandoff handoff = prepare(execution, made, codes, handed);
    EXPECT_EQ(handoff.rangeLow, 0.0);
    EXPECT_EQ(handoff.rangeHigh, 1.0);
    EXPECT_FALSE(handoff.stretched);
    EXPECT_EQ(handoff.sourceSampleType, an::SampleType::UInt16);
    EXPECT_EQ(handoff.run.selection.valid, 65u);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 5u; ++y)
        {
            for (std::uint32_t x = 0u; x < 13u; ++x)
            {
                ASSERT_EQ(handed.at(c, x, y), codes.at(c, x, y)) << c << "," << x << "," << y;
            }
        }
    }
    Owned<std::uint16_t> back = emptyImage<std::uint16_t>(13u, 5u, 3u);
    restore(execution, made, handoff, an::ExternalToolLayer::Image, handed, back);
    EXPECT_EQ(back.samples, codes.samples);

    // Eight-bit codes are 257 sixteen-bit codes each, exactly.
    const Owned<std::uint8_t> bytes = randomImage<std::uint8_t>(11u, 3u, 1u, 9u);
    Owned<std::uint16_t> wider = handedFor<std::uint16_t>(made, bytes.descriptor);
    (void)prepare(execution, made, bytes, wider);
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 11u; ++x)
        {
            ASSERT_EQ(wider.at(0u, x, y), static_cast<std::uint16_t>(bytes.at(0u, x, y) * 257u));
        }
    }
}

// A floating image inside the unit interval is handed as its nearest sixteen-bit codes, ties to even, and comes back
// within half a code.
TEST(ExternalToolHandoffTest, AFloatImageInsideTheUnitIntervalIsHandedAsItsNearestCodes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ExternalToolHandoffPlan made = plan();
    Owned<float> source = randomImage<float>(17u, 6u, 3u, 11u);
    source.at(0u, 0u, 0u) = 0.0f;
    source.at(1u, 1u, 0u) = 1.0f;
    // A value exactly between two codes rounds to the even one.
    source.at(2u, 2u, 0u) = static_cast<float>(2.5 / 65535.0);
    Owned<std::uint16_t> handed = handedFor<std::uint16_t>(made, source.descriptor);
    const an::ExternalToolHandoff handoff = prepare(execution, made, source, handed);
    EXPECT_EQ(handoff.rangeLow, 0.0);
    EXPECT_EQ(handoff.rangeHigh, 1.0);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 17u; ++x)
            {
                ASSERT_EQ(handed.at(c, x, y), narrowed<std::uint16_t>(static_cast<double>(source.at(c, x, y))))
                    << c << "," << x << "," << y;
            }
        }
    }
    EXPECT_EQ(handed.at(0u, 0u, 0u), 0u);
    EXPECT_EQ(handed.at(1u, 1u, 0u), 65535u);
    EXPECT_EQ(handed.at(2u, 2u, 0u), narrowed<std::uint16_t>(static_cast<double>(source.at(2u, 2u, 0u))));

    Owned<float> back = emptyImage<float>(17u, 6u, 3u, true);
    restore(execution, made, handoff, an::ExternalToolLayer::Image, handed, back);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 6u; ++y)
        {
            for (std::uint32_t x = 0u; x < 17u; ++x)
            {
                ASSERT_EQ(back.at(c, x, y), static_cast<float>(static_cast<double>(handed.at(c, x, y)) / 65535.0));
                ASSERT_LE(std::abs(static_cast<double>(back.at(c, x, y)) - static_cast<double>(source.at(c, x, y))),
                          0.5 / 65535.0 + 1.0e-7);
            }
        }
    }
}

// A floating image beyond the unit interval is mapped from the interval widened to its extremes, so nothing is
// clipped; an image comes back by the range's offset and span, a star layer by its span alone.
TEST(ExternalToolHandoffTest, AFloatImageBeyondTheUnitIntervalIsMappedIntoItAndBack)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    for (const an::ExternalToolFormat format : {an::ExternalToolFormat::UInt16, an::ExternalToolFormat::Float32})
    {
        const an::ExternalToolHandoffPlan made = plan({.format = format});
        Owned<float> source =
            randomImage<float>(15u, 7u, 3u, 13u, -0.25, 1.25, false, an::NumericDomain::PhysicalScaled);
        source.at(1u, 3u, 2u) = -0.3f;
        source.at(2u, 9u, 6u) = 1.4f;
        const double low = static_cast<double>(-0.3f);
        const double high = static_cast<double>(1.4f);
        const double span = high - low;
        Owned<std::uint16_t> codes = handedFor<std::uint16_t>(made, source.descriptor);
        Owned<float> floats = handedFor<float>(made, source.descriptor);
        const an::ExternalToolHandoff handoff = format == an::ExternalToolFormat::UInt16
                                                    ? prepare(execution, made, source, codes)
                                                    : prepare(execution, made, source, floats);
        EXPECT_EQ(handoff.rangeLow, low);
        EXPECT_EQ(handoff.rangeHigh, high);
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < 7u; ++y)
            {
                for (std::uint32_t x = 0u; x < 15u; ++x)
                {
                    // The working image holds the mapped value as a Float32 sample.
                    const float mapped = static_cast<float>((static_cast<double>(source.at(c, x, y)) - low) / span);
                    if (format == an::ExternalToolFormat::UInt16)
                    {
                        ASSERT_EQ(codes.at(c, x, y), (an::convertSample<float, std::uint16_t>(mapped, {}).value));
                    }
                    else
                    {
                        ASSERT_EQ(floats.at(c, x, y), mapped);
                    }
                }
            }
        }
        if (format == an::ExternalToolFormat::Float32)
        {
            // The Float32 hand-off loses nothing: the image comes back to within the mapping's own rounding.
            Owned<float> back = emptyImage<float>(15u, 7u, 3u);
            restore(execution, made, handoff, an::ExternalToolLayer::Image, floats, back);
            Owned<float> stars = emptyImage<float>(15u, 7u, 3u);
            restore(execution, made, handoff, an::ExternalToolLayer::StarLayer, floats, stars);
            for (std::uint32_t c = 0u; c < 3u; ++c)
            {
                for (std::uint32_t y = 0u; y < 7u; ++y)
                {
                    for (std::uint32_t x = 0u; x < 15u; ++x)
                    {
                        const double answer = static_cast<double>(floats.at(c, x, y));
                        ASSERT_EQ(back.at(c, x, y), static_cast<float>(low + answer * span));
                        ASSERT_EQ(stars.at(c, x, y), static_cast<float>(answer * span));
                        ASSERT_LE(
                            std::abs(static_cast<double>(back.at(c, x, y)) - static_cast<double>(source.at(c, x, y))),
                            1.0e-6);
                    }
                }
            }
        }
    }
}

// The automatic stretch is derived from the mapped image's statistics, per channel or linked, applied before the
// image is written, and undone on the way back by its inverse.
TEST(ExternalToolHandoffTest, TheAutomaticStretchIsDerivedFromTheMappedImageAndUndoneOnTheWayBack)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // A dark, linear-looking frame: most samples near the background, a few bright.
    Owned<float> source = randomImage<float>(31u, 19u, 3u, 17u, 0.02, 0.12);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        source.at(c, 5u, 5u) = 0.9f;
        source.at(c, 20u, 11u) = 0.75f - 0.1f * static_cast<float>(c);
    }
    for (const an::ExternalToolPreStretch stretch :
         {an::ExternalToolPreStretch::Unlinked, an::ExternalToolPreStretch::Linked})
    {
        const an::ExternalToolHandoffPlan made = plan({.preStretch = stretch});
        Owned<std::uint16_t> handed = handedFor<std::uint16_t>(made, source.descriptor);
        const an::ExternalToolHandoff handoff = prepare(execution, made, source, handed);
        ASSERT_TRUE(handoff.stretched);
        const std::array<an::MidtonesTransferParameters, 3u> expected =
            derivedTransfers(execution, source, stretch == an::ExternalToolPreStretch::Linked);
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            EXPECT_EQ(handoff.transfer[c], expected[c]) << c;
        }
        if (stretch == an::ExternalToolPreStretch::Unlinked)
        {
            EXPECT_NE(handoff.transfer[0], handoff.transfer[2]);
        }
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < 19u; ++y)
            {
                for (std::uint32_t x = 0u; x < 31u; ++x)
                {
                    const an::Result<double> forward =
                        an::evaluateMidtonesTransfer(static_cast<double>(source.at(c, x, y)), expected[c]);
                    ASSERT_TRUE(forward.hasValue());
                    const int code = narrowed<std::uint16_t>(static_cast<double>(static_cast<float>(*forward)));
                    ASSERT_LE(std::abs(static_cast<int>(handed.at(c, x, y)) - code), 1) << c << "," << x << "," << y;
                }
            }
        }
        // The answer is the handed image itself, as a tool that changes nothing would return it.
        Owned<float> back = emptyImage<float>(31u, 19u, 3u);
        restore(execution, made, handoff, an::ExternalToolLayer::Image, handed, back);
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < 19u; ++y)
            {
                for (std::uint32_t x = 0u; x < 31u; ++x)
                {
                    const double answer = static_cast<double>(handed.at(c, x, y)) / 65535.0;
                    ASSERT_TRUE(closeFloat(static_cast<double>(back.at(c, x, y)), inverseOf(answer, expected[c])))
                        << c << "," << x << "," << y;
                    // Above the shadow point the round trip is the source to within the stretched code's width.
                    if (static_cast<double>(source.at(c, x, y)) > expected[c].shadows + 0.01)
                    {
                        ASSERT_LE(
                            std::abs(static_cast<double>(back.at(c, x, y)) - static_cast<double>(source.at(c, x, y))),
                            2.0e-4);
                    }
                }
            }
        }
    }
}

// The inverse of a stretched star layer is mapped back by the span alone, after the transfer's inverse.
TEST(ExternalToolHandoffTest, AStarLayerComesBackThroughTheInverseAndTheSpanAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> source = randomImage<float>(12u, 9u, 1u, 19u, -0.1, 0.4, false, an::NumericDomain::PhysicalScaled);
    source.at(0u, 3u, 3u) = 1.6f;
    const an::ExternalToolHandoffPlan made = plan({.preStretch = an::ExternalToolPreStretch::Unlinked});
    Owned<std::uint16_t> handed = handedFor<std::uint16_t>(made, source.descriptor);
    const an::ExternalToolHandoff handoff = prepare(execution, made, source, handed);
    const double span = handoff.rangeHigh - handoff.rangeLow;
    EXPECT_LT(handoff.rangeLow, 0.0);
    EXPECT_GT(handoff.rangeHigh, 1.0);
    // A tool's star layer: anything the tool returns, here a ramp of codes.
    Owned<std::uint16_t> layer = handedFor<std::uint16_t>(made, source.descriptor);
    for (std::uint32_t y = 0u; y < 9u; ++y)
    {
        for (std::uint32_t x = 0u; x < 12u; ++x)
        {
            layer.at(0u, x, y) = static_cast<std::uint16_t>((x + 12u * y) * 600u);
        }
    }
    Owned<float> stars = emptyImage<float>(12u, 9u, 1u);
    restore(execution, made, handoff, an::ExternalToolLayer::StarLayer, layer, stars);
    for (std::uint32_t y = 0u; y < 9u; ++y)
    {
        for (std::uint32_t x = 0u; x < 12u; ++x)
        {
            const double answer = static_cast<double>(layer.at(0u, x, y)) / 65535.0;
            ASSERT_TRUE(
                closeFloat(static_cast<double>(stars.at(0u, x, y)), span * inverseOf(answer, handoff.transfer[0])))
                << x << "," << y;
        }
    }
}

// A pixel the validity rejects is left out of the range and the statistics and handed as zero in every channel; a
// sample that is not finite in a pixel the validity admits is refused.
TEST(ExternalToolHandoffTest, AnInvalidPixelIsHandedAsZeroAndANonFiniteValidSampleIsRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> source = randomImage<float>(10u, 8u, 3u, 23u, 0.05, 0.6);
    source.at(0u, 1u, 1u) = std::numeric_limits<float>::quiet_NaN();
    source.at(1u, 4u, 2u) = std::numeric_limits<float>::infinity();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    for (const an::ExternalToolPreStretch stretch :
         {an::ExternalToolPreStretch::None, an::ExternalToolPreStretch::Unlinked})
    {
        const an::ExternalToolHandoffPlan made = plan({.preStretch = stretch});
        Owned<std::uint16_t> handed = handedFor<std::uint16_t>(made, source.descriptor);
        std::fill(handed.samples.begin(), handed.samples.end(), std::uint16_t{0xBEEFu});
        const an::ExternalToolHandoff handoff =
            prepare(execution, made, source, handed, an::KernelValidityView{.descriptor = *finite});
        EXPECT_EQ(handoff.run.selection.total, 80u);
        EXPECT_EQ(handoff.run.selection.valid, 78u);
        EXPECT_EQ(handoff.rangeHigh, 1.0) << "an invalid infinity widens nothing";
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            EXPECT_EQ(handed.at(c, 1u, 1u), 0u) << c;
            EXPECT_EQ(handed.at(c, 4u, 2u), 0u) << c;
        }
        for (const std::uint16_t code : handed.samples)
        {
            ASSERT_NE(code, 0xBEEFu) << "every handed sample is written";
        }
    }
    Owned<std::uint16_t> handed = handedFor<std::uint16_t>(plan(), source.descriptor);
    EXPECT_EQ(an::prepareExternalToolHandoff(execution, plan(), source.source(), handed.samples.data(), handed.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

// A Float64 source is measured and written back as itself, and the hand-off writes any layout of its format.
TEST(ExternalToolHandoffTest, AFloat64SourceComesBackAsFloat64AndAnInterleavedHandOffIsWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<double> source = randomImage<double>(9u, 4u, 3u, 29u, -0.5, 2.0, false, an::NumericDomain::PhysicalScaled);
    source.at(0u, 0u, 0u) = -0.5000000001;
    const an::ExternalToolHandoffPlan made = plan({.format = an::ExternalToolFormat::Float32});
    Owned<float> handed = emptyImage<float>(9u, 4u, 3u, true);
    const an::ExternalToolHandoff handoff = prepare(execution, made, source, handed);
    EXPECT_EQ(handoff.rangeLow, -0.5000000001) << "a Float64 source's range is its own values";
    EXPECT_EQ(handoff.sourceSampleType, an::SampleType::Float64);
    const double span = handoff.rangeHigh - handoff.rangeLow;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 4u; ++y)
        {
            for (std::uint32_t x = 0u; x < 9u; ++x)
            {
                ASSERT_EQ(handed.at(c, x, y), static_cast<float>((source.at(c, x, y) - handoff.rangeLow) / span));
            }
        }
    }
    Owned<double> back = emptyImage<double>(9u, 4u, 3u);
    restore(execution, made, handoff, an::ExternalToolLayer::Image, handed, back);
    EXPECT_NEAR(back.at(1u, 2u, 3u), source.at(1u, 2u, 3u), 1.0e-6 * span);
}

// The restore refuses an answer that is not the source's shape and a record that is not one a hand-off makes.
TEST(ExternalToolHandoffTest, TheRestoreRefusesAMismatchedAnswerOrRecord)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ExternalToolHandoffPlan made = plan();
    const Owned<float> source = randomImage<float>(6u, 5u, 3u, 31u);
    Owned<std::uint16_t> handed = handedFor<std::uint16_t>(made, source.descriptor);
    const an::ExternalToolHandoff handoff = prepare(execution, made, source, handed);
    Owned<float> back = emptyImage<float>(6u, 5u, 3u);
    const Owned<std::uint16_t> narrow = emptyImage<std::uint16_t>(5u, 5u, 3u);
    EXPECT_EQ(an::restoreExternalToolAnswer(execution, made, handoff, an::ExternalToolLayer::Image, narrow.source(),
                                            back.samples.data(), back.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    an::ExternalToolHandoff wrong = handoff;
    wrong.rangeHigh = 0.5;
    EXPECT_EQ(an::restoreExternalToolAnswer(execution, made, wrong, an::ExternalToolLayer::Image, handed.source(),
                                            back.samples.data(), back.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::restoreExternalToolAnswer(execution, made, handoff, static_cast<an::ExternalToolLayer>(2u),
                                            handed.source(), back.samples.data(), back.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    // The destination is the source's representation.
    Owned<std::uint16_t> wrongType = emptyImage<std::uint16_t>(6u, 5u, 3u);
    EXPECT_EQ(an::restoreExternalToolAnswer(execution, made, handoff, an::ExternalToolLayer::Image, handed.source(),
                                            wrongType.samples.data(), wrongType.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
}

// The hand-off and the restore give the same bytes whatever the number of threads.
TEST(ExternalToolHandoffTest, TheHandOffAndTheRestoreDoNotDependOnTheNumberOfThreads)
{
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext one = makeExecution(engine, 1u);
    const an::ExecutionContext eight = makeExecution(engine, 8u);
    const Owned<float> source =
        randomImage<float>(257u, 131u, 3u, 37u, -0.2, 1.3, false, an::NumericDomain::PhysicalScaled);
    const an::ExternalToolHandoffPlan made = plan({.preStretch = an::ExternalToolPreStretch::Unlinked});
    Owned<std::uint16_t> serial = handedFor<std::uint16_t>(made, source.descriptor);
    Owned<std::uint16_t> parallel = handedFor<std::uint16_t>(made, source.descriptor);
    const an::ExternalToolHandoff serialRecord = prepare(one, made, source, serial);
    const an::ExternalToolHandoff parallelRecord = prepare(eight, made, source, parallel);
    EXPECT_EQ(serial.samples, parallel.samples);
    EXPECT_EQ(serialRecord.transfer, parallelRecord.transfer);
    EXPECT_EQ(serialRecord.rangeLow, parallelRecord.rangeLow);
    std::uint64_t distinct = 0u;
    for (std::size_t index = 1u; index < serial.samples.size(); ++index)
    {
        distinct += serial.samples[index] != serial.samples[index - 1u] ? 1u : 0u;
    }
    EXPECT_GT(distinct, serial.samples.size() / 2u) << "the comparison is of real work";
    Owned<float> serialBack = emptyImage<float>(257u, 131u, 3u);
    Owned<float> parallelBack = emptyImage<float>(257u, 131u, 3u);
    restore(one, made, serialRecord, an::ExternalToolLayer::Image, serial, serialBack);
    restore(eight, made, parallelRecord, an::ExternalToolLayer::Image, parallel, parallelBack);
    EXPECT_EQ(
        std::memcmp(serialBack.samples.data(), parallelBack.samples.data(), serialBack.samples.size() * sizeof(float)),
        0);
    EXPECT_NE(serialBack.at(1u, 100u, 100u), 0.0f);
}

// A stopped execution writes nothing and says why.
TEST(ExternalToolHandoffTest, AStoppedExecutionIsRefused)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    an::Result<an::ExecutionContext> stopped = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(stopped.hasValue());
    const Owned<float> source = randomImage<float>(6u, 5u, 1u, 41u);
    Owned<std::uint16_t> handed = handedFor<std::uint16_t>(plan(), source.descriptor);
    EXPECT_EQ(an::prepareExternalToolHandoff(*stopped, plan(), source.source(), handed.samples.data(), handed.layout)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
}

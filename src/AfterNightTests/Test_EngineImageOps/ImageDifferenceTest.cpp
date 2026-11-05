// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the image difference: two images compared sample by sample and written as an image.
//
// Oracles are written from the definitions: a sample is read as its domain says - an integer in a normalized
// or encoded domain through the engine's own full-scale conversion, a sensor code as the code, a float as it
// is - and the measure is formed in double and narrowed once. Validity is held per sample, and a sample the
// difference does not write keeps the sentinel the destination was filled with.

#include "SpatialFilterFixture.h"

#include <AfterNightEngine/Algorithms/Pointwise/ImageDifference.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stop_token>
#include <vector>

namespace
{

using spatial_test::interleavedLayout;
using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

constexpr float kSentinel = -7.0f;

[[nodiscard]] std::uint32_t channelsOf(an::ChannelSchema schema)
{
    switch (schema)
    {
    case an::ChannelSchema::Mono:
        return 1u;
    case an::ChannelSchema::Rgba:
        return 4u;
    default:
        return 3u;
    }
}

[[nodiscard]] an::ImageDescriptor describe(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           an::ChannelSchema schema,
                                           an::NumericDomain domain = an::NumericDomain::NormalizedLinear,
                                           an::ColorSpace space = an::ColorSpace::Unspecified)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channelsOf(schema), an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = type,
                                                               .channelSchema = schema,
                                                               .colorSpace = space,
                                                               .numericDomain = domain,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

[[nodiscard]] an::ImageDifferencePlan plan(const an::ImageDifferenceRequest& request = {})
{
    an::Result<an::ImageDifferencePlan> made = an::planImageDifference(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return *made;
}

/// Samples of one representation in a layout, with a descriptor, owning their storage.
template <typename SampleT> struct Owned
{
    an::ImageDescriptor descriptor;
    an::ImageLayout layout;
    std::vector<SampleT> samples;

    [[nodiscard]] an::ImageDifferenceSource source(an::KernelValidityView validity = {}) const
    {
        return an::ImageDifferenceSource{
            .descriptor = &descriptor, .samples = samples.data(), .layout = &layout, .validity = validity};
    }

    /// The sample of channel `c` at (x, y), through the layout.
    [[nodiscard]] SampleT at(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        const std::optional<std::uint64_t> offset =
            layout.sampleByteOffset(c / layout.samplesPerPixel(), y, x, c % layout.samplesPerPixel());
        return samples[static_cast<std::size_t>(*offset / sizeof(SampleT))];
    }
};

/// A planar image of random samples in [0, 1) scaled to the representation's full range.
template <typename SampleT>
[[nodiscard]] Owned<SampleT>
planarImage(std::uint32_t width, std::uint32_t height, an::ChannelSchema schema, std::uint64_t seed,
            an::NumericDomain domain = an::NumericDomain::NormalizedLinear, std::uint32_t padding = 0u)
{
    const an::SampleType type = an::kSampleTypeOf<SampleT>;
    const std::uint32_t channels = channelsOf(schema);
    const an::ImageLayout layout = planarLayout(type, width, height, channels, padding);
    Owned<SampleT> image{describe(type, width, height, schema, domain), layout,
                         std::vector<SampleT>(layout.totalBytes() / sizeof(SampleT), SampleT{})};
    Sequence sequence{seed};
    for (std::uint32_t c = 0u; c < channels; ++c)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double unit = sequence.next();
                SampleT value{};
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    value = static_cast<SampleT>(unit);
                }
                else
                {
                    const double low = static_cast<double>((std::numeric_limits<SampleT>::min)());
                    const double high = static_cast<double>((std::numeric_limits<SampleT>::max)());
                    value = static_cast<SampleT>(std::floor(low + unit * (high - low)));
                }
                const std::uint64_t offset = *layout.sampleByteOffset(c, y, x, 0u);
                image.samples[static_cast<std::size_t>(offset / sizeof(SampleT))] = value;
            }
        }
    }
    return image;
}

/// The same samples, interleaved.
template <typename SampleT> [[nodiscard]] Owned<SampleT> interleaved(const Owned<SampleT>& planar)
{
    const an::ImageExtent& extent = planar.descriptor.extent();
    const an::ImageLayout layout =
        interleavedLayout(an::kSampleTypeOf<SampleT>, extent.width(), extent.height(), extent.channelCount());
    Owned<SampleT> image{planar.descriptor, layout, std::vector<SampleT>(layout.totalBytes() / sizeof(SampleT))};
    for (std::uint32_t c = 0u; c < extent.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                const std::uint64_t offset = *layout.sampleByteOffset(0u, y, x, c);
                image.samples[static_cast<std::size_t>(offset / sizeof(SampleT))] = planar.at(c, x, y);
            }
        }
    }
    return image;
}

/// What a sample means in its domain, by the engine's own conversion.
template <typename SampleT> [[nodiscard]] double read(SampleT value, an::NumericDomain domain)
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<double>(value);
    }
    else
    {
        an::ConversionPolicy policy{};
        policy.domain =
            domain == an::NumericDomain::RawSensorCode ? an::DomainMapping::CodeValue : an::DomainMapping::FullScale;
        return an::convertSample<SampleT, double>(value, policy).value;
    }
}

/// A destination of `channels` Float32 planes filled with the sentinel.
struct Destination
{
    an::ImageLayout layout;
    std::vector<float> samples;

    [[nodiscard]] float at(std::uint32_t c, std::uint32_t x, std::uint32_t y) const
    {
        return samples[static_cast<std::size_t>(*layout.sampleByteOffset(c, y, x, 0u) / sizeof(float))];
    }
};

[[nodiscard]] Destination destinationFor(const an::ImageDifferencePlan& made, const an::ImageDescriptor& left)
{
    const an::Result<an::ImageLayoutRequest> request = an::imageDifferenceLayout(made, left);
    EXPECT_TRUE(request.hasValue()) << request.status().message();
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return Destination{*layout, std::vector<float>(layout->totalBytes() / sizeof(float), kSentinel)};
}

TEST(ImageDifferenceTest, ThePlannerRefusesWhatItCannotCompute)
{
    an::ImageDifferenceRequest unknown;
    unknown.measure = static_cast<an::ImageDifferenceMeasure>(9u);
    EXPECT_EQ(an::planImageDifference(unknown).status().code(), an::ErrorCode::InvalidArgument);

    for (const double bad : {-0.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        an::ImageDifferenceRequest weighted;
        weighted.luminanceWeights = {0.3, bad, 0.1};
        EXPECT_EQ(an::planImageDifference(weighted).status().code(), an::ErrorCode::InvalidArgument) << bad;
    }
    an::ImageDifferenceRequest zero;
    zero.luminanceWeights = {0.0, 0.0, 0.0};
    EXPECT_EQ(an::planImageDifference(zero).status().code(), an::ErrorCode::InvalidArgument);

    for (const an::SampleType output : {an::SampleType::UInt8, an::SampleType::UInt16, an::SampleType::Int32})
    {
        an::ImageDifferenceRequest integer;
        integer.output = output;
        EXPECT_EQ(an::planImageDifference(integer).status().code(), an::ErrorCode::InvalidArgument);
    }
    an::ImageDifferenceRequest wide;
    wide.output = an::SampleType::Float64;
    wide.measure = an::ImageDifferenceMeasure::Luminance;
    EXPECT_TRUE(an::planImageDifference(wide).hasValue());
}

TEST(ImageDifferenceTest, AnAbsoluteDifferenceIsEveryChannelsMagnitudeInDouble)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<float> left =
        planarImage<float>(13u, 9u, an::ChannelSchema::Rgb, 3u, an::NumericDomain::NormalizedLinear, 2u);
    const Owned<float> right = planarImage<float>(13u, 9u, an::ChannelSchema::Rgb, 4u);
    const an::ImageDifferencePlan made = plan();
    Destination destination = destinationFor(made, left.descriptor);
    const an::Result<an::ImageDifferenceResult> result = an::computeImageDifference(
        execution, made, left.source(), right.source(), destination.samples.data(), destination.layout);
    ASSERT_TRUE(result.hasValue()) << result.status().message();

    double largest = 0.0;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 9u; ++y)
        {
            for (std::uint32_t x = 0u; x < 13u; ++x)
            {
                const double expected =
                    std::fabs(static_cast<double>(left.at(c, x, y)) - static_cast<double>(right.at(c, x, y)));
                EXPECT_EQ(destination.at(c, x, y), static_cast<float>(expected)) << c << " " << x << " " << y;
                largest = (std::max)(largest, expected);
            }
        }
    }
    EXPECT_EQ(result->run.selection.total, 13u * 9u * 3u);
    EXPECT_EQ(result->run.selection.valid, 13u * 9u * 3u);
    EXPECT_EQ(result->run.outputsWritten, 13u * 9u * 3u);
    EXPECT_EQ(result->largestDifference, largest);
}

TEST(ImageDifferenceTest, TwoRepresentationsOfOneDomainAreReadAsTheNumbersTheyMean)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // A sixteen-bit frame against its own Float32 conversion differs by nothing.
    const Owned<std::uint16_t> codes = planarImage<std::uint16_t>(17u, 6u, an::ChannelSchema::Mono, 8u);
    Owned<float> converted = planarImage<float>(17u, 6u, an::ChannelSchema::Mono, 1u);
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 17u; ++x)
        {
            converted.samples[static_cast<std::size_t>(y) * 17u + x] =
                static_cast<float>(read(codes.at(0u, x, y), an::NumericDomain::NormalizedLinear));
        }
    }
    const an::ImageDifferencePlan made = plan(an::ImageDifferenceRequest{.output = an::SampleType::Float64});
    const an::Result<an::ImageLayoutRequest> request = an::imageDifferenceLayout(made, codes.descriptor);
    ASSERT_TRUE(request.hasValue());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    std::vector<double> out(layout->totalBytes() / sizeof(double), -1.0);
    const an::Result<an::ImageDifferenceResult> result =
        an::computeImageDifference(execution, made, codes.source(), converted.source(), out.data(), *layout);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 17u; ++x)
        {
            const double expected = std::fabs(read(codes.at(0u, x, y), an::NumericDomain::NormalizedLinear) -
                                              static_cast<double>(converted.at(0u, x, y)));
            const double written =
                out[static_cast<std::size_t>(*layout->sampleByteOffset(0u, y, x, 0u) / sizeof(double))];
            EXPECT_EQ(written, expected);
            EXPECT_LT(written, 1.0e-7);
        }
    }

    // Signed integers read through the engine's two's-complement full scale, sensor codes as codes.
    const Owned<std::int16_t> signedLeft = planarImage<std::int16_t>(9u, 4u, an::ChannelSchema::Mono, 21u);
    const Owned<std::int16_t> signedRight = planarImage<std::int16_t>(9u, 4u, an::ChannelSchema::Mono, 22u);
    const Owned<std::uint16_t> rawLeft =
        planarImage<std::uint16_t>(9u, 4u, an::ChannelSchema::Mono, 23u, an::NumericDomain::RawSensorCode);
    const Owned<std::uint16_t> rawRight =
        planarImage<std::uint16_t>(9u, 4u, an::ChannelSchema::Mono, 24u, an::NumericDomain::RawSensorCode);
    const an::ImageDifferencePlan floatPlan = plan();
    Destination signedOut = destinationFor(floatPlan, signedLeft.descriptor);
    Destination rawOut = destinationFor(floatPlan, rawLeft.descriptor);
    ASSERT_TRUE(an::computeImageDifference(execution, floatPlan, signedLeft.source(), signedRight.source(),
                                           signedOut.samples.data(), signedOut.layout)
                    .hasValue());
    ASSERT_TRUE(an::computeImageDifference(execution, floatPlan, rawLeft.source(), rawRight.source(),
                                           rawOut.samples.data(), rawOut.layout)
                    .hasValue());
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 9u; ++x)
        {
            const double signedExpected =
                std::fabs(read(signedLeft.at(0u, x, y), an::NumericDomain::NormalizedLinear) -
                          read(signedRight.at(0u, x, y), an::NumericDomain::NormalizedLinear));
            EXPECT_EQ(signedOut.at(0u, x, y), static_cast<float>(signedExpected));
            const double rawExpected =
                std::fabs(static_cast<double>(rawLeft.at(0u, x, y)) - static_cast<double>(rawRight.at(0u, x, y)));
            EXPECT_EQ(rawOut.at(0u, x, y), static_cast<float>(rawExpected));
        }
    }
}

TEST(ImageDifferenceTest, TheSingleChannelMeasuresAreTheChannelMeanAndTheLuminance)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const Owned<double> left = planarImage<double>(10u, 5u, an::ChannelSchema::Rgba, 31u);
    const Owned<double> right = interleaved(planarImage<double>(10u, 5u, an::ChannelSchema::Rgba, 32u));

    const an::ImageDifferencePlan mean =
        plan(an::ImageDifferenceRequest{.measure = an::ImageDifferenceMeasure::MeanAbsolute});
    Destination meanOut = destinationFor(mean, left.descriptor);
    ASSERT_EQ(meanOut.layout.extent().channelCount(), 1u);
    const an::Result<an::ImageDifferenceResult> meanResult = an::computeImageDifference(
        execution, mean, left.source(), right.source(), meanOut.samples.data(), meanOut.layout);
    ASSERT_TRUE(meanResult.hasValue()) << meanResult.status().message();
    EXPECT_EQ(meanResult->run.selection.total, 50u);

    const an::ColorChannelValues weights{0.25, 0.5, 0.125};
    const an::ImageDifferencePlan luminance =
        plan(an::ImageDifferenceRequest{.measure = an::ImageDifferenceMeasure::Luminance, .luminanceWeights = weights});
    Destination lumaOut = destinationFor(luminance, left.descriptor);
    ASSERT_TRUE(an::computeImageDifference(execution, luminance, left.source(), right.source(), lumaOut.samples.data(),
                                           lumaOut.layout)
                    .hasValue());

    for (std::uint32_t y = 0u; y < 5u; ++y)
    {
        for (std::uint32_t x = 0u; x < 10u; ++x)
        {
            double sum = 0.0;
            for (std::uint32_t c = 0u; c < 4u; ++c)
            {
                sum += std::fabs(left.at(c, x, y) - right.at(c, x, y));
            }
            EXPECT_EQ(meanOut.at(0u, x, y), static_cast<float>(sum / 4.0)) << "alpha takes part in the mean";
            double leftLuma = 0.0;
            double rightLuma = 0.0;
            for (std::uint32_t c = 0u; c < 3u; ++c)
            {
                leftLuma += weights[c] * left.at(c, x, y);
                rightLuma += weights[c] * right.at(c, x, y);
            }
            EXPECT_EQ(lumaOut.at(0u, x, y), static_cast<float>(std::fabs(leftLuma - rightLuma)))
                << "alpha takes no part in a luminance";
        }
    }

    // A grey image's luminance is its one channel, whatever the weights say.
    const Owned<float> greyLeft = planarImage<float>(6u, 3u, an::ChannelSchema::Mono, 41u);
    const Owned<float> greyRight = planarImage<float>(6u, 3u, an::ChannelSchema::Mono, 42u);
    Destination greyOut = destinationFor(luminance, greyLeft.descriptor);
    ASSERT_TRUE(an::computeImageDifference(execution, luminance, greyLeft.source(), greyRight.source(),
                                           greyOut.samples.data(), greyOut.layout)
                    .hasValue());
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 6u; ++x)
        {
            EXPECT_EQ(greyOut.at(0u, x, y), static_cast<float>(std::fabs(static_cast<double>(greyLeft.at(0u, x, y)) -
                                                                         static_cast<double>(greyRight.at(0u, x, y)))));
        }
    }
}

TEST(ImageDifferenceTest, ValidityIsJudgedPerSampleAndAnUnwrittenSampleKeepsWhatTheDestinationHeld)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> left = planarImage<float>(11u, 7u, an::ChannelSchema::Rgb, 51u);
    const Owned<float> right = planarImage<float>(11u, 7u, an::ChannelSchema::Rgb, 52u);
    // The left image rejects its non-finite samples, and one is planted in the green of pixel (3, 2).
    const std::size_t green = static_cast<std::size_t>(*left.layout.sampleByteOffset(1u, 2u, 3u, 0u) / sizeof(float));
    left.samples[green] = std::numeric_limits<float>::quiet_NaN();
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());
    // The right image carries one mask plane per channel, rejecting the blue of pixel (5, 4).
    const std::optional<an::ImageExtent> maskExtent = an::ImageExtent::create(11u, 7u, 3u, an::defaultEngineLimits());
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(2u) * 7u * 3u, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, *maskExtent);
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    ASSERT_TRUE(writable->setValid(2u, 5u, 4u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const std::optional<an::ValidityDescriptor> masked =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(masked.has_value());
    const an::ImageDifferenceSource leftSource = left.source(an::KernelValidityView{.descriptor = *finite});
    const an::ImageDifferenceSource rightSource =
        right.source(an::KernelValidityView{.descriptor = *masked, .mask = &mask});

    const an::ImageDifferencePlan absolute = plan();
    Destination perSample = destinationFor(absolute, left.descriptor);
    const an::Result<an::ImageDifferenceResult> result = an::computeImageDifference(
        execution, absolute, leftSource, rightSource, perSample.samples.data(), perSample.layout);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    EXPECT_EQ(perSample.at(1u, 3u, 2u), kSentinel);
    EXPECT_EQ(perSample.at(2u, 5u, 4u), kSentinel);
    EXPECT_NE(perSample.at(0u, 3u, 2u), kSentinel) << "one invalid channel leaves the pixel's others written";
    EXPECT_NE(perSample.at(2u, 3u, 2u), kSentinel);
    EXPECT_NE(perSample.at(1u, 5u, 4u), kSentinel);
    EXPECT_EQ(result->run.selection.total, 11u * 7u * 3u);
    EXPECT_EQ(result->run.selection.valid, 11u * 7u * 3u - 2u);
    EXPECT_EQ(result->run.outputsWritten, 11u * 7u * 3u - 2u);
    EXPECT_TRUE(std::isfinite(result->largestDifference));

    const an::ImageDifferencePlan mean =
        plan(an::ImageDifferenceRequest{.measure = an::ImageDifferenceMeasure::MeanAbsolute});
    Destination perPixel = destinationFor(mean, left.descriptor);
    const an::Result<an::ImageDifferenceResult> pixels =
        an::computeImageDifference(execution, mean, leftSource, rightSource, perPixel.samples.data(), perPixel.layout);
    ASSERT_TRUE(pixels.hasValue()) << pixels.status().message();
    EXPECT_EQ(perPixel.at(0u, 3u, 2u), kSentinel);
    EXPECT_EQ(perPixel.at(0u, 5u, 4u), kSentinel);
    EXPECT_NE(perPixel.at(0u, 4u, 2u), kSentinel);
    EXPECT_EQ(pixels->run.selection.total, 77u);
    EXPECT_EQ(pixels->run.selection.valid, 75u);
}

TEST(ImageDifferenceTest, ANonFiniteSampleARuleAdmitsGivesANonFiniteDifferenceAndSaysSo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> left = planarImage<float>(8u, 4u, an::ChannelSchema::Mono, 61u);
    const Owned<float> right = planarImage<float>(8u, 4u, an::ChannelSchema::Mono, 62u);
    left.samples[9] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageDifferencePlan made = plan();
    Destination out = destinationFor(made, left.descriptor);
    const an::Result<an::ImageDifferenceResult> result =
        an::computeImageDifference(execution, made, left.source(), right.source(), out.samples.data(), out.layout);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(std::isnan(out.at(0u, 1u, 1u)));
    EXPECT_TRUE(std::isnan(result->largestDifference));
    EXPECT_EQ(result->run.outputsWritten, 32u);
}

TEST(ImageDifferenceTest, TheAnswerDoesNotDependOnTheArrangementOrTheThreads)
{
    const Owned<float> left = planarImage<float>(301u, 67u, an::ChannelSchema::Rgb, 71u);
    const Owned<float> right = planarImage<float>(301u, 67u, an::ChannelSchema::Rgb, 72u);
    const Owned<float> leftInterleaved = interleaved(left);
    for (const an::ImageDifferenceMeasure measure :
         {an::ImageDifferenceMeasure::Absolute, an::ImageDifferenceMeasure::MeanAbsolute,
          an::ImageDifferenceMeasure::Luminance})
    {
        const an::ImageDifferencePlan made = plan(an::ImageDifferenceRequest{.measure = measure});
        std::optional<Destination> reference;
        std::optional<double> largest;
        for (const std::uint32_t threads : {1u, 3u, 8u})
        {
            const an::EngineContext engine = makeEngine(8u);
            const an::ExecutionContext execution = makeExecution(engine, threads);
            for (const Owned<float>* source : {&left, &leftInterleaved})
            {
                Destination out = destinationFor(made, left.descriptor);
                const an::Result<an::ImageDifferenceResult> result = an::computeImageDifference(
                    execution, made, source->source(), right.source(), out.samples.data(), out.layout);
                ASSERT_TRUE(result.hasValue()) << result.status().message();
                if (!reference.has_value())
                {
                    reference = out;
                    largest = result->largestDifference;
                    continue;
                }
                EXPECT_EQ(
                    std::memcmp(out.samples.data(), reference->samples.data(), out.samples.size() * sizeof(float)), 0)
                    << threads;
                EXPECT_EQ(result->largestDifference, *largest);
            }
        }
    }
}

TEST(ImageDifferenceTest, TheDescriptionIsStatedFromTheSourcesAndTheRequest)
{
    const an::ImageDescriptor rgb = describe(an::SampleType::UInt16, 12u, 5u, an::ChannelSchema::Rgb,
                                             an::NumericDomain::EncodedNonlinear, an::ColorSpace::DisplayRgb);
    const an::ImageDescriptor rgbFloat = describe(an::SampleType::Float32, 12u, 5u, an::ChannelSchema::Rgb,
                                                  an::NumericDomain::EncodedNonlinear, an::ColorSpace::DisplayRgb);
    const an::Result<an::ImageDescriptor> absolute = an::imageDifferenceDescriptor(plan(), rgb, rgbFloat);
    ASSERT_TRUE(absolute.hasValue()) << absolute.status().message();
    EXPECT_EQ(absolute->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(absolute->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(absolute->colorSpace(), an::ColorSpace::DisplayRgb);
    EXPECT_EQ(absolute->numericDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(absolute->extent().channelCount(), 3u);

    const an::Result<an::ImageDescriptor> mean = an::imageDifferenceDescriptor(
        plan(an::ImageDifferenceRequest{.measure = an::ImageDifferenceMeasure::MeanAbsolute,
                                        .output = an::SampleType::Float64}),
        rgb, rgbFloat);
    ASSERT_TRUE(mean.hasValue()) << mean.status().message();
    EXPECT_EQ(mean->sampleType(), an::SampleType::Float64);
    EXPECT_EQ(mean->channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(mean->colorSpace(), an::ColorSpace::Unspecified);
    EXPECT_EQ(mean->numericDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(mean->extent().channelCount(), 1u);
}

TEST(ImageDifferenceTest, ImagesThatDoNotMeanTheSameThingAreRefused)
{
    const an::ImageDifferencePlan made = plan();
    const an::ImageDescriptor base = describe(an::SampleType::Float32, 8u, 8u, an::ChannelSchema::Rgb);
    const an::ImageDescriptor wider = describe(an::SampleType::Float32, 9u, 8u, an::ChannelSchema::Rgb);
    const an::ImageDescriptor stretched =
        describe(an::SampleType::Float32, 8u, 8u, an::ChannelSchema::Rgb, an::NumericDomain::EncodedNonlinear);
    const an::ImageDescriptor xyz = describe(an::SampleType::Float32, 8u, 8u, an::ChannelSchema::Xyz,
                                             an::NumericDomain::NormalizedLinear, an::ColorSpace::CieXyz);
    const an::ImageDescriptor linear = describe(an::SampleType::Float32, 8u, 8u, an::ChannelSchema::Rgb,
                                                an::NumericDomain::NormalizedLinear, an::ColorSpace::LinearRgb);
    for (const an::ImageDescriptor* other : {&wider, &stretched, &xyz, &linear})
    {
        EXPECT_EQ(an::imageDifferenceDescriptor(made, base, *other).status().code(),
                  an::ErrorCode::IncompatibleArguments);
    }
    const an::ImageDifferencePlan luminance =
        plan(an::ImageDifferenceRequest{.measure = an::ImageDifferenceMeasure::Luminance});
    EXPECT_EQ(an::imageDifferenceDescriptor(luminance, xyz, xyz).status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::imageDifferenceLayout(luminance, xyz).status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(an::imageDifferenceDescriptor(made, xyz, xyz).hasValue());

    // The executor refuses the same pairs before it reads a sample.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> left = planarImage<float>(8u, 8u, an::ChannelSchema::Rgb, 81u);
    Owned<float> right = planarImage<float>(8u, 8u, an::ChannelSchema::Rgb, 82u, an::NumericDomain::EncodedNonlinear);
    Destination out = destinationFor(made, left.descriptor);
    EXPECT_EQ(an::computeImageDifference(execution, made, left.source(), right.source(), out.samples.data(), out.layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    for (const float sample : out.samples)
    {
        ASSERT_EQ(sample, kSentinel);
    }
}

TEST(ImageDifferenceTest, ABadDestinationOrSourceIsRefusedBeforeASampleIsWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    Owned<float> left = planarImage<float>(8u, 6u, an::ChannelSchema::Rgb, 91u);
    const Owned<float> right = planarImage<float>(8u, 6u, an::ChannelSchema::Rgb, 92u);
    const an::ImageDifferencePlan made = plan();
    Destination out = destinationFor(made, left.descriptor);

    // Interleaved is not the planar output the difference writes.
    const an::ImageLayout packed = interleavedLayout(an::SampleType::Float32, 8u, 6u, 3u);
    EXPECT_EQ(an::computeImageDifference(execution, made, left.source(), right.source(), out.samples.data(), packed)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    // The left image's own memory as the destination.
    EXPECT_EQ(
        an::computeImageDifference(execution, made, left.source(), right.source(), left.samples.data(), left.layout)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
    // A Float64 layout for a Float32 plan.
    const an::ImageLayout wide = planarLayout(an::SampleType::Float64, 8u, 6u, 3u);
    std::vector<double> doubles(wide.totalBytes() / sizeof(double));
    EXPECT_EQ(an::computeImageDifference(execution, made, left.source(), right.source(), doubles.data(), wide)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    // A null member, and a mask the rule does not read.
    an::ImageDifferenceSource broken = left.source();
    broken.samples = nullptr;
    EXPECT_EQ(an::computeImageDifference(execution, made, broken, right.source(), out.samples.data(), out.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(6u), 0xFFu);
    const std::optional<an::ImageExtent> maskExtent = an::ImageExtent::create(8u, 6u, 1u, an::defaultEngineLimits());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(bits, *maskExtent);
    ASSERT_TRUE(writable.has_value());
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    EXPECT_EQ(an::computeImageDifference(execution, made, left.source(an::KernelValidityView{.mask = &mask}),
                                         right.source(), out.samples.data(), out.layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    for (const float sample : out.samples)
    {
        ASSERT_EQ(sample, kSentinel);
    }

    // An execution that holds nothing, and one that was stopped.
    EXPECT_EQ(an::computeImageDifference(an::ExecutionContext{}, made, left.source(), right.source(),
                                         out.samples.data(), out.layout)
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    const an::Result<an::ExecutionContext> stopped = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(stopped.hasValue());
    EXPECT_EQ(an::computeImageDifference(*stopped, made, left.source(), right.source(), out.samples.data(), out.layout)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
}

TEST(ImageDifferenceTest, TheDeclaredEffectIsANewImageThatCombinesItsSourcesValidity)
{
    EXPECT_EQ(an::checkOperationEffect(an::kImageDifferenceEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kImageDifferenceEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kImageDifferenceEffect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(an::kImageDifferenceEffect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(an::kImageDifferenceEffect.solution, an::SolutionEffect::Preserves);
    EXPECT_EQ(an::kImageDifferenceEffect.codecMetadata, an::CodecMetadataEffect::Drops);
    EXPECT_EQ(an::kImageDifferenceEffect.outputIntent, an::OutputIntentEffect::Withdraws);
    EXPECT_EQ(an::kImageDifferenceEffect.validity.policy, an::ValidityPolicy::Combine);
}

} // namespace

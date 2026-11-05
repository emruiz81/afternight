// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for stated colour encodings, the plan between two of them, and its execution.

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout planarLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                           std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width + rowPaddingSamples) * sizeof(float);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::ImageLayout interleavedLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = channels,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width) * channels * sizeof(float);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::ColorEncoding srgbEncoded()
{
    an::ColorEncoding encoding;
    encoding.transfer = an::ColorTransferFunction::Srgb;
    return encoding;
}

[[nodiscard]] an::ColorEncoding schemaEncoding(an::ChannelSchema schema)
{
    an::ColorEncoding encoding;
    encoding.schema = schema;
    return encoding;
}

[[nodiscard]] std::array<double, 3u> convert(const an::ColorConversionPlan& plan, const std::array<double, 3u>& source)
{
    std::array<double, 3u> destination{};
    const an::Status status = an::convertColorPixel(plan, source, destination);
    EXPECT_TRUE(status.ok()) << status.message();
    return destination;
}

[[nodiscard]] std::array<double, 3u> convertPreview(const an::RealtimePreviewColorConversionPlan& plan,
                                                    const std::array<double, 3u>& source)
{
    std::array<double, 3u> destination{};
    const an::Status status = an::convertRealtimePreviewColorPixel(plan, source, destination);
    EXPECT_TRUE(status.ok()) << status.message();
    return destination;
}

void expectNear(const std::array<double, 3u>& produced, const std::array<double, 3u>& expected, double tolerance)
{
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        EXPECT_NEAR(produced[index], expected[index], tolerance) << "channel " << index;
    }
}

/// Runs a source encoding to a destination encoding and back, and reports the largest deviation.
[[nodiscard]] double roundTripError(const an::ColorEncoding& source, const an::ColorEncoding& destination,
                                    const std::array<double, 3u>& value)
{
    const an::Result<an::ColorConversionPlan> forward = an::planColorConversion(source, destination);
    const an::Result<an::ColorConversionPlan> backward = an::planColorConversion(destination, source);
    EXPECT_TRUE(forward.hasValue()) << forward.status().message();
    EXPECT_TRUE(backward.hasValue()) << backward.status().message();
    if (!forward.hasValue() || !backward.hasValue())
    {
        return 1.0;
    }
    const std::array<double, 3u> there = convert(*forward, value);
    const std::array<double, 3u> back = convert(*backward, there);
    double worst = 0.0;
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        worst = (std::max)(worst, std::abs(back[index] - value[index]));
    }
    return worst;
}

} // namespace

TEST(ColorTransformTest, AnEncodingStatesEveryDecisionAndRefusesTheOnesItDoesNotRead)
{
    EXPECT_EQ(an::checkColorEncoding(an::ColorEncoding{}), an::ColorEncodingError::None);

    an::ColorEncoding mono;
    mono.schema = an::ChannelSchema::Mono;
    EXPECT_EQ(an::checkColorEncoding(mono), an::ColorEncodingError::UnsupportedChannelSchema);

    an::ColorEncoding named;
    named.schema = an::ChannelSchema::Named;
    EXPECT_EQ(an::checkColorEncoding(named), an::ColorEncodingError::UnsupportedChannelSchema);

    an::ColorEncoding pureGamma;
    pureGamma.transfer = an::ColorTransferFunction::PureGamma;
    EXPECT_EQ(an::checkColorEncoding(pureGamma), an::ColorEncodingError::TransferGammaMissing);
    pureGamma.transferGamma = 0.0;
    EXPECT_EQ(an::checkColorEncoding(pureGamma), an::ColorEncodingError::TransferGammaNotPositive);
    pureGamma.transferGamma = 2.2;
    EXPECT_EQ(an::checkColorEncoding(pureGamma), an::ColorEncodingError::None);

    an::ColorEncoding strayGamma;
    strayGamma.transferGamma = 2.2;
    EXPECT_EQ(an::checkColorEncoding(strayGamma), an::ColorEncodingError::TransferGammaNotAllowed);

    an::ColorEncoding wideGamut;
    wideGamut.primaries = an::RgbPrimaries::ProPhoto;
    EXPECT_EQ(an::checkColorEncoding(wideGamut), an::ColorEncodingError::WhitePointDisagreesWithPrimaries);
    wideGamut.whitePoint = an::ChromaticWhitePoint::D50;
    EXPECT_EQ(an::checkColorEncoding(wideGamut), an::ColorEncodingError::None);

    an::ColorEncoding lab;
    lab.schema = an::ChannelSchema::Lab;
    lab.primaries = an::RgbPrimaries::Rec2020;
    EXPECT_EQ(an::checkColorEncoding(lab), an::ColorEncodingError::PrimariesNotAllowed);
    lab.primaries = an::RgbPrimaries::Srgb;
    lab.transfer = an::ColorTransferFunction::Srgb;
    EXPECT_EQ(an::checkColorEncoding(lab), an::ColorEncodingError::TransferNotAllowed);
    lab.transfer = an::ColorTransferFunction::Linear;
    EXPECT_EQ(an::checkColorEncoding(lab), an::ColorEncodingError::None);

    an::ColorEncoding strayPacking;
    strayPacking.perceptual = an::PerceptualEncoding::UnitPacked;
    EXPECT_EQ(an::checkColorEncoding(strayPacking), an::ColorEncodingError::PerceptualEncodingNotAllowed);

    an::ColorEncoding strayVariant;
    strayVariant.yCbCr = an::YCbCrVariant::Rec709FullRange;
    EXPECT_EQ(an::checkColorEncoding(strayVariant), an::ColorEncodingError::YCbCrVariantNotAllowed);

    an::ColorEncoding strayHue;
    strayHue.hue = an::HueConvention::Degrees;
    EXPECT_EQ(an::checkColorEncoding(strayHue), an::ColorEncodingError::HueConventionNotAllowed);

    // What a descriptor of each encoding says, which is the whole of the metadata claim.
    EXPECT_EQ(an::colorSpaceOfEncoding(an::ColorEncoding{}), an::ColorSpace::LinearRgb);
    EXPECT_EQ(an::numericDomainOfEncoding(an::ColorEncoding{}), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(an::colorSpaceOfEncoding(srgbEncoded()), an::ColorSpace::DisplayRgb);
    EXPECT_EQ(an::numericDomainOfEncoding(srgbEncoded()), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(an::colorSpaceOfEncoding(schemaEncoding(an::ChannelSchema::Xyz)), an::ColorSpace::CieXyz);
    EXPECT_EQ(an::numericDomainOfEncoding(schemaEncoding(an::ChannelSchema::Xyz)), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(an::colorSpaceOfEncoding(schemaEncoding(an::ChannelSchema::Lab)), an::ColorSpace::CieLab);
    EXPECT_EQ(an::numericDomainOfEncoding(schemaEncoding(an::ChannelSchema::Lab)), an::NumericDomain::EncodedNonlinear);
    EXPECT_FALSE(an::colorSpaceOfEncoding(mono).has_value());
    EXPECT_EQ(an::channelCountOfEncoding(an::ColorEncoding{}), 3u);

    an::ColorEncoding rgba;
    rgba.schema = an::ChannelSchema::Rgba;
    EXPECT_EQ(an::channelCountOfEncoding(rgba), 4u);
}

TEST(ColorTransformTest, PrimariesAndAdaptationAreDerivedSoTheForwardAndInverseAgree)
{
    // Every published set of primaries maps its own white to the white point's tristimulus values.
    for (const an::RgbPrimaries primaries :
         {an::RgbPrimaries::Srgb, an::RgbPrimaries::AdobeRgb1998, an::RgbPrimaries::DisplayP3,
          an::RgbPrimaries::Rec2020, an::RgbPrimaries::ProPhoto})
    {
        const an::Result<an::ColorMatrix3> forward = an::rgbToXyzMatrix(primaries);
        const an::Result<an::ColorMatrix3> inverse = an::xyzToRgbMatrix(primaries);
        ASSERT_TRUE(forward.hasValue()) << forward.status().message();
        ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();

        const std::array<double, 3u> white = an::whitePointTristimulus(an::whitePointOfPrimaries(primaries));
        const std::array<double, 3u> produced = an::applyColorMatrix(*forward, std::array<double, 3u>{1.0, 1.0, 1.0});
        expectNear(produced, white, 1e-12);

        // The round trip is the identity to the last bits the arithmetic allows, which a pair of
        // separately transcribed matrices is not.
        const an::ColorMatrix3 composed = an::multiplyColorMatrices(*inverse, *forward);
        for (std::size_t row = 0u; row < 3u; ++row)
        {
            for (std::size_t column = 0u; column < 3u; ++column)
            {
                EXPECT_NEAR(composed.coefficients[row * 3u + column], row == column ? 1.0 : 0.0, 1e-12);
            }
        }
    }

    // The sRGB matrix is the published one to six digits.
    const an::Result<an::ColorMatrix3> srgb = an::rgbToXyzMatrix(an::RgbPrimaries::Srgb);
    ASSERT_TRUE(srgb.hasValue());
    EXPECT_NEAR(srgb->coefficients[0], 0.4124564, 1e-6);
    EXPECT_NEAR(srgb->coefficients[4], 0.7151522, 1e-6);
    EXPECT_NEAR(srgb->coefficients[8], 0.9503041, 1e-6);

    // Adaptation between the same white is the identity; between two it is invertible.
    const an::Result<an::ColorMatrix3> same =
        an::chromaticAdaptationMatrix(an::ChromaticWhitePoint::D65, an::ChromaticWhitePoint::D65);
    ASSERT_TRUE(same.hasValue());
    EXPECT_EQ(*same, an::ColorMatrix3{});

    const an::Result<an::ColorMatrix3> there =
        an::chromaticAdaptationMatrix(an::ChromaticWhitePoint::D65, an::ChromaticWhitePoint::D50);
    ASSERT_TRUE(there.hasValue());
    const std::array<double, 3u> adapted =
        an::applyColorMatrix(*there, an::whitePointTristimulus(an::ChromaticWhitePoint::D65));
    expectNear(adapted, an::whitePointTristimulus(an::ChromaticWhitePoint::D50), 1e-12);

    // A singular matrix has no inverse, and neither does one carrying a value that is not a number.
    const an::ColorMatrix3 singular{{1.0, 2.0, 3.0, 2.0, 4.0, 6.0, 1.0, 0.0, 0.0}};
    EXPECT_EQ(an::invertColorMatrix(singular).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ColorTransformTest, KnownColoursReachTheirPublishedValues)
{
    // sRGB white is the D65 white point, and sRGB black is zero.
    const an::Result<an::ColorConversionPlan> toXyz =
        an::planColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Xyz));
    ASSERT_TRUE(toXyz.hasValue()) << toXyz.status().message();
    expectNear(convert(*toXyz, {1.0, 1.0, 1.0}), an::whitePointTristimulus(an::ChromaticWhitePoint::D65), 1e-9);
    expectNear(convert(*toXyz, {0.0, 0.0, 0.0}), {0.0, 0.0, 0.0}, 1e-12);

    // sRGB white in CIE units is L = 100 with no chroma at all.
    const an::Result<an::ColorConversionPlan> toLab =
        an::planColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lab));
    ASSERT_TRUE(toLab.hasValue()) << toLab.status().message();
    expectNear(convert(*toLab, {1.0, 1.0, 1.0}), {100.0, 0.0, 0.0}, 1e-9);

    // Mid grey is the published L* of 53.389 for the 0.5 sRGB code.
    const std::array<double, 3u> grey = convert(*toLab, {0.5, 0.5, 0.5});
    EXPECT_NEAR(grey[0], 53.3890, 1e-3);
    EXPECT_NEAR(grey[1], 0.0, 1e-9);
    EXPECT_NEAR(grey[2], 0.0, 1e-9);

    // Pure sRGB red is the published Lab triple.
    const std::array<double, 3u> red = convert(*toLab, {1.0, 0.0, 0.0});
    EXPECT_NEAR(red[0], 53.2408, 1e-3);
    EXPECT_NEAR(red[1], 80.0925, 1e-2);
    EXPECT_NEAR(red[2], 67.2032, 1e-2);

    // The same red in the cylindrical form, whose hue is measured in degrees.
    const an::Result<an::ColorConversionPlan> toLch =
        an::planColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lch));
    ASSERT_TRUE(toLch.hasValue()) << toLch.status().message();
    const std::array<double, 3u> redLch = convert(*toLch, {1.0, 0.0, 0.0});
    EXPECT_NEAR(redLch[0], 53.2408, 1e-3);
    EXPECT_NEAR(redLch[1], std::hypot(80.0925, 67.2032), 1e-2);
    EXPECT_NEAR(redLch[2], 39.999, 5e-2);

    // Hue, saturation and value of a saturated green, in the turn-fraction convention.
    an::ColorEncoding hsv = schemaEncoding(an::ChannelSchema::Hsv);
    hsv.transfer = an::ColorTransferFunction::Srgb;
    const an::Result<an::ColorConversionPlan> toHsv = an::planColorConversion(srgbEncoded(), hsv);
    ASSERT_TRUE(toHsv.hasValue()) << toHsv.status().message();
    expectNear(convert(*toHsv, {0.0, 1.0, 0.0}), {1.0 / 3.0, 1.0, 1.0}, 1e-12);
    expectNear(convert(*toHsv, {0.25, 0.25, 0.25}), {0.0, 0.0, 0.25}, 1e-12);

    // Luma and chroma of the same green, under the two published weightings.
    an::ColorEncoding rec601 = schemaEncoding(an::ChannelSchema::YCbCr);
    rec601.transfer = an::ColorTransferFunction::Srgb;
    const an::Result<an::ColorConversionPlan> toYCbCr = an::planColorConversion(srgbEncoded(), rec601);
    ASSERT_TRUE(toYCbCr.hasValue()) << toYCbCr.status().message();
    const std::array<double, 3u> greenLuma = convert(*toYCbCr, {0.0, 1.0, 0.0});
    EXPECT_NEAR(greenLuma[0], 0.587, 1e-12);
    EXPECT_NEAR(greenLuma[1], 0.5 - 0.587 / 1.772, 1e-12);
    EXPECT_NEAR(greenLuma[2], 0.5 - 0.587 / 1.402, 1e-12);

    an::ColorEncoding rec709 = rec601;
    rec709.yCbCr = an::YCbCrVariant::Rec709FullRange;
    const an::Result<an::ColorConversionPlan> to709 = an::planColorConversion(srgbEncoded(), rec709);
    ASSERT_TRUE(to709.hasValue());
    EXPECT_NEAR(convert(*to709, {0.0, 1.0, 0.0})[0], 0.7152, 1e-12);
}

TEST(ColorTransformTest, RealtimePreviewApproximatesColorPowersWithoutChangingProduction)
{
    const an::Result<an::RealtimePreviewColorConversionPlan> preview =
        an::planRealtimePreviewColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lab));
    ASSERT_TRUE(preview.hasValue()) << preview.status().message();
    EXPECT_EQ(preview->productionPlan().source(), srgbEncoded());
    EXPECT_GT(preview->maximumAbsolutePowerError(), 0.0);
    EXPECT_LT(preview->maximumAbsolutePowerError(), 0.11);

    constexpr double base = 0.5007;
    const double last = static_cast<double>(an::kRealtimePreviewColorTableEntries - 1u);
    const std::uint32_t lower = static_cast<std::uint32_t>(base * last);
    EXPECT_DOUBLE_EQ(preview->powerTwoPointFour(base), std::pow(static_cast<double>(lower) / last, 2.4));
    EXPECT_DOUBLE_EQ(preview->powerOneThird(base), std::pow(static_cast<double>(lower) / last, 1.0 / 3.0));
    EXPECT_DOUBLE_EQ(preview->powerOneOverTwoPointFour(base), std::pow(static_cast<double>(lower) / last, 1.0 / 2.4));
    EXPECT_DOUBLE_EQ(preview->powerTwoPointFour(1.000005), 1.0);
    EXPECT_DOUBLE_EQ(preview->powerTwoPointFour(1.1), std::pow(1.1, 2.4));

    const std::array<double, 3u> source{0.37, 0.52, 0.81};
    const std::array<double, 3u> exact = convert(preview->productionPlan(), source);
    const std::array<double, 3u> approximate = convertPreview(*preview, source);
    double worst = 0.0;
    for (std::size_t channel = 0u; channel < source.size(); ++channel)
    {
        worst = (std::max)(worst, std::abs(exact[channel] - approximate[channel]));
    }
    EXPECT_GT(worst, 0.0);
    EXPECT_LT(worst, 1.0);

    an::ColorEncoding invalid = srgbEncoded();
    invalid.schema = an::ChannelSchema::Mono;
    EXPECT_EQ(an::planRealtimePreviewColorConversion(invalid, schemaEncoding(an::ChannelSchema::Lab)).status().code(),
              an::ErrorCode::InvalidArgument);
}

TEST(ColorTransformTest, RealtimePreviewImageExecutionMatchesItsScalarMeaning)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 3u;
    const an::ImageLayout layout = planarLayout(kWidth, kHeight, 3u);
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight * 3u, 0.0f);
    std::vector<float> destination(source.size(), 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = static_cast<float>((index * 17u) % 101u) / 100.0f;
    }
    const an::Result<an::RealtimePreviewColorConversionPlan> plan =
        an::planRealtimePreviewColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lab));
    ASSERT_TRUE(plan.hasValue());
    const an::Status converted = an::convertRealtimePreviewImageColor(
        execution, *plan, an::SampleType::Float32, source.data(), layout, destination.data(), layout);
    ASSERT_TRUE(converted.ok()) << converted.message();

    const std::array<double, 3u> firstSource{source[0], source[kWidth * kHeight], source[2u * kWidth * kHeight]};
    const std::array<double, 3u> firstExpected = convertPreview(*plan, firstSource);
    EXPECT_FLOAT_EQ(destination[0], static_cast<float>(firstExpected[0]));
    EXPECT_FLOAT_EQ(destination[kWidth * kHeight], static_cast<float>(firstExpected[1]));
    EXPECT_FLOAT_EQ(destination[2u * kWidth * kHeight], static_cast<float>(firstExpected[2]));
}

TEST(ColorTransformTest, EveryRoundTripReturnsWhatItStartedWith)
{
    constexpr std::array<std::array<double, 3u>, 5u> samples{
        std::array<double, 3u>{0.2, 0.6, 0.9}, std::array<double, 3u>{0.03, 0.03, 0.03},
        std::array<double, 3u>{1.0, 0.0, 0.5}, std::array<double, 3u>{0.75, 0.75, 0.75},
        std::array<double, 3u>{0.001, 0.42, 0.999}};

    const an::ColorEncoding linear;
    for (const std::array<double, 3u>& sample : samples)
    {
        EXPECT_LT(roundTripError(linear, schemaEncoding(an::ChannelSchema::Xyz), sample), 1e-12);
        EXPECT_LT(roundTripError(linear, schemaEncoding(an::ChannelSchema::Lab), sample), 1e-12);
        EXPECT_LT(roundTripError(linear, schemaEncoding(an::ChannelSchema::Lch), sample), 1e-12);
        EXPECT_LT(roundTripError(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lab), sample), 1e-12);

        an::ColorEncoding hsl = schemaEncoding(an::ChannelSchema::Hsl);
        an::ColorEncoding hsv = schemaEncoding(an::ChannelSchema::Hsv);
        an::ColorEncoding hsi = schemaEncoding(an::ChannelSchema::Hsi);
        an::ColorEncoding yCbCr = schemaEncoding(an::ChannelSchema::YCbCr);
        hsl.hue = an::HueConvention::Degrees;
        EXPECT_LT(roundTripError(linear, hsl, sample), 1e-12);
        EXPECT_LT(roundTripError(linear, hsv, sample), 1e-12);
        EXPECT_LT(roundTripError(linear, hsi, sample), 1e-9);
        EXPECT_LT(roundTripError(linear, yCbCr, sample), 1e-12);

        // A change of primaries and of white point, which is where a transcribed inverse loses.
        an::ColorEncoding proPhoto;
        proPhoto.primaries = an::RgbPrimaries::ProPhoto;
        proPhoto.whitePoint = an::ChromaticWhitePoint::D50;
        EXPECT_LT(roundTripError(linear, proPhoto, sample), 1e-12);

        // The unit packing is a stated convention that survives its own round trip.
        an::ColorEncoding packedLab = schemaEncoding(an::ChannelSchema::Lab);
        an::ColorEncoding packedLch = schemaEncoding(an::ChannelSchema::Lch);
        packedLab.perceptual = an::PerceptualEncoding::UnitPacked;
        packedLch.perceptual = an::PerceptualEncoding::UnitPacked;
        EXPECT_LT(roundTripError(srgbEncoded(), packedLab, sample), 1e-12);
        EXPECT_LT(roundTripError(srgbEncoded(), packedLch, sample), 1e-12);
    }
}

TEST(ColorTransformTest, TheUnitPackingIsPreservedButOnlyWhenItIsAskedForByName)
{
    const an::Result<an::ColorConversionPlan> natural =
        an::planColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lab));
    an::ColorEncoding packed = schemaEncoding(an::ChannelSchema::Lab);
    packed.perceptual = an::PerceptualEncoding::UnitPacked;
    const an::Result<an::ColorConversionPlan> compatible = an::planColorConversion(srgbEncoded(), packed);
    ASSERT_TRUE(natural.hasValue());
    ASSERT_TRUE(compatible.hasValue());

    const std::array<double, 3u> value{0.8, 0.2, 0.4};
    const std::array<double, 3u> cie = convert(*natural, value);
    const std::array<double, 3u> unit = convert(*compatible, value);
    EXPECT_NEAR(unit[0], cie[0] / 100.0, 1e-12);
    EXPECT_NEAR(unit[1], (cie[1] + 128.0) / 255.0, 1e-12);
    EXPECT_NEAR(unit[2], (cie[2] + 128.0) / 255.0, 1e-12);

    // The cylindrical form packs its hue over the signed half turn.
    an::ColorEncoding packedLch = schemaEncoding(an::ChannelSchema::Lch);
    packedLch.perceptual = an::PerceptualEncoding::UnitPacked;
    const an::Result<an::ColorConversionPlan> naturalLch =
        an::planColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lch));
    const an::Result<an::ColorConversionPlan> compatibleLch = an::planColorConversion(srgbEncoded(), packedLch);
    ASSERT_TRUE(naturalLch.hasValue());
    ASSERT_TRUE(compatibleLch.hasValue());
    const std::array<double, 3u> cieLch = convert(*naturalLch, value);
    const std::array<double, 3u> unitLch = convert(*compatibleLch, value);
    const double signedHue = cieLch[2] >= 180.0 ? cieLch[2] - 360.0 : cieLch[2];
    EXPECT_NEAR(unitLch[0], cieLch[0] / 100.0, 1e-12);
    EXPECT_NEAR(unitLch[1], cieLch[1] / 200.0, 1e-12);
    EXPECT_NEAR(unitLch[2], (signedHue + 180.0) / 360.0, 1e-12);

    // Repacking one perceptual form into the other never visits the connection space.
    const an::Result<an::ColorConversionPlan> repack =
        an::planColorConversion(schemaEncoding(an::ChannelSchema::Lab), packed);
    ASSERT_TRUE(repack.hasValue());
    ASSERT_EQ(repack->stages().size(), 1u);
    EXPECT_EQ(repack->stages()[0], an::ColorStage::PackPerceptual);
}

TEST(ColorTransformTest, APlanIsAnOrderedListOfStagesAndSaysWhenItIsOneMatrix)
{
    // Nothing to do at all.
    const an::Result<an::ColorConversionPlan> identity =
        an::planColorConversion(an::ColorEncoding{}, an::ColorEncoding{});
    ASSERT_TRUE(identity.hasValue());
    EXPECT_TRUE(identity->isIdentity());
    EXPECT_TRUE(identity->stages().empty());
    EXPECT_TRUE(identity->isPureChannelMatrix());
    ASSERT_TRUE(identity->linearRgbMatrix().has_value());
    EXPECT_EQ(*identity->linearRgbMatrix(), an::ColorMatrix3{});

    // A change of primaries between two linear encodings is exactly a channel matrix.
    an::ColorEncoding rec2020;
    rec2020.primaries = an::RgbPrimaries::Rec2020;
    const an::Result<an::ColorConversionPlan> primaries = an::planColorConversion(an::ColorEncoding{}, rec2020);
    ASSERT_TRUE(primaries.hasValue());
    ASSERT_EQ(primaries->stages().size(), 2u);
    EXPECT_EQ(primaries->stages()[0], an::ColorStage::LinearRgbToXyz);
    EXPECT_EQ(primaries->stages()[1], an::ColorStage::XyzToLinearRgb);
    EXPECT_TRUE(primaries->isPureChannelMatrix());
    EXPECT_FALSE(primaries->isIdentity());

    // A change of white point adds the adaptation and stays a matrix.
    an::ColorEncoding proPhoto;
    proPhoto.primaries = an::RgbPrimaries::ProPhoto;
    proPhoto.whitePoint = an::ChromaticWhitePoint::D50;
    const an::Result<an::ColorConversionPlan> adapted = an::planColorConversion(an::ColorEncoding{}, proPhoto);
    ASSERT_TRUE(adapted.hasValue());
    ASSERT_EQ(adapted->stages().size(), 3u);
    EXPECT_EQ(adapted->stages()[1], an::ColorStage::AdaptWhitePoint);
    EXPECT_TRUE(adapted->isPureChannelMatrix());

    // A transfer change between the same primaries decodes and re-encodes and nothing else.
    an::ColorEncoding gamma;
    gamma.transfer = an::ColorTransferFunction::PureGamma;
    gamma.transferGamma = 2.2;
    const an::Result<an::ColorConversionPlan> transfer = an::planColorConversion(srgbEncoded(), gamma);
    ASSERT_TRUE(transfer.hasValue());
    ASSERT_EQ(transfer->stages().size(), 2u);
    EXPECT_EQ(transfer->stages()[0], an::ColorStage::DecodeTransfer);
    EXPECT_EQ(transfer->stages()[1], an::ColorStage::EncodeTransfer);
    EXPECT_FALSE(transfer->isPureChannelMatrix());
    EXPECT_TRUE(transfer->linearRgbMatrix().has_value());

    // A cylindrical destination is not a matrix even though it is computed from red, green and blue.
    an::ColorEncoding hsv = schemaEncoding(an::ChannelSchema::Hsv);
    const an::Result<an::ColorConversionPlan> cylindrical = an::planColorConversion(an::ColorEncoding{}, hsv);
    ASSERT_TRUE(cylindrical.hasValue());
    EXPECT_FALSE(cylindrical->isPureChannelMatrix());
    EXPECT_FALSE(cylindrical->linearRgbMatrix().has_value());

    // The longest chain this build plans, and its stages are all named.
    an::ColorEncoding packedLch = schemaEncoding(an::ChannelSchema::Lch);
    packedLch.perceptual = an::PerceptualEncoding::UnitPacked;
    packedLch.whitePoint = an::ChromaticWhitePoint::D50;
    an::ColorEncoding sourceHsi = schemaEncoding(an::ChannelSchema::Hsi);
    sourceHsi.transfer = an::ColorTransferFunction::Srgb;
    const an::Result<an::ColorConversionPlan> longest = an::planColorConversion(sourceHsi, packedLch);
    ASSERT_TRUE(longest.hasValue());
    ASSERT_EQ(longest->stages().size(), 7u);
    EXPECT_EQ(longest->stages()[0], an::ColorStage::HsiToRgb);
    EXPECT_EQ(longest->stages()[1], an::ColorStage::DecodeTransfer);
    EXPECT_EQ(longest->stages()[2], an::ColorStage::LinearRgbToXyz);
    EXPECT_EQ(longest->stages()[3], an::ColorStage::AdaptWhitePoint);
    EXPECT_EQ(longest->stages()[4], an::ColorStage::XyzToLab);
    EXPECT_EQ(longest->stages()[5], an::ColorStage::LabToLch);
    EXPECT_EQ(longest->stages()[6], an::ColorStage::PackPerceptual);
    EXPECT_LE(longest->stages().size(), an::kMaxColorStages);
    EXPECT_STREQ(an::colorStageName(an::ColorStage::AdaptWhitePoint), "AdaptWhitePoint");
    EXPECT_STREQ(an::colorStageName(static_cast<an::ColorStage>(200)), "UnknownColorStage");
}

TEST(ColorTransformTest, OpacityIsNeitherInventedNorDiscardedAndOutOfGamutValuesSurvive)
{
    an::ColorEncoding rgba;
    rgba.schema = an::ChannelSchema::Rgba;
    EXPECT_EQ(an::planColorConversion(an::ColorEncoding{}, rgba).status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::planColorConversion(rgba, an::ColorEncoding{}).status().code(), an::ErrorCode::IncompatibleArguments);

    an::ColorEncoding encodedRgba = rgba;
    encodedRgba.transfer = an::ColorTransferFunction::Srgb;
    const an::Result<an::ColorConversionPlan> alphaPlan = an::planColorConversion(rgba, encodedRgba);
    ASSERT_TRUE(alphaPlan.hasValue()) << alphaPlan.status().message();
    EXPECT_TRUE(alphaPlan->passesAlphaThrough());
    EXPECT_EQ(alphaPlan->sourceChannelCount(), 4u);

    std::array<double, 4u> out{};
    const an::Status wrote = an::convertColorPixel(*alphaPlan, std::array<double, 4u>{0.5, 0.5, 0.5, 0.25}, out);
    ASSERT_TRUE(wrote.ok()) << wrote.message();
    EXPECT_DOUBLE_EQ(out[3], 0.25);
    EXPECT_GT(out[0], 0.7);

    // A span of the wrong length is refused rather than read past.
    std::array<double, 3u> tooFew{};
    EXPECT_EQ(an::convertColorPixel(*alphaPlan, std::array<double, 4u>{0.0, 0.0, 0.0, 0.0}, tooFew).code(),
              an::ErrorCode::InvalidArgument);

    // An out-of-gamut negative keeps its sign through a transfer function instead of becoming a
    // not-a-number, which is what raising it to a fractional power would do.
    const an::Result<an::ColorConversionPlan> encode = an::planColorConversion(an::ColorEncoding{}, srgbEncoded());
    ASSERT_TRUE(encode.hasValue());
    const std::array<double, 3u> negative = convert(*encode, {-0.2, 0.5, 1.4});
    EXPECT_TRUE(std::isfinite(negative[0]));
    EXPECT_LT(negative[0], 0.0);
    EXPECT_GT(negative[2], 1.0);
    EXPECT_LT(roundTripError(an::ColorEncoding{}, srgbEncoded(), {-0.2, 0.5, 1.4}), 1e-12);
}

TEST(ColorTransformTest, AConversionRedescribesTheImageItConverted)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(8u, 4u, 3u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = an::SampleType::Float32,
                                             .channelSchema = an::ChannelSchema::Rgb,
                                             .colorSpace = an::ColorSpace::LinearRgb,
                                             .numericDomain = an::NumericDomain::NormalizedLinear,
                                             .cfa = an::CfaDescriptor{},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    ASSERT_TRUE(descriptor.has_value());

    const an::Result<an::ColorConversionPlan> toLab =
        an::planColorConversion(an::ColorEncoding{}, schemaEncoding(an::ChannelSchema::Lab));
    ASSERT_TRUE(toLab.hasValue());
    const an::Result<an::ImageDescriptor> moved = an::colorTransformedDescriptor(*descriptor, *toLab);
    ASSERT_TRUE(moved.hasValue()) << moved.status().message();
    EXPECT_EQ(moved->channelSchema(), an::ChannelSchema::Lab);
    EXPECT_EQ(moved->colorSpace(), an::ColorSpace::CieLab);
    EXPECT_EQ(moved->numericDomain(), an::NumericDomain::EncodedNonlinear);
    EXPECT_EQ(moved->extent().channelCount(), 3u);
    EXPECT_EQ(moved->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(moved->pixelAspectRatio(), descriptor->pixelAspectRatio());

    // A plan built for one description cannot be used to re-describe an image that says something
    // else, which is the failure that silently double-encodes a frame.
    const an::Result<an::ColorConversionPlan> fromEncoded =
        an::planColorConversion(srgbEncoded(), schemaEncoding(an::ChannelSchema::Lab));
    ASSERT_TRUE(fromEncoded.hasValue());
    EXPECT_EQ(an::colorTransformedDescriptor(*descriptor, *fromEncoded).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ColorTransformTest, AWholeImageConvertsThroughAnyArrangementAndInPlace)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 5u;
    const an::ImageLayout sourceLayout = planarLayout(kWidth, kHeight, 3u, 2u);
    const an::ImageLayout destinationLayout = interleavedLayout(kWidth, kHeight, 3u);

    std::vector<float> source(static_cast<std::size_t>(sourceLayout.totalBytes() / sizeof(float)), 0.0f);
    std::vector<float> destination(static_cast<std::size_t>(destinationLayout.totalBytes() / sizeof(float)), -7.0f);

    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>((channel * kHeight + y) * (kWidth + 2u) + x);
                source[index] = static_cast<float>((x + y * 3u + channel * 7u) % 17u) / 16.0f;
            }
        }
    }

    const an::Result<an::ColorConversionPlan> plan =
        an::planColorConversion(an::ColorEncoding{}, schemaEncoding(an::ChannelSchema::Lab));
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    const an::Status ran = an::convertImageColor(execution, *plan, an::SampleType::Float32, source.data(), sourceLayout,
                                                 destination.data(), destinationLayout);
    ASSERT_TRUE(ran.ok()) << ran.message();

    // Every pixel matches the single-pixel meaning, whatever arrangement each side is in.
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            std::array<double, 3u> pixel{};
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const std::size_t index = static_cast<std::size_t>((channel * kHeight + y) * (kWidth + 2u) + x);
                pixel[channel] = static_cast<double>(source[index]);
            }
            const std::array<double, 3u> expected = convert(*plan, pixel);
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const std::size_t index = static_cast<std::size_t>((y * kWidth + x) * 3u + channel);
                EXPECT_NEAR(static_cast<double>(destination[index]), expected[channel], 1e-4)
                    << "pixel " << x << "," << y << " channel " << channel;
            }
        }
    }

    // The same conversion in place writes the same samples.
    std::vector<float> inPlace = source;
    const an::Status again = an::convertImageColor(execution, *plan, an::SampleType::Float32, inPlace.data(),
                                                   sourceLayout, inPlace.data(), sourceLayout);
    ASSERT_TRUE(again.ok()) << again.message();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t planarIndex = static_cast<std::size_t>((channel * kHeight + y) * (kWidth + 2u) + x);
                const std::size_t interleavedIndex = static_cast<std::size_t>((y * kWidth + x) * 3u + channel);
                EXPECT_FLOAT_EQ(inPlace[planarIndex], destination[interleavedIndex]);
            }
        }
    }

    // Integer storage is refused rather than interpreted, and so is a partial overlap.
    EXPECT_EQ(an::convertImageColor(execution, *plan, an::SampleType::UInt16, source.data(), sourceLayout,
                                    destination.data(), destinationLayout)
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::convertImageColor(execution, *plan, an::SampleType::Float32, source.data(), sourceLayout,
                                    source.data() + 1u, sourceLayout)
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    const an::ImageLayout monoLayout = planarLayout(kWidth, kHeight, 1u);
    EXPECT_EQ(an::convertImageColor(execution, *plan, an::SampleType::Float32, source.data(), monoLayout,
                                    destination.data(), destinationLayout)
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::convertImageColor(an::ExecutionContext{}, *plan, an::SampleType::Float32, source.data(), sourceLayout,
                                    destination.data(), destinationLayout)
                  .code(),
              an::ErrorCode::NotInitialized);
}

TEST(ColorTransformTest, TightPlanarSrgbMatchesThePaddedPathAndWorksInPlace)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 17u;
    constexpr std::uint32_t kHeight = 5u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout tight = planarLayout(kWidth, kHeight, kChannels);
    const an::ImageLayout padded = planarLayout(kWidth, kHeight, kChannels, 3u);
    const std::size_t tightPlane = static_cast<std::size_t>(kWidth) * kHeight;
    const std::size_t paddedRow = kWidth + 3u;
    const std::size_t paddedPlane = paddedRow * kHeight;

    std::vector<float> tightSource(static_cast<std::size_t>(tight.totalBytes() / sizeof(float)), 0.0f);
    std::vector<float> paddedSource(static_cast<std::size_t>(padded.totalBytes() / sizeof(float)), -9.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const float value =
                    static_cast<float>(static_cast<int>((x * 5u + y * 3u + channel * 7u) % 23u) - 3) / 19.0f;
                tightSource[channel * tightPlane + y * kWidth + x] = value;
                paddedSource[channel * paddedPlane + y * paddedRow + x] = value;
            }
        }
    }

    an::ColorEncoding packedLab = schemaEncoding(an::ChannelSchema::Lab);
    packedLab.perceptual = an::PerceptualEncoding::UnitPacked;
    const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(srgbEncoded(), packedLab);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();

    std::vector<float> tightDestination(tightSource.size(), -7.0f);
    std::vector<float> paddedDestination(paddedSource.size(), -7.0f);
    ASSERT_TRUE(an::convertImageColor(execution, *plan, an::SampleType::Float32, tightSource.data(), tight,
                                      tightDestination.data(), tight)
                    .ok());
    ASSERT_TRUE(an::convertImageColor(execution, *plan, an::SampleType::Float32, paddedSource.data(), padded,
                                      paddedDestination.data(), padded)
                    .ok());

    std::vector<float> inPlace = tightSource;
    ASSERT_TRUE(
        an::convertImageColor(execution, *plan, an::SampleType::Float32, inPlace.data(), tight, inPlace.data(), tight)
            .ok());
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t tightIndex = channel * tightPlane + y * kWidth + x;
                const std::size_t paddedIndex = channel * paddedPlane + y * paddedRow + x;
                EXPECT_FLOAT_EQ(tightDestination[tightIndex], paddedDestination[paddedIndex]);
                EXPECT_FLOAT_EQ(inPlace[tightIndex], tightDestination[tightIndex]);
            }
        }
    }
}

// Unit-packed Lab back to RGB runs through its own loops, which replay the stage walker's expressions; whatever the
// arrangement, and in place, every sample is the pixel conversion's narrowed once, out-of-gamut signs included.
TEST(ColorTransformTest, PackedLabBackToRgbIsItsPixelsBitForBitInEveryArrangement)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 19u;
    constexpr std::uint32_t kHeight = 6u;
    constexpr std::uint32_t kChannels = 3u;
    const std::vector<an::ImageLayout> layouts{planarLayout(kWidth, kHeight, kChannels),
                                               planarLayout(kWidth, kHeight, kChannels, 5u),
                                               interleavedLayout(kWidth, kHeight, kChannels)};
    an::ColorEncoding packedLab = schemaEncoding(an::ChannelSchema::Lab);
    packedLab.perceptual = an::PerceptualEncoding::UnitPacked;
    for (const an::ColorEncoding& destination : {srgbEncoded(), an::ColorEncoding{}})
    {
        const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(packedLab, destination);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        for (const an::ImageLayout& layout : layouts)
        {
            std::vector<float> source(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 0.0f);
            const auto at = [&layout](std::uint32_t channel, std::uint32_t x, std::uint32_t y) {
                return static_cast<std::size_t>(*layout.sampleByteOffset(channel / layout.samplesPerPixel(), y, x,
                                                                         channel % layout.samplesPerPixel()) /
                                                sizeof(float));
            };
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    // Lightness across its range, and opponents far enough from neutral to leave the gamut.
                    source[at(0u, x, y)] = static_cast<float>(x) / static_cast<float>(kWidth - 1u);
                    source[at(1u, x, y)] = static_cast<float>((x * 7u + y * 3u) % 17u) / 16.0f;
                    source[at(2u, x, y)] = static_cast<float>((x * 5u + y * 11u) % 13u) / 12.0f;
                }
            }
            std::vector<float> converted(source.size(), -9.0f);
            ASSERT_TRUE(an::convertImageColor(execution, *plan, an::SampleType::Float32, source.data(), layout,
                                              converted.data(), layout)
                            .ok());
            std::vector<float> inPlace = source;
            ASSERT_TRUE(an::convertImageColor(execution, *plan, an::SampleType::Float32, inPlace.data(), layout,
                                              inPlace.data(), layout)
                            .ok());
            bool sawNegative = false;
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                for (std::uint32_t x = 0u; x < kWidth; ++x)
                {
                    const std::array<double, 3u> expected =
                        convert(*plan, {source[at(0u, x, y)], source[at(1u, x, y)], source[at(2u, x, y)]});
                    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
                    {
                        const float narrowed = static_cast<float>(expected[channel]);
                        sawNegative = sawNegative || narrowed < 0.0f;
                        EXPECT_EQ(converted[at(channel, x, y)], narrowed) << channel << " at " << x << "," << y;
                        EXPECT_EQ(inPlace[at(channel, x, y)], narrowed) << channel << " at " << x << "," << y;
                    }
                }
            }
            EXPECT_TRUE(sawNegative) << "the fixture leaves the gamut, so a restored sign is exercised";
        }
    }
}

TEST(ColorTransformTest, AFrameOfManyChunksConvertsEveryPixelOnceAtEveryWidth)
{
    // Large enough that the tight path divides it into many chunks at eight workers, and a multiple
    // of no chunk, so every boundary and a short last chunk are crossed. The padded path converts row
    // by row with the same formulas, so it is the answer bit for bit, for production and preview.
    constexpr std::uint32_t kWidth = 1021u;
    constexpr std::uint32_t kHeight = 401u;
    constexpr std::uint32_t kChannels = 3u;
    const an::ImageLayout tight = planarLayout(kWidth, kHeight, kChannels);
    const an::ImageLayout padded = planarLayout(kWidth, kHeight, kChannels, 3u);
    const std::size_t tightPlane = static_cast<std::size_t>(kWidth) * kHeight;
    const std::size_t paddedRow = kWidth + 3u;
    const std::size_t paddedPlane = paddedRow * kHeight;
    std::vector<float> tightSource(tightPlane * kChannels, 0.0f);
    std::vector<float> paddedSource(paddedPlane * kChannels, -9.0f);
    for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                // From below zero, through the transfer function's linear segment, to above one.
                const float value = static_cast<float>((x * 37u + y * 101u + channel * 53u) % 1201u) / 1000.0f - 0.1f;
                tightSource[channel * tightPlane + y * kWidth + x] = value;
                paddedSource[channel * paddedPlane + y * paddedRow + x] = value;
            }
        }
    }
    an::ColorEncoding packedLab = schemaEncoding(an::ChannelSchema::Lab);
    packedLab.perceptual = an::PerceptualEncoding::UnitPacked;
    const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(srgbEncoded(), packedLab);
    const an::Result<an::RealtimePreviewColorConversionPlan> previewPlan =
        an::planRealtimePreviewColorConversion(srgbEncoded(), packedLab);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    ASSERT_TRUE(previewPlan.hasValue()) << previewPlan.status().message();

    for (const std::uint32_t workers : {1u, 8u})
    {
        an::EngineContextOptions options;
        options.maxConcurrency = workers;
        an::Result<an::EngineContext> engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue()) << engine.status().message();
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, {});
        ASSERT_TRUE(execution.hasValue()) << execution.status().message();
        for (const bool preview : {false, true})
        {
            std::vector<float> tightDestination(tightSource.size(), -7.0f);
            std::vector<float> paddedDestination(paddedSource.size(), -7.0f);
            const auto run = [&](const float* source, const an::ImageLayout& layout, float* destination) {
                return preview ? an::convertRealtimePreviewImageColor(*execution, *previewPlan, an::SampleType::Float32,
                                                                      source, layout, destination, layout)
                               : an::convertImageColor(*execution, *plan, an::SampleType::Float32, source, layout,
                                                       destination, layout);
            };
            ASSERT_TRUE(run(tightSource.data(), tight, tightDestination.data()).ok());
            ASSERT_TRUE(run(paddedSource.data(), padded, paddedDestination.data()).ok());
            std::size_t differing = 0u;
            std::size_t first = tightDestination.size();
            for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
            {
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        const std::size_t tightIndex = channel * tightPlane + y * kWidth + x;
                        if (std::memcmp(&tightDestination[tightIndex],
                                        &paddedDestination[channel * paddedPlane + y * paddedRow + x],
                                        sizeof(float)) != 0)
                        {
                            first = differing == 0u ? tightIndex : first;
                            ++differing;
                        }
                    }
                }
            }
            EXPECT_EQ(differing, 0u) << workers << " workers, " << (preview ? "preview" : "production")
                                     << ", first at sample " << first;
        }
    }
}

TEST(ColorTransformTest, AChannelMatrixIsAFusablePointwiseStep)
{
    an::PointwisePlanShape shape;
    shape.channelCount = 3u;

    const an::Result<an::ColorMatrix3> matrix = an::rgbToXyzMatrix(an::RgbPrimaries::Srgb);
    ASSERT_TRUE(matrix.hasValue());
    const an::Result<an::PointwiseProgram> program = an::buildColorMatrixProgram(shape, *matrix);
    ASSERT_TRUE(program.hasValue()) << program.status().message();
    EXPECT_EQ(program->channelCount(), 3u);
    EXPECT_EQ(program->steps().size(), 1u);
    EXPECT_EQ(program->steps()[0].kind, an::PointwiseOpKind::ChannelMatrix);

    const std::array<double, 3u> input{0.3, 0.6, 0.1};
    std::array<double, 3u> output{};
    const an::Status evaluated = an::evaluatePointwisePixel(*program, input, output);
    ASSERT_TRUE(evaluated.ok()) << evaluated.message();
    expectNear(output, an::applyColorMatrix(*matrix, input), 1e-6);

    // The plan for a pure primaries change reports the same matrix, so a caller may execute the
    // whole conversion inside a chain it was already running.
    an::ColorEncoding rec2020;
    rec2020.primaries = an::RgbPrimaries::Rec2020;
    const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(an::ColorEncoding{}, rec2020);
    ASSERT_TRUE(plan.hasValue());
    ASSERT_TRUE(plan->isPureChannelMatrix());
    const std::array<double, 3u> throughPlan = convert(*plan, input);
    expectNear(an::applyColorMatrix(*plan->linearRgbMatrix(), input), throughPlan, 1e-12);

    // A coefficient count that is not the square of the channel count, a coefficient that is not
    // finite, and integer storage are each refused by name.
    constexpr std::array<double, 4u> tooFew{1.0, 0.0, 0.0, 1.0};
    EXPECT_EQ(an::buildChannelMatrixProgram(shape, tooFew).status().code(), an::ErrorCode::IncompatibleArguments);

    std::array<double, 9u> broken = matrix->coefficients;
    broken[4] = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::buildChannelMatrixProgram(shape, broken).status().code(), an::ErrorCode::InvalidArgument);

    an::PointwisePlanShape integerShape = shape;
    integerShape.sampleType = an::SampleType::UInt16;
    EXPECT_EQ(an::buildChannelMatrixProgram(integerShape, matrix->coefficients).status().code(),
              an::ErrorCode::IncompatibleArguments);

    // A one-channel matrix is a scale, which is the smallest case the general lowering has to admit.
    an::PointwisePlanShape monoShape;
    constexpr std::array<double, 1u> half{0.5};
    const an::Result<an::PointwiseProgram> monoProgram = an::buildChannelMatrixProgram(monoShape, half);
    ASSERT_TRUE(monoProgram.hasValue()) << monoProgram.status().message();
    std::array<double, 1u> monoOut{};
    const an::Status monoEvaluated = an::evaluatePointwisePixel(*monoProgram, std::array<double, 1u>{0.8}, monoOut);
    ASSERT_TRUE(monoEvaluated.ok());
    EXPECT_NEAR(monoOut[0], 0.4, 1e-6);
}

TEST(ColorTransformTest, MatrixRefusalsNameTheOperationCalled)
{
    an::ColorMatrix3 singular{};
    singular.coefficients.fill(0.0);
    EXPECT_STREQ(an::invertColorMatrix(singular).status().operation(), "an::invertColorMatrix");
    const auto primaries = static_cast<an::RgbPrimaries>(255);
    EXPECT_STREQ(an::rgbToXyzMatrix(primaries).status().operation(), "an::rgbToXyzMatrix");
    EXPECT_STREQ(an::xyzToRgbMatrix(primaries).status().operation(), "an::xyzToRgbMatrix");
    EXPECT_STREQ(an::chromaticAdaptationMatrix(static_cast<an::ChromaticWhitePoint>(255), an::ChromaticWhitePoint::D65)
                     .status()
                     .operation(),
                 "an::chromaticAdaptationMatrix");
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour calibration tools' shared support, called directly: the closed limits and the samples a measurement
/// keeps, the gain rule and its ties, a manual white and the offsets, a mask's representation, the SCNR protections,
/// the rectangles a dialog names and the refusals of an image a tool does not read.

#include "Test_ProcessesColorTools/ColorToolsTestSupport.h"

#include "processes/ColorToolsSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace support = an::ui::processes;
using namespace an::tests::processes;
using namespace an::tests::processes::colortools;

class ColorToolsUnitsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// Runs a call over an asset under an admitted operation and the asset's read lease.
    template <typename CallT> auto over(const an::consumer::ImageAsset& asset, CallT call)
    {
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
        EXPECT_TRUE(scope.hasValue());
        an::Result<an::consumer::AssetReadLease> lease = asset.read();
        EXPECT_TRUE(lease.hasValue());
        auto result = call(*scope, an::ui::ProcessSource::of(*lease));
        lease->release();
        scope->finish(an::Status::success());
        return result;
    }

    /// A three-channel Float32 image whose channel `c` at a pixel is `value(x, y, c)`.
    [[nodiscard]] an::consumer::ImageAsset
    rgb(std::uint32_t width, std::uint32_t height,
        const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
    {
        std::optional<an::consumer::ImageAsset> asset =
            an::tests::viewer::makeAsset<float>(session(), width, height, an::tests::viewer::Channels::Rgb, value);
        EXPECT_TRUE(asset.has_value());
        return std::move(*asset);
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
};

} // namespace

// A measurement's limits are a closed interval: a sample on either limit is kept.
TEST_F(ColorToolsUnitsTest, TheLimitsAreAClosedInterval)
{
    // Channel values 0.125, 0.25, ..., 1 along a row - binary fractions, so each limit is a sample exactly; the limits
    // 0.25 and 0.5 keep 0.25, 0.375 and 0.5.
    const an::consumer::ImageAsset image =
        rgb(8u, 1u, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.125f * static_cast<float>(x + 1u); });
    const an::Result<an::ColorChannelValues> means = over(image, [](auto& scope, const auto& source) {
        return support::measureChannelMeans(scope, source, std::nullopt, support::FractionRange{0.25, 0.5},
                                            "reference");
    });
    ASSERT_TRUE(means.hasValue()) << means.status().message();
    for (const double mean : *means)
    {
        EXPECT_DOUBLE_EQ(mean, 0.375);
    }
    const an::Result<an::ColorChannelValues> medians = over(image, [](auto& scope, const auto& source) {
        return support::measureChannelMedians(
            scope, source, an::StatisticsRegion{.originX = 4u, .width = 4u, .height = 1u}, std::nullopt, "reference");
    });
    ASSERT_TRUE(medians.hasValue()) << medians.status().message();
    for (const double median : *medians)
    {
        EXPECT_DOUBLE_EQ(median, 0.8125) << "the rectangle's four samples, the middle two averaged";
    }
}

// A measurement keeps only what the image says means something: a sample its rule rejects - a stored zero of an image
// whose zero means no data, judged on the stored samples - is not measured.
TEST_F(ColorToolsUnitsTest, AMeasurementKeepsOnlyWhatTheImageSaysMeansSomething)
{
    const an::consumer::ImageAsset image = rgb(4u, 1u, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
        return x == 0u ? 0.0f : 0.2f * static_cast<float>(x);
    });
    an::ImageSemanticsRequest meaning;
    meaning.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue());
    const std::optional<an::consumer::ImageAsset> noData =
        withMetadata(session(), image,
                     an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{},
                                               an::Provenance{}, an::CodecMetadata{}, std::nullopt));
    ASSERT_TRUE(noData.has_value());
    const std::optional<an::consumer::ImageAsset> asCodes = convertedTo(session(), *noData, an::SampleType::UInt16);
    ASSERT_TRUE(asCodes.has_value());
    for (const an::consumer::ImageAsset* asset : {&*noData, &*asCodes})
    {
        const an::Result<an::ColorChannelValues> medians = over(*asset, [](auto& scope, const auto& source) {
            return support::measureChannelMedians(scope, source, std::nullopt, std::nullopt, "reference");
        });
        ASSERT_TRUE(medians.hasValue()) << medians.status().message();
        EXPECT_NEAR((*medians)[0], 0.4, 1.0e-4) << "the median of 0.2, 0.4 and 0.6; the zero means nothing";
    }
}

// A linear match measures the samples that are not zero - a zero means nothing in either image - so a target with
// empty pixels matches a reference twice it plus 0.05 exactly.
TEST_F(ColorToolsUnitsTest, ALinearMatchMeasuresNoZero)
{
    // Thirty-two samples of 1/64 to 32/64, and thirty-two that are zero in the target and 0.5 in the reference.
    const auto value = [](std::uint32_t x, std::uint32_t y) {
        return y == 0u ? static_cast<float>(x + 1u) / 64.0f : 0.0f;
    };
    const an::consumer::ImageAsset target =
        rgb(32u, 2u, [&](std::uint32_t x, std::uint32_t y, std::uint32_t) { return value(x, y); });
    const an::consumer::ImageAsset reference = rgb(32u, 2u, [&](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return y == 0u ? 2.0f * value(x, y) + 0.05f : 0.5f;
    });
    an::Result<an::consumer::AssetReadLease> lease = reference.read();
    ASSERT_TRUE(lease.hasValue());
    const an::Result<an::FrameNormalization> match = over(target, [&](auto& scope, const auto& source) {
        return support::measureLinearMatch(scope, source, an::ui::ProcessSource::of(*lease),
                                           support::FractionRange{0.0, 1.0});
    });
    ASSERT_TRUE(match.hasValue()) << match.status().message();
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_NEAR(match->channels[c].scale, 2.0, 1.0e-6);
        EXPECT_NEAR(match->channels[c].offset, 0.05, 1.0e-6);
    }
}

// A linear match of two images of one size pairs every position whose reference lies within the limits, whatever the
// target holds there: a target that rises past the upper limit is measured whole.
TEST_F(ColorToolsUnitsTest, APairedLinearMatchHoldsOnlyTheReferenceToTheLimits)
{
    std::vector<double> targets;
    std::vector<double> references;
    for (std::uint32_t x = 0u; x < 32u; ++x)
    {
        const float t = static_cast<float>(x + 1u) / 32.0f;
        targets.push_back(static_cast<double>(t));
        references.push_back(static_cast<double>(0.6f * t * t));
    }
    const an::consumer::ImageAsset target =
        rgb(32u, 1u, [&](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<float>(targets[x]); });
    const an::consumer::ImageAsset reference =
        rgb(32u, 1u, [&](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<float>(references[x]); });
    an::Result<an::consumer::AssetReadLease> lease = reference.read();
    ASSERT_TRUE(lease.hasValue());
    const an::Result<an::FrameNormalization> match = over(target, [&](auto& scope, const auto& source) {
        return support::measureLinearMatch(scope, source, an::ui::ProcessSource::of(*lease),
                                           support::FractionRange{0.0, 0.61});
    });
    ASSERT_TRUE(match.hasValue()) << match.status().message();
    const double scale = deviationOf(references) / deviationOf(targets);
    const double offset = medianOf(references) - scale * medianOf(targets);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_NEAR(match->channels[c].scale, scale, 1.0e-6);
        EXPECT_NEAR(match->channels[c].offset, offset, 1.0e-6);
    }
}

// A measurement that keeps no sample of a channel is refused, naming the channel and the limits.
TEST_F(ColorToolsUnitsTest, AChannelThatKeepsNothingIsRefusedByName)
{
    const an::consumer::ImageAsset image =
        rgb(4u, 4u, [](std::uint32_t, std::uint32_t, std::uint32_t c) { return c == 2u ? 0.8f : 0.05f; });
    const an::Result<an::ColorChannelValues> medians = over(image, [](auto& scope, const auto& source) {
        return support::measureChannelMedians(scope, source, std::nullopt, support::FractionRange{0.0, 0.1},
                                              "background reference");
    });
    ASSERT_FALSE(medians.hasValue());
    EXPECT_EQ(medians.status().code(), an::ErrorCode::EmptySelection);
    EXPECT_NE(std::string(medians.status().message()).find("No blue sample of the background reference"),
              std::string::npos)
        << medians.status().message();
}

// The gain rule: each channel's white above its background, normalized to the strongest, which is the first of two
// equal ones, with offsets that keep every background where it was; a manual white is the gains about the background.
TEST_F(ColorToolsUnitsTest, TheGainRuleNormalizesToTheStrongestChannel)
{
    // A background of 0.125, 0.25 and 0.125 on the left half; whites of 0.625, 0.75 and 0.375 on the right - every
    // level a binary fraction, so the tie between red and green is exact.
    const an::consumer::ImageAsset image = rgb(8u, 2u, [](std::uint32_t x, std::uint32_t, std::uint32_t c) {
        const std::array<float, 3u> sky{0.125f, 0.25f, 0.125f};
        const std::array<float, 3u> white{0.625f, 0.75f, 0.375f};
        return x < 4u ? sky[c] : white[c];
    });
    const auto measure = [&](std::optional<an::ColorChannelGains> manual) {
        return over(image, [manual](auto& scope, const auto& source) {
            support::ColorCalibrationRequest request;
            request.manualWhite = manual;
            request.whiteRegion = an::StatisticsRegion{.originX = 4u, .width = 4u, .height = 2u};
            request.backgroundRegion = an::StatisticsRegion{.originX = 0u, .width = 4u, .height = 2u};
            return support::measureColorCalibration(scope, &source, source, request);
        });
    };
    const an::Result<support::ColorCalibrationMeasurement> automatic = measure(std::nullopt);
    ASSERT_TRUE(automatic.hasValue()) << automatic.status().message();
    // Red and green both stand 0.5 above their backgrounds: green, the later, is the strongest, and both gain one.
    EXPECT_DOUBLE_EQ(automatic->gains[0], 1.0);
    EXPECT_DOUBLE_EQ(automatic->gains[1], 1.0);
    EXPECT_DOUBLE_EQ(automatic->gains[2], 2.0);
    for (std::size_t c = 0u; c < 3u; ++c)
    {
        EXPECT_NEAR(automatic->offsets[c], automatic->background[c] * (1.0 - automatic->gains[c]), 1.0e-12);
    }
    EXPECT_DOUBLE_EQ(automatic->background[1], 0.25);

    const an::Result<support::ColorCalibrationMeasurement> manual = measure(an::ColorChannelGains{0.9, 1.0, 0.8});
    ASSERT_TRUE(manual.hasValue()) << manual.status().message();
    EXPECT_DOUBLE_EQ(manual->gains[0], 0.9);
    EXPECT_NEAR(manual->offsets[2], 0.125 * 0.2, 1.0e-12) << "scaled about the background, not about zero";

    const an::Result<support::ColorCalibrationMeasurement> zero = measure(an::ColorChannelGains{1.0, 0.0, 1.0});
    ASSERT_FALSE(zero.hasValue());
    EXPECT_EQ(zero.status().code(), an::ErrorCode::InvalidArgument);
}

// A colour calibration's background is the median of every sample of its reference - no limit bounds it - so five
// bright samples of eight make the background bright.
TEST_F(ColorToolsUnitsTest, TheBackgroundLevelIsTheMedianOfEverySample)
{
    const an::consumer::ImageAsset background =
        rgb(8u, 1u, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x < 3u ? 0.125f : 0.9375f; });
    const an::Result<support::ColorCalibrationMeasurement> measured =
        over(background, [](auto& scope, const auto& source) {
            support::ColorCalibrationRequest request;
            request.manualWhite = an::ColorChannelGains{1.0, 1.0, 1.0};
            return support::measureColorCalibration(scope, nullptr, source, request);
        });
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    for (const double level : measured->background)
    {
        EXPECT_DOUBLE_EQ(level, 0.9375);
    }
}

// A neutralization brings every channel's level to the mean of the three.
TEST_F(ColorToolsUnitsTest, ANeutralizationBringsEveryLevelToTheirMean)
{
    const an::consumer::ImageAsset image = rgb(
        4u, 4u, [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.05f + 0.02f * static_cast<float>(c); });
    const an::Result<support::NeutralizationMeasurement> measured = over(image, [](auto& scope, const auto& source) {
        return support::measureNeutralization(scope, source, std::nullopt, support::FractionRange{0.0, 0.1});
    });
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_NEAR(measured->pedestal, 0.07, 1.0e-6);
    for (std::size_t c = 0u; c < 3u; ++c)
    {
        EXPECT_NEAR(measured->levels[c] + measured->offsets[c], measured->pedestal, 1.0e-12);
    }
}

// A mask is the reference's representation at full scale where selected and at the bottom of its range elsewhere.
TEST_F(ColorToolsUnitsTest, AMaskSpansItsRepresentationsRange)
{
    const an::consumer::ImageAsset image =
        rgb(4u, 1u, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.25f * static_cast<float>(x); });
    for (const an::SampleType type : {an::SampleType::UInt16, an::SampleType::Int8, an::SampleType::Float64})
    {
        SCOPED_TRACE(an::sampleTypeName(type));
        const std::optional<an::consumer::ImageAsset> converted = convertedTo(session(), image, type);
        ASSERT_TRUE(converted.has_value());
        an::Result<an::consumer::ImageAsset> mask = over(*converted, [](auto& scope, const auto& source) {
            return support::selectedSamplesMask(scope, source,
                                                an::StatisticsRegion{.originX = 1u, .width = 3u, .height = 1u},
                                                support::FractionRange{0.0, 0.6});
        });
        ASSERT_TRUE(mask.hasValue()) << mask.status().message();
        const std::optional<fixtures::FixtureSamples> samples = channels::assetSamples(*mask);
        ASSERT_TRUE(samples.has_value());
        const double bottom =
            fixtures::isFloating(samples->sampleType()) ? 0.0 : fixtures::lowestOf(samples->sampleType());
        const double top =
            fixtures::isFloating(samples->sampleType()) ? 1.0 : fixtures::highestOf(samples->sampleType());
        // Pixel 0 lies outside the rectangle; pixels 1 and 2 hold 0.25 and 0.5; pixel 3 holds 0.75, above the limit.
        // A signed image's fractions are counted from its smallest value, so its 0.25 is no longer below 0.6.
        EXPECT_EQ(samples->value(0u, 0u, 0u), bottom);
        EXPECT_EQ(samples->value(1u, 0u, 0u), type == an::SampleType::Int8 ? bottom : top);
        EXPECT_EQ(samples->value(3u, 0u, 0u), bottom);
    }
}

// The SCNR protections the dialog offers, in its order, and the methods they name.
TEST_F(ColorToolsUnitsTest, TheScnrProtectionsNameTheEngineMethods)
{
    const std::vector<QString>& labels = support::scnrProtectionLabels();
    ASSERT_EQ(labels.size(), 4u);
    EXPECT_EQ(support::scnrMethodFromLabel(labels[0]), an::ChromaticNoiseMethod::AverageNeutral);
    EXPECT_EQ(support::scnrMethodFromLabel(labels[1]), an::ChromaticNoiseMethod::MaximumNeutral);
    EXPECT_EQ(support::scnrMethodFromLabel(labels[2]), an::ChromaticNoiseMethod::MaximumMask);
    EXPECT_EQ(support::scnrMethodFromLabel(labels[3]), an::ChromaticNoiseMethod::AdditiveMask);
    EXPECT_FALSE(support::scnrMethodFromLabel(QStringLiteral("Average")).has_value());
}

// The rectangle a dialog names: nothing for the whole image, and inside only when wholly inside and not empty.
TEST_F(ColorToolsUnitsTest, ARectangleIsTheWholeImageOrARegionInsideIt)
{
    const an::consumer::ImageAsset image =
        rgb(12u, 8u, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    const std::optional<an::ImageDescriptor> described = image.descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_FALSE(support::regionOf(QRect(0, 0, 12, 8), *described).has_value());
    const std::optional<an::StatisticsRegion> region = support::regionOf(QRect(2, 1, 4, 3), *described);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->originX, 2u);
    EXPECT_EQ(region->height, 3u);
    EXPECT_TRUE(support::rectangleInside(QRect(0, 0, 12, 8), *described));
    EXPECT_FALSE(support::rectangleInside(QRect(1, 0, 12, 8), *described));
    EXPECT_FALSE(support::rectangleInside(QRect(0, 0, 0, 8), *described));
    EXPECT_FALSE(support::rectangleInside(QRect(-1, 0, 4, 4), *described));
}

// The tools refuse an image they do not read: SCNR a grey image, a linear match a reference of another channel count.
TEST_F(ColorToolsUnitsTest, AnImageATooDoesNotReadIsRefused)
{
    std::optional<an::consumer::ImageAsset> grey = an::tests::viewer::makeAsset<float>(
        session(), 4u, 4u, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f * static_cast<float>(x + 1u); });
    ASSERT_TRUE(grey.has_value());
    const an::Result<an::consumer::ImageAsset> suppressed = over(*grey, [](auto& scope, const auto& source) {
        return support::suppressGreen(scope, source, support::GreenSuppression{}, support::ViewMaskSnapshot{});
    });
    ASSERT_FALSE(suppressed.hasValue());
    EXPECT_EQ(suppressed.status().code(), an::ErrorCode::Unsupported);

    const an::consumer::ImageAsset colour =
        rgb(4u, 4u, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f * static_cast<float>(x + 1u); });
    an::Result<an::consumer::AssetReadLease> reference = colour.read();
    ASSERT_TRUE(reference.hasValue());
    const an::Result<an::FrameNormalization> match = over(*grey, [&](auto& scope, const auto& source) {
        return support::measureLinearMatch(scope, source, an::ui::ProcessSource::of(*reference),
                                           support::FractionRange{0.0, 1.0});
    });
    ASSERT_FALSE(match.hasValue());
    EXPECT_EQ(match.status().code(), an::ErrorCode::IncompatibleArguments);
}

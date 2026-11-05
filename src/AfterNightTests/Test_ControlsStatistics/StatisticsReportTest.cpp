// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The figures the statistics window shows, held to statistics computed here rather than by the engine: every sample
/// read as a fraction of its representation's range - an unsigned integer over its largest code, a signed one from its
/// smallest value to its largest, a float as it is - over the finite samples the image's validity admits, and with the
/// clipped ones - at or below zero, at or above one - left out when the window leaves them out.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "controls/ImageStatisticsReport.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using an::tests::statistics::assetWithValidity;
using an::tests::statistics::Channels;
using an::tests::statistics::fixtureAsset;
using an::tests::statistics::FixtureAsset;

/// The figures of a list of fractions, by the textbook definitions: population moments, the median and the median
/// absolute deviation as the mean of the two middle values of an even count, and Tukey's biweight midvariance about the
/// median with a tuning constant of nine median absolute deviations.
struct StatedFigures
{
    std::uint64_t count = 0u;
    double mean = 0.0;
    double median = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double variance = 0.0;
    double averageDeviation = 0.0;
    double medianAbsoluteDeviation = 0.0;
    double biweightMidvariance = 0.0;
};

[[nodiscard]] double medianOf(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2u;
    return values.size() % 2u == 1u ? values[middle] : (values[middle - 1u] + values[middle]) / 2.0;
}

[[nodiscard]] StatedFigures statedFigures(const std::vector<double>& values)
{
    StatedFigures figures;
    figures.count = values.size();
    if (values.empty())
    {
        return figures;
    }
    figures.minimum = *std::min_element(values.begin(), values.end());
    figures.maximum = *std::max_element(values.begin(), values.end());
    figures.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    for (const double value : values)
    {
        figures.variance += (value - figures.mean) * (value - figures.mean);
        figures.averageDeviation += std::fabs(value - figures.mean);
    }
    figures.variance /= static_cast<double>(values.size());
    figures.averageDeviation /= static_cast<double>(values.size());
    figures.median = medianOf(values);
    std::vector<double> deviations;
    for (const double value : values)
    {
        deviations.push_back(std::fabs(value - figures.median));
    }
    figures.medianAbsoluteDeviation = medianOf(deviations);
    if (figures.medianAbsoluteDeviation > 0.0)
    {
        double upper = 0.0;
        double lower = 0.0;
        for (const double value : values)
        {
            const double u = (value - figures.median) / (9.0 * figures.medianAbsoluteDeviation);
            if (std::fabs(u) < 1.0)
            {
                const double square = u * u;
                upper += (value - figures.median) * (value - figures.median) * std::pow(1.0 - square, 4.0);
                lower += (1.0 - square) * (1.0 - 5.0 * square);
            }
        }
        figures.biweightMidvariance = static_cast<double>(values.size()) * upper / (lower * lower);
    }
    return figures;
}

/// A sample as a fraction of its representation's range, by the rule this file opens with.
[[nodiscard]] double fractionOf(const double value, const fixtures::FixtureSampleType type)
{
    if (fixtures::isFloating(type))
    {
        return value;
    }
    const double lowest = fixtures::lowestOf(type);
    return (value - lowest) / (fixtures::highestOf(type) - lowest);
}

/// One channel's fractions over its finite samples, the clipped ones left out when asked.
[[nodiscard]] std::vector<double> fractions(const fixtures::FixtureSamples& samples, const std::uint32_t channel,
                                            const bool excludeClipped)
{
    std::vector<double> values;
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples.width(); ++x)
        {
            const double value = fractionOf(samples.value(x, y, channel), samples.sampleType());
            if (!std::isfinite(value) || (excludeClipped && (value <= 0.0 || value >= 1.0)))
            {
                continue;
            }
            values.push_back(value);
        }
    }
    return values;
}

/// Whether a figure agrees with its statement to a rounding of the larger, and of one millionth of a millionth when
/// both are next to nothing - a constant channel's spread, which a stated mean accumulated in rounding steps leaves
/// above zero.
[[nodiscard]] testing::AssertionResult agrees(const char* what, const double got, const double want,
                                              const double relative = 1e-9)
{
    const double tolerance = (std::max)(1e-12, relative * (std::max)(std::fabs(got), std::fabs(want)));
    if (std::fabs(got - want) <= tolerance)
    {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << what << " " << got << " against " << want;
}

/// The cases' session and a report of an asset in it.
class StatisticsReportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        an::Result<an::consumer::EngineSession> session = fixtures::engine::createFixtureSession(2u);
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(*session));
    }

    void TearDown() override
    {
        m_session.reset();
    }

    [[nodiscard]] an::Result<an::ui::ImageStatisticsReport> report(const an::consumer::ImageAsset& asset,
                                                                   const bool excludeClipped)
    {
        an::Result<an::consumer::OperationScope> scope = fixtures::engine::admitFixtureOperation(*m_session);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::consumer::AssetReadLease> lease = asset.read();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        return an::ui::computeImageStatisticsReport(*scope, *lease, excludeClipped);
    }

    std::optional<an::consumer::EngineSession> m_session;
};

/// Where a channel's figures disagree with the statement, as text; empty when they agree.
[[nodiscard]] std::string disagreements(const an::ui::ImageStatisticsChannel& got, const StatedFigures& want)
{
    std::string text;
    const auto check = [&text](const testing::AssertionResult& result) {
        if (!result)
        {
            text += std::string(" ") + result.message() + ";";
        }
    };
    if (got.count != want.count)
    {
        text += " count " + std::to_string(got.count) + " against " + std::to_string(want.count) + ";";
    }
    if (want.count == 0u)
    {
        return text;
    }
    check(agrees("mean", got.mean, want.mean));
    check(agrees("median", got.median, want.median));
    check(agrees("minimum", got.minimum, want.minimum));
    check(agrees("maximum", got.maximum, want.maximum));
    check(agrees("variance", got.variance, want.variance, 1e-8));
    check(agrees("standard deviation", got.standardDeviation, std::sqrt(want.variance), 1e-8));
    check(agrees("average deviation", got.averageDeviation, want.averageDeviation, 1e-8));
    check(agrees("median absolute deviation", got.medianAbsoluteDeviation, want.medianAbsoluteDeviation));
    check(agrees("biweight midvariance", got.biweightMidvariance, want.biweightMidvariance, 1e-6));
    return text;
}

} // namespace

// Every standard fixture - the eight representations, grey and colour, the not-a-number fixtures among them - measures
// as stated, with the clipped samples kept and left out.
TEST_F(StatisticsReportTest, EveryStandardFixtureMeasuresAsStated)
{
    std::size_t compared = 0u;
    for (const fixtures::FixtureImage& image : fixtures::standardFixtures())
    {
        const std::optional<FixtureAsset> made = fixtureAsset(*m_session, image);
        ASSERT_TRUE(made.has_value()) << image.name;
        for (const bool excludeClipped : {false, true})
        {
            const an::Result<an::ui::ImageStatisticsReport> measured = report(*made->asset, excludeClipped);
            ASSERT_TRUE(measured.hasValue()) << image.name << ": " << measured.status().message();
            ASSERT_EQ(measured->channels.size(), made->samples.channelCount()) << image.name;
            for (std::uint32_t channel = 0u; channel < made->samples.channelCount(); ++channel)
            {
                const an::ui::ImageStatisticsChannel& got = measured->channels[channel];
                EXPECT_EQ(got.pixels, static_cast<std::uint64_t>(made->samples.width()) * made->samples.height());
                EXPECT_EQ(disagreements(got, statedFigures(fractions(made->samples, channel, excludeClipped))), "")
                    << image.name << " channel " << channel << (excludeClipped ? " without" : " with")
                    << " the clipped samples";
            }
            ++compared;
        }
    }
    EXPECT_GE(compared, 10u);
}

// Leaving the clipped samples out leaves out both ends of every representation: the smallest and largest code of an
// unsigned and of a signed integer, and zero and one and everything beyond them of a float.
TEST_F(StatisticsReportTest, LeavingTheClippedSamplesOutDropsBothEndsOfEveryRepresentation)
{
    const std::optional<an::consumer::ImageAsset> unsignedImage = an::tests::statistics::makeAsset<std::uint16_t>(
        *m_session, 5u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            constexpr std::uint16_t kValues[] = {0u, 1u, 30000u, 65534u, 65535u};
            return kValues[x];
        });
    const std::optional<an::consumer::ImageAsset> signedImage = an::tests::statistics::makeAsset<std::int8_t>(
        *m_session, 5u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            constexpr std::int8_t kValues[] = {-128, -127, 0, 126, 127};
            return kValues[x];
        });
    const std::optional<an::consumer::ImageAsset> floatImage = an::tests::statistics::makeAsset<float>(
        *m_session, 6u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            const float values[] = {-0.25f, 0.0f, 0.2f, 0.4f, 1.0f, 1.5f};
            return values[x];
        });
    ASSERT_TRUE(unsignedImage.has_value() && signedImage.has_value() && floatImage.has_value());

    for (const an::consumer::ImageAsset* image : {&*unsignedImage, &*signedImage, &*floatImage})
    {
        const an::Result<an::ui::ImageStatisticsReport> all = report(*image, false);
        const an::Result<an::ui::ImageStatisticsReport> inside = report(*image, true);
        ASSERT_TRUE(all.hasValue() && inside.hasValue());
        EXPECT_EQ(all->channels.front().count, image == &*floatImage ? 6u : 5u);
        EXPECT_EQ(inside->channels.front().count, image == &*floatImage ? 2u : 3u);
        EXPECT_GT(inside->channels.front().minimum, 0.0);
        EXPECT_LT(inside->channels.front().maximum, 1.0);
    }
    const an::Result<an::ui::ImageStatisticsReport> signedInside = report(*signedImage, true);
    ASSERT_TRUE(signedInside.hasValue());
    EXPECT_DOUBLE_EQ(signedInside->channels.front().minimum, 1.0 / 255.0);
    EXPECT_DOUBLE_EQ(signedInside->channels.front().median, 128.0 / 255.0);
}

// The image's own mask applies under the clipped samples' interval, whether one plane is shared by every channel or
// each channel has its own; the zero a signed image states as no data does too, and a stated range is intersected with
// it.
TEST_F(StatisticsReportTest, TheImagesOwnValidityAppliesWithTheClippedInterval)
{
    const auto value = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return static_cast<std::uint16_t>(x == 0u ? 0u : 1000u * (x + y + channel));
    };
    const an::ValidityDescriptor masked = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});

    // One plane: the first row is left out of every channel.
    const std::optional<an::consumer::ImageAsset> shared =
        assetWithValidity<std::uint16_t>(*m_session, 6u, 3u, Channels::Rgb, value, masked, 1u,
                                         [](std::uint32_t, std::uint32_t, std::uint32_t y) { return y != 0u; });
    ASSERT_TRUE(shared.has_value());
    const an::Result<an::ui::ImageStatisticsReport> sharedAll = report(*shared, false);
    const an::Result<an::ui::ImageStatisticsReport> sharedInside = report(*shared, true);
    ASSERT_TRUE(sharedAll.hasValue()) << sharedAll.status().message();
    ASSERT_TRUE(sharedInside.hasValue()) << sharedInside.status().message();
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(sharedAll->channels[channel].count, 12u);
        EXPECT_EQ(sharedInside->channels[channel].count, 10u);
    }

    // One plane per channel: channel c leaves out column c + 1 as well as the clipped first column.
    const std::optional<an::consumer::ImageAsset> perChannel = assetWithValidity<std::uint16_t>(
        *m_session, 6u, 3u, Channels::Rgb, value, masked, 3u,
        [](std::uint32_t plane, std::uint32_t x, std::uint32_t) { return x != plane + 1u; });
    ASSERT_TRUE(perChannel.has_value());
    const an::Result<an::ui::ImageStatisticsReport> perChannelInside = report(*perChannel, true);
    ASSERT_TRUE(perChannelInside.hasValue()) << perChannelInside.status().message();
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(perChannelInside->channels[channel].count, 12u) << "channel " << channel;
    }

    // A signed image whose zero is no data: zero and both ends are left out.
    const std::optional<an::consumer::ImageAsset> zeroIsNoData = assetWithValidity<std::int16_t>(
        *m_session, 5u, 1u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            constexpr std::int16_t kValues[] = {-32768, -5, 0, 5, 32767};
            return kValues[x];
        },
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData}));
    ASSERT_TRUE(zeroIsNoData.has_value());
    const an::Result<an::ui::ImageStatisticsReport> zeroInside = report(*zeroIsNoData, true);
    ASSERT_TRUE(zeroInside.hasValue()) << zeroInside.status().message();
    EXPECT_EQ(zeroInside->channels.front().count, 2u);

    // A stated range is intersected with the interval; one that lies outside it admits nothing.
    const std::optional<an::consumer::ImageAsset> ranged = assetWithValidity<std::uint8_t>(
        *m_session, 6u, 1u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<std::uint8_t>(x * 51u); },
        *an::ValidityDescriptor::create(
            {.rule = an::ValidityRule::DomainRange, .lowestValid = 0.0, .highestValid = 160.0}));
    ASSERT_TRUE(ranged.has_value());
    const an::Result<an::ui::ImageStatisticsReport> rangedInside = report(*ranged, true);
    ASSERT_TRUE(rangedInside.hasValue()) << rangedInside.status().message();
    EXPECT_EQ(rangedInside->channels.front().count, 3u);
}

// A channel that admits nothing has a count of zero and figures of zero, rather than a refusal.
TEST_F(StatisticsReportTest, AChannelThatAdmitsNothingHasACountOfZero)
{
    const std::optional<an::consumer::ImageAsset> ends = an::tests::statistics::makeAsset<std::uint8_t>(
        *m_session, 4u, 2u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x % 2u == 0u ? 0u : 255u);
        });
    ASSERT_TRUE(ends.has_value());
    const an::Result<an::ui::ImageStatisticsReport> inside = report(*ends, true);
    ASSERT_TRUE(inside.hasValue()) << inside.status().message();
    EXPECT_EQ(inside->channels.front().count, 0u);
    EXPECT_EQ(inside->channels.front().pixels, 8u);
    EXPECT_EQ(inside->channels.front().mean, 0.0);
}

// The background noise is taken from each row's first differences: rows alternating between two levels differ by the
// gap between them every step, whose spread over the square root of two is the noise; a ramp along each row has none.
TEST_F(StatisticsReportTest, TheBackgroundNoiseIsTakenFromEachRowsFirstDifferences)
{
    // Seventeen columns make sixteen differences, as many up as down, so their mean is zero and their spread the gap.
    const std::optional<an::consumer::ImageAsset> alternating = an::tests::statistics::makeAsset<std::uint8_t>(
        *m_session, 17u, 4u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x % 2u == 0u ? 50u : 150u);
        });
    const std::optional<an::consumer::ImageAsset> ramp = an::tests::statistics::makeAsset<std::uint8_t>(
        *m_session, 16u, 4u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<std::uint8_t>(10u + 10u * x); });
    ASSERT_TRUE(alternating.has_value() && ramp.has_value());
    const an::Result<an::ui::ImageStatisticsReport> noisy = report(*alternating, false);
    const an::Result<an::ui::ImageStatisticsReport> smooth = report(*ramp, false);
    ASSERT_TRUE(noisy.hasValue() && smooth.hasValue());
    EXPECT_TRUE(agrees("background noise", noisy->channels.front().backgroundNoise, (100.0 / 255.0) / std::sqrt(2.0)));
    EXPECT_NEAR(smooth->channels.front().backgroundNoise, 0.0, 1e-12);
}

// A scale multiplies a location and a spread by its full scale, and a variance by its square.
TEST(StatisticsScaleTest, AScaleMultipliesASpreadByItsFullScaleAndAVarianceByItsSquare)
{
    an::ui::ImageStatisticsChannel channel;
    channel.mean = 0.5;
    channel.standardDeviation = 0.1;
    channel.median = 0.25;
    channel.minimum = 0.0;
    channel.maximum = 1.0;
    channel.averageDeviation = 0.05;
    channel.biweightMidvariance = 0.02;
    channel.variance = 0.01;
    channel.medianAbsoluteDeviation = 0.04;
    channel.backgroundNoise = 0.002;

    const auto normalized = an::ui::scaledStatisticsFigures(channel, an::ui::StatisticsScale::Normalized);
    EXPECT_EQ(normalized, (std::array<double, an::ui::kStatisticsFigureRows>{0.5, 0.1, 0.25, 0.0, 1.0, 0.1, 0.05, 0.02,
                                                                             0.01, 0.04, 0.002}));
    const auto bytes = an::ui::scaledStatisticsFigures(channel, an::ui::StatisticsScale::Bits8);
    EXPECT_DOUBLE_EQ(bytes[0], 127.5);
    EXPECT_DOUBLE_EQ(bytes[1], 25.5);
    EXPECT_DOUBLE_EQ(bytes[4], 255.0);
    EXPECT_DOUBLE_EQ(bytes[7], 0.02 * 255.0 * 255.0);
    EXPECT_DOUBLE_EQ(bytes[8], 0.01 * 255.0 * 255.0);
    EXPECT_DOUBLE_EQ(bytes[10], 0.002 * 255.0);
    const auto words = an::ui::scaledStatisticsFigures(channel, an::ui::StatisticsScale::Bits16);
    EXPECT_DOUBLE_EQ(words[8], 0.01 * 65535.0 * 65535.0);
    const auto longs = an::ui::scaledStatisticsFigures(channel, an::ui::StatisticsScale::Bits32);
    EXPECT_DOUBLE_EQ(longs[2], 0.25 * 4294967295.0);
}

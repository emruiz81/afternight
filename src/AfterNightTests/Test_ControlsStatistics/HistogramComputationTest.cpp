// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The counts a histogram widget draws, held to the rule the product's histograms count by, stated here rather than
/// taken from the engine: a sample is read as a fraction of its representation's range - an unsigned integer over its
/// largest code, a signed one from its smallest value to its largest, a float as it is - counted as clipped at or
/// beyond either end, held into the unit interval and placed in bin floor(x * (n - 1)); a sample that is not a number
/// enters no bin but is still one of the samples the clipped shares are of, and only the samples the image's validity
/// admits are counted.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "widgets/HistogramComputation.h"
#include "widgets/HistogramDisplayData.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

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
using an::tests::statistics::fixtureAsset;
using an::tests::statistics::FixtureAsset;

/// One channel's counts by the stated rule, and the samples within a rounding of the boundary below each bin, which
/// either neighbour may hold: the engine places a sample by its own interval arithmetic.
struct ExpectedChannel
{
    an::ui::HistogramDisplayChannel counts;
    std::vector<std::uint64_t> atBoundary;
};

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

[[nodiscard]] std::vector<ExpectedChannel> expectedCounts(const fixtures::FixtureSamples& samples,
                                                          const std::size_t binCount)
{
    std::vector<ExpectedChannel> channels;
    for (std::uint32_t channel = 0u; channel < samples.channelCount(); ++channel)
    {
        ExpectedChannel expected;
        expected.counts.bins.assign(binCount, 0u);
        expected.atBoundary.assign(binCount + 1u, 0u);
        for (std::uint32_t y = 0u; y < samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples.width(); ++x)
            {
                const double value = fractionOf(samples.value(x, y, channel), samples.sampleType());
                ++expected.counts.samples;
                if (std::isnan(value))
                {
                    ++expected.counts.notANumber;
                    continue;
                }
                expected.counts.clippedLow += value <= 0.0 ? 1u : 0u;
                expected.counts.clippedHigh += value >= 1.0 ? 1u : 0u;
                const double position = std::clamp(value, 0.0, 1.0) * static_cast<double>(binCount - 1u);
                ++expected.counts.bins[static_cast<std::size_t>(position)];
                const double boundary = std::round(position);
                if (boundary >= 1.0 && boundary <= static_cast<double>(binCount - 1u) &&
                    std::fabs(position - boundary) <= 1e-9 * boundary)
                {
                    ++expected.atBoundary[static_cast<std::size_t>(boundary)];
                }
            }
        }
        channels.push_back(std::move(expected));
    }
    return channels;
}

/// Where a channel's bins differ from the rule other than by a boundary sample, as text; empty when they agree.
[[nodiscard]] std::string unexplainedDifferences(const std::vector<std::uint64_t>& counted,
                                                 const ExpectedChannel& expected)
{
    std::string text;
    std::uint64_t countedTotal = 0u;
    std::uint64_t expectedTotal = 0u;
    for (std::size_t bin = 0u; bin < counted.size(); ++bin)
    {
        countedTotal += counted[bin];
        expectedTotal += expected.counts.bins[bin];
        const auto difference =
            static_cast<std::int64_t>(counted[bin]) - static_cast<std::int64_t>(expected.counts.bins[bin]);
        const auto allowed = static_cast<std::int64_t>(expected.atBoundary[bin] + expected.atBoundary[bin + 1u]);
        if (difference > allowed || -difference > allowed)
        {
            text += " bin " + std::to_string(bin) + ": " + std::to_string(counted[bin]) + " against " +
                    std::to_string(expected.counts.bins[bin]) + ";";
        }
    }
    if (countedTotal != expectedTotal)
    {
        text += " totals " + std::to_string(countedTotal) + " against " + std::to_string(expectedTotal);
    }
    return text;
}

/// The cases' session.
class HistogramComputationTest : public ::testing::Test
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

    [[nodiscard]] an::Result<an::ui::HistogramDisplayData> count(const an::consumer::ImageAsset& asset,
                                                                 const an::ui::HistogramResolution resolution)
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
        return an::ui::computeHistogramDisplayData(scope->execution(), *lease, resolution);
    }

    std::optional<an::consumer::EngineSession> m_session;
};

} // namespace

// Every standard fixture - the eight representations, grey and colour, mosaics among them - counts as the rule says,
// at a power of two, a round number of levels and the full sixteen bits.
TEST_F(HistogramComputationTest, EveryStandardFixtureCountsByTheStatedRule)
{
    std::size_t compared = 0u;
    std::size_t signedCompared = 0u;
    std::uint64_t boundarySamples = 0u;
    for (const fixtures::FixtureImage& image : fixtures::standardFixtures())
    {
        const std::optional<FixtureAsset> made = fixtureAsset(*m_session, image);
        ASSERT_TRUE(made.has_value()) << image.name;
        for (const an::ui::HistogramResolution resolution :
             {an::ui::HistogramResolution::Bits8, an::ui::HistogramResolution::Levels100,
              an::ui::HistogramResolution::Bits16})
        {
            const std::size_t binCount = an::ui::histogramBinCount(resolution);
            const an::Result<an::ui::HistogramDisplayData> counted = count(*made->asset, resolution);
            ASSERT_TRUE(counted.hasValue()) << image.name << ": " << counted.status().message();
            const std::vector<ExpectedChannel> expected = expectedCounts(made->samples, binCount);
            ASSERT_EQ(counted->channels.size(), expected.size()) << image.name;
            for (std::size_t channel = 0u; channel < expected.size(); ++channel)
            {
                const an::ui::HistogramDisplayChannel& got = counted->channels[channel];
                const an::ui::HistogramDisplayChannel& want = expected[channel].counts;
                ASSERT_EQ(got.bins.size(), binCount) << image.name;
                EXPECT_EQ(unexplainedDifferences(got.bins, expected[channel]), "")
                    << image.name << " channel " << channel << " at " << binCount << " bins";
                boundarySamples += std::accumulate(expected[channel].atBoundary.begin(),
                                                   expected[channel].atBoundary.end(), std::uint64_t{0});
                EXPECT_EQ(got.clippedLow, want.clippedLow) << image.name << " channel " << channel;
                EXPECT_EQ(got.clippedHigh, want.clippedHigh) << image.name << " channel " << channel;
                EXPECT_EQ(got.notANumber, want.notANumber) << image.name << " channel " << channel;
                EXPECT_EQ(got.samples, want.samples) << image.name << " channel " << channel;
            }
            ++compared;
            if (!fixtures::isFloating(image.sampleType) && fixtures::lowestOf(image.sampleType) < 0.0)
            {
                ++signedCompared;
            }
        }
    }
    EXPECT_GE(compared, 15u);
    EXPECT_GT(signedCompared, 0u) << "no signed fixture was counted";
    RecordProperty("boundarySamples", std::to_string(boundarySamples));
}

// A signed image's smallest value is the left edge of its histogram and its largest the right, with zero in the
// middle: the image is read from its smallest value, never refused and never relabelled.
TEST_F(HistogramComputationTest, ASignedImageIsReadFromItsSmallestValue)
{
    const std::optional<an::consumer::ImageAsset> asset = an::tests::statistics::makeAsset<std::int16_t>(
        *m_session, 4u, 1u, an::tests::statistics::Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            constexpr std::int16_t kValues[] = {std::numeric_limits<std::int16_t>::min(), -1, 0,
                                                std::numeric_limits<std::int16_t>::max()};
            return kValues[x];
        });
    ASSERT_TRUE(asset.has_value());
    const an::Result<an::ui::HistogramDisplayData> counted = count(*asset, an::ui::HistogramResolution::Bits8);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    const an::ui::HistogramDisplayChannel& channel = counted->channels.front();
    EXPECT_EQ(channel.bins.front(), 1u);
    EXPECT_EQ(channel.bins.back(), 1u);
    // -1 and 0 sit just either side of the middle of the range: 32767 / 65535 and 32768 / 65535 of it.
    EXPECT_EQ(channel.bins[127], 2u);
    EXPECT_EQ(channel.clippedLow, 1u);
    EXPECT_EQ(channel.clippedHigh, 1u);
    EXPECT_EQ(channel.samples, 4u);
}

// A float outside the unit interval is counted in the end bin it is nearest and as clipped there; a not-a-number
// enters no bin and is counted beside the bins.
TEST_F(HistogramComputationTest, AFloatOutsideTheIntervalClipsAndANotANumberEntersNoBin)
{
    const std::optional<an::consumer::ImageAsset> asset = an::tests::statistics::makeAsset<float>(
        *m_session, 6u, 1u, an::tests::statistics::Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            const float values[] = {-0.5f, 0.0f, 0.5f, 1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN()};
            return values[x];
        });
    ASSERT_TRUE(asset.has_value());
    const an::Result<an::ui::HistogramDisplayData> counted = count(*asset, an::ui::HistogramResolution::Levels100);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    const an::ui::HistogramDisplayChannel& channel = counted->channels.front();
    EXPECT_EQ(channel.bins.front(), 2u);
    EXPECT_EQ(channel.bins[49], 1u);
    EXPECT_EQ(channel.bins.back(), 2u);
    EXPECT_EQ(channel.clippedLow, 2u);
    EXPECT_EQ(channel.clippedHigh, 2u);
    EXPECT_EQ(channel.notANumber, 1u);
    EXPECT_EQ(std::accumulate(channel.bins.begin(), channel.bins.end(), std::uint64_t{0}), 5u);
}

// The samples the image's validity leaves out are not counted, and the clipped share is of the samples it admits.
TEST_F(HistogramComputationTest, OnlyTheSamplesTheValidityAdmitsAreCounted)
{
    // Left out: every sample of the first row, whose codes are full scale. What stays: seven ordinary codes and a zero.
    const std::optional<an::consumer::ImageAsset> asset = an::tests::statistics::assetWithValidity<std::uint16_t>(
        *m_session, 8u, 2u, an::tests::statistics::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(x == 0u ? 0u : (y == 0u ? 65535u : 1000u * x));
        },
        *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}), 1u,
        [](std::uint32_t, std::uint32_t, std::uint32_t y) { return y != 0u; });
    ASSERT_TRUE(asset.has_value());
    const an::Result<an::ui::HistogramDisplayData> counted = count(*asset, an::ui::HistogramResolution::Bits8);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    const an::ui::HistogramDisplayChannel& channel = counted->channels.front();
    EXPECT_EQ(channel.samples, 8u);
    EXPECT_EQ(channel.clippedLow, 1u);
    EXPECT_EQ(channel.clippedHigh, 0u);
    EXPECT_EQ(std::accumulate(channel.bins.begin(), channel.bins.end(), std::uint64_t{0}), 8u);
}

// A channel whose validity admits nothing is drawn as nothing rather than refused.
TEST_F(HistogramComputationTest, AChannelThatAdmitsNothingHasEmptyCounts)
{
    const std::optional<an::consumer::ImageAsset> asset = an::tests::statistics::makeAsset<float>(
        *m_session, 4u, 4u, an::tests::statistics::Channels::Grey,
        [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::numeric_limits<float>::quiet_NaN(); });
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::OperationScope> scope = fixtures::engine::admitFixtureOperation(*m_session);
    ASSERT_TRUE(scope.hasValue());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    const an::Result<an::ui::HistogramDisplayData> counted = lease->visit([&](const auto& typed) {
        an::StatisticsSelection selection;
        selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        const auto view = typed.view();
        const an::ImageLayout layout = view->layout();
        return an::ui::computeHistogramDisplayData(scope->execution(), an::SampleType::Float32, view->data(), layout,
                                                   an::ui::HistogramResolution::Bits6, selection);
    });
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    ASSERT_EQ(counted->channels.size(), 1u);
    const an::ui::HistogramDisplayChannel& channel = counted->channels.front();
    EXPECT_EQ(channel.bins.size(), 64u);
    EXPECT_EQ(channel.maxValue(), 0u);
    EXPECT_EQ(channel.samples, 0u);
    EXPECT_TRUE(counted->isValid());
}

// Every resolution names the bin count its label says.
TEST(HistogramResolutionTest, EveryResolutionNamesItsBinCount)
{
    using an::ui::HistogramResolution;
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits6), 64u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits7), 128u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits8), 256u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits9), 512u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits10), 1024u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits12), 4096u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits14), 16384u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Bits16), 65536u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Levels100), 100u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Levels1000), 1000u);
    EXPECT_EQ(an::ui::histogramBinCount(HistogramResolution::Levels10000), 10000u);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Histograms made from counts rather than from the image they describe. The histogram of an image mapped channel by
/// channel, made from its codes: every code of an eight- or sixteen-bit image counted in a bin of its own over the
/// samples its validity admits, and those counts remapped through what a map makes of every code into exactly the
/// counts of the mapped image itself - every bin, both clipped ends and the samples admitted - at every resolution, in
/// each of the four eight- and sixteen-bit representations, with and without a mask. And the histogram of an image a
/// part of which was replaced, from the whole image's counts, the part's and the replacement's.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "widgets/HistogramComputation.h"
#include "widgets/HistogramDisplayData.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using an::tests::statistics::assetWithValidity;
using an::tests::viewer::Channels;
using an::tests::viewer::makeAsset;

/// The cases' session and the operation each count runs under.
class HistogramRemapTest : public ::testing::Test
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

public:
    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return *m_session;
    }

    /// The display counts of an asset, counted directly.
    [[nodiscard]] an::Result<an::ui::HistogramDisplayData> count(const an::consumer::ImageAsset& asset,
                                                                 an::ui::HistogramResolution resolution) const
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

    /// The codes of an asset, counted.
    [[nodiscard]] an::Result<an::ui::CodeHistogram> codes(const an::consumer::ImageAsset& asset) const
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
        return an::ui::countCodes(scope->execution(), *lease);
    }

    /// The display counts of codes remapped through a row of them mapped.
    [[nodiscard]] an::Result<an::ui::HistogramDisplayData> remap(const an::ui::CodeHistogram& counted,
                                                                 const an::consumer::ImageAsset& mapped,
                                                                 an::ui::HistogramResolution resolution) const
    {
        an::Result<an::consumer::OperationScope> scope = fixtures::engine::admitFixtureOperation(*m_session);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::consumer::AssetReadLease> lease = mapped.read();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        return an::ui::remapHistogramDisplayData(scope->engine(), scope->execution(), counted, *lease, resolution);
    }

private:
    std::optional<an::consumer::EngineSession> m_session;
};

/// A sample of `SampleT` at a fraction of its range, from its smallest value to its largest.
template <typename SampleT> [[nodiscard]] SampleT atFraction(double fraction)
{
    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::lowest)());
    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
    return static_cast<SampleT>(std::lround(lowest + std::clamp(fraction, 0.0, 1.0) * (highest - lowest)));
}

/// A map of each channel's codes alone - a different gain and offset per channel, clipping at both ends - in the
/// representation's own codes.
template <typename SampleT> [[nodiscard]] SampleT mapped(SampleT code, std::uint32_t channel)
{
    const double lowest = static_cast<double>((std::numeric_limits<SampleT>::lowest)());
    const double highest = static_cast<double>((std::numeric_limits<SampleT>::max)());
    const double fraction = (static_cast<double>(code) - lowest) / (highest - lowest);
    return atFraction<SampleT>(fraction * (1.0 + 0.3 * channel) - 0.05 * (channel + 1u));
}

/// A colour image of `SampleT` holding both extremes of its range and every level between in some channel.
template <typename SampleT> [[nodiscard]] SampleT sourceValue(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    if (x == 0u && y == 0u)
    {
        return (std::numeric_limits<SampleT>::lowest)();
    }
    if (x == 1u && y == 0u)
    {
        return (std::numeric_limits<SampleT>::max)();
    }
    return atFraction<SampleT>(static_cast<double>((x * 37u + y * 101u + channel * 59u) % 997u) / 996.0);
}

/// Holds two display counts equal, channel by channel.
void expectSameCounts(const an::ui::HistogramDisplayData& remapped, const an::ui::HistogramDisplayData& counted,
                      const char* what)
{
    ASSERT_EQ(remapped.channels.size(), counted.channels.size()) << what;
    for (std::size_t channel = 0u; channel < counted.channels.size(); ++channel)
    {
        const an::ui::HistogramDisplayChannel& got = remapped.channels[channel];
        const an::ui::HistogramDisplayChannel& want = counted.channels[channel];
        EXPECT_EQ(got.bins, want.bins) << what << ", channel " << channel;
        EXPECT_EQ(got.clippedLow, want.clippedLow) << what << ", channel " << channel;
        EXPECT_EQ(got.clippedHigh, want.clippedHigh) << what << ", channel " << channel;
        EXPECT_EQ(got.notANumber, want.notANumber) << what << ", channel " << channel;
        EXPECT_EQ(got.samples, want.samples) << what << ", channel " << channel;
    }
}

/// Remaps an image of `SampleT`'s codes through `mapped` and holds the counts to those of the image mapped, at every
/// resolution, with or without a mask over a third of its pixels.
template <typename SampleT> void remapsAsTheMappedImageCounts(const HistogramRemapTest& test, bool masked)
{
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 64u;
    const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)> source =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return sourceValue<SampleT>(x, y, c); };
    const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)> image =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return mapped<SampleT>(sourceValue<SampleT>(x, y, c), c);
        };
    const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)> valid =
        [](std::uint32_t, std::uint32_t x, std::uint32_t y) { return (x + y) % 3u != 0u; };
    const std::optional<an::ValidityDescriptor> rule =
        an::ValidityDescriptor::create({.rule = masked ? an::ValidityRule::ExplicitMask : an::ValidityRule::AllValid});
    ASSERT_TRUE(rule.has_value());
    std::optional<an::consumer::ImageAsset> sourceAsset = assetWithValidity<SampleT>(
        test.session(), kWidth, kHeight, Channels::Rgb, source, *rule, masked ? 1u : 0u, valid);
    std::optional<an::consumer::ImageAsset> mappedAsset = assetWithValidity<SampleT>(
        test.session(), kWidth, kHeight, Channels::Rgb, image, *rule, masked ? 1u : 0u, valid);
    ASSERT_TRUE(sourceAsset.has_value() && mappedAsset.has_value());

    const std::uint32_t codes = an::ui::codeCountOf(an::kSampleTypeOf<SampleT>);
    const auto lowest = static_cast<std::int64_t>((std::numeric_limits<SampleT>::lowest)());
    std::optional<an::consumer::ImageAsset> row = makeAsset<SampleT>(
        test.session(), codes, 1u, Channels::Rgb, [lowest](std::uint32_t x, std::uint32_t, std::uint32_t c) {
            return mapped<SampleT>(static_cast<SampleT>(lowest + static_cast<std::int64_t>(x)), c);
        });
    ASSERT_TRUE(row.has_value());

    an::Result<an::ui::CodeHistogram> counted = test.codes(*sourceAsset);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    for (const an::ui::HistogramResolution resolution :
         {an::ui::HistogramResolution::Bits6, an::ui::HistogramResolution::Bits8, an::ui::HistogramResolution::Bits10,
          an::ui::HistogramResolution::Bits16, an::ui::HistogramResolution::Levels100,
          an::ui::HistogramResolution::Levels1000})
    {
        an::Result<an::ui::HistogramDisplayData> remapped = test.remap(*counted, *row, resolution);
        an::Result<an::ui::HistogramDisplayData> direct = test.count(*mappedAsset, resolution);
        ASSERT_TRUE(remapped.hasValue()) << remapped.status().message();
        ASSERT_TRUE(direct.hasValue()) << direct.status().message();
        expectSameCounts(*remapped, *direct, masked ? "masked" : "every sample valid");
    }
}

} // namespace

// Every code is counted in a bin of its own, over the samples the image's validity admits.
TEST_F(HistogramRemapTest, EveryCodeIsCountedInABinOfItsOwnOverTheSamplesTheValidityAdmits)
{
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<std::uint16_t>((x * 311u + y * 577u + c * 7919u) % 4096u);
        };
    const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)> valid =
        [](std::uint32_t, std::uint32_t x, std::uint32_t y) { return (x * 7u + y) % 5u != 0u; };
    const std::optional<an::ValidityDescriptor> rule =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(rule.has_value());
    std::optional<an::consumer::ImageAsset> asset =
        assetWithValidity<std::uint16_t>(session(), kWidth, kHeight, Channels::Rgb, value, *rule, 1u, valid);
    ASSERT_TRUE(asset.has_value());

    an::Result<an::ui::CodeHistogram> counted = codes(*asset);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->sampleType, an::SampleType::UInt16);
    ASSERT_EQ(counted->channels.size(), 3u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::vector<std::uint64_t> expected(65536u, 0u);
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                if (valid(0u, x, y))
                {
                    ++expected[value(x, y, channel)];
                }
            }
        }
        EXPECT_EQ(counted->channels[channel], expected) << "channel " << channel;
    }
}

// The codes remapped through what a map makes of every code are the counts of the mapped image itself, exactly, in each
// eight- and sixteen-bit representation at every resolution: the bins, the samples clipped at either end and the
// samples admitted, with every sample valid and under a mask.
TEST_F(HistogramRemapTest, RemappedCodesAreExactlyTheCountsOfTheMappedImage)
{
    for (const bool masked : {false, true})
    {
        remapsAsTheMappedImageCounts<std::uint8_t>(*this, masked);
        remapsAsTheMappedImageCounts<std::int8_t>(*this, masked);
        remapsAsTheMappedImageCounts<std::uint16_t>(*this, masked);
        remapsAsTheMappedImageCounts<std::int16_t>(*this, masked);
    }
}

// A representation with no codes to count, and a mapped row that is not one row of every code in the codes' own
// representation and channels, are refused.
TEST_F(HistogramRemapTest, NoCodesAreCountedOfAFloatingImageAndAMappedRowMustBeEveryCodeInTheirRepresentation)
{
    std::optional<an::consumer::ImageAsset> floating = makeAsset<float>(
        session(), 8u, 4u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(floating.has_value());
    EXPECT_EQ(codes(*floating).status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::ui::codeCountOf(an::SampleType::Float32), 0u);
    EXPECT_EQ(an::ui::codeCountOf(an::SampleType::UInt32), 0u);
    EXPECT_EQ(an::ui::codeCountOf(an::SampleType::Int8), 256u);
    EXPECT_EQ(an::ui::codeCountOf(an::SampleType::Int16), 65536u);

    std::optional<an::consumer::ImageAsset> grey =
        makeAsset<std::uint8_t>(session(), 8u, 4u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x);
        });
    ASSERT_TRUE(grey.has_value());
    an::Result<an::ui::CodeHistogram> counted = codes(*grey);
    ASSERT_TRUE(counted.hasValue());
    std::optional<an::consumer::ImageAsset> shortRow =
        makeAsset<std::uint8_t>(session(), 128u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x);
        });
    std::optional<an::consumer::ImageAsset> otherKind = makeAsset<std::uint16_t>(
        session(), 256u, 1u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<std::uint16_t>(x); });
    std::optional<an::consumer::ImageAsset> colourRow =
        makeAsset<std::uint8_t>(session(), 256u, 1u, Channels::Rgb, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x);
        });
    ASSERT_TRUE(shortRow.has_value() && otherKind.has_value() && colourRow.has_value());
    EXPECT_EQ(remap(*counted, *shortRow, an::ui::HistogramResolution::Bits8).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(remap(*counted, *otherKind, an::ui::HistogramResolution::Bits8).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(remap(*counted, *colourRow, an::ui::HistogramResolution::Bits8).status().code(),
              an::ErrorCode::IncompatibleArguments);
}

namespace
{

/// The part of an image a replacement is pasted over, in its pixels.
constexpr std::uint32_t kPartX = 7u;
constexpr std::uint32_t kPartY = 5u;
constexpr std::uint32_t kPartWidth = 19u;
constexpr std::uint32_t kPartHeight = 11u;

/// Whether a pixel lies in that part.
[[nodiscard]] bool inPart(std::uint32_t x, std::uint32_t y)
{
    return x >= kPartX && x < kPartX + kPartWidth && y >= kPartY && y < kPartY + kPartHeight;
}

/// Counts an image with that part replaced, and derives the same counts from the whole image's, the part's and the
/// replacement's, at every resolution: they are the same.
template <typename SampleT>
void countsAsReplaced(const HistogramRemapTest& test,
                      const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& whole,
                      const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& replacement,
                      const char* what)
{
    constexpr std::uint32_t kWidth = 40u;
    constexpr std::uint32_t kHeight = 24u;
    std::optional<an::consumer::ImageAsset> wholeAsset =
        makeAsset<SampleT>(test.session(), kWidth, kHeight, Channels::Rgb, whole);
    std::optional<an::consumer::ImageAsset> partAsset = makeAsset<SampleT>(
        test.session(), kPartWidth, kPartHeight, Channels::Rgb,
        [&whole](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return whole(kPartX + x, kPartY + y, c); });
    std::optional<an::consumer::ImageAsset> replacementAsset =
        makeAsset<SampleT>(test.session(), kPartWidth, kPartHeight, Channels::Rgb, replacement);
    std::optional<an::consumer::ImageAsset> replacedAsset = makeAsset<SampleT>(
        test.session(), kWidth, kHeight, Channels::Rgb, [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return inPart(x, y) ? replacement(x - kPartX, y - kPartY, c) : whole(x, y, c);
        });
    ASSERT_TRUE(wholeAsset.has_value() && partAsset.has_value() && replacementAsset.has_value() &&
                replacedAsset.has_value());
    for (const an::ui::HistogramResolution resolution :
         {an::ui::HistogramResolution::Bits6, an::ui::HistogramResolution::Bits8, an::ui::HistogramResolution::Bits12,
          an::ui::HistogramResolution::Bits16, an::ui::HistogramResolution::Levels100})
    {
        an::Result<an::ui::HistogramDisplayData> wholeCounts = test.count(*wholeAsset, resolution);
        an::Result<an::ui::HistogramDisplayData> partCounts = test.count(*partAsset, resolution);
        an::Result<an::ui::HistogramDisplayData> replacementCounts = test.count(*replacementAsset, resolution);
        an::Result<an::ui::HistogramDisplayData> direct = test.count(*replacedAsset, resolution);
        ASSERT_TRUE(wholeCounts.hasValue() && partCounts.hasValue() && replacementCounts.hasValue() &&
                    direct.hasValue());
        an::Result<an::ui::HistogramDisplayData> derived =
            an::ui::replacedPartHistogram(*wholeCounts, *partCounts, *replacementCounts);
        ASSERT_TRUE(derived.hasValue()) << derived.status().message();
        expectSameCounts(*derived, *direct, what);
    }
}

} // namespace

// The counts of an image a part of which was replaced are the whole image's, less the part's, plus the replacement's -
// every bin, both clipped ends, the samples that are not a number and those counted - at every resolution: sixteen-bit
// codes that reach both ends of their range, and floating samples beyond the unit interval and not a number.
TEST_F(HistogramRemapTest, AnImageWithAPartReplacedCountsAsTheWholeLessThePartPlusTheReplacement)
{
    countsAsReplaced<std::uint16_t>(
        *this, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return sourceValue<std::uint16_t>(x, y, c); },
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return mapped<std::uint16_t>(sourceValue<std::uint16_t>(x + 3u, y + 1u, c), c);
        },
        "sixteen-bit codes");
    const auto floating = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        if ((x + 3u * y + c) % 29u == 0u)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return -0.2f + 1.4f * static_cast<float>((x * 13u + y * 7u + c * 31u) % 101u) / 100.0f;
    };
    countsAsReplaced<float>(
        *this, floating,
        [&floating](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return 0.6f * floating(y, x, c) + 0.3f; },
        "floating samples");
}

// Counts of other channels or other resolutions, and a part with more samples in a bin than the whole image, are not
// combined.
TEST_F(HistogramRemapTest, CountsOfOtherChannelsOrResolutionsOrAPartLargerThanTheWholeAreNotCombined)
{
    const auto constant = [](std::uint32_t, std::uint32_t, std::uint32_t) { return static_cast<std::uint8_t>(90u); };
    std::optional<an::consumer::ImageAsset> grey = makeAsset<std::uint8_t>(session(), 8u, 4u, Channels::Grey, constant);
    std::optional<an::consumer::ImageAsset> colour =
        makeAsset<std::uint8_t>(session(), 8u, 4u, Channels::Rgb, constant);
    std::optional<an::consumer::ImageAsset> larger =
        makeAsset<std::uint8_t>(session(), 16u, 4u, Channels::Grey, constant);
    ASSERT_TRUE(grey.has_value() && colour.has_value() && larger.has_value());
    an::Result<an::ui::HistogramDisplayData> greyCounts = count(*grey, an::ui::HistogramResolution::Bits8);
    an::Result<an::ui::HistogramDisplayData> colourCounts = count(*colour, an::ui::HistogramResolution::Bits8);
    an::Result<an::ui::HistogramDisplayData> finer = count(*grey, an::ui::HistogramResolution::Bits10);
    an::Result<an::ui::HistogramDisplayData> largerCounts = count(*larger, an::ui::HistogramResolution::Bits8);
    ASSERT_TRUE(greyCounts.hasValue() && colourCounts.hasValue() && finer.hasValue() && largerCounts.hasValue());
    EXPECT_EQ(an::ui::replacedPartHistogram(*greyCounts, *colourCounts, *greyCounts).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::ui::replacedPartHistogram(*greyCounts, *greyCounts, *finer).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::ui::replacedPartHistogram(*greyCounts, *largerCounts, *greyCounts).status().code(),
              an::ErrorCode::IncompatibleArguments);
    an::Result<an::ui::HistogramDisplayData> itself =
        an::ui::replacedPartHistogram(*largerCounts, *greyCounts, *greyCounts);
    ASSERT_TRUE(itself.hasValue());
    expectSameCounts(*itself, *largerCounts, "a part replaced by itself");
}

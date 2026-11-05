// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of the combination processes, reproduced through the process's own window: the record's scene
/// opened in a view with the images the process reads beside it, the window opened as the launcher opens it, its
/// widgets set to the record's mode as a person sets them, Apply, and what the result holds - the target's document, or
/// the new view the combination lands in - held to the record by the record's own bound, or by what an accepted
/// difference states, every sample beyond its bound one the difference explains.

#include "Test_ProcessesCombinations/CombinationsTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <tuple>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::combinations;

/// Holds a result to a record beyond the record's own bound; empty when it holds, the reason otherwise.
using Judgement = std::function<std::string(const fixtures::FixtureSamples& input, const fixtures::Expectation& record,
                                            const fixtures::FixtureSamples& result)>;

/// A difference from the record that a case accepts: the records it applies to, how far the result may be from the
/// record, and why.
struct AcceptedDifference
{
    /// The start of the ids of the records it applies to.
    std::string records;

    /// The largest absolute difference allowed against the record, in the record's units; ignored when a judgement
    /// holds the result instead.
    double bound = 0.0;

    /// Why.
    std::string reason;

    /// What holds the result when no bound can, or nothing.
    Judgement judge;
};

/// A result that is the record's picture stretched otherwise: every sample in the unit interval, and the result's
/// samples ordered as the record's are - a brighter sample in the record is no darker in the result, within a slack -
/// over every channel.
Judgement sameOrderWithin(double slack)
{
    return [slack](const fixtures::FixtureSamples&, const fixtures::Expectation& record,
                   const fixtures::FixtureSamples& result) -> std::string {
        const fixtures::FixtureSamples& expected = record.result;
        const std::uint32_t width = expected.width();
        const std::uint32_t height = expected.height();
        std::uint64_t disordered = 0u;
        std::uint64_t compared = 0u;
        for (std::uint32_t channel = 0u; channel < expected.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x + 1u < width; ++x)
                {
                    const double a = result.value(x, y, channel);
                    const double b = result.value(x + 1u, y, channel);
                    if (!(a >= 0.0 && a <= 1.0) || !(b >= 0.0 && b <= 1.0))
                    {
                        return "a sample at column " + std::to_string(x) + " row " + std::to_string(y) +
                               " is outside the unit interval: " + std::to_string(a);
                    }
                    const double before = expected.value(x, y, channel);
                    const double after = expected.value(x + 1u, y, channel);
                    if (!std::isfinite(before) || !std::isfinite(after))
                    {
                        continue;
                    }
                    ++compared;
                    if ((before < after - slack && a > b + slack) || (before > after + slack && a < b - slack))
                    {
                        ++disordered;
                    }
                }
            }
        }
        if (compared == 0u)
        {
            return "no pair could be compared";
        }
        if (disordered > compared / 200u)
        {
            return std::to_string(disordered) + " of " + std::to_string(compared) +
                   " neighbouring pairs are ordered the other way";
        }
        return std::string();
    };
}

/// A composition whose green and blue planes were matched to the red: the red plane as it is, and each matched plane
/// an affine function of its own input plane with a positive slope - what a scaling match is - where the record's
/// least-squares line over the corpus's synthetic planes, which run against the red, has a negative slope and turns
/// them over.
Judgement affineInEachMatchedPlane()
{
    return [](const fixtures::FixtureSamples& input, const fixtures::Expectation&,
              const fixtures::FixtureSamples& result) -> std::string {
        if (input.channelCount() != 3u || result.channelCount() != 3u)
        {
            return "the composition is three planes of a three-channel scene";
        }
        const std::uint32_t width = result.width();
        const std::uint32_t height = result.height();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (std::abs(result.value(x, y, 0u) - input.value(x, y, 0u)) > 1.0e-6)
                {
                    return "the red plane, the reference, is not as it was";
                }
            }
        }
        for (std::uint32_t channel = 1u; channel < 3u; ++channel)
        {
            double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
            const double count = static_cast<double>(width) * height;
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double in = input.value(x, y, channel);
                    const double out = result.value(x, y, channel);
                    sumX += in;
                    sumY += out;
                    sumXX += in * in;
                    sumXY += in * out;
                }
            }
            const double denominator = count * sumXX - sumX * sumX;
            if (denominator <= 0.0)
            {
                return "a matched plane holds one value";
            }
            const double slope = (count * sumXY - sumX * sumY) / denominator;
            const double intercept = (sumY - slope * sumX) / count;
            if (slope <= 0.0)
            {
                return "the plane " + std::to_string(channel) + " was matched with a slope of " + std::to_string(slope);
            }
            double residual = 0.0;
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const double out = result.value(x, y, channel);
                    if (out <= 0.0 || out >= 1.0)
                    {
                        continue;
                    }
                    residual = (std::max)(residual, std::abs(out - (slope * input.value(x, y, channel) + intercept)));
                }
            }
            if (residual > 1.0e-5)
            {
                return "the plane " + std::to_string(channel) + " departs from a line by " + std::to_string(residual);
            }
        }
        return std::string();
    };
}

const std::vector<AcceptedDifference>& acceptedDifferences()
{
    static const std::vector<AcceptedDifference> differences{
        {.records = "image-blend.hue",
         .bound = 5.0e-5,
         .reason = "the hue, saturation, colour and lightness modes work in CIE L*a*b* in double, where the record "
                   "works in single precision",
         .judge = {}},
        {.records = "image-blend.saturation", .bound = 5.0e-5, .reason = "as the hue mode", .judge = {}},
        {.records = "image-blend.color", .bound = 5.0e-5, .reason = "as the hue mode", .judge = {}},
        {.records = "image-blend.lightness", .bound = 5.0e-5, .reason = "as the hue mode", .judge = {}},
        {.records = "image-blend.multiply-preserve-lightness",
         .bound = 5.0e-5,
         .reason = "the kept lightness goes through CIE L*a*b* in double",
         .judge = {}},
        {.records = "lrgb-combination.",
         .bound = 5.0e-5,
         .reason = "the combination works in CIE L*c*h* in double, where the record works in single precision",
         .judge = {}},
        {.records = "narrowband-palette-composer.",
         .bound = 1.0e-2,
         .reason = "the inputs are stretched by exact statistics from every sample, where the record's screen "
                   "transfer measures a subsample",
         .judge = {}},
        {.records = "channel-composition.linear-match",
         .bound = 0.0,
         .reason = "the linear match is the normalization stage's scaling match, whose slope is a ratio of scales and "
                   "positive, where the record's least-squares line over the corpus's planes, which run against "
                   "the red, has a negative slope and turns them over",
         .judge = affineInEachMatchedPlane()},
        {.records = "channel-composition.cie-",
         .bound = 5.0e-5,
         .reason = "the colour conversion is the engine's, in double, where the record's is single precision",
         .judge = {}},
        {.records = "channel-composition.hs", .bound = 5.0e-5, .reason = "as the CIE spaces", .judge = {}},
        {.records = "channel-composition.ycbcr",
         .bound = 5.0e-4,
         .reason = "the engine's BT.601 coefficients are exact, where the record's are rounded to three places",
         .judge = {}},
    };
    return differences;
}

const AcceptedDifference* acceptedDifferenceFor(const std::string& id)
{
    for (const AcceptedDifference& difference : acceptedDifferences())
    {
        if (id.rfind(difference.records, 0u) == 0u)
        {
            return &difference;
        }
    }
    return nullptr;
}

class CombinationsRecordTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const CombinationsProcess& process() const
    {
        return combinationsProcesses()[GetParam()];
    }
};

} // namespace

// Every record of the process's corpus file, through its window.
TEST_P(CombinationsRecordTest, EveryRecordOfTheProcessIsReproducedThroughItsWindow)
{
    const CombinationsProcess& under = process();
    const std::vector<fixtures::Expectation> records = recordsOf(under.records);
    ASSERT_FALSE(records.empty()) << under.records;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const fixtures::Expectation& record : records)
    {
        SCOPED_TRACE(record.id);
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(record.inputName);
        ASSERT_NE(image, nullptr);
        std::string error;
        const std::optional<fixtures::FixtureSamples> input = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(input.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *input))
            << "the record was captured from another input than this scene";

        std::optional<an::consumer::ImageAsset> asset = sceneAsset(context->engineSession(), *input);
        ASSERT_TRUE(asset.has_value());
        Targets targets = openTargets(*context, under, std::move(*asset), record.inputName, record.inputName);
        ASSERT_NE(targets.target, nullptr);
        WindowHandle window = under.open(targets.target.get());
        ASSERT_NE(window, nullptr);
        under.configure(*window, targets, modeOf(record));
        const std::optional<fixtures::FixtureSamples> result = applyAndRead(*context, under, *window, targets);
        EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(under.committed(*window), 1u);
        ASSERT_TRUE(result.has_value());

        const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, *result);
        const AcceptedDifference* difference = acceptedDifferenceFor(record.id);
        if (compared.largestDifference > (difference != nullptr ? difference->bound : 1.0e-6) &&
            result->channelCount() == record.result.channelCount())
        {
            // The three samples furthest from the record, for the reader of a failure.
            std::vector<std::tuple<double, std::uint32_t, std::uint32_t, std::uint32_t>> worst;
            for (std::uint32_t c = 0u; c < result->channelCount(); ++c)
            {
                for (std::uint32_t y = 0u; y < result->height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < result->width(); ++x)
                    {
                        worst.emplace_back(std::abs(result->value(x, y, c) - record.result.value(x, y, c)), x, y, c);
                    }
                }
            }
            std::partial_sort(worst.begin(), worst.begin() + (std::min)(worst.size(), std::size_t{3}), worst.end(),
                              [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
            for (std::size_t index = 0u; index < (std::min)(worst.size(), std::size_t{3}); ++index)
            {
                const auto [apart, x, y, c] = worst[index];
                std::cout << record.id << ": at " << x << "," << y << " channel " << c << " the record holds "
                          << record.result.value(x, y, c) << " and the result " << result->value(x, y, c) << "\n";
            }
        }
        if (difference != nullptr)
        {
            ASSERT_EQ(result->channelCount(), record.result.channelCount());
            if (difference->judge)
            {
                const std::string judged = difference->judge(*input, record, *result);
                EXPECT_TRUE(judged.empty()) << difference->reason << ": " << judged;
            }
            else
            {
                EXPECT_LE(compared.largestDifference, difference->bound) << difference->reason;
            }
            std::cout << record.id << ": largest difference " << compared.largestDifference << " ("
                      << difference->reason << ")\n";
        }
        else
        {
            EXPECT_TRUE(compared.matched) << compared.reason << "; " << compared.differingSamples
                                          << " samples differ, the largest by " << compared.largestDifference;
            std::cout << record.id << ": largest difference " << compared.largestDifference << "\n";
        }
        window.reset();
        dropTargets(*context, targets);
    }
}

INSTANTIATE_TEST_SUITE_P(Processes, CombinationsRecordTest, ::testing::ValuesIn(processesWithRecords()),
                         [](const ::testing::TestParamInfo<std::size_t>& info) {
                             return std::string(combinationsProcesses()[info.param].name);
                         });

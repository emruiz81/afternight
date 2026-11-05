// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of the colour calibration tools, reproduced through the window as a person runs it: the
/// record's scene opened in a view made active - with the linear match's reference beside it - the window set to the
/// record's mode and applied, and the view's image held to the record by the record's own bound, or, where the window's
/// estimator is not the record's, to the stated composition computed here from the scene's samples and to a record that
/// differs from it the way the two estimators explain.

#include "Test_ProcessesColorTools/ColorToolsTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::colortools;

/// The stated neutralization of a scene: each channel's median within the closed limits, brought to their mean, held to
/// the unit interval.
[[nodiscard]] std::vector<std::vector<double>> statedNeutralization(const fixtures::FixtureSamples& input,
                                                                    double lowest, double highest)
{
    std::array<double, 3u> levels{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        levels[channel] = medianOf(channelFractions(input, channel, [&](std::uint32_t, std::uint32_t, double value) {
            return value >= lowest && value <= highest;
        }));
    }
    const double pedestal = (levels[0] + levels[1] + levels[2]) / 3.0;
    std::vector<std::vector<double>> result(3u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        result[channel] = channelFractions(input, channel, [](std::uint32_t, std::uint32_t, double) { return true; });
        for (double& value : result[channel])
        {
            value = std::clamp(value + pedestal - levels[channel], 0.0, 1.0);
        }
    }
    return result;
}

/// The stated linear match of a scene to a reference of its size: over the positions both share where the reference
/// lies within the limits and neither is zero, the ratio of the median absolute deviations and the offset that moves
/// the target's median onto the reference's.
[[nodiscard]] std::vector<std::vector<double>> statedLinearMatch(const fixtures::FixtureSamples& input,
                                                                 const fixtures::FixtureSamples& reference,
                                                                 double lowest, double highest)
{
    std::vector<std::vector<double>> result(input.channelCount());
    for (std::uint32_t channel = 0u; channel < input.channelCount(); ++channel)
    {
        const auto shared = [&](std::uint32_t x, std::uint32_t y, double) {
            const double r = fractionOf(reference, x, y, channel);
            const double t = fractionOf(input, x, y, channel);
            return r >= lowest && r <= highest && r != 0.0 && t != 0.0;
        };
        const std::vector<double> targets = channelFractions(input, channel, shared);
        std::vector<double> references;
        for (std::uint32_t y = 0u; y < input.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < input.width(); ++x)
            {
                if (shared(x, y, 0.0))
                {
                    references.push_back(fractionOf(reference, x, y, channel));
                }
            }
        }
        const double targetScale = deviationOf(targets);
        const double referenceScale = deviationOf(references);
        const double scale = targetScale > 0.0 && referenceScale > 0.0 ? referenceScale / targetScale : 1.0;
        const double offset = medianOf(references) - scale * medianOf(targets);
        result[channel] = channelFractions(input, channel, [](std::uint32_t, std::uint32_t, double) { return true; });
        for (double& value : result[channel])
        {
            value = value * scale + offset;
        }
    }
    return result;
}

/// The largest difference between a result and stated values, as fractions of full scale.
[[nodiscard]] double largestFromStated(const fixtures::FixtureSamples& made,
                                       const std::vector<std::vector<double>>& stated)
{
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < made.channelCount(); ++channel)
    {
        std::size_t index = 0u;
        for (std::uint32_t y = 0u; y < made.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < made.width(); ++x, ++index)
            {
                largest = (std::max)(largest, std::fabs(fractionOf(made, x, y, channel) - stated[channel][index]));
            }
        }
    }
    return largest;
}

/// How far, per channel, a record departs from being the result moved by one offset: the spread of their differences
/// over the samples neither held to the unit interval.
[[nodiscard]] double offsetSpread(const fixtures::FixtureSamples& held, const fixtures::FixtureSamples& made)
{
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < held.channelCount(); ++channel)
    {
        double low = std::numeric_limits<double>::infinity();
        double high = -std::numeric_limits<double>::infinity();
        for (std::uint32_t y = 0u; y < held.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < held.width(); ++x)
            {
                const double a = fractionOf(held, x, y, channel);
                const double b = fractionOf(made, x, y, channel);
                if (a <= 0.0 || a >= 1.0 || b <= 0.0 || b >= 1.0)
                {
                    continue;
                }
                low = (std::min)(low, a - b);
                high = (std::max)(high, a - b);
            }
        }
        if (high >= low)
        {
            largest = (std::max)(largest, high - low);
        }
    }
    return largest;
}

/// How far, per channel, a record departs from being one affine map of the result's input: the largest residual of the
/// line through the record's samples at the input's smallest and largest values.
[[nodiscard]] double affineResidual(const fixtures::FixtureSamples& held, const fixtures::FixtureSamples& input)
{
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < held.channelCount(); ++channel)
    {
        std::uint32_t lowX = 0u, lowY = 0u, highX = 0u, highY = 0u;
        double low = std::numeric_limits<double>::infinity();
        double high = -std::numeric_limits<double>::infinity();
        for (std::uint32_t y = 0u; y < input.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < input.width(); ++x)
            {
                const double value = fractionOf(input, x, y, channel);
                if (value < low)
                {
                    low = value;
                    lowX = x;
                    lowY = y;
                }
                if (value > high)
                {
                    high = value;
                    highX = x;
                    highY = y;
                }
            }
        }
        const double slope =
            (fractionOf(held, highX, highY, channel) - fractionOf(held, lowX, lowY, channel)) / (high - low);
        const double intercept = fractionOf(held, lowX, lowY, channel) - slope * low;
        for (std::uint32_t y = 0u; y < input.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < input.width(); ++x)
            {
                largest = (std::max)(largest, std::fabs(fractionOf(held, x, y, channel) -
                                                        (slope * fractionOf(input, x, y, channel) + intercept)));
            }
        }
    }
    return largest;
}

/// The stated colour calibration of a scene that is its own white and background reference: each channel's background
/// the exact median of every sample, its white the mean within the closed limits 0 to 0.9 - or a manual white balance -
/// the gains each channel's white above its background normalized to the strongest's, about the background, held to the
/// unit interval.
[[nodiscard]] std::vector<std::vector<double>> statedColorCalibration(const fixtures::FixtureSamples& input,
                                                                      const std::array<double, 3u>* manual)
{
    std::array<double, 3u> background{};
    std::array<double, 3u> above{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        background[channel] =
            medianOf(channelFractions(input, channel, [](std::uint32_t, std::uint32_t, double) { return true; }));
        const std::vector<double> white = channelFractions(
            input, channel, [](std::uint32_t, std::uint32_t, double value) { return value >= 0.0 && value <= 0.9; });
        double sum = 0.0;
        for (const double value : white)
        {
            sum += value;
        }
        above[channel] = std::fabs(sum / static_cast<double>(white.size()) - background[channel]);
    }
    std::array<double, 3u> gains{};
    const std::size_t strongest =
        above[0] > above[1] ? (above[0] > above[2] ? 0u : 2u) : (above[1] > above[2] ? 1u : 2u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        gains[channel] = manual != nullptr ? (*manual)[channel] : above[strongest] / above[channel];
    }
    std::vector<std::vector<double>> result(3u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        result[channel] = channelFractions(input, channel, [](std::uint32_t, std::uint32_t, double) { return true; });
        for (double& value : result[channel])
        {
            value = std::clamp(value * gains[channel] + background[channel] * (1.0 - gains[channel]), 0.0, 1.0);
        }
    }
    return result;
}

/// The least-squares line through a result's channel against its input's, over the samples the result did not hold to
/// the unit interval, and how far the farthest of them lies from it.
struct ChannelLine
{
    double gain = 0.0;
    double offset = 0.0;
    double residual = 0.0;
};

[[nodiscard]] ChannelLine unclampedLine(const fixtures::FixtureSamples& out, const fixtures::FixtureSamples& input,
                                        std::uint32_t channel)
{
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    double count = 0.0;
    const auto unclamped = [](double value) { return value > 0.0 && value < 1.0; };
    for (std::uint32_t y = 0u; y < out.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < out.width(); ++x)
        {
            const double o = fractionOf(out, x, y, channel);
            if (!unclamped(o))
            {
                continue;
            }
            const double v = fractionOf(input, x, y, channel);
            sx += v;
            sy += o;
            sxx += v * v;
            sxy += v * o;
            count += 1.0;
        }
    }
    ChannelLine line;
    line.gain = (count * sxy - sx * sy) / (count * sxx - sx * sx);
    line.offset = (sy - line.gain * sx) / count;
    for (std::uint32_t y = 0u; y < out.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < out.width(); ++x)
        {
            const double o = fractionOf(out, x, y, channel);
            if (unclamped(o))
            {
                line.residual = (std::max)(line.residual,
                                           std::fabs(o - (line.gain * fractionOf(input, x, y, channel) + line.offset)));
            }
        }
    }
    return line;
}

/// How close a result is to the stated composition: a Float32 working copy's rounding of a fraction.
constexpr double kStatedBound = 2.0e-6;

class ColorToolsRecordTest : public ::testing::TestWithParam<const char*>
{
};

} // namespace

// Every record of a process's corpus file, through its window as a person runs it.
TEST_P(ColorToolsRecordTest, EveryRecordOfTheProcessIsReproducedThroughItsWindow)
{
    const std::string file = GetParam();
    const ColorTool* under = toolOfRecords(file);
    ASSERT_NE(under, nullptr) << file;
    const std::vector<fixtures::Expectation> records = recordsOf(file);
    ASSERT_FALSE(records.empty()) << file;
    const shared::RegistrySnapshot snapshot;
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
        const std::string mode = modeOf(record);

        // The linear match's record matched the scene to the second scene, which the window reads from a view of its
        // own.
        ViewHandle reference;
        if (under->needsReference)
        {
            reference = sceneView(context->engineSession(), "scene-rgb-float32-second", kReferenceView);
            ASSERT_NE(reference, nullptr);
            activate(*context, *reference);
        }
        ViewHandle target = sceneView(context->engineSession(), record.inputName);
        ASSERT_NE(target, nullptr);
        activate(*context, *target);
        WindowHandle window = under->open(target.get());
        under->configure(*window, mode);
        if (under->needsReference)
        {
            under->chooseReference(*window, QString::fromLatin1(kReferenceView));
        }
        const std::size_t before = context->madeViews.size();
        ASSERT_TRUE(applyAndSettle(*context, *window, *under));
        EXPECT_EQ(context->madeViews.size(), before);
        EXPECT_TRUE(context->errorTexts.isEmpty()) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
        ASSERT_TRUE(made.has_value());
        window.reset();
        dropView(*context, target);
        if (reference != nullptr)
        {
            dropView(*context, reference);
        }

        const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, *made);
        std::cout << "[ record ] " << record.id << ": largest " << compared.largestDifference << ", "
                  << compared.differingSamples << " samples differ" << std::endl;
        if (file == "background-neutralization")
        {
            // Each channel's level is its median within the closed limits, where the record subtracts its mean within
            // the half-open ones. The result is the stated composition, and the record differs from it by one offset
            // per channel - the other estimator - and by nothing else.
            const double upper = mode == "upper-0.5" ? 0.5 : 0.1;
            EXPECT_LE(largestFromStated(*made, statedNeutralization(*input, 0.0, upper)), kStatedBound);
            EXPECT_LE(offsetSpread(record.result, *made), 1.0e-5)
                << "the record differs from the result by more than one offset per channel";
            continue;
        }
        if (file == "linear-match")
        {
            // The scaling match of the robust locations and scales, where the record fits a least-squares line
            // through the paired samples. Both are one affine map of the target per channel.
            const std::optional<fixtures::FixtureSamples> second = sceneSamples("scene-rgb-float32-second");
            ASSERT_TRUE(second.has_value());
            EXPECT_LE(largestFromStated(*made, statedLinearMatch(*input, *second, 0.0, 0.92)), kStatedBound);
            EXPECT_LE(affineResidual(record.result, *input), 1.0e-5)
                << "the record is not one affine map of the target per channel";
            continue;
        }
        if (file == "color-calibration")
        {
            // Each channel's background is the exact median of the reference, where the record interpolates a
            // histogram's, and its white the mean within the closed limits. The records' white and background
            // references are one twelve-by-eight scene, so each gain is the ratio of two channels' gaps between their
            // mean and their median, which the median's estimate decides. The result is the stated composition, and the
            // record is a gain about a background per channel, as the composition is.
            const bool manual = mode == "manual-0.9-1-0.8";
            const std::array<double, 3u> manualGains{0.9, 1.0, 0.8};
            // A gain of tens amplifies a fraction's Float32 rounding: an integer result is held within two codes.
            const double bound =
                fixtures::isFloating(made->sampleType())
                    ? kStatedBound
                    : 2.0 / (fixtures::highestOf(made->sampleType()) - fixtures::lowestOf(made->sampleType()));
            EXPECT_LE(largestFromStated(*made, statedColorCalibration(*input, manual ? &manualGains : nullptr)), bound);
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const ChannelLine held = unclampedLine(record.result, *input, channel);
                EXPECT_LE(held.residual, 1.0e-5) << "channel " << channel << " of the record is not one gain";
                if (manual)
                {
                    EXPECT_NEAR(held.gain, manualGains[channel], 1.0e-5) << "channel " << channel;
                    const ChannelLine ours = unclampedLine(*made, *input, channel);
                    EXPECT_NEAR(held.offset, ours.offset, 1.0e-3)
                        << "channel " << channel << ": the backgrounds differ by more than the median's estimate";
                }
            }
            continue;
        }
        // SCNR: a lightness kept through CIE L*a*b* goes through the record's XYZ matrices and the engine's, the
        // record's inverse a printed approximation of its own.
        if (mode != "average-neutral-lightness-free")
        {
            EXPECT_LE(largestFractionDifference(*made, record.result), 5.0e-5) << record.id;
            continue;
        }
        EXPECT_TRUE(compared.matched) << record.id << ": " << compared.reason;
    }
}

INSTANTIATE_TEST_SUITE_P(Records, ColorToolsRecordTest,
                         ::testing::Values("color-calibration", "background-neutralization", "linear-match", "scnr"),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name;
                             for (const char c : std::string_view(info.param))
                             {
                                 if (c != '-')
                                 {
                                     name += c;
                                 }
                             }
                             return name;
                         });

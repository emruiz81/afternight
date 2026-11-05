// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of the eight pointwise and tone processes, reproduced through the process's own window: the
/// record's scene opened in a view, the window opened over it as the launcher opens it, its widgets set to the record's
/// mode as a person sets them, Apply, and what the view shows held to the record - by the record's own bound, or by the
/// bound an accepted difference states, every sample beyond it one the difference explains.

#include "Test_ProcessesPointwise/PointwiseTestSupport.h"

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::pointwise;

/// Whether a pixel of a scene is one an accepted difference explains a larger departure at.
using Explanation = std::function<bool(const fixtures::FixtureSamples& input, std::uint32_t x, std::uint32_t y)>;

/// A difference from the record that a case accepts: the records it applies to, how far the result may be from the
/// record, and why.
struct AcceptedDifference
{
    /// The start of the ids of the records it applies to.
    std::string records;

    /// Whether the record holds every recomposed colour to the unit interval, which the engine does not do to a
    /// floating result: the result is compared after the same clamp.
    bool recordClamps = false;

    /// The largest absolute difference allowed against the record, in the record's units.
    double bound = 0.0;

    /// Why.
    std::string reason;

    /// The pixels at which a sample may lie further than the bound, and why; none when no sample may.
    Explanation explains;

    /// The departure an explained pixel shows, for a reader.
    std::string explained;
};

/// A sample of a scene in the records' units: an unsigned integer divided by its largest value in single precision, a
/// floating sample as it is.
[[nodiscard]] double normalized(const fixtures::FixtureSamples& samples, std::uint32_t x, std::uint32_t y,
                                std::uint32_t channel)
{
    const double value = samples.value(x, y, channel);
    switch (samples.sampleType())
    {
    case fixtures::FixtureSampleType::UInt16:
        return static_cast<double>(static_cast<float>(value) / 65535.0f);
    case fixtures::FixtureSampleType::UInt8:
        return static_cast<double>(static_cast<float>(value) / 255.0f);
    default:
        return value;
    }
}

/// The HSL lightness of a pixel of a colour scene, in the records' units.
[[nodiscard]] double hslLightness(const fixtures::FixtureSamples& samples, std::uint32_t x, std::uint32_t y)
{
    const double r = normalized(samples, x, y, 0u);
    const double g = normalized(samples, x, y, 1u);
    const double b = normalized(samples, x, y, 2u);
    return ((std::max)({r, g, b}) + (std::min)({r, g, b})) / 2.0;
}

/// The green channel of a colour scene in the records' units, in single precision.
[[nodiscard]] std::vector<float> greenOf(const fixtures::FixtureSamples& samples)
{
    std::vector<float> green;
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples.width(); ++x)
        {
            green.push_back(static_cast<float>(normalized(samples, x, y, 1u)));
        }
    }
    return green;
}

/// The exact median of values, the mean of the two middle ones for an even count.
[[nodiscard]] double exactMedian(std::vector<float> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2u;
    return values.size() % 2u == 1u
               ? static_cast<double>(values[middle])
               : (static_cast<double>(values[middle - 1u]) + static_cast<double>(values[middle])) / 2.0;
}

/// The record's median, estimated from a histogram of as many bins as there are values, up to 65,536, over the values'
/// range, interpolated inside the bin that holds the middle - restated here so a case can say where a background
/// derived from it lies.
[[nodiscard]] double histogramEstimatedMedian(const std::vector<float>& values)
{
    const float minimum = *std::min_element(values.begin(), values.end());
    const float maximum = *std::max_element(values.begin(), values.end());
    if (maximum == minimum)
    {
        return minimum;
    }
    const std::size_t bins = (std::min<std::size_t>)(65536u, values.size());
    const float scale = static_cast<float>(bins - 1u) / (maximum - minimum);
    std::vector<std::uint32_t> histogram(bins, 0u);
    for (const float value : values)
    {
        ++histogram[static_cast<std::uint16_t>(scale * (value - minimum))];
    }
    const float threshold = 0.5f * static_cast<float>(values.size());
    std::size_t bin = 0u;
    std::size_t count = 0u;
    while (static_cast<float>(count) < threshold)
    {
        count += histogram[bin++];
    }
    const std::size_t before = count - histogram[bin - 1u];
    const float above = static_cast<float>(count) - threshold;
    const float below = threshold - static_cast<float>(before);
    float median = (below * static_cast<float>(bin) + above * static_cast<float>(bin - 1u)) / (above + below);
    median = median / scale + minimum;
    return static_cast<double>((std::min)((std::max)(median, minimum), maximum));
}

/// The standard deviation the record adds to that median, over single-precision values summed in double.
[[nodiscard]] double populationDeviation(const std::vector<float>& values)
{
    double sum = 0.0;
    double squares = 0.0;
    for (const float value : values)
    {
        sum += value;
        squares += static_cast<double>(value * value);
    }
    const double mean = sum / static_cast<double>(values.size());
    return std::sqrt(squares / static_cast<double>(values.size()) - mean * mean);
}

/// The colour saturation's background explanation: a pixel whose lightness lies between the background derived from
/// the exact median and the one the record derives from its histogram estimate is enhanced on one side and left on the
/// other.
[[nodiscard]] Explanation betweenTheTwoBackgrounds()
{
    return [](const fixtures::FixtureSamples& input, std::uint32_t x, std::uint32_t y) {
        const std::vector<float> green = greenOf(input);
        const double deviation = populationDeviation(green);
        const double exact = exactMedian(green) + deviation;
        const double estimated = histogramEstimatedMedian(green) + deviation;
        const double lightness = hslLightness(input, x, y);
        return lightness > (std::min)(exact, estimated) && lightness <= (std::max)(exact, estimated);
    };
}

/// The LCh adjustment's hue explanation: a pixel whose CIE hue, turned by the offset, leaves the record's [-180, 179]
/// degrees - which it holds there rather than turning on round - is a colour the engine turns and the record stops.
[[nodiscard]] Explanation hueLeavesTheRecordsRange(double offsetDegrees)
{
    return [offsetDegrees](const fixtures::FixtureSamples& input, std::uint32_t x, std::uint32_t y) {
        an::ColorEncoding rgb{};
        rgb.transfer = an::ColorTransferFunction::Srgb;
        an::ColorEncoding lch{};
        lch.schema = an::ChannelSchema::Lch;
        const an::Result<an::ColorConversionPlan> plan = an::planColorConversion(rgb, lch);
        if (!plan.hasValue())
        {
            return false;
        }
        const std::array<double, 3u> source{normalized(input, x, y, 0u), normalized(input, x, y, 1u),
                                            normalized(input, x, y, 2u)};
        std::array<double, 3u> converted{};
        if (an::convertColorPixel(*plan, source, converted).failed())
        {
            return false;
        }
        // The engine's hue runs from zero to 360 degrees, the record's from -180 to 180.
        double hue = converted[2];
        if (hue > 180.0)
        {
            hue -= 360.0;
        }
        const double turned = hue + offsetDegrees;
        return turned < -180.0 || turned > 179.0;
    };
}

constexpr const char* kCie = "the record inverts CIE XYZ through a printed matrix that is not the inverse of its "
                             "forward one, and the engine inverts the matrix it used";
constexpr const char* kLumaChroma = "the record's luma-chroma coefficients are rounded to three decimals in each "
                                    "direction, and the engine derives both from the BT.601 weights";

const std::vector<AcceptedDifference>& acceptedDifferences()
{
    static const std::vector<AcceptedDifference> differences{
        {.records = "gamma-correction.channel-cie-", .recordClamps = true, .bound = 5.0e-5, .reason = kCie},
        {.records = "gamma-correction.channel-ycbcr-", .recordClamps = true, .bound = 1.0e-3, .reason = kLumaChroma},
        {.records = "arcsinh-stretch.stretch-50-even-luminance/",
         .bound = 1.0e-4,
         .reason = "the record weighs each channel by 0.3333, a luminance 0.9999 of the mean the engine takes, "
                   "which moves the ratio every channel is scaled by by at most one part in ten thousand"},
        {.records = "color-saturation.amount-0.5/scene-rgb-uint16",
         .bound = 1.0,
         .reason = "the background is the green channel's exact median plus its deviation; the record estimates "
                   "the median from a histogram of one bin per pixel, here 96, which puts it 0.00135 lower",
         .explains = betweenTheTwoBackgrounds(),
         .explained = "a pixel whose lightness lies between the two backgrounds, enhanced in the record alone"},
        {.records = "adjust-lch.lightness-10-chroma-30-hue-minus-20/scene-rgb-float32",
         .bound = 5.0e-5,
         .reason = kCie,
         .explains = hueLeavesTheRecordsRange(-20.0),
         .explained = "a hue the record holds at -180 degrees, which the engine turns on round"},
        {.records = "adjust-lch.lightness-10-chroma-30-hue-minus-20/scene-rgb-uint16",
         .bound = 4.0,
         .reason = kCie,
         .explains = hueLeavesTheRecordsRange(-20.0),
         .explained = "a hue the record holds at -180 degrees, which the engine turns on round"},
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

class PointwiseRecordTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const PointwiseProcess& process() const
    {
        return pointwiseProcesses()[GetParam()];
    }
};

/// Holds a result to a record by its accepted difference: within the bound, after the record's clamp where it clamps,
/// and every sample beyond the bound at a pixel the difference explains.
void expectWithinAcceptedDifference(const fixtures::Expectation& record, const fixtures::FixtureSamples& input,
                                    const fixtures::FixtureSamples& result, const AcceptedDifference& difference)
{
    ASSERT_EQ(result.channelCount(), record.result.channelCount());
    ASSERT_EQ(result.sampleType(), record.result.sampleType());
    double largest = 0.0;
    double largestExplained = 0.0;
    std::uint64_t explained = 0u;
    for (std::uint32_t c = 0u; c < result.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < result.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < result.width(); ++x)
            {
                const double value = result.value(x, y, c);
                const double held = difference.recordClamps ? (std::min)((std::max)(value, 0.0), 1.0) : value;
                const double expected = record.result.value(x, y, c);
                if (std::isnan(expected) && std::isnan(held))
                {
                    continue;
                }
                const double apart = std::abs(held - expected);
                if (apart <= difference.bound)
                {
                    largest = (std::max)(largest, apart);
                    continue;
                }
                if (difference.explains && difference.explains(input, x, y))
                {
                    largestExplained = (std::max)(largestExplained, apart);
                    ++explained;
                    continue;
                }
                ADD_FAILURE() << record.id << " (" << difference.reason << "): " << apart << " apart at " << x << ","
                              << y << " channel " << c << ", " << value << " against " << expected
                              << ", which the accepted difference does not explain";
            }
        }
    }
    std::cout << record.id << ": largest difference " << largest << " within " << difference.bound;
    if (explained > 0u)
    {
        std::cout << "; " << explained << " samples up to " << largestExplained << " apart at " << difference.explained;
    }
    std::cout << "\n";
}

} // namespace

// Every record of the process's record file, through its window.
TEST_P(PointwiseRecordTest, EveryRecordOfTheProcessIsReproducedThroughItsWindow)
{
    const PointwiseProcess& under = process();
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

        ViewHandle target = sceneView(context->engineSession(), record.inputName);
        ASSERT_NE(target, nullptr);
        WindowHandle window = under.open(*target);
        ASSERT_NE(window, nullptr);
        under.configure(*window, modeOf(record));
        ASSERT_TRUE(applyAndSettle(*window, *target));
        EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);

        const std::optional<fixtures::FixtureSamples> result = documentSamples(target->document());
        ASSERT_TRUE(result.has_value());
        if (const AcceptedDifference* difference = acceptedDifferenceFor(record.id); difference != nullptr)
        {
            expectWithinAcceptedDifference(record, *input, *result, *difference);
        }
        else
        {
            const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, *result);
            EXPECT_TRUE(compared.matched) << compared.reason << "; " << compared.differingSamples
                                          << " samples differ, the largest by " << compared.largestDifference;
            std::cout << record.id << ": largest difference " << compared.largestDifference << "\n";
        }
        window.reset();
        dropView(*context, target);
    }
}

INSTANTIATE_TEST_SUITE_P(EightProcesses, PointwiseRecordTest,
                         ::testing::Range<std::size_t>(0u, pointwiseProcesses().size()),
                         [](const ::testing::TestParamInfo<std::size_t>& info) {
                             return std::string(pointwiseProcesses()[info.param].name);
                         });

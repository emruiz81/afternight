// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of the mask, star and background processes, reproduced through the process's own window: the
/// record's scene opened in a view, the window opened over it as the launcher opens it, its widgets set to the record's
/// mode as a person sets them, Apply, and what the result holds - the target's document, or the new view a mask lands
/// in - held to the record by the record's own bound, or by what an accepted difference states, every sample beyond its
/// bound one the difference explains.

#include "Test_ProcessesMasks/MasksTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::masks;

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

    /// Whether the Apply is refused by name, leaving the target as it was: the record holds an image nothing was
    /// written into.
    bool refused = false;
};

/// Whether a stored sample sits where the record's unit-interval clamp could have put it, which no smoothness claim
/// reaches.
bool atUnitBound(double value)
{
    return value <= 0.0 || value >= 1.0;
}

/// A difference that is a surface rather than a pattern of pixels: within a bound everywhere, and departing from the
/// mean of its eight neighbours by no more than a roughness - what two fits of one background differ by, and not what
/// a different correction of the samples would. A relative difference is the ratio of the result to the record, less
/// one, for a correction that divides.
Judgement smoothDifference(double bound, double roughness, bool relative)
{
    return [bound, roughness, relative](const fixtures::FixtureSamples&, const fixtures::Expectation& record,
                                        const fixtures::FixtureSamples& result) -> std::string {
        const fixtures::FixtureSamples& expected = record.result;
        const std::uint32_t width = expected.width();
        const std::uint32_t height = expected.height();
        auto differenceAt = [&](std::uint32_t x, std::uint32_t y, std::uint32_t channel) -> std::optional<double> {
            const double before = expected.value(x, y, channel);
            const double after = result.value(x, y, channel);
            if (!std::isfinite(before) || !std::isfinite(after) || atUnitBound(before) || atUnitBound(after))
            {
                return std::nullopt;
            }
            return relative ? after / before - 1.0 : after - before;
        };
        double largest = 0.0;
        double roughest = 0.0;
        std::uint64_t judged = 0u;
        for (std::uint32_t channel = 0u; channel < expected.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const std::optional<double> here = differenceAt(x, y, channel);
                    if (!here.has_value())
                    {
                        continue;
                    }
                    ++judged;
                    largest = (std::max)(largest, std::abs(*here));
                    if (x == 0u || y == 0u || x + 1u == width || y + 1u == height)
                    {
                        continue;
                    }
                    double sum = 0.0;
                    int counted = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0)
                            {
                                continue;
                            }
                            if (const std::optional<double> neighbour =
                                    differenceAt(static_cast<std::uint32_t>(static_cast<int>(x) + dx),
                                                 static_cast<std::uint32_t>(static_cast<int>(y) + dy), channel);
                                neighbour.has_value())
                            {
                                sum += *neighbour;
                                ++counted;
                            }
                        }
                    }
                    if (counted == 8)
                    {
                        roughest = (std::max)(roughest, std::abs(*here - sum / 8.0));
                    }
                }
            }
        }
        if (judged == 0u)
        {
            return "no sample could be judged";
        }
        if (largest > bound || roughest > roughness)
        {
            return "the difference reaches " + std::to_string(largest) + " against " + std::to_string(bound) +
                   ", and departs from its neighbours by " + std::to_string(roughest) + " against " +
                   std::to_string(roughness);
        }
        return std::string();
    };
}

/// A star mask from another detector, which on a scene this small has room to search for few of the stars: every sample
/// a coverage in the unit interval, at least one star painted, and every star painted - each a set of covered samples
/// joined through their eight neighbours - one the record paints too, so nothing is painted where the record saw no
/// star.
Judgement paintsStarsTheRecordPaints()
{
    return [](const fixtures::FixtureSamples&, const fixtures::Expectation& record,
              const fixtures::FixtureSamples& result) -> std::string {
        const fixtures::FixtureSamples& expected = record.result;
        const std::uint32_t width = expected.width();
        const std::uint32_t height = expected.height();
        std::vector<bool> visited(static_cast<std::size_t>(width) * height, false);
        std::size_t stars = 0u;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const double value = result.value(x, y, 0u);
                if (!(value >= 0.0 && value <= 1.0))
                {
                    return "a sample at column " + std::to_string(x) + " row " + std::to_string(y) +
                           " is no coverage: " + std::to_string(value);
                }
                if (value <= 0.0 || visited[static_cast<std::size_t>(y) * width + x])
                {
                    continue;
                }
                ++stars;
                bool onRecord = false;
                std::vector<std::pair<std::uint32_t, std::uint32_t>> pending{{x, y}};
                visited[static_cast<std::size_t>(y) * width + x] = true;
                while (!pending.empty())
                {
                    const auto [px, py] = pending.back();
                    pending.pop_back();
                    onRecord = onRecord || expected.value(px, py, 0u) > 0.0;
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const int nx = static_cast<int>(px) + dx;
                            const int ny = static_cast<int>(py) + dy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) || ny >= static_cast<int>(height))
                            {
                                continue;
                            }
                            const std::size_t at = static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx);
                            if (!visited[at] &&
                                result.value(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny), 0u) > 0.0)
                            {
                                visited[at] = true;
                                pending.emplace_back(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
                            }
                        }
                    }
                }
                if (!onRecord)
                {
                    return "the star painted at column " + std::to_string(x) + " row " + std::to_string(y) +
                           " is one the record does not paint";
                }
            }
        }
        if (stars == 0u)
        {
            return "the mask paints no star";
        }
        return std::string();
    };
}

const std::vector<AcceptedDifference>& acceptedDifferences()
{
    static const std::vector<AcceptedDifference> differences{
        {.records = "range-selection.lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2",
         .bound = 0.05,
         .reason = "the smoothing is the exact Gaussian of the stated deviation, where the record approximates it "
                   "with three box passes",
         .judge = {},
         .refused = false},
        {.records = "star-mask.",
         .bound = 0.0,
         .reason = "the stars are found by the engine's one fitted detector, where the records hold another "
                   "detector, which the engine does not carry; its search box fits this scene only a few times",
         .judge = paintsStarsTheRecordPaints(),
         .refused = false},
        {.records = "background-extraction.grid-4-rbf-subtract",
         .bound = 0.0,
         .reason = "the thin-plate spline is fitted with its whole affine part in normalized coordinates and evaluated "
                   "at every pixel, where the record fits a constant in pixel coordinates on a quarter lattice "
                   "and scales it back up",
         .judge = smoothDifference(2.5e-3, 2.0e-4, false),
         .refused = false},
        {.records = "background-extraction.grid-4-polynomial-4-divide",
         .bound = 0.0,
         .reason = "eight samples do not determine a fourth-degree polynomial's fifteen terms; the record holds a "
                   "result nothing was written into",
         .judge = {},
         .refused = true},
        {.records = "auto-gradient-removal.default",
         .bound = 0.0,
         .reason = "the background is the engine's fit, which differs from the record's by a surface",
         .judge = smoothDifference(5.0e-4, 1.0e-4, false),
         .refused = false},
        {.records = "auto-gradient-removal.polynomial-divide-downsample-2",
         .bound = 0.0,
         .reason = "the background is the engine's fit, which differs from the record's by a surface the division "
                   "carries as a ratio",
         .judge = smoothDifference(3.0e-2, 5.0e-3, true),
         .refused = false},
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

class MasksRecordTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const MasksProcess& process() const
    {
        return masksProcesses()[GetParam()];
    }
};

} // namespace

// Every record of the process's corpus file, through its window.
TEST_P(MasksRecordTest, EveryRecordOfTheProcessIsReproducedThroughItsWindow)
{
    const MasksProcess& under = process();
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
        // A process that reduces against a starless image reads the corpus's starless scene from a view beside it.
        ViewHandle starless;
        if (under.needsStarless)
        {
            starless = sceneView(context->engineSession(), record.inputName + "-starless", "starless");
            ASSERT_NE(starless, nullptr);
            context->addViewControl(starless.get());
        }
        WindowHandle window = under.open(*target);
        ASSERT_NE(window, nullptr);
        under.configure(*window, modeOf(record));
        const AcceptedDifference* difference = acceptedDifferenceFor(record.id);
        if (difference != nullptr && difference->refused)
        {
            // Refused by name, with the target left as it was.
            const std::size_t before = context->madeViews.size();
            EXPECT_TRUE(applyAndSettle(*window, *target));
            EXPECT_EQ(context->errorCount(), 1) << difference->reason;
            EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
            EXPECT_EQ(context->madeViews.size(), before);
            const std::optional<fixtures::FixtureSamples> kept = documentSamples(target->document());
            ASSERT_TRUE(kept.has_value());
            EXPECT_EQ(kept->digest(), input->digest());
            std::cout << record.id << ": refused (" << difference->reason << ")\n";
            window.reset();
            dropView(*context, starless);
            dropView(*context, target);
            continue;
        }
        const std::optional<fixtures::FixtureSamples> result = applyAndRead(*context, under, *window, *target);
        EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(window->runTelemetry().runsCommitted, 1u);
        ASSERT_TRUE(result.has_value());

        const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, *result);
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
        dropView(*context, starless);
        dropView(*context, target);
    }
}

INSTANTIATE_TEST_SUITE_P(Processes, MasksRecordTest, ::testing::ValuesIn(processesWithRecords()),
                         [](const ::testing::TestParamInfo<std::size_t>& info) {
                             return std::string(masksProcesses()[info.param].name);
                         });

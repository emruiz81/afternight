// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The complete autostretch rows and the definition the engine is held to.
#include "AutostretchOracle.h"
#include "AutostretchScenarios.h"
#include "CapturedStatistics.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace an::bench;

namespace
{

/// The automatic stretch's sigma: the deviation over the inverse normal at three quarters.
constexpr long double kSigma = 1.4826022185056018L;

/// A report that measured and derived exactly as the definition does.
AutostretchReport exactReport(std::span<const PresentationLocation> locations, bool linked)
{
    AutostretchReport report;
    report.channels = static_cast<std::uint32_t>(locations.size());
    const std::vector<PresentationTransfer> transfers =
        linked ? std::vector<PresentationTransfer>(locations.size(), linkedPresentationTransfer(locations, -2.8, 0.25))
               : unlinkedPresentationTransfers(locations, -2.8, 0.25);
    for (std::size_t channel = 0u; channel < locations.size(); ++channel)
    {
        report.medians[channel] = locations[channel].median;
        report.deviations[channel] = locations[channel].deviation;
        report.transfers[channel] = transfers[channel];
    }
    return report;
}

/// Planar samples whose channels differ in level and spread, like an unbalanced colour sky with stars.
std::vector<float> unbalancedSky(std::uint32_t pixels, std::uint32_t channels)
{
    std::vector<float> samples(static_cast<std::size_t>(pixels) * channels);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (std::uint32_t pixel = 0u; pixel < pixels; ++pixel)
        {
            const float level = 0.02f + 0.01f * static_cast<float>(channel);
            const float noise = static_cast<float>((pixel * 7919u + channel * 104729u) % 1000u) / 250000.0f;
            const float star = pixel % 97u == 0u ? 0.9f : 0.0f;
            samples[static_cast<std::size_t>(channel) * pixels + pixel] = (std::min)(1.0f, level + noise + star);
        }
    }
    return samples;
}

/// One row of the catalogue by id.
ScenarioSpec rowNamed(const std::string& id)
{
    for (const ScenarioSpec& row : autostretchScenarios())
    {
        if (row.id == id)
        {
            return row;
        }
    }
    ADD_FAILURE() << "no row " << id;
    return {};
}

/// The output a report's transfers give, in single precision.
std::vector<float> applied(std::span<const float> input, const AutostretchReport& report)
{
    std::vector<float> output(input.size());
    const std::size_t plane = input.size() / report.channels;
    for (std::size_t index = 0u; index < input.size(); ++index)
    {
        output[index] = static_cast<float>(presentationTransfer(
            std::clamp(static_cast<long double>(input[index]), 0.0L, 1.0L), report.transfers[index / plane]));
    }
    return output;
}

} // namespace

TEST(AutostretchOracleTest, TheRowsCoverEveryStageOverMonoAndColourCaptures)
{
    const std::vector<ScenarioSpec> rows = autostretchScenarios();
    const std::vector<std::string> ids = {"autostretch.complete.eagle.f32",
                                          "autostretch.complete.carina.f32",
                                          "autostretch.complete.ngc253_rgb.unlinked.f32",
                                          "autostretch.complete.ngc253_rgb.linked.f32",
                                          "autostretch.complete.carina_calibrated.f32",
                                          "autostretch.statistics.eagle.f32",
                                          "autostretch.statistics.carina.f32",
                                          "autostretch.statistics.ngc253_rgb.f32",
                                          "autostretch.statistics.carina_calibrated.f32",
                                          "autostretch.prepared.eagle.f32",
                                          "autostretch.prepared.carina.f32",
                                          "autostretch.prepared.ngc253_rgb.unlinked.f32",
                                          "autostretch.prepared.ngc253_rgb.linked.f32",
                                          "autostretch.prepared.carina_calibrated.f32",
                                          "autostretch.complete.flat.f32",
                                          "autostretch.complete.stars_rgb.linked.f32"};
    ASSERT_EQ(rows.size(), ids.size());
    for (std::size_t index = 0u; index < rows.size(); ++index)
    {
        const ScenarioSpec& row = rows[index];
        EXPECT_EQ(row.id, ids[index]);
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_TRUE(requiresAutostretchOracle(row)) << row.id;
        const bool measures = row.parameters.at(kAutostretchBoundaryParameter) == kAutostretchStatistics;
        EXPECT_EQ(row.tolerance, measures ? kAutostretchStatisticsTolerance : 1e-6) << row.id;
        EXPECT_EQ(row.parameters.at("shadowsClipping"), "-2.8") << row.id;
        EXPECT_EQ(row.parameters.at("targetBackground"), "0.25") << row.id;
    }
    // The colour frame is binned from the Bayer capture: a quarter of its pixels, three samples each.
    const ScenarioSpec colour = rowNamed("autostretch.complete.ngc253_rgb.linked.f32");
    EXPECT_EQ(colour.parameters.at(kAutostretchSamplesParameter), kAutostretchBinnedColour);
    EXPECT_EQ(colour.parameters.at("linked"), "true");
    EXPECT_EQ(colour.elementsProcessed, std::uint64_t{2772u} * 1842u * 3u);
    EXPECT_EQ(rowNamed("autostretch.complete.carina.f32").elementsProcessed, std::uint64_t{8288u} * 5644u);
    // The calibrated light is read as the pipeline wrote it.
    const ScenarioSpec calibrated = rowNamed("autostretch.complete.carina_calibrated.f32");
    EXPECT_EQ(calibrated.corpusId, kAutostretchCalibratedLight);
    EXPECT_EQ(calibrated.input.sampleType, SampleType::Float32);
    EXPECT_EQ(calibrated.parameters.at(kAutostretchSamplesParameter), kAutostretchAsGiven);
}

TEST(AutostretchOracleTest, ABinnedColourFrameIsEachBlocksRedMeanGreenAndBlue)
{
    // A 5 x 3 capture: the odd last column and row are dropped, leaving two pixels.
    const std::array<std::uint16_t, 15u> codes = {100u, 200u, 300u, 400u, 9u, 600u, 700u, 800u,
                                                  900u, 9u,   9u,   9u,   9u, 9u,   9u};
    std::vector<std::uint8_t> bytes;
    for (const std::uint16_t code : codes)
    {
        bytes.push_back(static_cast<std::uint8_t>(code & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>(code >> 8u));
    }
    const std::vector<float> planes = makeBinnedColourCapture(bytes, 5u, 3u);
    ASSERT_EQ(planes.size(), 6u);
    EXPECT_EQ(planes[0], 100.0f / 65535.0f);
    EXPECT_EQ(planes[1], 300.0f / 65535.0f);
    EXPECT_EQ(planes[2], 800.0f / 131070.0f);
    EXPECT_EQ(planes[3], 1200.0f / 131070.0f);
    EXPECT_EQ(planes[4], 700.0f / 65535.0f);
    EXPECT_EQ(planes[5], 900.0f / 65535.0f);
    EXPECT_TRUE(makeBinnedColourCapture(std::span<const std::uint8_t>(bytes).first(28u), 5u, 3u).empty());
}

TEST(AutostretchOracleTest, TheLinkedTransferAveragesTheChannelsEndpointsAndMedians)
{
    const std::vector<PresentationLocation> sky = {
        {.median = 0.05, .deviation = 0.004}, {.median = 0.07, .deviation = 0.002}, {.median = 0.06, .deviation = 0.0}};
    const PresentationTransfer transfer = linkedPresentationTransfer(sky, -2.8, 0.25);
    // A zero deviation counts as one thousandth of the range.
    const long double shadows =
        ((0.05L - 2.8L * 0.004L * kSigma) + (0.07L - 2.8L * 0.002L * kSigma) + (0.06L - 2.8L * 0.001L)) / 3.0L;
    EXPECT_NEAR(static_cast<double>(transfer.shadows), static_cast<double>(shadows), 1e-15);
    EXPECT_EQ(transfer.highlights, 1.0L);
    // The balance is the midtones that take the mean median's height above the shadows to the target.
    const PresentationTransfer balance{.midtones = 0.25L, .shadows = 0.0L, .highlights = 1.0L};
    EXPECT_NEAR(static_cast<double>(transfer.midtones),
                static_cast<double>(presentationTransfer(0.06L - transfer.shadows, balance)), 1e-15);

    // Every median above one half mirrors the stretch about the highlights.
    const std::vector<PresentationLocation> bright = {{.median = 0.9, .deviation = 0.01},
                                                      {.median = 0.8, .deviation = 0.02}};
    const PresentationTransfer mirrored = linkedPresentationTransfer(bright, -2.8, 0.25);
    EXPECT_EQ(mirrored.shadows, 0.0L);
    const long double highlights = ((0.9L + 2.8L * 0.01L * kSigma) + (0.8L + 2.8L * 0.02L * kSigma)) / 2.0L;
    EXPECT_NEAR(static_cast<double>(mirrored.highlights), static_cast<double>((std::min)(1.0L, highlights)), 1e-15);
    EXPECT_NEAR(static_cast<double>(mirrored.midtones),
                static_cast<double>(1.0L - presentationTransfer(mirrored.highlights - 0.85L, balance)), 1e-15);

    // A shadow point below zero is held at zero, and no channel is the identity.
    const std::vector<PresentationLocation> dark = {{.median = 0.001, .deviation = 0.01}};
    EXPECT_EQ(linkedPresentationTransfer(dark, -2.8, 0.25).shadows, 0.0L);
    const PresentationTransfer identity = linkedPresentationTransfer({}, -2.8, 0.25);
    EXPECT_EQ(identity.midtones, 0.5L);
    EXPECT_EQ(identity.shadows, 0.0L);
    EXPECT_EQ(identity.highlights, 1.0L);
}

TEST(AutostretchOracleTest, ACapturesLocationByCountingIsItsLocationBySorting)
{
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t index = 0u; index < 4001u; ++index)
    {
        const auto code = static_cast<std::uint16_t>((index * 2654435761u) % 9973u + 1000u);
        bytes.push_back(static_cast<std::uint8_t>(code & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>(code >> 8u));
    }
    const std::vector<PresentationLocation> counted = exactCapturedLocations(bytes);
    const std::vector<PresentationLocation> sorted = exactAutostretchLocations(makeUnitIntervalCapture(bytes), 1u);
    ASSERT_EQ(counted.size(), 1u);
    ASSERT_EQ(sorted.size(), 1u);
    EXPECT_EQ(counted[0].median, sorted[0].median);
    EXPECT_EQ(counted[0].deviation, sorted[0].deviation);
    EXPECT_TRUE(exactCapturedLocations(std::span<const std::uint8_t>(bytes).first(3u)).empty());
}

TEST(AutostretchOracleTest, EachStageIsJudgedApart)
{
    constexpr std::uint32_t kPixels = 4096u;
    const std::vector<float> input = unbalancedSky(kPixels, 3u);
    const std::vector<PresentationLocation> exact = exactAutostretchLocations(input, 3u);
    for (const char* id :
         {"autostretch.complete.ngc253_rgb.unlinked.f32", "autostretch.complete.ngc253_rgb.linked.f32"})
    {
        ScenarioSpec row = rowNamed(id);
        row.elementsProcessed = std::uint64_t{kPixels} * 3u;
        const bool linked = row.parameters.at("linked") == "true";
        const AutostretchReport report = exactReport(exact, linked);
        const std::vector<float> output = applied(input, report);

        ResultRecord record;
        checkAutostretchOutput(row, input, output, report, exact, record);
        ASSERT_TRUE(record.correctnessMetric.has_value()) << id;
        EXPECT_LT(*record.correctnessMetric, 1e-7) << id;
        EXPECT_EQ(record.counters.at("autostretchStatisticsError"), 0.0) << id;
        EXPECT_EQ(record.counters.at("autostretchTransferError"), 0.0) << id;
        EXPECT_EQ(record.counters.at("autostretchSamplesCompared"), static_cast<double>(input.size())) << id;
        EXPECT_TRUE(autostretchEvidenceValid(row, record)) << id;

        // A median measured within the statistics tier, and the transfer derived from it, pass; the
        // output then differs from the exact definition, which is recorded and not judged.
        std::vector<PresentationLocation> nearby = exact;
        nearby[1].median += 0.5 * kAutostretchStatisticsTolerance;
        const AutostretchReport approximate = exactReport(nearby, linked);
        ResultRecord close;
        checkAutostretchOutput(row, input, applied(input, approximate), approximate, exact, close);
        EXPECT_TRUE(autostretchEvidenceValid(row, close)) << id;
        EXPECT_GT(close.counters.at("autostretchDefinitionError"), 1e-6) << id;

        // A statistic outside the tier fails, however well the rest follows from it.
        nearby[1].median += 2.0 * kAutostretchStatisticsTolerance;
        const AutostretchReport far = exactReport(nearby, linked);
        ResultRecord measuredBadly;
        checkAutostretchOutput(row, input, applied(input, far), far, exact, measuredBadly);
        EXPECT_FALSE(autostretchEvidenceValid(row, measuredBadly)) << id;

        // A transfer that does not follow from the reported statistics fails, even applied faithfully.
        AutostretchReport derivedBadly = report;
        derivedBadly.transfers[2].midtones += 1e-5L;
        ResultRecord transferRecord;
        checkAutostretchOutput(row, input, applied(input, derivedBadly), derivedBadly, exact, transferRecord);
        EXPECT_FALSE(autostretchEvidenceValid(row, transferRecord)) << id;

        // An output sample that does not follow from the transfer fails.
        std::vector<float> moved = output;
        moved[2u * kPixels + 17u] += 1e-4f;
        ResultRecord outputRecord;
        checkAutostretchOutput(row, input, moved, report, exact, outputRecord);
        EXPECT_FALSE(autostretchEvidenceValid(row, outputRecord)) << id;
        EXPECT_NEAR(outputRecord.counters.at("autostretchOutputError"), 1e-4, 1e-6) << id;

        // An output that covers less than the row fails.
        ResultRecord shortRecord;
        checkAutostretchOutput(row, input, std::span<const float>(output).first(10u), report, exact, shortRecord);
        EXPECT_FALSE(autostretchEvidenceValid(row, shortRecord)) << id;
    }

    // A linked row's transfer is the one every channel shares, not each channel's own.
    ScenarioSpec linkedRow = rowNamed("autostretch.complete.ngc253_rgb.linked.f32");
    linkedRow.elementsProcessed = std::uint64_t{kPixels} * 3u;
    const AutostretchReport unlinkedTransfers = exactReport(exact, false);
    ResultRecord crossed;
    checkAutostretchOutput(linkedRow, input, applied(input, unlinkedTransfers), unlinkedTransfers, exact, crossed);
    EXPECT_FALSE(autostretchEvidenceValid(linkedRow, crossed));
}

TEST(AutostretchOracleTest, AStatisticsRowIsJudgedByItsStatisticsAlone)
{
    constexpr std::uint32_t kPixels = 2048u;
    const std::vector<float> input = unbalancedSky(kPixels, 3u);
    const std::vector<PresentationLocation> exact = exactAutostretchLocations(input, 3u);
    const ScenarioSpec row = rowNamed("autostretch.statistics.ngc253_rgb.f32");
    AutostretchReport report = exactReport(exact, false);
    ResultRecord record;
    checkAutostretchOutput(row, input, {}, report, exact, record);
    ASSERT_TRUE(record.correctnessMetric.has_value());
    EXPECT_EQ(*record.correctnessMetric, 0.0);
    EXPECT_FALSE(record.counters.contains("autostretchOutputError"));
    EXPECT_TRUE(autostretchEvidenceValid(row, record));

    report.deviations[0] += 2.0 * kAutostretchStatisticsTolerance;
    ResultRecord far;
    checkAutostretchOutput(row, input, {}, report, exact, far);
    EXPECT_FALSE(autostretchEvidenceValid(row, far));
    EXPECT_NEAR(*far.correctnessMetric, 2.0 * kAutostretchStatisticsTolerance, 1e-12);

    // The digest text carries every reported statistic.
    EXPECT_NE(autostretchStatisticsText(exactReport(exact, false)), autostretchStatisticsText(report));

    ResultRecord empty;
    EXPECT_FALSE(autostretchEvidenceValid(row, empty));
}

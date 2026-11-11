// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The report model beside the Markdown report a run still writes: for every scenario both are built from the same
/// run, and the figures the Markdown prints are the model's. The two are built by different code from the same
/// records, so a figure the model lost or changed on the way shows here.
///
/// The Markdown takes a few of its figures from the run's log; those are not held here, since the model takes none.
/// These cases go when the Markdown report does.

#include <gtest/gtest.h>

#include "Test_ImageIntegration/reporting/ReportModelTestSupport.h"

#include "processes/ImageIntegration/reporting/WorkflowReportBuilder.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace an::tests::integration::report;
using namespace ImageIntegration::Reporting;

namespace
{
/// The Markdown report of a scenario's run, as a run writes it today.
QString markdownOf(const ReportScenario& scenario)
{
    return WorkflowReportBuilder(scenario.manifest, scenario.result, QString(), scenario.context, nullptr,
                                 scenario.session())
        .build();
}

/// A duration as the Markdown prints it.
QString markdownDuration(const qint64 ms)
{
    if (ms < 1000)
    {
        return QStringLiteral("%1ms").arg(ms);
    }
    const qint64 totalSeconds = ms / 1000;
    if (totalSeconds < 60)
    {
        return QStringLiteral("%1s").arg(totalSeconds);
    }
    qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    if (minutes < 60)
    {
        return QStringLiteral("%1m %2s").arg(minutes).arg(seconds);
    }
    const qint64 hours = minutes / 60;
    minutes = minutes % 60;
    return QStringLiteral("%1h %2m %3s").arg(hours).arg(minutes).arg(seconds);
}

/// A figure with two decimals and no trailing zeros, as the Markdown prints a stack's figures.
QString markdownCompact(const double value)
{
    QString text = QString::number(value, 'f', 2);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.'))))
    {
        text.chop(1);
    }
    return text;
}

/// A fraction as the percentage the Markdown prints.
QString markdownPercent(const double fraction)
{
    return QString::number(fraction * 100.0, 'f', 1) + QStringLiteral("%");
}

/// A measured figure at the ten significant digits the Markdown prints.
QString markdownStatistic(const double value)
{
    return QString::number(value, 'g', 10);
}

/// One figure of each channel as the Markdown's statistics table prints it.
template <class Figure>
QString markdownPerChannel(const std::vector<ReportChannelStatistics>& channels, Figure&& figure)
{
    if (channels.size() == 1)
    {
        return markdownStatistic(figure(channels.front()));
    }
    QStringList parts;
    for (std::size_t i = 0; i < channels.size(); ++i)
    {
        parts << QStringLiteral("C%1=%2").arg(i).arg(markdownStatistic(figure(channels[i])));
    }
    return parts.join(QStringLiteral(", "));
}

/// The part of the Markdown about one master: from its heading to the next heading of its level or above.
QString masterSection(const QString& markdown, const ReportMaster& master)
{
    const QString heading = QStringLiteral("### %1\n- **Output**: %2 (").arg(master.name, master.fileName);
    const qsizetype start = markdown.indexOf(heading);
    if (start < 0)
    {
        return {};
    }
    qsizetype end = markdown.indexOf(QStringLiteral("\n### "), start + heading.size());
    const qsizetype nextPart = markdown.indexOf(QStringLiteral("\n## "), start + heading.size());
    if (end < 0 || (nextPart >= 0 && nextPart < end))
    {
        end = nextPart;
    }
    return end < 0 ? markdown.mid(start) : markdown.mid(start, end - start);
}

::testing::AssertionResult states(const QString& markdown, const QString& text)
{
    if (markdown.contains(text))
    {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "the Markdown does not state: " << text.toStdString();
}
} // namespace

TEST(ReportModelParity, TheRunsTotalsAreTheOnesTheMarkdownPrints)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        const QString markdown = markdownOf(*scenario);
        const ReportModel model = buildScenarioModel(*scenario);
        const ReportRun& run = model.run;

        EXPECT_TRUE(states(markdown, QStringLiteral("| Light Frames | %1 |\n").arg(run.frames)));
        if (run.capturedMs > 0)
        {
            EXPECT_TRUE(states(
                markdown, QStringLiteral("| Total Integration Time | **%1** |").arg(markdownDuration(run.capturedMs))));
        }
        EXPECT_TRUE(
            states(markdown, QStringLiteral("| Processing Duration | %1 |").arg(markdownDuration(run.processingMs))));
        EXPECT_TRUE(states(markdown, QStringLiteral("| Execution Mode | %1 |").arg(run.mode)));
        EXPECT_TRUE(states(markdown, QStringLiteral("Execution ID: %1").arg(run.executionId)));
        EXPECT_TRUE(states(markdown, QStringLiteral("| AfterNight Version | %1 |").arg(run.applicationVersion)));

        // The masters the run made and the ones that failed are counted from its results by both.
        EXPECT_TRUE(states(markdown, QStringLiteral("| Regenerated This Run | %1 |").arg(run.groups.made)));
        EXPECT_TRUE(states(markdown, QStringLiteral("| Failed Groups | %1 |").arg(run.groups.failed)));
        // The Markdown counts every group with no result as reused, and counts a combined colour image among the
        // masters made but not among the groups: the two agree where the run completed and combined none.
        const bool combined = std::any_of(model.masters.begin(), model.masters.end(), [](const ReportMaster& master) {
            return master.kind == ReportMasterKind::CombinedRgb;
        });
        if (!combined && run.outcome != ReportOutcome::Cancelled && run.outcome != ReportOutcome::Failed &&
            run.outcome != ReportOutcome::Partial)
        {
            EXPECT_TRUE(states(markdown, QStringLiteral("| Reused Existing Output | %1 |").arg(run.groups.reused)));
            EXPECT_TRUE(states(markdown, QStringLiteral("| Integration Groups (Total) | %1 |").arg(run.groups.total)));
        }

        if (run.nights.size() > 0)
        {
            EXPECT_TRUE(states(markdown, QStringLiteral("| Observing Sessions | %1 night").arg(run.nights.size())));
        }
        if (run.warnings > 0)
        {
            EXPECT_TRUE(states(markdown, QStringLiteral("| Warnings | %1 |").arg(run.warnings)));
        }
        if (run.errors > 0)
        {
            EXPECT_TRUE(states(markdown, QStringLiteral("| Errors | %1 |").arg(run.errors)));
        }

        const QString status = run.outcome == ReportOutcome::Cancelled ? QStringLiteral("Cancelled")
                               : !scenario->result.success             ? QStringLiteral("Failed")
                                                                       : QStringLiteral("Success");
        EXPECT_TRUE(states(markdown, QStringLiteral("<span class=\"an-status-pill")));
        EXPECT_TRUE(states(markdown, QStringLiteral("\">%1</span>").arg(status)));
    }
}

TEST(ReportModelParity, EachMastersFiguresAreTheOnesTheMarkdownPrints)
{
    int mastersHeld = 0;
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        const QString markdown = markdownOf(*scenario);
        const ReportModel model = buildScenarioModel(*scenario);

        for (const ReportMaster& master : model.masters)
        {
            if (master.state != ReportMasterState::Made)
            {
                // The Markdown prints a master only when the run made it.
                EXPECT_FALSE(markdown.contains(QStringLiteral("### %1\n- **Output**").arg(master.name)))
                    << master.name.toStdString();
                continue;
            }
            SCOPED_TRACE(master.name.toStdString());
            const QString section = masterSection(markdown, master);
            ASSERT_FALSE(section.isEmpty()) << "the Markdown has no part for the master";
            ++mastersHeld;

            ASSERT_TRUE(master.lightsCombined.has_value());
            EXPECT_TRUE(states(section, QStringLiteral("- **Light Frames**: %1\n").arg(*master.lightsCombined)));
            if (master.exposure.integrationMs > 0)
            {
                EXPECT_TRUE(states(section, QStringLiteral("- **Integration Time**: %1 (")
                                                .arg(markdownDuration(master.exposure.integrationMs))));
            }
            ASSERT_TRUE(master.durationMs.has_value());
            EXPECT_TRUE(states(section, QStringLiteral("%1\n").arg(markdownDuration(*master.durationMs))));

            if (master.stack.has_value())
            {
                const ReportStack& stack = *master.stack;
                EXPECT_TRUE(states(section, QStringLiteral("- **Effective Frames**: %1 / %2\n")
                                                .arg(markdownCompact(stack.effectiveFrames))
                                                .arg(stack.inputFrames)));
                EXPECT_TRUE(
                    states(section,
                           QStringLiteral("- **Actual SNR Gain**: %1x\n").arg(markdownCompact(stack.measuredSnrGain))));
                EXPECT_TRUE(states(section, QStringLiteral("- **Theoretical SNR Gain**: %1x\n")
                                                .arg(markdownCompact(stack.theoreticalSnrGain))));
                EXPECT_TRUE(states(
                    section,
                    QStringLiteral("- **Integration Efficiency**: %1\n").arg(markdownPercent(stack.efficiency))));
                EXPECT_TRUE(states(section, QStringLiteral("- **Coverage**: min %1 | median %2 | max %3 | stdev %4\n")
                                                .arg(markdownCompact(stack.lowestCoverage))
                                                .arg(markdownCompact(stack.medianCoverage))
                                                .arg(markdownCompact(stack.highestCoverage))
                                                .arg(markdownCompact(stack.coverageDeviation))));
                if (stack.rejection.has_value())
                {
                    EXPECT_TRUE(states(section, QStringLiteral("- **Global Rejection Rate**: %1\n")
                                                    .arg(markdownPercent(stack.rejection->global))));
                    EXPECT_TRUE(
                        states(section,
                               QStringLiteral("- **Per-Frame Rejection Rate**: min %1 | mean %2 | median %3 | max %4\n")
                                   .arg(markdownPercent(stack.rejection->lowestFrame))
                                   .arg(markdownPercent(stack.rejection->meanFrame))
                                   .arg(markdownPercent(stack.rejection->medianFrame))
                                   .arg(markdownPercent(stack.rejection->highestFrame))));
                }
                else
                {
                    EXPECT_FALSE(section.contains(QStringLiteral("Global Rejection Rate")));
                }
                if (stack.weights.has_value())
                {
                    EXPECT_TRUE(states(section, QStringLiteral("- **Weight Range**: %1 to %2\n")
                                                    .arg(markdownCompact(stack.weights->lowest))
                                                    .arg(markdownCompact(stack.weights->highest))));
                    EXPECT_TRUE(states(section, QStringLiteral("- **Weight Efficiency**: %1\n")
                                                    .arg(markdownPercent(stack.weights->efficiency))));
                }
                if (stack.localWeights.has_value())
                {
                    EXPECT_TRUE(
                        states(section, QStringLiteral("- **Grids**: %1 frames\n").arg(stack.localWeights->grids)));
                }
                if (stack.restack.has_value())
                {
                    EXPECT_TRUE(states(section, QStringLiteral("- **Spatial Re-Stack**: %1\n")
                                                    .arg(stack.restack->applied ? QStringLiteral("Applied")
                                                                                : QStringLiteral("Not applied"))));
                    for (const ReportRestackExclusion& excluded : stack.restack->excluded)
                    {
                        EXPECT_TRUE(states(section, excluded.fileName));
                    }
                }
            }
            else
            {
                EXPECT_FALSE(section.contains(QStringLiteral("#### Stacking Quality Report")));
            }

            if (master.statistics.has_value())
            {
                const std::vector<ReportChannelStatistics>& channels = master.statistics->channelStats;
                const auto row = [&channels](const char* name, auto figure) {
                    return QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                        .arg(QString::fromLatin1(name), markdownPerChannel(channels, figure));
                };
                // The table is printed beside the master's picture, which a run that was not archived has none of.
                if (section.contains(QStringLiteral("Image statistics")))
                {
                    EXPECT_TRUE(states(section, QStringLiteral("<tr><td>Dimensions</td><td>%1×%2</td></tr>")
                                                    .arg(master.statistics->width)
                                                    .arg(master.statistics->height)));
                    EXPECT_TRUE(states(section, row("Min", [](const auto& c) { return c.minimum; })));
                    EXPECT_TRUE(states(section, row("Max", [](const auto& c) { return c.maximum; })));
                    EXPECT_TRUE(states(section, row("Mean", [](const auto& c) { return c.mean; })));
                    EXPECT_TRUE(states(section, row("Median", [](const auto& c) { return c.median; })));
                    EXPECT_TRUE(states(section, row("Sigma", [](const auto& c) { return c.standardDeviation; })));
                    EXPECT_TRUE(states(section, row("Avg Dev", [](const auto& c) { return c.averageDeviation; })));
                    EXPECT_TRUE(states(section, row("Variance", [](const auto& c) { return c.variance; })));
                    EXPECT_TRUE(states(section, row("MAD", [](const auto& c) { return c.medianAbsoluteDeviation; })));
                    EXPECT_TRUE(states(section, row("BG Noise", [](const auto& c) { return c.backgroundNoise; })));
                    EXPECT_TRUE(states(section, row("BWMV", [](const auto& c) { return c.biweightMidvariance; })));
                }
                // The headline figures are printed whether or not there is a picture.
                ASSERT_TRUE(master.snapshot.mean.has_value());
                EXPECT_TRUE(states(
                    section,
                    QStringLiteral("- **Quality Snapshot**: Mean %1").arg(markdownStatistic(*master.snapshot.mean))));
                if (master.snapshot.snr.has_value())
                {
                    EXPECT_TRUE(states(
                        section, QStringLiteral("SNR proxy %1").arg(QString::number(*master.snapshot.snr, 'f', 2))));
                }
                if (master.overlapRetained.has_value())
                {
                    EXPECT_TRUE(states(section, QStringLiteral("- **Registration Overlap Retention**: %1 (loss %2)")
                                                    .arg(markdownPercent(*master.overlapRetained))
                                                    .arg(markdownPercent(1.0 - *master.overlapRetained))));
                }
            }
            else
            {
                EXPECT_FALSE(section.contains(QStringLiteral("- **Quality Snapshot**")));
            }
        }
    }
    EXPECT_GT(mastersHeld, 30) << "the scenarios' masters were held to their Markdown";
}

TEST(ReportModelParity, TheWeightsOfEachGroupAreTheOnesTheMarkdownPrints)
{
    int groupsHeld = 0;
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        const QString markdown = markdownOf(*scenario);
        const ReportModel model = buildScenarioModel(*scenario);

        for (const ReportMaster& master : model.masters)
        {
            if (!master.weighting.has_value() || !master.weighting->medianWeight.has_value())
            {
                continue;
            }
            const ReportMasterWeighting& weighting = *master.weighting;
            // One row of the Markdown's table per group: its name, its mode, the lights registered over its lights,
            // and its median weight.
            const QString row =
                QStringLiteral("| %1 | %2 | <span class=\"an-num\">%3/%4</span> | <span class=\"an-num\">%5</span> |")
                    .arg(master.name.toHtmlEscaped(), weighting.mode.toHtmlEscaped())
                    .arg(weighting.framesRegistered)
                    .arg(master.exposure.frames)
                    .arg(QString::number(*weighting.medianWeight, 'f', 3));
            EXPECT_TRUE(states(markdown, row));
            if (weighting.weightedIntegrationMs > 0)
            {
                EXPECT_TRUE(states(markdown, QStringLiteral("<span class=\"an-num\">%1</span>")
                                                 .arg(markdownDuration(weighting.weightedIntegrationMs))));
            }
            ++groupsHeld;
        }
    }
    EXPECT_GT(groupsHeld, 20);
}

TEST(ReportModelParity, TheFingerprintIsTheOneTheMarkdownPrints)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        const QString markdown = markdownOf(*scenario);
        const ReportRun run = buildScenarioModel(*scenario).run;

        EXPECT_TRUE(states(markdown, QStringLiteral("| Composite Fingerprint | `%1` |").arg(run.fingerprint)));
        EXPECT_TRUE(
            states(markdown, QStringLiteral("| Manifest/Settings Fingerprint | `%1` |").arg(run.settingsFingerprint)));
        EXPECT_TRUE(states(markdown, QStringLiteral("| Input Fingerprint | `%1` |").arg(run.inputsFingerprint)));
        EXPECT_TRUE(states(markdown, QStringLiteral("| Reproducibility Fingerprint | `%1` |").arg(run.fingerprint)));
    }
}

TEST(ReportModelParity, TheCalibrationAndTheRegistrationAreTheOnesTheMarkdownPrints)
{
    for (const ReportScenarioId id : allReportScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const std::optional<ReportScenario> scenario = buildReportScenario(id);
        ASSERT_TRUE(scenario.has_value());
        const QString markdown = markdownOf(*scenario);
        const ReportModel model = buildScenarioModel(*scenario);

        for (const ReportCalibrationMaster& master : model.calibrationMasters)
        {
            const QString type = master.kind == ReportCalibrationKind::Bias   ? QStringLiteral("Bias")
                                 : master.kind == ReportCalibrationKind::Dark ? QStringLiteral("Dark")
                                                                              : QStringLiteral("Flat");
            EXPECT_TRUE(
                states(markdown, QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n")
                                     .arg(type, master.name, QString::number(master.frames),
                                          master.fileName.toHtmlEscaped(), master.stackingSummary.toHtmlEscaped())));
        }
        for (const ReportLightGroup& group : model.lightGroups)
        {
            if (group.preCalibrated)
            {
                EXPECT_TRUE(states(
                    markdown, QStringLiteral("%1 (%2 light(s))").arg(group.name).arg(group.lights).toHtmlEscaped()));
            }
        }
        for (const ReportRegistrationGroup& group : model.registrationGroups)
        {
            // The Markdown prints the reference the manifest names; the model also reads the one the registration
            // chose out of its records, which the Markdown never did.
            const auto planned =
                std::find_if(scenario->manifest.registrationGroups.begin(), scenario->manifest.registrationGroups.end(),
                             [&group](const WorkflowManifest::RegistrationGroup& candidate) {
                                 return candidate.groupKey.toString() == group.key;
                             });
            ASSERT_NE(planned, scenario->manifest.registrationGroups.end());
            EXPECT_TRUE(
                states(markdown, QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n")
                                     .arg(group.name)
                                     .arg(group.lights)
                                     .arg(planned->referenceFrame.has_value() ? group.reference : QStringLiteral("-"))
                                     .arg(group.alignmentMode)
                                     .arg(group.transform.toHtmlEscaped())));
            if (group.distortionCorrected.value_or(0) > 0)
            {
                EXPECT_TRUE(states(markdown, QStringLiteral("| %1 | %2 | %3 |\n")
                                                 .arg(group.name)
                                                 .arg(*group.distortionCorrected)
                                                 .arg(*group.aligned)));
            }
            if (group.chromaticAberrationCorrected.value_or(0) > 0)
            {
                EXPECT_TRUE(states(markdown, QStringLiteral("| %1 | %2 | %3 |\n")
                                                 .arg(group.name)
                                                 .arg(*group.chromaticAberrationCorrected)
                                                 .arg(*group.chromaticAberrationPassedThrough)));
            }
        }
        for (const ReportTarget& target : model.targets)
        {
            EXPECT_TRUE(states(markdown, QStringLiteral("<h3>%1</h3><ul><li><strong>Total frames</strong>: %2</li>")
                                             .arg(target.name.toHtmlEscaped())
                                             .arg(target.frames)));
        }
    }
}

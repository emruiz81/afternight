// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the report model's suites share: the model of a scenario, built as the product builds it, and the lookups a
/// case names a master, a group or a setting by.

#include "Test_ImageIntegration/reporting/ReportScenarios.h"

#include "processes/ImageIntegration/reporting/ReportModel.h"
#include "processes/ImageIntegration/reporting/ReportModelBuilder.h"
#include "processes/ImageIntegration/reporting/ReportSources.h"

#include <QHash>
#include <QList>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace an::tests::integration::report
{
/// A scenario's name, for a failure message.
[[nodiscard]] inline std::string scenarioName(const ReportScenarioId id)
{
    return reportScenarioName(id).toStdString();
}

/// The report model of a scenario's run, built through the product's sources: the scenario's session, and its
/// metrics database as the run being committed reads it.
///
/// @param scenario the run.
/// @param runDirectory the run's folder under `history/`, for a run that was archived; empty for one that was not.
[[nodiscard]] inline ImageIntegration::Reporting::ReportModel buildScenarioModel(const ReportScenario& scenario,
                                                                                 const QString& runDirectory = {})
{
    const ImageIntegration::Reporting::ProductionReportSources production(scenario.session(),
                                                                          scenario.metricsDatabasePath);
    return ImageIntegration::Reporting::buildReportModel(
        ImageIntegration::Reporting::ReportModelInputs{.manifest = scenario.manifest,
                                                       .result = scenario.result,
                                                       .runDirectory = runDirectory,
                                                       .context = scenario.context,
                                                       .counters = std::nullopt},
        production.sources());
}

/// An image source that counts how often each file is measured, and measures through another.
class CountingImages final : public ImageIntegration::Reporting::IReportImages
{
public:
    explicit CountingImages(ImageIntegration::Reporting::IReportImages* inner) : m_inner(inner) {}

    [[nodiscard]] std::optional<ImageIntegration::Reporting::ReportImageMeasurement>
    measure(const QString& path, const std::uint32_t width) override
    {
        ++reads[path];
        widths.append(width);
        return m_inner->measure(path, width);
    }

    [[nodiscard]] std::optional<ImageIntegration::Reporting::ReportImageLevels>
    measureLevels(const QString& path, const std::uint32_t width) override
    {
        ++reads[path];
        ++levelReads;
        widths.append(width);
        return m_inner->measureLevels(path, width);
    }

    /// How many of the measurements were of a level and a noise alone.
    int levelReads = 0;

    /// How often each file was measured, by its path, and the thread widths the measurements were given.
    QHash<QString, int> reads;
    QList<std::uint32_t> widths;

    /// Every measurement asked for.
    [[nodiscard]] int total() const
    {
        int count = 0;
        for (const int ofFile : reads)
        {
            count += ofFile;
        }
        return count;
    }

private:
    ImageIntegration::Reporting::IReportImages* m_inner = nullptr;
};

/// A metrics source that answers for the lights through another, and states the comet fits and the catalog sources
/// it is given.
class StubbedMetrics final : public ImageIntegration::Reporting::IReportMetrics
{
public:
    explicit StubbedMetrics(ImageIntegration::Reporting::IReportMetrics* inner) : m_inner(inner) {}

    [[nodiscard]] bool available() override
    {
        return true;
    }

    [[nodiscard]] std::optional<int> settingsVersion(const QString& groupKey) override
    {
        return m_inner->settingsVersion(groupKey);
    }

    [[nodiscard]] QHash<QString, ImageIntegration::Reporting::RegistrationMetricRow>
    metricsByFrame(const QString& groupKey, const int settingsVersion) override
    {
        QHash<QString, ImageIntegration::Reporting::RegistrationMetricRow> rows =
            m_inner->metricsByFrame(groupKey, settingsVersion);
        if (editRows)
        {
            editRows(rows);
        }
        return rows;
    }

    [[nodiscard]] QHash<QString, FrameNoiseStatistics> noiseByFrame(const QString& groupKey,
                                                                    const int settingsVersion) override
    {
        return m_inner->noiseByFrame(groupKey, settingsVersion);
    }

    [[nodiscard]] QList<ImageIntegration::Reporting::CometReportRun> cometRuns(const WorkflowManifest&) override
    {
        return runs;
    }

    [[nodiscard]] QList<ImageIntegration::Reporting::SpccHandoffGroupSummary>
    catalogHandoff(const WorkflowManifest&) override
    {
        return handoffs;
    }

    [[nodiscard]] QString referenceFrame(const QString& groupKey, const int settingsVersion) override
    {
        return m_inner->referenceFrame(groupKey, settingsVersion);
    }

    QList<ImageIntegration::Reporting::CometReportRun> runs;
    QList<ImageIntegration::Reporting::SpccHandoffGroupSummary> handoffs;

    /// Changes the rows of a group before the builder reads them, when set.
    std::function<void(QHash<QString, ImageIntegration::Reporting::RegistrationMetricRow>&)> editRows;

private:
    ImageIntegration::Reporting::IReportMetrics* m_inner = nullptr;
};

/// The master of the group taken through a filter, the combined colour image left out; null when there is none.
[[nodiscard]] inline const ImageIntegration::Reporting::ReportMaster*
masterOfFilter(const ImageIntegration::Reporting::ReportModel& model, const QString& filter)
{
    const auto found = std::find_if(
        model.masters.begin(), model.masters.end(), [&filter](const ImageIntegration::Reporting::ReportMaster& master) {
            return master.filter == filter && master.kind == ImageIntegration::Reporting::ReportMasterKind::Stack;
        });
    return found == model.masters.end() ? nullptr : &*found;
}

/// The block of the given title among a group's settings; null when the group has none.
[[nodiscard]] inline const ImageIntegration::Reporting::ReportSettingsBlock*
blockOf(const std::vector<ImageIntegration::Reporting::ReportSettingsBlock>& blocks, const QString& title)
{
    const auto found = std::find_if(
        blocks.begin(), blocks.end(),
        [&title](const ImageIntegration::Reporting::ReportSettingsBlock& block) { return block.title == title; });
    return found == blocks.end() ? nullptr : &*found;
}

/// The row of the given name in a block; null when the block has none.
[[nodiscard]] inline const ImageIntegration::Reporting::ReportSettingsRow*
rowOf(const ImageIntegration::Reporting::ReportSettingsBlock* block, const QString& name)
{
    if (block == nullptr)
    {
        return nullptr;
    }
    const auto found =
        std::find_if(block->rows.begin(), block->rows.end(),
                     [&name](const ImageIntegration::Reporting::ReportSettingsRow& row) { return row.name == name; });
    return found == block->rows.end() ? nullptr : &*found;
}
} // namespace an::tests::integration::report

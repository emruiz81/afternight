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

#include <QString>

#include <algorithm>
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

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Builds a run's report model.
///
/// `buildReportModel` fills a `ReportModel` once, from the run's manifest and result and from what the sources read:
/// the registration's metrics, the headers, each master's samples, the session's notes and the earlier runs of the
/// output folder. Every figure comes from one of those records. The run's log is carried as text and never read for
/// a figure, so a change to a log line cannot change a report.

#include "AfterNightProcessesExport.h"

#include "ReportModel.h"
#include "ReportSources.h"

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace ImageIntegration::Reporting
{
/// The run a report model is built of.
struct ReportModelInputs
{
    /// The run's manifest and result, borrowed for the build.
    const WorkflowManifest& manifest;
    const WorkflowExecutor::Result& result;

    /// The run's folder under `history/`: relative paths of the result resolve against it, the previews are looked
    /// for in it, and the earlier runs are its siblings. Empty for a run that has no archive.
    QString runDirectory;

    /// The archive's context: the application's version, the execution and the times and zone it states.
    QJsonObject context;

    /// The run's diagnostics as they stand, for a run that collected them and whose result does not hold them yet.
    std::optional<WorkflowExecutionCounters> counters;

    /// Whether the run is read later from its archive, not reported as it is committed. A later run of the folder may
    /// have replaced what this one left outside its archive, so nothing of that kind is read: the normalization's
    /// coefficients are not stated.
    bool readFromArchive = false;
};

/// Builds the report model of a run through the given sources.
///
/// @param inputs the run.
/// @param sources what the build reads through; a source left null states nothing.
/// @return the model.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReportModel buildReportModel(const ReportModelInputs& inputs,
                                                                   const ReportSources& sources);
} // namespace ImageIntegration::Reporting

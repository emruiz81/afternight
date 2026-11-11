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
};

/// Builds the report model of a run through the given sources.
///
/// @param inputs the run.
/// @param sources what the build reads through; a source left null states nothing.
/// @return the model.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReportModel buildReportModel(const ReportModelInputs& inputs,
                                                                   const ReportSources& sources);

/// Restates on a run's model what the run knows only once it is committed: the executor's own phases and the run's
/// total, which include the time the archive and the report themselves took; the diagnostics as they stood at the
/// end; and what the engine counted of its memory. Everything else of the model is left as it was built: a model is
/// built of a run once.
///
/// @param model the run's model, as `buildReportModel` returned it.
/// @param result the run's result, complete.
AFTERNIGHTPROCESSES_API void restateRunCompletion(ReportModel& model, const WorkflowExecutor::Result& result);

/// States on a run's model what its archive holds on disk, among what the run wrote: the files of the run's folder
/// in the history, each at its size. A master the history keeps is counted there as well as among the masters.
///
/// @param model the run's model.
/// @param bytes the size of the folder's files together.
/// @param files how many files the folder holds.
AFTERNIGHTPROCESSES_API void restateRunArchive(ReportModel& model, qint64 bytes, int files);

/// Restates on a run's model the run's log, which is complete only after the report was first written.
///
/// @param model the run's model.
/// @param log the log, whole.
AFTERNIGHTPROCESSES_API void restateRunLog(ReportModel& model, const QString& log);
} // namespace ImageIntegration::Reporting

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration::Reporting
{

/// A run as its archive states it: the manifest it ran, and the result its `workflow_manifest.json` and its log give
/// back.
struct ArchivedWorkflowRun
{
    WorkflowManifest manifest;
    WorkflowExecutor::Result result;
};

/// Reads a run from its archive.
///
/// @param runDir the run's archive directory, where its log is.
/// @param archiveRoot the archive's `workflow_manifest.json` document.
/// @return the run; nothing for a document that holds no manifest.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<ArchivedWorkflowRun>
readArchivedWorkflowRun(const QString& runDir, const QJsonObject& archiveRoot);

/// Builds the report of an archived run again from its archive.
///
/// @param runDir the run's archive directory.
/// @param archiveRoot the archive's `workflow_manifest.json` document.
/// @param session the session the report reads the archived images and headers in, or null; without one the report
///        states no figure that needs a file read. The reads wait for an admission, so a caller on the GUI thread
///        passes none.
/// @return the report's markdown.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString buildWorkflowReportMarkdownFromArchive(
    const QString& runDir, const QJsonObject& archiveRoot, const an::consumer::EngineSession* session = nullptr);

} // namespace ImageIntegration::Reporting

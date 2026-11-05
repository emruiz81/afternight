// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QJsonObject>
#include <QString>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration::Reporting
{

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

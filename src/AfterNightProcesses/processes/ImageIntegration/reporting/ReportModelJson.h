// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The report model as a JSON document: what a run's archive keeps as `workflow_report.json`.
///
/// The document is the model, field for field, under the format number `kReportSchema`. A later run compares itself
/// with it without reading an image again, a newer application writes an older run's report again from it, and a
/// suite reads a report's structure from it.
///
/// - Every field is written. A value the model does not state is left out, and reads back as not stated.
/// - A key the reader does not know is ignored, so a field added later does not break an older reader of the same
///   format.
/// - A document of a newer format is refused by name: its fields may mean what this reader cannot know.

#include "AfterNightProcessesExport.h"

#include "ReportModel.h"

#include <QJsonObject>

#include <optional>

namespace ImageIntegration::Reporting
{
/// What reading a stored report model gave.
struct ReportModelRead
{
    /// The model; nothing when the document was refused.
    std::optional<ReportModel> model;

    /// Why the document was refused, as a report states it among the things it could not state; nothing when the
    /// model was read.
    std::optional<ReportNotStated> refusal;
};

namespace ReportModelJson
{
/// The document of a model.
///
/// @param model the model.
/// @return the document, its `reportSchema` the model's.
[[nodiscard]] AFTERNIGHTPROCESSES_API QJsonObject toJson(const ReportModel& model);

/// The model of a document.
///
/// @param document a document `toJson` wrote, by this version or another.
/// @return the model; or the refusal, for a document that states no format or a newer one than `kReportSchema`.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReportModelRead fromJson(const QJsonObject& document);
} // namespace ReportModelJson
} // namespace ImageIntegration::Reporting

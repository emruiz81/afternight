// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A run's archive as a report model: the report the archive keeps, or why it gives none.
///
/// A report model is built of a run once, as the run is committed, and the archive keeps it as
/// `workflow_report.json`. Everything that needs an archived run's report afterwards - a later run that compares
/// itself with it, the list of a folder's runs, a report written again - reads that document through here. Nothing
/// is built from an archive's manifest and results, and no image is read: an archive either keeps a report of this
/// version's format, or it is refused, and the refusal says which of the three things is the matter, in a sentence
/// a person can be shown. A reader never fails silently and never guesses.

#include "AfterNightProcessesExport.h"

#include "ReportModel.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace ImageIntegration::Reporting
{
class IReportFiles;

/// The name of the stored report model in a run's folder.
inline constexpr char kStoredReportFileName[] = "workflow_report.json";

/// Why an archive gives no report model.
enum class ReportArchiveRefusal
{
    /// The archive keeps no stored report: the folder, or the file in it, is not there.
    NoStoredReport,

    /// The file is there and is not a document that reads: it is cut short, empty, or not a JSON object.
    NotReadable,

    /// The document states no format, or another than this version's.
    OtherFormat
};

/// What reading an archive's report gave: the model, or the refusal with its reason.
struct ReportArchiveRead
{
    /// The model; nothing when the archive was refused.
    std::optional<ReportModel> model;

    /// Why the archive was refused; nothing when the model was read.
    std::optional<ReportArchiveRefusal> refusal;

    /// The refusal in a sentence; empty when the model was read.
    QString why;

    [[nodiscard]] bool isRead() const noexcept
    {
        return model.has_value();
    }
};

namespace ReportArchiveReader
{
/// Reads a stored report from its bytes.
///
/// @param stored the contents of a `workflow_report.json`.
/// @return the model, or the refusal: `NotReadable` for bytes that are no JSON object, `OtherFormat` for a
///     document of no format or of another.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReportArchiveRead parse(const QByteArray& stored);

/// Reads the report a run's archive keeps.
///
/// @param runDirectory the run's folder under `history/`.
/// @return the model, or the refusal.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReportArchiveRead read(const QString& runDirectory);

/// Reads the report a run's archive keeps, through the files a report model is built through.
///
/// @param runDirectory the run's folder under `history/`.
/// @param files the files to read through.
/// @return the model, or the refusal.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReportArchiveRead read(const QString& runDirectory, IReportFiles& files);
} // namespace ReportArchiveReader
} // namespace ImageIntegration::Reporting

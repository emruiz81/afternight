// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A run's report as a file: the document written from a report model, in either of its two forms.
///
/// The linked form is `workflow_report.html` in the run's folder, beside the `report_previews/` its pictures are
/// in. The embedded form is one file at a path of the caller's choosing, with every picture inside it, which opens
/// from an empty folder. No run writes either yet: the report a run writes is still the one `WorkflowReportBuilder`
/// builds, and these are written by the suites.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/reporting/ReportAssets.h"

#include <QString>
#include <QStringList>

namespace ImageIntegration::Reporting
{
struct ReportModel;

/// The name of the linked report in a run's folder.
inline constexpr char kWorkflowReportFileName[] = "workflow_report.html";

/// What writing a report came to.
struct WorkflowReportWritten
{
    /// The file written; empty when none was.
    QString path;

    /// Why no file was written; empty when one was.
    QString error;

    /// What the document's markup rules refused while it was written. A document with any is still written: it
    /// says less than it should, and the list says what.
    QStringList faults;

    [[nodiscard]] bool isWritten() const
    {
        return error.isEmpty() && !path.isEmpty();
    }
};

/// Writes a report's document from its model.
///
/// @param model the run's report model.
/// @param runDirectory the run's folder under `history/`: where the linked form is written, and where either form's
///     pictures are.
/// @param form the form to write.
/// @param destination the file an embedded report is written to. Not read for the linked form.
/// @return the file written, or why none was. A file that exists is replaced whole or left as it was.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowReportWritten writeWorkflowReport(const ReportModel& model,
                                                                                const QString& runDirectory,
                                                                                ReportForm form,
                                                                                const QString& destination = {});
} // namespace ImageIntegration::Reporting

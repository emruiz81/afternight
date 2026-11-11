// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A run's report as files: the document written from a report model, in either of its two forms, and what a run's
/// archive keeps of its report.
///
/// The linked form is `workflow_report.html` in the run's folder, beside the `report_previews/` its pictures are
/// in. The embedded form is one file at a path of the caller's choosing, with every picture inside it, which opens
/// from an empty folder.
///
/// An archive keeps three things of a report, which `storeWorkflowReport` writes together: the model as
/// `workflow_report.json`, which a later run compares itself with; the linked document; and, beside the run's own
/// pictures, copies of the pictures of the earlier runs its masters are compared with, so the report stays whole
/// once the history drops those runs. The archiver stores a run's report as it commits the run, and again when the
/// run's final timings and its final log are known.
///
/// Afterwards a report is only ever written from the model its archive keeps: `rerenderWorkflowReport` writes the
/// linked document again and `saveWorkflowReportAsOneFile` the embedded one. Neither builds anything of a run: an
/// archive that keeps no stored report, or one of another format, is refused in the reader's words and nothing is
/// written.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/reporting/ReportAssets.h"

#include <QString>
#include <QStringList>

namespace ImageIntegration::Reporting
{
struct ReportModel;

/// The name of the linked report in a run's folder.
inline constexpr char kWorkflowReportFileName[] = "workflow_report.html";

/// The names an earlier run's two pictures of a master are copied under, in the folder of this run's own pictures
/// of that master: the whole frame and the detail at full scale.
inline constexpr char kPreviousMasterPictureName[] = "previous_master.jpg";
inline constexpr char kPreviousDetailPictureName[] = "previous_detail.jpg";

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

/// What storing a run's report in its archive came to.
struct WorkflowReportStored
{
    /// The stored model and the linked document; each empty when it was not written.
    QString modelPath;
    QString reportPath;

    /// Why the report was not stored; empty when both files were written.
    QString error;

    /// What the document's markup rules refused while it was written.
    QStringList faults;

    /// The pictures of earlier runs that could not be copied into the run's folder, each with why. The report
    /// stored states none of them: a picture that is not there is not named.
    QStringList picturesNotCopied;

    [[nodiscard]] bool isStored() const
    {
        return error.isEmpty() && !modelPath.isEmpty() && !reportPath.isEmpty();
    }
};

/// Stores a run's report in its archive: copies the pictures of the earlier runs the model's masters are compared
/// with into the run's folder, then writes the model as `workflow_report.json` and the linked document beside it.
///
/// The pictures are copied first, from the archives of the runs each baseline names, as those runs' stored reports
/// name them; a picture already copied is left as it is, and stands when the run it was copied from is gone. A
/// baseline's picture that is not there and cannot be copied is taken out of the model before it is written, so
/// neither the stored model nor the document names a file that is not there. A link to the report of an earlier
/// run that is no longer there is taken out the same way.
///
/// @param model the run's report model, built with `runDirectory` as the run's folder.
/// @param runDirectory the run's folder under `history/`.
/// @return the files written, or why the report was not stored.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowReportStored storeWorkflowReport(const ReportModel& model,
                                                                               const QString& runDirectory);

/// Writes a run's linked report again from the model its archive keeps.
///
/// The document links the report of each earlier run of the folder that is still there: a run the history has
/// dropped since the report was last written is listed without a link.
///
/// @param runDirectory the run's folder under `history/`.
/// @return the file written; or why none was - the reader's refusal for an archive that keeps no stored report or
///     one of another format - and then nothing in the folder is changed.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowReportWritten rerenderWorkflowReport(const QString& runDirectory);

/// Writes a run's report as one file that opens on its own, from the model its archive keeps: every picture inside
/// it, no folder of the host named and no link leaving it.
///
/// @param runDirectory the run's folder under `history/`.
/// @param destination the file to write.
/// @return the file written; or why none was, the reader's refusal among the reasons.
[[nodiscard]] AFTERNIGHTPROCESSES_API WorkflowReportWritten saveWorkflowReportAsOneFile(const QString& runDirectory,
                                                                                        const QString& destination);
} // namespace ImageIntegration::Reporting

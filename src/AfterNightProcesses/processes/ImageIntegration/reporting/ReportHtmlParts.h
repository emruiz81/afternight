// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the units that write the report's document share. Private to the reporting units.
///
/// The document is written part by part, each part by its own unit. A part states the anchors it will write before
/// any part is written, so a link is made only to a place the document has: an issue of the summary points at a
/// master's file only when that file is in the document. Then each part writes its body and names its entries of
/// the contents.

#include "processes/ImageIntegration/reporting/ReportAssets.h"
#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"

#include <QCoreApplication>
#include <QSet>
#include <QString>

#include <vector>

namespace ImageIntegration::Reporting
{
/// A sentence or a name of the document, in the report's translation context. No translation ships: the report
/// reads in English.
[[nodiscard]] inline QString documentText(const char* text)
{
    return QCoreApplication::translate("ImageIntegrationReport", text);
}

/// A count with the word for what is counted: the singular for one, the plural otherwise. Both arrive translated
/// and hold `%1` where the count stands.
[[nodiscard]] inline QString counted(const qint64 count, const char* singular, const char* plural)
{
    return documentText(count == 1 ? singular : plural).arg(formatCount(count));
}

/// One entry of the contents.
struct ReportContentsEntry
{
    /// The anchor the entry leads to, without the `#`.
    QString anchor;

    QString label;

    /// A figure after the label: how many issues, how many lights of how many. May be empty.
    QString count;

    /// Whether the entry leads to something that needs attention, which the contents mark with an icon.
    bool attention = false;
};

/// One part of the document.
struct ReportPart
{
    /// The part's heading in the contents: "1 · Summary".
    QString heading;

    std::vector<ReportContentsEntry> entries;

    /// The part's markup: one `section`.
    ReportMarkup body;

    /// Whether the part has anything to say. An absent part is not written and has no entry.
    [[nodiscard]] bool isPresent() const
    {
        return !body.isEmpty();
    }
};

/// What every part is written from.
struct ReportDocumentContext
{
    const ReportModel& model;
    ReportAssets& assets;

    /// The zone the run's archive states its times in.
    ReportTimeZone zone;

    /// Every anchor the document will have, stated before any part is written.
    QSet<QString> anchors;

    [[nodiscard]] bool hasAnchor(const QString& anchor) const
    {
        return anchors.contains(anchor);
    }
};

// ---- Fragments the parts share (ReportHtmlWriter.cpp).

/// A pill: an icon and a word, on the soft colour of its kind. A status is never told by the colour alone.
///
/// @param kind `good`, `warning`, `critical`, `note`, `reused` or `skipped`: the icon and the class `is-<kind>`.
void writePill(ReportMarkup& out, const char* kind, const QString& text);

/// A term with what it means: the word, and its explanation as the tip of an element a reader can point at or reach
/// with the keyboard.
void writeTerm(ReportMarkup& out, const QString& term, const QString& meaning);

/// A chart inside the container that lets a wide one scroll sideways instead of shrinking: the container can be
/// reached with the keyboard and says what it holds.
void writeScrollingChart(ReportMarkup& out, const QString& chart, const QString& label);

/// The master a light belongs to, as a link to the master's file where the document has it and as its name
/// otherwise.
void writeMasterLink(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                     const QString& text);

/// A sentence with the values that stand in it set in bold: the pattern holds `%1`, `%2` and so on where they
/// stand, and arrives translated.
void writeSentence(ReportMarkup& out, const QString& pattern, const QStringList& values);

/// One column of a table.
struct ReportColumn
{
    QString heading;

    /// Whether the column holds figures, which are set to the right.
    bool right = false;

    /// What the heading means, as its tip; may be empty.
    QString meaning;
};

/// Opens a table inside the container that lets a wide one scroll sideways, writes its headings and leaves its body
/// open for the rows.
///
/// @param cssClass a class of the table; may be empty.
/// @param label what the table holds, said to a reader who cannot see it.
void openTable(ReportMarkup& out, const QString& cssClass, const QString& label,
               const std::vector<ReportColumn>& columns);

/// Closes a table `openTable` opened.
void closeTable(ReportMarkup& out);

/// The words that stand between two parts of one line: a middle dot with a space on each side.
[[nodiscard]] QString dotSeparator();

// ---- The header and the summary (ReportHtmlSummary.cpp).

/// The anchors the header and the summary write for a model.
[[nodiscard]] QStringList summaryAnchors(const ReportModel& model);

/// The header: the title, how the run ended, the verdict, four figures and the session's notes.
[[nodiscard]] ReportMarkup writeReportHeader(const ReportDocumentContext& context);

/// Part 1: what needs attention, what was made, the lights used and left out, the nights and the lights' quality.
[[nodiscard]] ReportPart writeReportSummary(const ReportDocumentContext& context);
} // namespace ImageIntegration::Reporting

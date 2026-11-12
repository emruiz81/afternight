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
///
/// Each fact has one home. The header and the summary state the run's figures; a master's file states that
/// master's; what several masters share - a calibration master, a registration group - is stated once, in the part
/// of that name, and a master's file links to it instead of repeating it.

#include "processes/ImageIntegration/reporting/ReportAssets.h"
#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QString>

#include <optional>
#include <utility>
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

    /// What the document calls each master, by the master's key (`masterLabels`).
    QHash<QString, QString> labels;

    [[nodiscard]] bool hasAnchor(const QString& anchor) const
    {
        return anchors.contains(anchor);
    }

    /// What the document calls a master: its filter where that tells it apart, its full name otherwise.
    [[nodiscard]] QString labelOf(const ReportMaster& master) const
    {
        return labels.value(master.key, master.name);
    }

    /// A sentence the run wrote, as the document prints it: a group's key inside it replaced by the group's name,
    /// and the folders an embedded document must not print left out.
    [[nodiscard]] QString said(const QString& text) const;
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

/// Words as a link to an anchor where the document has it, and as the words alone otherwise.
void writeAnchorLink(ReportMarkup& out, const ReportDocumentContext& context, const QString& anchor,
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

/// Opens a part: its `section`, and its heading with the part's number over it and what the part holds under it.
void openPart(ReportMarkup& out, const char* anchor, int number, const QString& title, const QString& lead);

/// Opens a block of a part: a `section` of the given anchor with its title, a few words beside the title and a
/// line under it. The last two may be empty.
void openBlock(ReportMarkup& out, const char* anchor, const QString& title, const QString& aside, const QString& lead);

/// Opens a card: a `div` with its title and a few words beside it.
void openCard(ReportMarkup& out, const QString& title, const QString& aside = {});

/// Opens a panel inside a card or a master's file: a `div` on the page's colour, with its title and a few words
/// beside it. A panel with an empty title has no heading.
///
/// @param heading the element of the title - `h5`, `h6` - one level under the heading the panel stands under.
void openPanel(ReportMarkup& out, const char* heading, const QString& title, const QString& aside = {});

/// A line of small print under a chart or a table.
void writeCaption(ReportMarkup& out, const QString& text);

/// Opens a section a reader unfolds: a `details` with its title and a few words beside it.
void openFold(ReportMarkup& out, const QString& title, const QString& aside = {});

/// Closes a section `openFold` opened.
void closeFold(ReportMarkup& out);

/// One row of a list of facts: a name and its value.
struct ReportFact
{
    QString name;
    QString value;

    /// What the name means, as its tip; may be empty.
    QString meaning;
};

/// A list of facts, a name and a value a row. A fact with an empty value is left out; a list with none is not
/// written.
void writeFacts(ReportMarkup& out, const std::vector<ReportFact>& facts);

/// The same list written row by row, for a value that is more than text: `openFacts`, then for each row either
/// `writeFact` or `openFact`, the value's markup and `closeFact`, then `closeFacts`.
void openFacts(ReportMarkup& out);
void writeFact(ReportMarkup& out, const QString& name, const QString& value, const QString& meaning = {});
void openFact(ReportMarkup& out, const QString& name, const QString& meaning = {});
void closeFact(ReportMarkup& out);
void closeFacts(ReportMarkup& out);

// ---- Settings (ReportHtmlWriter.cpp).

/// The values most groups of one stage use, by block and setting. A value more than half of the stage's groups use
/// is the common one; with no such value - two groups that differ, or every group its own - there is none.
class ReportSettingsMajority
{
public:
    /// Counts one group of the stage in.
    void add(const std::vector<ReportSettingsBlock>& blocks);

    /// How many groups were counted.
    [[nodiscard]] int groups() const noexcept
    {
        return m_groups;
    }

    /// The value most groups use for a setting, or nothing when there is none.
    [[nodiscard]] std::optional<QString> common(const QString& block, const QString& setting) const;

private:
    int m_groups = 0;
    QHash<QString, QHash<QString, int>> m_uses;
};

/// One group's settings of one step: a titled list of a name and a value a row. A value that most groups of the
/// stage do not use carries the mark `≠` and says in its tip what they use; a value the application decided says
/// what was asked for and why.
///
/// @param title the list's title; the block's own when empty.
/// @param heading the element of the title - `h4`, `h5`, `h6` - one level under the heading the list stands under.
/// @param majority the stage's common values; null marks nothing.
void writeSettingsBlock(ReportMarkup& out, const ReportDocumentContext& context, const ReportSettingsBlock& block,
                        const QString& title, const char* heading, const ReportSettingsMajority* majority);

/// The same list written piece by piece, for one that begins with rows of its own: `openSettings`, any
/// `writeSettingLink` and `writeSettingRows`, then `closeSettings`.
void openSettings(ReportMarkup& out, const QString& title, const char* heading);
void writeSettingRows(ReportMarkup& out, const ReportDocumentContext& context, const ReportSettingsBlock& block,
                      const ReportSettingsMajority* majority);

/// A row whose value leads somewhere: a link to the anchor where the document has it, with a few words after it.
/// A row with no words to show prints `none` in the place of the value.
void writeSettingLink(ReportMarkup& out, const ReportDocumentContext& context, const QString& name,
                      const QString& anchor, const QString& text, const QString& note = {});
void closeSettings(ReportMarkup& out);

/// A title inside a panel, as `openPanel` writes the panel's own.
void writePanelTitle(ReportMarkup& out, const char* heading, const QString& title, const QString& aside = {});

// ---- The groups of a model, by key (ReportHtmlWriter.cpp).

[[nodiscard]] const ReportMaster* masterOfKey(const ReportModel& model, const QString& key);
[[nodiscard]] const ReportLightGroup* lightGroupOfKey(const ReportModel& model, const QString& key);
[[nodiscard]] const ReportRegistrationGroup* registrationOfKey(const ReportModel& model, const QString& key);
[[nodiscard]] const ReportCalibrationMaster* calibrationOfSlug(const ReportModel& model, const QString& slug);

/// The line that says what the mark `≠` means.
void writeMarkKey(ReportMarkup& out, const QString& text);

// ---- Lights and masters, in words (ReportHtmlWriter.cpp).

/// A light's state as a chart marks it. A light whose fate the run did not record is drawn as a light: a chart has
/// no mark for "not recorded", and the tables beside it state what is known.
[[nodiscard]] ChartState chartStateOf(const ReportFrame& frame);

/// What a state is called.
[[nodiscard]] QString stateName(ChartState state);

/// The class of a state's swatch.
[[nodiscard]] QString stateClass(ChartState state);

/// A swatch of a state's colour and the state's name.
void writeStateName(ReportMarkup& out, ChartState state);

/// What became of a light, in a line: its state and, where the state has one, its reason.
[[nodiscard]] QString fateLine(const ReportFrame& frame);

/// Whether a light is turned against its reference by enough to say so: more than a degree. Every light is turned
/// by a fraction of one; a light taken after a meridian flip by half a turn.
[[nodiscard]] bool isTurned(const ReportFrame& frame);

/// A word or two, and the kind of pill that says them. A null kind is nothing to say.
struct ReportStateWords
{
    const char* kind = nullptr;
    QString text;
};

/// What became of a master, for one the run did not make; nothing for one it made.
[[nodiscard]] ReportStateWords masterStateWords(ReportMasterState state);

/// Where a master's astrometric solution came from; nothing when the run recorded nothing about it.
[[nodiscard]] ReportStateWords astrometryWords(ReportAstrometryOutcome outcome);

/// How a run ended, in a word or two, and the kind of pill that says it.
[[nodiscard]] ReportStateWords outcomeWords(ReportOutcome outcome);

/// What became of a calibration master, in a word a line of small print takes; nothing for one the run made.
[[nodiscard]] QString calibrationStateNote(ReportCalibrationState state);

/// What the document calls each master: its filter, with its target before it in a run of several, and its full
/// name where two masters would otherwise be called the same.
[[nodiscard]] QHash<QString, QString> masterLabels(const ReportModel& model);

/// How many of a master's lights the stack combined, and their exposure; nothing when the run recorded no light's
/// fate for it.
struct MasterUse
{
    int taken = 0;
    std::optional<int> used;
    std::optional<qint64> integratedMs;
};

[[nodiscard]] MasterUse useOf(const ReportMaster& master);

/// Whether a master's file is on disk: the run made it, or it was still valid.
[[nodiscard]] bool isThere(const ReportMaster& master);

/// Whether the run fell short of what it planned: it failed, was cancelled, or left a master unmade - whether it
/// stopped there or went on with its other masters. A run in which a light failed and every master was made did not.
[[nodiscard]] bool runStopped(const ReportModel& model);

/// A value that may not be stated, as a chart takes it: not a number where there is none.
[[nodiscard]] double valueOf(const std::optional<double>& value);

/// The median of some values, or nothing when none is a number.
[[nodiscard]] std::optional<double> medianOfValues(std::vector<double> values);

/// A share as a percentage: `96.6 %`.
[[nodiscard]] QString percentText(double fraction);

/// The time of day of an instant, in the archive's zone: `21:40`.
[[nodiscard]] QString clockText(const QDateTime& instant, const ReportTimeZone& zone);

/// Minutes from the midnight that begins a night's date to an instant, in the archive's zone.
[[nodiscard]] double minutesIntoNight(const QDate& night, const QDateTime& instant, const ReportTimeZone& zone);

/// The shift of each light of some masters against its registration reference, a series per night, with the legend
/// that names the nights. Nothing is returned when no light states a shift.
struct ReportDither
{
    QString chart;
    QString legend;
};

[[nodiscard]] std::optional<ReportDither> ditherOf(const ReportDocumentContext& context,
                                                   const std::vector<const ReportMaster*>& masters, int size);

// ---- The header and the summary (ReportHtmlSummary.cpp).

/// The anchors the header and the summary write for a model.
[[nodiscard]] QStringList summaryAnchors(const ReportModel& model);

/// The header: the title, how the run ended, the verdict, four figures and the session's notes.
[[nodiscard]] ReportMarkup writeReportHeader(const ReportDocumentContext& context);

/// Part 1: what needs attention, what was made, the lights used and left out, the nights and the lights' quality.
[[nodiscard]] ReportPart writeReportSummary(const ReportDocumentContext& context);

// ---- Master by master (ReportHtmlMaster.cpp).

/// The anchor of a master's file.
[[nodiscard]] QString masterFileAnchor(const ReportMaster& master);

/// The anchors part 2 writes for a model.
[[nodiscard]] QStringList mastersAnchors(const ReportModel& model);

/// Part 2: a file per master - the picture, the stack, its lights, how it was made - or, for a master the run did
/// not make, why.
[[nodiscard]] ReportPart writeReportMasters(const ReportDocumentContext& context);

// ---- Against earlier runs (ReportHtmlEarlier.cpp).

/// The anchors part 3 writes for a model.
[[nodiscard]] QStringList earlierAnchors(const ReportModel& model);

/// Part 3: each master beside the latest earlier run that made it, what changed since the latest earlier run that
/// completed, and the runs of the folder; one sentence when there is no earlier run. A run that made no master to
/// compare and left some as they were says so under the part's heading, naming them.
[[nodiscard]] ReportPart writeReportEarlier(const ReportDocumentContext& context);

// ---- What the masters share (ReportHtmlShared.cpp).

/// The anchors part 4 writes for a model.
[[nodiscard]] QStringList sharedAnchors(const ReportModel& model);

/// Part 4: which groups made each master, the calibration masters and the registration groups, each stated once.
[[nodiscard]] ReportPart writeReportShared(const ReportDocumentContext& context);

// ---- The run (ReportHtmlRun.cpp).

/// The anchors part 5 writes for a model.
[[nodiscard]] QStringList runAnchors(const ReportModel& model);

/// Part 5: the stages on a time axis, what was left to the application, the settings stated once, what the run
/// used and wrote, and its diagnostics.
[[nodiscard]] ReportPart writeReportRun(const ReportDocumentContext& context);

// ---- Provenance (ReportHtmlProvenance.cpp).

/// The anchors part 6 writes for a model.
[[nodiscard]] QStringList provenanceAnchors(const ReportModel& model);

/// Part 6: how the lights were taken, with what, by which software, from which files, and what the report could
/// not state.
[[nodiscard]] ReportPart writeReportProvenance(const ReportDocumentContext& context);
} // namespace ImageIntegration::Reporting

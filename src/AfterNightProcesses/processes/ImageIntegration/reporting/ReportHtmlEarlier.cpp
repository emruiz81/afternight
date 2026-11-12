// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The third part of the report's document: the run against the earlier runs of its output folder.
///
/// Three blocks, each present only when the model has something to say in it. "Before and after" sets each master
/// the run made beside the latest earlier run that made it: the two runs' archived pictures under a divider a reader
/// drags, or side by side when the frames are of different shapes, with the figures of both and each change in
/// words. "What changed" gives the reasons a master can differ: the run's totals, the settings that differ in the
/// groups both runs have, the groups only one has, the frames gained and lost, and the application. "Over the runs"
/// lists every earlier run of the folder with this one.
///
/// A master the run found still valid is the earlier run's own and is compared with nothing. Such masters are named,
/// with that reason, under the comparison; a run that set no master beside an earlier run has no comparison, so
/// there the part's head names them.
///
/// Every figure of an earlier run is the one its stored report states, and both sides of a comparison are read by
/// the same functions (`ReportFigures.h`). The change between two figures is put in words here, where the document
/// is written; the document's script moves the divider and computes nothing. With no earlier run the part is one
/// sentence.

#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportFigures.h"
#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace ImageIntegration::Reporting
{
namespace
{
/// How many masters one comparison switches between: the stylesheet has a rule per place. A run with more is given
/// a second comparison.
constexpr std::size_t kComparedAtMost = 12;

/// The most earlier runs one master is set beside, one at a time: the stylesheet has a rule per place.
constexpr std::size_t kComparedRunsAtMost = 6;

/// How many runs the charts of the folder's runs draw: the latest ones, this run last. The table lists every run.
constexpr std::size_t kRunsDrawnAtMost = 8;

/// How many file names a change of inputs prints before it says how many more there are.
constexpr qsizetype kNamesShownAtMost = 3;

/// Which blocks of the part a model calls for.
struct EarlierPlan
{
    bool beforeAfter = false;
    bool changed = false;
    bool history = false;
};

[[nodiscard]] EarlierPlan planEarlier(const ReportModel& model)
{
    EarlierPlan plan;
    plan.beforeAfter = std::any_of(model.masters.begin(), model.masters.end(),
                                   [](const ReportMaster& master) { return master.baseline.has_value(); });
    plan.changed = model.comparison.has_value();
    plan.history = !model.history.empty();
    return plan;
}

/// When a run was archived, as a reader meets it; the run's id where its report states no time.
[[nodiscard]] QString whenText(const std::optional<QDateTime>& archivedAt, const QString& runId,
                               const ReportDocumentContext& context)
{
    return archivedAt.has_value() && archivedAt->isValid() ? formatDateTime(*archivedAt, context.zone) : runId;
}

/// What a run is named by: when it was archived, and its id.
struct RunStamp
{
    std::optional<QDateTime> archivedAt;
    QString runId;
};

/// The seconds that follow each run's name where some runs stand together: the runs a master is switched between,
/// the rows of the folder's runs, the columns of its charts. A run is named by when it was archived, to the minute
/// (`whenText`), and two runs of one minute would read alike. Where two of the runs would, every one that states a
/// time is named to the second, `:47` after its minute; otherwise, and for a run that states no time, nothing
/// follows its name.
[[nodiscard]] QStringList secondsWhereAlike(const std::vector<RunStamp>& runs, const ReportDocumentContext& context)
{
    QSet<QString> names;
    for (const RunStamp& run : runs)
    {
        names.insert(whenText(run.archivedAt, run.runId, context));
    }
    const bool alike = names.size() != static_cast<qsizetype>(runs.size());
    QStringList seconds;
    for (const RunStamp& run : runs)
    {
        const bool timed = run.archivedAt.has_value() && run.archivedAt->isValid();
        seconds << (alike && timed ? run.archivedAt->toTimeZone(context.zone.resolved()).toString(QStringLiteral(":ss"))
                                   : QString());
    }
    return seconds;
}

// ---- A change, in words.

/// Whether a change is for the better, for the worse, or neither.
enum class Sense
{
    Neither,
    Better,
    Worse
};

/// A change between two figures: its words, and whether it is for the better. Empty words are no change to state,
/// since one of the two runs does not state the figure.
struct Change
{
    QString text;
    Sense sense = Sense::Neither;
};

/// Which way a figure is better.
enum class Better
{
    More,
    Less,
    Neither
};

[[nodiscard]] Sense senseOf(const bool more, const Better better)
{
    if (better == Better::Neither)
    {
        return Sense::Neither;
    }
    return more == (better == Better::More) ? Sense::Better : Sense::Worse;
}

[[nodiscard]] Change noChange()
{
    return Change{.text = documentText("No change"), .sense = Sense::Neither};
}

/// A change of a count: `+43`, `−2`.
[[nodiscard]] Change countChange(const std::optional<qint64>& before, const std::optional<qint64>& after,
                                 const Better better)
{
    if (!before.has_value() || !after.has_value())
    {
        return {};
    }
    if (*before == *after)
    {
        return noChange();
    }
    const qint64 difference = *after - *before;
    return Change{.text = (difference > 0 ? QStringLiteral("+") : QString()) + formatCount(difference),
                  .sense = senseOf(difference > 0, better)};
}

/// A change of a duration: `+2 h 09 m`.
[[nodiscard]] Change durationChange(const std::optional<qint64>& before, const std::optional<qint64>& after,
                                    const Better better)
{
    if (!before.has_value() || !after.has_value())
    {
        return {};
    }
    if (*before == *after)
    {
        return noChange();
    }
    const qint64 difference = *after - *before;
    return Change{.text = (difference > 0 ? QStringLiteral("+") : QString()) + formatDurationMs(difference),
                  .sense = senseOf(difference > 0, better)};
}

/// A change of a size: `+12.4 MB`.
[[nodiscard]] Change sizeChange(const qint64 before, const qint64 after)
{
    if (before == after)
    {
        return noChange();
    }
    const qint64 difference = after - before;
    return Change{.text = (difference > 0 ? QStringLiteral("+") : QString()) + formatByteSize(difference),
                  .sense = Sense::Neither};
}

/// A change of a measured figure, as a share of what it was: `12.4 % lower`.
[[nodiscard]] Change relativeChange(const std::optional<double>& before, const std::optional<double>& after,
                                    const Better better)
{
    if (!before.has_value() || !after.has_value() || !std::isfinite(*before) || !std::isfinite(*after))
    {
        return {};
    }
    if (*before == *after)
    {
        return noChange();
    }
    const bool more = *after > *before;
    if (*before == 0.0)
    {
        return Change{.text = more ? documentText("Higher") : documentText("Lower"), .sense = senseOf(more, better)};
    }
    const double share = 100.0 * std::abs(*after - *before) / std::abs(*before);
    return Change{.text = (more ? documentText("%1 % higher") : documentText("%1 % lower")).arg(formatFigure(share)),
                  .sense = senseOf(more, better)};
}

/// A change of a measured figure, as the difference in its own unit: `0.07 px lower`.
///
/// @param unit the unit with the space before it, or empty.
[[nodiscard]] Change absoluteChange(const std::optional<double>& before, const std::optional<double>& after,
                                    const QString& unit, const Better better)
{
    if (!before.has_value() || !after.has_value() || !std::isfinite(*before) || !std::isfinite(*after))
    {
        return {};
    }
    if (*before == *after)
    {
        return noChange();
    }
    const bool more = *after > *before;
    return Change{.text = (more ? documentText("%1 higher") : documentText("%1 lower"))
                              .arg(formatFigure(std::abs(*after - *before)) + unit),
                  .sense = senseOf(more, better)};
}

/// A change in a table's cell: its words and, where it is for the better or the worse, an icon and the word, so the
/// colour is never what says it.
void writeChange(ReportMarkup& out, const Change& change)
{
    if (change.text.isEmpty())
    {
        out.element("td", {{"class", QStringLiteral("muted")}}, noValueText());
        return;
    }
    out.open("td");
    switch (change.sense)
    {
    case Sense::Better:
        out.open("span", {{"class", QStringLiteral("chg is-good")}});
        out.icon("good");
        out.element("span", change.text);
        out.element("small", documentText("better"));
        out.close("span");
        break;
    case Sense::Worse:
        out.open("span", {{"class", QStringLiteral("chg is-bad")}});
        out.icon("warning");
        out.element("span", change.text);
        out.element("small", documentText("worse"));
        out.close("span");
        break;
    case Sense::Neither:
        out.element("span", {{"class", QStringLiteral("chg")}}, change.text);
        break;
    }
    out.close("td");
}

/// One row of a comparison of figures: the figure's name, its value in the earlier run and in this one, and the
/// change.
struct FigureRow
{
    QString name;
    QString before;
    QString after;
    Change change;

    /// What the figure is, as the tip of its name; may be empty.
    QString meaning;
};

void writeFigureRows(ReportMarkup& out, const std::vector<FigureRow>& rows)
{
    for (const FigureRow& row : rows)
    {
        out.open("tr");
        out.open("td");
        if (row.meaning.isEmpty())
        {
            out.text(row.name);
        }
        else
        {
            writeTerm(out, row.name, row.meaning);
        }
        out.close("td");
        out.element("td", {{"class", QStringLiteral("r num")}}, row.before);
        out.open("td", {{"class", QStringLiteral("r num")}});
        out.element("b", row.after);
        out.close("td");
        writeChange(out, row.change);
        out.close("tr");
    }
}

[[nodiscard]] std::optional<qint64> wide(const std::optional<int>& value)
{
    return value.has_value() ? std::optional<qint64>(*value) : std::nullopt;
}

// ---- Before and after.

/// The lights a stack combined of the lights its group took: `45 of 47`.
[[nodiscard]] QString lightsText(const ReportBaseline& figures)
{
    if (figures.frames.has_value() && figures.framesTaken.has_value())
    {
        return documentText("%1 of %2").arg(formatCount(*figures.frames), formatCount(*figures.framesTaken));
    }
    if (figures.frames.has_value())
    {
        return formatCount(*figures.frames);
    }
    return noValueText();
}

/// The figures of a master in an earlier run and in this one. A figure neither run states has no row.
[[nodiscard]] std::vector<FigureRow> masterFigureRows(const ReportBaseline& before, const ReportBaseline& now)
{
    std::vector<FigureRow> rows;
    if (before.frames.has_value() || now.frames.has_value())
    {
        rows.push_back({documentText("Lights used"), lightsText(before), lightsText(now),
                        countChange(wide(before.frames), wide(now.frames), Better::More),
                        documentText("The lights the stack combined, of the lights the group took.")});
    }
    if (before.integrationMs.has_value() || now.integrationMs.has_value())
    {
        rows.push_back({documentText("Integration"),
                        before.integrationMs.has_value() ? formatDurationMs(*before.integrationMs) : noValueText(),
                        now.integrationMs.has_value() ? formatDurationMs(*now.integrationMs) : noValueText(),
                        durationChange(before.integrationMs, now.integrationMs, Better::More),
                        documentText("The exposure of the lights the stack combined.")});
    }
    if (before.backgroundNoise.has_value() || now.backgroundNoise.has_value())
    {
        rows.push_back({documentText("Noise"), formatFigure(before.backgroundNoise), formatFigure(now.backgroundNoise),
                        relativeChange(before.backgroundNoise, now.backgroundNoise, Better::Less),
                        documentText("The background noise measured on the linear master, as a fraction of its "
                                     "display range. Lower is better.")});
    }
    if (before.snr.has_value() || now.snr.has_value())
    {
        rows.push_back({documentText("Signal to noise"), formatFigure(before.snr), formatFigure(now.snr),
                        relativeChange(before.snr, now.snr, Better::More),
                        documentText("The master's mean level over its background noise. Higher is better.")});
    }
    if (before.fwhm.has_value() || now.fwhm.has_value())
    {
        const auto pixels = [](const std::optional<double>& width) {
            return width.has_value() ? documentText("%1 px").arg(formatFigure(*width)) : noValueText();
        };
        rows.push_back({documentText("Median FWHM"), pixels(before.fwhm), pixels(now.fwhm),
                        absoluteChange(before.fwhm, now.fwhm, documentText(" px"), Better::Less),
                        documentText("The median star width of the lights the stack combined. Lower is sharper.")});
    }
    if (before.effectiveFrames.has_value() || now.effectiveFrames.has_value())
    {
        rows.push_back({documentText("Effective frames"), formatFigure(before.effectiveFrames),
                        formatFigure(now.effectiveFrames),
                        absoluteChange(before.effectiveFrames, now.effectiveFrames, QString(), Better::More),
                        documentText("How many lights of equal weight and no rejection would give the same noise.")});
    }
    if (before.rejectedFraction.has_value() || now.rejectedFraction.has_value())
    {
        const auto share = [](const std::optional<double>& fraction) {
            return fraction.has_value() ? percentText(*fraction) : noValueText();
        };
        const auto points = [](const std::optional<double>& fraction) {
            return fraction.has_value() ? std::optional<double>(100.0 * *fraction) : std::nullopt;
        };
        rows.push_back({documentText("Samples rejected"), share(before.rejectedFraction), share(now.rejectedFraction),
                        absoluteChange(points(before.rejectedFraction), points(now.rejectedFraction),
                                       documentText(" points"), Better::Neither),
                        documentText("The share of all samples the stack rejected. Neither more nor less is better "
                                     "by itself: it follows the lights and the rejection's settings.")});
    }
    return rows;
}

/// The pictures of one master in the two runs, as the document's form gives them.
struct ComparedPictures
{
    QString now;
    QString before;
    QString nowDetail;
    QString beforeDetail;

    /// Whether the earlier run's pictures are of its master under this run's display stretch, and not that run's
    /// own archived previews under its own.
    bool sharedStretch = false;

    /// Whether both runs have the whole frame, so there is something to set side by side.
    [[nodiscard]] bool arePaired() const
    {
        return !now.isEmpty() && !before.isEmpty();
    }

    /// Whether both runs have the detail too.
    [[nodiscard]] bool haveDetail() const
    {
        return arePaired() && !nowDetail.isEmpty() && !beforeDetail.isEmpty();
    }
};

[[nodiscard]] ComparedPictures picturesOf(const ReportDocumentContext& context, const ReportMaster& master,
                                          const ReportBaseline& baseline)
{
    ComparedPictures pictures;
    if (!baseline.made)
    {
        return pictures;
    }
    // Shown whole and at the divider's size: the pictures are the document's full ones, not its thumbnails. Where
    // the earlier master was pictured under this run's stretch that picture is the one shown, and its detail with
    // it or none: a detail under another stretch beside it would undo what the pair is for.
    pictures.sharedStretch = !baseline.matchedPreview.isEmpty();
    pictures.now = context.assets.image(master.preview);
    pictures.before = context.assets.image(pictures.sharedStretch ? baseline.matchedPreview : baseline.preview);
    pictures.nowDetail = context.assets.image(master.detail);
    pictures.beforeDetail = context.assets.image(pictures.sharedStretch ? baseline.matchedDetail : baseline.detail);
    return pictures;
}

[[nodiscard]] ComparedPictures picturesOf(const ReportDocumentContext& context, const ReportMaster& master)
{
    return master.baseline.has_value() ? picturesOf(context, master, *master.baseline) : ComparedPictures{};
}

/// The earlier runs a master is set beside, the latest first: the latest run that made it and, in the linked
/// form, the runs that made it before, whose pictures are in those runs' own folders. The form that is one file
/// carries the latest alone: every earlier run's pictures inside it would make it several times its size.
[[nodiscard]] std::vector<const ReportBaseline*> comparedRuns(const ReportDocumentContext& context,
                                                              const ReportMaster& master)
{
    std::vector<const ReportBaseline*> runs{&*master.baseline};
    if (context.assets.form() == ReportForm::Linked)
    {
        for (const ReportBaseline& earlier : master.earlierBaselines)
        {
            if (runs.size() == kComparedRunsAtMost)
            {
                break;
            }
            runs.push_back(&earlier);
        }
    }
    return runs;
}

/// One picture of a comparison: the whole frame and, where both runs have it, the detail the switch shows in its
/// place.
void writeComparedPicture(ReportMarkup& out, const QString& whole, const QString& detail, const bool withDetail,
                          const QString& words, const QString& detailWords)
{
    out.leaf("img",
             {{"class", QStringLiteral("whole")}, {"src", whole}, {"alt", words}, {"loading", QStringLiteral("lazy")}});
    if (withDetail)
    {
        out.leaf("img", {{"class", QStringLiteral("detail")},
                         {"src", detail},
                         {"alt", detailWords},
                         {"loading", QStringLiteral("lazy")}});
    }
}

/// The two runs' pictures of one master: under one divider where the frames are of one shape, side by side where
/// they are not.
void writePair(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
               const ReportBaseline& baseline, const ComparedPictures& pictures)
{
    const QString label = context.labelOf(master);
    const QString when = whenText(baseline.archivedAt, baseline.runId, context);
    const bool detail = pictures.haveDetail();
    const QString nowWords = documentText("%1 in this run, stretched preview").arg(label);
    const QString nowDetailWords = documentText("%1 in this run, the centre at full scale").arg(label);
    const QString beforeWords = documentText("%1 in the run of %2, stretched preview").arg(label, when);
    const QString beforeDetailWords = documentText("%1 in the run of %2, the centre at full scale").arg(label, when);

    if (baseline.sideBySide)
    {
        out.open("div", {{"class", detail ? QStringLiteral("apart has-detail") : QStringLiteral("apart")}});
        out.open("figure");
        writeComparedPicture(out, pictures.before, pictures.beforeDetail, detail, beforeWords, beforeDetailWords);
        out.element("figcaption", when);
        out.close("figure");
        out.open("figure");
        writeComparedPicture(out, pictures.now, pictures.nowDetail, detail, nowWords, nowDetailWords);
        out.element("figcaption", documentText("This run"));
        out.close("figure");
        out.close("div");
        writeCaption(out, documentText("The two frames are of different shapes, so they stand side by side. Each "
                                       "picture has its own display stretch; the figures are measured on the "
                                       "linear masters."));
        return;
    }

    out.open("div", {{"class", detail ? QStringLiteral("ba has-detail") : QStringLiteral("ba")},
                     {"data-compare", QStringLiteral("")}});
    writeComparedPicture(out, pictures.now, pictures.nowDetail, detail, nowWords, nowDetailWords);
    out.open("div", {{"class", QStringLiteral("before")}});
    writeComparedPicture(out, pictures.before, pictures.beforeDetail, detail, beforeWords, beforeDetailWords);
    out.close("div");
    out.element("span", {{"class", QStringLiteral("tag l")}}, when);
    out.element("span", {{"class", QStringLiteral("tag r")}}, documentText("This run"));
    out.element("span", {{"class", QStringLiteral("handle")}}, QString());
    out.leaf("input", {{"type", QStringLiteral("range")},
                       {"min", QStringLiteral("0")},
                       {"max", QStringLiteral("100")},
                       {"value", QStringLiteral("50")},
                       {"aria-label", documentText("Divider between the earlier run and this run of %1").arg(label)}});
    out.element("span", {{"class", QStringLiteral("ring")}}, QString());
    out.close("div");
    writeCaption(out,
                 pictures.sharedStretch
                     ? documentText("Left of the divider is the earlier run, right of it this one; drag it. The "
                                    "earlier master is pictured under this run's display stretch, so a difference "
                                    "between the two is the masters'; the figures are measured on the linear "
                                    "masters.")
                     : documentText("Left of the divider is the earlier run, right of it this one; drag it. Each "
                                    "picture is its own run's archived preview, under its own display stretch; the "
                                    "figures are measured on the linear masters."));
}

/// One master beside one earlier run: the two runs' pictures and the figures of both.
void writeComparedRun(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                      const ReportBaseline& baseline, const ComparedPictures& pictures, const QString& classes)
{
    const QString label = context.labelOf(master);
    out.open("div", {{"class", classes}});
    out.open("div");
    if (pictures.arePaired())
    {
        writePair(out, context, master, baseline, pictures);
    }
    else
    {
        out.element("p", {{"class", QStringLiteral("said muted")}},
                    documentText("One of the two runs archived no picture of this master, so only the figures are "
                                 "compared."));
    }
    out.close("div");

    out.open("div", {{"class", QStringLiteral("figs")}});
    const std::vector<FigureRow> rows = masterFigureRows(baseline, masterFigures(master));
    if (rows.empty())
    {
        out.element("p", {{"class", QStringLiteral("said muted")}},
                    documentText("Neither run states a figure of this master."));
    }
    else
    {
        openTable(out, QStringLiteral("figures"), documentText("%1 in the earlier run and in this one").arg(label),
                  {{label},
                   {whenText(baseline.archivedAt, baseline.runId, context), true},
                   {documentText("This run"), true},
                   {documentText("Change")}});
        writeFigureRows(out, rows);
        closeTable(out);
    }
    out.close("div");
    out.close("div");
}

/// One master of a comparison: its heading, the two runs' pictures and the figures of both; or, for a master no
/// earlier run made, that it is new. A master several earlier runs made is set beside one of them at a time: a
/// radio button per run picks which, the latest to begin with, with no script.
void writeComparedMaster(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                         const std::size_t index, const ComparedPictures& pictures, const QString& comparison)
{
    const ReportBaseline& baseline = *master.baseline;
    const QString label = context.labelOf(master);
    const std::vector<const ReportBaseline*> runs =
        baseline.made ? comparedRuns(context, master) : std::vector<const ReportBaseline*>{};

    out.open("div", {{"class", QStringLiteral("cmpitem cmpitem-%1").arg(index)}});
    out.open("h4", {{"class", QStringLiteral("cmphead")}});
    writeMasterLink(out, context, master, label);
    out.text(QStringLiteral(" "));
    out.element("small", !baseline.made    ? documentText("new in this run")
                         : runs.size() > 1 ? documentText("an earlier run against this run")
                                           : documentText("the run of %1 against this run")
                                                 .arg(whenText(baseline.archivedAt, baseline.runId, context)));
    out.close("h4");

    if (!baseline.made)
    {
        out.open("div", {{"class", QStringLiteral("newmaster")}});
        out.open("p");
        out.element("b", documentText("%1 is new in this run.").arg(label));
        out.text(QStringLiteral(" "));
        out.text(documentText("No earlier run of this folder that keeps a report made it, so there is nothing to "
                              "compare it with."));
        out.close("p");
        out.close("div");
        out.close("div");
        return;
    }

    if (runs.size() == 1)
    {
        writeComparedRun(out, context, master, baseline, pictures, QStringLiteral("cmpgrid"));
        out.close("div");
        return;
    }

    // The radio buttons stand before the runs they show, inside the master's own item: the stylesheet's rules
    // need them there.
    const QString name = QStringLiteral("%1-%2-r").arg(comparison).arg(index);
    for (std::size_t run = 0; run < runs.size(); ++run)
    {
        out.leaf("input", {{"type", QStringLiteral("radio")},
                           {"class", QStringLiteral("run run-%1").arg(run)},
                           {"name", name},
                           {"id", QStringLiteral("%1-%2").arg(name).arg(run)},
                           {"checked", run == 0 ? QStringLiteral("checked") : QString()}});
    }
    // A run is named by when it was archived, to the minute. Two runs of one minute would read alike, so the runs
    // of such a switch are named to the second.
    std::vector<RunStamp> stamps;
    stamps.reserve(runs.size());
    for (const ReportBaseline* run : runs)
    {
        stamps.push_back({run->archivedAt, run->runId});
    }
    const QStringList seconds = secondsWhereAlike(stamps, context);
    QStringList names;
    for (std::size_t run = 0; run < runs.size(); ++run)
    {
        names << whenText(runs[run]->archivedAt, runs[run]->runId, context) + seconds.at(static_cast<qsizetype>(run));
    }
    out.open("div", {{"class", QStringLiteral("cmpbar runbar noprint")}});
    out.element("span", {{"class", QStringLiteral("lab")}}, documentText("Against the run of"));
    out.open("div", {{"class", QStringLiteral("switch against")}});
    for (std::size_t run = 0; run < runs.size(); ++run)
    {
        out.element("label", {{"for", QStringLiteral("%1-%2").arg(name).arg(run)}}, names[static_cast<qsizetype>(run)]);
    }
    out.close("div");
    out.close("div");
    for (std::size_t run = 0; run < runs.size(); ++run)
    {
        writeComparedRun(out, context, master, *runs[run],
                         run == 0 ? pictures : picturesOf(context, master, *runs[run]),
                         QStringLiteral("cmpgrid cmprun cmprun-%1").arg(run));
    }
    out.close("div");
}

/// One comparison: the masters it switches between, one shown at a time. A radio button per master picks which,
/// and two more pick the whole frame or the detail, with no script; the script only moves the divider.
void writeComparator(ReportMarkup& out, const ReportDocumentContext& context,
                     const std::vector<const ReportMaster*>& masters, const int number)
{
    std::vector<ComparedPictures> pictures;
    pictures.reserve(masters.size());
    bool anyDetail = false;
    for (const ReportMaster* master : masters)
    {
        pictures.push_back(picturesOf(context, *master));
        anyDetail = anyDetail || pictures.back().haveDetail();
        // The runs before the latest have pairs of their own, and a detail of one is a reason for the switch too.
        if (master->baseline.has_value() && master->baseline->made)
        {
            const std::vector<const ReportBaseline*> runs = comparedRuns(context, *master);
            for (std::size_t run = 1; run < runs.size() && !anyDetail; ++run)
            {
                anyDetail = picturesOf(context, *master, *runs[run]).haveDetail();
            }
        }
    }
    const QString name = QStringLiteral("cmp-%1").arg(number);
    const QString zoom = name + QStringLiteral("-z");

    out.open("div", {{"class", QStringLiteral("cmp")}});
    if (masters.size() > 1)
    {
        for (std::size_t index = 0; index < masters.size(); ++index)
        {
            out.leaf("input", {{"type", QStringLiteral("radio")},
                               {"class", QStringLiteral("pick pick-%1").arg(index)},
                               {"name", name},
                               {"id", QStringLiteral("%1-%2").arg(name).arg(index)},
                               {"checked", index == 0 ? QStringLiteral("checked") : QString()}});
        }
    }
    if (anyDetail)
    {
        for (int index = 0; index < 2; ++index)
        {
            out.leaf("input", {{"type", QStringLiteral("radio")},
                               {"class", QStringLiteral("zoom zoom-%1").arg(index)},
                               {"name", zoom},
                               {"id", QStringLiteral("%1-%2").arg(zoom).arg(index)},
                               {"checked", index == 0 ? QStringLiteral("checked") : QString()}});
        }
    }
    if (masters.size() > 1 || anyDetail)
    {
        out.open("div", {{"class", QStringLiteral("cmpbar noprint")}});
        if (masters.size() > 1)
        {
            out.element("span", {{"class", QStringLiteral("lab")}}, documentText("Master"));
            out.open("div", {{"class", QStringLiteral("switch masters")}});
            for (std::size_t index = 0; index < masters.size(); ++index)
            {
                out.element("label", {{"for", QStringLiteral("%1-%2").arg(name).arg(index)}},
                            context.labelOf(*masters[index]));
            }
            out.close("div");
        }
        if (anyDetail)
        {
            out.element("span", {{"class", QStringLiteral("lab")}}, documentText("Show"));
            out.open("div", {{"class", QStringLiteral("switch zooms")}});
            out.element("label", {{"for", zoom + QStringLiteral("-0")}}, documentText("Whole frame"));
            out.element("label", {{"for", zoom + QStringLiteral("-1")}}, documentText("Detail"));
            out.close("div");
        }
        out.close("div");
    }
    out.open("div", {{"class", QStringLiteral("cmpitems")}});
    for (std::size_t index = 0; index < masters.size(); ++index)
    {
        writeComparedMaster(out, context, *masters[index], index, pictures[index], name);
    }
    out.close("div");
    out.close("div");
}

/// The masters a comparison shows, in the comparisons they are shown in: the masters of one target together, and no
/// more in one comparison than the stylesheet switches between.
[[nodiscard]] std::vector<std::vector<const ReportMaster*>> comparedMasters(const ReportModel& model)
{
    QStringList targets;
    for (const ReportMaster& master : model.masters)
    {
        if (master.baseline.has_value() && !targets.contains(master.target))
        {
            targets.append(master.target);
        }
    }
    std::vector<std::vector<const ReportMaster*>> comparisons;
    for (const QString& target : targets)
    {
        std::vector<const ReportMaster*> masters;
        for (const ReportMaster& master : model.masters)
        {
            if (!master.baseline.has_value() || master.target != target)
            {
                continue;
            }
            if (masters.size() == kComparedAtMost)
            {
                comparisons.push_back(std::move(masters));
                masters.clear();
            }
            masters.push_back(&master);
        }
        if (!masters.empty())
        {
            comparisons.push_back(std::move(masters));
        }
    }
    return comparisons;
}

/// The masters the run found still valid, by name, and why they stand beside no earlier run: such a master is the
/// earlier run's own, so there are not two of it to compare. Empty for a run that made every master it has.
[[nodiscard]] QString reusedMastersSentence(const ReportDocumentContext& context)
{
    QStringList reused;
    for (const ReportMaster& master : context.model.masters)
    {
        if (master.state == ReportMasterState::Reused)
        {
            reused << context.labelOf(master);
        }
    }
    if (reused.isEmpty())
    {
        return {};
    }
    return counted(reused.size(),
                   QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                     "%1 master was still valid and was not made again, so it is compared with "
                                     "nothing: %2."),
                   QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                     "%1 masters were still valid and were not made again, so they are compared "
                                     "with nothing: %2."))
        .arg(reused.join(QStringLiteral(", ")));
}

void writeBeforeAfter(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const std::vector<std::vector<const ReportMaster*>> comparisons = comparedMasters(model);
    std::size_t compared = 0;
    for (const std::vector<const ReportMaster*>& masters : comparisons)
    {
        compared += masters.size();
    }

    openBlock(out, "before-after", documentText("Before and after"),
              compared == 1 ? documentText("the same master in two runs")
                            : documentText("the same master in two runs, master by master"),
              documentText("Each master this run made, beside the latest earlier run that made it."));
    const bool severalTargets = model.run.targets.size() > 1;
    int number = 0;
    for (const std::vector<const ReportMaster*>& masters : comparisons)
    {
        openCard(out, severalTargets ? masters.front()->target : QString());
        writeComparator(out, context, masters, number++);
        out.close("div");
    }

    // The masters the run found still valid are named under the comparison, with why they are not in it.
    if (const QString reused = reusedMastersSentence(context); !reused.isEmpty())
    {
        writeCaption(out, reused);
    }
    out.close("section");
}

// ---- What changed.

/// The run's totals in the earlier run and in this one.
[[nodiscard]] std::vector<FigureRow> totalRows(const ReportRunTotals& before, const ReportRunTotals& now)
{
    const auto count = [](const std::optional<int>& value) {
        return value.has_value() ? formatCount(*value) : noValueText();
    };
    const auto duration = [](const std::optional<qint64>& value) {
        return value.has_value() ? formatDurationMs(*value) : noValueText();
    };
    std::vector<FigureRow> rows;
    rows.push_back({documentText("Masters"), formatCount(before.masters), formatCount(now.masters),
                    countChange(before.masters, now.masters, Better::Neither),
                    documentText("The masters that were there when the run ended: made, or still valid.")});
    rows.push_back({documentText("Lights taken"),
                    formatCount(before.frames),
                    formatCount(now.frames),
                    countChange(before.frames, now.frames, Better::Neither),
                    {}});
    if (before.framesUsed.has_value() || now.framesUsed.has_value())
    {
        rows.push_back({documentText("Lights used"), count(before.framesUsed), count(now.framesUsed),
                        countChange(wide(before.framesUsed), wide(now.framesUsed), Better::More),
                        documentText("The lights the masters combine.")});
    }
    rows.push_back({documentText("Exposure taken"),
                    formatDurationMs(before.integrationMs),
                    formatDurationMs(now.integrationMs),
                    durationChange(before.integrationMs, now.integrationMs, Better::Neither),
                    {}});
    if (before.integratedMs.has_value() || now.integratedMs.has_value())
    {
        rows.push_back({documentText("Integration"), duration(before.integratedMs), duration(now.integratedMs),
                        durationChange(before.integratedMs, now.integratedMs, Better::More),
                        documentText("The exposure of the lights the masters combine.")});
    }
    rows.push_back({documentText("Size of the masters"),
                    formatByteSize(before.outputBytes),
                    formatByteSize(now.outputBytes),
                    sizeChange(before.outputBytes, now.outputBytes),
                    {}});
    if (before.snr.has_value() || now.snr.has_value())
    {
        rows.push_back({documentText("Signal to noise"), formatFigure(before.snr), formatFigure(now.snr),
                        relativeChange(before.snr, now.snr, Better::More),
                        documentText("The mean over the masters of each master's mean level over its background "
                                     "noise.")});
    }
    if (before.overlapRetained.has_value() || now.overlapRetained.has_value())
    {
        const auto share = [](const std::optional<double>& fraction) {
            return fraction.has_value() ? percentText(*fraction) : noValueText();
        };
        const auto points = [](const std::optional<double>& fraction) {
            return fraction.has_value() ? std::optional<double>(100.0 * *fraction) : std::nullopt;
        };
        rows.push_back({documentText("Overlap retained"), share(before.overlapRetained), share(now.overlapRetained),
                        absoluteChange(points(before.overlapRetained), points(now.overlapRetained),
                                       documentText(" points"), Better::More),
                        documentText("The mean over the masters of the share of the capture's area each master "
                                     "covers.")});
    }
    return rows;
}

/// What a group is called where a change names it: a master by the document's name for it, another group by its
/// own.
[[nodiscard]] QString groupLabel(const ReportDocumentContext& context, const QString& key, const QString& name)
{
    if (const ReportMaster* master = masterOfKey(context.model, key); master != nullptr)
    {
        return context.labelOf(*master);
    }
    return name;
}

/// The settings that changed the same way in several groups of one stage, as one row.
struct SettingRow
{
    QString stage;
    QStringList groups;
    QString setting;
    QString before;
    QString after;
};

[[nodiscard]] std::vector<SettingRow> settingRows(const ReportDocumentContext& context,
                                                  const ReportComparison& comparison)
{
    std::vector<SettingRow> rows;
    for (const ReportSettingChange& change : comparison.settingsChanged)
    {
        // A setting is named with its block where the block is not the stage itself.
        const QString setting = change.block.isEmpty() || change.block == change.stage
                                    ? change.name
                                    : change.block + dotSeparator() + change.name;
        const QString group = groupLabel(context, change.groupKey, change.group);
        const auto same = std::find_if(rows.begin(), rows.end(), [&](const SettingRow& row) {
            return row.stage == change.stage && row.setting == setting && row.before == change.before &&
                   row.after == change.after;
        });
        if (same != rows.end())
        {
            if (!group.isEmpty() && !same->groups.contains(group))
            {
                same->groups.append(group);
            }
            continue;
        }
        rows.push_back(SettingRow{.stage = change.stage,
                                  .groups = group.isEmpty() ? QStringList() : QStringList{group},
                                  .setting = setting,
                                  .before = change.before,
                                  .after = change.after});
    }
    return rows;
}

void writeSettingsChanged(ReportMarkup& out, const ReportDocumentContext& context, const ReportComparison& comparison)
{
    const std::vector<SettingRow> rows = settingRows(context, comparison);
    openCard(
        out, documentText("Settings that changed"),
        counted(comparison.settingsCompared,
                QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 setting compared, in the groups both runs have"),
                QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 settings compared, in the groups both runs have")));
    if (rows.empty())
    {
        out.element("p", documentText("No setting of the groups both runs have differs."));
    }
    else
    {
        openTable(out, QStringLiteral("changes"), documentText("The settings that differ between the two runs"),
                  {{documentText("Stage")},
                   {documentText("Groups")},
                   {documentText("Setting")},
                   {documentText("Was")},
                   {documentText("Is now")}});
        for (const SettingRow& row : rows)
        {
            out.open("tr");
            out.element("td", row.stage);
            out.element("td", row.groups.isEmpty() ? documentText("the run") : row.groups.join(QStringLiteral(", ")));
            out.element("td", row.setting);
            // A value is printed as a group's settings print it: a key inside it as the group's name, and a file
            // without its folder in the embedded form.
            out.element("td", {{"class", QStringLiteral("muted")}},
                        row.before.isEmpty() ? documentText("not stated") : context.said(row.before));
            out.open("td");
            out.element("b", row.after.isEmpty() ? documentText("not stated") : context.said(row.after));
            out.close("td");
            out.close("tr");
        }
        closeTable(out);
        const int unchanged =
            comparison.settingsCompared -
            static_cast<int>(std::count_if(
                comparison.settingsChanged.begin(), comparison.settingsChanged.end(),
                [](const ReportSettingChange& change) { return !change.before.isEmpty() && !change.after.isEmpty(); }));
        if (unchanged > 0)
        {
            writeCaption(out, counted(unchanged,
                                      QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                                        "The %1 other setting of those groups is the same."),
                                      QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                                        "The %1 other settings of those groups are the same.")));
        }
    }
    out.close("div");
}

/// Some file names in a line, the first few and how many more there are.
[[nodiscard]] QString namesLine(const ReportDocumentContext& context, const QStringList& names)
{
    QStringList shown;
    for (qsizetype index = 0; index < names.size() && index < kNamesShownAtMost; ++index)
    {
        shown << context.assets.path(names.at(index));
    }
    QString line = shown.join(QStringLiteral(", "));
    if (names.size() > kNamesShownAtMost)
    {
        line += documentText(" and %1 more").arg(formatCount(names.size() - kNamesShownAtMost));
    }
    return line;
}

void writeFramesChanged(ReportMarkup& out, const ReportDocumentContext& context, const ReportComparison& comparison)
{
    openCard(out, documentText("Frames"), documentText("of the groups both runs have"));
    if (comparison.inputsChanged.empty())
    {
        out.element("p", documentText("The groups both runs have took the same frames."));
    }
    else
    {
        out.open("ul", {{"class", QStringLiteral("chlist")}});
        for (const ReportInputChange& change : comparison.inputsChanged)
        {
            const bool added = !change.added.isEmpty();
            const bool removed = !change.removed.isEmpty();
            out.open("li");
            out.element("span",
                        {{"class", added && !removed ? QStringLiteral("mark plus") : QStringLiteral("mark minus")}},
                        added && removed ? QStringLiteral("±") : (added ? QStringLiteral("+") : QStringLiteral("−")));
            out.open("div");
            QStringList counts;
            if (added)
            {
                counts << counted(change.added.size(), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 frame added"),
                                  QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 frames added"));
            }
            if (removed)
            {
                counts << counted(change.removed.size(),
                                  QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 frame removed"),
                                  QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 frames removed"));
            }
            out.text(change.stage + dotSeparator() + groupLabel(context, change.groupKey, change.group) +
                     QStringLiteral(": ") + counts.join(QStringLiteral(", ")));
            if (added)
            {
                out.element("small", documentText("Added: %1").arg(namesLine(context, change.added)));
            }
            if (removed)
            {
                out.element("small", documentText("Removed: %1").arg(namesLine(context, change.removed)));
            }
            out.close("div");
            out.close("li");
        }
        out.close("ul");
    }
    out.close("div");
}

/// The application the two runs ran under, and the groups only one of them has.
void writeApplicationAndGroups(ReportMarkup& out, const ReportDocumentContext& context,
                               const ReportComparison& comparison)
{
    const ReportRun& run = context.model.run;
    openCard(out, documentText("Application"));
    const auto fact = [&out](const QString& name, const QString& before, const QString& after) {
        if (before.trimmed().isEmpty() && after.trimmed().isEmpty())
        {
            return;
        }
        openFact(out, name);
        out.text(after.trimmed().isEmpty() ? noValueText() : after);
        out.text(QStringLiteral(" "));
        out.element("small", {{"class", QStringLiteral("muted")}},
                    before == after ? documentText("unchanged")
                                    : documentText("was %1").arg(before.trimmed().isEmpty() ? noValueText() : before));
        closeFact(out);
    };
    // A fingerprint is told from another by its first characters.
    const auto shortened = [](const QString& fingerprint) { return fingerprint.left(12); };
    openFacts(out);
    fact(documentText("Version"), comparison.previousApplicationVersion, run.applicationVersion);
    fact(documentText("Settings fingerprint"), shortened(comparison.previousSettingsFingerprint),
         shortened(run.settingsFingerprint));
    fact(documentText("Inputs fingerprint"), shortened(comparison.previousInputsFingerprint),
         shortened(run.inputsFingerprint));
    fact(documentText("Run fingerprint"), shortened(comparison.previousFingerprint), shortened(run.fingerprint));
    closeFacts(out);

    if (!comparison.groupsChanged.empty())
    {
        writePanelTitle(out, "h5", documentText("Groups only one run has"));
        out.open("ul", {{"class", QStringLiteral("chlist")}});
        for (const ReportGroupChange& change : comparison.groupsChanged)
        {
            out.open("li");
            out.element("span", {{"class", change.added ? QStringLiteral("mark plus") : QStringLiteral("mark minus")}},
                        change.added ? QStringLiteral("+") : QStringLiteral("−"));
            out.open("div");
            out.text(change.stage + dotSeparator() + groupLabel(context, change.groupKey, change.group));
            out.element("small", change.added ? documentText("New in this run")
                                              : documentText("In the earlier run, not in this one"));
            out.close("div");
            out.close("li");
        }
        out.close("ul");
    }
    out.close("div");
}

void writeChanged(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportComparison& comparison = *context.model.comparison;
    const QString when = whenText(comparison.baselineArchivedAt, comparison.baselineRunId, context);
    openBlock(out, "changed", documentText("What changed"), documentText("since the run of %1").arg(when),
              documentText("A difference in a master comes from one of these: other frames, other settings, or "
                           "another version of the application."));

    out.open("div", {{"class", QStringLiteral("g21")}});
    openCard(out, documentText("The run as a whole"));
    openTable(out, QStringLiteral("figures"), documentText("The totals of the earlier run and of this one"),
              {{documentText("Total")}, {when, true}, {documentText("This run"), true}, {documentText("Change")}});
    writeFigureRows(out, totalRows(comparison.previous, comparison.current));
    closeTable(out);
    out.close("div");
    writeApplicationAndGroups(out, context, comparison);
    out.close("div");

    writeSettingsChanged(out, context, comparison);
    writeFramesChanged(out, context, comparison);
    out.close("section");
}

// ---- Over the runs of the folder.

/// One run as the charts and the table of the folder's runs take it.
struct FolderRun
{
    QString runId;
    QString refusal;
    std::optional<QDateTime> archivedAt;
    std::optional<ReportOutcome> outcome;
    std::optional<int> framesUsed;
    std::optional<int> frames;
    std::optional<qint64> integrationMs;
    std::optional<int> masters;
    std::optional<double> backgroundNoise;
    QString report;
    bool thisRun = false;

    /// The seconds that follow the run's name, `:47`, where two runs of the folder were archived in one minute;
    /// empty otherwise (`secondsWhereAlike`).
    QString seconds;
};

/// The master whose noise the runs are compared by: the first of this run's that is there.
[[nodiscard]] const ReportMaster* noiseMaster(const ReportModel& model)
{
    const auto first = std::find_if(model.masters.begin(), model.masters.end(), isThere);
    return first == model.masters.end() ? nullptr : &*first;
}

/// The earlier runs of the folder, oldest first, then this run.
[[nodiscard]] std::vector<FolderRun> folderRuns(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    std::vector<FolderRun> runs;
    runs.reserve(model.history.size() + 1);
    for (const ReportHistoryEntry& entry : model.history)
    {
        runs.push_back(FolderRun{.runId = entry.runId,
                                 .refusal = entry.refusal,
                                 .archivedAt = entry.archivedAt,
                                 .outcome = entry.outcome,
                                 .framesUsed = entry.framesUsed,
                                 .frames = entry.frames,
                                 .integrationMs = entry.integrationMs,
                                 .masters = entry.masters,
                                 .backgroundNoise = entry.backgroundNoise,
                                 .report = entry.report,
                                 .thisRun = false,
                                 .seconds = {}});
    }
    const ReportRun& run = model.run;
    const ReportMaster* master = noiseMaster(model);
    runs.push_back(FolderRun{.runId = run.executionId,
                             .refusal = {},
                             .archivedAt = run.archivedAt,
                             .outcome = run.outcome,
                             .framesUsed = run.framesUsed,
                             .frames = run.frames,
                             .integrationMs = run.integratedMs,
                             .masters = run.groups.made + run.groups.reused,
                             .backgroundNoise = master != nullptr ? master->snapshot.backgroundNoise : std::nullopt,
                             .report = {},
                             .thisRun = true,
                             .seconds = {}});

    // The runs stand together in the table and under the charts, so two of one minute are named to the second. A
    // run that gave no report is listed by its id, whatever time its entry states.
    std::vector<RunStamp> stamps;
    stamps.reserve(runs.size());
    for (const FolderRun& listed : runs)
    {
        stamps.push_back({listed.refusal.isEmpty() ? listed.archivedAt : std::nullopt, listed.runId});
    }
    const QStringList seconds = secondsWhereAlike(stamps, context);
    for (std::size_t index = 0; index < runs.size(); ++index)
    {
        runs[index].seconds = seconds.at(static_cast<qsizetype>(index));
    }
    return runs;
}

/// Whether a run ended with no figure to draw: it failed or was cancelled. A run that made some of its masters and
/// not the others states its figures as a run that made them all does, so its column is drawn.
[[nodiscard]] bool failedOrCancelled(const std::optional<ReportOutcome>& outcome)
{
    return outcome == ReportOutcome::Failed || outcome == ReportOutcome::Cancelled;
}

/// One figure over the latest runs that keep a report: a column per run, this run in the series colour, a run that
/// failed or was cancelled as a cross.
[[nodiscard]] QString runsFigure(const ReportDocumentContext& context, const std::vector<FolderRun>& runs,
                                 const QString& label,
                                 const std::function<std::optional<double>(const FolderRun&)>& valueOf,
                                 const std::function<QString(double)>& textOf)
{
    std::vector<const FolderRun*> drawn;
    for (const FolderRun& run : runs)
    {
        if (run.refusal.isEmpty())
        {
            drawn.push_back(&run);
        }
    }
    if (drawn.size() > kRunsDrawnAtMost)
    {
        drawn.erase(drawn.begin(), drawn.end() - static_cast<std::ptrdiff_t>(kRunsDrawnAtMost));
    }

    RunsChart chart;
    chart.description = {label, documentText("No run of this folder states the figure.")};
    for (const FolderRun* run : drawn)
    {
        ChartRun column;
        // The run is named as its row of the table names it: to the second where two runs share a minute.
        const QString when = whenText(run->archivedAt, run->runId, context) + run->seconds;
        if (run->archivedAt.has_value() && run->archivedAt->isValid())
        {
            const QString day = formatDate(run->archivedAt->toTimeZone(context.zone.resolved()).date());
            column.label = {day.left(day.lastIndexOf(QLatin1Char(' '))),
                            clockText(*run->archivedAt, context.zone) + run->seconds};
        }
        else
        {
            column.label = {run->runId.left(8)};
        }
        const std::optional<double> value = failedOrCancelled(run->outcome) ? std::nullopt : valueOf(*run);
        column.value = value;
        column.valueText = value.has_value() ? textOf(*value) : QString();
        column.thisRun = run->thisRun;
        column.tip.heading = run->thisRun ? documentText("This run") : when;
        if (run->outcome.has_value())
        {
            column.tip.lines << outcomeWords(*run->outcome).text;
        }
        column.tip.lines << (value.has_value() ? textOf(*value) : documentText("Not stated"));
        chart.runs.push_back(column);
    }
    return runsChart(chart);
}

void writeHistory(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const std::vector<FolderRun> runs = folderRuns(context);
    const ReportMaster* noiseOf = noiseMaster(model);
    const bool anyReport =
        context.assets.form() == ReportForm::Linked &&
        std::any_of(runs.begin(), runs.end(), [](const FolderRun& run) { return !run.report.isEmpty(); });
    const std::size_t stated = static_cast<std::size_t>(
        std::count_if(runs.begin(), runs.end(), [](const FolderRun& run) { return run.refusal.isEmpty(); }));

    openBlock(out, "history", documentText("Over the runs of this folder"),
              counted(static_cast<qint64>(runs.size()), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 run"),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 runs, oldest first")),
              documentText("This run is drawn in colour; a cross is a run that failed or was cancelled."));
    out.open("div", {{"class", QStringLiteral("card")}});

    out.open("div", {{"class", QStringLiteral("hist3")}});
    const auto figure = [&](const QString& title, const QString& aside, const QString& chart) {
        out.open("figure");
        out.open("figcaption");
        out.element("span", title);
        if (!aside.isEmpty())
        {
            out.element("small", aside);
        }
        out.close("figcaption");
        out.chart(chart);
        out.close("figure");
    };
    figure(documentText("Integration"), documentText("of the lights used"),
           runsFigure(
               context, runs, documentText("The integration time of each run of the folder"),
               [](const FolderRun& run) {
                   return run.integrationMs.has_value() ? std::optional<double>(static_cast<double>(*run.integrationMs))
                                                        : std::nullopt;
               },
               [](const double value) { return formatDurationMs(static_cast<qint64>(std::llround(value))); }));
    figure(documentText("Lights used"), QString(),
           runsFigure(
               context, runs, documentText("The lights each run of the folder used"),
               [](const FolderRun& run) {
                   return run.framesUsed.has_value() ? std::optional<double>(static_cast<double>(*run.framesUsed))
                                                     : std::nullopt;
               },
               [](const double value) { return formatCount(static_cast<qint64>(std::llround(value))); }));
    figure(documentText("Noise"),
           noiseOf != nullptr ? documentText("%1, lower is better").arg(context.labelOf(*noiseOf)) : QString(),
           runsFigure(
               context, runs, documentText("The noise of one master in each run of the folder"),
               [](const FolderRun& run) { return run.backgroundNoise; },
               [](const double value) { return formatFigure(value); }));
    out.close("div");
    if (stated > kRunsDrawnAtMost)
    {
        writeCaption(out, documentText("The charts draw the latest %1 runs; the table lists every run.")
                              .arg(formatCount(static_cast<qint64>(kRunsDrawnAtMost))));
    }

    std::vector<ReportColumn> columns = {
        {documentText("Run")},
        {documentText("Ended")},
        {documentText("Lights used"), true},
        {documentText("Integration"), true},
        {documentText("Masters"), true},
        {documentText("Noise"), true,
         noiseOf != nullptr
             ? documentText("The background noise of %1 in each run that states it.").arg(context.labelOf(*noiseOf))
             : QString()}};
    if (anyReport)
    {
        columns.push_back({documentText("Report")});
    }
    openTable(out, QStringLiteral("runs"), documentText("The runs of this output folder"), columns);
    for (const FolderRun& run : runs)
    {
        out.open("tr", {{"class", run.thisRun ? QStringLiteral("thisrun") : QString()}});
        out.open("td", {{"class", QStringLiteral("nowrap")}});
        if (run.refusal.isEmpty())
        {
            out.text(whenText(run.archivedAt, run.runId, context) + run.seconds);
        }
        else
        {
            out.element("code", run.runId);
        }
        if (run.thisRun)
        {
            out.text(dotSeparator());
            out.element("b", documentText("this run"));
        }
        out.close("td");
        if (!run.refusal.isEmpty())
        {
            // A run whose archive keeps no report this version reads is listed, and says why it states nothing.
            out.open("td", {{"class", QStringLiteral("muted")}, {"colspan", QStringLiteral("5")}});
            out.element("b", documentText("No report."));
            out.text(QStringLiteral(" "));
            out.text(context.assets.message(run.refusal));
            out.close("td");
        }
        else
        {
            out.open("td");
            if (run.outcome.has_value())
            {
                const ReportStateWords words = outcomeWords(*run.outcome);
                writePill(out, words.kind, words.text);
            }
            else
            {
                out.text(noValueText());
            }
            out.close("td");
            out.element("td", {{"class", QStringLiteral("r num")}},
                        run.framesUsed.has_value() && run.frames.has_value()
                            ? documentText("%1 of %2").arg(formatCount(*run.framesUsed), formatCount(*run.frames))
                            : (run.frames.has_value() ? documentText("%1 taken").arg(formatCount(*run.frames))
                                                      : noValueText()));
            out.element("td", {{"class", QStringLiteral("r num")}},
                        run.integrationMs.has_value() ? formatDurationMs(*run.integrationMs) : noValueText());
            out.element("td", {{"class", QStringLiteral("r num")}},
                        run.masters.has_value() ? formatCount(*run.masters) : noValueText());
            out.element("td", {{"class", QStringLiteral("r num")}}, formatFigure(run.backgroundNoise));
        }
        if (anyReport)
        {
            out.open("td");
            if (!run.report.isEmpty())
            {
                out.element("a", {{"href", run.report}}, documentText("Its report"));
            }
            else if (!run.thisRun)
            {
                out.text(noValueText());
            }
            out.close("td");
        }
        out.close("tr");
    }
    closeTable(out);
    out.close("div");
    out.close("section");
}

/// What the part says under its heading: which run this one is compared with, or why with none.
[[nodiscard]] QString earlierLead(const ReportDocumentContext& context, QStringList& bold)
{
    const ReportModel& model = context.model;
    if (model.comparison.has_value())
    {
        const ReportComparison& comparison = *model.comparison;
        bold = {whenText(comparison.baselineArchivedAt, comparison.baselineRunId, context),
                formatCount(comparison.previous.masters), formatCount(comparison.previous.frames)};
        return comparison.previous.masters == 1
                   ? documentText("The run as a whole is compared with the latest earlier run of this folder that "
                                  "completed: %1, %2 master from %3 lights.")
                   : documentText("The run as a whole is compared with the latest earlier run of this folder that "
                                  "completed: %1, %2 masters from %3 lights.");
    }
    if (!model.history.empty())
    {
        return documentText("No earlier run of this folder both completed and keeps a report this version reads, "
                            "so the run as a whole is compared with none.");
    }
    return documentText("This is the first run of this output folder that keeps a report: there is no earlier run "
                        "to compare it with.");
}
} // namespace

// ---- What the writer calls.

QStringList earlierAnchors(const ReportModel& model)
{
    const EarlierPlan plan = planEarlier(model);
    QStringList anchors = {QStringLiteral("earlier")};
    if (plan.beforeAfter)
    {
        anchors << QStringLiteral("before-after");
    }
    if (plan.changed)
    {
        anchors << QStringLiteral("changed");
    }
    if (plan.history)
    {
        anchors << QStringLiteral("history");
    }
    return anchors;
}

ReportPart writeReportEarlier(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const EarlierPlan plan = planEarlier(model);
    ReportPart part;
    part.heading = QStringLiteral("3") + dotSeparator() + documentText("Against earlier runs");

    ReportMarkup& out = part.body;
    // The lead is written with its figures in bold, so the part's heading is opened by hand.
    out.open("section", {{"class", QStringLiteral("part")}, {"id", QStringLiteral("earlier")}});
    out.open("div", {{"class", QStringLiteral("parthead")}});
    out.element("span", {{"class", QStringLiteral("eyebrow")}}, documentText("Part %1").arg(3));
    out.element("h2", documentText("Against earlier runs"));
    QStringList bold;
    const QString lead = earlierLead(context, bold);
    out.open("p");
    writeSentence(out, lead, bold);
    out.close("p");
    // A run that set no master beside an earlier run has no comparison for the masters it found still valid to be
    // named under. The part would then say nothing of its masters, so its head names them and says why none is
    // compared.
    if (!plan.beforeAfter)
    {
        if (const QString reused = reusedMastersSentence(context); !reused.isEmpty())
        {
            out.element("p", reused);
        }
    }
    out.close("div");

    if (plan.beforeAfter)
    {
        writeBeforeAfter(out, context);
        const qsizetype compared =
            std::count_if(model.masters.begin(), model.masters.end(),
                          [](const ReportMaster& master) { return master.baseline.has_value(); });
        part.entries.push_back(
            {QStringLiteral("before-after"), documentText("Before and after"), formatCount(compared), false});
    }
    if (plan.changed)
    {
        writeChanged(out, context);
        part.entries.push_back({QStringLiteral("changed"), documentText("What changed"), {}, false});
    }
    if (plan.history)
    {
        writeHistory(out, context);
        part.entries.push_back({QStringLiteral("history"), documentText("Over the runs"),
                                formatCount(static_cast<qint64>(model.history.size()) + 1), false});
    }
    if (part.entries.empty())
    {
        // A part of one sentence has no block to lead to: the contents lead to the part itself.
        part.entries.push_back({QStringLiteral("earlier"), documentText("No earlier run"), {}, false});
    }
    out.close("section");
    return part;
}
} // namespace ImageIntegration::Reporting

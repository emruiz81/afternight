// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The header and the first part of the report's document: how the run ended, what needs attention, what was made,
/// the lights used and left out, the nights and the lights' quality.
///
/// Every figure here is the model's. A block that has nothing to say is absent: the masters' gallery for a run of
/// one master, whose picture sits in the header instead; the outcome for a run that completed; the nights and the
/// quality charts for a run whose lights state no measure.

#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <QHash>
#include <QTime>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace ImageIntegration::Reporting
{
namespace
{
constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();

/// The run's messages are folded away when there are more than this many.
constexpr std::size_t kMessagesShownUnfolded = 5;

// ---- The lights of the run.

/// One light with the master it belongs to; `master` is null for a light of a group the run planned no master of.
struct Light
{
    const ReportFrame* frame = nullptr;
    const ReportMaster* master = nullptr;

    /// The place of the light's group among the groups a chart names.
    int group = 0;
};

[[nodiscard]] bool isUsed(const ReportFrame& frame)
{
    return frame.state == ReportFrameState::Used;
}

/// What a state means for the lights in it.
[[nodiscard]] QString stateMeaning(const ChartState state)
{
    switch (state)
    {
    case ChartState::Used:
        return documentText("Combined into a master.");
    case ChartState::ExcludedByUser:
        return documentText("You left these lights out, in the lights' list or from the stack alone.");
    case ChartState::RegistrationFailed:
        return documentText("The registration could not bring these lights to the reference, so no stack took them.");
    case ChartState::LeftOutByStack:
        return documentText("The stack dropped these lights on its own: something it needs of them was missing.");
    case ChartState::ExcludedOnRestack:
        return documentText("The first stack rejected so much of these lights that the master was stacked again "
                            "without them.");
    case ChartState::NotStacked:
        return documentText("Their master was not made, so these lights went into nothing.");
    }
    return {};
}

/// The states other than used, in the order the report lists them.
constexpr std::array<ChartState, 5> kLeftOutStates = {ChartState::ExcludedByUser, ChartState::RegistrationFailed,
                                                      ChartState::LeftOutByStack, ChartState::ExcludedOnRestack,
                                                      ChartState::NotStacked};

/// Every light of the run in the order it was taken; the lights that state no time follow, in the model's order.
[[nodiscard]] std::vector<Light> lightsInCaptureOrder(const ReportModel& model)
{
    std::vector<Light> lights;
    int group = 0;
    for (const ReportMaster& master : model.masters)
    {
        if (master.frames.empty())
        {
            continue;
        }
        for (const ReportFrame& frame : master.frames)
        {
            lights.push_back(Light{.frame = &frame, .master = &master, .group = group});
        }
        ++group;
    }
    for (const ReportGroupNotStacked& notStacked : model.groupsNotStacked)
    {
        if (notStacked.frames.empty())
        {
            continue;
        }
        for (const ReportFrame& frame : notStacked.frames)
        {
            lights.push_back(Light{.frame = &frame, .master = nullptr, .group = group});
        }
        ++group;
    }
    std::stable_sort(lights.begin(), lights.end(), [](const Light& left, const Light& right) {
        const bool leftTimed = left.frame->capturedAt.has_value();
        const bool rightTimed = right.frame->capturedAt.has_value();
        if (leftTimed != rightTimed)
        {
            return leftTimed;
        }
        return leftTimed && *left.frame->capturedAt < *right.frame->capturedAt;
    });
    return lights;
}

/// The names of the groups `lightsInCaptureOrder` numbers, in that order.
[[nodiscard]] QStringList lightGroupNames(const ReportModel& model, const QHash<QString, QString>& labels)
{
    QStringList names;
    for (const ReportMaster& master : model.masters)
    {
        if (!master.frames.empty())
        {
            names << labels.value(master.key, master.name);
        }
    }
    for (const ReportGroupNotStacked& notStacked : model.groupsNotStacked)
    {
        if (!notStacked.frames.empty())
        {
            names << notStacked.name;
        }
    }
    return names;
}

// ---- What is present.

/// Which blocks of the summary a model calls for.
struct SummaryPlan
{
    bool outcome = false;
    bool made = false;
    bool frames = false;
    bool nights = false;
    bool quality = false;
};

[[nodiscard]] SummaryPlan planSummary(const ReportModel& model)
{
    SummaryPlan plan;
    plan.outcome = runStopped(model);
    plan.made = model.masters.size() > 1;
    plan.frames = model.run.frames > 0;
    const auto consider = [&plan](const ReportFrame& frame) {
        const bool measured = frame.fwhm.has_value() || frame.stars.has_value() || frame.backgroundNoise.has_value();
        plan.nights = plan.nights || measured;
        plan.quality = plan.quality || frame.fwhm.has_value() || (isUsed(frame) && frame.weight.has_value());
    };
    for (const ReportMaster& master : model.masters)
    {
        std::for_each(master.frames.begin(), master.frames.end(), consider);
    }
    for (const ReportGroupNotStacked& group : model.groupsNotStacked)
    {
        std::for_each(group.frames.begin(), group.frames.end(), consider);
    }
    return plan;
}

// ---- Fragments.

[[nodiscard]] const char* severityKind(const ReportIssueSeverity severity)
{
    switch (severity)
    {
    case ReportIssueSeverity::Critical:
        return "critical";
    case ReportIssueSeverity::Warning:
        return "warning";
    case ReportIssueSeverity::Note:
        return "note";
    }
    return "note";
}

/// The issues that are computed kinds, and the ones that are the run's own messages.
struct IssueSplit
{
    std::vector<const ReportIssue*> kinds;
    std::vector<const ReportIssue*> messages;
    int critical = 0;
    int warnings = 0;
    int notes = 0;
};

[[nodiscard]] IssueSplit splitIssues(const ReportModel& model)
{
    IssueSplit split;
    for (const ReportIssue& issue : model.issues)
    {
        if (issue.kind == ReportIssueKind::RunMessage)
        {
            split.messages.push_back(&issue);
            continue;
        }
        split.kinds.push_back(&issue);
        switch (issue.severity)
        {
        case ReportIssueSeverity::Critical:
            ++split.critical;
            break;
        case ReportIssueSeverity::Warning:
            ++split.warnings;
            break;
        case ReportIssueSeverity::Note:
            ++split.notes;
            break;
        }
    }
    return split;
}

/// Whether anything of the run needs a second look: a critical issue or a warning, computed or in the run's words.
[[nodiscard]] bool needsAttention(const ReportModel& model)
{
    return std::any_of(model.issues.begin(), model.issues.end(),
                       [](const ReportIssue& issue) { return issue.severity != ReportIssueSeverity::Note; });
}

// ---- The header.

struct Tile
{
    const char* icon;
    QString label;
    QString value;

    /// What stands small after the value: "of 174".
    QString valueAside;

    QString hint;

    /// The line under the rule: the earlier run's figure, or when the run was archived.
    QString under;
};

void writeTile(ReportMarkup& out, const Tile& tile)
{
    out.open("div", {{"class", QStringLiteral("tile")}});
    out.open("div", {{"class", QStringLiteral("lab")}});
    out.icon(tile.icon);
    out.element("span", tile.label);
    out.close("div");
    out.open("div", {{"class", QStringLiteral("val")}});
    out.text(tile.value);
    if (!tile.valueAside.isEmpty())
    {
        out.text(QStringLiteral(" "));
        out.element("small", tile.valueAside);
    }
    out.close("div");
    if (!tile.hint.isEmpty())
    {
        out.element("div", {{"class", QStringLiteral("hint")}}, tile.hint);
    }
    if (!tile.under.isEmpty())
    {
        out.element("div", {{"class", QStringLiteral("delta")}}, tile.under);
    }
    out.close("div");
}

/// The masters by what became of them, in a line: "5 made, 1 reused".
[[nodiscard]] QString groupCountsLine(const ReportGroupCounts& groups)
{
    QStringList parts;
    if (groups.made > 0)
    {
        parts << documentText("%1 made").arg(formatCount(groups.made));
    }
    if (groups.reused > 0)
    {
        parts << documentText("%1 reused").arg(formatCount(groups.reused));
    }
    if (groups.failed > 0)
    {
        parts << documentText("%1 failed").arg(formatCount(groups.failed));
    }
    if (groups.notReached > 0)
    {
        parts << documentText("%1 not reached").arg(formatCount(groups.notReached));
    }
    return parts.join(QStringLiteral(", "));
}

[[nodiscard]] std::vector<Tile> headerTiles(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const ReportRun& run = model.run;
    const std::optional<ReportComparison>& comparison = model.comparison;
    const bool compared = comparison.has_value() && !comparison->baselineRunId.isEmpty();

    Tile lights{.icon = "frames", .label = documentText("Lights used")};
    if (run.framesUsed.has_value())
    {
        lights.value = formatCount(*run.framesUsed);
        lights.valueAside = documentText("of %1").arg(formatCount(run.frames));
        const int leftOut = run.frames - *run.framesUsed;
        lights.hint =
            leftOut > 0 ? counted(leftOut, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 left out, with its reason"),
                                  QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 left out, each with its reason"))
                        : documentText("Every light was used");
    }
    else
    {
        lights.label = documentText("Lights taken");
        lights.value = formatCount(run.frames);
        lights.hint = documentText("The run recorded no light's fate");
    }
    if (compared)
    {
        // The earlier run's figure is the same figure: the lights used where both runs state them.
        lights.under =
            run.framesUsed.has_value() && comparison->previous.framesUsed.has_value()
                ? documentText("Earlier run: %1 of %2")
                      .arg(formatCount(*comparison->previous.framesUsed), formatCount(comparison->previous.frames))
                : documentText("Earlier run: %1 lights taken").arg(formatCount(comparison->previous.frames));
    }

    Tile integration{.icon = "clock", .label = documentText("Integration")};
    if (run.integratedMs.has_value())
    {
        integration.value = formatDurationMs(*run.integratedMs);
        integration.hint = documentText("of %1 captured").arg(formatDurationMs(run.capturedMs));
    }
    else
    {
        integration.label = documentText("Captured");
        integration.value = formatDurationMs(run.capturedMs);
    }
    if (compared)
    {
        integration.under =
            run.integratedMs.has_value() && comparison->previous.integratedMs.has_value()
                ? documentText("Earlier run: %1").arg(formatDurationMs(*comparison->previous.integratedMs))
                : documentText("Earlier run: %1 captured").arg(formatDurationMs(comparison->previous.integrationMs));
    }

    Tile time{.icon = "clock", .label = documentText("Run time")};
    time.value = formatDurationMs(run.processingMs);
    time.hint = run.incremental ? documentText("An incremental run") : QString();
    if (run.archivedAt.has_value())
    {
        time.under = documentText("Archived %1").arg(formatDateTime(*run.archivedAt, context.zone));
    }

    std::vector<Tile> tiles;
    if (model.masters.size() == 1)
    {
        const ReportMaster& master = model.masters.front();
        Tile noise{.icon = "file", .label = documentText("Noise")};
        noise.value = formatFigure(master.snapshot.backgroundNoise);
        QStringList hint;
        if (master.snapshot.snr.has_value())
        {
            hint << documentText("SNR %1").arg(formatFigure(*master.snapshot.snr));
        }
        if (master.stack.has_value())
        {
            hint << documentText("%1 effective frames").arg(formatFigure(master.stack->effectiveFrames));
        }
        noise.hint = hint.join(dotSeparator());
        if (const ReportStateWords words = masterStateWords(master.state); words.kind != nullptr)
        {
            noise.under = words.text;
        }
        else if (master.baseline.has_value() && master.baseline->made && master.baseline->backgroundNoise.has_value())
        {
            // The master's own baseline: the latest earlier run that made it.
            noise.under = documentText("Earlier run: %1").arg(formatFigure(master.baseline->backgroundNoise));
        }
        tiles = {lights, integration, noise, time};
    }
    else
    {
        Tile masters{.icon = "file", .label = documentText("Masters")};
        masters.value = formatCount(run.groups.made + run.groups.reused);
        if (run.groups.made + run.groups.reused != run.groups.total)
        {
            masters.valueAside = documentText("of %1").arg(formatCount(run.groups.total));
        }
        masters.hint = groupCountsLine(run.groups);
        if (compared)
        {
            masters.under = counted(comparison->previous.masters,
                                    QT_TRANSLATE_NOOP("ImageIntegrationReport", "Earlier run: %1 master"),
                                    QT_TRANSLATE_NOOP("ImageIntegrationReport", "Earlier run: %1 masters"));
        }
        tiles = {masters, lights, integration, time};
    }
    return tiles;
}

/// The nights the lights were taken on, in a few words.
[[nodiscard]] QString nightsLine(const ReportRun& run)
{
    if (run.nights.empty())
    {
        return {};
    }
    if (run.nights.size() == 1)
    {
        return documentText("night of %1").arg(formatDate(run.nights.front().date));
    }
    return documentText("%1 nights, %2 to %3")
        .arg(formatCount(static_cast<qint64>(run.nights.size())), formatDate(run.nights.front().date),
             formatDate(run.nights.back().date));
}

void writeVerdict(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportRun& run = context.model.run;
    out.open("p", {{"class", QStringLiteral("verdict")}});
    out.text(context.assets.message(run.verdict));
    if (run.framesUsed.has_value() && run.integratedMs.has_value())
    {
        out.text(QStringLiteral(" "));
        writeSentence(out,
                      context.model.masters.size() == 1
                          ? documentText("The master combines %1 of %2 lights, %3 of integration.")
                          : documentText("The masters combine %1 of %2 lights, %3 of integration."),
                      {formatCount(*run.framesUsed), formatCount(run.frames), formatDurationMs(*run.integratedMs)});
    }
    else if (run.frames > 0)
    {
        out.text(QStringLiteral(" "));
        writeSentence(out, documentText("The run took %1 lights, %2 of exposure."),
                      {formatCount(run.frames), formatDurationMs(run.capturedMs)});
    }
    if (run.processingMs > 0)
    {
        out.text(QStringLiteral(" "));
        writeSentence(out, documentText("It ran for %1."), {formatDurationMs(run.processingMs)});
    }
    out.text(QStringLiteral(" "));
    out.text(needsAttention(context.model) ? documentText("See what needs attention below.")
                                           : documentText("Nothing needs attention."));
    out.close("p");
}

/// The one master's picture, which stands in the header of a run that made no other.
void writeHeaderPicture(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    const QString source = context.assets.image(master.preview);
    if (source.isEmpty())
    {
        return;
    }
    const QString anchor = QStringLiteral("file-") + master.slug;
    const bool linked = context.hasAnchor(anchor);
    const char* element = linked ? "a" : "div";
    out.open(element, {{"class", QStringLiteral("shot")}, {"href", linked ? QStringLiteral("#") + anchor : QString()}});
    out.leaf("img", {{"src", source}, {"alt", documentText("%1, stretched preview").arg(master.name)}});
    QStringList under;
    if (!master.fileName.isEmpty())
    {
        under << master.fileName;
    }
    if (master.statistics.has_value())
    {
        under << QStringLiteral("%1 × %2").arg(formatCount(static_cast<qint64>(master.statistics->width)),
                                               formatCount(static_cast<qint64>(master.statistics->height)));
        under << counted(static_cast<qint64>(master.statistics->channels),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 channel"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 channels"));
    }
    if (!under.isEmpty())
    {
        out.element("span", under.join(dotSeparator()));
    }
    out.close(element);
}

// ---- The outcome of a run that stopped.

void writeMasterNames(ReportMarkup& out, const ReportDocumentContext& context,
                      const std::vector<const ReportMaster*>& masters, const QHash<QString, QString>& labels,
                      const bool withReason)
{
    out.open("ul", {{"class", QStringLiteral("plain")}});
    for (const ReportMaster* master : masters)
    {
        out.open("li");
        writeMasterLink(out, context, *master, labels.value(master->key, master->name));
        if (const ReportStateWords words = masterStateWords(master->state); words.kind != nullptr)
        {
            out.text(QStringLiteral(" "));
            out.element("span", {{"class", QStringLiteral("muted")}}, words.text.toLower());
        }
        if (withReason && !master->failure.trimmed().isEmpty())
        {
            out.element("p", {{"class", QStringLiteral("muted")}}, context.assets.message(master->failure));
        }
        out.close("li");
    }
    out.close("ul");
}

void writeOutcome(ReportMarkup& out, const ReportDocumentContext& context, const QHash<QString, QString>& labels)
{
    const ReportModel& model = context.model;
    const ReportStateWords words = outcomeWords(model.run.outcome);

    // The run's own account of how it stopped: the first critical issue. Its action says what a new run makes.
    const auto first = std::find_if(model.issues.begin(), model.issues.end(), [](const ReportIssue& issue) {
        return issue.severity == ReportIssueSeverity::Critical && issue.kind != ReportIssueKind::RunMessage;
    });
    const ReportIssue* account = first == model.issues.end() ? nullptr : &*first;

    std::vector<const ReportMaster*> kept;
    std::vector<const ReportMaster*> lost;
    for (const ReportMaster& master : model.masters)
    {
        const bool there = master.state == ReportMasterState::Made || master.state == ReportMasterState::Reused;
        (there ? kept : lost).push_back(&master);
    }

    openBlock(out, "outcome", documentText("What happened"), words.text,
              account != nullptr ? context.assets.message(account->title) : context.assets.message(model.run.verdict));
    out.open("div", {{"class", QStringLiteral("g3")}});

    openCard(out, documentText("What was kept"),
             counted(static_cast<qint64>(kept.size()), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 master"),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 masters")));
    if (kept.empty())
    {
        writeCaption(out, documentText("No master of this run is on disk."));
    }
    else
    {
        writeMasterNames(out, context, kept, labels, false);
        writeCaption(out, documentText("These masters are whole: a run that stops leaves what it finished."));
    }
    out.close("div");

    openCard(out, documentText("What was not made"),
             counted(static_cast<qint64>(lost.size()), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 master"),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 masters")));
    if (lost.empty())
    {
        writeCaption(out, documentText("Every master the run planned was made or was still valid."));
    }
    else
    {
        writeMasterNames(out, context, lost, labels, true);
    }
    out.close("div");

    openCard(out, documentText("What to do"));
    if (account != nullptr && !account->detail.trimmed().isEmpty())
    {
        out.element("p", context.assets.message(account->detail));
    }
    if (account != nullptr && !account->action.trimmed().isEmpty())
    {
        out.element("p", {{"class", QStringLiteral("do")}}, context.assets.message(account->action));
    }
    else
    {
        writeCaption(out, documentText("Run the workflow again: the masters that are still valid are not made a "
                                       "second time."));
    }
    out.close("div");

    out.close("div");
    out.close("section");
}

// ---- What needs attention.

void writeIssue(ReportMarkup& out, const ReportDocumentContext& context, const ReportIssue& issue)
{
    const char* kind = severityKind(issue.severity);
    out.open("li", {{"class", QStringLiteral("issue is-") + QLatin1String(kind)}});
    out.icon(kind);
    out.open("div");
    out.element("b", context.assets.message(issue.title));
    if (!issue.detail.trimmed().isEmpty())
    {
        out.element("p", context.assets.message(issue.detail));
    }
    if (!issue.action.trimmed().isEmpty())
    {
        out.open("p", {{"class", QStringLiteral("do")}});
        out.element("b", documentText("What to do"));
        out.text(QStringLiteral(" "));
        out.text(context.assets.message(issue.action));
        out.close("p");
    }
    out.close("div");
    if (!issue.anchor.isEmpty() && issue.anchor != QStringLiteral("attention") && context.hasAnchor(issue.anchor))
    {
        out.element("a", {{"class", QStringLiteral("noprint")}, {"href", QStringLiteral("#") + issue.anchor}},
                    documentText("See it"));
    }
    out.close("li");
}

void writeIssues(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<const ReportIssue*>& issues)
{
    out.open("ul", {{"class", QStringLiteral("issues")}});
    for (const ReportIssue* issue : issues)
    {
        writeIssue(out, context, *issue);
    }
    out.close("ul");
}

void writeAttention(ReportMarkup& out, const ReportDocumentContext& context, const IssueSplit& split)
{
    QStringList counts;
    if (split.critical > 0)
    {
        counts << counted(split.critical, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 critical"),
                          QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 critical"));
    }
    if (split.warnings > 0)
    {
        counts << counted(split.warnings, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 warning"),
                          QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 warnings"));
    }
    if (split.notes > 0)
    {
        counts << counted(split.notes, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 note"),
                          QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 notes"));
    }
    if (!split.messages.empty())
    {
        counts << counted(static_cast<qint64>(split.messages.size()),
                          QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 message from the run"),
                          QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 messages from the run"));
    }
    const bool attention = needsAttention(context.model);
    openBlock(out, "attention", documentText("Needs attention"),
              counts.isEmpty() ? documentText("nothing") : counts.join(dotSeparator()),
              attention ? documentText("What the run could not do as asked, most serious first.") : QString());
    if (!attention)
    {
        out.open("div", {{"class", QStringLiteral("calm")}});
        out.icon("good");
        out.element("span", documentText("The run did everything it was asked to do."));
        out.close("div");
    }
    if (!split.kinds.empty())
    {
        writeIssues(out, context, split.kinds);
    }
    if (!split.messages.empty())
    {
        // Every warning and error of the run, word for word: a fact may stand above as a kind and here in the run's
        // own words.
        const QString heading = documentText("Messages from the run");
        if (split.messages.size() > kMessagesShownUnfolded)
        {
            out.open("details", {{"class", QStringLiteral("fold")}});
            out.open("summary");
            out.icon("chevron", QStringLiteral("chev"));
            out.element("span", heading);
            out.element("small", formatCount(static_cast<qint64>(split.messages.size())));
            out.close("summary");
            writeIssues(out, context, split.messages);
            out.close("details");
        }
        else
        {
            out.element("h4", {{"class", QStringLiteral("sub")}}, heading);
            writeIssues(out, context, split.messages);
        }
    }
    out.close("section");
}

// ---- What was made.

void writeMasterCard(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                     const QString& label)
{
    const QString anchor = QStringLiteral("file-") + master.slug;
    const bool linked = context.hasAnchor(anchor);
    const char* element = linked ? "a" : "div";
    out.open(element,
             {{"class", QStringLiteral("mcard")}, {"href", linked ? QStringLiteral("#") + anchor : QString()}});

    out.open("div", {{"class", QStringLiteral("well")}});
    if (const QString source = context.assets.thumbnail(master.preview); !source.isEmpty())
    {
        out.leaf("img", {{"src", source},
                         {"alt", documentText("%1, stretched preview").arg(master.name)},
                         {"loading", QStringLiteral("lazy")}});
    }
    else
    {
        out.element("span", {{"class", QStringLiteral("nopic")}}, documentText("No picture"));
    }
    out.close("div");

    out.open("div", {{"class", QStringLiteral("body")}});
    out.open("h4");
    out.element("span", label);
    if (master.kind == ReportMasterKind::CombinedRgb)
    {
        out.element("small", documentText("Combination"));
    }
    if (const ReportStateWords words = masterStateWords(master.state); words.kind != nullptr)
    {
        writePill(out, words.kind, words.text);
    }
    out.close("h4");

    const MasterUse use = useOf(master);
    out.open("div", {{"class", QStringLiteral("line")}});
    if (master.kind == ReportMasterKind::CombinedRgb)
    {
        out.element("span", documentText("From the red, green and blue masters"));
    }
    else if (master.state == ReportMasterState::Failed || master.state == ReportMasterState::NotReached ||
             master.state == ReportMasterState::Cancelled)
    {
        // A master that was not made combined nothing: its lights are counted, and no integration is claimed.
        out.element("span", counted(use.taken, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light, not stacked"),
                                    QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights, not stacked")));
    }
    else if (use.used.has_value())
    {
        out.open("span");
        writeSentence(out, documentText("%1 of %2 lights").arg(QStringLiteral("%1"), formatCount(use.taken)),
                      {formatCount(*use.used)});
        out.close("span");
        out.element("b", formatDurationMs(*use.integratedMs));
    }
    else
    {
        out.element("span", counted(use.taken, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                    QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")));
        out.element("b", formatDurationMs(master.exposure.integrationMs));
    }
    out.close("div");
    if (!master.failure.trimmed().isEmpty())
    {
        out.element("div", {{"class", QStringLiteral("line muted")}}, context.assets.message(master.failure));
    }
    out.close("div");
    out.close(element);
}

void writeFigureCell(ReportMarkup& out, const QString& figure, const QString& aside = {},
                     const std::optional<double>& share = std::nullopt,
                     const std::optional<double>& mark = std::nullopt)
{
    out.open("td", {{"class", QStringLiteral("r num")}});
    out.text(figure);
    if (!aside.isEmpty())
    {
        out.text(QStringLiteral(" "));
        out.element("span", {{"class", QStringLiteral("muted")}}, aside);
    }
    if (share.has_value())
    {
        out.chart(cellBar(*share, mark));
    }
    out.close("td");
}

void writeCompareTable(ReportMarkup& out, const ReportDocumentContext& context, const QHash<QString, QString>& labels)
{
    const ReportModel& model = context.model;
    double bestSnr = 0.0;
    for (const ReportMaster& master : model.masters)
    {
        bestSnr = (std::max)(bestSnr, master.snapshot.snr.value_or(0.0));
    }

    openTable(out, QStringLiteral("compare"), documentText("How the masters compare"),
              {{documentText("Master")},
               {documentText("SNR"), true,
                documentText("The master's mean level over its background noise. The bar is its share of the "
                             "best.")},
               {documentText("Noise"), true,
                documentText("The background noise of the linear master, as a fraction of full scale.")},
               {documentText("Effective frames"), true,
                documentText("How many lights of equal weight and no rejection would give the same noise. The bar "
                             "is their share of the lights combined.")},
               {documentText("SNR gain"), true,
                documentText("The gain in signal to noise the stack measured, beside the one its light count "
                             "promises.")},
               {documentText("Astrometry"), false,
                documentText("Inherited: carried from the registration reference, the master not plate-solved. "
                             "Solved: the master was plate-solved after stacking.")}});

    const bool severalTargets = model.targets.size() > 1;
    const auto writeRows = [&](const QString& target) {
        for (const ReportMaster& master : model.masters)
        {
            if (severalTargets && master.target != target)
            {
                continue;
            }
            out.open("tr");
            out.open("td");
            out.open("b");
            writeMasterLink(out, context, master, labels.value(master.key, master.name));
            out.close("b");
            if (const ReportStateWords words = masterStateWords(master.state); words.kind != nullptr)
            {
                out.text(QStringLiteral(" "));
                out.element("span", {{"class", QStringLiteral("muted")}}, words.text.toLower());
            }
            out.close("td");

            const ReportMasterSnapshot& snapshot = master.snapshot;
            writeFigureCell(out, formatFigure(snapshot.snr), {},
                            snapshot.snr.has_value() && bestSnr > 0.0 ? std::optional<double>(*snapshot.snr / bestSnr)
                                                                      : std::nullopt);
            writeFigureCell(out, formatFigure(snapshot.backgroundNoise));
            if (master.stack.has_value() && master.stack->inputFrames > 0)
            {
                const ReportStack& stack = *master.stack;
                const double inputs = static_cast<double>(stack.inputFrames);
                writeFigureCell(out, formatFigure(stack.effectiveFrames),
                                documentText("of %1").arg(formatCount(static_cast<qint64>(stack.inputFrames))),
                                stack.effectiveFrames / inputs);
                writeFigureCell(out, formatFigure(stack.measuredSnrGain) + QStringLiteral("×"),
                                documentText("of %1").arg(formatFigure(stack.theoreticalSnrGain)) + QStringLiteral("×"),
                                stack.theoreticalSnrGain > 0.0
                                    ? std::optional<double>(stack.measuredSnrGain / stack.theoreticalSnrGain)
                                    : std::nullopt);
            }
            else
            {
                writeFigureCell(out, noValueText());
                writeFigureCell(out, noValueText());
            }

            out.open("td");
            if (const ReportStateWords words = astrometryWords(master.astrometry); words.kind != nullptr)
            {
                writePill(out, words.kind, words.text);
            }
            else
            {
                out.text(noValueText());
            }
            out.close("td");
            out.close("tr");
        }
    };

    if (severalTargets)
    {
        for (const ReportTarget& target : model.targets)
        {
            out.open("tr", {{"class", QStringLiteral("grouprow")}});
            out.open("th", {{"colspan", QStringLiteral("6")}, {"scope", QStringLiteral("colgroup")}});
            out.text(target.name);
            out.close("th");
            out.close("tr");
            writeRows(target.name);
        }
    }
    else
    {
        writeRows({});
    }
    closeTable(out);
}

void writeMade(ReportMarkup& out, const ReportDocumentContext& context, const QHash<QString, QString>& labels)
{
    const ReportModel& model = context.model;
    openBlock(out, "made", documentText("What was made"),
              documentText("stretched previews; every figure is measured on the linear master"),
              groupCountsLine(model.run.groups));

    const auto writeGallery = [&](const QString& target, const bool byTarget) {
        out.open("div", {{"class", QStringLiteral("gallery")}});
        for (const ReportMaster& master : model.masters)
        {
            if (!byTarget || master.target == target)
            {
                // Under its target's heading a master is called by its filter alone, where that tells it apart.
                const QString filter = master.filter.trimmed();
                const bool told =
                    byTarget && !filter.isEmpty() && master.kind == ReportMasterKind::Stack &&
                    std::count_if(model.masters.begin(), model.masters.end(), [&](const ReportMaster& other) {
                        return other.target == target && other.filter.trimmed() == filter;
                    }) == 1;
                writeMasterCard(out, context, master, told ? filter : labels.value(master.key, master.name));
            }
        }
        out.close("div");
    };

    if (model.targets.size() > 1)
    {
        // Several targets: each has its own line of lights and integration, and its own masters under it.
        for (const ReportTarget& target : model.targets)
        {
            out.open("h4", {{"class", QStringLiteral("target")}});
            out.element("span", target.name);
            out.element("small", counted(target.frames, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")) +
                                     dotSeparator() + formatDurationMs(target.integrationMs));
            out.close("h4");
            writeGallery(target.name, true);
        }
    }
    else
    {
        writeGallery({}, false);
    }

    const bool measured = std::any_of(model.masters.begin(), model.masters.end(), [](const ReportMaster& master) {
        return master.snapshot.snr.has_value() || master.snapshot.backgroundNoise.has_value() ||
               master.stack.has_value();
    });
    if (measured)
    {
        openCard(out, documentText("How the masters compare"), documentText("point at a heading for what it means"));
        writeCompareTable(out, context, labels);
        out.close("div");
    }
    out.close("section");
}

// ---- The lights used and left out.

/// How many lights of the run are in each state a chart tells apart.
[[nodiscard]] std::map<ChartState, int> stateCounts(const ReportRun& run)
{
    std::map<ChartState, int> counts;
    for (const ReportFrameStateCount& count : run.frameStates)
    {
        ReportFrame frame;
        frame.state = count.state;
        counts[chartStateOf(frame)] += count.frames;
    }
    return counts;
}

[[nodiscard]] SegmentedBar usedAndLeftOut(const std::map<ChartState, int>& counts, const int total)
{
    SegmentedBar bar;
    bar.description = {documentText("The run's lights, used and left out"),
                       documentText("The run recorded no light's fate.")};
    bar.width = 480;
    const auto add = [&](const ChartState state) {
        const auto found = counts.find(state);
        if (found == counts.end() || found->second <= 0)
        {
            return;
        }
        ChartSegment segment;
        segment.state = state;
        segment.value = static_cast<double>(found->second);
        segment.valueText = formatCount(found->second);
        segment.tip.heading = stateName(state);
        segment.tip.lines << counted(found->second, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights"));
        if (total > 0)
        {
            segment.tip.lines << documentText("%1 of %2")
                                     .arg(percentText(static_cast<double>(found->second) / total), formatCount(total));
        }
        bar.segments.push_back(segment);
    };
    add(ChartState::Used);
    std::for_each(kLeftOutStates.begin(), kLeftOutStates.end(), add);
    return bar;
}

/// The reasons lights were left out: one row per state that has a light, and none for a state that has not.
void writeReasons(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<Light>& lights,
                  const QStringList& groups)
{
    openTable(
        out, QStringLiteral("reasons"), documentText("Why lights were left out"),
        {{documentText("Why")}, {documentText("Lights"), true}, {documentText("In")}, {documentText("What it means")}});
    for (const ChartState state : kLeftOutStates)
    {
        // Where the lights of this state are, group by group, in the model's order.
        std::vector<std::pair<const Light*, int>> places;
        int total = 0;
        for (const Light& light : lights)
        {
            if (!light.frame->state.has_value() || chartStateOf(*light.frame) != state)
            {
                continue;
            }
            ++total;
            const auto place = std::find_if(places.begin(), places.end(),
                                            [&light](const auto& entry) { return entry.first->group == light.group; });
            if (place == places.end())
            {
                places.emplace_back(&light, 1);
            }
            else
            {
                ++place->second;
            }
        }
        if (total == 0)
        {
            continue;
        }
        std::sort(places.begin(), places.end(),
                  [](const auto& left, const auto& right) { return left.first->group < right.first->group; });

        out.open("tr");
        out.open("td");
        writeStateName(out, state);
        out.close("td");
        out.element("td", {{"class", QStringLiteral("r num")}}, formatCount(total));
        out.open("td");
        bool first = true;
        for (const auto& [light, count] : places)
        {
            if (!first)
            {
                out.text(dotSeparator());
            }
            first = false;
            const QString place = (light->group >= 0 && light->group < groups.size() ? groups.at(light->group)
                                                                                     : documentText("No group")) +
                                  QStringLiteral(" ") + formatCount(count);
            if (light->master != nullptr)
            {
                writeMasterLink(out, context, *light->master, place);
            }
            else
            {
                out.text(place);
            }
        }
        out.close("td");
        out.element("td", stateMeaning(state));
        out.close("tr");
    }
    closeTable(out);
}

/// The lights night by night: when they were taken, how many the stacks used, their exposure and their sharpness.
void writeNightsTable(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<Light>& lights)
{
    struct Night
    {
        std::optional<QDate> date;
        std::optional<QDateTime> first;
        std::optional<QDateTime> last;
        int taken = 0;
        int used = 0;
        bool fatesStated = false;
        double usedSeconds = 0.0;
        std::vector<double> fwhm;
    };
    std::vector<Night> nights;
    for (const Light& light : lights)
    {
        const ReportFrame& frame = *light.frame;
        auto night = std::find_if(nights.begin(), nights.end(),
                                  [&frame](const Night& candidate) { return candidate.date == frame.night; });
        if (night == nights.end())
        {
            nights.push_back(Night{.date = frame.night});
            night = nights.end() - 1;
        }
        ++night->taken;
        night->fatesStated = night->fatesStated || frame.state.has_value();
        if (frame.capturedAt.has_value())
        {
            night->first = night->first.has_value() ? (std::min)(*night->first, *frame.capturedAt) : *frame.capturedAt;
            night->last = night->last.has_value() ? (std::max)(*night->last, *frame.capturedAt) : *frame.capturedAt;
        }
        if (isUsed(frame))
        {
            ++night->used;
            night->usedSeconds += frame.exposureSeconds.value_or(0.0);
            if (frame.fwhm.has_value())
            {
                night->fwhm.push_back(*frame.fwhm);
            }
        }
    }
    // The nights in order, the lights that are of no night last.
    std::stable_sort(nights.begin(), nights.end(), [](const Night& left, const Night& right) {
        if (left.date.has_value() != right.date.has_value())
        {
            return left.date.has_value();
        }
        return left.date.has_value() && *left.date < *right.date;
    });

    openTable(out, QStringLiteral("bynight"), documentText("The lights night by night"),
              {{documentText("Night")},
               {documentText("Taken")},
               {documentText("Used"), true},
               {documentText("Integration"), true},
               {documentText("Median FWHM"), true}});
    for (const Night& night : nights)
    {
        out.open("tr");
        out.open("td");
        out.element("b", night.date.has_value() ? formatDate(*night.date) : documentText("No capture time"));
        out.close("td");
        out.element("td", {{"class", QStringLiteral("nowrap")}},
                    night.first.has_value() && night.last.has_value()
                        ? QStringLiteral("%1 – %2").arg(clockText(*night.first, context.zone),
                                                        clockText(*night.last, context.zone))
                        : noValueText());
        out.element("td", {{"class", QStringLiteral("r num")}},
                    night.fatesStated ? documentText("%1 of %2").arg(formatCount(night.used), formatCount(night.taken))
                                      : formatCount(night.taken));
        out.element("td", {{"class", QStringLiteral("r num")}},
                    night.fatesStated ? formatDurationMs(static_cast<qint64>(std::llround(night.usedSeconds * 1000.0)))
                                      : noValueText());
        const std::optional<double> median = medianOfValues(night.fwhm);
        out.element("td", {{"class", QStringLiteral("r num")}},
                    median.has_value() ? documentText("%1 px").arg(formatFigure(*median)) : noValueText());
        out.close("tr");
    }
    closeTable(out);
}

void writeFrames(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<Light>& lights,
                 const QStringList& groups)
{
    const ReportModel& model = context.model;
    const ReportRun& run = model.run;
    const std::map<ChartState, int> counts = stateCounts(run);

    QString aside = documentText("%1 taken").arg(formatCount(run.frames));
    if (run.framesUsed.has_value())
    {
        aside += dotSeparator() + documentText("%1 used").arg(formatCount(*run.framesUsed));
    }
    openBlock(out, "frames", documentText("Lights"), aside,
              model.masters.size() > 1 ? documentText("What went into the masters, and what was left out.")
                                       : documentText("What went into the master, and what was left out."));
    out.open("div", {{"class", QStringLiteral("g2")}});

    openCard(out, documentText("Used and left out"));
    const SegmentedBar bar = usedAndLeftOut(counts, run.frames);
    out.chart(segmentedBar(bar));
    std::vector<ChartLegendItem> legend;
    for (const ChartSegment& segment : bar.segments)
    {
        legend.push_back(ChartLegendItem{.mark = ChartLegendItem::Mark::Swatch,
                                         .state = segment.state,
                                         .night = 0,
                                         .label = stateName(segment.state),
                                         .value = segment.valueText});
    }
    out.chart(chartLegend(legend));
    const bool anyLeftOut = std::any_of(counts.begin(), counts.end(), [](const auto& entry) {
        return entry.first != ChartState::Used && entry.second > 0;
    });
    if (anyLeftOut)
    {
        writeReasons(out, context, lights, groups);
    }
    else if (!counts.empty())
    {
        writeCaption(out, documentText("Every light the run took went into a master."));
    }
    if (!model.groupsNotStacked.empty())
    {
        QStringList names;
        for (const ReportGroupNotStacked& group : model.groupsNotStacked)
        {
            names << group.name;
        }
        writeCaption(out, documentText("No master was planned of %1: you left every one of its lights out.")
                              .arg(names.join(QStringLiteral(", "))));
    }
    out.close("div");

    openCard(out, documentText("By night"), documentText("lights used of those taken"));
    writeNightsTable(out, context, lights);
    out.close("div");

    out.close("div");
    out.close("section");
}

// ---- The nights.

/// A light's name in a tip: its group, then its file.
[[nodiscard]] QString lightHeading(const Light& light, const QStringList& groups)
{
    const QString group = light.group >= 0 && light.group < groups.size() ? groups.at(light.group) : QString();
    return group.isEmpty() ? light.frame->fileName : group + dotSeparator() + light.frame->fileName;
}

[[nodiscard]] ConditionsChart conditionsOf(const ReportDocumentContext& context, const std::vector<Light>& lights,
                                           const QStringList& groups)
{
    ConditionsChart chart;
    chart.description = {documentText("Sharpness, stars detected and background noise of every light, at the time it "
                                      "was taken"),
                         documentText("No light of this run states a measure to draw.")};
    chart.measures = {{documentText("FWHM (px)"), {}, {}},
                      {documentText("Stars detected"), {}, {}},
                      {documentText("Background noise (%1 the group's median)").arg(QChar(0x00D7)), {}, {}}};
    chart.groups = groups;

    std::vector<QDate> nights;
    for (const Light& light : lights)
    {
        if (light.frame->night.has_value() &&
            std::find(nights.begin(), nights.end(), *light.frame->night) == nights.end())
        {
            nights.push_back(*light.frame->night);
        }
    }
    std::sort(nights.begin(), nights.end());
    for (const QDate& night : nights)
    {
        chart.nights << formatDate(night);
    }

    // The background noise is read against the median of the light's own group: filters differ too much to share an
    // axis otherwise.
    std::map<int, std::vector<double>> noiseByGroup;
    for (const Light& light : lights)
    {
        if (light.frame->backgroundNoise.has_value())
        {
            noiseByGroup[light.group].push_back(*light.frame->backgroundNoise);
        }
    }
    std::map<int, double> medianNoise;
    for (auto& [group, values] : noiseByGroup)
    {
        if (const std::optional<double> median = medianOfValues(values); median.has_value() && *median > 0.0)
        {
            medianNoise[group] = *median;
        }
    }

    for (const Light& light : lights)
    {
        const ReportFrame& frame = *light.frame;
        ConditionsLight entry;
        entry.group = light.group;
        entry.night = -1;
        if (frame.night.has_value())
        {
            entry.night = static_cast<int>(std::find(nights.begin(), nights.end(), *frame.night) - nights.begin());
            if (frame.capturedAt.has_value())
            {
                entry.startMinutes = minutesIntoNight(*frame.night, *frame.capturedAt, context.zone);
            }
        }
        entry.exposureMinutes = frame.exposureSeconds.value_or(0.0) / 60.0;
        const auto median = medianNoise.find(light.group);
        const double relative = frame.backgroundNoise.has_value() && median != medianNoise.end()
                                    ? *frame.backgroundNoise / median->second
                                    : kNotANumber;
        entry.values = {valueOf(frame.fwhm), frame.stars.has_value() ? static_cast<double>(*frame.stars) : kNotANumber,
                        relative};
        entry.state = chartStateOf(frame);
        entry.tip.heading = lightHeading(light, groups);
        if (frame.capturedAt.has_value())
        {
            entry.tip.lines << formatDateTime(*frame.capturedAt, context.zone);
        }
        if (frame.fwhm.has_value())
        {
            entry.tip.lines << documentText("FWHM %1 px").arg(formatFigure(*frame.fwhm));
        }
        if (frame.stars.has_value())
        {
            entry.tip.lines << counted(*frame.stars, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 star"),
                                       QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 stars"));
        }
        if (std::isfinite(relative))
        {
            entry.tip.lines << documentText("Background noise %1 %2").arg(QChar(0x00D7)).arg(formatFigure(relative));
        }
        entry.tip.lines << fateLine(frame);
        chart.lights.push_back(entry);
    }
    return chart;
}

void writeNights(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<Light>& lights,
                 const QStringList& groups)
{
    const std::size_t nights = context.model.run.nights.size();
    const ConditionsChart chart = conditionsOf(context, lights, groups);
    const bool byClock = conditionsLayout(chart) == ConditionsLayout::ByClock;

    openBlock(out, "nights",
              nights > 1 ? documentText("The %1 nights").arg(formatCount(static_cast<qint64>(nights)))
                         : documentText("The night"),
              documentText("every light at the time it was taken"),
              documentText("A cross is a light the stacks left out. Point at a light for its figures."));
    out.open("div", {{"class", QStringLiteral("card")}});
    writeScrollingChart(out, conditionsChart(chart), chart.description.label);
    writeCaption(out, byClock ? documentText("Each night is a column on one clock axis, and each group's lights a "
                                             "band of time.")
                              : documentText("The lights are in the order they were taken, not on a clock axis: the "
                                             "run spans more than four nights, or a light states no capture time."));
    out.close("div");
    out.close("section");
}

// ---- The lights' quality.

void writeQuality(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<Light>& lights,
                  const QStringList& groups, const QHash<QString, QString>& labels)
{
    const ReportModel& model = context.model;
    const QString nothing = documentText("No light of this run states a measure to draw.");

    std::vector<double> usedFwhm;
    std::vector<double> usedWeights;
    ScatterChart scatter;
    scatter.description = {documentText("Eccentricity against FWHM, one mark per light"), nothing};
    scatter.xAxis.title = documentText("FWHM (px)");
    scatter.yAxis.title = documentText("Eccentricity");
    scatter.width = 340;
    scatter.height = 250;
    std::vector<ChartState> leftOutDrawn;
    for (const Light& light : lights)
    {
        const ReportFrame& frame = *light.frame;
        if (isUsed(frame) && frame.fwhm.has_value())
        {
            usedFwhm.push_back(*frame.fwhm);
        }
        if (isUsed(frame) && frame.weight.has_value())
        {
            usedWeights.push_back(*frame.weight);
        }
        if (frame.fwhm.has_value() && frame.eccentricity.has_value())
        {
            ChartPoint point;
            point.x = *frame.fwhm;
            point.y = *frame.eccentricity;
            point.state = chartStateOf(frame);
            point.tip.heading = lightHeading(light, groups);
            point.tip.lines = {documentText("FWHM %1 px").arg(formatFigure(*frame.fwhm)),
                               documentText("Eccentricity %1").arg(formatFigure(*frame.eccentricity)), fateLine(frame)};
            scatter.points.push_back(point);
            if (point.state != ChartState::Used &&
                std::find(leftOutDrawn.begin(), leftOutDrawn.end(), point.state) == leftOutDrawn.end())
            {
                leftOutDrawn.push_back(point.state);
            }
        }
    }

    openBlock(out, "quality", documentText("Light quality"),
              counted(static_cast<qint64>(lights.size()), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")),
              documentText("The same lights, summed up three ways."));
    out.open("div", {{"class", QStringLiteral("g3")}});

    // How sharp: master by master, or as one distribution when there is one master.
    if (!usedFwhm.empty())
    {
        if (model.masters.size() > 1)
        {
            RangeChart range;
            range.description = {documentText("FWHM by master: range, middle half and median"), nothing};
            range.axis.title = documentText("FWHM (px), lights used");
            range.width = 340;
            for (const ReportMaster& master : model.masters)
            {
                std::vector<double> values;
                for (const ReportFrame& frame : master.frames)
                {
                    if (isUsed(frame) && frame.fwhm.has_value())
                    {
                        values.push_back(*frame.fwhm);
                    }
                }
                const std::optional<ChartRange> figures = chartRange(values);
                if (!figures.has_value())
                {
                    continue;
                }
                ChartRangeRow row;
                row.label = labels.value(master.key, master.name);
                row.range = *figures;
                row.valueText = formatFigure(figures->median);
                row.tip.heading = row.label + dotSeparator() + documentText("FWHM");
                row.tip.lines = {
                    documentText("Median %1 px").arg(formatFigure(figures->median)),
                    documentText("Middle half %1 to %2")
                        .arg(formatFigure(figures->lowerQuartile), formatFigure(figures->upperQuartile)),
                    documentText("Range %1 to %2").arg(formatFigure(figures->lowest), formatFigure(figures->highest))};
                range.rows.push_back(row);
            }
            openCard(out, documentText("Sharpness by master"), documentText("lights used"));
            out.chart(rangeChart(range));
            writeCaption(out, documentText("Line: the whole range. Bar: the middle half. Dot and figure: the "
                                           "median."));
            out.close("div");
        }
        else
        {
            DistributionChart sharp;
            sharp.description = {documentText("How many lights lie at each FWHM"), nothing};
            sharp.axis.title = documentText("FWHM (px)");
            sharp.width = 340;
            sharp.height = 232;
            sharp.bins = chartBins(usedFwhm, 12);
            for (ChartBin& bin : sharp.bins)
            {
                bin.tip.heading = documentText("FWHM %1 to %2 px").arg(formatFigure(bin.low), formatFigure(bin.high));
                bin.tip.lines = {counted(static_cast<qint64>(bin.count),
                                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights"))};
            }
            openCard(out, documentText("How sharp"), documentText("lights used"));
            out.chart(distributionChart(sharp));
            writeCaption(out, documentText("How many lights lie at each FWHM."));
            out.close("div");
        }
    }

    if (!scatter.points.empty())
    {
        openCard(out, documentText("Sharp and round?"));
        out.chart(scatterChart(scatter));
        if (!leftOutDrawn.empty())
        {
            std::vector<ChartLegendItem> legend;
            legend.push_back(ChartLegendItem{.mark = ChartLegendItem::Mark::Dot,
                                             .state = ChartState::Used,
                                             .night = 0,
                                             .label = stateName(ChartState::Used),
                                             .value = {}});
            for (const ChartState state : kLeftOutStates)
            {
                if (std::find(leftOutDrawn.begin(), leftOutDrawn.end(), state) != leftOutDrawn.end())
                {
                    legend.push_back(ChartLegendItem{.mark = ChartLegendItem::Mark::Cross,
                                                     .state = state,
                                                     .night = 0,
                                                     .label = stateName(state),
                                                     .value = {}});
                }
            }
            out.chart(chartLegend(legend));
        }
        writeCaption(out, documentText("One mark per light. Up and to the right is soft and trailed."));
        out.close("div");
    }

    if (!usedWeights.empty())
    {
        DistributionChart weights;
        weights.description = {documentText("Distribution of the weights the stacks used"), nothing};
        weights.axis.title = documentText("Weight");
        weights.width = 340;
        weights.height = 232;
        weights.bins = chartBins(usedWeights, 10, 0.0, 1.0);
        for (ChartBin& bin : weights.bins)
        {
            bin.tip.heading = documentText("Weight %1 to %2").arg(formatFigure(bin.low), formatFigure(bin.high));
            bin.tip.lines = {counted(static_cast<qint64>(bin.count),
                                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights"))};
        }
        openCard(
            out,
            model.masters.size() > 1 ? documentText("Weights the stacks used") : documentText("Weights the stack used"),
            counted(static_cast<qint64>(usedWeights.size()), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                    QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")));
        out.chart(distributionChart(weights));
        writeCaption(out, documentText("A weight is relative to the heaviest light: a light at 1 counts fully."));
        out.close("div");
    }

    out.close("div");
    out.close("section");
}
} // namespace

// ---- What the writer calls.

QStringList summaryAnchors(const ReportModel& model)
{
    const SummaryPlan plan = planSummary(model);
    QStringList anchors = {QStringLiteral("summary"), QStringLiteral("attention")};
    if (plan.outcome)
    {
        anchors << QStringLiteral("outcome");
    }
    if (plan.made)
    {
        anchors << QStringLiteral("made");
    }
    if (plan.frames)
    {
        anchors << QStringLiteral("frames");
    }
    if (plan.nights)
    {
        anchors << QStringLiteral("nights");
    }
    if (plan.quality)
    {
        anchors << QStringLiteral("quality");
    }
    return anchors;
}

ReportMarkup writeReportHeader(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const ReportRun& run = model.run;
    // The picture of a run's one master stands in the header, beside the figures: such a run has no gallery.
    const bool pictured = model.masters.size() == 1 && !context.assets.image(model.masters.front().preview).isEmpty();

    ReportMarkup out;
    out.open("header", {{"class", pictured ? QStringLiteral("hero one") : QStringLiteral("hero")},
                        {"id", QStringLiteral("top")}});
    out.open("div", {{"class", QStringLiteral("hero-main")}});

    out.open("div", {{"class", QStringLiteral("hero-top")}});
    out.open("div");
    QString title;
    if (run.targets.size() == 1)
    {
        title = run.targets.front();
    }
    else if (run.targets.size() > 1)
    {
        title = counted(run.targets.size(), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 target"),
                        QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 targets"));
    }
    else
    {
        title = run.title;
    }
    out.element("h1", title);
    QStringList under;
    if (!run.process.trimmed().isEmpty())
    {
        under << run.process.trimmed();
    }
    if (!run.profile.trimmed().isEmpty())
    {
        under << run.profile.trimmed();
    }
    if (run.targets.size() > 1)
    {
        under << run.targets.join(QStringLiteral(", "));
    }
    if (const QString nights = nightsLine(run); !nights.isEmpty())
    {
        under << nights;
    }
    out.element("p", {{"class", QStringLiteral("sub")}}, under.join(dotSeparator()));
    out.close("div");
    out.element("span", {{"class", QStringLiteral("sp")}}, QString());
    const ReportStateWords words = outcomeWords(run.outcome);
    writePill(out, words.kind, words.text);
    out.open("button", {{"type", QStringLiteral("button")},
                        {"class", QStringLiteral("themebtn noprint")},
                        {"data-theme-toggle", QStringLiteral("1")}});
    out.icon("theme");
    out.element("span", documentText("Theme"));
    out.close("button");
    out.close("div");

    writeVerdict(out, context);

    out.open("div", {{"class", QStringLiteral("tiles")}});
    for (const Tile& tile : headerTiles(context))
    {
        writeTile(out, tile);
    }
    out.close("div");
    out.close("div");

    if (pictured)
    {
        writeHeaderPicture(out, context, model.masters.front());
    }

    if (!run.sessionNotes.trimmed().isEmpty())
    {
        out.open("div", {{"class", QStringLiteral("notes")}});
        out.element("b", documentText("Your notes"));
        out.open("div");
        out.element("p", run.sessionNotes.trimmed());
        if (run.sessionNotesChangedAt.has_value())
        {
            out.element("small",
                        documentText("Last changed %1").arg(formatDateTime(*run.sessionNotesChangedAt, context.zone)));
        }
        out.close("div");
        out.close("div");
    }
    out.close("header");
    return out;
}

ReportPart writeReportSummary(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const SummaryPlan plan = planSummary(model);
    const QHash<QString, QString>& labels = context.labels;
    const std::vector<Light> lights = lightsInCaptureOrder(model);
    const QStringList groups = lightGroupNames(model, labels);
    const IssueSplit issues = splitIssues(model);
    const std::size_t nights = model.run.nights.size();

    ReportPart part;
    part.heading = QStringLiteral("1") + dotSeparator() + documentText("Summary");

    ReportMarkup& out = part.body;
    openPart(out, "summary", 1,
             nights > 1 ? documentText("The nights in summary") : documentText("The night in summary"),
             model.masters.size() > 1
                 ? documentText("What was made, what needs a second look, and what the lights were like. "
                                "Everything below this part breaks these figures down.")
                 : documentText("What went into the master, what needs a second look, and what the lights were "
                                "like. Everything below this part breaks these figures down."));

    if (plan.outcome)
    {
        writeOutcome(out, context, labels);
        part.entries.push_back({QStringLiteral("outcome"), documentText("What happened"), {}, true});
    }

    writeAttention(out, context, issues);
    const int serious = issues.critical + issues.warnings;
    part.entries.push_back({QStringLiteral("attention"), documentText("Needs attention"),
                            serious > 0 ? formatCount(serious) : QString(), false});

    if (plan.made)
    {
        writeMade(out, context, labels);
        part.entries.push_back({QStringLiteral("made"), documentText("What was made"),
                                formatCount(static_cast<qint64>(model.masters.size())), false});
    }
    if (plan.frames)
    {
        writeFrames(out, context, lights, groups);
        part.entries.push_back(
            {QStringLiteral("frames"), documentText("Lights"),
             model.run.framesUsed.has_value()
                 ? QStringLiteral("%1/%2").arg(formatCount(*model.run.framesUsed), formatCount(model.run.frames))
                 : formatCount(model.run.frames),
             false});
    }
    if (plan.nights)
    {
        writeNights(out, context, lights, groups);
        part.entries.push_back({QStringLiteral("nights"),
                                nights > 1 ? documentText("The nights") : documentText("The night"),
                                nights > 1 ? formatCount(static_cast<qint64>(nights)) : QString(), false});
    }
    if (plan.quality)
    {
        writeQuality(out, context, lights, groups, labels);
        part.entries.push_back({QStringLiteral("quality"), documentText("Light quality"), {}, false});
    }

    out.close("section");
    return part;
}
} // namespace ImageIntegration::Reporting

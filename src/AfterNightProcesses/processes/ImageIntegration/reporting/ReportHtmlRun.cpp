// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The fifth part of the report's document: the run itself.
///
/// What the application did, in order, on one time axis - its own preparation, the stages, the commit, the archive
/// and this report - then what it decided on its own, the few settings that belong to the run and not to a group,
/// what the run wrote and used, and its diagnostics: the counters where they were collected, and the log as the
/// run wrote it. The log is printed, never read for a figure.

#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <algorithm>
#include <optional>

namespace ImageIntegration::Reporting
{
namespace
{
/// Which blocks of the part a model calls for.
struct RunPlan
{
    bool timeline = false;
    bool decisions = false;
    bool once = false;
    bool resources = false;
    bool diagnostics = false;
};

[[nodiscard]] RunPlan planRun(const ReportModel& model)
{
    const ReportRun& run = model.run;
    RunPlan plan;
    plan.timeline = !model.stages.empty() || run.preparation.has_value() || run.executor.has_value();
    plan.decisions = !model.decisions.empty();
    plan.once = !run.settings.empty() || !run.mode.trimmed().isEmpty();
    plan.resources = !run.disk.empty() || run.engineMemory.has_value() || !run.cacheDiagnostics.empty();
    plan.diagnostics = !run.counters.empty() || !run.log.trimmed().isEmpty();
    return plan;
}

// ---- Stage by stage.

[[nodiscard]] ChartStageState chartStageOf(const ReportStageState state)
{
    switch (state)
    {
    case ReportStageState::Ran:
        return ChartStageState::Ran;
    case ReportStageState::Partial:
        return ChartStageState::Partial;
    case ReportStageState::Reused:
        return ChartStageState::Reused;
    case ReportStageState::Failed:
        return ChartStageState::Failed;
    case ReportStageState::Cancelled:
        return ChartStageState::Cancelled;
    case ReportStageState::NotReached:
        return ChartStageState::NotReached;
    case ReportStageState::Skipped:
        return ChartStageState::Skipped;
    }
    return ChartStageState::Ran;
}

/// What became of a stage, in a word or two.
[[nodiscard]] QString stageStateName(const ReportStageState state)
{
    switch (state)
    {
    case ReportStageState::Ran:
        return documentText("Ran");
    case ReportStageState::Partial:
        return documentText("Ran in part");
    case ReportStageState::Reused:
        return documentText("Reused");
    case ReportStageState::Failed:
        return documentText("Failed");
    case ReportStageState::Cancelled:
        return documentText("Cancelled");
    case ReportStageState::NotReached:
        return documentText("Not reached");
    case ReportStageState::Skipped:
        return documentText("No work");
    }
    return {};
}

[[nodiscard]] QString stageStateMeaning(const ReportStageState state)
{
    switch (state)
    {
    case ReportStageState::Ran:
        return documentText("The stage ran to its end.");
    case ReportStageState::Partial:
        return documentText("The stage ran and some of its work failed.");
    case ReportStageState::Reused:
        return documentText("Everything the stage makes was still valid, so it made nothing again.");
    case ReportStageState::Failed:
        return documentText("The stage failed.");
    case ReportStageState::Cancelled:
        return documentText("The run was cancelled inside the stage.");
    case ReportStageState::NotReached:
        return documentText("The run stopped before the stage.");
    case ReportStageState::Skipped:
        return documentText("The run had no work for the stage.");
    }
    return {};
}

/// One row of the run's time: a stage, or a piece of the application's own work around the stages.
struct TimeRow
{
    QString name;
    QString note;
    std::optional<ReportStageState> state;
    std::optional<qint64> durationMs;
    int groups = 0;
};

[[nodiscard]] std::vector<TimeRow> timeRows(const ReportModel& model)
{
    const ReportRun& run = model.run;
    std::vector<TimeRow> rows;
    if (run.preparation.has_value())
    {
        rows.push_back({documentText("Building the manifest"), documentText("before the stages"), std::nullopt,
                        run.preparation->manifestBuildMs, 0});
        rows.push_back({documentText("Planning and validating"), documentText("before the stages"), std::nullopt,
                        run.preparation->planAndValidationMs, 0});
    }
    for (const ReportStage& stage : model.stages)
    {
        rows.push_back({stage.name, stage.note.trimmed(), stage.state, stage.durationMs, stage.groups});
    }
    if (run.executor.has_value())
    {
        rows.push_back({documentText("Committing the run's state"), documentText("after the stages"), std::nullopt,
                        run.executor->stateCommitMs, 0});
        rows.push_back({documentText("Archiving the run"), documentText("after the stages"), std::nullopt,
                        run.executor->archiveMs, 0});
        rows.push_back({documentText("Writing this report"), documentText("after the stages"), std::nullopt,
                        run.executor->reportMs, 0});
    }
    return rows;
}

[[nodiscard]] QString groupsText(const int groups)
{
    return counted(groups, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 group"),
                   QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 groups"));
}

void writeTimeline(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const std::vector<TimeRow> rows = timeRows(model);

    TimelineChart chart;
    chart.description = {documentText("The stages of the run and the application's own work, on one time axis"),
                         documentText("The run timed no stage.")};
    chart.stageHeading = documentText("Stage");
    chart.valueHeading = documentText("Time");
    for (const TimeRow& row : rows)
    {
        ChartStage stage;
        stage.name = row.name;
        stage.minor = !row.state.has_value();
        stage.state = row.state.has_value() ? chartStageOf(*row.state) : ChartStageState::Ran;
        QStringList note;
        if (row.groups > 0)
        {
            note << groupsText(row.groups);
        }
        if (!row.note.isEmpty())
        {
            note << context.said(row.note);
        }
        stage.note = note.join(dotSeparator());
        if (row.durationMs.has_value())
        {
            stage.durationMs = static_cast<double>(*row.durationMs);
        }
        const bool ran = !row.state.has_value() || *row.state == ReportStageState::Ran;
        stage.valueText = row.durationMs.has_value() && (ran || *row.durationMs > 0)
                              ? formatDurationMs(*row.durationMs)
                              : (row.state.has_value() ? stageStateName(*row.state).toLower() : noValueText());
        stage.tip.heading = row.name;
        if (row.state.has_value())
        {
            stage.tip.lines << stageStateMeaning(*row.state);
        }
        if (row.groups > 0)
        {
            stage.tip.lines << groupsText(row.groups);
        }
        if (row.durationMs.has_value())
        {
            stage.tip.lines << formatDurationMs(*row.durationMs);
        }
        chart.stages.push_back(stage);
    }

    openBlock(out, "timeline", documentText("Stage by stage"),
              model.run.processingMs > 0
                  ? documentText("%1 in the executor").arg(formatDurationMs(model.run.processingMs))
                  : QString(),
              documentText("The application's own work before and after the stages is drawn in grey."));
    out.open("div", {{"class", QStringLiteral("card")}});
    writeScrollingChart(out, timelineChart(chart), chart.description.label);

    // A run that stopped says where, and leads to what it left unmade. A stage that went on after part of its work
    // failed - a light with no transform, a group that could not be stacked - did not stop the run, and says that.
    const auto stoppedAt = std::find_if(model.stages.begin(), model.stages.end(), [](const ReportStage& stage) {
        return stage.state == ReportStageState::Failed || stage.state == ReportStageState::Cancelled;
    });
    const auto faltered = std::find_if(model.stages.begin(), model.stages.end(), [](const ReportStage& stage) {
        return stage.state == ReportStageState::Partial;
    });
    if (stoppedAt == model.stages.end() && faltered != model.stages.end())
    {
        out.open("p", {{"class", QStringLiteral("cap")}});
        out.text(documentText("Part of %1 failed, and the run went on.").arg(faltered->name));
        if (context.hasAnchor(QStringLiteral("attention")))
        {
            out.text(QStringLiteral(" "));
            out.element("a", {{"href", QStringLiteral("#attention")}}, documentText("What failed"));
        }
        out.close("p");
    }
    if (stoppedAt != model.stages.end())
    {
        const int notReached =
            static_cast<int>(std::count_if(model.stages.begin(), model.stages.end(), [](const ReportStage& stage) {
                return stage.state == ReportStageState::NotReached;
            }));
        out.open("p", {{"class", QStringLiteral("cap")}});
        out.text(documentText("The run stopped in %1.").arg(stoppedAt->name));
        if (notReached > 0)
        {
            out.text(QStringLiteral(" "));
            out.text(counted(notReached,
                             QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 stage after it was not reached."),
                             QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 stages after it were not reached.")));
        }
        if (context.hasAnchor(QStringLiteral("outcome")))
        {
            out.text(QStringLiteral(" "));
            out.element("a", {{"href", QStringLiteral("#outcome")}}, documentText("What was kept and what was not"));
        }
        out.close("p");
    }

    // The chart's values, for a reader who cannot see it and for one who wants the figures.
    openFold(out, documentText("The same as a table"),
             counted(static_cast<qint64>(rows.size()), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 row"),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 rows")));
    openTable(out, QStringLiteral("stages"), documentText("The stages of the run"),
              {{documentText("Stage")},
               {documentText("What became of it")},
               {documentText("Groups"), true},
               {documentText("Time"), true},
               {documentText("Note")}});
    for (const TimeRow& row : rows)
    {
        out.open("tr");
        out.open("td");
        out.element(row.state.has_value() ? "b" : "span", row.name);
        out.close("td");
        out.element("td",
                    row.state.has_value() ? stageStateName(*row.state) : documentText("The application's own work"));
        out.element("td", {{"class", QStringLiteral("r num")}},
                    row.groups > 0 ? formatCount(row.groups) : noValueText());
        out.element("td", {{"class", QStringLiteral("r num")}},
                    row.durationMs.has_value() ? formatDurationMs(*row.durationMs) : noValueText());
        out.element("td", row.note.isEmpty() ? noValueText() : context.said(row.note));
        out.close("tr");
    }
    closeTable(out);
    closeFold(out);
    out.close("div");
    out.close("section");
}

// ---- Left to the application.

void writeDecisions(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    openBlock(out, "decisions", documentText("Left to the application"),
              counted(static_cast<qint64>(model.decisions.size()),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 setting on Auto"),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 settings on Auto")),
              documentText("What was asked for, what it resolved to, and why."));
    out.open("div", {{"class", QStringLiteral("card")}});
    openTable(out, QStringLiteral("decisions"), documentText("The settings the application decided"),
              {{documentText("Setting")},
               {documentText("For")},
               {documentText("Asked for")},
               {documentText("Resolved to")},
               {documentText("Why")}});
    for (const ReportDecision& decision : model.decisions)
    {
        out.open("tr");
        out.open("td");
        out.element("b", decision.area);
        out.close("td");
        out.open("td");
        if (const ReportMaster* master = masterOfKey(model, decision.groupKey); master != nullptr)
        {
            writeMasterLink(out, context, *master, context.labelOf(*master));
        }
        else if (const ReportRegistrationGroup* group = registrationOfKey(model, decision.groupKey); group != nullptr)
        {
            writeAnchorLink(out, context, group->slug, group->name);
        }
        else
        {
            out.text(decision.group.trimmed().isEmpty() ? noValueText() : decision.group);
        }
        out.close("td");
        out.element("td", decision.requested.trimmed().isEmpty() ? noValueText() : context.said(decision.requested));
        out.element("td", decision.resolved.trimmed().isEmpty() ? noValueText() : context.said(decision.resolved));
        out.element("td", decision.reason.trimmed().isEmpty() ? noValueText() : context.said(decision.reason));
        out.close("tr");
    }
    closeTable(out);
    out.close("div");
    out.close("section");
}

// ---- Stated once for the run.

void writeOnce(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportRun& run = context.model.run;
    openBlock(out, "once", documentText("Stated once for the run"),
              documentText("the only settings that do not belong to a group"), {});
    out.open("div", {{"class", QStringLiteral("gfit")}});

    ReportSettingsBlock scope;
    scope.title = documentText("The run");
    const auto add = [&scope](const QString& name, const QString& value) {
        if (!value.trimmed().isEmpty())
        {
            scope.rows.push_back(ReportSettingsRow{.name = name, .value = value, .requested = {}, .reason = {}});
        }
    };
    add(documentText("Mode"), run.mode);
    add(documentText("Scope"), run.scope);
    add(documentText("Only group"), run.scopeName);
    if (run.incremental)
    {
        add(documentText("Groups found changed"),
            run.changedGroups.isEmpty() ? documentText("None") : run.changedGroups.join(QStringLiteral(", ")));
    }
    if (!scope.rows.empty())
    {
        writeSettingsBlock(out, context, scope, {}, "h4", nullptr);
    }
    for (const ReportSettingsBlock& block : run.settings)
    {
        writeSettingsBlock(out, context, block, {}, "h4", nullptr);
    }
    out.close("div");
    out.close("section");
}

// ---- What the run wrote and used.

void writeResources(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportRun& run = context.model.run;
    openBlock(out, "resources", documentText("What the run wrote and used"), {}, {});
    // Two cards side by side; a run that states one has it across, its chart beside its table.
    const bool memory = run.engineMemory.has_value() || !run.cacheDiagnostics.empty();
    const bool both = !run.disk.empty() && memory;
    out.open("div", {{"class", both ? QStringLiteral("g2") : QStringLiteral("g1")}});

    if (!run.disk.empty())
    {
        qint64 total = 0;
        BarsChart bars;
        bars.description = {documentText("What the run wrote to disk, by kind"),
                            documentText("The run recorded nothing written to disk.")};
        bars.width = 460;
        bars.labelWidth = 170;
        for (const ReportDiskUse& use : run.disk)
        {
            total += use.bytes;
            ChartBar bar;
            bar.label = use.kind;
            bar.value = static_cast<double>(use.bytes);
            bar.valueText = formatByteSize(use.bytes);
            bar.tip.heading = use.kind;
            bar.tip.lines = {formatByteSize(use.bytes),
                             counted(use.files, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 file"),
                                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 files"))};
            bars.bars.push_back(bar);
        }
        openCard(out, documentText("Written to disk"), formatByteSize(total));
        out.open("div", {{"class", both ? QStringLiteral("g1") : QStringLiteral("g2")}});
        out.open("div");
        out.chart(barsChart(bars));
        out.close("div");
        openTable(out, QStringLiteral("disk"), documentText("What the run wrote to disk"),
                  {{documentText("Kind")}, {documentText("Files"), true}, {documentText("Size"), true}});
        for (const ReportDiskUse& use : run.disk)
        {
            out.open("tr");
            out.element("td", use.kind);
            out.element("td", {{"class", QStringLiteral("r num")}}, formatCount(use.files));
            out.element("td", {{"class", QStringLiteral("r num")}}, formatByteSize(use.bytes));
            out.close("tr");
        }
        closeTable(out);
        out.close("div");
        out.close("div");
    }

    if (memory)
    {
        openCard(out, documentText("Memory and caches"));
        std::vector<ReportFact> facts;
        if (run.engineMemory.has_value())
        {
            const ReportEngineMemory& memory = *run.engineMemory;
            facts.push_back({documentText("Engine memory at its peak"),
                             formatByteSize(static_cast<qint64>(memory.peakBytes)),
                             documentText("The most memory the engine held at once during the run.")});
            facts.push_back({documentText("Peak before the run"),
                             formatByteSize(static_cast<qint64>(memory.peakBytesBeforeRun)),
                             {}});
            facts.push_back({documentText("Still held at the end"),
                             formatByteSize(static_cast<qint64>(memory.liveBytesAtEnd)),
                             {}});
            facts.push_back({documentText("Allocations"), formatCount(static_cast<qint64>(memory.allocations)), {}});
            facts.push_back(
                {documentText("Allocated in all"), formatByteSize(static_cast<qint64>(memory.bytesAllocated)), {}});
            facts.push_back({documentText("Allocations refused"),
                             formatCount(static_cast<qint64>(memory.deniedAllocations)),
                             documentText("Requests the engine's memory budget turned down.")});
        }
        for (const ReportCounter& counter : run.cacheDiagnostics)
        {
            facts.push_back({counter.name, formatCount(static_cast<qint64>(counter.value)), {}});
        }
        writeFacts(out, facts);
        out.close("div");
    }

    out.close("div");
    out.close("section");
}

// ---- Diagnostics.

/// The run's log, line by line, as the run wrote it: a stage's heading is set in bold, a warning and an error are
/// marked, and nothing is read out of it.
void writeLog(ReportMarkup& out, const ReportDocumentContext& context, const QString& log)
{
    const QStringList lines = context.assets.message(log).split(QLatin1Char('\n'));
    out.open("pre", {{"class", QStringLiteral("log")},
                     {"tabindex", QStringLiteral("0")},
                     {"role", QStringLiteral("group")},
                     {"aria-label", documentText("The run's log")}});
    for (const QString& line : lines)
    {
        if (line.contains(QStringLiteral("STAGE:")))
        {
            out.element("b", line);
        }
        else if (line.contains(QStringLiteral("[ERROR]")))
        {
            out.element("span", {{"class", QStringLiteral("log-error")}}, line);
        }
        else if (line.contains(QStringLiteral("[WARNING]")) || line.contains(QStringLiteral("[WARN]")))
        {
            out.element("span", {{"class", QStringLiteral("log-warning")}}, line);
        }
        else
        {
            out.text(line);
        }
        out.text(QStringLiteral("\n"));
    }
    out.close("pre");
}

void writeDiagnostics(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportRun& run = context.model.run;
    openBlock(out, "diagnostics", documentText("Diagnostics"), {}, {});
    out.open("div", {{"class", QStringLiteral("card")}});
    if (!run.counters.empty())
    {
        openFold(out, documentText("Execution counters"), documentText("how much work the run did"));
        openTable(out, QStringLiteral("counters"), documentText("The run's execution counters"),
                  {{documentText("Counter")}, {documentText("Count"), true}});
        for (const ReportCounter& counter : run.counters)
        {
            out.open("tr");
            out.element("td", counter.name);
            out.element("td", {{"class", QStringLiteral("r num")}}, formatCount(static_cast<qint64>(counter.value)));
            out.close("tr");
        }
        closeTable(out);
        closeFold(out);
    }
    if (!run.log.trimmed().isEmpty())
    {
        const qsizetype lines = run.log.trimmed().count(QLatin1Char('\n')) + 1;
        openFold(out, documentText("Log"),
                 counted(lines, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 line, as the run wrote it"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lines, as the run wrote them")));
        writeLog(out, context, run.log.trimmed());
        closeFold(out);
    }
    out.close("div");
    out.close("section");
}
} // namespace

// ---- What the writer calls.

QStringList runAnchors(const ReportModel& model)
{
    const RunPlan plan = planRun(model);
    QStringList anchors = {QStringLiteral("run")};
    if (plan.timeline)
    {
        anchors << QStringLiteral("timeline");
    }
    if (plan.decisions)
    {
        anchors << QStringLiteral("decisions");
    }
    if (plan.once)
    {
        anchors << QStringLiteral("once");
    }
    if (plan.resources)
    {
        anchors << QStringLiteral("resources");
    }
    if (plan.diagnostics)
    {
        anchors << QStringLiteral("diagnostics");
    }
    return anchors;
}

ReportPart writeReportRun(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const RunPlan plan = planRun(model);
    ReportPart part;
    part.heading = QStringLiteral("5") + dotSeparator() + documentText("The run");

    ReportMarkup& out = part.body;
    openPart(out, "run", 5, documentText("The run"),
             documentText("What the application did, in order, what it decided on its own, and what it used."));
    if (plan.timeline)
    {
        writeTimeline(out, context);
        part.entries.push_back({QStringLiteral("timeline"), documentText("Stage by stage"), {}, runStopped(model)});
    }
    if (plan.decisions)
    {
        writeDecisions(out, context);
        part.entries.push_back({QStringLiteral("decisions"), documentText("Left to the application"),
                                formatCount(static_cast<qint64>(model.decisions.size())), false});
    }
    if (plan.once)
    {
        writeOnce(out, context);
        part.entries.push_back({QStringLiteral("once"), documentText("Stated once"), {}, false});
    }
    if (plan.resources)
    {
        writeResources(out, context);
        part.entries.push_back({QStringLiteral("resources"), documentText("Written and used"), {}, false});
    }
    if (plan.diagnostics)
    {
        writeDiagnostics(out, context);
        part.entries.push_back({QStringLiteral("diagnostics"), documentText("Diagnostics"), {}, false});
    }
    out.close("section");
    return part;
}
} // namespace ImageIntegration::Reporting

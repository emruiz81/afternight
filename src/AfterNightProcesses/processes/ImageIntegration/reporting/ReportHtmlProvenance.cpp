// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sixth part of the report's document: provenance.
///
/// What the lights said about themselves and what would be needed to do the run again: how each group was taken,
/// with what equipment, what the headers state, which application wrote the report and under which fingerprints,
/// the run in one line to copy, every input file, and - last - what the report could not state and why.

#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <algorithm>
#include <optional>

namespace ImageIntegration::Reporting
{
namespace
{
/// Which blocks of the part a model calls for.
struct ProvenancePlan
{
    bool acquisition = false;
    bool equipment = false;
    bool inputs = false;
    bool notStated = false;
};

[[nodiscard]] ProvenancePlan planProvenance(const ReportModel& model)
{
    ProvenancePlan plan;
    plan.acquisition = !model.run.acquisition.empty();
    plan.equipment =
        !model.run.equipment.empty() || (model.run.headers.has_value() && !model.run.headers->fields.empty());
    plan.inputs = std::any_of(model.masters.begin(), model.masters.end(),
                              [](const ReportMaster& master) { return !master.frames.empty(); }) ||
                  std::any_of(model.groupsNotStacked.begin(), model.groupsNotStacked.end(),
                              [](const ReportGroupNotStacked& group) { return !group.frames.empty(); }) ||
                  std::any_of(model.calibrationMasters.begin(), model.calibrationMasters.end(),
                              [](const ReportCalibrationMaster& master) { return !master.inputFiles.isEmpty(); });
    plan.notStated = !model.notStated.empty();
    return plan;
}

// ---- Acquisition.

/// A sensor's temperature over some lights: one figure, or the range. Where only one bound is stated, that bound
/// is the figure.
[[nodiscard]] QString temperatureText(const std::optional<double>& lowest, const std::optional<double>& highest)
{
    if (!lowest.has_value() && !highest.has_value())
    {
        return noValueText();
    }
    // A bound is read only where it is stated: the other one stands in for a bound that is not.
    const double low = lowest.has_value() ? *lowest : *highest;
    const double high = highest.has_value() ? *highest : *lowest;
    const QString unit = QStringLiteral(" °C");
    return formatFigure(low) == formatFigure(high)
               ? formatFigure(low) + unit
               : documentText("%1 to %2").arg(formatFigure(low), formatFigure(high)) + unit;
}

[[nodiscard]] QString exposureBins(const ReportExposure& exposure)
{
    QStringList parts;
    for (const ReportExposureBin& bin : exposure.bins)
    {
        parts << QStringLiteral("%1 × %2 s").arg(formatCount(bin.frames), formatFigure(bin.seconds));
    }
    const int without = exposure.frames - exposure.framesWithExposure;
    if (without > 0)
    {
        parts << counted(without, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 that states none"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 that state none"));
    }
    return parts.isEmpty() ? noValueText() : parts.join(QStringLiteral(", "));
}

void writeAcquisition(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    openBlock(out, "acquisition", documentText("Acquisition"), documentText("from the lights' headers"),
              documentText("How each group of lights was taken, and its share of the exposure the run took."));
    out.open("div", {{"class", QStringLiteral("card")}});
    openTable(
        out, QStringLiteral("acquisition"), documentText("How the lights were taken, group by group"),
        {{documentText("Group")},
         {documentText("Lights"), true},
         {documentText("Exposures")},
         {documentText("Share"), true, documentText("The group's share of the exposure of every light the run took.")},
         {documentText("Gain")},
         {documentText("Offset")},
         {documentText("Sensor")},
         {documentText("First light")},
         {documentText("Last light")}});
    const auto writeRows = [&](const QString& target, const bool byTarget) {
        for (const ReportAcquisitionRow& row : model.run.acquisition)
        {
            if (byTarget && row.target != target)
            {
                continue;
            }
            const ReportMaster* master = masterOfKey(model, row.groupKey);
            std::optional<QDateTime> first;
            std::optional<QDateTime> last;
            if (master != nullptr)
            {
                for (const ReportFrame& frame : master->frames)
                {
                    if (frame.capturedAt.has_value())
                    {
                        first = first.has_value() ? (std::min)(*first, *frame.capturedAt) : *frame.capturedAt;
                        last = last.has_value() ? (std::max)(*last, *frame.capturedAt) : *frame.capturedAt;
                    }
                }
            }
            out.open("tr");
            out.open("td");
            out.open("b");
            if (master != nullptr)
            {
                writeMasterLink(out, context, *master, context.labelOf(*master));
            }
            else
            {
                out.text(row.group);
            }
            out.close("b");
            out.close("td");
            out.element("td", {{"class", QStringLiteral("r num")}}, formatCount(row.exposure.frames));
            out.element("td", exposureBins(row.exposure));
            out.open("td", {{"class", QStringLiteral("r num")}});
            out.text(percentText(row.share));
            out.chart(cellBar(row.share));
            out.close("td");
            out.element("td", row.gain.isEmpty() ? noValueText() : row.gain.join(QStringLiteral(", ")));
            out.element("td", row.offset.isEmpty() ? noValueText() : row.offset.join(QStringLiteral(", ")));
            out.element("td", {{"class", QStringLiteral("nowrap")}},
                        temperatureText(row.lowestTemperature, row.highestTemperature));
            out.element("td", {{"class", QStringLiteral("nowrap")}},
                        first.has_value() ? formatDateTime(*first, context.zone) : noValueText());
            out.element("td", {{"class", QStringLiteral("nowrap")}},
                        last.has_value() ? formatDateTime(*last, context.zone) : noValueText());
            out.close("tr");
        }
    };
    if (model.targets.size() > 1)
    {
        for (const ReportTarget& target : model.targets)
        {
            out.open("tr", {{"class", QStringLiteral("grouprow")}});
            out.open("th", {{"colspan", QStringLiteral("9")}, {"scope", QStringLiteral("colgroup")}});
            out.text(target.name);
            out.close("th");
            out.close("tr");
            writeRows(target.name, true);
        }
    }
    else
    {
        writeRows({}, false);
    }
    closeTable(out);
    out.close("div");
    out.close("section");
}

// ---- Equipment and headers.

void writeEquipment(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportRun& run = context.model.run;
    const bool equipment = !run.equipment.empty();
    const bool headers = run.headers.has_value() && !run.headers->fields.empty();
    openBlock(out, "equipment", documentText("Equipment and headers"), {}, {});
    out.open("div", {{"class", QStringLiteral("g1")}});

    if (equipment)
    {
        openCard(out, documentText("Equipment"), documentText("as the masters' headers state it"));
        openTable(out, QStringLiteral("equipment"), documentText("The equipment the masters were taken with"),
                  {{documentText("Masters")},
                   {documentText("Camera")},
                   {documentText("Telescope")},
                   {documentText("Taken at")},
                   {documentText("Master")},
                   {documentText("Gain")},
                   {documentText("Offset")},
                   {documentText("Site")}});
        const auto cell = [&out](const QString& value) {
            out.element("td", value.trimmed().isEmpty() ? noValueText() : value);
        };
        for (const ReportEquipmentRow& row : run.equipment)
        {
            out.open("tr");
            out.open("td");
            out.element("b", row.masters.join(QStringLiteral(", ")));
            out.close("td");
            cell(row.camera);
            cell(row.telescope);
            cell(row.captureResolution);
            cell(row.outputResolution);
            cell(row.gain);
            cell(row.offset);
            cell(row.site);
            out.close("tr");
        }
        closeTable(out);
        out.close("div");
    }

    if (headers)
    {
        const ReportHeaderCompleteness& stated = *run.headers;
        BarsChart bars;
        bars.description = {documentText("How many lights' headers state each field"),
                            documentText("No header was read.")};
        bars.width = 420;
        bars.labelWidth = 150;
        double shares = 0.0;
        for (const ReportHeaderField& field : stated.fields)
        {
            ChartBar bar;
            bar.label = field.name;
            bar.value = static_cast<double>(field.present);
            bar.valueText = formatCount(field.present);
            bar.tip.heading = field.name;
            bar.tip.lines = {counted(field.present, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light states it"),
                                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights state it"))};
            if (stated.lights > 0)
            {
                const double share = static_cast<double>(field.present) / static_cast<double>(stated.lights);
                shares += share;
                bar.tip.lines << percentText(share);
            }
            bars.bars.push_back(bar);
        }
        // The axis reaches every light, so a field every light states fills its row.
        if (!bars.bars.empty() && stated.lights > 0)
        {
            const bool full =
                std::any_of(stated.fields.begin(), stated.fields.end(),
                            [&stated](const ReportHeaderField& field) { return field.present >= stated.lights; });
            if (!full)
            {
                ChartBar all;
                all.label = documentText("Every light");
                all.value = static_cast<double>(stated.lights);
                all.valueText = formatCount(stated.lights);
                all.tip.heading = documentText("Every light");
                all.tip.lines = {documentText("What a field every light states would reach.")};
                bars.bars.insert(bars.bars.begin(), all);
            }
        }
        openCard(out, documentText("What the headers state"),
                 documentText("lights that state each, of %1").arg(formatCount(stated.lights)));
        out.open("div", {{"class", QStringLiteral("half")}});
        out.chart(barsChart(bars));
        out.close("div");
        if (stated.lights > 0 && !stated.fields.empty())
        {
            writeCaption(out, documentText("On average a field is stated by %1 of the lights. A field that is "
                                           "missing is one the report cannot state, not a zero.")
                                  .arg(percentText(shares / static_cast<double>(stated.fields.size()))));
        }
        out.close("div");
    }

    out.close("div");
    out.close("section");
}

// ---- Software and fingerprints.

/// The run in one line, to copy into a message.
[[nodiscard]] QString oneLine(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const ReportRun& run = model.run;
    QStringList parts;
    if (!run.targets.isEmpty())
    {
        parts << run.targets.join(QStringLiteral(", "));
    }
    parts << (run.framesUsed.has_value()
                  ? documentText("%1 of %2 lights").arg(formatCount(*run.framesUsed), formatCount(run.frames))
                  : counted(run.frames, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                            QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")));
    parts << (run.integratedMs.has_value() ? formatDurationMs(*run.integratedMs) : formatDurationMs(run.capturedMs));
    parts << counted(static_cast<qint64>(model.masters.size()),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 master"),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 masters"));
    parts << (run.applicationVersion.trimmed().isEmpty()
                  ? QStringLiteral("AfterNight")
                  : QStringLiteral("AfterNight %1").arg(run.applicationVersion.trimmed()));
    if (!run.executionId.trimmed().isEmpty())
    {
        parts << documentText("run %1").arg(run.executionId.trimmed());
    }
    if (!run.fingerprint.trimmed().isEmpty())
    {
        parts << documentText("fingerprint %1").arg(run.fingerprint.trimmed().left(12));
    }
    return parts.join(dotSeparator());
}

void writeSoftware(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportRun& run = context.model.run;
    openBlock(out, "software", documentText("Software and fingerprints"), {}, {});
    out.open("div", {{"class", QStringLiteral("g2")}});

    openCard(out, documentText("What wrote this"));
    std::vector<ReportFact> facts;
    facts.push_back({documentText("Application"),
                     run.applicationVersion.trimmed().isEmpty()
                         ? QStringLiteral("AfterNight")
                         : QStringLiteral("AfterNight %1").arg(run.applicationVersion.trimmed()),
                     {}});
    facts.push_back({documentText("Platform"), run.platform, {}});
    facts.push_back({documentText("Process"), run.process, {}});
    facts.push_back({documentText("Profile"), run.profile, {}});
    facts.push_back({documentText("Run"), run.executionId, {}});
    if (run.archivedAt.has_value())
    {
        facts.push_back({documentText("Archived"),
                         QStringLiteral("%1 (%2)").arg(formatDateTime(*run.archivedAt, context.zone),
                                                       formatUtcOffset(run.utcOffsetSeconds)),
                         {}});
    }
    facts.push_back({documentText("Time zone"), run.timeZoneId, {}});
    if (run.archiveFormat > 0)
    {
        facts.push_back({documentText("Archive format"), formatCount(run.archiveFormat), {}});
    }
    if (run.manifestFormat > 0)
    {
        facts.push_back({documentText("Manifest format"), formatCount(run.manifestFormat), {}});
    }
    facts.push_back({documentText("Report format"), formatCount(context.model.reportSchema), {}});
    facts.push_back({documentText("Output folder"), context.assets.message(run.outputFolder), {}});
    writeFacts(out, facts);
    out.close("div");

    openCard(out, documentText("Fingerprints"));
    openFacts(out);
    const auto fingerprint = [&out](const QString& name, const QString& value, const QString& meaning) {
        if (value.trimmed().isEmpty())
        {
            return;
        }
        openFact(out, name, meaning);
        out.element("code", value.trimmed());
        closeFact(out);
    };
    fingerprint(documentText("Run"), run.fingerprint,
                documentText("Made of the two below: a run with the same settings and the same inputs has the same "
                             "one."));
    fingerprint(documentText("Settings"), run.settingsFingerprint, documentText("Every setting of every group."));
    fingerprint(documentText("Inputs"), run.inputsFingerprint, documentText("Every input file."));
    closeFacts(out);
    writePanelTitle(out, "h5", documentText("In one line"), documentText("select it to copy"));
    out.element("p", {{"class", QStringLiteral("shareline")}}, oneLine(context));
    out.close("div");

    out.close("div");
    out.close("section");
}

// ---- The input files.

/// A list of files: under each folder's path, the names of its files. A document that travels alone prints the
/// names and no folder.
void writeFileList(ReportMarkup& out, const ReportDocumentContext& context, const QStringList& files)
{
    const bool folders = context.assets.form() == ReportForm::Linked;
    QString folder;
    bool open = false;
    const auto closeList = [&out, &open]() {
        if (open)
        {
            out.close("ul");
            open = false;
        }
    };
    for (const QString& file : files)
    {
        QString normal = file.trimmed();
        normal.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const qsizetype slash = normal.lastIndexOf(QLatin1Char('/'));
        const QString in = slash > 0 ? normal.left(slash) : QString();
        const QString name = normal.mid(slash + 1);
        if (!open || in != folder)
        {
            closeList();
            folder = in;
            if (folders && !folder.isEmpty())
            {
                out.open("p", {{"class", QStringLiteral("folder")}});
                out.element("code", folder);
                out.close("p");
            }
            out.open("ul", {{"class", QStringLiteral("filelist")}});
            open = true;
        }
        out.element("li", name);
    }
    closeList();
}

void writeInputs(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    openBlock(out, "inputs", documentText("Input files"), documentText("every file the run was given, by group"), {});
    out.open("div", {{"class", QStringLiteral("card")}});
    const auto framesOf = [](const std::vector<ReportFrame>& frames) {
        QStringList files;
        for (const ReportFrame& frame : frames)
        {
            files << (frame.path.trimmed().isEmpty() ? frame.fileName : frame.path);
        }
        return files;
    };
    for (const ReportMaster& master : model.masters)
    {
        if (master.frames.empty())
        {
            continue;
        }
        openFold(out, documentText("Lights of %1").arg(context.labelOf(master)),
                 counted(static_cast<qint64>(master.frames.size()),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 file"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 files")));
        writeFileList(out, context, framesOf(master.frames));
        closeFold(out);
    }
    for (const ReportGroupNotStacked& group : model.groupsNotStacked)
    {
        if (group.frames.empty())
        {
            continue;
        }
        openFold(out, documentText("Lights of %1, all left out").arg(group.name),
                 counted(static_cast<qint64>(group.frames.size()),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 file"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 files")));
        writeFileList(out, context, framesOf(group.frames));
        closeFold(out);
    }
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        if (master.inputFiles.isEmpty())
        {
            continue;
        }
        QString kind;
        switch (master.kind)
        {
        case ReportCalibrationKind::Bias:
            kind = documentText("Bias frames");
            break;
        case ReportCalibrationKind::Dark:
            kind = documentText("Dark frames");
            break;
        case ReportCalibrationKind::Flat:
            kind = documentText("Flat frames");
            break;
        }
        openFold(out, master.name.trimmed().isEmpty() ? kind : kind + dotSeparator() + master.name,
                 counted(static_cast<qint64>(master.inputFiles.size()),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 file"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 files")));
        writeFileList(out, context, master.inputFiles);
        closeFold(out);
    }
    out.close("div");
    out.close("section");
}

// ---- What the report could not state.

void writeNotStated(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    openBlock(out, "not-stated", documentText("What the report could not state"),
              counted(static_cast<qint64>(model.notStated.size()),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 thing"),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 things")),
              documentText("A value that is missing above is missing here for a reason, never printed as a zero."));
    out.open("ul", {{"class", QStringLiteral("notstated")}});
    for (const ReportNotStated& entry : model.notStated)
    {
        out.open("li");
        out.element("b", context.said(entry.what));
        out.element("p", context.said(entry.why));
        out.close("li");
    }
    out.close("ul");
    out.close("section");
}
} // namespace

// ---- What the writer calls.

QStringList provenanceAnchors(const ReportModel& model)
{
    const ProvenancePlan plan = planProvenance(model);
    QStringList anchors = {QStringLiteral("provenance"), QStringLiteral("software")};
    if (plan.acquisition)
    {
        anchors << QStringLiteral("acquisition");
    }
    if (plan.equipment)
    {
        anchors << QStringLiteral("equipment");
    }
    if (plan.inputs)
    {
        anchors << QStringLiteral("inputs");
    }
    if (plan.notStated)
    {
        anchors << QStringLiteral("not-stated");
    }
    return anchors;
}

ReportPart writeReportProvenance(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const ProvenancePlan plan = planProvenance(model);
    ReportPart part;
    part.heading = QStringLiteral("6") + dotSeparator() + documentText("Provenance");

    ReportMarkup& out = part.body;
    openPart(out, "provenance", 6, documentText("Provenance"),
             documentText("What the lights said about themselves, and what would be needed to do this run again."));
    if (plan.acquisition)
    {
        writeAcquisition(out, context);
        part.entries.push_back({QStringLiteral("acquisition"), documentText("Acquisition"), {}, false});
    }
    if (plan.equipment)
    {
        writeEquipment(out, context);
        part.entries.push_back({QStringLiteral("equipment"), documentText("Equipment and headers"), {}, false});
    }
    writeSoftware(out, context);
    part.entries.push_back({QStringLiteral("software"), documentText("Software"), {}, false});
    if (plan.inputs)
    {
        writeInputs(out, context);
        part.entries.push_back({QStringLiteral("inputs"), documentText("Input files"), {}, false});
    }
    if (plan.notStated)
    {
        writeNotStated(out, context);
        part.entries.push_back({QStringLiteral("not-stated"), documentText("Not stated"),
                                formatCount(static_cast<qint64>(model.notStated.size())), false});
    }
    out.close("section");
    return part;
}
} // namespace ImageIntegration::Reporting

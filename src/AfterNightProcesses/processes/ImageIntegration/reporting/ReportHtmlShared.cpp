// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The fourth part of the report's document: what the masters share.
///
/// A calibration master and a registration group serve several masters, so each is stated once, here, with its
/// own settings, and a master's file links to it. The part opens with which groups made each master: a table that
/// reads from left to right for a run of several masters, one line for a run of one.

#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <algorithm>
#include <array>
#include <optional>

namespace ImageIntegration::Reporting
{
namespace
{
/// Which blocks of the part a model calls for.
struct SharedPlan
{
    bool lineage = false;
    bool calibration = false;
    bool registration = false;

    [[nodiscard]] bool any() const
    {
        return lineage || calibration || registration;
    }
};

[[nodiscard]] SharedPlan planShared(const ReportModel& model)
{
    SharedPlan plan;
    plan.calibration = !model.calibrationMasters.empty();
    plan.registration = !model.registrationGroups.empty();
    // Which groups made a master is worth saying only when there is a group to name.
    plan.lineage = (plan.calibration || plan.registration || !model.lightGroups.empty()) &&
                   std::any_of(model.masters.begin(), model.masters.end(),
                               [](const ReportMaster& master) { return master.kind == ReportMasterKind::Stack; });
    return plan;
}

[[nodiscard]] QString kindName(const ReportCalibrationKind kind)
{
    switch (kind)
    {
    case ReportCalibrationKind::Bias:
        return documentText("Bias");
    case ReportCalibrationKind::Dark:
        return documentText("Dark");
    case ReportCalibrationKind::Flat:
        return documentText("Flat");
    }
    return {};
}

// ---- What made each master.

/// One calibration master a group of lights takes: where its row is, and what it is called.
struct Taken
{
    QString slug;
    QString name;
    QString note;

    friend bool operator==(const Taken&, const Taken&) = default;
};

/// One row of the map: a master and the groups behind it.
struct LineageRow
{
    const ReportMaster* master = nullptr;
    int lights = 0;
    bool preCalibrated = false;
    std::array<std::vector<Taken>, 3> calibration;
    const ReportRegistrationGroup* registration = nullptr;
};

void take(std::vector<Taken>& taken, const ReportModel& model, const QString& file, const QString& slug)
{
    if (file.trimmed().isEmpty() && slug.isEmpty())
    {
        return;
    }
    const ReportCalibrationMaster* master = calibrationOfSlug(model, slug);
    Taken entry{.slug = master != nullptr ? slug : QString(),
                .name = master != nullptr && !master->name.trimmed().isEmpty() ? master->name : file,
                .note = master != nullptr ? calibrationStateNote(master->state) : QString()};
    if (std::find(taken.begin(), taken.end(), entry) == taken.end())
    {
        taken.push_back(entry);
    }
}

[[nodiscard]] LineageRow lineageOf(const ReportModel& model, const ReportMaster& master)
{
    LineageRow row;
    row.master = &master;
    row.lights = static_cast<int>(master.frames.size());
    row.registration = registrationOfKey(model, master.registrationGroupKey);
    bool any = false;
    bool every = true;
    for (const QString& key : master.lightGroups)
    {
        const ReportLightGroup* group = lightGroupOfKey(model, key);
        if (group == nullptr)
        {
            continue;
        }
        any = true;
        every = every && group->preCalibrated;
        take(row.calibration[0], model, group->biasMaster, group->biasMasterSlug);
        take(row.calibration[1], model, group->darkMaster, group->darkMasterSlug);
        take(row.calibration[2], model, group->flatMaster, group->flatMasterSlug);
    }
    row.preCalibrated = any && every;
    return row;
}

/// Whether the run says a master's lights lack a calibration master the other lights of the run take.
[[nodiscard]] bool lacks(const ReportModel& model, const ReportMaster& master, const ReportIssueKind kind)
{
    return std::any_of(model.issues.begin(), model.issues.end(),
                       [&](const ReportIssue& issue) { return issue.kind == kind && issue.groupKey == master.key; });
}

void writeTaken(ReportMarkup& out, const ReportDocumentContext& context, const Taken& taken)
{
    const bool linked = !taken.slug.isEmpty() && context.hasAnchor(taken.slug);
    const char* element = linked ? "a" : "span";
    out.open(element, {{"href", linked ? QStringLiteral("#") + taken.slug : QString()}});
    out.text(taken.name);
    if (!taken.note.isEmpty())
    {
        out.element("small", taken.note);
    }
    out.close(element);
}

void writeCalibrationCell(ReportMarkup& out, const ReportDocumentContext& context, const LineageRow& row,
                          const std::size_t kind, const int span)
{
    static const std::array<ReportIssueKind, 3> kLacking = {
        ReportIssueKind::RunMessage, ReportIssueKind::LightsWithoutDark, ReportIssueKind::LightsWithoutFlat};
    const std::vector<Taken>& taken = row.calibration[kind];
    const QString rows = span > 1 ? QString::number(span) : QString();
    if (!taken.empty())
    {
        out.open("td", {{"class", span > 1 ? QStringLiteral("shared") : QString()}, {"rowspan", rows}});
        for (const Taken& entry : taken)
        {
            writeTaken(out, context, entry);
        }
        out.close("td");
        return;
    }
    if (kind > 0 && lacks(context.model, *row.master, kLacking[kind]))
    {
        out.open("td", {{"class", QStringLiteral("miss")}, {"rowspan", rows}});
        out.open("span");
        out.icon("warning");
        out.text(kind == 1 ? documentText("No dark") : documentText("No flat"));
        out.close("span");
        out.close("td");
        return;
    }
    out.open("td", {{"rowspan", rows}});
    out.element("span", {{"class", QStringLiteral("muted")}},
                row.preCalibrated ? documentText("pre-calibrated") : documentText("none"));
    out.close("td");
}

/// The rows of one target, or of the whole run: a cell that several rows in a row share is written once and spans
/// them.
void writeLineageRows(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<LineageRow>& rows)
{
    // For each column, how many rows the cell that starts at a row spans; zero for a row a cell above covers.
    const auto spansOf = [&rows](const auto& same) {
        std::vector<int> spans(rows.size(), 1);
        for (std::size_t row = 0; row < rows.size();)
        {
            std::size_t end = row + 1;
            while (end < rows.size() && same(rows[row], rows[end]))
            {
                spans[end] = 0;
                ++end;
            }
            spans[row] = static_cast<int>(end - row);
            row = end;
        }
        return spans;
    };
    std::array<std::vector<int>, 3> calibrationSpans;
    for (std::size_t kind = 0; kind < 3; ++kind)
    {
        calibrationSpans[kind] = spansOf([kind](const LineageRow& left, const LineageRow& right) {
            return !left.calibration[kind].empty() && left.calibration[kind] == right.calibration[kind];
        });
    }
    const std::vector<int> registrationSpans = spansOf([](const LineageRow& left, const LineageRow& right) {
        return left.registration != nullptr && left.registration == right.registration;
    });

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const LineageRow& row = rows[index];
        const ReportMaster& master = *row.master;
        out.open("tr");

        out.open("td");
        out.open("span");
        out.text(counted(row.lights, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")));
        if (row.preCalibrated)
        {
            out.element("small", documentText("marked as calibrated already"));
        }
        out.close("span");
        out.close("td");

        for (std::size_t kind = 0; kind < 3; ++kind)
        {
            if (calibrationSpans[kind][index] > 0)
            {
                writeCalibrationCell(out, context, row, kind, calibrationSpans[kind][index]);
            }
        }

        if (registrationSpans[index] > 0)
        {
            const int span = registrationSpans[index];
            out.open("td", {{"class", span > 1 ? QStringLiteral("shared") : QString()},
                            {"rowspan", span > 1 ? QString::number(span) : QString()}});
            if (row.registration != nullptr)
            {
                const bool linked = context.hasAnchor(row.registration->slug);
                const char* element = linked ? "a" : "span";
                out.open(element, {{"href", linked ? QStringLiteral("#") + row.registration->slug : QString()}});
                out.text(row.registration->name);
                if (span > 1)
                {
                    out.element("small", counted(span, QT_TRANSLATE_NOOP("ImageIntegrationReport", "shared by %1"),
                                                 QT_TRANSLATE_NOOP("ImageIntegrationReport", "shared by %1")));
                }
                out.close(element);
            }
            else
            {
                out.element("span", {{"class", QStringLiteral("muted")}}, documentText("none"));
            }
            out.close("td");
        }

        out.open("td", {{"class", QStringLiteral("out")}});
        const bool linked = context.hasAnchor(masterFileAnchor(master));
        const char* element = linked ? "a" : "span";
        out.open(element, {{"href", linked ? QStringLiteral("#") + masterFileAnchor(master) : QString()}});
        if (const QString source = context.assets.thumbnail(master.preview); !source.isEmpty())
        {
            out.leaf("img", {{"src", source}, {"alt", QStringLiteral("")}, {"loading", QStringLiteral("lazy")}});
        }
        out.open("span");
        out.text(context.labelOf(master));
        if (const ReportStateWords words = masterStateWords(master.state); words.kind != nullptr)
        {
            out.element("small", words.text.toLower());
        }
        out.close("span");
        out.close(element);
        out.close("td");
        out.close("tr");
    }
}

/// A combined colour image has no lights and no groups of its own: its row says what it is made from.
void writeCombinedRow(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    out.open("tr");
    out.open("td", {{"colspan", QStringLiteral("5")}});
    out.element("span", {{"class", QStringLiteral("muted")}},
                documentText("Combined from the red, green and blue masters of its registration group"));
    out.close("td");
    out.open("td", {{"class", QStringLiteral("out")}});
    const bool linked = context.hasAnchor(masterFileAnchor(master));
    const char* element = linked ? "a" : "span";
    out.open(element, {{"href", linked ? QStringLiteral("#") + masterFileAnchor(master) : QString()}});
    if (const QString source = context.assets.thumbnail(master.preview); !source.isEmpty())
    {
        out.leaf("img", {{"src", source}, {"alt", QStringLiteral("")}, {"loading", QStringLiteral("lazy")}});
    }
    out.element("span", context.labelOf(master));
    out.close(element);
    out.close("td");
    out.close("tr");
}

void writeLineageTable(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    openTable(out, QStringLiteral("lin"), documentText("The groups that made each master"),
              {{documentText("Lights")},
               {documentText("Bias")},
               {documentText("Dark")},
               {documentText("Flat")},
               {documentText("Registration")},
               {documentText("Master")}});
    const auto writeTarget = [&](const QString& target, const bool byTarget) {
        std::vector<LineageRow> rows;
        for (const ReportMaster& master : model.masters)
        {
            if (master.kind == ReportMasterKind::Stack && (!byTarget || master.target == target))
            {
                rows.push_back(lineageOf(model, master));
            }
        }
        writeLineageRows(out, context, rows);
        for (const ReportMaster& master : model.masters)
        {
            if (master.kind == ReportMasterKind::CombinedRgb && (!byTarget || master.target == target))
            {
                writeCombinedRow(out, context, master);
            }
        }
    };
    if (model.targets.size() > 1)
    {
        for (const ReportTarget& target : model.targets)
        {
            out.open("tr", {{"class", QStringLiteral("grouprow")}});
            out.open("th", {{"colspan", QStringLiteral("6")}, {"scope", QStringLiteral("colgroup")}});
            out.text(target.name);
            out.close("th");
            out.close("tr");
            writeTarget(target.name, true);
        }
    }
    else
    {
        writeTarget({}, false);
    }
    closeTable(out);
}

/// What made a run's one master, in one line.
void writeLineageLine(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    const LineageRow row = lineageOf(context.model, master);
    const auto arrow = [&out]() { out.element("i", QStringLiteral("→")); };
    out.open("div", {{"class", QStringLiteral("chainline")}});
    out.element("span", counted(row.lights, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")));
    arrow();
    bool calibrated = false;
    for (std::size_t kind = 0; kind < 3; ++kind)
    {
        static const std::array<ReportCalibrationKind, 3> kKinds = {
            ReportCalibrationKind::Bias, ReportCalibrationKind::Dark, ReportCalibrationKind::Flat};
        for (const Taken& taken : row.calibration[kind])
        {
            if (calibrated)
            {
                out.element("i", QStringLiteral("+"));
            }
            calibrated = true;
            Taken named = taken;
            named.name = kindName(kKinds[kind]) + QLatin1Char(' ') + taken.name;
            writeTaken(out, context, named);
        }
    }
    if (!calibrated)
    {
        out.element("span", {{"class", QStringLiteral("muted")}},
                    row.preCalibrated ? documentText("marked as calibrated already")
                                      : documentText("no calibration master"));
    }
    if (row.registration != nullptr)
    {
        arrow();
        const bool linked = context.hasAnchor(row.registration->slug);
        const char* element = linked ? "a" : "span";
        out.element(element, {{"href", linked ? QStringLiteral("#") + row.registration->slug : QString()}},
                    documentText("Registration"));
    }
    arrow();
    const bool linked = context.hasAnchor(masterFileAnchor(master));
    const char* element = linked ? "a" : "span";
    out.element(element, {{"href", linked ? QStringLiteral("#") + masterFileAnchor(master) : QString()}},
                documentText("The stack"));
    out.close("div");
}

void writeLineage(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const bool one = model.masters.size() == 1;
    openBlock(out, "lineage", one ? documentText("What made the master") : documentText("What made each master"),
              one ? QString() : documentText("the groups, stage by stage"),
              one ? QString()
                  : documentText("Read a row from left to right. A cell that spans several rows is one group those "
                                 "masters share; a name leads to the group."));
    out.open("div", {{"class", QStringLiteral("card")}});
    if (one)
    {
        writeLineageLine(out, context, model.masters.front());
    }
    else
    {
        writeLineageTable(out, context);
    }
    out.close("div");
    out.close("section");
}

// ---- The calibration masters.

/// A figure per channel in one cell: the channels set apart by a slash.
[[nodiscard]] QString perChannel(const std::vector<double>& values)
{
    QStringList parts;
    for (const double value : values)
    {
        parts << formatFigure(value);
    }
    return parts.isEmpty() ? noValueText() : parts.join(QStringLiteral(" / "));
}

void writeCalibrationRow(ReportMarkup& out, const ReportDocumentContext& context, const ReportCalibrationMaster& master)
{
    out.open("tr", {{"id", master.slug}});

    out.open("td");
    if (const QString source = context.assets.thumbnail(master.preview); !source.isEmpty())
    {
        out.leaf("img", {{"src", source}, {"alt", QStringLiteral("")}, {"loading", QStringLiteral("lazy")}});
    }
    out.close("td");

    out.open("td");
    out.element("b",
                kindName(master.kind) + (master.name.trimmed().isEmpty() ? QString() : QLatin1Char(' ') + master.name));
    if (!master.fileName.isEmpty())
    {
        out.leaf("br");
        out.element("code", {{"class", QStringLiteral("muted")}}, master.fileName);
    }
    out.close("td");

    QString frames = formatCount(master.frames);
    if (master.exposureSeconds.has_value() && *master.exposureSeconds > 0.0)
    {
        frames += QStringLiteral(" × %1 s").arg(formatFigure(*master.exposureSeconds));
    }
    out.element("td", {{"class", QStringLiteral("r num")}}, frames);

    out.element("td", {{"class", QStringLiteral("r num")}}, perChannel(master.level));
    out.open("td", {{"class", QStringLiteral("r num")}});
    out.text(perChannel(master.noise));
    QStringList how;
    if (master.measuredFraction.has_value() && *master.measuredFraction < 1.0 && !master.level.empty())
    {
        how << documentText("over the centre, %1 of the frame").arg(percentText(*master.measuredFraction));
    }
    if (!master.measuredByRun.isEmpty())
    {
        how << documentText("measured by run %1").arg(master.measuredByRun);
    }
    if (!how.isEmpty())
    {
        out.leaf("br");
        out.element("span", {{"class", QStringLiteral("muted")}}, how.join(dotSeparator()));
    }
    out.close("td");

    out.open("td");
    bool with = false;
    const auto writeWith = [&](const QString& name, const QString& slug) {
        if (name.trimmed().isEmpty())
        {
            return;
        }
        if (with)
        {
            out.text(QStringLiteral(", "));
        }
        with = true;
        writeAnchorLink(out, context, slug, name);
    };
    writeWith(master.biasMaster, master.biasMasterSlug);
    writeWith(master.darkMaster, master.darkMasterSlug);
    if (!with)
    {
        out.element("span", {{"class", QStringLiteral("muted")}},
                    master.kind == ReportCalibrationKind::Flat ? documentText("not calibrated") : noValueText());
    }
    out.close("td");

    out.open("td");
    bool used = false;
    for (const QString& key : master.usedBy)
    {
        if (const ReportMaster* light = masterOfKey(context.model, key); light != nullptr)
        {
            if (used)
            {
                out.text(QStringLiteral(", "));
            }
            used = true;
            writeMasterLink(out, context, *light, context.labelOf(*light));
        }
    }
    if (!used)
    {
        out.text(noValueText());
    }
    out.close("td");

    out.open("td");
    switch (master.state)
    {
    case ReportCalibrationState::Made:
        out.element("span", {{"class", QStringLiteral("muted")}}, documentText("Made"));
        break;
    case ReportCalibrationState::Reused:
        writePill(out, "reused", documentText("Reused"));
        break;
    case ReportCalibrationState::Supplied:
        writePill(out, "note", documentText("Supplied by you"));
        break;
    case ReportCalibrationState::Failed:
        writePill(out, "critical", documentText("Failed"));
        break;
    }
    out.close("td");
    out.close("tr");
}

void writeCalibration(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    int made = 0;
    int reused = 0;
    int supplied = 0;
    int failed = 0;
    std::array<ReportSettingsMajority, 3> majorities;
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        made += master.state == ReportCalibrationState::Made ? 1 : 0;
        reused += master.state == ReportCalibrationState::Reused ? 1 : 0;
        supplied += master.state == ReportCalibrationState::Supplied ? 1 : 0;
        failed += master.state == ReportCalibrationState::Failed ? 1 : 0;
        majorities[static_cast<std::size_t>(master.kind)].add(master.settings);
    }
    QStringList states;
    if (made > 0)
    {
        states << documentText("%1 made").arg(formatCount(made));
    }
    if (reused > 0)
    {
        states << documentText("%1 reused").arg(formatCount(reused));
    }
    if (supplied > 0)
    {
        states << documentText("%1 supplied by you").arg(formatCount(supplied));
    }
    if (failed > 0)
    {
        states << documentText("%1 failed").arg(formatCount(failed));
    }

    openBlock(out, "cal", documentText("Calibration masters"),
              counted(static_cast<qint64>(model.calibrationMasters.size()),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 master"),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 masters")) +
                  dotSeparator() + states.join(QStringLiteral(", ")),
              documentText("Each is a stack of its own, with its own settings. The level and the noise are fractions "
                           "of full scale."));
    out.open("div", {{"class", QStringLiteral("card")}});
    openTable(out, QStringLiteral("cal"), documentText("The calibration masters"),
              {{documentText("Picture")},
               {documentText("Master")},
               {documentText("Frames"), true},
               {documentText("Level"), true, documentText("The mean level, a figure per channel.")},
               {documentText("Noise"), true, documentText("The background noise, a figure per channel.")},
               {documentText("Calibrated with")},
               {documentText("Used by")},
               {documentText("State")}});
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        writeCalibrationRow(out, context, master);
    }
    closeTable(out);

    const bool anySettings =
        std::any_of(model.calibrationMasters.begin(), model.calibrationMasters.end(),
                    [](const ReportCalibrationMaster& master) { return !master.settings.empty(); });
    if (anySettings)
    {
        openFold(out, documentText("How each was stacked"),
                 counted(static_cast<qint64>(model.calibrationMasters.size()),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 master"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 masters")));
        out.open("div", {{"class", QStringLiteral("gfit")}});
        bool marks = false;
        for (const ReportCalibrationMaster& master : model.calibrationMasters)
        {
            const ReportSettingsMajority& majority = majorities[static_cast<std::size_t>(master.kind)];
            marks = marks || majority.groups() > 2;
            const QString title =
                kindName(master.kind) + (master.name.trimmed().isEmpty() ? QString() : QLatin1Char(' ') + master.name);
            for (const ReportSettingsBlock& block : master.settings)
            {
                writeSettingsBlock(out, context, block,
                                   master.settings.size() > 1 ? title + dotSeparator() + block.title : title, "h5",
                                   &majority);
            }
        }
        out.close("div");
        if (marks)
        {
            writeMarkKey(out, documentText("marks a value most masters of the same kind do not use; point at it for "
                                           "what they use."));
        }
        closeFold(out);
    }
    out.close("div");
    out.close("section");
}

// ---- The registration groups.

void writeRegistrationFacts(ReportMarkup& out, const ReportDocumentContext& context,
                            const ReportRegistrationGroup& group)
{
    std::vector<ReportFact> facts;
    facts.push_back({documentText("Lights"), formatCount(group.lights), {}});
    if (group.registered.has_value())
    {
        facts.push_back({documentText("Registered"),
                         documentText("%1 of %2").arg(formatCount(*group.registered), formatCount(group.lights)),
                         {}});
    }
    if (group.failed.has_value() && *group.failed > 0)
    {
        facts.push_back({documentText("Failed"), formatCount(*group.failed),
                         documentText("Lights the registration could not bring to the reference: no stack took "
                                      "them.")});
    }
    if (!group.reference.trimmed().isEmpty())
    {
        facts.push_back({documentText("Reference"),
                         context.said(group.reference) + dotSeparator() +
                             (group.referenceChosenAutomatically ? documentText("chosen by the application")
                                                                 : documentText("chosen by you")),
                         documentText("The light every other light of the group is aligned to.")});
    }
    if (!group.alignmentMode.trimmed().isEmpty())
    {
        facts.push_back({documentText("Aligned on"), group.alignmentMode, {}});
    }
    if (!group.transform.trimmed().isEmpty())
    {
        facts.push_back({documentText("Transform"), group.transform, {}});
    }
    if (!group.transformsUsed.empty())
    {
        QStringList used;
        for (const ReportTransformUse& use : group.transformsUsed)
        {
            used << (use.frames.has_value() ? QStringLiteral("%1 × %2").arg(use.kind, formatCount(*use.frames))
                                            : use.kind);
        }
        facts.push_back({documentText("Transforms used"), used.join(QStringLiteral(", ")), {}});
    }
    if (group.aligned.has_value())
    {
        facts.push_back({documentText("Aligned"), formatCount(*group.aligned), {}});
    }
    if (group.distortionCorrected.has_value() && *group.distortionCorrected > 0)
    {
        facts.push_back({documentText("Distortion corrected"), formatCount(*group.distortionCorrected),
                         documentText("Lights whose optical distortion was taken out before they were aligned.")});
    }
    if (group.chromaticAberrationCorrected.has_value() &&
        (*group.chromaticAberrationCorrected > 0 || group.chromaticAberrationPassedThrough.value_or(0) > 0))
    {
        facts.push_back(
            {documentText("Chromatic aberration corrected"), formatCount(*group.chromaticAberrationCorrected), {}});
        if (group.chromaticAberrationPassedThrough.has_value())
        {
            facts.push_back(
                {documentText("Passed through uncorrected"), formatCount(*group.chromaticAberrationPassedThrough), {}});
        }
    }
    if (group.overlap.has_value())
    {
        facts.push_back({documentText("Overlap retained"), percentText(*group.overlap),
                         documentText("The share of the capture's area the group's output keeps: the smallest its "
                                      "masters state.")});
    }
    if (group.medianAstrometricRmsArcsec.has_value())
    {
        facts.push_back({documentText("Astrometric residual, median"),
                         documentText("%1 arcseconds").arg(formatFigure(*group.medianAstrometricRmsArcsec)),
                         {}});
    }
    if (group.medianCatalogMatches.has_value())
    {
        facts.push_back({documentText("Catalog matches, median"), formatFigure(*group.medianCatalogMatches), {}});
    }
    writeFacts(out, facts);
}

[[nodiscard]] QString countOrNone(const std::optional<int>& value)
{
    return value.has_value() ? formatCount(*value) : QString();
}

void writeCometRun(ReportMarkup& out, const ReportDocumentContext& context, const ReportCometRun& run)
{
    writePanelTitle(out, "h5", documentText("The comet's path"), run.status);
    std::vector<ReportFact> facts;
    facts.push_back({documentText("Mode"), run.mode, {}});
    facts.push_back({documentText("Fit"), run.fitModel, {}});
    if (run.rmsPixels.has_value())
    {
        facts.push_back({documentText("Residual"), documentText("%1 px").arg(formatFigure(*run.rmsPixels)), {}});
    }
    if (run.largestResidualPixels.has_value())
    {
        facts.push_back({documentText("Largest residual"),
                         documentText("%1 px").arg(formatFigure(*run.largestResidualPixels)),
                         {}});
    }
    facts.push_back({documentText("Lights taken by the fit"), countOrNone(run.inputsAccepted), {}});
    facts.push_back({documentText("Lights refused"), countOrNone(run.inputsRejected), {}});
    facts.push_back({documentText("Lights only predicted"), countOrNone(run.inputsPredictedOnly), {}});
    facts.push_back({documentText("Positions"),
                     documentText("%1 set by you, %2 found, %3 only predicted, %4 outliers")
                         .arg(formatCount(run.pointsManual), formatCount(run.pointsAutomatic),
                              formatCount(run.pointsPredictedOnly), formatCount(run.pointsOutliers)),
                     {}});
    facts.push_back({documentText("Ready to stack"), context.said(run.readiness), {}});
    if (run.transformsSucceeded.has_value() || run.transformsFailed.has_value() || run.transformsMissing.has_value())
    {
        facts.push_back(
            {documentText("Transforms"),
             documentText("%1 made, %2 failed, %3 missing")
                 .arg(formatCount(run.transformsSucceeded.value_or(0)), formatCount(run.transformsFailed.value_or(0)),
                      formatCount(run.transformsMissing.value_or(0))),
             {}});
    }
    facts.push_back({documentText("Why not ready"), context.said(run.readinessFailure), {}});
    facts.push_back({documentText("Written on"), context.said(run.outputPlane), {}});
    facts.push_back({documentText("Warnings"), context.said(run.warnings), {}});
    writeFacts(out, facts);
}

void writeCatalogHandoff(ReportMarkup& out, const ReportCatalogHandoff& handoff)
{
    writePanelTitle(out, "h5", documentText("Catalog sources kept for colour calibration"));
    std::vector<ReportFact> facts;
    facts.push_back({documentText("Astrometric catalog"), handoff.astrometricCatalog, {}});
    facts.push_back({documentText("Photometric catalog"), handoff.photometricCatalog, {}});
    facts.push_back({documentText("Lights"), formatCount(handoff.frames), {}});
    facts.push_back({documentText("References"), formatCount(handoff.references), {}});
    facts.push_back({documentText("Sources"), formatCount(handoff.sources), {}});
    facts.push_back({documentText("Ready to use"), formatCount(handoff.satisfied), {}});
    const auto addWhenAny = [&facts](const QString& name, const int count) {
        if (count > 0)
        {
            facts.push_back({name, formatCount(count), {}});
        }
    };
    addWhenAny(documentText("Source missing"), handoff.missingSource);
    addWhenAny(documentText("Catalog chunk or cache missing"), handoff.missingChunkOrCache);
    addWhenAny(documentText("Manifest mismatch"), handoff.manifestMismatch);
    addWhenAny(documentText("Stale match"), handoff.staleMatch);
    addWhenAny(documentText("Refused for quality"), handoff.rejectedQualityFields);
    addWhenAny(documentText("Not tested"), handoff.untested);
    if (handoff.medianResidualPixels.has_value())
    {
        facts.push_back({documentText("Residual, median"),
                         documentText("%1 px").arg(formatFigure(*handoff.medianResidualPixels)),
                         {}});
    }
    if (handoff.medianSeparationArcsec.has_value())
    {
        facts.push_back({documentText("Separation, median"),
                         documentText("%1 arcseconds").arg(formatFigure(*handoff.medianSeparationArcsec)),
                         {}});
    }
    writeFacts(out, facts);
}

void writeRegistrationGroup(ReportMarkup& out, const ReportDocumentContext& context,
                            const ReportRegistrationGroup& group, const ReportSettingsMajority& majority)
{
    const ReportModel& model = context.model;
    std::vector<const ReportMaster*> served;
    for (const QString& key : group.masters)
    {
        const ReportMaster* master = masterOfKey(model, key);
        if (master != nullptr && master->kind == ReportMasterKind::Stack)
        {
            served.push_back(master);
        }
    }
    // A group that serves one master has that master's lights and no others, and the master's file already shows
    // where they sat.
    const std::optional<ReportDither> dither =
        served.size() > 1 ? ditherOf(context, served, 300) : std::optional<ReportDither>();

    out.open("div", {{"class", QStringLiteral("card regcard")}, {"id", group.slug}});
    out.open("h4", {{"class", QStringLiteral("big")}});
    out.element("span", group.name);
    out.open("small");
    out.text(counted(group.lights, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")));
    if (!served.empty())
    {
        out.text(dotSeparator() + (served.size() == 1 ? documentText("master") : documentText("masters")) +
                 QLatin1Char(' '));
        for (std::size_t index = 0; index < served.size(); ++index)
        {
            if (index > 0)
            {
                out.text(QStringLiteral(", "));
            }
            writeMasterLink(out, context, *served[index], context.labelOf(*served[index]));
        }
    }
    out.close("small");
    out.close("h4");

    out.open("div", {{"class", dither.has_value() ? QStringLiteral("reg") : QStringLiteral("reg two")}});
    out.open("div");
    writeRegistrationFacts(out, context, group);
    for (const ReportCometRun& run : group.cometRuns)
    {
        writeCometRun(out, context, run);
    }
    if (group.catalogHandoff.has_value())
    {
        writeCatalogHandoff(out, *group.catalogHandoff);
    }
    out.close("div");
    if (dither.has_value())
    {
        out.open("div");
        writePanelTitle(out, "h5", documentText("Where each light sat"),
                        documentText("every light of the group, against the reference"));
        out.chart(dither->chart);
        out.chart(dither->legend);
        out.close("div");
    }
    out.open("div");
    for (const ReportSettingsBlock& block : group.settings)
    {
        writeSettingsBlock(out, context, block, documentText("Settings"), "h5", &majority);
    }
    out.close("div");
    out.close("div");
    out.close("div");
}

void writeRegistration(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    ReportSettingsMajority majority;
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        majority.add(group.settings);
    }
    const std::size_t groups = model.registrationGroups.size();
    openBlock(out, "registration", documentText("Registration"),
              counted(static_cast<qint64>(groups), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 group"),
                      QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 groups")),
              groups > 1 ? documentText("Masters registered together are aligned to one another; each group has a "
                                        "reference of its own.")
                         : QString());
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        writeRegistrationGroup(out, context, group, majority);
    }
    if (majority.groups() > 2)
    {
        writeMarkKey(out, documentText("marks a value most registration groups do not use; point at it for what "
                                       "they use."));
    }
    out.close("section");
}
} // namespace

// ---- What the writer calls.

QStringList sharedAnchors(const ReportModel& model)
{
    const SharedPlan plan = planShared(model);
    QStringList anchors;
    if (!plan.any())
    {
        return anchors;
    }
    anchors << QStringLiteral("shared");
    if (plan.lineage)
    {
        anchors << QStringLiteral("lineage");
    }
    if (plan.calibration)
    {
        anchors << QStringLiteral("cal");
        for (const ReportCalibrationMaster& master : model.calibrationMasters)
        {
            anchors << master.slug;
        }
    }
    if (plan.registration)
    {
        anchors << QStringLiteral("registration");
        for (const ReportRegistrationGroup& group : model.registrationGroups)
        {
            anchors << group.slug;
        }
    }
    return anchors;
}

ReportPart writeReportShared(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    const SharedPlan plan = planShared(model);
    ReportPart part;
    if (!plan.any())
    {
        return part;
    }
    const bool one = model.masters.size() == 1;
    part.heading = QStringLiteral("4") + dotSeparator() +
                   (one ? documentText("Calibration and registration") : documentText("What they share"));

    ReportMarkup& out = part.body;
    openPart(out, "shared", 4,
             one ? documentText("Calibration and registration") : documentText("What the masters share"),
             one ? documentText("The groups behind the master, each with its own settings.")
                 : documentText("Calibration masters and registration groups serve several masters, so they are "
                                "stated once, here. Settings belong to these groups and to each master's own; only "
                                "a few are stated for the run."));
    if (plan.lineage)
    {
        writeLineage(out, context);
        part.entries.push_back({QStringLiteral("lineage"),
                                one ? documentText("What made the master") : documentText("What made each master"),
                                {},
                                false});
    }
    if (plan.calibration)
    {
        writeCalibration(out, context);
        part.entries.push_back({QStringLiteral("cal"), documentText("Calibration masters"),
                                formatCount(static_cast<qint64>(model.calibrationMasters.size())), false});
    }
    if (plan.registration)
    {
        writeRegistration(out, context);
        part.entries.push_back({QStringLiteral("registration"), documentText("Registration"),
                                model.registrationGroups.size() > 1
                                    ? formatCount(static_cast<qint64>(model.registrationGroups.size()))
                                    : QString(),
                                false});
    }
    out.close("section");
    return part;
}
} // namespace ImageIntegration::Reporting

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The parts of the report's document after the summary, held by reading the document back: a file per master
/// with its sections or the form of a master that was not made, what the masters share stated once, the run and
/// the provenance. Each table is held to the model's records, the mark of a differing setting to the rule that
/// places it, and a fixed list of the run's figures to their one home.

#include <gtest/gtest.h>

#include "ReportDocumentTestSupport.h"
#include "ReportModelTestSupport.h"
#include "ReportRenderedScenarios.h"

#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

using namespace ImageIntegration::Reporting;
using namespace an::tests::integration::report;

namespace
{
[[nodiscard]] QString fileAnchor(const ReportMaster& master)
{
    return QStringLiteral("file-") + master.slug;
}

[[nodiscard]] bool isThere(const ReportMaster& master)
{
    return master.state == ReportMasterState::Made || master.state == ReportMasterState::Reused;
}

[[nodiscard]] bool isUsed(const ReportFrame& frame)
{
    return frame.state == ReportFrameState::Used;
}

/// What the document calls a light's state.
[[nodiscard]] QString stateWords(const ReportFrame& frame)
{
    if (!frame.state.has_value())
    {
        return QStringLiteral("Not recorded");
    }
    switch (*frame.state)
    {
    case ReportFrameState::Used:
        return QStringLiteral("Used");
    case ReportFrameState::ExcludedByUser:
    case ReportFrameState::StackExcludedByUser:
        return QStringLiteral("Excluded by you");
    case ReportFrameState::RegistrationFailed:
        return QStringLiteral("Registration failed");
    case ReportFrameState::LeftOutByStack:
        return QStringLiteral("Left out by the stack");
    case ReportFrameState::ExcludedOnRestack:
        return QStringLiteral("Excluded on the restack");
    case ReportFrameState::NotStacked:
        return QStringLiteral("Not stacked");
    }
    return {};
}

/// The one element of an id in a document; null, with the case failed, when it is not there once.
[[nodiscard]] const DocumentElement* one(const ParsedDocument& parsed, const QString& id)
{
    const std::vector<const DocumentElement*> found = parsed.withId(id);
    if (found.size() != 1)
    {
        ADD_FAILURE() << "#" << id.toStdString() << " is written " << found.size() << " times";
        return nullptr;
    }
    return found.front();
}

/// The elements of a class inside another that are of a given name.
[[nodiscard]] std::vector<const DocumentElement*>
ofClassInside(const ParsedDocument& parsed, const DocumentElement& ancestor, const char* name, const char* wanted)
{
    std::vector<const DocumentElement*> found;
    for (const DocumentElement* element : parsed.ofClassInside(ancestor, wanted))
    {
        if (element->name == QLatin1String(name))
        {
            found.push_back(element);
        }
    }
    return found;
}

/// The rows of a table's body, each as its cells.
[[nodiscard]] std::vector<std::vector<const DocumentElement*>> bodyRows(const ParsedDocument& parsed,
                                                                        const DocumentElement& table)
{
    std::vector<std::vector<const DocumentElement*>> rows;
    const DocumentElement* body = firstInside(parsed, table, "tbody");
    if (body == nullptr)
    {
        return rows;
    }
    for (const DocumentElement* row : childrenOf(parsed, *body))
    {
        rows.push_back(childrenOf(parsed, *row));
    }
    return rows;
}

/// The headings of a table, in order.
[[nodiscard]] QStringList headings(const ParsedDocument& parsed, const DocumentElement& table)
{
    QStringList found;
    const DocumentElement* head = firstInside(parsed, table, "thead");
    if (head != nullptr)
    {
        for (const DocumentElement* heading : namedInside(parsed, *head, "th"))
        {
            found << heading->text;
        }
    }
    return found;
}

/// The lists of settings inside an element: each list's title and the names of its rows, in order.
struct SettingsList
{
    QString title;
    QStringList names;
    const DocumentElement* element = nullptr;
};

[[nodiscard]] std::vector<SettingsList> settingsInside(const ParsedDocument& parsed, const DocumentElement& ancestor)
{
    std::vector<SettingsList> lists;
    for (const DocumentElement* card : parsed.ofClassInside(ancestor, "setcard"))
    {
        SettingsList list;
        list.element = card;
        const std::vector<const DocumentElement*> titles = parsed.ofClassInside(*card, "ptitle");
        list.title = titles.empty() ? QString() : titles.front()->text;
        for (const DocumentElement* name : namedInside(parsed, *card, "dt"))
        {
            list.names << name->text;
        }
        lists.push_back(list);
    }
    return lists;
}

[[nodiscard]] QStringList rowNames(const ReportSettingsBlock& block)
{
    QStringList names;
    for (const ReportSettingsRow& row : block.rows)
    {
        names << row.name;
    }
    return names;
}

/// How many values of some groups' settings more than half of the groups do not use: the rule the document's mark
/// follows, counted here on the model alone.
[[nodiscard]] int differingValues(const std::vector<const std::vector<ReportSettingsBlock>*>& groups)
{
    QHash<QString, QHash<QString, int>> uses;
    for (const std::vector<ReportSettingsBlock>* blocks : groups)
    {
        for (const ReportSettingsBlock& block : *blocks)
        {
            for (const ReportSettingsRow& row : block.rows)
            {
                ++uses[block.title + QChar(0x001F) + row.name][row.value];
            }
        }
    }
    int differing = 0;
    const int count = static_cast<int>(groups.size());
    for (const std::vector<ReportSettingsBlock>* blocks : groups)
    {
        for (const ReportSettingsBlock& block : *blocks)
        {
            for (const ReportSettingsRow& row : block.rows)
            {
                const QHash<QString, int>& values = uses[block.title + QChar(0x001F) + row.name];
                for (auto value = values.constBegin(); value != values.constEnd(); ++value)
                {
                    if (value.value() * 2 > count && value.key() != row.value)
                    {
                        ++differing;
                    }
                }
            }
        }
    }
    return differing;
}

/// The values the document must mark in a model: over the masters' own settings, the groups of lights', the
/// calibration masters' of each kind, and the registration groups'.
[[nodiscard]] int differingValues(const ReportModel& model)
{
    std::vector<const std::vector<ReportSettingsBlock>*> stacking;
    for (const ReportMaster& master : model.masters)
    {
        if (master.kind == ReportMasterKind::Stack)
        {
            stacking.push_back(&master.settings);
        }
    }
    std::vector<const std::vector<ReportSettingsBlock>*> lights;
    for (const ReportLightGroup& group : model.lightGroups)
    {
        lights.push_back(&group.settings);
    }
    std::array<std::vector<const std::vector<ReportSettingsBlock>*>, 3> calibration;
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        calibration[static_cast<std::size_t>(master.kind)].push_back(&master.settings);
    }
    std::vector<const std::vector<ReportSettingsBlock>*> registration;
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        registration.push_back(&group.settings);
    }
    return differingValues(stacking) + differingValues(lights) + differingValues(calibration[0]) +
           differingValues(calibration[1]) + differingValues(calibration[2]) + differingValues(registration);
}

/// A scenario's model written again as a linked document, read back.
[[nodiscard]] ParsedDocument writtenAgain(const ReportModel& model, const Rendered& report,
                                          const ReportForm form = ReportForm::Linked)
{
    const ReportDocument document =
        writeReportDocument(model, {.form = form, .runDirectory = report.scenario.runDirectory});
    EXPECT_TRUE(document.faults.isEmpty()) << document.faults.join(QLatin1Char('\n')).toStdString();
    ParsedDocument parsed = parseDocument(document.html);
    EXPECT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    return parsed;
}

/// How often a figure stands in a text as a figure of its own, not as the beginning or the end of a longer one.
[[nodiscard]] qsizetype countFigure(const QString& text, const QString& figure)
{
    const QRegularExpression whole(QStringLiteral("(?<![\\d.])%1(?![\\d])").arg(QRegularExpression::escape(figure)));
    qsizetype count = 0;
    QRegularExpressionMatchIterator matches = whole.globalMatch(text);
    while (matches.hasNext())
    {
        matches.next();
        ++count;
    }
    return count;
}

/// The text of the parts after the summary.
[[nodiscard]] QString laterText(const ParsedDocument& parsed, const std::vector<const char*>& parts)
{
    QString text;
    for (const char* part : parts)
    {
        if (const DocumentElement* element = parsed.byId(part); element != nullptr)
        {
            text += element->text + QLatin1Char('\n');
        }
    }
    return text;
}
} // namespace

// ---- The parts and their blocks.

TEST_F(ReportHtmlWriterTest, TheLaterPartsAndTheirBlocksAreThereExactlyWhenTheRunCallsForThem)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;
        const ReportRun& run = model.run;
        const auto count = [&parsed](const char* anchor) { return parsed.withId(QLatin1String(anchor)).size(); };
        const auto when = [](const bool present) { return present ? 1u : 0u; };

        // The parts, in their order.
        QStringList parts;
        for (const DocumentElement* part : parsed.ofClass("part"))
        {
            parts << part->attribute("id");
            EXPECT_EQ(part->name, QStringLiteral("section"));
        }
        const bool shared = !model.calibrationMasters.empty() || !model.registrationGroups.empty();
        QStringList expected = {QStringLiteral("summary"), QStringLiteral("masters"), QStringLiteral("earlier")};
        if (shared)
        {
            expected << QStringLiteral("shared");
        }
        expected << QStringLiteral("run") << QStringLiteral("provenance");
        EXPECT_EQ(parts, expected);

        // What the masters share.
        EXPECT_EQ(count("lineage"), when(shared));
        EXPECT_EQ(count("cal"), when(!model.calibrationMasters.empty()));
        EXPECT_EQ(count("registration"), when(!model.registrationGroups.empty()));

        // The run.
        EXPECT_EQ(count("timeline"), 1u);
        EXPECT_EQ(count("decisions"), when(!model.decisions.empty()));
        EXPECT_EQ(count("once"), 1u);
        EXPECT_EQ(count("resources"),
                  when(!run.disk.empty() || run.engineMemory.has_value() || !run.cacheDiagnostics.empty()));
        EXPECT_EQ(count("diagnostics"), when(!run.counters.empty() || !run.log.trimmed().isEmpty()));

        // Provenance.
        EXPECT_EQ(count("acquisition"), when(!run.acquisition.empty()));
        EXPECT_EQ(count("equipment"),
                  when(!run.equipment.empty() || (run.headers.has_value() && !run.headers->fields.empty())));
        EXPECT_EQ(count("software"), 1u);
        EXPECT_EQ(count("inputs"), 1u);
        EXPECT_EQ(count("not-stated"), when(!model.notStated.empty()));

        // Every block is inside the part that holds it.
        const std::map<QString, QStringList> homes = {
            {QStringLiteral("shared"),
             {QStringLiteral("lineage"), QStringLiteral("cal"), QStringLiteral("registration")}},
            {QStringLiteral("run"),
             {QStringLiteral("timeline"), QStringLiteral("decisions"), QStringLiteral("once"),
              QStringLiteral("resources"), QStringLiteral("diagnostics")}},
            {QStringLiteral("provenance"),
             {QStringLiteral("acquisition"), QStringLiteral("equipment"), QStringLiteral("software"),
              QStringLiteral("inputs"), QStringLiteral("not-stated")}}};
        for (const auto& [part, blocks] : homes)
        {
            const std::vector<const DocumentElement*> parents = parsed.withId(part);
            for (const QString& block : blocks)
            {
                for (const DocumentElement* element : parsed.withId(block))
                {
                    ASSERT_EQ(parents.size(), 1u) << part.toStdString();
                    EXPECT_TRUE(parsed.isInside(*element, *parents.front())) << block.toStdString();
                    EXPECT_TRUE(element->hasClass("block")) << block.toStdString();
                }
            }
        }
    }
}

TEST_F(ReportHtmlWriterTest, APartOfOneMasterIsNamedForOne)
{
    const Rendered* oneMaster = reportOf(ReportScenarioId::OneColourMaster);
    ASSERT_NE(oneMaster, nullptr);
    const DocumentElement* masters = one(oneMaster->linkedParsed, QStringLiteral("masters"));
    ASSERT_NE(masters, nullptr);
    const DocumentElement* heading = firstInside(oneMaster->linkedParsed, *masters, "h2");
    ASSERT_NE(heading, nullptr);
    EXPECT_EQ(heading->text, QStringLiteral("The master"));
    const DocumentElement* shared = one(oneMaster->linkedParsed, QStringLiteral("shared"));
    ASSERT_NE(shared, nullptr);
    heading = firstInside(oneMaster->linkedParsed, *shared, "h2");
    ASSERT_NE(heading, nullptr);
    EXPECT_EQ(heading->text, QStringLiteral("Calibration and registration"));

    const Rendered* five = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(five, nullptr);
    masters = one(five->linkedParsed, QStringLiteral("masters"));
    ASSERT_NE(masters, nullptr);
    heading = firstInside(five->linkedParsed, *masters, "h2");
    ASSERT_NE(heading, nullptr);
    EXPECT_EQ(heading->text, QStringLiteral("Master by master"));
}

// ---- A master's file.

TEST_F(ReportHtmlWriterTest, EveryMasterHasAFileWithItsSectionsOrTheFormOfOneNotMade)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;

        EXPECT_EQ(parsed.ofClass("file").size(), model.masters.size());
        std::size_t open = 0;
        for (const ReportMaster& master : model.masters)
        {
            SCOPED_TRACE(master.name.toStdString());
            const QString anchor = fileAnchor(master);
            const DocumentElement* file = one(parsed, anchor);
            ASSERT_NE(file, nullptr);
            EXPECT_EQ(file->name, QStringLiteral("details"));
            EXPECT_TRUE(file->hasClass("file"));
            open += file->has("open") ? 1u : 0u;
            const DocumentElement* summary = firstInside(parsed, *file, "summary");
            ASSERT_NE(summary, nullptr);
            EXPECT_NE(firstInside(parsed, *summary, "h3"), nullptr);

            const auto section = [&](const char* name) {
                // A section is named by its file and its own name, two hyphens between them.
                const std::vector<const DocumentElement*> found =
                    parsed.withId(anchor + QStringLiteral("--") + QLatin1String(name));
                EXPECT_LE(found.size(), 1u);
                if (!found.empty())
                {
                    EXPECT_TRUE(parsed.isInside(*found.front(), *file));
                    EXPECT_TRUE(found.front()->hasClass("fsec"));
                }
                return found.size() == 1;
            };
            const bool picture = section("picture");
            const bool stack = section("stack");
            const bool lights = section("lights");
            const bool made = section("made");
            const std::vector<const DocumentElement*> notes = parsed.ofClassInside(*file, "alert");

            EXPECT_TRUE(made);
            if (master.kind == ReportMasterKind::CombinedRgb)
            {
                // A combined image has no lights and no stack of its own.
                EXPECT_EQ(picture, isThere(master));
                EXPECT_FALSE(stack);
                EXPECT_FALSE(lights);
                continue;
            }
            EXPECT_EQ(lights, !master.frames.empty());
            if (isThere(master))
            {
                // A master that is on disk has its four sections whatever could be measured of it.
                EXPECT_TRUE(picture);
                EXPECT_TRUE(stack);
                EXPECT_EQ(notes.size(), master.state == ReportMasterState::Reused ? 1u : 0u);
            }
            else
            {
                // One that was not made says why first, and shows no picture.
                EXPECT_FALSE(picture);
                ASSERT_EQ(notes.size(), 1u);
                EXPECT_TRUE(
                    notes.front()->hasClass(master.state == ReportMasterState::Failed ? "is-critical" : "is-skipped"));
                if (!master.failure.trimmed().isEmpty())
                {
                    EXPECT_TRUE(notes.front()->text.contains(master.failure.trimmed()))
                        << notes.front()->text.toStdString();
                }
                EXPECT_TRUE(parsed.ofClassInside(*file, "viewer").empty());
                // The phases of its finalization that ran are shown where the run timed them.
                EXPECT_EQ(stack, master.durationMs.has_value() || !master.finalization.empty());
            }
        }
        // The first file is open, so the part shows what a file holds; the others are folded.
        EXPECT_EQ(open, 1u);
        ASSERT_FALSE(model.masters.empty());
        const DocumentElement* first = one(parsed, fileAnchor(model.masters.front()));
        ASSERT_NE(first, nullptr);
        EXPECT_TRUE(first->has("open"));
    }

    // The scenarios built for a form state it by name.
    const Rendered* failed = reportOf(ReportScenarioId::FailedInStacking);
    ASSERT_NE(failed, nullptr);
    std::size_t critical = 0;
    std::size_t skipped = 0;
    for (const DocumentElement* note : failed->linkedParsed.ofClass("alert"))
    {
        critical += note->hasClass("is-critical") ? 1u : 0u;
        skipped += note->hasClass("is-skipped") ? 1u : 0u;
    }
    EXPECT_EQ(critical, 1u);
    EXPECT_EQ(skipped, 1u);

    const Rendered* incremental = reportOf(ReportScenarioId::IncrementalRun);
    ASSERT_NE(incremental, nullptr);
    std::size_t reused = 0;
    for (const DocumentElement* note : incremental->linkedParsed.ofClass("alert"))
    {
        reused += note->hasClass("is-reused") ? 1u : 0u;
    }
    EXPECT_EQ(reused, static_cast<std::size_t>(incremental->model.run.groups.reused));
    EXPECT_GT(reused, 0u);
}

TEST_F(ReportHtmlWriterTest, AReusedMasterSaysWhichRunMadeItAndLinksThatRunsReportWhereTheHistoryHasIt)
{
    const Rendered* report = reportOf(ReportScenarioId::IncrementalRun);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const auto reused = std::find_if(model.masters.begin(), model.masters.end(), [](const ReportMaster& master) {
        return master.state == ReportMasterState::Reused;
    });
    ASSERT_NE(reused, model.masters.end());
    const QString earlier = QStringLiteral("20260101_010101_001");
    const QString address = QStringLiteral("../20260101_010101_001/workflow_report.html");
    reused->madeByRun = earlier;
    const QString anchor = fileAnchor(*reused);

    // The history does not hold that run's report: the run is named, and nothing is linked.
    {
        const ParsedDocument parsed = writtenAgain(model, *report);
        const DocumentElement* file = one(parsed, anchor);
        ASSERT_NE(file, nullptr);
        const std::vector<const DocumentElement*> notes = parsed.ofClassInside(*file, "alert");
        ASSERT_EQ(notes.size(), 1u);
        EXPECT_TRUE(notes.front()->text.contains(earlier));
        EXPECT_TRUE(namedInside(parsed, *notes.front(), "a").empty());
    }

    // The history holds it: the run's name leads to that report, in the form that stays beside the history.
    model.history.push_back(ReportHistoryEntry{.runId = earlier,
                                               .archivedAt = std::nullopt,
                                               .outcome = std::nullopt,
                                               .framesUsed = std::nullopt,
                                               .frames = std::nullopt,
                                               .integrationMs = std::nullopt,
                                               .masters = std::nullopt,
                                               .backgroundNoise = std::nullopt,
                                               .report = address});
    {
        const ParsedDocument parsed = writtenAgain(model, *report);
        const DocumentElement* file = one(parsed, anchor);
        ASSERT_NE(file, nullptr);
        const std::vector<const DocumentElement*> notes = parsed.ofClassInside(*file, "alert");
        ASSERT_EQ(notes.size(), 1u);
        const std::vector<const DocumentElement*> links = namedInside(parsed, *notes.front(), "a");
        ASSERT_EQ(links.size(), 1u);
        EXPECT_EQ(links.front()->attribute("href"), address);
        EXPECT_EQ(links.front()->text, earlier);
    }
    // A document that travels alone names the run and no other file.
    {
        const ParsedDocument parsed = writtenAgain(model, *report, ReportForm::Embedded);
        for (const DocumentElement* link : parsed.named("a"))
        {
            EXPECT_TRUE(link->attribute("href").startsWith(QLatin1Char('#'))) << link->attribute("href").toStdString();
        }
        const DocumentElement* file = one(parsed, anchor);
        ASSERT_NE(file, nullptr);
        EXPECT_TRUE(file->text.contains(earlier));
    }
}

TEST_F(ReportHtmlWriterTest, AMastersPictureSwitchesBetweenTheMasterItsDetailAndItsMaps)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    const ParsedDocument& parsed = report->linkedParsed;
    QSet<QString> names;
    for (const ReportMaster& master : report->model.masters)
    {
        SCOPED_TRACE(master.name.toStdString());
        const DocumentElement* file = one(parsed, fileAnchor(master));
        ASSERT_NE(file, nullptr);
        std::size_t expected = (master.preview.isEmpty() ? 0u : 1u) + (master.detail.isEmpty() ? 0u : 1u);
        for (const ReportMap& map : master.maps)
        {
            expected += map.preview.isEmpty() ? 0u : 1u;
        }
        ASSERT_GE(expected, 2u);
        const std::vector<const DocumentElement*> viewers = parsed.ofClassInside(*file, "viewer");
        ASSERT_EQ(viewers.size(), 1u);
        const DocumentElement& viewer = *viewers.front();

        const std::vector<const DocumentElement*> views = ofClassInside(parsed, viewer, "img", "view");
        const std::vector<const DocumentElement*> picks = ofClassInside(parsed, viewer, "input", "pick");
        const std::vector<const DocumentElement*> labels = namedInside(parsed, viewer, "label");
        const std::vector<const DocumentElement*> readings = parsed.ofClassInside(viewer, "read");
        EXPECT_EQ(views.size(), expected);
        ASSERT_EQ(picks.size(), expected);
        ASSERT_EQ(labels.size(), expected);
        ASSERT_EQ(readings.size(), expected);

        // One button per view, the first one picked, each with the label that picks it and its own reading.
        std::size_t checked = 0;
        for (std::size_t index = 0; index < expected; ++index)
        {
            EXPECT_EQ(picks[index]->attribute("type"), QStringLiteral("radio"));
            EXPECT_TRUE(picks[index]->hasClass(qPrintable(QStringLiteral("pick-%1").arg(index))));
            EXPECT_EQ(labels[index]->attribute("for"), picks[index]->attribute("id"));
            EXPECT_FALSE(labels[index]->text.trimmed().isEmpty());
            EXPECT_TRUE(views[index]->hasClass(qPrintable(QStringLiteral("view-%1").arg(index))));
            EXPECT_FALSE(views[index]->attribute("alt").trimmed().isEmpty());
            EXPECT_TRUE(readings[index]->hasClass(qPrintable(QStringLiteral("read-%1").arg(index))));
            EXPECT_GT(readings[index]->text.size(), 40) << "a view says how to read it";
            checked += picks[index]->has("checked") ? 1u : 0u;
            names.insert(picks[index]->attribute("name"));
        }
        EXPECT_EQ(checked, 1u);
        EXPECT_TRUE(picks.front()->has("checked"));

        // A map is named as the run names it.
        for (const ReportMap& map : master.maps)
        {
            if (!map.preview.isEmpty())
            {
                EXPECT_TRUE(std::any_of(labels.begin(), labels.end(), [&map](const DocumentElement* label) {
                    return label->text == map.title;
                })) << map.title.toStdString();
            }
        }
    }
    // The buttons of one master's picture do not answer for another's.
    EXPECT_EQ(names.size(), static_cast<qsizetype>(report->model.masters.size()));

    // The luminance master of this run has the maps to switch to.
    const ReportMaster* luminance = masterOfFilter(report->model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    EXPECT_FALSE(luminance->maps.empty());
}

TEST_F(ReportHtmlWriterTest, AMasterStatesItsStatisticsAndItsSamplesPerChannel)
{
    const auto held = [](const Rendered& report, const ReportMaster& master) {
        SCOPED_TRACE(master.name.toStdString());
        const ParsedDocument& parsed = report.linkedParsed;
        const DocumentElement* picture = one(parsed, fileAnchor(master) + QStringLiteral("--picture"));
        ASSERT_NE(picture, nullptr);
        ASSERT_TRUE(master.statistics.has_value());
        const std::size_t channels = master.statistics->channelStats.size();

        const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *picture, "table", "stats");
        ASSERT_EQ(tables.size(), 1u);
        EXPECT_EQ(static_cast<std::size_t>(headings(parsed, *tables.front()).size()), 1u + channels);
        const std::vector<std::vector<const DocumentElement*>> rows = bodyRows(parsed, *tables.front());
        ASSERT_GE(rows.size(), 12u);
        for (const std::vector<const DocumentElement*>& row : rows)
        {
            ASSERT_EQ(row.size(), 1u + channels);
        }
        // A figure of each channel stands in the channel's column.
        const auto rowOfName = [&rows](const QString& name) {
            return std::find_if(rows.begin(), rows.end(),
                                [&name](const auto& row) { return row.front()->text == name; });
        };
        const auto noise = rowOfName(QStringLiteral("Background noise"));
        const auto median = rowOfName(QStringLiteral("Median"));
        ASSERT_NE(noise, rows.end());
        ASSERT_NE(median, rows.end());
        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            EXPECT_EQ((*noise)[channel + 1]->text,
                      formatFigure(master.statistics->channelStats[channel].backgroundNoise));
            EXPECT_EQ((*median)[channel + 1]->text, formatFigure(master.statistics->channelStats[channel].median));
        }

        // The spread of its samples: one chart, a panel per channel.
        const std::vector<const DocumentElement*> charts = parsed.ofClassInside(*picture, "chart-samples");
        ASSERT_EQ(charts.size(), 1u);
        EXPECT_EQ(parsed.ofClassInside(*charts.front(), "c-hist").size(), master.distribution.size());
        EXPECT_EQ(master.distribution.size(), channels);
    };

    const Rendered* colour = reportOf(ReportScenarioId::OneColourMaster);
    ASSERT_NE(colour, nullptr);
    ASSERT_EQ(colour->model.masters.size(), 1u);
    ASSERT_TRUE(colour->model.masters.front().statistics.has_value());
    EXPECT_EQ(colour->model.masters.front().statistics->channelStats.size(), 3u);
    held(*colour, colour->model.masters.front());

    const Rendered* mono = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(mono, nullptr);
    ASSERT_EQ(mono->model.masters.size(), 1u);
    ASSERT_TRUE(mono->model.masters.front().statistics.has_value());
    EXPECT_EQ(mono->model.masters.front().statistics->channelStats.size(), 1u);
    held(*mono, mono->model.masters.front());

    // The combined image of a run of several masters has its three channels too.
    const Rendered* five = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(five, nullptr);
    const auto combined =
        std::find_if(five->model.masters.begin(), five->model.masters.end(),
                     [](const ReportMaster& master) { return master.kind == ReportMasterKind::CombinedRgb; });
    ASSERT_NE(combined, five->model.masters.end());
    held(*five, *combined);

    // With no session nothing was measured: the file says so and leads to the reason.
    const Rendered* blind = reportOf(ReportScenarioId::FiveFiltersWithoutSession);
    ASSERT_NE(blind, nullptr);
    for (const ReportMaster& master : blind->model.masters)
    {
        EXPECT_FALSE(master.statistics.has_value());
        const DocumentElement* picture = one(blind->linkedParsed, fileAnchor(master) + QStringLiteral("--picture"));
        ASSERT_NE(picture, nullptr);
        EXPECT_TRUE(ofClassInside(blind->linkedParsed, *picture, "table", "stats").empty());
        EXPECT_TRUE(blind->linkedParsed.ofClassInside(*picture, "chart-samples").empty());
    }
}

TEST_F(ReportHtmlWriterTest, AMastersLightsAreARowEachWithTheModelsValues)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        for (const ReportMaster& master : report->model.masters)
        {
            SCOPED_TRACE(master.name.toStdString());
            const DocumentElement* file = one(parsed, fileAnchor(master));
            ASSERT_NE(file, nullptr);
            const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *file, "table", "sortable");
            if (master.frames.empty())
            {
                EXPECT_TRUE(tables.empty());
                continue;
            }
            ASSERT_EQ(tables.size(), 1u);
            const QStringList columns = headings(parsed, *tables.front());
            const std::vector<std::vector<const DocumentElement*>> rows = bodyRows(parsed, *tables.front());
            ASSERT_EQ(rows.size(), master.frames.size());

            const qsizetype fileColumn = columns.indexOf(QStringLiteral("File"));
            const qsizetype stateColumn = columns.indexOf(QStringLiteral("State"));
            ASSERT_GE(fileColumn, 0);
            ASSERT_GE(stateColumn, 0);
            EXPECT_EQ(columns.front(), QStringLiteral("#"));

            // A column is there exactly when a light of the group states its measure.
            const auto stated = [&master](const auto& value) {
                return std::any_of(master.frames.begin(), master.frames.end(), value);
            };
            const std::map<QString, bool> optional = {
                {QStringLiteral("FWHM"), stated([](const ReportFrame& frame) { return frame.fwhm.has_value(); })},
                {QStringLiteral("Stars"), stated([](const ReportFrame& frame) { return frame.stars.has_value(); })},
                {QStringLiteral("Weight"), stated([](const ReportFrame& frame) { return frame.weight.has_value(); })},
                {QStringLiteral("Share %"),
                 stated([](const ReportFrame& frame) { return frame.stackShare.has_value(); })},
                {QStringLiteral("Zero pt"),
                 stated([](const ReportFrame& frame) { return frame.zeroPoint.has_value(); })},
                {QStringLiteral("Shift x"), stated([](const ReportFrame& frame) { return frame.shiftX.has_value(); })},
                {QStringLiteral("Rejected %"),
                 stated([](const ReportFrame& frame) { return frame.rejectedFraction.has_value(); })},
                {QStringLiteral("Taken"),
                 stated([](const ReportFrame& frame) { return frame.capturedAt.has_value(); })}};
            for (const auto& [name, present] : optional)
            {
                EXPECT_EQ(columns.contains(name), present) << name.toStdString();
            }

            QHash<QString, const ReportFrame*> byName;
            for (const ReportFrame& frame : master.frames)
            {
                byName.insert(frame.fileName, &frame);
            }
            ASSERT_EQ(byName.size(), static_cast<qsizetype>(master.frames.size())) << "two lights share a name";

            const auto cellOf = [&](const std::vector<const DocumentElement*>& row, const char* name) {
                const qsizetype column = columns.indexOf(QLatin1String(name));
                return column < 0 ? nullptr : row[static_cast<std::size_t>(column)];
            };
            QSet<QString> listed;
            std::size_t leftOut = 0;
            double previous = 0.0;
            for (const std::vector<const DocumentElement*>& row : rows)
            {
                ASSERT_EQ(row.size(), static_cast<std::size_t>(columns.size()));
                const QString name = row[static_cast<std::size_t>(fileColumn)]->text;
                ASSERT_TRUE(byName.contains(name)) << name.toStdString();
                listed.insert(name);
                const ReportFrame& frame = *byName.value(name);
                // A cell is sorted by its text, or by the key it states where its text is not what it is
                // sorted by: the time a light was taken is sorted as an instant.
                if (const DocumentElement* cell = cellOf(row, "Taken"); cell != nullptr && frame.capturedAt.has_value())
                {
                    EXPECT_EQ(cell->attribute("data-v").toLongLong(), frame.capturedAt->toSecsSinceEpoch());
                }
                if (const DocumentElement* cell = cellOf(row, "Shift x"); cell != nullptr && frame.shiftX.has_value())
                {
                    const QString key = cell->has("data-v") ? cell->attribute("data-v") : cell->text;
                    bool read = false;
                    const double shift = key.toDouble(&read);
                    EXPECT_TRUE(read) << key.toStdString();
                    EXPECT_NEAR(shift, *frame.shiftX, 1e-3 * (1.0 + std::abs(*frame.shiftX)));
                }
                EXPECT_EQ(row[static_cast<std::size_t>(stateColumn)]->text, stateWords(frame));
                if (const DocumentElement* cell = cellOf(row, "FWHM"); cell != nullptr)
                {
                    EXPECT_EQ(cell->text, formatFigure(frame.fwhm));
                }
                if (const DocumentElement* cell = cellOf(row, "Weight"); cell != nullptr)
                {
                    EXPECT_EQ(cell->text, formatFigure(frame.weight));
                }
                if (const DocumentElement* cell = cellOf(row, "Share %"); cell != nullptr)
                {
                    EXPECT_EQ(cell->text,
                              frame.stackShare.has_value() ? formatFigure(100.0 * *frame.stackShare) : noValueText());
                }
                if (const DocumentElement* cell = cellOf(row, "Stars"); cell != nullptr)
                {
                    EXPECT_EQ(cell->text, frame.stars.has_value() ? formatFigure(static_cast<double>(*frame.stars))
                                                                  : noValueText());
                }
                const DocumentElement* line = parentOf(parsed, *row.front());
                ASSERT_NE(line, nullptr);
                const bool marked = line->hasClass("out");
                EXPECT_EQ(marked, frame.state.has_value() && !isUsed(frame)) << name.toStdString();
                leftOut += marked ? 1u : 0u;
                // The rows are in the order the lights were taken.
                const double place = row.front()->text.toDouble();
                EXPECT_GT(place, previous);
                previous = place;
            }
            EXPECT_EQ(listed.size(), static_cast<qsizetype>(master.frames.size()));

            // The lights left out are listed above the table, each with its state, where the master was made.
            const std::vector<const DocumentElement*> leftOutTables = ofClassInside(parsed, *file, "table", "leftout");
            if (isThere(master) && leftOut > 0)
            {
                ASSERT_EQ(leftOutTables.size(), 1u);
                EXPECT_EQ(bodyRows(parsed, *leftOutTables.front()).size(), leftOut);
            }
            else
            {
                EXPECT_TRUE(leftOutTables.empty());
            }
        }
    }

    // The exposure is a column only where the lights of a group differ in it: a group of one exposure says it in
    // the section's heading.
    const auto exposureColumn = [](const Rendered& report) {
        const ReportMaster& master = report.model.masters.front();
        const DocumentElement* file = report.linkedParsed.byId(qPrintable(fileAnchor(master)));
        if (file == nullptr)
        {
            ADD_FAILURE() << "the master has no file";
            return false;
        }
        const std::vector<const DocumentElement*> tables =
            ofClassInside(report.linkedParsed, *file, "table", "sortable");
        return tables.size() == 1 && headings(report.linkedParsed, *tables.front()).contains(QStringLiteral("Exp. s"));
    };
    const Rendered* merged = reportOf(ReportScenarioId::HdrExposureMerge);
    const Rendered* plain = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(merged, nullptr);
    ASSERT_NE(plain, nullptr);
    ASSERT_GT(merged->model.masters.front().exposure.bins.size(), 1u);
    ASSERT_EQ(plain->model.masters.front().exposure.bins.size(), 1u);
    EXPECT_TRUE(exposureColumn(*merged));
    EXPECT_FALSE(exposureColumn(*plain));

    // The run built for it has a light in every state, and the table says each.
    const Rendered* every = reportOf(ReportScenarioId::EveryLightState);
    ASSERT_NE(every, nullptr);
    ASSERT_EQ(every->model.masters.size(), 1u);
    const DocumentElement* file = one(every->linkedParsed, fileAnchor(every->model.masters.front()));
    ASSERT_NE(file, nullptr);
    for (const char* state :
         {"Excluded by you", "Registration failed", "Left out by the stack", "Excluded on the restack"})
    {
        EXPECT_TRUE(file->text.contains(QLatin1String(state))) << state;
    }
}

TEST_F(ReportHtmlWriterTest, AMastersLightsAreChartedMeasureByMeasureWithASwitchThatNeedsNoScript)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    const ParsedDocument& parsed = report->linkedParsed;
    for (const ReportMaster& master : report->model.masters)
    {
        if (master.frames.empty())
        {
            continue;
        }
        SCOPED_TRACE(master.name.toStdString());
        const DocumentElement* lights = one(parsed, fileAnchor(master) + QStringLiteral("--lights"));
        ASSERT_NE(lights, nullptr);
        const std::vector<const DocumentElement*> charts = parsed.ofClassInside(*lights, "fchart");
        ASSERT_EQ(charts.size(), 1u);
        const std::vector<const DocumentElement*> layers = parsed.ofClassInside(*charts.front(), "c-layer");
        const std::vector<const DocumentElement*> picks = ofClassInside(parsed, *charts.front(), "input", "pick");
        const std::vector<const DocumentElement*> labels = namedInside(parsed, *charts.front(), "label");
        ASSERT_GE(layers.size(), 4u);
        ASSERT_LE(layers.size(), 9u);
        ASSERT_EQ(picks.size(), layers.size());
        ASSERT_EQ(labels.size(), layers.size());
        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            EXPECT_TRUE(picks[index]->hasClass(qPrintable(QStringLiteral("pick-%1").arg(index))));
            EXPECT_EQ(labels[index]->attribute("for"), picks[index]->attribute("id"));
            EXPECT_EQ(picks[index]->has("checked"), index == 0);
        }
        EXPECT_EQ(labels.front()->text, QStringLiteral("FWHM"));
        // Where each light sat stands beside it.
        EXPECT_EQ(parsed.ofClassInside(*lights, "chart-dither").size(), 1u);
    }

    // Without a metrics database no light states a measure: the file says so and draws nothing.
    const Rendered* blind = reportOf(ReportScenarioId::FiveFiltersWithoutMetrics);
    ASSERT_NE(blind, nullptr);
    for (const ReportMaster& master : blind->model.masters)
    {
        if (master.frames.empty())
        {
            continue;
        }
        const DocumentElement* lights = one(blind->linkedParsed, fileAnchor(master) + QStringLiteral("--lights"));
        ASSERT_NE(lights, nullptr);
        EXPECT_TRUE(blind->linkedParsed.ofClassInside(*lights, "chart-dither").empty());
    }
}

TEST_F(ReportHtmlWriterTest, AStacksClustersAndTheMapsItDidNotWriteAreStatedInTheMastersFile)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const auto luminance = std::find_if(model.masters.begin(), model.masters.end(), [](const ReportMaster& master) {
        return master.filter == QStringLiteral("Luminance") && master.kind == ReportMasterKind::Stack;
    });
    ASSERT_NE(luminance, model.masters.end());
    ASSERT_TRUE(luminance->stack.has_value());
    const QString anchor = fileAnchor(*luminance);

    // As built the stack found no cluster to list, and wrote the maps its settings ask for.
    {
        const DocumentElement* file = one(report->linkedParsed, anchor);
        ASSERT_NE(file, nullptr);
        EXPECT_TRUE(ofClassInside(report->linkedParsed, *file, "table", "clusters").empty());
        EXPECT_FALSE(file->text.contains(QStringLiteral("The settings ask for")));
    }

    ReportStackClusters clusters;
    clusters.count = 25;
    clusters.pixels = 4321;
    clusters.fraction = 0.0123;
    clusters.largestPixels = 987;
    clusters.largestFraction = 0.0031;
    clusters.rejectedElements = 6543;
    clusters.rejectedElementFraction = 0.021;
    for (int index = 0; index < 25; ++index)
    {
        clusters.clusters.push_back(
            ReportRejectionCluster{.map = index % 2 == 0 ? ReportClusterMap::High : ReportClusterMap::Low,
                                   .channel = 0,
                                   .pixels = static_cast<std::uint64_t>(987 - index * 30),
                                   .minX = 10 + index,
                                   .minY = 20,
                                   .maxX = 40 + index,
                                   .maxY = 30,
                                   .centroidX = 25.5 + index,
                                   .centroidY = 25.25});
    }
    luminance->stack->clusters = clusters;
    // The settings ask for two rejection maps, and the run wrote one.
    luminance->rejectionMapsExpected = 2;
    const auto firstRejection = std::find_if(luminance->maps.begin(), luminance->maps.end(),
                                             [](const ReportMap& map) { return map.kind == ReportMapKind::Rejection; });
    ASSERT_NE(firstRejection, luminance->maps.end());
    luminance->maps.erase(firstRejection);

    const ParsedDocument parsed = writtenAgain(model, *report);
    const DocumentElement* file = one(parsed, anchor);
    ASSERT_NE(file, nullptr);
    EXPECT_TRUE(file->text.contains(formatCount(4321)));
    EXPECT_TRUE(file->text.contains(formatCount(6543)));
    const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *file, "table", "clusters");
    ASSERT_EQ(tables.size(), 1u);
    // The largest twenty are listed, in the model's order, and the fold says how many there are.
    const std::vector<std::vector<const DocumentElement*>> rows = bodyRows(parsed, *tables.front());
    ASSERT_EQ(rows.size(), 20u);
    EXPECT_EQ(rows.front()[0]->text, QStringLiteral("High"));
    EXPECT_EQ(rows.front()[2]->text, formatCount(987));
    EXPECT_EQ(rows[1][0]->text, QStringLiteral("Low"));
    EXPECT_TRUE(file->text.contains(QStringLiteral("the 20 largest of 25")));
    // Each half of the sentence takes the number of its own count.
    EXPECT_TRUE(file->text.contains(QStringLiteral("The settings ask for 2 rejection maps; 1 was written.")))
        << file->text.toStdString();
}

// ---- Settings.

TEST_F(ReportHtmlWriterTest, AMastersFileHoldsItsOwnSettingsAndNoSharedGroups)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;

        // The names only a shared group's settings state: what a master's file must not hold.
        QSet<QString> sharedOnly;
        for (const ReportRegistrationGroup& group : model.registrationGroups)
        {
            for (const ReportSettingsBlock& block : group.settings)
            {
                for (const ReportSettingsRow& row : block.rows)
                {
                    sharedOnly.insert(row.name);
                }
            }
        }
        for (const ReportMaster& master : model.masters)
        {
            for (const ReportSettingsBlock& block : master.settings)
            {
                for (const ReportSettingsRow& row : block.rows)
                {
                    sharedOnly.remove(row.name);
                }
            }
        }
        for (const ReportLightGroup& group : model.lightGroups)
        {
            for (const ReportSettingsBlock& block : group.settings)
            {
                for (const ReportSettingsRow& row : block.rows)
                {
                    sharedOnly.remove(row.name);
                }
            }
        }

        for (const ReportMaster& master : model.masters)
        {
            SCOPED_TRACE(master.name.toStdString());
            const DocumentElement* file = one(parsed, fileAnchor(master));
            ASSERT_NE(file, nullptr);
            const std::vector<SettingsList> lists = settingsInside(parsed, *file);

            // Its own blocks, each once, with the model's rows in the model's order: a setting of a mode is
            // there exactly when the model states it.
            for (const ReportSettingsBlock& block : master.settings)
            {
                const auto found = std::count_if(lists.begin(), lists.end(), [&block](const SettingsList& list) {
                    return list.title == block.title;
                });
                EXPECT_EQ(found, 1) << block.title.toStdString();
                for (const SettingsList& list : lists)
                {
                    if (list.title == block.title)
                    {
                        EXPECT_EQ(list.names, rowNames(block));
                    }
                }
            }
            // Its lights' calibration: the masters they take, as links to where each is stated, then the settings.
            std::size_t calibrationLists = 0;
            for (const SettingsList& list : lists)
            {
                if (!list.title.startsWith(QStringLiteral("Lights calibration")))
                {
                    continue;
                }
                ++calibrationLists;
                for (const DocumentElement* link : namedInside(parsed, *list.element, "a"))
                {
                    const QString target = link->attribute("href").mid(1);
                    EXPECT_TRUE(std::any_of(
                        model.calibrationMasters.begin(), model.calibrationMasters.end(),
                        [&target](const ReportCalibrationMaster& calibration) { return calibration.slug == target; }))
                        << target.toStdString();
                }
            }
            EXPECT_EQ(calibrationLists, static_cast<std::size_t>(master.lightGroups.size()));

            // And nothing a shared group states: no list of another title, no name only such a group has.
            QStringList allowed = {QStringLiteral("Registered in"), QStringLiteral("Files written")};
            for (const ReportSettingsBlock& block : master.settings)
            {
                allowed << block.title;
            }
            for (const SettingsList& list : lists)
            {
                EXPECT_TRUE(allowed.contains(list.title) || list.title.startsWith(QStringLiteral("Lights calibration")))
                    << list.title.toStdString();
                for (const QString& name : list.names)
                {
                    EXPECT_FALSE(sharedOnly.contains(name)) << name.toStdString();
                }
            }
        }

        // What is shared is stated once, in its own place.
        if (!model.calibrationMasters.empty())
        {
            const DocumentElement* calibration = one(parsed, QStringLiteral("cal"));
            ASSERT_NE(calibration, nullptr);
            std::size_t blocks = 0;
            for (const ReportCalibrationMaster& master : model.calibrationMasters)
            {
                blocks += master.settings.size();
            }
            EXPECT_EQ(settingsInside(parsed, *calibration).size(), blocks);
        }
        for (const ReportRegistrationGroup& group : model.registrationGroups)
        {
            const DocumentElement* card = one(parsed, group.slug);
            ASSERT_NE(card, nullptr);
            const std::vector<SettingsList> lists = settingsInside(parsed, *card);
            ASSERT_EQ(lists.size(), group.settings.size());
            for (std::size_t index = 0; index < lists.size(); ++index)
            {
                EXPECT_EQ(lists[index].names, rowNames(group.settings[index]));
            }
        }
        // The few settings of the run are stated once, with the run's mode before them.
        const DocumentElement* once = one(parsed, QStringLiteral("once"));
        ASSERT_NE(once, nullptr);
        const std::vector<SettingsList> runLists = settingsInside(parsed, *once);
        ASSERT_EQ(runLists.size(), model.run.settings.size() + 1);
        for (std::size_t index = 0; index < model.run.settings.size(); ++index)
        {
            EXPECT_EQ(runLists[index + 1].title, model.run.settings[index].title);
            EXPECT_EQ(runLists[index + 1].names, rowNames(model.run.settings[index]));
        }
    }

    // A setting of a mode is printed only in that mode: the drizzle of the one group that was drizzled, the comet's
    // of the run that tracked one.
    const Rendered* drizzled = reportOf(ReportScenarioId::DrizzledGroup);
    ASSERT_NE(drizzled, nullptr);
    std::size_t drizzleBlocks = 0;
    for (const ReportMaster& master : drizzled->model.masters)
    {
        drizzleBlocks += blockOf(master.settings, QStringLiteral("Drizzle")) != nullptr ? 1u : 0u;
    }
    EXPECT_EQ(drizzleBlocks, 1u);
    std::size_t drizzleLists = 0;
    for (const DocumentElement* title : drizzled->linkedParsed.ofClass("ptitle"))
    {
        drizzleLists += title->text == QStringLiteral("Drizzle") ? 1u : 0u;
    }
    EXPECT_EQ(drizzleLists, 1u);

    const Rendered* comet = reportOf(ReportScenarioId::CometRun);
    const Rendered* plain = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(comet, nullptr);
    ASSERT_NE(plain, nullptr);
    const auto names = [](const ParsedDocument& parsed) {
        QStringList found;
        for (const DocumentElement* name : parsed.named("dt"))
        {
            found << name->text;
        }
        return found;
    };
    EXPECT_TRUE(names(comet->linkedParsed).contains(QStringLiteral("Comet Observations")));
    EXPECT_FALSE(names(plain->linkedParsed).contains(QStringLiteral("Comet Observations")));
}

TEST_F(ReportHtmlWriterTest, TheMarkOfADifferingValueStandsExactlyWhereMostGroupsUseAnother)
{
    const auto marks = [](const ParsedDocument& parsed) {
        std::vector<const DocumentElement*> found;
        for (const DocumentElement* value : parsed.ofClass("diff"))
        {
            EXPECT_EQ(value->name, QStringLiteral("dd"));
            EXPECT_TRUE(value->text.startsWith(QChar(0x2260))) << value->text.toStdString();
            EXPECT_TRUE(value->attribute("data-tip").contains(QStringLiteral("Most groups: ")));
            found.push_back(value);
        }
        return found;
    };

    // Every scenario: as many marks as the rule places on its model.
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        EXPECT_EQ(marks(report->linkedParsed).size(), static_cast<std::size_t>(differingValues(report->model)));
        // The mark is written nowhere else.
        std::size_t signs = 0;
        for (const DocumentElement* sign : report->linkedParsed.ofClass("ne"))
        {
            signs += report->linkedParsed.ofClass("keyline").empty() ||
                             !parentOf(report->linkedParsed, *sign)->hasClass("keyline")
                         ? 1u
                         : 0u;
        }
        EXPECT_EQ(signs, marks(report->linkedParsed).size());
    }

    // The five filters, each master's rejection stated here: a master whose value most do not use carries the mark
    // and says what they use.
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    const auto withSigma = [&report](const QStringList& sigmas) {
        ReportModel model = report->model;
        qsizetype next = 0;
        for (ReportMaster& master : model.masters)
        {
            if (master.kind != ReportMasterKind::Stack)
            {
                continue;
            }
            EXPECT_LT(next, sigmas.size()) << "a value for each of the run's stacks";
            if (next >= sigmas.size())
            {
                break;
            }
            for (ReportSettingsBlock& block : master.settings)
            {
                for (ReportSettingsRow& row : block.rows)
                {
                    if (block.title == QStringLiteral("Stacking") && row.name == QStringLiteral("Sigma Low"))
                    {
                        row.value = sigmas.at(next);
                    }
                }
            }
            ++next;
        }
        return model;
    };
    const auto sigmaMarks = [&](const ReportModel& model) {
        const ParsedDocument parsed = writtenAgain(model, *report);
        QStringList marked;
        const DocumentElement* files = parsed.byId("masters");
        if (files == nullptr)
        {
            ADD_FAILURE() << "the document has no part of the masters' files";
            return marked;
        }
        // Inside the masters' files: a calibration master states a rejection of its own, further down.
        for (const DocumentElement* value : parsed.ofClassInside(*files, "diff"))
        {
            if (value->attribute("data-tip").startsWith(QStringLiteral("Sigma Low\n")))
            {
                marked << value->text.mid(1).trimmed() + QStringLiteral(" | ") + value->attribute("data-tip");
            }
        }
        return marked;
    };
    const ReportMaster* luminance = masterOfFilter(report->model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    ASSERT_NE(rowOf(blockOf(luminance->settings, QStringLiteral("Stacking")), QStringLiteral("Sigma Low")), nullptr);
    const QString three = QStringLiteral("3");

    // Every master the same: nothing is marked.
    QStringList marked = sigmaMarks(withSigma({three, three, three, three, three}));
    EXPECT_TRUE(marked.isEmpty()) << marked.join(QLatin1Char('\n')).toStdString();

    // One of five differs: it is marked, and its tip says what most use.
    marked = sigmaMarks(withSigma({three, three, three, three, QStringLiteral("2.5")}));
    ASSERT_EQ(marked.size(), 1);
    EXPECT_TRUE(marked.front().startsWith(QStringLiteral("2.5 | ")));
    EXPECT_TRUE(marked.front().contains(QStringLiteral("This group: 2.5")));
    EXPECT_TRUE(marked.front().contains(QStringLiteral("Most groups: 3")));

    // Two of five differ, each in its own way: three still agree, and both are marked.
    marked = sigmaMarks(withSigma({three, three, three, QStringLiteral("2.6"), QStringLiteral("2.5")}));
    EXPECT_EQ(marked.size(), 2);

    // Three of five differ: no value is used by more than half, so nothing is marked.
    marked = sigmaMarks(withSigma({three, three, QStringLiteral("2.7"), QStringLiteral("2.6"), QStringLiteral("2.5")}));
    EXPECT_TRUE(marked.isEmpty()) << marked.join(QLatin1Char('\n')).toStdString();

    // Three of five share another value: that one is what most use, and the two that kept theirs are marked.
    marked = sigmaMarks(withSigma({QStringLiteral("2.5"), QStringLiteral("2.5"), QStringLiteral("2.5"), three, three}));
    ASSERT_EQ(marked.size(), 2);
    EXPECT_TRUE(marked.front().startsWith(QStringLiteral("3 | ")));
    EXPECT_TRUE(marked.front().contains(QStringLiteral("Most groups: 2.5")));

    // Two groups that differ have no majority: a run of two masters marks nothing among their own settings.
    const Rendered* two = reportOf(ReportScenarioId::DrizzledGroup);
    ASSERT_NE(two, nullptr);
    ASSERT_EQ(two->model.masters.size(), 2u);
    EXPECT_TRUE(marks(two->linkedParsed).empty());
}

// ---- What the masters share.

TEST_F(ReportHtmlWriterTest, WhatMadeEachMasterIsATableForSeveralAndOneLineForOne)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;
        if (parsed.withId(QStringLiteral("lineage")).empty())
        {
            continue;
        }
        const DocumentElement* lineage = one(parsed, QStringLiteral("lineage"));
        ASSERT_NE(lineage, nullptr);
        const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *lineage, "table", "lin");
        const std::vector<const DocumentElement*> lines = parsed.ofClassInside(*lineage, "chainline");
        if (model.masters.size() == 1)
        {
            // One master: no map of groups, one line that leads to each group and to the master's file.
            EXPECT_TRUE(tables.empty());
            ASSERT_EQ(lines.size(), 1u);
            QStringList targets;
            for (const DocumentElement* link : namedInside(parsed, *lines.front(), "a"))
            {
                targets << link->attribute("href").mid(1);
            }
            EXPECT_TRUE(targets.contains(fileAnchor(model.masters.front())));
            for (const ReportRegistrationGroup& group : model.registrationGroups)
            {
                EXPECT_TRUE(targets.contains(group.slug));
            }
            for (const ReportCalibrationMaster& master : model.calibrationMasters)
            {
                if (!master.usedBy.isEmpty())
                {
                    EXPECT_TRUE(targets.contains(master.slug)) << master.slug.toStdString();
                }
            }
            continue;
        }
        EXPECT_TRUE(lines.empty());
        ASSERT_EQ(tables.size(), 1u);
        // A row per master, each ending in the master, under a line per target where there are several.
        std::size_t masters = 0;
        std::size_t targets = 0;
        const DocumentElement* body = firstInside(parsed, *tables.front(), "tbody");
        ASSERT_NE(body, nullptr);
        for (const DocumentElement* row : childrenOf(parsed, *body))
        {
            if (row->hasClass("grouprow"))
            {
                ++targets;
                continue;
            }
            ++masters;
            const std::vector<const DocumentElement*> cells = childrenOf(parsed, *row);
            ASSERT_FALSE(cells.empty());
            EXPECT_TRUE(cells.back()->hasClass("out"));
        }
        EXPECT_EQ(masters, model.masters.size());
        EXPECT_EQ(targets, model.targets.size() > 1 ? model.targets.size() : 0u);

        // A registration group several masters share is one cell that spans them.
        for (const ReportRegistrationGroup& group : model.registrationGroups)
        {
            std::size_t cells = 0;
            int spanned = 0;
            for (const DocumentElement* cell : namedInside(parsed, *tables.front(), "td"))
            {
                const DocumentElement* link = firstInside(parsed, *cell, "a");
                if (link != nullptr && link->attribute("href") == QLatin1Char('#') + group.slug)
                {
                    ++cells;
                    spanned += cell->has("rowspan") ? cell->attribute("rowspan").toInt() : 1;
                }
            }
            int served = 0;
            for (const ReportMaster& master : model.masters)
            {
                served += master.kind == ReportMasterKind::Stack && master.registrationGroupKey == group.key ? 1 : 0;
            }
            EXPECT_EQ(spanned, served) << group.name.toStdString();
            if (model.targets.size() <= 1)
            {
                EXPECT_EQ(cells, served > 0 ? 1u : 0u) << group.name.toStdString();
            }
        }
    }

    // The five filters: the lights that take no flat say so in the flat's column.
    const Rendered* five = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(five, nullptr);
    const DocumentElement* lineage = one(five->linkedParsed, QStringLiteral("lineage"));
    ASSERT_NE(lineage, nullptr);
    const std::vector<const DocumentElement*> missing = five->linkedParsed.ofClassInside(*lineage, "miss");
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing.front()->text, QStringLiteral("No flat"));

    // Two targets: each has its own line in the masters' part too.
    const Rendered* targets = reportOf(ReportScenarioId::TwoTargets);
    ASSERT_NE(targets, nullptr);
    EXPECT_EQ(targets->linkedParsed.ofClass("targethead").size(), targets->model.targets.size());
    EXPECT_EQ(targets->model.targets.size(), 2u);
    EXPECT_TRUE(five->linkedParsed.ofClass("targethead").empty());
}

TEST_F(ReportHtmlWriterTest, TheSharedGroupsAreARecordEachOfTheModel)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;

        // A row per calibration master, where a link to it lands.
        if (!model.calibrationMasters.empty())
        {
            const DocumentElement* block = one(parsed, QStringLiteral("cal"));
            ASSERT_NE(block, nullptr);
            const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *block, "table", "cal");
            ASSERT_EQ(tables.size(), 1u);
            const DocumentElement* body = firstInside(parsed, *tables.front(), "tbody");
            ASSERT_NE(body, nullptr);
            const std::vector<const DocumentElement*> rows = childrenOf(parsed, *body);
            ASSERT_EQ(rows.size(), model.calibrationMasters.size());
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                const ReportCalibrationMaster& master = model.calibrationMasters[index];
                EXPECT_EQ(rows[index]->attribute("id"), master.slug);
                EXPECT_TRUE(rows[index]->text.contains(master.fileName));
                if (!master.level.empty())
                {
                    EXPECT_TRUE(rows[index]->text.contains(formatFigure(master.level.front())));
                }
                switch (master.state)
                {
                case ReportCalibrationState::Reused:
                    EXPECT_TRUE(rows[index]->text.contains(QStringLiteral("Reused")));
                    break;
                case ReportCalibrationState::Supplied:
                    EXPECT_TRUE(rows[index]->text.contains(QStringLiteral("Supplied by you")));
                    break;
                default:
                    break;
                }
            }
        }

        // A card per registration group, with its dither where it serves several masters and not otherwise: a
        // group of one master has that master's lights, and the master's file already shows where they sat.
        const std::vector<const DocumentElement*> cards = parsed.ofClass("regcard");
        ASSERT_EQ(cards.size(), model.registrationGroups.size());
        for (std::size_t index = 0; index < cards.size(); ++index)
        {
            const ReportRegistrationGroup& group = model.registrationGroups[index];
            SCOPED_TRACE(group.name.toStdString());
            EXPECT_EQ(cards[index]->attribute("id"), group.slug);
            EXPECT_TRUE(cards[index]->text.contains(group.name));
            int served = 0;
            bool shifts = false;
            for (const ReportMaster& master : model.masters)
            {
                if (master.kind == ReportMasterKind::Stack && master.registrationGroupKey == group.key)
                {
                    ++served;
                    shifts =
                        shifts || std::any_of(master.frames.begin(), master.frames.end(), [](const ReportFrame& frame) {
                            return frame.shiftX.has_value() && frame.shiftY.has_value();
                        });
                }
            }
            EXPECT_EQ(parsed.ofClassInside(*cards[index], "chart-dither").size(), served > 1 && shifts ? 1u : 0u);
            if (!group.reference.isEmpty())
            {
                EXPECT_TRUE(cards[index]->text.contains(group.reference));
            }
        }
    }

    // The scenarios built for a state or a figure state it by name.
    const Rendered* supplied = reportOf(ReportScenarioId::PreCalibratedAndSuppliedDark);
    ASSERT_NE(supplied, nullptr);
    const DocumentElement* shared = one(supplied->linkedParsed, QStringLiteral("shared"));
    ASSERT_NE(shared, nullptr);
    EXPECT_TRUE(shared->text.contains(QStringLiteral("Supplied by you")));
    EXPECT_TRUE(shared->text.contains(QStringLiteral("marked as calibrated already")));

    const Rendered* corrected = reportOf(ReportScenarioId::DistortionCorrectionsAndManualPoints);
    ASSERT_NE(corrected, nullptr);
    const DocumentElement* registration = one(corrected->linkedParsed, QStringLiteral("registration"));
    ASSERT_NE(registration, nullptr);
    EXPECT_TRUE(registration->text.contains(QStringLiteral("Distortion corrected")));
    EXPECT_TRUE(registration->text.contains(QStringLiteral("Chromatic aberration corrected")));
    const Rendered* five = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(five, nullptr);
    registration = one(five->linkedParsed, QStringLiteral("registration"));
    ASSERT_NE(registration, nullptr);
    EXPECT_FALSE(registration->text.contains(QStringLiteral("Distortion corrected")));
}

TEST_F(ReportHtmlWriterTest, ACometsPathAndTheCatalogSourcesKeptAreStatedInTheirRegistrationGroup)
{
    const Rendered* report = reportOf(ReportScenarioId::CometRun);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    ASSERT_FALSE(model.registrationGroups.empty());
    ReportCometRun run;
    run.status = QStringLiteral("Completed");
    run.mode = QStringLiteral("Automatic");
    run.fitModel = QStringLiteral("Linear");
    run.rmsPixels = 0.4321;
    run.inputsAccepted = 18;
    run.inputsRejected = 2;
    run.pointsAutomatic = 18;
    run.readiness = QStringLiteral("Ready");
    // The run counted the transforms it made and the ones that failed, and states no count of the missing.
    run.transformsSucceeded = 17;
    run.transformsFailed = 1;
    model.registrationGroups.front().cometRuns.push_back(run);
    ReportCatalogHandoff handoff;
    handoff.frames = 20;
    handoff.sources = 4321;
    handoff.satisfied = 19;
    handoff.staleMatch = 1;
    handoff.astrometricCatalog = QStringLiteral("gaia_dr3_g14");
    handoff.medianSeparationArcsec = 0.2468;
    model.registrationGroups.front().catalogHandoff = handoff;

    const ParsedDocument parsed = writtenAgain(model, *report);
    const DocumentElement* card = one(parsed, model.registrationGroups.front().slug);
    ASSERT_NE(card, nullptr);
    for (const QString& stated :
         {QStringLiteral("The comet's path"), QStringLiteral("Linear"), formatFigure(0.4321),
          QStringLiteral("gaia_dr3_g14"), formatCount(4321), formatFigure(0.2468), QStringLiteral("Stale match")})
    {
        EXPECT_TRUE(card->text.contains(stated)) << stated.toStdString();
    }
    // A count of nothing is not listed.
    EXPECT_FALSE(card->text.contains(QStringLiteral("Manifest mismatch")));
    // A count the comet run does not state is not a count of nought.
    EXPECT_TRUE(card->text.contains(QStringLiteral("17 made, 1 failed"))) << card->text.toStdString();
    EXPECT_FALSE(card->text.contains(QStringLiteral("missing"))) << card->text.toStdString();

    // The run as built states neither.
    const DocumentElement* asBuilt = one(report->linkedParsed, report->model.registrationGroups.front().slug);
    ASSERT_NE(asBuilt, nullptr);
    EXPECT_EQ(asBuilt->text.contains(QStringLiteral("The comet's path")),
              !report->model.registrationGroups.front().cometRuns.empty());
}

// ---- The run.

TEST_F(ReportHtmlWriterTest, TheRunIsItsStagesItsDecisionsAndWhatItWrote)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;
        const ReportRun& run = model.run;

        // The timeline, and its values as a table: the stages, with the application's own work before and after.
        const DocumentElement* timeline = one(parsed, QStringLiteral("timeline"));
        ASSERT_NE(timeline, nullptr);
        EXPECT_EQ(parsed.ofClassInside(*timeline, "chart-timeline").size(), 1u);
        const std::vector<const DocumentElement*> stageTables = ofClassInside(parsed, *timeline, "table", "stages");
        ASSERT_EQ(stageTables.size(), 1u);
        const std::vector<std::vector<const DocumentElement*>> stages = bodyRows(parsed, *stageTables.front());
        const std::size_t before = run.preparation.has_value() ? 2u : 0u;
        const std::size_t after = run.executor.has_value() ? 3u : 0u;
        ASSERT_EQ(stages.size(), before + model.stages.size() + after);
        for (std::size_t index = 0; index < model.stages.size(); ++index)
        {
            const std::vector<const DocumentElement*>& row = stages[before + index];
            const ReportStage& stage = model.stages[index];
            EXPECT_EQ(row[0]->text, stage.name);
            EXPECT_EQ(row[3]->text, stage.durationMs.has_value() ? formatDurationMs(*stage.durationMs) : noValueText());
        }
        if (stopped(model))
        {
            // A run that stopped says in which stage, and leads to what it left unmade.
            const auto at = std::find_if(model.stages.begin(), model.stages.end(), [](const ReportStage& stage) {
                return stage.state == ReportStageState::Failed || stage.state == ReportStageState::Cancelled ||
                       stage.state == ReportStageState::Partial;
            });
            if (at != model.stages.end())
            {
                EXPECT_TRUE(timeline->text.contains(QStringLiteral("The run stopped in %1.").arg(at->name)));
            }
        }

        // What was left to the application.
        if (!model.decisions.empty())
        {
            const DocumentElement* decisions = one(parsed, QStringLiteral("decisions"));
            ASSERT_NE(decisions, nullptr);
            const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *decisions, "table", "decisions");
            ASSERT_EQ(tables.size(), 1u);
            const std::vector<std::vector<const DocumentElement*>> rows = bodyRows(parsed, *tables.front());
            ASSERT_EQ(rows.size(), model.decisions.size());
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                EXPECT_EQ(rows[index][0]->text, model.decisions[index].area);
                EXPECT_EQ(rows[index][3]->text, model.decisions[index].resolved.trimmed().isEmpty()
                                                    ? noValueText()
                                                    : model.decisions[index].resolved);
            }
        }

        // What it wrote.
        if (!run.disk.empty())
        {
            const DocumentElement* resources = one(parsed, QStringLiteral("resources"));
            ASSERT_NE(resources, nullptr);
            const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *resources, "table", "disk");
            ASSERT_EQ(tables.size(), 1u);
            const std::vector<std::vector<const DocumentElement*>> rows = bodyRows(parsed, *tables.front());
            ASSERT_EQ(rows.size(), run.disk.size());
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                EXPECT_EQ(rows[index][0]->text, run.disk[index].kind);
                EXPECT_EQ(rows[index][2]->text, formatByteSize(run.disk[index].bytes));
            }
        }
    }
}

TEST_F(ReportHtmlWriterTest, TheCountersAndTheEnginesMemoryAppearOnlyWhenTheRunCollectedThem)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    // As built the run collected neither.
    ASSERT_TRUE(report->model.run.counters.empty());
    ASSERT_FALSE(report->model.run.engineMemory.has_value());
    EXPECT_TRUE(
        ofClassInside(report->linkedParsed, *one(report->linkedParsed, QStringLiteral("run")), "table", "counters")
            .empty());
    EXPECT_FALSE(report->linkedParsed.bodyText().contains(QStringLiteral("Execution counters")));
    EXPECT_FALSE(report->linkedParsed.bodyText().contains(QStringLiteral("Engine memory at its peak")));

    ReportModel model = report->model;
    model.run.counters = {
        ReportCounter{.key = QStringLiteral("imagesRead"), .name = QStringLiteral("Images read"), .value = 1234},
        ReportCounter{.key = QStringLiteral("imagesWritten"), .name = QStringLiteral("Images written"), .value = 56}};
    model.run.engineMemory = ReportEngineMemory{.peakBytes = 3221225472ull,
                                                .peakBytesBeforeRun = 1048576ull,
                                                .liveBytesAtEnd = 2097152ull,
                                                .allocations = 98765ull,
                                                .bytesAllocated = 10737418240ull,
                                                .deniedAllocations = 0ull};
    model.run.cacheDiagnostics = {
        ReportCounter{.key = QStringLiteral("cacheHits"), .name = QStringLiteral("Cache hits"), .value = 42}};
    const ParsedDocument parsed = writtenAgain(model, *report);

    const DocumentElement* diagnostics = one(parsed, QStringLiteral("diagnostics"));
    ASSERT_NE(diagnostics, nullptr);
    const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *diagnostics, "table", "counters");
    ASSERT_EQ(tables.size(), 1u);
    const std::vector<std::vector<const DocumentElement*>> rows = bodyRows(parsed, *tables.front());
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0][0]->text, QStringLiteral("Images read"));
    EXPECT_EQ(rows[0][1]->text, formatCount(1234));

    const DocumentElement* resources = one(parsed, QStringLiteral("resources"));
    ASSERT_NE(resources, nullptr);
    EXPECT_TRUE(resources->text.contains(QStringLiteral("Engine memory at its peak")));
    EXPECT_TRUE(resources->text.contains(formatByteSize(3221225472ll)));
    EXPECT_TRUE(resources->text.contains(QStringLiteral("Cache hits")));
}

TEST_F(ReportHtmlWriterTest, TheLogIsPrintedAsTheRunWroteItWithItsStagesMarked)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const QString hostile = QStringLiteral("[ERROR] <script>alert(1)</script> & ]]> </pre> ") + QChar(0x0001);
    model.run.log = QStringLiteral("[HEADER] \n") + QString(3, QChar(0x2550)) +
                    QStringLiteral(" STAGE: Bias Stacking (1 group) ") + QString(3, QChar(0x2550)) +
                    QStringLiteral("\n[DETAIL] reading a|b|c\n[WARNING] a flat is missing\n") + hostile +
                    QStringLiteral("\n") + QString(3, QChar(0x2550)) +
                    QStringLiteral(" STAGE: Lights Stacking (5 groups) ") + QString(3, QChar(0x2550)) +
                    QStringLiteral("\n[SUCCESS] done");

    const ReportDocument document =
        writeReportDocument(model, {.form = ReportForm::Linked, .runDirectory = report->scenario.runDirectory});
    EXPECT_TRUE(document.faults.isEmpty());
    const ParsedDocument parsed = parseDocument(document.html);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    EXPECT_EQ(parsed.named("script").size(), 1u);

    const std::vector<const DocumentElement*> logs = parsed.ofClass("log");
    ASSERT_EQ(logs.size(), 1u);
    EXPECT_EQ(logs.front()->name, QStringLiteral("pre"));
    // Every line, as it was written, the forbidden character replaced.
    QString read = model.run.log;
    read.replace(QChar(0x0001), QChar(0xFFFD));
    EXPECT_EQ(logs.front()->text.trimmed(), read.trimmed());
    // A stage's heading is marked, and so are a warning and an error.
    EXPECT_EQ(namedInside(parsed, *logs.front(), "b").size(), 2u);
    EXPECT_EQ(parsed.ofClassInside(*logs.front(), "log-warning").size(), 1u);
    EXPECT_EQ(parsed.ofClassInside(*logs.front(), "log-error").size(), 1u);
    // The log is folded away.
    const DocumentElement* fold = parentOf(parsed, *logs.front());
    ASSERT_NE(fold, nullptr);
    EXPECT_EQ(fold->name, QStringLiteral("details"));
    EXPECT_FALSE(fold->has("open"));

    // A run with no log has no fold for one.
    model.run.log.clear();
    EXPECT_TRUE(writtenAgain(model, *report).ofClass("log").empty());
}

// ---- Provenance.

TEST_F(ReportHtmlWriterTest, ProvenanceStatesTheAcquisitionTheSoftwareAndEveryInputFile)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;
        const ReportRun& run = model.run;

        if (!run.acquisition.empty())
        {
            const DocumentElement* acquisition = one(parsed, QStringLiteral("acquisition"));
            ASSERT_NE(acquisition, nullptr);
            const std::vector<const DocumentElement*> tables =
                ofClassInside(parsed, *acquisition, "table", "acquisition");
            ASSERT_EQ(tables.size(), 1u);
            std::size_t rows = 0;
            std::size_t targets = 0;
            const DocumentElement* body = firstInside(parsed, *tables.front(), "tbody");
            ASSERT_NE(body, nullptr);
            for (const DocumentElement* row : childrenOf(parsed, *body))
            {
                (row->hasClass("grouprow") ? targets : rows) += 1;
            }
            EXPECT_EQ(rows, run.acquisition.size());
            EXPECT_EQ(targets, model.targets.size() > 1 ? model.targets.size() : 0u);
            for (const ReportAcquisitionRow& row : run.acquisition)
            {
                for (const ReportExposureBin& bin : row.exposure.bins)
                {
                    EXPECT_TRUE(acquisition->text.contains(
                        QStringLiteral("%1 × %2 s").arg(formatCount(bin.frames), formatFigure(bin.seconds))));
                }
            }
        }
        if (!run.equipment.empty())
        {
            const DocumentElement* equipment = one(parsed, QStringLiteral("equipment"));
            ASSERT_NE(equipment, nullptr);
            const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *equipment, "table", "equipment");
            ASSERT_EQ(tables.size(), 1u);
            EXPECT_EQ(bodyRows(parsed, *tables.front()).size(), run.equipment.size());
            for (const ReportEquipmentRow& row : run.equipment)
            {
                if (!row.camera.isEmpty())
                {
                    EXPECT_TRUE(equipment->text.contains(row.camera));
                }
            }
        }

        const DocumentElement* software = one(parsed, QStringLiteral("software"));
        ASSERT_NE(software, nullptr);
        for (const QString& stated :
             {run.fingerprint, run.settingsFingerprint, run.inputsFingerprint, run.executionId, run.platform})
        {
            if (!stated.trimmed().isEmpty())
            {
                EXPECT_TRUE(software->text.contains(stated.trimmed())) << stated.toStdString();
            }
        }
        // The run in one line.
        const std::vector<const DocumentElement*> lines = parsed.ofClassInside(*software, "shareline");
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_TRUE(lines.front()->text.contains(formatCount(run.frames)));
        if (!run.applicationVersion.isEmpty())
        {
            EXPECT_TRUE(lines.front()->text.contains(run.applicationVersion));
        }

        // Every input file, by group: the lights of each master, the lights of a group left out whole, and the
        // frames of each calibration master.
        const DocumentElement* inputs = one(parsed, QStringLiteral("inputs"));
        ASSERT_NE(inputs, nullptr);
        std::size_t files = 0;
        std::size_t groups = 0;
        for (const ReportMaster& master : model.masters)
        {
            files += master.frames.size();
            groups += master.frames.empty() ? 0u : 1u;
        }
        for (const ReportGroupNotStacked& group : model.groupsNotStacked)
        {
            files += group.frames.size();
            groups += group.frames.empty() ? 0u : 1u;
        }
        for (const ReportCalibrationMaster& master : model.calibrationMasters)
        {
            files += static_cast<std::size_t>(master.inputFiles.size());
            groups += master.inputFiles.isEmpty() ? 0u : 1u;
        }
        EXPECT_EQ(namedInside(parsed, *inputs, "li").size(), files);
        const std::vector<const DocumentElement*> folds = namedInside(parsed, *inputs, "details");
        EXPECT_EQ(folds.size(), groups);
        for (const DocumentElement* fold : folds)
        {
            EXPECT_FALSE(fold->has("open")) << "the lists of input files are folded";
        }

        // The embedded form lists the same files by name alone.
        const DocumentElement* embedded = one(report->embeddedParsed, QStringLiteral("inputs"));
        ASSERT_NE(embedded, nullptr);
        EXPECT_EQ(namedInside(report->embeddedParsed, *embedded, "li").size(), files);
        EXPECT_TRUE(report->embeddedParsed.ofClassInside(*embedded, "folder").empty());
    }
}

// A group's sensor temperature is one figure or a range. A group that states one bound and not the other prints
// the bound it states, whichever it is, and the bound it does not state is never read; with both it prints the
// range, or one figure where they are the same; with neither, no figure.
TEST_F(ReportHtmlWriterTest, AGroupsSensorTemperatureIsPrintedFromWhicheverBoundsItStates)
{
    const Rendered* report = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    ASSERT_FALSE(model.run.acquisition.empty());
    ReportAcquisitionRow& row = model.run.acquisition.front();

    // The cell under "Sensor" in the first row of the table, in the model written again.
    const auto sensor = [&model, report]() {
        const ParsedDocument parsed = writtenAgain(model, *report);
        const DocumentElement* acquisition = one(parsed, QStringLiteral("acquisition"));
        if (acquisition == nullptr)
        {
            return QString();
        }
        const std::vector<const DocumentElement*> tables = ofClassInside(parsed, *acquisition, "table", "acquisition");
        if (tables.size() != 1u)
        {
            ADD_FAILURE() << "the acquisition is written as " << tables.size() << " tables";
            return QString();
        }
        const qsizetype column = headings(parsed, *tables.front()).indexOf(QStringLiteral("Sensor"));
        const std::vector<std::vector<const DocumentElement*>> rows = bodyRows(parsed, *tables.front());
        if (column < 0 || rows.empty() || rows.front().size() <= static_cast<std::size_t>(column))
        {
            ADD_FAILURE() << "the table has no row with a cell under Sensor";
            return QString();
        }
        return rows.front()[static_cast<std::size_t>(column)]->text;
    };
    const QString unit = QStringLiteral(" °C");

    // The lowest alone, then the highest alone.
    row.lowestTemperature = -10.5;
    row.highestTemperature.reset();
    EXPECT_EQ(sensor(), formatFigure(-10.5) + unit);
    row.lowestTemperature.reset();
    row.highestTemperature = -9.5;
    EXPECT_EQ(sensor(), formatFigure(-9.5) + unit);

    // Both: the range, and one figure where the two are the same.
    row.lowestTemperature = -10.5;
    EXPECT_EQ(sensor(), QStringLiteral("%1 to %2").arg(formatFigure(-10.5), formatFigure(-9.5)) + unit);
    row.highestTemperature = -10.5;
    EXPECT_EQ(sensor(), formatFigure(-10.5) + unit);

    // Neither: the report prints no figure, never a zero.
    row.lowestTemperature.reset();
    row.highestTemperature.reset();
    EXPECT_EQ(sensor(), noValueText());
}

TEST_F(ReportHtmlWriterTest, WhatTheReportCouldNotStateIsListedEntryByEntry)
{
    std::size_t entries = 0;
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;
        const std::vector<const DocumentElement*> lists = parsed.ofClass("notstated");
        if (model.notStated.empty())
        {
            EXPECT_TRUE(lists.empty());
            EXPECT_TRUE(parsed.withId(QStringLiteral("not-stated")).empty());
            continue;
        }
        ASSERT_EQ(lists.size(), 1u);
        const std::vector<const DocumentElement*> items = childrenOf(parsed, *lists.front());
        ASSERT_EQ(items.size(), model.notStated.size());
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const DocumentElement* what = firstInside(parsed, *items[index], "b");
            const DocumentElement* why = firstInside(parsed, *items[index], "p");
            ASSERT_NE(what, nullptr);
            ASSERT_NE(why, nullptr);
            if (!model.notStated[index].what.contains(QLatin1Char('|')))
            {
                EXPECT_EQ(what->text, model.notStated[index].what);
            }
            if (!model.notStated[index].why.contains(QLatin1Char('|')))
            {
                EXPECT_EQ(why->text, model.notStated[index].why);
            }
        }
        entries += items.size();
    }
    EXPECT_GT(entries, 0u);

    // The run reported with no session is the one with the most to say.
    const Rendered* blind = reportOf(ReportScenarioId::FiveFiltersWithoutSession);
    ASSERT_NE(blind, nullptr);
    EXPECT_GE(blind->model.notStated.size(), blind->model.masters.size());
    const Rendered* five = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(five, nullptr);
    EXPECT_TRUE(five->model.notStated.empty());
}

// ---- One home for each figure.

TEST_F(ReportHtmlWriterTest, NoFigureOfTheRunIsPrintedTwiceOutsideItsHome)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;
        const ReportRun& run = model.run;
        const QString masters = laterText(parsed, {"masters"});
        const QString after = laterText(parsed, {"shared", "run", "provenance"});

        // The lights the masters combine, of those taken: the header's, broken down in the summary. Below the
        // masters' files it is said once more, in the line a person copies.
        if (run.framesUsed.has_value())
        {
            const QString used =
                QStringLiteral("%1 of %2 lights").arg(formatCount(*run.framesUsed), formatCount(run.frames));
            EXPECT_LE(countFigure(after, used), 1) << used.toStdString();
            // A master's file states that master's lights: the run's only where the run has one master.
            int owners = 0;
            for (const ReportMaster& master : model.masters)
            {
                int combined = 0;
                bool stated = false;
                for (const ReportFrame& frame : master.frames)
                {
                    stated = stated || frame.state.has_value();
                    combined += isUsed(frame) ? 1 : 0;
                }
                owners += stated && isThere(master) && combined == *run.framesUsed &&
                                  static_cast<int>(master.frames.size()) == run.frames
                              ? 1
                              : 0;
            }
            EXPECT_LE(countFigure(masters, used), owners) << used.toStdString();
        }

        // The integration time: the header's. Below the masters' files, once more in that line.
        if (run.integratedMs.has_value() && *run.integratedMs > 0)
        {
            const QString integration = formatDurationMs(*run.integratedMs);
            EXPECT_LE(countFigure(after, integration), 1) << integration.toStdString();
        }

        // Each master's noise: the summary's, and once more in the master's file, where its statistics state it
        // channel by channel.
        QHash<QString, int> stated;
        for (const ReportMaster& master : model.masters)
        {
            if (!master.statistics.has_value())
            {
                continue;
            }
            for (const ReportChannelStatistics& channel : master.statistics->channelStats)
            {
                ++stated[formatFigure(channel.backgroundNoise)];
            }
        }
        for (const ReportMaster& master : model.masters)
        {
            if (!master.snapshot.backgroundNoise.has_value())
            {
                continue;
            }
            const QString noise = formatFigure(*master.snapshot.backgroundNoise);
            if (noise.size() < 5)
            {
                // A figure of a digit or two is in every table for another reason.
                continue;
            }
            EXPECT_LE(countFigure(masters, noise), stated.value(noise)) << noise.toStdString();
            EXPECT_EQ(countFigure(after, noise), 0) << noise.toStdString();
        }
    }
}

// One figure has one name. The summary and the comparison with earlier runs say "Effective frames", so a master's
// stack does too: two names would read as two figures.
TEST_F(ReportHtmlWriterTest, TheEffectiveFramesHaveOneNameWhereverTheyArePrinted)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        for (const ParsedDocument* parsed : {&report->linkedParsed, &report->embeddedParsed})
        {
            for (const DocumentTextRun& run : parsed->runs)
            {
                EXPECT_FALSE(run.text.contains(QStringLiteral("effective light"), Qt::CaseInsensitive))
                    << run.text.toStdString();
            }
        }
    }

    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    const auto luminance =
        std::find_if(report->model.masters.begin(), report->model.masters.end(), [](const ReportMaster& master) {
            return master.filter == QStringLiteral("Luminance") && master.kind == ReportMasterKind::Stack;
        });
    ASSERT_NE(luminance, report->model.masters.end());
    ASSERT_TRUE(luminance->stack.has_value());
    const DocumentElement* stack = one(report->linkedParsed, fileAnchor(*luminance) + QStringLiteral("--stack"));
    ASSERT_NE(stack, nullptr);
    EXPECT_TRUE(stack->text.contains(QStringLiteral("Effective frames"))) << stack->text.left(400).toStdString();
}

// A light's turn is a column of the table of lights only where it says something: a master with a light turned by
// more than a degree. The fraction of a degree every light is turned by is no column.
TEST_F(ReportHtmlWriterTest, ATurnedLightGivesTheTableOfLightsATurnColumn)
{
    const Rendered* report = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    ASSERT_FALSE(model.masters.empty());
    ReportMaster& master = model.masters.front();
    ASSERT_GE(master.frames.size(), 2u);
    const QString anchor = fileAnchor(master) + QStringLiteral("--lights");

    const auto headings = [&anchor](const ParsedDocument& parsed) {
        QStringList found;
        const DocumentElement* lights = one(parsed, anchor);
        if (lights != nullptr)
        {
            for (const DocumentElement* heading : namedInside(parsed, *lights, "th"))
            {
                found.append(heading->text.trimmed());
            }
        }
        return found;
    };

    // No light is turned by a degree: no column.
    for (ReportFrame& frame : master.frames)
    {
        frame.rotationDegrees = 0.02;
    }
    EXPECT_FALSE(headings(writtenAgain(model, *report)).contains(QStringLiteral("Turn")));

    // One light was taken after a meridian flip.
    master.frames[1].rotationDegrees = 180.0;
    const ParsedDocument turned = writtenAgain(model, *report);
    EXPECT_TRUE(headings(turned).contains(QStringLiteral("Turn")));
    const DocumentElement* lights = one(turned, anchor);
    ASSERT_NE(lights, nullptr);
    EXPECT_TRUE(lights->text.contains(QStringLiteral("180")));
}

// A master whose stack recorded no effective frames, no measured gain and no coverage prints none of them: no
// line of its stack, no bar, and a dash in the table that compares the masters - never a zero.
TEST_F(ReportHtmlWriterTest, AStackThatRecordedNoEffectiveFramesGainOrCoveragePrintsNoneOfThem)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const auto luminance = std::find_if(model.masters.begin(), model.masters.end(), [](const ReportMaster& master) {
        return master.filter == QStringLiteral("Luminance") && master.kind == ReportMasterKind::Stack;
    });
    ASSERT_NE(luminance, model.masters.end());
    ASSERT_TRUE(luminance->stack.has_value());
    const QString anchor = fileAnchor(*luminance) + QStringLiteral("--stack");

    // As built, the stack states all three.
    {
        const DocumentElement* stack = one(report->linkedParsed, anchor);
        ASSERT_NE(stack, nullptr);
        EXPECT_TRUE(stack->text.contains(QStringLiteral("Effective frames")));
        EXPECT_TRUE(stack->text.contains(QStringLiteral("SNR gain")));
        EXPECT_TRUE(stack->text.contains(QStringLiteral("lights per pixel")));
    }

    luminance->stack->effectiveFrames.reset();
    luminance->stack->measuredSnrGain.reset();
    luminance->stack->efficiency.reset();
    luminance->stack->coverage.reset();
    const ParsedDocument parsed = writtenAgain(model, *report);

    const DocumentElement* stack = one(parsed, anchor);
    ASSERT_NE(stack, nullptr);
    EXPECT_FALSE(stack->text.contains(QStringLiteral("Effective frames"))) << stack->text.left(300).toStdString();
    EXPECT_FALSE(stack->text.contains(QStringLiteral("SNR gain")));
    EXPECT_FALSE(stack->text.contains(QStringLiteral("lights per pixel")));
    // What the stack did record is still there.
    EXPECT_TRUE(stack->text.contains(QStringLiteral("Rejected")));

    // The table that compares the masters has a dash in both of the master's cells.
    const DocumentElement* made = parsed.byId("made");
    ASSERT_NE(made, nullptr);
    const std::vector<const DocumentElement*> tables = parsed.ofClassInside(*made, "compare");
    ASSERT_EQ(tables.size(), 1u);
    bool found = false;
    for (const std::vector<const DocumentElement*>& cells : bodyRows(parsed, *tables.front()))
    {
        if (cells.size() == 6u && cells[0]->text.contains(luminance->filter))
        {
            found = true;
            EXPECT_EQ(cells[3]->text, noValueText());
            EXPECT_EQ(cells[4]->text, noValueText());
        }
    }
    EXPECT_TRUE(found);
}

// The table of calibration masters has a column for what each is checked by: a bias's and a dark's hot pixels, a
// flat's corners against its centre, and a dash for a master that states neither.
TEST_F(ReportHtmlWriterTest, TheCalibrationMastersStateWhatEachIsCheckedBy)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const auto ofKind = [&model](const ReportCalibrationKind kind) {
        return std::find_if(model.calibrationMasters.begin(), model.calibrationMasters.end(),
                            [kind](const ReportCalibrationMaster& master) { return master.kind == kind; });
    };
    const auto dark = ofKind(ReportCalibrationKind::Dark);
    const auto flat = ofKind(ReportCalibrationKind::Flat);
    const auto bias = ofKind(ReportCalibrationKind::Bias);
    ASSERT_NE(dark, model.calibrationMasters.end());
    ASSERT_NE(flat, model.calibrationMasters.end());
    ASSERT_NE(bias, model.calibrationMasters.end());
    dark->hotPixelFraction = {0.0005};
    flat->cornerLevel = {0.72};
    bias->hotPixelFraction.clear();

    const ParsedDocument parsed = writtenAgain(model, *report);
    const DocumentElement* block = one(parsed, QStringLiteral("cal"));
    ASSERT_NE(block, nullptr);
    QStringList headings;
    for (const DocumentElement* heading : namedInside(parsed, *block, "th"))
    {
        headings.append(heading->text.trimmed());
    }
    EXPECT_TRUE(headings.contains(QStringLiteral("Check"))) << headings.join(QLatin1Char('|')).toStdString();

    const DocumentElement* darkRow = one(parsed, dark->slug);
    ASSERT_NE(darkRow, nullptr);
    EXPECT_TRUE(darkRow->text.contains(QStringLiteral("0.05 % hot"))) << darkRow->text.toStdString();
    const DocumentElement* flatRow = one(parsed, flat->slug);
    ASSERT_NE(flatRow, nullptr);
    EXPECT_TRUE(flatRow->text.contains(QStringLiteral("corners at 72 %"))) << flatRow->text.toStdString();
    const DocumentElement* biasRow = one(parsed, bias->slug);
    ASSERT_NE(biasRow, nullptr);
    EXPECT_FALSE(biasRow->text.contains(QStringLiteral("hot")));
}

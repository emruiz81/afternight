// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The third part of the report's document, read back: the run against the earlier runs of its folder.
///
/// The part is in every report and is one sentence where the folder has no earlier run. Where it has, each master
/// the run made stands beside the latest earlier run that made it - the two pictures under one divider, or side by
/// side when the frames are of different shapes - with the figures of both and each change in words; the settings
/// that differ are listed once per change; and the runs of the folder are listed with this one last.

#include <gtest/gtest.h>

#include "ReportRenderedScenarios.h"

#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/reporting/ReportModelJson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>
#include <optional>
#include <vector>

using namespace an::tests::integration::report;
using namespace ImageIntegration::Reporting;

namespace
{
/// The one element of an id; null, with the case failed, when there is none or more than one.
const DocumentElement* only(const ParsedDocument& parsed, const char* id)
{
    const std::vector<const DocumentElement*> found = parsed.withId(QLatin1String(id));
    EXPECT_EQ(found.size(), 1u) << id;
    return found.size() == 1 ? found.front() : nullptr;
}

/// The cells of each row of a table's body, as text.
std::vector<QStringList> bodyCells(const ParsedDocument& parsed, const DocumentElement& table)
{
    std::vector<QStringList> rows;
    const DocumentElement* body = firstInside(parsed, table, "tbody");
    if (body == nullptr)
    {
        return rows;
    }
    for (const DocumentElement* row : namedInside(parsed, *body, "tr"))
    {
        QStringList cells;
        for (const DocumentElement* cell : childrenOf(parsed, *row))
        {
            cells << cell->text.simplified();
        }
        rows.push_back(cells);
    }
    return rows;
}

/// The row of a figures table whose first cell is the given name; empty when there is none.
QStringList rowNamed(const std::vector<QStringList>& rows, const QString& name)
{
    for (const QStringList& row : rows)
    {
        if (!row.isEmpty() && row.front() == name)
        {
            return row;
        }
    }
    return {};
}

/// The masters a comparison shows, in the document's order: the ones the model gives a baseline.
std::vector<const ReportMaster*> comparedOf(const ReportModel& model)
{
    std::vector<const ReportMaster*> masters;
    for (const ReportMaster& master : model.masters)
    {
        if (master.baseline.has_value())
        {
            masters.push_back(&master);
        }
    }
    return masters;
}

/// The elements of a name inside another that carry a class.
std::vector<const DocumentElement*> classedInside(const ParsedDocument& parsed, const DocumentElement& ancestor,
                                                  const char* name, const char* wanted)
{
    std::vector<const DocumentElement*> found;
    for (const DocumentElement* element : namedInside(parsed, ancestor, name))
    {
        if (element->hasClass(wanted))
        {
            found.push_back(element);
        }
    }
    return found;
}

/// A document of a model, written in the linked form and read back.
ParsedDocument linkedDocument(const ReportModel& model, const QString& runDirectory)
{
    const ReportDocument document =
        writeReportDocument(model, {.form = ReportForm::Linked, .runDirectory = runDirectory});
    EXPECT_TRUE(document.faults.isEmpty()) << document.faults.join(QLatin1Char('\n')).toStdString();
    ParsedDocument parsed = parseDocument(document.html);
    EXPECT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    return parsed;
}
} // namespace

// ---- The part and its blocks.

TEST_F(ReportHtmlWriterTest, ThePartOnEarlierRunsIsInEveryReportWithTheBlocksItsRunCallsFor)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ReportModel& model = report->model;
        for (const ParsedDocument* parsed : {&report->linkedParsed, &report->embeddedParsed})
        {
            const DocumentElement* part = only(*parsed, "earlier");
            ASSERT_NE(part, nullptr);
            EXPECT_EQ(part->name, QStringLiteral("section"));
            EXPECT_TRUE(part->hasClass("part"));
            const DocumentElement* heading = firstInside(*parsed, *part, "h2");
            ASSERT_NE(heading, nullptr);
            EXPECT_EQ(heading->text, QStringLiteral("Against earlier runs"));

            const bool compared = std::any_of(model.masters.begin(), model.masters.end(),
                                              [](const ReportMaster& master) { return master.baseline.has_value(); });
            const auto count = [parsed](const char* anchor) { return parsed->withId(QLatin1String(anchor)).size(); };
            EXPECT_EQ(count("before-after"), compared ? 1u : 0u);
            EXPECT_EQ(count("changed"), model.comparison.has_value() ? 1u : 0u);
            EXPECT_EQ(count("history"), model.history.empty() ? 0u : 1u);
            for (const char* block : {"before-after", "changed", "history"})
            {
                for (const DocumentElement* element : parsed->withId(QLatin1String(block)))
                {
                    EXPECT_TRUE(parsed->isInside(*element, *part)) << block;
                    EXPECT_TRUE(element->hasClass("block")) << block;
                }
            }
        }

        // The part stands after the masters' files and before what the masters share.
        QStringList parts;
        for (const DocumentElement* part : report->linkedParsed.ofClass("part"))
        {
            parts << part->attribute("id");
        }
        const qsizetype at = parts.indexOf(QStringLiteral("earlier"));
        ASSERT_GT(at, 0);
        EXPECT_EQ(parts.at(at - 1), QStringLiteral("masters"));
        ASSERT_LT(at + 1, parts.size());
        EXPECT_TRUE(parts.at(at + 1) == QStringLiteral("shared") || parts.at(at + 1) == QStringLiteral("run"));

        // The contents name it, with an entry for each block it has.
        const std::vector<const DocumentElement*> contents = report->linkedParsed.named("nav");
        ASSERT_EQ(contents.size(), 1u);
        EXPECT_TRUE(contents.front()->text.contains(QStringLiteral("Against earlier runs")));
    }
}

TEST_F(ReportHtmlWriterTest, WithNoEarlierRunThePartOnEarlierRunsIsOneSentence)
{
    for (const ReportScenarioId id :
         {ReportScenarioId::FiveFilters, ReportScenarioId::OneMonoMaster, ReportScenarioId::FailedInStacking})
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        ASSERT_TRUE(report->model.history.empty());
        const ParsedDocument& parsed = report->linkedParsed;
        const DocumentElement* part = only(parsed, "earlier");
        ASSERT_NE(part, nullptr);

        EXPECT_TRUE(namedInside(parsed, *part, "section").empty()) << "no block";
        EXPECT_TRUE(namedInside(parsed, *part, "table").empty());
        EXPECT_TRUE(namedInside(parsed, *part, "img").empty());
        const std::vector<const DocumentElement*> sentences = namedInside(parsed, *part, "p");
        ASSERT_EQ(sentences.size(), 1u);
        EXPECT_TRUE(sentences.front()->text.contains(QStringLiteral("no earlier run")))
            << sentences.front()->text.toStdString();
        EXPECT_EQ(sentences.front()->text.count(QLatin1Char('.')), 1) << "one sentence";

        // The contents lead to the part itself, since it has no block to lead to.
        const std::vector<const DocumentElement*> contents = parsed.named("nav");
        ASSERT_EQ(contents.size(), 1u);
        int entries = 0;
        for (const DocumentElement* link : namedInside(parsed, *contents.front(), "a"))
        {
            if (link->attribute("href") == QStringLiteral("#earlier"))
            {
                ++entries;
                EXPECT_EQ(link->text.simplified(), QStringLiteral("No earlier run"));
            }
        }
        EXPECT_EQ(entries, 1);
    }
}

// ---- Before and after.

TEST_F(ReportHtmlWriterTest, EachMasterTheRunMadeStandsBesideTheLatestEarlierRunThatMadeIt)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_NE(report, nullptr);
    const ReportModel& model = report->model;
    const std::vector<const ReportMaster*> masters = comparedOf(model);
    ASSERT_EQ(masters.size(), 6u);
    const ReportTimeZone zone{.id = model.run.timeZoneId, .utcOffsetSeconds = model.run.utcOffsetSeconds};

    const ParsedDocument& parsed = report->linkedParsed;
    const DocumentElement* block = only(parsed, "before-after");
    ASSERT_NE(block, nullptr);
    const std::vector<const DocumentElement*> comparisons = parsed.ofClassInside(*block, "cmp");
    ASSERT_EQ(comparisons.size(), 1u) << "one target, and no more masters than one comparison switches between";
    const std::vector<const DocumentElement*> items = parsed.ofClassInside(*block, "cmpitem");
    ASSERT_EQ(items.size(), masters.size());

    // A radio button per master picks which is shown, the first to begin with; two more pick the view.
    const std::vector<const DocumentElement*> picks = classedInside(parsed, *comparisons.front(), "input", "pick");
    ASSERT_EQ(picks.size(), masters.size());
    int checked = 0;
    for (std::size_t index = 0; index < picks.size(); ++index)
    {
        EXPECT_EQ(picks[index]->attribute("type"), QStringLiteral("radio"));
        EXPECT_TRUE(picks[index]->hasClass(qPrintable(QStringLiteral("pick-%1").arg(index))));
        EXPECT_EQ(parentOf(parsed, *picks[index]), comparisons.front()) << "the stylesheet's rules need it there";
        checked += picks[index]->has("checked") ? 1 : 0;
    }
    EXPECT_TRUE(picks.front()->has("checked"));
    EXPECT_EQ(checked, 1);
    const std::vector<const DocumentElement*> masterSwitch = parsed.ofClassInside(*block, "masters");
    ASSERT_EQ(masterSwitch.size(), 1u);
    const std::vector<const DocumentElement*> labels = namedInside(parsed, *masterSwitch.front(), "label");
    ASSERT_EQ(labels.size(), masters.size());
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        EXPECT_EQ(labels[index]->attribute("for"), picks[index]->attribute("id"));
        EXPECT_FALSE(labels[index]->text.trimmed().isEmpty());
        EXPECT_FALSE(labels[index]->text.contains(QLatin1Char('|'))) << "a master is named, not keyed";
    }
    const std::vector<const DocumentElement*> zooms = classedInside(parsed, *comparisons.front(), "input", "zoom");
    ASSERT_EQ(zooms.size(), 2u);
    EXPECT_TRUE(zooms.front()->has("checked"));
    EXPECT_FALSE(zooms.back()->has("checked"));

    for (std::size_t index = 0; index < masters.size(); ++index)
    {
        const ReportMaster& master = *masters[index];
        SCOPED_TRACE(master.name.toStdString());
        const DocumentElement& item = *items[index];
        EXPECT_TRUE(item.hasClass(qPrintable(QStringLiteral("cmpitem-%1").arg(index))));
        const ReportBaseline& baseline = *master.baseline;

        if (!baseline.made)
        {
            // A master no earlier run made says so, and shows nothing to compare.
            EXPECT_EQ(parsed.ofClassInside(item, "newmaster").size(), 1u);
            EXPECT_TRUE(item.text.contains(QStringLiteral("is new in this run")));
            EXPECT_TRUE(namedInside(parsed, item, "img").empty());
            EXPECT_TRUE(namedInside(parsed, item, "table").empty());
            continue;
        }

        // The two runs' pictures under one divider, which a range input moves.
        const std::vector<const DocumentElement*> dividers = parsed.ofClassInside(item, "ba");
        ASSERT_EQ(dividers.size(), 1u);
        EXPECT_TRUE(dividers.front()->has("data-compare"));
        EXPECT_TRUE(dividers.front()->hasClass("has-detail"));
        const std::vector<const DocumentElement*> ranges = namedInside(parsed, *dividers.front(), "input");
        ASSERT_EQ(ranges.size(), 1u);
        EXPECT_EQ(ranges.front()->attribute("type"), QStringLiteral("range"));
        EXPECT_FALSE(ranges.front()->attribute("aria-label").isEmpty());
        QStringList whole;
        QStringList detail;
        for (const DocumentElement* picture : namedInside(parsed, *dividers.front(), "img"))
        {
            EXPECT_FALSE(picture->attribute("alt").isEmpty());
            (picture->hasClass("detail") ? detail : whole) << picture->attribute("src");
        }
        EXPECT_EQ(whole, (QStringList{master.preview, baseline.preview})) << "this run's, then the earlier run's";
        EXPECT_EQ(detail, (QStringList{master.detail, baseline.detail}));
        const std::vector<const DocumentElement*> before = parsed.ofClassInside(*dividers.front(), "before");
        ASSERT_EQ(before.size(), 1u);
        EXPECT_EQ(namedInside(parsed, *before.front(), "img").size(), 2u);

        // The figures of both runs, and the change between them.
        const std::vector<const DocumentElement*> tables = classedInside(parsed, item, "table", "figures");
        ASSERT_EQ(tables.size(), 1u);
        const std::vector<QStringList> rows = bodyCells(parsed, *tables.front());
        const QStringList noise = rowNamed(rows, QStringLiteral("Noise"));
        ASSERT_EQ(noise.size(), 4);
        EXPECT_EQ(noise.at(1), formatFigure(baseline.backgroundNoise));
        EXPECT_EQ(noise.at(2), formatFigure(master.snapshot.backgroundNoise));
        EXPECT_FALSE(noise.at(3).isEmpty());
        if (baseline.frames.has_value())
        {
            const QStringList lights = rowNamed(rows, QStringLiteral("Lights used"));
            ASSERT_EQ(lights.size(), 4);
            EXPECT_EQ(
                lights.at(1),
                QStringLiteral("%1 of %2").arg(formatCount(*baseline.frames), formatCount(*baseline.framesTaken)));
            int used = 0;
            for (const ReportFrame& frame : master.frames)
            {
                used += frame.state == ReportFrameState::Used ? 1 : 0;
            }
            EXPECT_EQ(lights.at(2),
                      QStringLiteral("%1 of %2")
                          .arg(formatCount(used), formatCount(static_cast<qint64>(master.frames.size()))));
            EXPECT_TRUE(lights.at(3).startsWith(QLatin1Char('+'))) << lights.at(3).toStdString();
        }
        else
        {
            EXPECT_TRUE(rowNamed(rows, QStringLiteral("Lights used")).isEmpty()) << "a figure neither run states";
        }
        // The item names the run the master is compared with.
        ASSERT_TRUE(baseline.archivedAt.has_value());
        EXPECT_TRUE(item.text.contains(formatDateTime(*baseline.archivedAt, zone)));
    }

    // The embedded form carries the same comparison, its pictures inside it.
    const DocumentElement* embedded = only(report->embeddedParsed, "before-after");
    ASSERT_NE(embedded, nullptr);
    EXPECT_EQ(report->embeddedParsed.ofClassInside(*embedded, "cmpitem").size(), masters.size());
    const std::vector<const DocumentElement*> pictures = namedInside(report->embeddedParsed, *embedded, "img");
    EXPECT_EQ(pictures.size(), 5u * 4u);
    for (const DocumentElement* picture : pictures)
    {
        EXPECT_TRUE(picture->attribute("src").startsWith(QStringLiteral("data:image/jpeg;base64,")));
    }
}

TEST_F(ReportHtmlWriterTest, EveryChangeForTheBetterOrTheWorseSaysSoInWords)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_NE(report, nullptr);
    const ParsedDocument& parsed = report->linkedParsed;
    const DocumentElement* part = only(parsed, "earlier");
    ASSERT_NE(part, nullptr);

    int marked = 0;
    for (const DocumentElement* change : parsed.ofClassInside(*part, "chg"))
    {
        const bool better = change->hasClass("is-good");
        const bool worse = change->hasClass("is-bad");
        if (!better && !worse)
        {
            continue;
        }
        ++marked;
        // The colour is never what says it: an icon and the word stand beside the figure.
        EXPECT_EQ(namedInside(parsed, *change, "svg").size(), 1u);
        const DocumentElement* word = firstInside(parsed, *change, "small");
        ASSERT_NE(word, nullptr);
        EXPECT_EQ(word->text, better ? QStringLiteral("better") : QStringLiteral("worse"));
    }
    EXPECT_GT(marked, 0);
}

TEST_F(ReportHtmlWriterTest, ABaselineOfAnotherShapeStandsSideBySideWithNoDivider)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const auto luminance = std::find_if(model.masters.begin(), model.masters.end(), [](const ReportMaster& master) {
        return master.filter == QStringLiteral("Luminance") && master.kind == ReportMasterKind::Stack;
    });
    ASSERT_NE(luminance, model.masters.end());
    ASSERT_TRUE(luminance->baseline.has_value());
    luminance->baseline->sideBySide = true;

    const ParsedDocument parsed = linkedDocument(model, report->scenario.runDirectory);
    const DocumentElement* block = only(parsed, "before-after");
    ASSERT_NE(block, nullptr);
    const std::vector<const DocumentElement*> items = parsed.ofClassInside(*block, "cmpitem");
    ASSERT_EQ(items.size(), 6u);

    // The luminance master is the first: its two frames are apart, each named, with nothing to drag.
    const DocumentElement& apart = *items.front();
    EXPECT_TRUE(parsed.ofClassInside(apart, "ba").empty());
    EXPECT_TRUE(namedInside(parsed, apart, "input").empty());
    const std::vector<const DocumentElement*> pair = parsed.ofClassInside(apart, "apart");
    ASSERT_EQ(pair.size(), 1u);
    const std::vector<const DocumentElement*> figures = namedInside(parsed, *pair.front(), "figure");
    ASSERT_EQ(figures.size(), 2u);
    for (const DocumentElement* figure : figures)
    {
        EXPECT_EQ(namedInside(parsed, *figure, "img").size(), 2u) << "the whole frame and the detail";
        const DocumentElement* caption = firstInside(parsed, *figure, "figcaption");
        ASSERT_NE(caption, nullptr);
        EXPECT_FALSE(caption->text.trimmed().isEmpty());
    }
    EXPECT_EQ(figures.back()->text.simplified(), QStringLiteral("This run"));
    EXPECT_TRUE(apart.text.contains(QStringLiteral("side by side")));

    // The others keep their dividers.
    EXPECT_EQ(parsed.ofClassInside(*block, "ba").size(), 4u);
}

TEST_F(ReportHtmlWriterTest, TheDetailIsOfferedOnlyWhereBothRunsHaveOne)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_NE(report, nullptr);

    // No earlier run's detail: no switch between the whole frame and the detail, and no detail in the document.
    ReportModel none = report->model;
    for (ReportMaster& master : none.masters)
    {
        if (master.baseline.has_value())
        {
            master.baseline->detail.clear();
        }
    }
    const ParsedDocument without = linkedDocument(none, report->scenario.runDirectory);
    const DocumentElement* block = only(without, "before-after");
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(classedInside(without, *block, "input", "zoom").empty());
    EXPECT_TRUE(without.ofClassInside(*block, "zooms").empty());
    EXPECT_TRUE(without.ofClassInside(*block, "has-detail").empty());
    EXPECT_TRUE(classedInside(without, *block, "img", "detail").empty());
    EXPECT_EQ(classedInside(without, *block, "img", "whole").size(), 5u * 2u);

    // One master whose both runs have it: the switch is there, and that master alone answers it.
    ReportModel one = none;
    const auto luminance = std::find_if(one.masters.begin(), one.masters.end(), [](const ReportMaster& master) {
        return master.filter == QStringLiteral("Luminance") && master.kind == ReportMasterKind::Stack;
    });
    ASSERT_NE(luminance, one.masters.end());
    luminance->baseline->detail =
        report->model.masters[static_cast<std::size_t>(luminance - one.masters.begin())].baseline->detail;
    const ParsedDocument with = linkedDocument(one, report->scenario.runDirectory);
    block = only(with, "before-after");
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(classedInside(with, *block, "input", "zoom").size(), 2u);
    ASSERT_EQ(with.ofClassInside(*block, "zooms").size(), 1u);
    EXPECT_EQ(namedInside(with, *with.ofClassInside(*block, "zooms").front(), "label").size(), 2u);
    EXPECT_EQ(with.ofClassInside(*block, "has-detail").size(), 1u);
    EXPECT_EQ(classedInside(with, *block, "img", "detail").size(), 2u);
}

TEST_F(ReportHtmlWriterTest, AComparisonOfOneMasterHasNoSwitchBetweenMasters)
{
    const Rendered* report = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    ASSERT_EQ(model.masters.size(), 1u);
    ReportMaster& master = model.masters.front();
    ASSERT_FALSE(master.preview.isEmpty());
    master.baseline = ReportBaseline{.runId = QStringLiteral("20260301_210000_000"),
                                     .archivedAt = model.run.archivedAt,
                                     .made = true,
                                     .frames = 12,
                                     .framesTaken = 14,
                                     .backgroundNoise = 0.004,
                                     .preview = master.preview};

    const ParsedDocument parsed = linkedDocument(model, report->scenario.runDirectory);
    const DocumentElement* block = only(parsed, "before-after");
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(parsed.ofClassInside(*block, "cmpitem").size(), 1u);
    EXPECT_TRUE(classedInside(parsed, *block, "input", "pick").empty());
    EXPECT_TRUE(parsed.ofClassInside(*block, "masters").empty());
    EXPECT_TRUE(parsed.ofClassInside(*block, "cmpbar").empty()) << "neither a master nor a view to pick";
    EXPECT_EQ(parsed.ofClassInside(*block, "ba").size(), 1u);
}

TEST(ReportHtmlEarlier, ARunComparedWithTheSameRunStatesNoChange)
{
    auto scenario = buildReportScenario(ReportScenarioId::FiveFilters);
    ASSERT_TRUE(scenario.has_value());
    ASSERT_TRUE(archiveScenarioRun(*scenario));
    // An earlier run that stated what this run states: the two sides of every figure are read the same way, so
    // none of them differs.
    const ReportModel alone = buildScenarioModel(*scenario, scenario->runDirectory);
    const QString earlier = QFileInfo(scenario->runDirectory).dir().filePath(QStringLiteral("20260301_210000_000"));
    ASSERT_TRUE(QDir().mkpath(earlier));
    {
        QFile file(QDir(earlier).filePath(QLatin1String(kStoredReportFileName)));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(ReportModelJson::toJson(alone)).toJson(QJsonDocument::Compact));
    }

    const ReportModel model = buildScenarioModel(*scenario, scenario->runDirectory);
    ASSERT_TRUE(model.comparison.has_value());
    EXPECT_TRUE(model.comparison->settingsChanged.empty());
    EXPECT_TRUE(model.comparison->groupsChanged.empty());
    EXPECT_TRUE(model.comparison->inputsChanged.empty());
    EXPECT_GT(model.comparison->settingsCompared, 0);
    EXPECT_EQ(model.comparison->current, model.comparison->previous);

    const ParsedDocument parsed = linkedDocument(model, scenario->runDirectory);
    const DocumentElement* part = only(parsed, "earlier");
    ASSERT_NE(part, nullptr);
    const std::vector<const DocumentElement*> changes = parsed.ofClassInside(*part, "chg");
    EXPECT_GT(changes.size(), model.masters.size());
    for (const DocumentElement* change : changes)
    {
        EXPECT_EQ(change->text.simplified(), QStringLiteral("No change"));
        EXPECT_FALSE(change->hasClass("is-good"));
        EXPECT_FALSE(change->hasClass("is-bad"));
    }
    // The earlier run's archive holds no picture, so the figures alone are compared.
    const DocumentElement* block = only(parsed, "before-after");
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(namedInside(parsed, *block, "img").empty());
    EXPECT_EQ(classedInside(parsed, *block, "table", "figures").size(), model.masters.size());
    const DocumentElement* changed = only(parsed, "changed");
    ASSERT_NE(changed, nullptr);
    EXPECT_TRUE(changed->text.contains(QStringLiteral("No setting of the groups both runs have differs.")));
    EXPECT_TRUE(changed->text.contains(QStringLiteral("took the same frames")));
    EXPECT_TRUE(classedInside(parsed, *changed, "table", "changes").empty());
}

// ---- What changed.

TEST_F(ReportHtmlWriterTest, WhatChangedListsEachChangeOnceWithTheGroupsItIsIn)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_NE(report, nullptr);
    const ReportModel& model = report->model;
    ASSERT_TRUE(model.comparison.has_value());
    const ReportComparison& comparison = *model.comparison;
    const ParsedDocument& parsed = report->linkedParsed;
    const DocumentElement* block = only(parsed, "changed");
    ASSERT_NE(block, nullptr);

    // The run's totals in both runs.
    const std::vector<const DocumentElement*> totals = classedInside(parsed, *block, "table", "figures");
    ASSERT_EQ(totals.size(), 1u);
    const std::vector<QStringList> totalRows = bodyCells(parsed, *totals.front());
    const QStringList taken = rowNamed(totalRows, QStringLiteral("Lights taken"));
    ASSERT_EQ(taken.size(), 4);
    EXPECT_EQ(taken.at(1), formatCount(comparison.previous.frames));
    EXPECT_EQ(taken.at(2), formatCount(comparison.current.frames));
    const QStringList size = rowNamed(totalRows, QStringLiteral("Size of the masters"));
    ASSERT_EQ(size.size(), 4);
    EXPECT_EQ(size.at(1), formatByteSize(comparison.previous.outputBytes));
    EXPECT_EQ(size.at(2), formatByteSize(comparison.current.outputBytes));
    EXPECT_FALSE(rowNamed(totalRows, QStringLiteral("Masters")).isEmpty());

    // The weighting changed in four masters the same way: one row names the four.
    const std::vector<const DocumentElement*> tables = classedInside(parsed, *block, "table", "changes");
    ASSERT_EQ(tables.size(), 1u);
    const std::vector<QStringList> rows = bodyCells(parsed, *tables.front());
    int weighting = 0;
    for (const QStringList& row : rows)
    {
        ASSERT_EQ(row.size(), 5);
        EXPECT_NE(row.at(3), row.at(4)) << "a setting both runs state the same is not listed";
        EXPECT_FALSE(row.at(1).contains(QLatin1Char('|'))) << "a group is named, not keyed";
        if (row.at(2) == QStringLiteral("Weighting"))
        {
            ++weighting;
            EXPECT_EQ(row.at(0), QStringLiteral("Stacking"));
            EXPECT_EQ(row.at(1), QStringLiteral("Luminance, Red, Green, Blue"));
            EXPECT_EQ(row.at(3), QStringLiteral("Noise"));
            EXPECT_EQ(row.at(4), QStringLiteral("Weighted FWHM"));
        }
    }
    EXPECT_EQ(weighting, 1);
    EXPECT_LE(rows.size(), comparison.settingsChanged.size());

    // The groups only this run has, and the frames the others gained and lost.
    const std::vector<const DocumentElement*> lists = parsed.ofClassInside(*block, "chlist");
    ASSERT_EQ(lists.size(), 2u);
    EXPECT_EQ(namedInside(parsed, *lists.front(), "li").size(), comparison.groupsChanged.size());
    EXPECT_TRUE(lists.front()->text.contains(QStringLiteral("New in this run")));
    EXPECT_EQ(namedInside(parsed, *lists.back(), "li").size(), comparison.inputsChanged.size());
    EXPECT_TRUE(lists.back()->text.contains(QStringLiteral("28 frames added, 2 frames removed")))
        << lists.back()->text.toStdString();

    // The application: the version did not change, the inputs' fingerprint did.
    EXPECT_TRUE(block->text.contains(QStringLiteral("unchanged")));
    EXPECT_TRUE(block->text.contains(QStringLiteral("was %1").arg(comparison.previousInputsFingerprint.left(12))));
}

TEST_F(ReportHtmlWriterTest, AChangedSettingThatNamesAFileIsPrintedWithoutItsFolderInTheEmbeddedForm)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    ASSERT_TRUE(model.comparison.has_value());
    const QString output = QDir::fromNativeSeparators(model.run.outputFolder);
    ASSERT_FALSE(output.isEmpty());
    model.comparison->settingsChanged.push_back(
        ReportSettingChange{.stage = QStringLiteral("The run"),
                            .groupKey = {},
                            .group = {},
                            .block = QStringLiteral("Plate Solve"),
                            .name = QStringLiteral("Catalog"),
                            .before = output + QStringLiteral("/catalogs/earlier.cat"),
                            .after = QDir::toNativeSeparators(output + QStringLiteral("/catalogs/later.cat"))});

    const ReportDocument embedded =
        writeReportDocument(model, {.form = ReportForm::Embedded, .runDirectory = report->scenario.runDirectory});
    EXPECT_FALSE(embedded.html.contains(output, Qt::CaseInsensitive));
    EXPECT_FALSE(embedded.html.contains(QDir::toNativeSeparators(output), Qt::CaseInsensitive));
    EXPECT_TRUE(embedded.html.contains(QStringLiteral("earlier.cat")));
    EXPECT_TRUE(embedded.html.contains(QStringLiteral("later.cat")));

    // The linked form, which stays on the machine that wrote it, prints the values as the runs stated them.
    const ReportDocument linked =
        writeReportDocument(model, {.form = ReportForm::Linked, .runDirectory = report->scenario.runDirectory});
    EXPECT_TRUE(linked.html.contains(output + QStringLiteral("/catalogs/earlier.cat")));
}

// ---- The runs of the folder.

TEST_F(ReportHtmlWriterTest, TheRunsOfTheFolderAreListedWithThisRunLast)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterTwoEarlierRuns);
    ASSERT_NE(report, nullptr);
    const ReportModel& model = report->model;
    ASSERT_EQ(model.history.size(), 3u);
    const ParsedDocument& parsed = report->linkedParsed;
    const DocumentElement* block = only(parsed, "history");
    ASSERT_NE(block, nullptr);

    // Three figures over the runs, a chart each.
    const std::vector<const DocumentElement*> charts = parsed.ofClassInside(*block, "hist3");
    ASSERT_EQ(charts.size(), 1u);
    EXPECT_EQ(namedInside(parsed, *charts.front(), "svg").size(), 3u);
    EXPECT_EQ(namedInside(parsed, *charts.front(), "figcaption").size(), 3u);

    const std::vector<const DocumentElement*> tables = classedInside(parsed, *block, "table", "runs");
    ASSERT_EQ(tables.size(), 1u);
    const DocumentElement* body = firstInside(parsed, *tables.front(), "tbody");
    ASSERT_NE(body, nullptr);
    const std::vector<const DocumentElement*> rows = namedInside(parsed, *body, "tr");
    ASSERT_EQ(rows.size(), model.history.size() + 1);
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(rows[index]->hasClass("thisrun"), index + 1 == rows.size());
    }
    EXPECT_TRUE(rows.back()->text.contains(QStringLiteral("this run")));
    ASSERT_TRUE(model.run.framesUsed.has_value());
    EXPECT_TRUE(rows.back()->text.contains(
        QStringLiteral("%1 of %2").arg(formatCount(*model.run.framesUsed), formatCount(model.run.frames))));

    // A run that keeps a report states its figures and leads to its report; one that keeps none says so by its id.
    for (std::size_t index = 0; index < model.history.size(); ++index)
    {
        const ReportHistoryEntry& entry = model.history[index];
        SCOPED_TRACE(entry.runId.toStdString());
        // The report a run's archive holds is a file like any other: the row leads to it.
        const std::vector<const DocumentElement*> links = namedInside(parsed, *rows[index], "a");
        ASSERT_EQ(links.size(), 1u);
        EXPECT_EQ(links.front()->attribute("href"), entry.report);
        EXPECT_TRUE(QFileInfo::exists(QDir(report->scenario.runDirectory).filePath(entry.report)));
        if (!entry.refusal.isEmpty())
        {
            EXPECT_TRUE(rows[index]->text.contains(entry.runId));
            EXPECT_TRUE(rows[index]->text.contains(QStringLiteral("No report.")));
            EXPECT_TRUE(rows[index]->text.contains(entry.refusal));
            continue;
        }
        ASSERT_TRUE(entry.frames.has_value());
        ASSERT_TRUE(entry.framesUsed.has_value());
        EXPECT_TRUE(rows[index]->text.contains(
            QStringLiteral("%1 of %2").arg(formatCount(*entry.framesUsed), formatCount(*entry.frames))));
        EXPECT_TRUE(rows[index]->text.contains(formatFigure(entry.backgroundNoise)));
    }

    // The embedded form is one file: it lists the runs and leads to none of their reports.
    const DocumentElement* embedded = only(report->embeddedParsed, "history");
    ASSERT_NE(embedded, nullptr);
    EXPECT_TRUE(namedInside(report->embeddedParsed, *embedded, "a").empty());
    const std::vector<const DocumentElement*> embeddedTables =
        classedInside(report->embeddedParsed, *embedded, "table", "runs");
    ASSERT_EQ(embeddedTables.size(), 1u);
    EXPECT_EQ(namedInside(report->embeddedParsed, *embeddedTables.front(), "th").size(), 6u);
    EXPECT_EQ(namedInside(parsed, *tables.front(), "th").size(), 7u);
}

TEST_F(ReportHtmlWriterTest, AReusedMastersFileLeadsToTheReportOfTheRunThatMadeIt)
{
    const Rendered* report = reportOf(ReportScenarioId::IncrementalRun);
    ASSERT_NE(report, nullptr);
    const ReportModel& model = report->model;
    ASSERT_EQ(model.history.size(), 1u);
    ASSERT_FALSE(model.history.front().report.isEmpty());

    int reused = 0;
    for (const ReportMaster& master : model.masters)
    {
        if (master.state != ReportMasterState::Reused)
        {
            continue;
        }
        SCOPED_TRACE(master.name.toStdString());
        ++reused;
        EXPECT_EQ(master.madeByRun, model.history.front().runId);
        const std::vector<const DocumentElement*> files =
            report->linkedParsed.withId(QStringLiteral("file-") + master.slug);
        ASSERT_EQ(files.size(), 1u);
        const std::vector<const DocumentElement*> links = namedInside(report->linkedParsed, *files.front(), "a");
        EXPECT_TRUE(std::any_of(links.begin(), links.end(), [&model](const DocumentElement* link) {
            return link->attribute("href") == model.history.front().report;
        }));
    }
    EXPECT_EQ(reused, 3);
    // The comparison says why they are not in it.
    const DocumentElement* block = only(report->linkedParsed, "before-after");
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(block->text.contains(QStringLiteral("3 masters were still valid")));
    EXPECT_EQ(report->linkedParsed.ofClassInside(*block, "cmpitem").size(), 2u);
}

// ---- The header.

TEST_F(ReportHtmlWriterTest, TheHeaderSetsTheEarlierRunsFiguresUnderItsOwn)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFiltersAfterEarlierRun);
    ASSERT_NE(report, nullptr);
    const ReportModel& model = report->model;
    ASSERT_TRUE(model.comparison.has_value());
    const ReportRunTotals& previous = model.comparison->previous;
    ASSERT_TRUE(previous.framesUsed.has_value());
    ASSERT_TRUE(previous.integratedMs.has_value());
    const DocumentElement* header = only(report->linkedParsed, "top");
    ASSERT_NE(header, nullptr);

    QStringList under;
    for (const DocumentElement* line : report->linkedParsed.ofClassInside(*header, "delta"))
    {
        if (line->text.startsWith(QStringLiteral("Earlier run:")))
        {
            under << line->text.simplified();
        }
    }
    const QStringList expected = {
        QStringLiteral("Earlier run: %1 masters").arg(formatCount(previous.masters)),
        QStringLiteral("Earlier run: %1 of %2").arg(formatCount(*previous.framesUsed), formatCount(previous.frames)),
        QStringLiteral("Earlier run: %1").arg(formatDurationMs(*previous.integratedMs))};
    EXPECT_EQ(under, expected);

    // A run with no earlier run sets nothing under its figures.
    const Rendered* alone = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(alone, nullptr);
    const DocumentElement* aloneHeader = only(alone->linkedParsed, "top");
    ASSERT_NE(aloneHeader, nullptr);
    EXPECT_FALSE(aloneHeader->text.contains(QStringLiteral("Earlier run")));
}

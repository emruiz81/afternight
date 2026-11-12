// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The report's document, held by reading it back: it is well-formed for every scenario in both forms, each block is
/// there exactly when the run calls for it, every link lands, the embedded form reaches for nothing outside itself,
/// and what a run wrote comes out as text whatever it holds. The style and the script are held to their rules here
/// too: the tokens are the user guide's, the script names no network.

#include <gtest/gtest.h>

#include "ReportChartSamples.h"
#include "ReportDocumentTestSupport.h"
#include "ReportModelTestSupport.h"
#include "ReportRenderedScenarios.h"

#include "processes/ImageIntegration/reporting/ReportAssets.h"
#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <vector>

using namespace ImageIntegration::Reporting;
using namespace an::tests::integration::report;

namespace
{
/// Text that would break markup written without escaping, with a character XML forbids.
[[nodiscard]] QString hostileText()
{
    return QStringLiteral("M <b> & \"R\" 'G' ]]> </script> </style> <!--") + QChar(0x0001) + QStringLiteral("end");
}

/// The hostile text as a reader must get it back: the forbidden character replaced.
[[nodiscard]] QString hostileTextRead()
{
    return QStringLiteral("M <b> & \"R\" 'G' ]]> </script> </style> <!--") + QChar(0xFFFD) + QStringLiteral("end");
}

/// The folder that holds the repository's `src`, found from the working directory; empty when there is none.
[[nodiscard]] QString repositoryRoot()
{
    QDir folder(QDir::currentPath());
    do
    {
        if (folder.exists(QStringLiteral("src/AfterNightApp")))
        {
            return folder.absolutePath();
        }
    } while (folder.cdUp());
    return {};
}

[[nodiscard]] QString readTextFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

/// A stylesheet's rules: each innermost block's selector and its declarations by property. A rule inside an `@media`
/// block is listed with that block's condition before its selector.
struct CssRule
{
    QString selector;
    std::map<QString, QString> declarations;
};

[[nodiscard]] std::vector<CssRule> cssRules(QString css)
{
    static const QRegularExpression kComment(QStringLiteral("/\\*.*?\\*/"),
                                             QRegularExpression::DotMatchesEverythingOption);
    css.remove(kComment);
    std::vector<CssRule> rules;
    QString media;
    QString pending;
    int depth = 0;
    for (qsizetype index = 0; index < css.size(); ++index)
    {
        const QChar character = css.at(index);
        if (character == QLatin1Char('{'))
        {
            const QString head = pending.simplified();
            pending.clear();
            if (head.startsWith(QLatin1Char('@')))
            {
                media = head;
                ++depth;
                continue;
            }
            const qsizetype end = css.indexOf(QLatin1Char('}'), index);
            CssRule rule;
            rule.selector = media.isEmpty() ? head : media + QStringLiteral(" | ") + head;
            const QString body = css.mid(index + 1, end - index - 1);
            for (const QString& declaration : body.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            {
                const qsizetype colon = declaration.indexOf(QLatin1Char(':'));
                if (colon > 0)
                {
                    rule.declarations[declaration.left(colon).trimmed()] = declaration.mid(colon + 1).simplified();
                }
            }
            rules.push_back(rule);
            index = end;
        }
        else if (character == QLatin1Char('}'))
        {
            // The end of an `@media` block.
            depth = (std::max)(0, depth - 1);
            media.clear();
            pending.clear();
        }
        else
        {
            pending += character;
        }
    }
    return rules;
}

/// The custom properties of the first rule whose selector is the one given.
[[nodiscard]] std::map<QString, QString> tokensOf(const std::vector<CssRule>& rules, const QString& selector)
{
    std::map<QString, QString> tokens;
    for (const CssRule& rule : rules)
    {
        if (rule.selector != selector)
        {
            continue;
        }
        for (const auto& [property, value] : rule.declarations)
        {
            if (property.startsWith(QStringLiteral("--")))
            {
                tokens[property] = value;
            }
        }
        break;
    }
    return tokens;
}

/// A token's value with nothing that does not change the colour it names.
[[nodiscard]] QString normalizedValue(QString value)
{
    return value.remove(QLatin1Char(' ')).toLower();
}

/// The relative luminance of a colour, as the contrast of two colours is defined on it.
[[nodiscard]] double luminance(const QString& colour)
{
    const QColor parsed(colour);
    const auto linear = [](const double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(parsed.redF()) + 0.7152 * linear(parsed.greenF()) + 0.0722 * linear(parsed.blueF());
}

[[nodiscard]] double contrast(const QString& first, const QString& second)
{
    const double lighter = (std::max)(luminance(first), luminance(second));
    const double darker = (std::min)(luminance(first), luminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}

/// Whether any light of a model states one of the measures the nights are drawn of.
[[nodiscard]] bool anyLightMeasured(const ReportModel& model)
{
    const auto measured = [](const ReportFrame& frame) {
        return frame.fwhm.has_value() || frame.stars.has_value() || frame.backgroundNoise.has_value();
    };
    for (const ReportMaster& master : model.masters)
    {
        if (std::any_of(master.frames.begin(), master.frames.end(), measured))
        {
            return true;
        }
    }
    for (const ReportGroupNotStacked& group : model.groupsNotStacked)
    {
        if (std::any_of(group.frames.begin(), group.frames.end(), measured))
        {
            return true;
        }
    }
    return false;
}

/// How many states other than used have a light in a model, the two a user leaves a light out by counted as one.
[[nodiscard]] int leftOutStatesWithLights(const ReportModel& model)
{
    bool user = false;
    int others = 0;
    for (const ReportFrameStateCount& count : model.run.frameStates)
    {
        if (count.frames <= 0 || count.state == ReportFrameState::Used)
        {
            continue;
        }
        if (count.state == ReportFrameState::ExcludedByUser || count.state == ReportFrameState::StackExcludedByUser)
        {
            user = true;
        }
        else
        {
            ++others;
        }
    }
    return others + (user ? 1 : 0);
}
} // namespace

// ---- The markup's rules, on the writer itself.

TEST(ReportMarkup, WritesElementsAttributesAndTextUnderTheRules)
{
    ReportMarkup markup;
    markup.open("div", {{"class", QStringLiteral("card")}, {"hidden", QString()}, {"title", QStringLiteral("")}});
    markup.element("p", QStringLiteral("a < b & c"));
    markup.leaf("img", {{"src", QStringLiteral("a b.jpg")}, {"alt", QStringLiteral("say \"hi\"")}});
    markup.close("div");

    EXPECT_TRUE(markup.isBalanced());
    EXPECT_TRUE(markup.faults().isEmpty()) << markup.faults().join(QLatin1Char('\n')).toStdString();
    // An attribute with a null value is left out; one with an empty value is written.
    EXPECT_EQ(markup.markup(), QStringLiteral("<div class=\"card\" title=\"\"><p>a &lt; b &amp; c</p>"
                                              "<img src=\"a b.jpg\" alt=\"say &quot;hi&quot;\"/></div>"));
}

TEST(ReportMarkup, RefusesWhatTheRulesForbid)
{
    {
        ReportMarkup markup;
        markup.open("div").close("p");
        EXPECT_FALSE(markup.faults().isEmpty()) << "an end tag that does not match";
        EXPECT_FALSE(markup.isBalanced());
    }
    {
        ReportMarkup markup;
        markup.close("div");
        EXPECT_FALSE(markup.faults().isEmpty()) << "an end tag with nothing open";
    }
    {
        ReportMarkup markup;
        markup.open("br");
        EXPECT_FALSE(markup.faults().isEmpty()) << "a void element opened";
        EXPECT_TRUE(markup.markup().isEmpty());
    }
    {
        ReportMarkup markup;
        markup.leaf("div");
        EXPECT_FALSE(markup.faults().isEmpty()) << "an element with content closed in its own tag";
        EXPECT_TRUE(markup.markup().isEmpty());
    }
    {
        ReportMarkup markup;
        markup.open("Div");
        markup.open("a b");
        markup.open("p><script");
        EXPECT_EQ(markup.faults().size(), 3) << "an element's name that is not lower-case letters and digits";
        EXPECT_TRUE(markup.markup().isEmpty());
    }
    {
        ReportMarkup markup;
        markup.open("p", {{"on click", QStringLiteral("x")}, {"x\"y", QStringLiteral("x")}}).close("p");
        EXPECT_EQ(markup.faults().size(), 2) << "an attribute's name that is not a plain name";
        EXPECT_EQ(markup.markup(), QStringLiteral("<p></p>"));
    }
    {
        ReportMarkup markup;
        markup.icon("no-such-icon");
        EXPECT_FALSE(markup.faults().isEmpty()) << "an icon the report does not have";
    }
    {
        ReportMarkup inner;
        inner.open("p");
        ReportMarkup outer;
        outer.append(inner);
        EXPECT_FALSE(outer.faults().isEmpty()) << "markup appended with an element still open";
    }
}

TEST(ReportMarkup, EscapesTextAndReplacesWhatXmlForbids)
{
    EXPECT_EQ(ReportMarkup::escaped(QStringLiteral("<a href=\"x\">'&'</a>")),
              QStringLiteral("&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;"));
    // A control character other than a tab and a line break is replaced; so are the two code points XML excludes and
    // half of a surrogate pair.
    EXPECT_EQ(ReportMarkup::escaped(QStringLiteral("a") + QChar(0x0001) + QChar(0x001F) + QStringLiteral("b")),
              QStringLiteral("a") + QChar(0xFFFD) + QChar(0xFFFD) + QStringLiteral("b"));
    EXPECT_EQ(ReportMarkup::escaped(QString(QChar(0xFFFE)) + QChar(0xFFFF) + QChar(0xD800)), QString(3, QChar(0xFFFD)));
    const QString pair = QString::fromUcs4(U"\U0001F52D");
    EXPECT_EQ(ReportMarkup::escaped(pair), pair);
    // In text a line break is itself; in an attribute a parser would read it as a space, so it is a reference.
    EXPECT_EQ(ReportMarkup::escaped(QStringLiteral("a\tb\r\nc\rd"), false), QStringLiteral("a\tb\nc\nd"));
    EXPECT_EQ(ReportMarkup::escaped(QStringLiteral("a\tb\nc"), true), QStringLiteral("a&#9;b&#10;c"));
}

TEST(ReportMarkup, AnIconIsHiddenFromAReaderWhoCannotSeeIt)
{
    ReportMarkup markup;
    markup.icon("warning", QStringLiteral("big"));
    const ParsedDocument parsed = parseDocument(markup.markup());
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    ASSERT_FALSE(parsed.elements.empty());
    EXPECT_EQ(parsed.elements.front().name, QStringLiteral("svg"));
    EXPECT_EQ(parsed.elements.front().attribute("aria-hidden"), QStringLiteral("true"));
    EXPECT_TRUE(parsed.elements.front().hasClass("ic"));
    EXPECT_TRUE(parsed.elements.front().hasClass("big"));
    EXPECT_TRUE(selfClosingFaults(markup.markup()).isEmpty());
}

// ---- The document, for every scenario.

TEST_F(ReportHtmlWriterTest, TheDocumentIsWellFormedForEveryScenarioInBothForms)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        for (const bool embedded : {false, true})
        {
            SCOPED_TRACE(embedded ? "embedded" : "linked");
            const ReportDocument& document = embedded ? report->embedded : report->linked;
            const ParsedDocument& parsed = embedded ? report->embeddedParsed : report->linkedParsed;
            EXPECT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
            EXPECT_TRUE(document.faults.isEmpty()) << document.faults.join(QLatin1Char('\n')).toStdString();
            EXPECT_TRUE(document.html.startsWith(QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">")));
            EXPECT_EQ(parsed.named("html").size(), 1u);
            EXPECT_EQ(parsed.named("head").size(), 1u);
            EXPECT_EQ(parsed.named("body").size(), 1u);
            EXPECT_EQ(parsed.named("main").size(), 1u);
            EXPECT_EQ(parsed.named("h1").size(), 1u);
            const std::vector<const DocumentElement*> titles = parsed.named("title");
            ASSERT_EQ(titles.size(), 1u);
            EXPECT_FALSE(titles.front()->text.trimmed().isEmpty());
        }
    }
}

TEST_F(ReportHtmlWriterTest, NoElementIsClosedInItsOwnTagButAVoidOneOrOneInsideAnSvg)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const QStringList faults = selfClosingFaults(report->linked.html);
        EXPECT_TRUE(faults.isEmpty()) << faults.mid(0, 10).join(QLatin1Char('\n')).toStdString();
    }
    // The check itself tells the two apart.
    EXPECT_EQ(selfClosingFaults(QStringLiteral("<div class=\"a\"/><svg><path d=\"M0 0\"/></svg><br/>")).size(), 1);
    EXPECT_EQ(selfClosingFaults(QStringLiteral("<p><br></p>")).size(), 1);
}

TEST_F(ReportHtmlWriterTest, EachBlockIsThereExactlyWhenTheRunCallsForIt)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;

        const auto count = [&parsed](const char* anchor) { return parsed.withId(QLatin1String(anchor)).size(); };
        EXPECT_EQ(count("top"), 1u);
        EXPECT_EQ(count("summary"), 1u);
        EXPECT_EQ(count("attention"), 1u);
        EXPECT_EQ(count("outcome"), stopped(model) ? 1u : 0u);
        EXPECT_EQ(count("made"), model.masters.size() > 1 ? 1u : 0u);
        EXPECT_EQ(count("frames"), model.run.frames > 0 ? 1u : 0u);
        EXPECT_EQ(count("nights"), anyLightMeasured(model) ? 1u : 0u);

        // No id is written twice.
        QStringList ids = parsed.ids();
        const qsizetype written = ids.size();
        ids.removeDuplicates();
        EXPECT_EQ(ids.size(), written);

        // The blocks of the summary are inside it, and the outcome of a run that stopped leads them.
        const DocumentElement* summary = parsed.byId("summary");
        ASSERT_NE(summary, nullptr);
        const std::vector<const DocumentElement*> blocks = parsed.ofClassInside(*summary, "block");
        ASSERT_FALSE(blocks.empty());
        EXPECT_EQ(blocks.front()->attribute("id"),
                  stopped(model) ? QStringLiteral("outcome") : QStringLiteral("attention"));
    }

    // The scenarios that are built for a block's presence or absence state it by name.
    const Rendered* oneMaster = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(oneMaster, nullptr);
    EXPECT_TRUE(oneMaster->linkedParsed.withId(QStringLiteral("made")).empty());
    EXPECT_TRUE(oneMaster->linkedParsed.withId(QStringLiteral("outcome")).empty());

    const Rendered* five = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(five, nullptr);
    EXPECT_EQ(five->linkedParsed.withId(QStringLiteral("made")).size(), 1u);
    EXPECT_EQ(five->linkedParsed.withId(QStringLiteral("nights")).size(), 1u);
    EXPECT_EQ(five->linkedParsed.withId(QStringLiteral("quality")).size(), 1u);
    EXPECT_TRUE(five->linkedParsed.withId(QStringLiteral("outcome")).empty());

    const Rendered* withoutMetrics = reportOf(ReportScenarioId::FiveFiltersWithoutMetrics);
    ASSERT_NE(withoutMetrics, nullptr);
    EXPECT_TRUE(withoutMetrics->linkedParsed.withId(QStringLiteral("nights")).empty());

    for (const ReportScenarioId id : {ReportScenarioId::FailedInStacking, ReportScenarioId::CancelledAfterTwoMasters})
    {
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        EXPECT_EQ(report->linkedParsed.withId(QStringLiteral("outcome")).size(), 1u) << scenarioName(id);
    }
}

TEST_F(ReportHtmlWriterTest, EveryLinkLandsAndTheContentsNameEveryBlock)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const QStringList ids = parsed.ids();

        // A link leads to a place of the document, or - in the linked form, which lies in the folder's history - to
        // the report of an earlier run of the folder, which is there.
        QStringList earlierReports;
        for (const ReportHistoryEntry& entry : report->model.history)
        {
            if (!entry.report.isEmpty())
            {
                earlierReports << entry.report;
            }
        }
        for (const DocumentElement* link : parsed.named("a"))
        {
            const QString target = link->attribute("href");
            EXPECT_FALSE(link->text.trimmed().isEmpty()) << target.toStdString() << " has no words";
            if (earlierReports.contains(target))
            {
                EXPECT_TRUE(QFileInfo::exists(QDir(report->scenario.runDirectory).filePath(target)))
                    << target.toStdString() << " leads to a report that is not there";
                continue;
            }
            ASSERT_TRUE(target.startsWith(QLatin1Char('#'))) << target.toStdString();
            EXPECT_TRUE(ids.contains(target.mid(1))) << target.toStdString() << " leads nowhere";
        }

        // The contents lead to the overview, to every block and to every master's file, in the document's order,
        // and to nothing else.
        const std::vector<const DocumentElement*> rails = parsed.ofClass("toc");
        ASSERT_EQ(rails.size(), 1u);
        EXPECT_EQ(rails.front()->name, QStringLiteral("nav"));
        QStringList listed;
        for (const DocumentElement* link : parsed.named("a"))
        {
            if (parsed.isInside(*link, *rails.front()))
            {
                listed << link->attribute("href").mid(1);
            }
        }
        QStringList expected = {QStringLiteral("top")};
        for (const DocumentElement& element : parsed.elements)
        {
            if (element.hasClass("block") || element.hasClass("file"))
            {
                expected << element.attribute("id");
            }
            else if (element.hasClass("part") && parsed.ofClassInside(element, "block").empty() &&
                     parsed.ofClassInside(element, "file").empty())
            {
                // A part of one sentence has no block: the contents lead to the part itself.
                expected << element.attribute("id");
            }
        }
        EXPECT_EQ(listed, expected);

        // Each part's heading stands over its entries.
        EXPECT_EQ(parsed.ofClassInside(*rails.front(), "toc-h").size(), parsed.ofClass("part").size());
    }
}

// The document closes by saying which application wrote it, of which run, and when the run was archived: what a
// printed page still says of itself.
TEST_F(ReportHtmlWriterTest, TheFooterNamesTheApplicationTheRunAndWhenItWasArchived)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ReportRun& run = report->model.run;
        for (const ParsedDocument* parsed : {&report->linkedParsed, &report->embeddedParsed})
        {
            const std::vector<const DocumentElement*> footers = parsed->named("footer");
            ASSERT_EQ(footers.size(), 1u);
            const QString text = footers.front()->text;
            EXPECT_TRUE(text.contains(QStringLiteral("AfterNight %1").arg(run.applicationVersion)))
                << text.toStdString();
            EXPECT_TRUE(text.contains(run.executionId)) << text.toStdString();
            ASSERT_TRUE(run.archivedAt.has_value());
            EXPECT_TRUE(text.contains(QStringLiteral("archived"))) << text.toStdString();
            EXPECT_TRUE(
                text.contains(QString::number(run.archivedAt->toOffsetFromUtc(run.utcOffsetSeconds).date().year())))
                << text.toStdString();
        }
    }
}

TEST_F(ReportHtmlWriterTest, TheHeaderStatesTheModelsVerdictAndFigures)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportRun& run = report->model.run;

        const DocumentElement* top = parsed.byId("top");
        ASSERT_NE(top, nullptr);
        EXPECT_EQ(top->name, QStringLiteral("header"));

        const std::vector<const DocumentElement*> verdicts = parsed.ofClassInside(*top, "verdict");
        ASSERT_EQ(verdicts.size(), 1u);
        const QString verdict = verdicts.front()->text;
        EXPECT_TRUE(verdict.contains(run.verdict)) << verdict.toStdString();
        if (run.framesUsed.has_value() && run.integratedMs.has_value())
        {
            EXPECT_TRUE(verdict.contains(formatCount(*run.framesUsed))) << verdict.toStdString();
            EXPECT_TRUE(verdict.contains(formatCount(run.frames))) << verdict.toStdString();
            EXPECT_TRUE(verdict.contains(formatDurationMs(*run.integratedMs))) << verdict.toStdString();
        }
        if (run.processingMs > 0)
        {
            EXPECT_TRUE(verdict.contains(formatDurationMs(run.processingMs))) << verdict.toStdString();
        }

        // Four figures, and one pill that says how the run ended in a word as well as a colour.
        EXPECT_EQ(parsed.ofClassInside(*top, "tile").size(), 4u);
        const std::vector<const DocumentElement*> pills = parsed.ofClassInside(*top, "pill");
        ASSERT_FALSE(pills.empty());
        EXPECT_FALSE(pills.front()->text.trimmed().isEmpty());
        const bool critical = pills.front()->hasClass("is-critical");
        EXPECT_EQ(critical, stopped(report->model));
        if (run.outcome == ReportOutcome::Completed)
        {
            EXPECT_TRUE(pills.front()->hasClass("is-good"));
        }

        // The title is the run's target, or how many it has.
        const std::vector<const DocumentElement*> headings = parsed.named("h1");
        ASSERT_EQ(headings.size(), 1u);
        if (run.targets.size() == 1)
        {
            EXPECT_EQ(headings.front()->text, run.targets.front());
        }
        else if (run.targets.size() > 1)
        {
            EXPECT_TRUE(headings.front()->text.contains(QString::number(run.targets.size())));
        }
    }
}

TEST_F(ReportHtmlWriterTest, TheIssuesAreInTheModelsOrderEachWithItsAction)
{
    bool anyAction = false;
    bool anyIssue = false;
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;

        const DocumentElement* attention = parsed.byId("attention");
        ASSERT_NE(attention, nullptr);
        const std::vector<const DocumentElement*> items = parsed.ofClassInside(*attention, "issue");
        ASSERT_EQ(items.size(), model.issues.size());

        int lastSeverity = -1;
        bool inMessages = false;
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const ReportIssue& issue = model.issues[index];
            const DocumentElement& item = *items[index];
            SCOPED_TRACE(issue.title.toStdString());
            anyIssue = true;

            const char* severity = issue.severity == ReportIssueSeverity::Critical  ? "is-critical"
                                   : issue.severity == ReportIssueSeverity::Warning ? "is-warning"
                                                                                    : "is-note";
            EXPECT_TRUE(item.hasClass(severity));
            const DocumentElement* title = firstInside(parsed, item, "b");
            ASSERT_NE(title, nullptr);
            EXPECT_EQ(title->text, issue.title);
            if (!issue.detail.trimmed().isEmpty())
            {
                EXPECT_TRUE(item.text.contains(issue.detail));
            }

            const std::vector<const DocumentElement*> actions = parsed.ofClassInside(item, "do");
            if (issue.action.trimmed().isEmpty())
            {
                EXPECT_TRUE(actions.empty());
            }
            else
            {
                anyAction = true;
                ASSERT_EQ(actions.size(), 1u);
                EXPECT_TRUE(actions.front()->text.contains(issue.action));
            }

            // The computed kinds come first, the most severe first; the run's own messages follow them.
            if (issue.kind == ReportIssueKind::RunMessage)
            {
                inMessages = true;
            }
            else
            {
                EXPECT_FALSE(inMessages) << "a computed issue after the run's messages";
                EXPECT_GE(static_cast<int>(issue.severity), lastSeverity);
                lastSeverity = static_cast<int>(issue.severity);
            }
        }

        // A run with nothing to attend to says so.
        const bool attentionNeeded =
            std::any_of(model.issues.begin(), model.issues.end(),
                        [](const ReportIssue& issue) { return issue.severity != ReportIssueSeverity::Note; });
        EXPECT_EQ(parsed.ofClassInside(*attention, "calm").size(), attentionNeeded ? 0u : 1u);
    }
    EXPECT_TRUE(anyIssue);
    EXPECT_TRUE(anyAction);
}

TEST_F(ReportHtmlWriterTest, MoreThanFiveMessagesOfTheRunAreFolded)
{
    const Rendered* report = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const auto messagesOf = [](const ReportModel& from) {
        return std::count_if(from.issues.begin(), from.issues.end(),
                             [](const ReportIssue& issue) { return issue.kind == ReportIssueKind::RunMessage; });
    };
    for (int number = static_cast<int>(messagesOf(model)); number < 5; ++number)
    {
        model.issues.push_back(ReportIssue{.kind = ReportIssueKind::RunMessage,
                                           .severity = ReportIssueSeverity::Warning,
                                           .title = QStringLiteral("A warning of the run, number %1").arg(number),
                                           .detail = {},
                                           .action = {},
                                           .groupKey = {},
                                           .anchor = {}});
    }
    ASSERT_EQ(messagesOf(model), 5);

    const ParsedDocument five = parseDocument(writeReportDocument(model, {}).html);
    ASSERT_TRUE(five.error.isEmpty()) << five.error.toStdString();
    ASSERT_NE(five.byId("attention"), nullptr);
    EXPECT_TRUE(namedInside(five, *five.byId("attention"), "details").empty());
    EXPECT_EQ(five.ofClass("issue").size(), model.issues.size());

    model.issues.push_back(ReportIssue{.kind = ReportIssueKind::RunMessage,
                                       .severity = ReportIssueSeverity::Critical,
                                       .title = QStringLiteral("An error of the run"),
                                       .detail = {},
                                       .action = {},
                                       .groupKey = {},
                                       .anchor = {}});
    const ParsedDocument six = parseDocument(writeReportDocument(model, {}).html);
    ASSERT_TRUE(six.error.isEmpty()) << six.error.toStdString();
    ASSERT_NE(six.byId("attention"), nullptr);
    const std::vector<const DocumentElement*> folds = namedInside(six, *six.byId("attention"), "details");
    ASSERT_EQ(folds.size(), 1u);
    // Every message is inside the fold, word for word, and none was dropped.
    EXPECT_EQ(six.ofClassInside(*folds.front(), "issue").size(), 6u);
    EXPECT_EQ(six.ofClass("issue").size(), model.issues.size());
    EXPECT_TRUE(folds.front()->text.contains(QStringLiteral("An error of the run")));
}

TEST_F(ReportHtmlWriterTest, AReasonWithNoLightAppearsNowhere)
{
    bool anyReason = false;
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;
        const DocumentElement* frames = parsed.byId("frames");
        if (frames == nullptr)
        {
            continue;
        }
        const int reasons = leftOutStatesWithLights(model);
        anyReason = anyReason || reasons > 0;

        // A row per reason that has a light, each stating how many, and no row for a reason that has none.
        const std::vector<const DocumentElement*> tables = parsed.ofClassInside(*frames, "reasons");
        ASSERT_EQ(tables.size(), reasons > 0 ? 1u : 0u);
        if (reasons > 0)
        {
            int rows = 0;
            int lights = 0;
            for (const DocumentElement* row : parsed.named("tr"))
            {
                if (!parsed.isInside(*row, *tables.front()) || firstInside(parsed, *row, "td") == nullptr)
                {
                    continue;
                }
                ++rows;
                const std::vector<const DocumentElement*> cells = childrenOf(parsed, *row);
                ASSERT_EQ(cells.size(), 4u);
                QString figure = cells[1]->text;
                figure.remove(QChar(0x2009));
                EXPECT_GT(figure.toInt(), 0);
                lights += figure.toInt();
                EXPECT_FALSE(cells[2]->text.trimmed().isEmpty()) << "a reason that says where its lights are";
            }
            EXPECT_EQ(rows, reasons);
            int leftOut = 0;
            for (const ReportFrameStateCount& count : model.run.frameStates)
            {
                leftOut += count.state == ReportFrameState::Used ? 0 : count.frames;
            }
            EXPECT_EQ(lights, leftOut);
        }

        // The legend of the bar names the same states, with the lights used when there are any.
        const bool anyUsed = std::any_of(model.run.frameStates.begin(), model.run.frameStates.end(),
                                         [](const ReportFrameStateCount& count) {
                                             return count.state == ReportFrameState::Used && count.frames > 0;
                                         });
        const std::vector<const DocumentElement*> legends = parsed.ofClassInside(*frames, "legend");
        const std::size_t entries = static_cast<std::size_t>(reasons + (anyUsed ? 1 : 0));
        ASSERT_EQ(legends.size(), entries > 0 ? 1u : 0u);
        if (entries > 0)
        {
            EXPECT_EQ(childrenOf(parsed, *legends.front()).size(), entries);
        }
    }
    EXPECT_TRUE(anyReason);

    // The run built to hold every state a light can end in states each of them.
    const Rendered* every = reportOf(ReportScenarioId::EveryLightState);
    ASSERT_NE(every, nullptr);
    EXPECT_GE(leftOutStatesWithLights(every->model), 4);
}

TEST_F(ReportHtmlWriterTest, ARunOfOneMasterHasItsPictureInTheHeaderAndNoGallery)
{
    const Rendered* report = reportOf(ReportScenarioId::OneColourMaster);
    ASSERT_NE(report, nullptr);
    const ParsedDocument& parsed = report->linkedParsed;
    ASSERT_EQ(report->model.masters.size(), 1u);
    const ReportMaster& master = report->model.masters.front();
    ASSERT_FALSE(master.preview.isEmpty());

    EXPECT_TRUE(parsed.ofClass("gallery").empty());
    EXPECT_TRUE(parsed.ofClass("compare").empty());

    const DocumentElement* top = parsed.byId("top");
    ASSERT_NE(top, nullptr);
    EXPECT_TRUE(top->hasClass("one"));
    const std::vector<const DocumentElement*> shots = parsed.ofClassInside(*top, "shot");
    ASSERT_EQ(shots.size(), 1u);
    const DocumentElement* picture = firstInside(parsed, *shots.front(), "img");
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->attribute("src"), master.preview);
    EXPECT_FALSE(picture->attribute("alt").isEmpty());

    // Its third figure is the master's noise, which a run of several masters states master by master instead.
    const std::vector<const DocumentElement*> tiles = parsed.ofClassInside(*top, "tile");
    ASSERT_EQ(tiles.size(), 4u);
    EXPECT_TRUE(tiles[2]->text.contains(formatFigure(master.snapshot.backgroundNoise)));

    // A run of one master whose picture is not there keeps the header's one column.
    ReportModel unpictured = report->model;
    unpictured.masters.front().preview.clear();
    const ParsedDocument plain = parseDocument(writeReportDocument(unpictured, {}).html);
    ASSERT_TRUE(plain.error.isEmpty()) << plain.error.toStdString();
    ASSERT_NE(plain.byId("top"), nullptr);
    EXPECT_FALSE(plain.byId("top")->hasClass("one"));
    EXPECT_TRUE(namedInside(plain, *plain.byId("top"), "img").empty());
}

TEST_F(ReportHtmlWriterTest, TheMastersOfSeveralMastersAreACardAndARowEach)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    const ParsedDocument& parsed = report->linkedParsed;
    const ReportModel& model = report->model;

    const DocumentElement* made = parsed.byId("made");
    ASSERT_NE(made, nullptr);
    EXPECT_EQ(parsed.ofClassInside(*made, "mcard").size(), model.masters.size());
    EXPECT_EQ(parsed.ofClassInside(*made, "gallery").size(), 1u);
    EXPECT_FALSE(parsed.byId("top")->hasClass("one"));

    // A row per master: its figures are the model's.
    const std::vector<const DocumentElement*> tables = parsed.ofClassInside(*made, "compare");
    ASSERT_EQ(tables.size(), 1u);
    std::vector<const DocumentElement*> rows;
    for (const DocumentElement* row : parsed.named("tr"))
    {
        if (parsed.isInside(*row, *tables.front()) && firstInside(parsed, *row, "td") != nullptr)
        {
            rows.push_back(row);
        }
    }
    ASSERT_EQ(rows.size(), model.masters.size());
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const ReportMaster& master = model.masters[index];
        SCOPED_TRACE(master.name.toStdString());
        const std::vector<const DocumentElement*> cells = childrenOf(parsed, *rows[index]);
        ASSERT_EQ(cells.size(), 6u);
        EXPECT_TRUE(cells[1]->text.startsWith(formatFigure(master.snapshot.snr)));
        EXPECT_EQ(cells[2]->text, formatFigure(master.snapshot.backgroundNoise));
        if (master.stack.has_value())
        {
            EXPECT_TRUE(cells[3]->text.startsWith(formatFigure(master.stack->effectiveFrames)));
        }
        else
        {
            EXPECT_EQ(cells[3]->text, noValueText());
        }
    }

    // A picture per master the archive holds one of, each said in words.
    std::size_t pictured = 0;
    for (const ReportMaster& master : model.masters)
    {
        pictured += master.preview.isEmpty() ? 0u : 1u;
    }
    EXPECT_GT(pictured, 0u);
    std::size_t pictures = 0;
    for (const DocumentElement* picture : parsed.named("img"))
    {
        if (parsed.isInside(*picture, *made))
        {
            ++pictures;
            EXPECT_FALSE(picture->attribute("alt").trimmed().isEmpty());
        }
    }
    EXPECT_EQ(pictures, pictured);
}

TEST_F(ReportHtmlWriterTest, SeveralTargetsAreGroupedByTarget)
{
    const Rendered* report = reportOf(ReportScenarioId::TwoTargets);
    ASSERT_NE(report, nullptr);
    const ParsedDocument& parsed = report->linkedParsed;
    const ReportModel& model = report->model;
    ASSERT_EQ(model.targets.size(), 2u);

    const DocumentElement* made = parsed.byId("made");
    ASSERT_NE(made, nullptr);
    const std::vector<const DocumentElement*> targets = parsed.ofClassInside(*made, "target");
    ASSERT_EQ(targets.size(), 2u);
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        // Each target has its own line of lights and integration.
        EXPECT_TRUE(targets[index]->text.contains(model.targets[index].name));
        EXPECT_TRUE(targets[index]->text.contains(formatDurationMs(model.targets[index].integrationMs)));
        EXPECT_TRUE(targets[index]->text.contains(formatCount(model.targets[index].frames)));
    }
    EXPECT_EQ(parsed.ofClassInside(*made, "gallery").size(), 2u);
    EXPECT_EQ(parsed.ofClassInside(*made, "mcard").size(), model.masters.size());
    EXPECT_EQ(parsed.ofClassInside(*made, "grouprow").size(), 2u);

    const std::vector<const DocumentElement*> headings = parsed.named("h1");
    ASSERT_EQ(headings.size(), 1u);
    EXPECT_TRUE(headings.front()->text.contains(QStringLiteral("2")));
}

TEST_F(ReportHtmlWriterTest, ARunThatStoppedSaysWhatWasKeptAndWhatWasNot)
{
    for (const ReportScenarioId id : {ReportScenarioId::FailedInStacking, ReportScenarioId::CancelledAfterTwoMasters})
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;
        const ReportModel& model = report->model;

        const DocumentElement* outcome = parsed.byId("outcome");
        ASSERT_NE(outcome, nullptr);
        const std::vector<const DocumentElement*> cards = parsed.ofClassInside(*outcome, "card");
        ASSERT_EQ(cards.size(), 3u);

        std::size_t kept = 0;
        std::size_t lost = 0;
        for (const ReportMaster& master : model.masters)
        {
            const bool there = master.state == ReportMasterState::Made || master.state == ReportMasterState::Reused;
            (there ? kept : lost) += 1;
        }
        ASSERT_GT(kept, 0u);
        ASSERT_GT(lost, 0u);
        const auto items = [&parsed](const DocumentElement& card) {
            std::size_t count = 0;
            for (const DocumentElement* item : parsed.named("li"))
            {
                count += parsed.isInside(*item, card) ? 1u : 0u;
            }
            return count;
        };
        EXPECT_EQ(items(*cards[0]), kept);
        EXPECT_EQ(items(*cards[1]), lost);
        EXPECT_FALSE(cards[2]->text.trimmed().isEmpty());

        // The header's pill and the gallery say the same of each master.
        const DocumentElement* made = parsed.byId("made");
        ASSERT_NE(made, nullptr);
        std::size_t marked = 0;
        for (const DocumentElement* pill : parsed.ofClassInside(*made, "pill"))
        {
            marked += pill->hasClass("is-critical") || pill->hasClass("is-skipped") ? 1u : 0u;
        }
        EXPECT_GE(marked, lost);
    }
}

TEST_F(ReportHtmlWriterTest, NoTextHoldsAPlaceholderARawGroupKeyOrAValueThatIsNotANumber)
{
    static const QRegularExpression kPlaceholder(QStringLiteral("%[0-9]"));
    static const QRegularExpression kNotANumber(QStringLiteral("\\b(nan|inf)\\b"),
                                                QRegularExpression::CaseInsensitiveOption);
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->linkedParsed;

        // A message of the run is printed word for word, a group's key included when the run wrote one: the
        // report's own words are what is held here.
        const auto withoutTheRunsMessages = [&report](QString text) {
            for (const ReportIssue& issue : report->model.issues)
            {
                if (issue.kind == ReportIssueKind::RunMessage)
                {
                    text.remove(issue.title);
                    text.remove(issue.detail);
                }
            }
            return text;
        };
        const std::vector<const DocumentElement*> logs = parsed.ofClass("log");

        // A placeholder is looked for run by run of the text: a table sets "1.3 %" in one cell and "1.4 %" in the
        // next, and the two read as one line would pass for one.
        QStringList pieces;
        for (const DocumentTextRun& run : parsed.runs)
        {
            const DocumentElement& owner = parsed.elements[static_cast<std::size_t>(run.element)];
            const bool logged = std::any_of(logs.begin(), logs.end(), [&](const DocumentElement* log) {
                return log == &owner || parsed.isInside(owner, *log);
            });
            if (!logged)
            {
                pieces << run.text;
            }
        }
        QStringList texts = {parsed.bodyText()};
        for (const DocumentElement& element : parsed.elements)
        {
            for (const char* attribute : {"data-tip", "aria-label", "alt", "title"})
            {
                if (element.has(attribute))
                {
                    texts << element.attribute(attribute);
                    pieces << element.attribute(attribute);
                }
            }
        }
        for (const QString& piece : pieces)
        {
            const QString text = withoutTheRunsMessages(piece);
            const QRegularExpressionMatch placeholder = kPlaceholder.match(text);
            EXPECT_FALSE(placeholder.hasMatch())
                << text.mid((std::max)(qsizetype(0), placeholder.capturedStart() - 60), 120).toStdString();
        }
        for (const QString& whole : texts)
        {
            QString text = withoutTheRunsMessages(whole);
            // The run's log is printed word for word too.
            for (const DocumentElement* log : logs)
            {
                text.remove(log->text);
            }
            const QRegularExpressionMatch notANumber = kNotANumber.match(text);
            EXPECT_FALSE(notANumber.hasMatch())
                << text.mid((std::max)(qsizetype(0), notANumber.capturedStart() - 60), 120).toStdString();
            EXPECT_FALSE(text.contains(QStringLiteral("|-|")));
            for (const ReportMaster& master : report->model.masters)
            {
                if (master.key.contains(QLatin1Char('|')))
                {
                    EXPECT_FALSE(text.contains(master.key)) << master.key.toStdString();
                }
            }
            for (const ReportRegistrationGroup& group : report->model.registrationGroups)
            {
                if (group.key.contains(QLatin1Char('|')))
                {
                    EXPECT_FALSE(text.contains(group.key)) << group.key.toStdString();
                }
            }
        }
    }
}

TEST_F(ReportHtmlWriterTest, TheSameModelIsTheSameDocument)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    const ReportDocumentOptions options{.form = ReportForm::Linked, .runDirectory = report->scenario.runDirectory};
    EXPECT_EQ(writeReportDocument(report->model, options).html, report->linked.html);
}

// ---- The two forms.

TEST_F(ReportHtmlWriterTest, TheLinkedFormNamesPicturesThatAreInTheRunsFolder)
{
    std::size_t pictures = 0;
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const QDir run(report->scenario.runDirectory);
        for (const DocumentElement* picture : report->linkedParsed.named("img"))
        {
            ++pictures;
            const QString source = picture->attribute("src");
            EXPECT_TRUE(QDir::isRelativePath(source)) << source.toStdString();
            EXPECT_FALSE(source.contains(QLatin1Char('\\'))) << source.toStdString();
            EXPECT_FALSE(source.startsWith(QStringLiteral("data:"))) << "a picture inside a linked report";
            EXPECT_TRUE(QFileInfo::exists(run.filePath(source))) << source.toStdString();
        }
    }
    EXPECT_GT(pictures, 0u);
}

TEST_F(ReportHtmlWriterTest, TheEmbeddedFormReachesForNothingOutsideItself)
{
    std::size_t pictures = 0;
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const ParsedDocument& parsed = report->embeddedParsed;
        for (const DocumentElement& element : parsed.elements)
        {
            if (element.has("src"))
            {
                EXPECT_EQ(element.name, QStringLiteral("img"));
                EXPECT_TRUE(element.attribute("src").startsWith(QStringLiteral("data:image/jpeg;base64,")))
                    << element.attribute("src").left(60).toStdString();
            }
            if (element.has("href"))
            {
                EXPECT_TRUE(element.attribute("href").startsWith(QLatin1Char('#')))
                    << element.attribute("href").toStdString();
            }
        }
        EXPECT_FALSE(report->embedded.html.contains(QStringLiteral("report_previews/")));
        // It shows the pictures the linked form shows, but those of the runs that made a master before the latest:
        // every earlier run's pictures inside one file would make it several times its size.
        std::size_t ofRunsBeforeTheLatest = 0;
        for (const DocumentElement* shown : report->linkedParsed.ofClass("cmprun"))
        {
            if (!shown->hasClass("cmprun-0"))
            {
                ofRunsBeforeTheLatest += namedInside(report->linkedParsed, *shown, "img").size();
            }
        }
        EXPECT_EQ(parsed.named("img").size() + ofRunsBeforeTheLatest, report->linkedParsed.named("img").size());
        pictures += parsed.named("img").size();
    }
    EXPECT_GT(pictures, 0u);

    // A picture inside the document is one a browser can read.
    const Rendered* five = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(five, nullptr);
    const std::vector<const DocumentElement*> embedded = five->embeddedParsed.named("img");
    ASSERT_FALSE(embedded.empty());
    const QByteArray bytes = QByteArray::fromBase64(
        embedded.front()->attribute("src").mid(QStringLiteral("data:image/jpeg;base64,").size()).toLatin1());
    QImage decoded;
    EXPECT_TRUE(decoded.loadFromData(bytes, "jpeg"));
    EXPECT_FALSE(decoded.isNull());
}

TEST_F(ReportHtmlWriterTest, TheEmbeddedFormPrintsNoFolder)
{
    for (const ReportScenarioId id : reportedScenarios())
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const QString output = QDir::fromNativeSeparators(report->scenario.outputDirectory);
        const QString parent = QFileInfo(output).absolutePath();
        for (const QString& folder : {output, parent})
        {
            EXPECT_FALSE(report->embedded.html.contains(folder, Qt::CaseInsensitive)) << folder.toStdString();
            EXPECT_FALSE(report->embedded.html.contains(QDir::toNativeSeparators(folder), Qt::CaseInsensitive))
                << folder.toStdString();
        }
    }

    // A message of the run that names the output folder and a light's file states the folder's name and the
    // file's, in either spelling of the separator.
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const QString output = QDir::fromNativeSeparators(model.run.outputFolder);
    ASSERT_FALSE(output.isEmpty());
    const ReportMaster* luminance = masterOfFilter(model, QStringLiteral("Luminance"));
    ASSERT_NE(luminance, nullptr);
    ASSERT_FALSE(luminance->frames.empty());
    const QString light = QDir::fromNativeSeparators(luminance->frames.front().path);
    ASSERT_TRUE(light.contains(QLatin1Char('/')));
    const QString lightName = QFileInfo(light).fileName();
    model.issues.push_back(
        ReportIssue{.kind = ReportIssueKind::RunMessage,
                    .severity = ReportIssueSeverity::Warning,
                    .title = QStringLiteral("Could not write into %1").arg(QDir::toNativeSeparators(output)),
                    .detail = QStringLiteral("Skipping %1 and %2/master.fit").arg(light, output),
                    .action = QStringLiteral("Look at %1").arg(QDir::toNativeSeparators(light)),
                    .groupKey = {},
                    .anchor = {}});

    const ReportDocument embedded =
        writeReportDocument(model, {.form = ReportForm::Embedded, .runDirectory = report->scenario.runDirectory});
    const ParsedDocument parsed = parseDocument(embedded.html);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    EXPECT_FALSE(embedded.html.contains(output, Qt::CaseInsensitive));
    EXPECT_FALSE(embedded.html.contains(QDir::toNativeSeparators(output), Qt::CaseInsensitive));
    EXPECT_FALSE(embedded.html.contains(QFileInfo(light).absolutePath(), Qt::CaseInsensitive));
    const QString outputName = QFileInfo(output).fileName();
    const QString text = parsed.bodyText();
    EXPECT_TRUE(text.contains(QStringLiteral("Could not write into %1").arg(outputName))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("Skipping %1 and %2/master.fit").arg(lightName, outputName)));
    EXPECT_TRUE(text.contains(QStringLiteral("Look at %1").arg(lightName)));

    // The linked form, which stays on the machine that wrote it, prints the message as the run wrote it.
    const ReportDocument linked =
        writeReportDocument(model, {.form = ReportForm::Linked, .runDirectory = report->scenario.runDirectory});
    EXPECT_TRUE(parseDocument(linked.html).bodyText().contains(QStringLiteral("Skipping %1").arg(light)));
}

TEST_F(ReportHtmlWriterTest, TheDocumentsStayWithinTheirSizes)
{
    for (const ReportScenarioId id : {ReportScenarioId::FiveFilters, ReportScenarioId::TwoThousandLights})
    {
        SCOPED_TRACE(scenarioName(id));
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr);
        const qsizetype linked = report->linked.html.toUtf8().size();
        const qsizetype embedded = report->embedded.html.toUtf8().size();
        const qsizetype lights = report->model.run.frames;
        const qsizetype masters = static_cast<qsizetype>(report->model.masters.size());
        // The linked form: 400 KB and 2.5 KB a light. The embedded one: that and 1 MB a master.
        EXPECT_LE(linked, 400 * 1024 + lights * 2560) << linked;
        EXPECT_LE(embedded, linked + masters * 1024 * 1024) << embedded;
    }
}

// ---- What a run wrote comes out as text.

TEST_F(ReportHtmlWriterTest, HostileTextFromARunComesOutAsText)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const QString hostile = hostileText();
    const QString read = hostileTextRead();

    // A header's value, a file's name and a message of the run, each carrying markup, a CDATA end, a script end, a
    // comment start, quotes and a control character.
    model.run.targets = {hostile};
    model.run.sessionNotes = hostile;
    model.run.verdict = hostile;
    model.run.process = hostile;
    ASSERT_FALSE(model.masters.empty());
    model.masters.front().name = hostile;
    model.masters.front().filter = hostile;
    model.masters.front().fileName = hostile;
    ASSERT_FALSE(model.masters.front().frames.empty());
    model.masters.front().frames.front().fileName = hostile;
    model.issues.push_back(ReportIssue{.kind = ReportIssueKind::RunMessage,
                                       .severity = ReportIssueSeverity::Warning,
                                       .title = hostile,
                                       .detail = hostile,
                                       .action = hostile,
                                       .groupKey = {},
                                       .anchor = {}});

    for (const ReportForm form : {ReportForm::Linked, ReportForm::Embedded})
    {
        SCOPED_TRACE(form == ReportForm::Linked ? "linked" : "embedded");
        const ReportDocument document =
            writeReportDocument(model, {.form = form, .runDirectory = report->scenario.runDirectory});
        EXPECT_TRUE(document.faults.isEmpty()) << document.faults.join(QLatin1Char('\n')).toStdString();
        const ParsedDocument parsed = parseDocument(document.html);
        ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();

        // None of it became markup: the document has the elements it had.
        EXPECT_EQ(parsed.named("script").size(), 1u);
        EXPECT_EQ(parsed.named("style").size(), 1u);
        EXPECT_FALSE(document.html.contains(QStringLiteral("M <b> &")));
        EXPECT_FALSE(document.html.contains(QChar(0x0001)));
        EXPECT_TRUE(selfClosingFaults(document.html).isEmpty());

        // And each place reads back as what was written, the control character replaced.
        const std::vector<const DocumentElement*> headings = parsed.named("h1");
        ASSERT_EQ(headings.size(), 1u);
        EXPECT_EQ(headings.front()->text, read);
        const std::vector<const DocumentElement*> titles = parsed.named("title");
        ASSERT_EQ(titles.size(), 1u);
        EXPECT_TRUE(titles.front()->text.startsWith(read));
        const std::vector<const DocumentElement*> notes = parsed.ofClass("notes");
        ASSERT_EQ(notes.size(), 1u);
        EXPECT_TRUE(notes.front()->text.contains(read));
        const std::vector<const DocumentElement*> verdicts = parsed.ofClass("verdict");
        ASSERT_EQ(verdicts.size(), 1u);
        EXPECT_TRUE(verdicts.front()->text.startsWith(read));

        const std::vector<const DocumentElement*> issues = parsed.ofClass("issue");
        ASSERT_FALSE(issues.empty());
        const DocumentElement* title = firstInside(parsed, *issues.back(), "b");
        ASSERT_NE(title, nullptr);
        EXPECT_EQ(title->text, read);
        EXPECT_EQ(issues.back()->text.count(read), 3);

        // An attribute holds it too: the picture's words, and a light's tip.
        bool inAlt = false;
        bool inTip = false;
        for (const DocumentElement& element : parsed.elements)
        {
            inAlt = inAlt || element.attribute("alt").contains(read);
            inTip = inTip || element.attribute("data-tip").contains(read);
        }
        EXPECT_TRUE(inAlt);
        EXPECT_TRUE(inTip);
    }
}

// ---- The style and the script.

TEST(ReportHtmlAssets, TheDocumentCarriesTheStyleAndTheScriptAsTheyAre)
{
    const QString stylesheet = reportStylesheet();
    const QString script = reportScript();
    ASSERT_FALSE(stylesheet.isEmpty()) << "the report's stylesheet is not in the library's resources";
    ASSERT_FALSE(script.isEmpty()) << "the report's script is not in the library's resources";
    EXPECT_FALSE(stylesheet.contains(QLatin1Char('\r')));
    EXPECT_FALSE(script.contains(QLatin1Char('\r')));

    const ReportDocument document = writeReportDocument(ReportModel{}, {});
    const ParsedDocument parsed = parseDocument(document.html);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    const std::vector<const DocumentElement*> styles = parsed.named("style");
    const std::vector<const DocumentElement*> scripts = parsed.named("script");
    ASSERT_EQ(styles.size(), 1u);
    ASSERT_EQ(scripts.size(), 1u);
    // Each is the asset, whole, between the comment marks that hide its CDATA section from the other parser.
    EXPECT_EQ(styles.front()->text, QStringLiteral("/**/\n") + stylesheet + QStringLiteral("/**/"));
    EXPECT_EQ(scripts.front()->text, QStringLiteral("//\n") + script + QStringLiteral("//"));
    EXPECT_FALSE(scripts.front()->has("src"));
    EXPECT_TRUE(parsed.named("link").empty());
}

TEST(ReportHtmlAssets, NeitherAssetCanEndItsElementOrItsCdataSection)
{
    for (const QString& asset : {reportStylesheet(), reportScript()})
    {
        EXPECT_FALSE(asset.contains(QStringLiteral("]]>")));
        EXPECT_FALSE(asset.contains(QStringLiteral("</script"), Qt::CaseInsensitive));
        EXPECT_FALSE(asset.contains(QStringLiteral("</style"), Qt::CaseInsensitive));
        EXPECT_FALSE(asset.contains(QStringLiteral("<!--")));
    }
}

TEST(ReportHtmlAssets, TheScriptNamesNoNetworkAndRunsNoText)
{
    const QString script = reportScript();
    ASSERT_FALSE(script.isEmpty());
    for (const char* forbidden : {"fetch", "XMLHttpRequest", "import", "eval", "http:", "https:", "WebSocket",
                                  "sendBeacon", "Function(", "innerHTML", "document.write"})
    {
        EXPECT_FALSE(script.contains(QLatin1String(forbidden))) << forbidden;
    }
}

TEST(ReportHtmlAssets, TheStylesheetLoadsNothing)
{
    const QString stylesheet = reportStylesheet();
    ASSERT_FALSE(stylesheet.isEmpty());
    for (const char* forbidden : {"url(", "@import", "http:", "https:", "@font-face"})
    {
        EXPECT_FALSE(stylesheet.contains(QLatin1String(forbidden), Qt::CaseInsensitive)) << forbidden;
    }
}

TEST(ReportHtmlAssets, TheTokensAreTheUserGuides)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty()) << "Could not locate repository root";
    const QString guide =
        readTextFile(QDir(root).filePath(QStringLiteral("docs/user-guide/theme/assets/guide/css/tokens.css")));
    ASSERT_FALSE(guide.isEmpty()) << "the user guide's tokens are not where the report takes them from";

    const std::vector<CssRule> guideRules = cssRules(guide);
    const std::vector<CssRule> reportRules = cssRules(reportStylesheet());
    const std::map<QString, QString> guideDark = tokensOf(guideRules, QStringLiteral(":root"));
    const std::map<QString, QString> guideLight = tokensOf(guideRules, QStringLiteral(":root[data-theme=\"light\"]"));
    ASSERT_FALSE(guideDark.empty());
    ASSERT_FALSE(guideLight.empty());

    const std::map<QString, QString> dark = tokensOf(reportRules, QStringLiteral(":root, :root[data-theme=\"dark\"]"));
    const std::map<QString, QString> light = tokensOf(reportRules, QStringLiteral(":root[data-theme=\"light\"]"));
    const std::map<QString, QString> preferred = tokensOf(
        reportRules, QStringLiteral("@media (prefers-color-scheme: light) | :root:not([data-theme=\"dark\"])"));
    const std::map<QString, QString> printed =
        tokensOf(reportRules, QStringLiteral("@media print | html:root, html:root[data-theme]"));
    const std::map<QString, QString> shape = tokensOf(reportRules, QStringLiteral(":root"));
    ASSERT_FALSE(dark.empty());
    ASSERT_FALSE(light.empty());
    ASSERT_FALSE(shape.empty());

    // Every token the two files share has one value, theme by theme.
    const auto held = [](const std::map<QString, QString>& report, const std::map<QString, QString>& toGuide) {
        int shared = 0;
        for (const auto& [name, value] : report)
        {
            const auto guideValue = toGuide.find(name);
            if (guideValue == toGuide.end())
            {
                continue;
            }
            ++shared;
            EXPECT_EQ(normalizedValue(value).toStdString(), normalizedValue(guideValue->second).toStdString())
                << name.toStdString();
        }
        return shared;
    };
    EXPECT_GE(held(dark, guideDark), 15);
    EXPECT_GE(held(light, guideLight), 15);
    EXPECT_GE(held(shape, guideDark), 3);

    // The light values are stated three times - for a reader who prefers light, for one who chose it, and for
    // print - and the three are one. The two themes define the same tokens.
    EXPECT_EQ(preferred, light);
    EXPECT_EQ(printed, light);
    ASSERT_EQ(dark.size(), light.size());
    for (const auto& [name, value] : dark)
    {
        EXPECT_TRUE(light.contains(name)) << name.toStdString();
    }
}

TEST(ReportHtmlAssets, TheColoursKeepTheirContrastInBothThemes)
{
    const std::vector<CssRule> rules = cssRules(reportStylesheet());
    const std::map<QString, std::map<QString, QString>> themes = {
        {QStringLiteral("dark"), tokensOf(rules, QStringLiteral(":root, :root[data-theme=\"dark\"]"))},
        {QStringLiteral("light"), tokensOf(rules, QStringLiteral(":root[data-theme=\"light\"]"))}};
    for (const auto& [theme, tokens] : themes)
    {
        const auto token = [&tokens](const char* name) {
            const auto found = tokens.find(QLatin1String(name));
            EXPECT_NE(found, tokens.end()) << name;
            return found == tokens.end() ? QString() : found->second;
        };
        // What a chart sits on: a card, and the page a master's inner panels take.
        for (const char* surface : {"--card", "--bg"})
        {
            SCOPED_TRACE(testing::Message() << theme.toStdString() << " on " << surface);
            // What a mark is drawn in: the two series, the status colours, the grey of a light the user left out.
            for (const char* mark : {"--series", "--series-2", "--success", "--warning", "--error", "--text-3"})
            {
                EXPECT_GE(contrast(token(mark), token(surface)), 3.0) << mark;
            }
        }
        // What text is written in, on every surface text stands on: those two and a tile.
        for (const char* surface : {"--card", "--bg", "--card-2"})
        {
            SCOPED_TRACE(testing::Message() << theme.toStdString() << " on " << surface);
            for (const char* ink : {"--text", "--text-2", "--text-3", "--accent"})
            {
                EXPECT_GE(contrast(token(ink), token(surface)), 4.5) << ink;
            }
        }
        // The words on the accent, as the measure a chart shows is picked.
        EXPECT_GE(contrast(token("--on-accent"), token("--accent")), 4.5) << theme.toStdString();
    }
}

TEST_F(ReportHtmlWriterTest, TheStylesheetStylesEveryClassTheDocumentAndTheChartsUse)
{
    const QString stylesheet = reportStylesheet();
    static const QRegularExpression kClasses(QStringLiteral("class=\"([^\"]+)\""));
    QStringList markups;
    for (const ReportScenarioId id : reportedScenarios())
    {
        const Rendered* report = reportOf(id);
        ASSERT_NE(report, nullptr) << scenarioName(id);
        markups << report->linked.html;
    }
    for (const ReportChartSamples::SampleChart& chart : ReportChartSamples::everySampleChart())
    {
        markups << chart.markup << chart.legend;
    }

    QSet<QString> used;
    for (const QString& markup : markups)
    {
        QRegularExpressionMatchIterator matches = kClasses.globalMatch(markup);
        while (matches.hasNext())
        {
            for (const QString& name : matches.next().captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            {
                used.insert(name);
            }
        }
    }
    ASSERT_GT(used.size(), 40);

    QStringList unstyled;
    for (const QString& name : used)
    {
        // A chart's kind and a mark's shape name what an element is; they need no rule of their own.
        if (name.startsWith(QStringLiteral("chart-")) || name.startsWith(QStringLiteral("c-shape-")) ||
            name == QStringLiteral("is-empty"))
        {
            continue;
        }
        const QRegularExpression rule(QStringLiteral("\\.%1(?![-\\w])").arg(QRegularExpression::escape(name)));
        if (!rule.match(stylesheet).hasMatch())
        {
            unstyled << name;
        }
    }
    unstyled.sort();
    EXPECT_TRUE(unstyled.isEmpty()) << unstyled.join(QLatin1Char(' ')).toStdString();
}

// ---- The pictures and the paths of each form.

TEST(ReportAssets, APictureInsideADocumentIsBoundedAndWrittenAgainAsAJpeg)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QDir directory(folder.path());
    ASSERT_TRUE(directory.mkpath(QStringLiteral("report_previews/group")));

    QImage large(3000, 2000, QImage::Format_RGB32);
    large.fill(QColor(40, 60, 90));
    const QString largePath = directory.filePath(QStringLiteral("report_previews/group/master.jpg"));
    ASSERT_TRUE(large.save(largePath, "JPG", 90));
    QImage small(64, 48, QImage::Format_RGB32);
    small.fill(QColor(200, 30, 30));
    const QString smallPath = directory.filePath(QStringLiteral("report_previews/group/detail.png"));
    ASSERT_TRUE(small.save(smallPath, "PNG"));

    const auto decoded = [](const QByteArray& jpeg) {
        QBuffer buffer;
        buffer.setData(jpeg);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QByteArray format = reader.format();
        const QImage image = reader.read();
        return std::pair<QByteArray, QSize>(format, image.size());
    };

    // A large picture is reduced to the bound on its longer side, its shape kept.
    const auto [largeFormat, largeSize] = decoded(boundedJpeg(largePath, kEmbeddedImageLongestSide, 82));
    EXPECT_EQ(largeFormat, QByteArray("jpeg"));
    EXPECT_EQ(largeSize, QSize(1280, 853));

    // A small one keeps its size, and is a JPEG whatever it was.
    const auto [smallFormat, smallSize] = decoded(boundedJpeg(smallPath, kEmbeddedImageLongestSide, 82));
    EXPECT_EQ(smallFormat, QByteArray("jpeg"));
    EXPECT_EQ(smallSize, QSize(64, 48));

    // What is not a picture, or is not there, is nothing.
    const QString notAPicture = directory.filePath(QStringLiteral("notes.txt"));
    QFile text(notAPicture);
    ASSERT_TRUE(text.open(QIODevice::WriteOnly));
    text.write("not a picture");
    text.close();
    EXPECT_TRUE(boundedJpeg(notAPicture, kEmbeddedImageLongestSide, 82).isEmpty());
    EXPECT_TRUE(boundedJpeg(directory.filePath(QStringLiteral("absent.jpg")), 1280, 82).isEmpty());

    // The linked form names the file; the embedded one holds it, and names nothing it cannot read.
    ReportAssets linked(ReportForm::Linked, folder.path());
    EXPECT_EQ(linked.image(QStringLiteral("report_previews\\group\\master.jpg")),
              QStringLiteral("report_previews/group/master.jpg"));
    EXPECT_TRUE(linked.image(QString()).isEmpty());

    ReportAssets embedded(ReportForm::Embedded, folder.path());
    const QString source = embedded.image(QStringLiteral("report_previews/group/master.jpg"));
    ASSERT_TRUE(source.startsWith(QStringLiteral("data:image/jpeg;base64,")));
    EXPECT_EQ(decoded(QByteArray::fromBase64(source.mid(23).toLatin1())).second, QSize(1280, 853));
    EXPECT_EQ(embedded.image(QStringLiteral("report_previews/group/master.jpg")), source);
    EXPECT_TRUE(embedded.image(QStringLiteral("report_previews/group/absent.jpg")).isEmpty());
    EXPECT_TRUE(embedded.image(QStringLiteral("notes.txt")).isEmpty());
    EXPECT_TRUE(ReportAssets(ReportForm::Embedded, QString()).image(QStringLiteral("master.jpg")).isEmpty());

    // A picture shown small is the same file in a linked document, and a smaller picture inside an embedded one, so
    // a document that shows a picture small and whole does not carry it twice at its whole size.
    EXPECT_EQ(linked.thumbnail(QStringLiteral("report_previews/group/master.jpg")),
              QStringLiteral("report_previews/group/master.jpg"));
    const QString small480 = embedded.thumbnail(QStringLiteral("report_previews/group/master.jpg"));
    ASSERT_TRUE(small480.startsWith(QStringLiteral("data:image/jpeg;base64,")));
    EXPECT_EQ(decoded(QByteArray::fromBase64(small480.mid(23).toLatin1())).second,
              QSize(kEmbeddedThumbnailLongestSide, 320));
    EXPECT_LT(small480.size(), source.size());
    EXPECT_EQ(embedded.image(QStringLiteral("report_previews/group/master.jpg")), source);
    EXPECT_TRUE(embedded.thumbnail(QStringLiteral("report_previews/group/absent.jpg")).isEmpty());
    EXPECT_TRUE(embedded.thumbnail(QString()).isEmpty());
}

TEST(ReportAssets, AnEmbeddedDocumentPrintsAFilesNameWithoutItsFolders)
{
    const ReportAssets linked(ReportForm::Linked, QString());
    EXPECT_EQ(linked.path(QStringLiteral("C:\\Astro\\M 20\\master_L.fit")),
              QStringLiteral("C:\\Astro\\M 20\\master_L.fit"));

    const ReportAssets embedded(ReportForm::Embedded, QString());
    EXPECT_EQ(embedded.path(QStringLiteral("C:\\Astro\\M 20\\master_L.fit")), QStringLiteral("master_L.fit"));
    EXPECT_EQ(embedded.path(QStringLiteral("/home/user/astro/master_L.fit")), QStringLiteral("master_L.fit"));
    EXPECT_EQ(embedded.path(QStringLiteral("master_L.fit")), QStringLiteral("master_L.fit"));
}

// ---- The report as a file.

TEST_F(ReportHtmlWriterTest, TheLinkedReportIsWrittenIntoTheRunsFolder)
{
    const Rendered* report = reportOf(ReportScenarioId::OneColourMaster);
    ASSERT_NE(report, nullptr);
    const QString run = report->scenario.runDirectory;

    const WorkflowReportWritten written = writeWorkflowReport(report->model, run, ReportForm::Linked);
    ASSERT_TRUE(written.isWritten()) << written.error.toStdString();
    EXPECT_TRUE(written.faults.isEmpty());
    EXPECT_EQ(QFileInfo(written.path).absoluteFilePath(),
              QFileInfo(QDir(run).filePath(QStringLiteral("workflow_report.html"))).absoluteFilePath());
    EXPECT_EQ(readTextFile(written.path), report->linked.html);

    // Without the run's folder there is nowhere to write it, and the failure says so.
    const WorkflowReportWritten nowhere =
        writeWorkflowReport(report->model, QDir(run).filePath(QStringLiteral("no-such-run")), ReportForm::Linked);
    EXPECT_FALSE(nowhere.isWritten());
    EXPECT_TRUE(nowhere.path.isEmpty());
    EXPECT_TRUE(nowhere.error.contains(QStringLiteral("no-such-run"))) << nowhere.error.toStdString();
}

TEST_F(ReportHtmlWriterTest, TheEmbeddedReportOpensAloneFromAnEmptyFolder)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    QTemporaryDir elsewhere;
    ASSERT_TRUE(elsewhere.isValid());
    const QString destination = QDir(elsewhere.path()).filePath(QStringLiteral("shared/M 20 report.html"));

    const WorkflowReportWritten written =
        writeWorkflowReport(report->model, report->scenario.runDirectory, ReportForm::Embedded, destination);
    ASSERT_TRUE(written.isWritten()) << written.error.toStdString();
    EXPECT_EQ(written.path, destination);

    // The file is the only thing in its folder, and it names nothing beside itself.
    EXPECT_EQ(QFileInfo(destination).absoluteDir().entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot),
              QStringList{QStringLiteral("M 20 report.html")});
    const QString html = readTextFile(destination);
    EXPECT_EQ(html, report->embedded.html);
    const ParsedDocument parsed = parseDocument(html);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    EXPECT_FALSE(parsed.named("img").empty());
    for (const DocumentElement& element : parsed.elements)
    {
        if (element.has("src"))
        {
            EXPECT_TRUE(element.attribute("src").startsWith(QStringLiteral("data:")));
        }
    }

    // An embedded report needs a file named to write it to.
    const WorkflowReportWritten unnamed =
        writeWorkflowReport(report->model, report->scenario.runDirectory, ReportForm::Embedded);
    EXPECT_FALSE(unnamed.isWritten());
    EXPECT_FALSE(unnamed.error.isEmpty());
}

// A stage that went on after part of its work failed - a light with no transform - did not stop the run, and a run
// that made every master is not led by what was kept and what was not. The timeline said the run stopped in that
// stage.
TEST_F(ReportHtmlWriterTest, ARunWhoseStageWentOnAfterAFailureIsNotSaidToHaveStopped)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const auto ran = std::find_if(model.stages.begin(), model.stages.end(),
                                  [](const ReportStage& stage) { return stage.state == ReportStageState::Ran; });
    ASSERT_NE(ran, model.stages.end());
    ran->state = ReportStageState::Partial;
    model.run.outcome = ReportOutcome::CompletedWithWarnings;

    const ParsedDocument parsed = parseDocument(writeReportDocument(model, {}).html);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    EXPECT_TRUE(parsed.withId(QStringLiteral("outcome")).empty());
    const DocumentElement* timeline = parsed.byId("timeline");
    ASSERT_NE(timeline, nullptr);
    EXPECT_TRUE(timeline->text.contains(QStringLiteral("Part of %1 failed, and the run went on.").arg(ran->name)))
        << timeline->text.toStdString();
    EXPECT_FALSE(timeline->text.contains(QStringLiteral("The run stopped in")));
}

// What a run wrote may name folders none of the report's files is in: a catalog under the person's profile, a
// solver's temporary file. The single file is meant to be sent, so it names none of them: a profile goes whole, and
// of any other path its last part stays. The linked report is the run's own, on the machine it ran on, and keeps
// the run's words.
TEST_F(ReportHtmlWriterTest, TheSingleFileNamesNoFolderOfTheMachineInWhatTheRunWrote)
{
    const Rendered* report = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    const QString catalog =
        QStringLiteral("Gaia enrichment is on, but no Gaia catalog is installed at "
                       "'C:/Users/John Smith/AppData/Local/AfterNight/catalogs/gaia-dr3': 3 solved light(s) were "
                       "registered without the catalog's photometric figures.");
    const QString solver =
        QStringLiteral("The fallback solve of C:\\Users\\jane\\AppData\\Local\\Temp\\solve_1\\field.fits failed.");
    const QString elsewhere = QStringLiteral("Read /home/ana/astro/flat_master.fits and D:\\Catalogs\\ucac4\\z001");
    const QString prose = QStringLiteral("Stage: stacking 3/4 done (I/O 12 ms), and/or nothing");
    model.run.log = QStringList{catalog, solver, elsewhere, prose}.join(QLatin1Char('\n'));
    model.issues.push_back(ReportIssue{.kind = ReportIssueKind::RunMessage,
                                       .severity = ReportIssueSeverity::Warning,
                                       .title = catalog,
                                       .detail = {},
                                       .action = {},
                                       .groupKey = {},
                                       .anchor = {}});

    const QString embedded =
        writeReportDocument(model, {.form = ReportForm::Embedded, .runDirectory = report->scenario.runDirectory}).html;
    for (const QString& told :
         {QStringLiteral("John Smith"), QStringLiteral("jane"), QStringLiteral("/home/ana"), QStringLiteral("AppData"),
          QStringLiteral("C:/Users"), QStringLiteral("C:\\Users"), QStringLiteral("D:\\Catalogs")})
    {
        EXPECT_FALSE(embedded.contains(told)) << told.toStdString();
    }
    for (const QString& kept : {QStringLiteral("gaia-dr3"), QStringLiteral("field.fits failed."),
                                QStringLiteral("flat_master.fits"), QStringLiteral("z001"), prose})
    {
        EXPECT_TRUE(embedded.contains(kept)) << kept.toStdString();
    }

    const QString linked =
        writeReportDocument(model, {.form = ReportForm::Linked, .runDirectory = report->scenario.runDirectory}).html;
    EXPECT_TRUE(linked.contains(QStringLiteral("C:/Users/John Smith/AppData/Local/AfterNight/catalogs/gaia-dr3")));
}

// A file in a drive's root has no folder to drop from the single file's words. The drive alone was taken for one,
// and the same two letters went out of every word that ends in them before a colon: "Stage:" read "Stag".
TEST_F(ReportHtmlWriterTest, AFileInADrivesRootTakesNothingOutOfTheSingleFilesWords)
{
    const Rendered* report = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    ASSERT_FALSE(model.masters.empty());
    model.masters.front().file = QStringLiteral("E:/stacked_master.fits");
    model.run.log = QStringLiteral("Stage: LightsStacking\nGroup failure: none\nWrote E:/stacked_master.fits");

    const QString embedded =
        writeReportDocument(model, {.form = ReportForm::Embedded, .runDirectory = report->scenario.runDirectory}).html;
    EXPECT_TRUE(embedded.contains(QStringLiteral("Stage: LightsStacking")));
    EXPECT_TRUE(embedded.contains(QStringLiteral("Group failure: none")));
    EXPECT_TRUE(embedded.contains(QStringLiteral("Wrote stacked_master.fits")));
}

// A group's folder is named after its target, and a target may hold a character a browser does not read as part of
// a path: after "#" it reads a fragment. The linked report escapes it, so the picture's address names the file.
TEST_F(ReportHtmlWriterTest, APictureInAFolderNamedWithAHashIsLinkedByAPathABrowserReadsWhole)
{
    const Rendered* report = reportOf(ReportScenarioId::OneMonoMaster);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    ASSERT_FALSE(model.masters.empty());
    model.masters.front().preview = QStringLiteral("report_previews/Panel_#2_50%/master.jpg");

    const ParsedDocument parsed = parseDocument(writeReportDocument(model, {}).html);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    int pictures = 0;
    for (const DocumentElement& element : parsed.elements)
    {
        if (element.has("src") && element.attribute("src").contains(QStringLiteral("Panel_")))
        {
            ++pictures;
            EXPECT_EQ(element.attribute("src"), QStringLiteral("report_previews/Panel_%232_50%25/master.jpg"));
        }
    }
    EXPECT_GT(pictures, 0);
}

// The exposure a master's lights add up to is a sum only when every light of it states one. A master whose lights
// state none printed "0 ms" beside their count.
TEST_F(ReportHtmlWriterTest, AMasterWhoseLightsStateNoExposureStatesNoIntegrationBesideItsLights)
{
    const Rendered* report = reportOf(ReportScenarioId::FiveFilters);
    ASSERT_NE(report, nullptr);
    ReportModel model = report->model;
    for (ReportMaster& master : model.masters)
    {
        for (ReportFrame& frame : master.frames)
        {
            frame.exposureSeconds.reset();
        }
    }

    const ParsedDocument parsed = parseDocument(writeReportDocument(model, {}).html);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    const std::vector<const DocumentElement*> lines = parsed.ofClass("fline");
    ASSERT_FALSE(lines.empty());
    bool counted = false;
    for (const DocumentElement* line : lines)
    {
        counted = counted || line->text.contains(QStringLiteral(" lights"));
        EXPECT_FALSE(line->text.contains(QStringLiteral("0 ms"))) << line->text.toStdString();
    }
    EXPECT_TRUE(counted) << "a master's line still counts its lights";
    for (const DocumentElement* card : parsed.ofClass("mcard"))
    {
        EXPECT_FALSE(card->text.contains(QStringLiteral("0 ms"))) << card->text.toStdString();
    }
}

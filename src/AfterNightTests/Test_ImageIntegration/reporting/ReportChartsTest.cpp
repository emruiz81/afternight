// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The charts of the integration report, each held by parsing the SVG it returns: a mark per value, a cross per
/// light left out, a tip per mark, round ticks that span the data, and a stated chart when there is nothing to draw.

#include <gtest/gtest.h>

#include "ReportChartSamples.h"

#include "processes/ImageIntegration/reporting/ReportCharts.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using namespace ImageIntegration::Reporting;
using namespace ReportChartSamples;

namespace
{
constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();

/// One element of a parsed chart.
struct Element
{
    QString name;
    QXmlStreamAttributes attributes;

    /// The text directly inside it.
    QString text;

    [[nodiscard]] QString attribute(const char* key) const
    {
        return attributes.value(QLatin1String(key)).toString();
    }
    [[nodiscard]] bool has(const char* key) const
    {
        return attributes.hasAttribute(QLatin1String(key));
    }
    [[nodiscard]] double number(const char* key) const
    {
        return attribute(key).toDouble();
    }
    [[nodiscard]] bool hasClass(const char* wanted) const
    {
        return attribute("class").split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(QLatin1String(wanted));
    }
};

/// A chart's markup as its elements, in document order.
struct Parsed
{
    std::vector<Element> elements;
    QString error;

    [[nodiscard]] std::vector<Element> ofClass(const char* wanted) const
    {
        std::vector<Element> found;
        for (const Element& element : elements)
        {
            if (element.hasClass(wanted))
            {
                found.push_back(element);
            }
        }
        return found;
    }

    [[nodiscard]] std::size_t count(const char* wanted) const
    {
        return ofClass(wanted).size();
    }

    [[nodiscard]] const Element& root() const
    {
        return elements.front();
    }

    /// The texts of the elements of a class.
    [[nodiscard]] QStringList texts(const char* wanted) const
    {
        QStringList found;
        for (const Element& element : ofClass(wanted))
        {
            found << element.text;
        }
        return found;
    }
};

[[nodiscard]] Parsed parse(const QString& markup)
{
    Parsed parsed;
    QXmlStreamReader reader(markup);
    std::vector<std::size_t> open;
    while (!reader.atEnd())
    {
        switch (reader.readNext())
        {
        case QXmlStreamReader::StartElement: {
            Element element;
            element.name = reader.name().toString();
            element.attributes = reader.attributes();
            open.push_back(parsed.elements.size());
            parsed.elements.push_back(element);
            break;
        }
        case QXmlStreamReader::Characters:
            if (!open.empty())
            {
                parsed.elements[open.back()].text += reader.text().toString();
            }
            break;
        case QXmlStreamReader::EndElement:
            open.pop_back();
            break;
        default:
            break;
        }
    }
    if (reader.hasError())
    {
        parsed.error = QStringLiteral("%1 at line %2, column %3")
                           .arg(reader.errorString())
                           .arg(reader.lineNumber())
                           .arg(reader.columnNumber());
    }
    return parsed;
}

/// A chart's markup parsed, the case failing where it is not one well-formed SVG element.
[[nodiscard]] Parsed parsedChart(const QString& markup)
{
    Parsed parsed = parse(markup);
    EXPECT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    EXPECT_FALSE(parsed.elements.empty());
    if (!parsed.elements.empty())
    {
        EXPECT_EQ(parsed.root().name.toStdString(), "svg");
    }
    return parsed;
}

/// A number as an axis prints it, read back: the thin spaces between groups dropped, the minus sign made a hyphen.
[[nodiscard]] double tickValue(QString text)
{
    text.remove(QChar(0x2009));
    text.replace(QChar(0x2212), QLatin1Char('-'));
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? value : kNotANumber;
}

/// Whether some ticks are multiples of one step that is 1, 2, 2.5 or 5 times a power of ten.
[[nodiscard]] testing::AssertionResult ticksAreRound(const std::vector<double>& ticks)
{
    if (ticks.size() < 2)
    {
        return testing::AssertionFailure() << "fewer than two ticks";
    }
    const double step = ticks[1] - ticks[0];
    if (!(step > 0.0))
    {
        return testing::AssertionFailure() << "the ticks do not ascend";
    }
    const double mantissa = step / std::pow(10.0, std::floor(std::log10(step) + 1e-9));
    bool roundStep = false;
    for (const double allowed : {1.0, 2.0, 2.5, 5.0, 10.0})
    {
        roundStep = roundStep || std::abs(mantissa - allowed) < 1e-6;
    }
    if (!roundStep)
    {
        return testing::AssertionFailure() << "a step of " << step << " is not a round number";
    }
    for (std::size_t index = 0; index < ticks.size(); ++index)
    {
        const double multiple = ticks[index] / step;
        if (std::abs(multiple - std::round(multiple)) > 1e-6)
        {
            return testing::AssertionFailure() << ticks[index] << " is not a multiple of " << step;
        }
        if (index > 0 && std::abs((ticks[index] - ticks[index - 1]) - step) > step * 1e-6)
        {
            return testing::AssertionFailure() << "the ticks are not evenly spaced at " << ticks[index];
        }
    }
    return testing::AssertionSuccess();
}

/// How many separate lines a path draws: its moves.
[[nodiscard]] qsizetype subpaths(const Element& path)
{
    return path.attribute("d").count(QLatin1Char('M'));
}

/// Every chart of the sample page with the name it has there.
[[nodiscard]] std::vector<SampleChart> sampleCharts()
{
    static const std::vector<SampleChart> kCharts = everySampleChart();
    return kCharts;
}

/// Text that would break markup written without escaping, with a character XML forbids.
[[nodiscard]] QString hostileText()
{
    return QStringLiteral("L <b> & \"R\" 'G' ]]> </svg>") + QChar(0x0001) + QStringLiteral("end");
}

/// The hostile text as a reader must get it back: the forbidden character replaced.
[[nodiscard]] QString hostileTextRead()
{
    return QStringLiteral("L <b> & \"R\" 'G' ]]> </svg>") + QChar(0xFFFD) + QStringLiteral("end");
}

[[nodiscard]] ChartDescription described()
{
    return {QStringLiteral("A chart"), nothingToDraw()};
}

} // namespace

// ---- The figures a chart needs before it is drawn.

TEST(ReportCharts, TicksAreRoundAndSpanTheData)
{
    struct Span
    {
        double lowest;
        double highest;
        int steps;
    };
    const std::vector<Span> spans = {{2.31, 4.87, 4},     {0.0, 1.0, 4},   {0.0117, 0.0142, 2}, {84.0, 1017.0, 2},
                                     {-13.4, 11.9, 4},    {0.0, 37.0, 3},  {1200.0, 1560.0, 6}, {3.0e-5, 9.0e-5, 3},
                                     {-0.004, -0.001, 3}, {0.0, 2.0e7, 4}, {17.0, 17.5, 4},     {0.999, 1.001, 2}};
    for (const Span& span : spans)
    {
        const std::vector<double> ticks = chartTicks(span.lowest, span.highest, span.steps);
        SCOPED_TRACE(testing::Message() << span.lowest << " to " << span.highest);
        EXPECT_TRUE(ticksAreRound(ticks));
        ASSERT_GE(ticks.size(), 2u);
        EXPECT_LE(ticks.front(), span.lowest);
        EXPECT_GE(ticks.back(), span.highest);
        // The first tick is the last round number under the data, and the last the first over it.
        EXPECT_GT(ticks[1], span.lowest);
        EXPECT_LT(ticks[ticks.size() - 2], span.highest);
        EXPECT_LE(ticks.size(), static_cast<std::size_t>(span.steps) + 3u);
    }
}

TEST(ReportCharts, TicksOfEqualValuesLieAroundThem)
{
    for (const double value : {3.2, 0.0, -7.0, 1.0e-4, 5000.0})
    {
        const std::vector<double> ticks = chartTicks(value, value, 4);
        SCOPED_TRACE(value);
        EXPECT_TRUE(ticksAreRound(ticks));
        ASSERT_GE(ticks.size(), 2u);
        EXPECT_LE(ticks.front(), value);
        EXPECT_GE(ticks.back(), value);
        EXPECT_LT(ticks.front(), ticks.back());
    }
}

TEST(ReportCharts, TicksOfBoundsThatAreNotNumbersAreZeroAndOne)
{
    const std::vector<double> expected = {0.0, 1.0};
    EXPECT_EQ(chartTicks(kNotANumber, 4.0, 4), expected);
    EXPECT_EQ(chartTicks(1.0, std::numeric_limits<double>::infinity(), 4), expected);
    // Bounds given the wrong way round are the same range.
    EXPECT_EQ(chartTicks(4.87, 2.31, 4), chartTicks(2.31, 4.87, 4));
    // A count of steps that is not positive still gives a range.
    EXPECT_GE(chartTicks(0.0, 10.0, 0).size(), 2u);
}

TEST(ReportCharts, ARangeIsItsExtremesItsQuartilesAndItsMedian)
{
    const std::optional<ChartRange> odd = chartRange({9.0, 1.0, 5.0, 3.0, 7.0});
    ASSERT_TRUE(odd.has_value());
    EXPECT_DOUBLE_EQ(odd->lowest, 1.0);
    EXPECT_DOUBLE_EQ(odd->lowerQuartile, 3.0);
    EXPECT_DOUBLE_EQ(odd->median, 5.0);
    EXPECT_DOUBLE_EQ(odd->upperQuartile, 7.0);
    EXPECT_DOUBLE_EQ(odd->highest, 9.0);

    // An even count: the median is the mean of the two in the middle, a quartile lies between its neighbours.
    const std::optional<ChartRange> even = chartRange({4.0, 1.0, 3.0, 2.0});
    ASSERT_TRUE(even.has_value());
    EXPECT_DOUBLE_EQ(even->median, 2.5);
    EXPECT_DOUBLE_EQ(even->lowerQuartile, 1.75);
    EXPECT_DOUBLE_EQ(even->upperQuartile, 3.25);

    const std::optional<ChartRange> one = chartRange({2.5});
    ASSERT_TRUE(one.has_value());
    EXPECT_DOUBLE_EQ(one->lowest, 2.5);
    EXPECT_DOUBLE_EQ(one->median, 2.5);
    EXPECT_DOUBLE_EQ(one->highest, 2.5);

    // A value that is not a number is left out, and with no number there is no range.
    const std::optional<ChartRange> withGaps = chartRange({kNotANumber, 2.0, kNotANumber, 4.0});
    ASSERT_TRUE(withGaps.has_value());
    EXPECT_DOUBLE_EQ(withGaps->median, 3.0);
    EXPECT_FALSE(chartRange({}).has_value());
    EXPECT_FALSE(chartRange({kNotANumber, kNotANumber}).has_value());
}

TEST(ReportCharts, BinsCountEveryValueOnce)
{
    const std::vector<double> values = {0.0, 0.05, 0.1, 0.55, 0.99, 1.0, kNotANumber, 1.4, -0.2};
    const std::vector<ChartBin> bins = chartBins(values, 10, 0.0, 1.0);
    ASSERT_EQ(bins.size(), 10u);
    EXPECT_DOUBLE_EQ(bins.front().low, 0.0);
    EXPECT_DOUBLE_EQ(bins.back().high, 1.0);
    std::uint64_t counted = 0;
    for (std::size_t index = 0; index < bins.size(); ++index)
    {
        counted += bins[index].count;
        EXPECT_NEAR(bins[index].high - bins[index].low, 0.1, 1e-9);
        if (index > 0)
        {
            EXPECT_DOUBLE_EQ(bins[index].low, bins[index - 1].high);
        }
        EXPECT_TRUE(bins[index].tip.isEmpty());
    }
    // Every number is counted once; the one that is not a number in no bin.
    EXPECT_EQ(counted, 8u);
    // 0, 0.05 and the value under the range in the first bin; 0.1 on an inner edge in the bin it opens.
    EXPECT_EQ(bins[0].count, 3u);
    EXPECT_EQ(bins[1].count, 1u);
    EXPECT_EQ(bins[5].count, 1u);
    // 0.99, the value on the top edge and the value over the range in the last bin.
    EXPECT_EQ(bins[9].count, 3u);
}

TEST(ReportCharts, BinsWithoutStatedBoundsSpanRoundNumbers)
{
    const std::vector<double> values = {2.31, 2.9, 3.4, 3.41, 4.87};
    const std::vector<ChartBin> bins = chartBins(values, 12);
    ASSERT_EQ(bins.size(), 12u);
    EXPECT_LE(bins.front().low, 2.31);
    EXPECT_GE(bins.back().high, 4.87);
    // The bins lie between the round numbers an axis of the same values would start and end on.
    const std::vector<double> ticks = chartTicks(2.31, 4.87, 12);
    EXPECT_DOUBLE_EQ(bins.front().low, ticks.front());
    EXPECT_DOUBLE_EQ(bins.back().high, ticks.back());
    std::uint64_t counted = 0;
    for (const ChartBin& bin : bins)
    {
        counted += bin.count;
    }
    EXPECT_EQ(counted, values.size());

    EXPECT_TRUE(chartBins(values, 0).empty());
    EXPECT_TRUE(chartBins({}, 10).empty());
    EXPECT_TRUE(chartBins({kNotANumber}, 10).empty());
    // Equal values still get a range to lie in.
    const std::vector<ChartBin> equal = chartBins({3.0, 3.0, 3.0}, 10);
    ASSERT_EQ(equal.size(), 10u);
    EXPECT_LT(equal.front().low, equal.back().high);
}

// ---- What every chart keeps.

TEST(ReportCharts, EveryChartIsOneWellFormedElementThatSaysWhatItShows)
{
    for (const SampleChart& chart : sampleCharts())
    {
        SCOPED_TRACE(chart.title.toStdString());
        // A cell holds two bars; every other sample is one chart.
        const QStringList pieces = chart.markup.split(QStringLiteral("</svg> "), Qt::SkipEmptyParts);
        for (QString piece : pieces)
        {
            if (!piece.endsWith(QStringLiteral("</svg>")))
            {
                piece += QStringLiteral("</svg>");
            }
            const Parsed parsed = parsedChart(piece);
            ASSERT_FALSE(parsed.elements.empty());
            const Element& root = parsed.root();
            EXPECT_TRUE(root.has("viewBox"));
            if (root.hasClass("cellbar"))
            {
                // A bar in a cell repeats the figure beside it.
                EXPECT_EQ(root.attribute("aria-hidden").toStdString(), "true");
            }
            else
            {
                EXPECT_TRUE(root.hasClass("chart"));
                EXPECT_EQ(root.attribute("role").toStdString(), "img");
                EXPECT_FALSE(root.attribute("aria-label").isEmpty());
            }
        }
        if (!chart.legend.isEmpty())
        {
            const Parsed legend = parse(chart.legend);
            EXPECT_TRUE(legend.error.isEmpty()) << legend.error.toStdString();
        }
    }
}

TEST(ReportCharts, NoChartNamesAColourOrCarriesAStyle)
{
    static const QRegularExpression kColour(QStringLiteral("#[0-9A-Fa-f]{3,8}\\b|rgba?\\(|hsla?\\("));
    for (const SampleChart& chart : sampleCharts())
    {
        SCOPED_TRACE(chart.title.toStdString());
        for (const QString& markup : {chart.markup, chart.legend})
        {
            EXPECT_FALSE(markup.contains(QStringLiteral("fill=\"#")));
            EXPECT_FALSE(markup.contains(QStringLiteral("stroke=\"#")));
            EXPECT_FALSE(markup.contains(QStringLiteral("style=")));
            // A reference to the chart's own gradient is the one place a number sign may stand.
            QString withoutReferences = markup;
            withoutReferences.remove(QRegularExpression(QStringLiteral("url\\(#[A-Za-z0-9_-]+\\)")));
            EXPECT_FALSE(kColour.match(withoutReferences).hasMatch())
                << kColour.match(withoutReferences).captured().toStdString();
        }
    }
}

TEST(ReportCharts, NoChartPrintsAValueThatIsNotANumber)
{
    static const QRegularExpression kNotNumber(QStringLiteral("\\b(nan|inf)\\b"),
                                               QRegularExpression::CaseInsensitiveOption);
    for (const SampleChart& chart : sampleCharts())
    {
        SCOPED_TRACE(chart.title.toStdString());
        EXPECT_FALSE(kNotNumber.match(chart.markup).hasMatch())
            << kNotNumber.match(chart.markup).captured().toStdString();
        EXPECT_FALSE(chart.markup.contains(QStringLiteral("%1")));
    }
}

TEST(ReportCharts, AnEmptySeriesIsAStatedEmptyChart)
{
    const ChartDescription description = described();
    ConditionsChart conditions;
    conditions.description = description;
    conditions.measures = {{QStringLiteral("FWHM"), {}, {}}};
    SegmentedBar segments;
    segments.description = description;
    segments.segments = {{ChartState::Used, 0.0, QStringLiteral("0"), {}}};
    RangeChart range;
    range.description = description;
    ScatterChart scatter;
    scatter.description = description;
    scatter.points = {{kNotANumber, 1.0, ChartState::Used, {}}, {2.0, kNotANumber, ChartState::Used, {}}};
    DistributionChart distribution;
    distribution.description = description;
    FrameMeasureChart measures;
    measures.description = description;
    measures.id = QStringLiteral("none");
    measures.measures = {{QStringLiteral("FWHM"), {}, {}, {}}};
    DitherChart dither;
    dither.description = description;
    dither.nights = {{QStringLiteral("14 July"), {}}};
    SampleDistributionChart samples;
    samples.description = description;
    samples.channels = {{QStringLiteral("Red"), 0.001, 1.0, std::vector<std::uint64_t>(96, 0u), {}, {}},
                        {QStringLiteral("Green"), 0.0, 1.0, {4u, 5u}, {}, {}}};
    CoverageBar coverageOfNothing;
    coverageOfNothing.description = description;
    TimelineChart stages;
    stages.description = description;
    BarsChart noBars;
    noBars.description = description;
    RunsChart noRuns;
    noRuns.description = description;

    const std::vector<std::pair<const char*, QString>> charts = {
        {"conditions", conditionsChart(conditions)},
        {"segments", segmentedBar(segments)},
        {"range", rangeChart(range)},
        {"scatter", scatterChart(scatter)},
        {"distribution", distributionChart(distribution)},
        {"measures", frameMeasureChart(measures)},
        {"dither", ditherChart(dither)},
        {"samples", sampleDistributionChart(samples)},
        {"coverage", coverageBar(coverageOfNothing)},
        {"timeline", timelineChart(stages)},
        {"bars", barsChart(noBars)},
        {"runs", runsChart(noRuns)},
    };
    for (const auto& [name, markup] : charts)
    {
        SCOPED_TRACE(name);
        const Parsed parsed = parsedChart(markup);
        ASSERT_FALSE(parsed.elements.empty());
        EXPECT_TRUE(parsed.root().hasClass("chart"));
        EXPECT_TRUE(parsed.root().hasClass("is-empty"));
        EXPECT_EQ(parsed.root().attribute("aria-label").toStdString(), description.label.toStdString());
        // The chart is the sentence and nothing else: no axis, no mark, no tip.
        ASSERT_EQ(parsed.elements.size(), 2u);
        EXPECT_TRUE(parsed.elements[1].hasClass("c-empty"));
        EXPECT_EQ(parsed.elements[1].text.toStdString(), nothingToDraw().toStdString());
        EXPECT_FALSE(markup.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
    }
}

TEST(ReportCharts, ATipAndALabelAreEscaped)
{
    const QString hostile = hostileText();
    ChartTip tip;
    tip.heading = hostile;
    tip.lines = {QStringLiteral("line\none"), hostile};

    ScatterChart scatter;
    scatter.description = {hostile, hostile};
    scatter.xAxis.title = hostile;
    scatter.yAxis.title = hostile;
    scatter.points = {{1.0, 2.0, ChartState::Used, tip}, {2.0, 3.0, ChartState::RegistrationFailed, tip}};
    const Parsed parsed = parsedChart(scatterChart(scatter));
    ASSERT_FALSE(parsed.elements.empty());
    EXPECT_EQ(parsed.root().attribute("aria-label"), hostileTextRead());
    // The titles come out as text: the hostile markup made no element.
    EXPECT_EQ(parsed.texts("c-lab"), QStringList({hostileTextRead(), hostileTextRead()}));
    for (const Element& element : parsed.elements)
    {
        EXPECT_NE(element.name.toStdString(), "b");
    }
    // A tip is its heading and its lines, a line feed between them; a line feed inside one is a space.
    const std::vector<Element> hits = parsed.ofClass("c-hitc");
    ASSERT_EQ(hits.size(), 2u);
    const QString expected = hostileTextRead() + QStringLiteral("\nline one\n") + hostileTextRead();
    EXPECT_EQ(hits[0].attribute("data-tip"), expected);
    EXPECT_EQ(hits[1].attribute("data-tip"), expected);

    // The same text everywhere else a chart takes words.
    BarsChart barsOfText;
    barsOfText.description = described();
    barsOfText.bars = {{hostile, 2.0, hostile, tip}};
    const Parsed bars = parsedChart(barsChart(barsOfText));
    EXPECT_EQ(bars.count("c-hit"), 1u);
    EXPECT_EQ(bars.ofClass("c-hit").front().attribute("data-tip"), expected);
    EXPECT_EQ(bars.texts("c-val"), QStringList({hostileTextRead()}));

    const Parsed legend = parse(chartLegend({{ChartLegendItem::Mark::Swatch, ChartState::Used, 0, hostile, hostile}}));
    EXPECT_TRUE(legend.error.isEmpty()) << legend.error.toStdString();
}

TEST(ReportCharts, AChartWithoutATipCarriesNoTipAttribute)
{
    ScatterChart scatter;
    scatter.description = described();
    scatter.points = {{1.0, 2.0, ChartState::Used, {}}, {2.0, 3.0, ChartState::Used, {}}};
    const QString markup = scatterChart(scatter);
    EXPECT_FALSE(markup.contains(QStringLiteral("data-tip")));
}

// ---- The nights.

TEST(ReportCharts, TheConditionsHaveAPanelPerNightAndMeasure)
{
    const std::vector<SampleLight> lights = sampleLights(5, 34, 2);
    const ConditionsChart chart = conditionsOf(lights, 2);
    ASSERT_EQ(conditionsLayout(chart), ConditionsLayout::ByClock);
    const Parsed parsed = parsedChart(conditionsChart(chart));
    const std::size_t leftOut = leftOutOf(lights);
    ASSERT_GT(leftOut, 0u);

    // A night's name over each column of panels, a measure's title beside each row.
    EXPECT_EQ(parsed.texts("c-ptitle"), QStringList({QStringLiteral("14 July"), QStringLiteral("15 July")}));
    const QStringList titles = parsed.texts("c-lab");
    for (const ChartAxis& measure : chart.measures)
    {
        EXPECT_TRUE(titles.contains(measure.title)) << measure.title.toStdString();
    }
    // In each of the three panels: a dot per light used, a cross per light left out.
    EXPECT_EQ(parsed.count("c-dotS"), (lights.size() - leftOut) * 3u);
    EXPECT_EQ(parsed.count("c-x"), leftOut * 3u);
    // One hover band per light, each with the light's tip.
    const std::vector<Element> bands = parsed.ofClass("c-hit");
    ASSERT_EQ(bands.size(), lights.size());
    for (std::size_t index = 0; index < bands.size(); ++index)
    {
        EXPECT_FALSE(bands[index].attribute("data-tip").isEmpty());
    }
    EXPECT_TRUE(bands.front().attribute("data-tip").startsWith(QStringLiteral("L · light 1 · 20:30\n")));
    // The groups ran one after another, so each is a band of time with its name over it, on both nights.
    EXPECT_EQ(parsed.texts("c-bandname").count(QStringLiteral("Ha")), 2);
    EXPECT_EQ(parsed.texts("c-bandname").size(), 10);
}

TEST(ReportCharts, TheConditionsAreOnAClockAxisOfWholeHours)
{
    const std::vector<SampleLight> lights = sampleLights(5, 34, 2);
    const Parsed parsed = parsedChart(conditionsChart(conditionsOf(lights, 2)));
    static const QRegularExpression kHour(QStringLiteral("^([01][0-9]|2[0-3]):00$"));
    QStringList hours;
    for (const QString& tick : parsed.texts("c-tick"))
    {
        if (tick.contains(QLatin1Char(':')))
        {
            EXPECT_TRUE(kHour.match(tick).hasMatch()) << tick.toStdString();
            hours << tick;
        }
    }
    // The first light was taken at 20:30: the axis starts on the hour before, on both nights.
    EXPECT_EQ(hours.count(QStringLiteral("20:00")), 2);
    EXPECT_GE(hours.size(), 6);
    // The hours at a night's two ends are set inside its column, so the last hour of one night and the first of
    // the next never meet.
    for (const Element& tick : parsed.ofClass("c-tick"))
    {
        if (tick.text == QStringLiteral("20:00"))
        {
            EXPECT_FALSE(tick.has("text-anchor"));
        }
        if (tick.text == QStringLiteral("02:00"))
        {
            EXPECT_EQ(tick.attribute("text-anchor").toStdString(), "end");
        }
    }
    EXPECT_EQ(hours.count(QStringLiteral("02:00")), 2);

    // The other ticks are the measures' values, round and spanning each measure.
    std::vector<double> values;
    for (const QString& tick : parsed.texts("c-tick"))
    {
        if (!tick.contains(QLatin1Char(':')))
        {
            values.push_back(tickValue(tick));
            EXPECT_TRUE(std::isfinite(values.back())) << tick.toStdString();
        }
    }
    EXPECT_GE(values.size(), 6u);
}

TEST(ReportCharts, ALightLeftOutWithoutAMeasureCarriesItsCrossOnTheAxis)
{
    ConditionsChart chart;
    chart.description = described();
    chart.measures = {{QStringLiteral("FWHM (px)"), {}, {}}};
    chart.nights = {QStringLiteral("14 July")};
    chart.groups = {QStringLiteral("L")};
    for (int index = 0; index < 4; ++index)
    {
        ConditionsLight light;
        light.startMinutes = 1260.0 + 3.0 * index;
        light.exposureMinutes = 2.0;
        light.values = {2.5 + 0.1 * index};
        chart.lights.push_back(light);
    }
    // Excluded in the lights' list: nothing was measured. A used light with no measure draws no mark at all.
    chart.lights[1].state = ChartState::ExcludedByUser;
    chart.lights[1].values = {kNotANumber};
    chart.lights[2].values = {};
    const Parsed parsed = parsedChart(conditionsChart(chart));
    EXPECT_EQ(parsed.count("c-dotS"), 2u);
    const std::vector<Element> crosses = parsed.ofClass("c-x");
    ASSERT_EQ(crosses.size(), 1u);
    EXPECT_TRUE(crosses.front().hasClass("s-user"));
    // The cross sits where the panel's axis is.
    const std::vector<Element> axes = parsed.ofClass("c-axis");
    ASSERT_FALSE(axes.empty());
    const std::vector<Element> rings = parsed.ofClass("c-xring");
    ASSERT_EQ(rings.size(), 1u);
    EXPECT_NEAR(rings.front().number("cy"), axes.front().number("y1"), 0.11);
    EXPECT_EQ(parsed.count("c-hit"), 4u);
}

TEST(ReportCharts, TheConditionsFallBackToCaptureOrder)
{
    // More than four nights.
    const std::vector<SampleLight> sixNights = sampleLights(3, 36, 6);
    const ConditionsChart many = conditionsOf(sixNights, 6);
    EXPECT_EQ(conditionsLayout(many), ConditionsLayout::ByCaptureOrder);
    const Parsed parsed = parsedChart(conditionsChart(many));
    EXPECT_EQ(parsed.count("c-hit"), sixNights.size());
    EXPECT_EQ(parsed.count("c-x"), leftOutOf(sixNights) * 3u);
    // No clock: the nights are named where each begins.
    for (const QString& tick : parsed.texts("c-tick"))
    {
        EXPECT_FALSE(tick.contains(QStringLiteral(":00"))) << tick.toStdString();
    }
    EXPECT_EQ(parsed.texts("c-ptitle").size(), 6);

    // A light that states no time.
    const std::vector<SampleLight> twoNights = sampleLights(2, 12, 2);
    ConditionsChart untimed = conditionsOf(twoNights, 2);
    EXPECT_EQ(conditionsLayout(untimed), ConditionsLayout::ByClock);
    untimed.lights[3].startMinutes = kNotANumber;
    EXPECT_EQ(conditionsLayout(untimed), ConditionsLayout::ByCaptureOrder);
    EXPECT_EQ(parsedChart(conditionsChart(untimed)).count("c-hit"), twoNights.size());

    // A light of a night the chart does not name.
    ConditionsChart unnamed = conditionsOf(twoNights, 2);
    unnamed.lights[0].night = 7;
    EXPECT_EQ(conditionsLayout(unnamed), ConditionsLayout::ByCaptureOrder);
    EXPECT_EQ(parsedChart(conditionsChart(unnamed)).count("c-hit"), twoNights.size());
}

TEST(ReportCharts, ADenseConditionsChartDrawsNoMarkAndNoBandPerLight)
{
    const std::vector<SampleLight> lights = sampleLights(1, 301, 1);
    ASSERT_GT(lights.size(), kChartDenseAbove);
    const Parsed dense = parsedChart(conditionsChart(conditionsOf(lights, 1)));
    EXPECT_EQ(dense.count("c-dotS"), 0u);
    EXPECT_EQ(dense.count("c-hit"), 0u);
    EXPECT_GE(dense.count("c-line15"), 3u);
    // The crosses stay, and each says which light it is.
    const std::vector<Element> rings = dense.ofClass("c-xring");
    ASSERT_EQ(rings.size(), leftOutOf(lights) * 3u);
    for (const Element& ring : rings)
    {
        EXPECT_FALSE(ring.attribute("data-tip").isEmpty());
    }

    // At the limit itself every light still has its mark and its band.
    const std::vector<SampleLight> limit = sampleLights(1, 300, 1);
    const Parsed marked = parsedChart(conditionsChart(conditionsOf(limit, 1)));
    EXPECT_EQ(marked.count("c-hit"), 300u);
    EXPECT_EQ(marked.count("c-dotS"), (300u - leftOutOf(limit)) * 3u);
}

// ---- Used and left out.

TEST(ReportCharts, TheSegmentedBarHasASegmentPerStateThatHasLights)
{
    const std::vector<SampleLight> lights = sampleLights(5, 34, 2);
    const SegmentedBar bar = usedAndLeftOutOf(lights);
    const Parsed parsed = parsedChart(segmentedBar(bar));
    const std::vector<Element> segments = parsed.ofClass("c-seg");
    // The session has lights in five states; no light is "not stacked", so that state has no segment.
    ASSERT_EQ(segments.size(), 5u);
    EXPECT_TRUE(segments[0].hasClass("s-used"));
    EXPECT_TRUE(segments[1].hasClass("s-registration"));
    EXPECT_TRUE(segments[2].hasClass("s-user"));
    EXPECT_TRUE(segments[3].hasClass("s-stack"));
    EXPECT_TRUE(segments[4].hasClass("s-restack"));
    EXPECT_EQ(parsed.count("s-notstacked"), 0u);

    double covered = 0.0;
    double previousEnd = -1.0;
    for (const Element& segment : segments)
    {
        EXPECT_FALSE(segment.attribute("data-tip").isEmpty());
        EXPECT_EQ(segment.attribute("tabindex").toStdString(), "0");
        EXPECT_GT(segment.number("width"), 0.0);
        EXPECT_GT(segment.number("x"), previousEnd);
        previousEnd = segment.number("x") + segment.number("width");
        covered += segment.number("width");
    }
    EXPECT_LE(previousEnd, bar.width + 0.11);
    // The segments are in proportion: the used lights take their share of what is drawn.
    const double used = static_cast<double>(lights.size() - leftOutOf(lights)) / static_cast<double>(lights.size());
    EXPECT_NEAR(segments[0].number("width") / covered, used, 0.03);
    // The count of the widest segment stands under it.
    EXPECT_TRUE(parsed.texts("c-val").contains(bar.segments.front().valueText));
}

TEST(ReportCharts, ASegmentOfOneLightAmongThousandsIsStillSeen)
{
    SegmentedBar bar;
    bar.description = described();
    bar.segments = {{ChartState::Used, 1999.0, QStringLiteral("1 999"), {}},
                    {ChartState::RegistrationFailed, 1.0, QStringLiteral("1"), {}},
                    {ChartState::NotStacked, kNotANumber, QStringLiteral("?"), {}}};
    const Parsed parsed = parsedChart(segmentedBar(bar));
    const std::vector<Element> segments = parsed.ofClass("c-seg");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_GE(segments[1].number("width"), 4.0);
    EXPECT_LE(segments[1].number("x") + segments[1].number("width"), bar.width + 0.11);
}

// ---- Range by group.

TEST(ReportCharts, TheRangeChartDrawsTheRangeTheMiddleHalfAndTheMedianOfEachGroup)
{
    const std::vector<SampleLight> lights = sampleLights(5, 34, 2);
    const RangeChart chart = rangeOf(lights, 5);
    ASSERT_EQ(chart.rows.size(), 5u);
    const Parsed parsed = parsedChart(rangeChart(chart));
    const std::vector<Element> whiskers = parsed.ofClass("c-whisk");
    const std::vector<Element> boxes = parsed.ofClass("c-iqr");
    const std::vector<Element> medians = parsed.ofClass("c-dot");
    ASSERT_EQ(whiskers.size(), 5u);
    ASSERT_EQ(boxes.size(), 5u);
    ASSERT_EQ(medians.size(), 5u);
    for (std::size_t row = 0; row < 5; ++row)
    {
        // The middle half lies inside the range, the median inside the middle half.
        const double boxStart = boxes[row].number("x");
        const double boxEnd = boxStart + boxes[row].number("width");
        EXPECT_LE(whiskers[row].number("x1"), boxStart + 0.11);
        EXPECT_GE(whiskers[row].number("x2"), boxEnd - 0.11);
        EXPECT_GE(medians[row].number("cx"), boxStart - 0.11);
        EXPECT_LE(medians[row].number("cx"), boxEnd + 0.11);
    }
    const std::vector<Element> rows = parsed.ofClass("c-hit");
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_TRUE(rows.front().attribute("data-tip").startsWith(QStringLiteral("L · FWHM\nMedian ")));
    // Each row's name at its left and its median, as the report prints it, at its right.
    const QStringList names = parsed.texts("c-lab");
    EXPECT_TRUE(names.contains(QStringLiteral("Ha")));
    EXPECT_TRUE(names.contains(chart.axis.title));
    EXPECT_EQ(parsed.texts("c-val").size(), 5);
    EXPECT_EQ(parsed.texts("c-val").front(), chart.rows.front().valueText);

    std::vector<double> ticks;
    double lowest = std::numeric_limits<double>::infinity();
    double highest = -lowest;
    for (const QString& tick : parsed.texts("c-tick"))
    {
        ticks.push_back(tickValue(tick));
    }
    for (const ChartRangeRow& row : chart.rows)
    {
        lowest = (std::min)(lowest, row.range.lowest);
        highest = (std::max)(highest, row.range.highest);
    }
    EXPECT_TRUE(ticksAreRound(ticks));
    EXPECT_LE(ticks.front(), lowest);
    EXPECT_GE(ticks.back(), highest);
}

TEST(ReportCharts, ARangeOfOneValueIsStillDrawn)
{
    RangeChart chart;
    chart.description = described();
    chart.rows = {{QStringLiteral("L"), {3.0, 3.0, 3.0, 3.0, 3.0}, QStringLiteral("3"), {}}};
    const Parsed parsed = parsedChart(rangeChart(chart));
    EXPECT_EQ(parsed.count("c-dot"), 1u);
    EXPECT_FALSE(parsed.root().hasClass("is-empty"));
}

// ---- One measure against another.

TEST(ReportCharts, TheScatterHasAMarkPerLightAndACrossPerLightLeftOut)
{
    const std::vector<SampleLight> lights = sampleLights(5, 34, 2);
    const ScatterChart chart = scatterOf(lights);
    const Parsed parsed = parsedChart(scatterChart(chart));
    // The light excluded in the lights' list states neither measure: it is not in the chart.
    const std::size_t unmeasured = static_cast<std::size_t>(
        std::count_if(lights.begin(), lights.end(), [](const SampleLight& light) { return std::isnan(light.fwhm); }));
    ASSERT_EQ(unmeasured, 1u);
    const std::size_t leftOut = leftOutOf(lights);
    EXPECT_EQ(parsed.count("c-pt"), lights.size() - leftOut);
    EXPECT_EQ(parsed.count("c-x"), leftOut - unmeasured);
    const std::vector<Element> hits = parsed.ofClass("c-hitc");
    ASSERT_EQ(hits.size(), lights.size() - unmeasured);
    for (const Element& hit : hits)
    {
        EXPECT_FALSE(hit.attribute("data-tip").isEmpty());
    }
    EXPECT_EQ(parsed.texts("c-lab"), QStringList({chart.xAxis.title, chart.yAxis.title}));

    // Every mark lies inside the plot the ticks span.
    const std::vector<Element> grid = parsed.ofClass("c-grid");
    double left = std::numeric_limits<double>::infinity();
    double right = -left;
    for (const Element& line : grid)
    {
        left = (std::min)(left, line.number("x1"));
        right = (std::max)(right, line.number("x2"));
    }
    for (const Element& mark : parsed.ofClass("c-pt"))
    {
        EXPECT_GE(mark.number("cx"), left - 0.11);
        EXPECT_LE(mark.number("cx"), right + 0.11);
    }
}

TEST(ReportCharts, AScatterOfOnePointOrOfEqualValuesIsDrawn)
{
    ScatterChart one;
    one.description = described();
    one.points = {{3.1, 0.4, ChartState::Used, {QStringLiteral("One"), {}}}};
    const Parsed single = parsedChart(scatterChart(one));
    EXPECT_EQ(single.count("c-pt"), 1u);
    EXPECT_EQ(single.count("c-hitc"), 1u);

    ScatterChart equal;
    equal.description = described();
    for (int index = 0; index < 5; ++index)
    {
        equal.points.push_back({3.0, 0.4, index == 4 ? ChartState::LeftOutByStack : ChartState::Used, {}});
    }
    const Parsed same = parsedChart(scatterChart(equal));
    EXPECT_EQ(same.count("c-pt"), 4u);
    const std::vector<Element> crosses = same.ofClass("c-x");
    ASSERT_EQ(crosses.size(), 1u);
    EXPECT_TRUE(crosses.front().hasClass("s-stack"));
    // The marks share one place, inside the plot.
    const std::vector<Element> marks = same.ofClass("c-pt");
    EXPECT_DOUBLE_EQ(marks[0].number("cx"), marks[3].number("cx"));
    EXPECT_GT(marks[0].number("cx"), 0.0);
    EXPECT_LT(marks[0].number("cx"), equal.width);
}

TEST(ReportCharts, ADenseScatterDrawsItsMarksAsOnePath)
{
    const std::vector<SampleLight> lights = sampleLights(1, 400, 2);
    const Parsed parsed = parsedChart(scatterChart(scatterOf(lights)));
    const std::size_t unmeasured = 1u;
    const std::size_t leftOut = leftOutOf(lights);
    EXPECT_EQ(parsed.count("c-pt"), 0u);
    EXPECT_EQ(parsed.count("c-hitc"), 0u);
    const std::vector<Element> marks = parsed.ofClass("c-pts");
    ASSERT_EQ(marks.size(), 1u);
    EXPECT_EQ(static_cast<std::size_t>(subpaths(marks.front())), lights.size() - leftOut);
    const std::vector<Element> rings = parsed.ofClass("c-xring");
    ASSERT_EQ(rings.size(), leftOut - unmeasured);
    for (const Element& ring : rings)
    {
        EXPECT_FALSE(ring.attribute("data-tip").isEmpty());
    }
}

// ---- A distribution.

TEST(ReportCharts, TheDistributionHasABarPerBinThatHoldsALight)
{
    const std::vector<SampleLight> lights = sampleLights(5, 34, 2);
    const DistributionChart chart = weightsOf(lights);
    ASSERT_EQ(chart.bins.size(), 10u);
    const Parsed parsed = parsedChart(distributionChart(chart));
    std::size_t filled = 0;
    std::uint64_t peak = 0;
    for (const ChartBin& bin : chart.bins)
    {
        filled += bin.count > 0 ? 1u : 0u;
        peak = (std::max)(peak, bin.count);
    }
    ASSERT_GT(filled, 1u);
    EXPECT_EQ(parsed.count("c-bar"), filled);
    // Every bin can be pointed at, the empty ones too, and says its range.
    const std::vector<Element> hits = parsed.ofClass("c-hit");
    ASSERT_EQ(hits.size(), 10u);
    EXPECT_TRUE(hits.front().attribute("data-tip").startsWith(QStringLiteral("Weight 0.0 to 0.1\n")));
    // The tallest bar has its count over it.
    EXPECT_TRUE(parsed.texts("c-val").contains(QString::number(peak)));
    EXPECT_TRUE(parsed.texts("c-lab").contains(chart.axis.title));

    // The count axis is whole numbers from zero that reach the tallest bar.
    std::vector<double> counts;
    QStringList edges;
    for (const Element& tick : parsed.ofClass("c-tick"))
    {
        if (tick.attribute("text-anchor") == QStringLiteral("end"))
        {
            counts.push_back(tickValue(tick.text));
        }
        else
        {
            edges << tick.text;
        }
    }
    EXPECT_TRUE(ticksAreRound(counts));
    EXPECT_DOUBLE_EQ(counts.front(), 0.0);
    EXPECT_GE(counts.back(), static_cast<double>(peak));
    for (const double count : counts)
    {
        EXPECT_DOUBLE_EQ(count, std::round(count));
    }
    // The value axis names the bins' edges, the first and the last among them, to the decimals a bin's width needs.
    EXPECT_EQ(edges.front().toStdString(), "0.0");
    EXPECT_EQ(edges.back().toStdString(), "1.0");
    EXPECT_EQ(edges.size(), 6);
}

TEST(ReportCharts, ADistributionOfOneLightHasWholeCounts)
{
    DistributionChart chart;
    chart.description = described();
    chart.bins = chartBins({0.42}, 10, 0.0, 1.0);
    const Parsed parsed = parsedChart(distributionChart(chart));
    EXPECT_EQ(parsed.count("c-bar"), 1u);
    for (const Element& tick : parsed.ofClass("c-tick"))
    {
        if (tick.attribute("text-anchor") == QStringLiteral("end"))
        {
            EXPECT_DOUBLE_EQ(tickValue(tick.text), std::round(tickValue(tick.text))) << tick.text.toStdString();
        }
    }
}

// ---- A master's lights.

TEST(ReportCharts, TheFrameMeasureChartHasALayerPerMeasureAndOneSetOfBands)
{
    const std::vector<SampleLight> lights = lightsOfGroup(sampleLights(5, 34, 2), 0);
    const FrameMeasureChart chart = measuresOf(lights, QStringLiteral("l"));
    const Parsed parsed = parsedChart(frameMeasureChart(chart));
    const std::size_t leftOut = leftOutOf(lights);
    ASSERT_EQ(leftOut, 5u);

    // A layer per measure, numbered in order, each with its title.
    const std::vector<Element> layers = parsed.ofClass("c-layer");
    ASSERT_EQ(layers.size(), chart.measures.size());
    for (std::size_t index = 0; index < layers.size(); ++index)
    {
        EXPECT_TRUE(layers[index].hasClass(qPrintable(QStringLiteral("c-layer-%1").arg(index))));
    }
    QStringList titles;
    for (const ChartMeasure& measure : chart.measures)
    {
        titles << measure.title;
    }
    EXPECT_EQ(parsed.texts("c-ptitle"), titles);

    // One set of hover bands for all the layers: a band per light, with the light's tip.
    const std::vector<Element> bands = parsed.ofClass("c-hit");
    ASSERT_EQ(bands.size(), lights.size());
    EXPECT_TRUE(bands.front().attribute("data-tip").startsWith(QStringLiteral("L · light 1 · ")));
    // The bands tile the plot from left to right.
    for (std::size_t index = 1; index < bands.size(); ++index)
    {
        EXPECT_NEAR(bands[index].number("x"), bands[index - 1].number("x") + bands[index - 1].number("width"), 0.2);
    }

    // In every layer: a cross per light left out. A dot per light used that states the measure: the weight of a
    // light the stack dropped is not stated, and that light is a cross, so the dots are the same in every layer.
    EXPECT_EQ(parsed.count("c-x"), leftOut * chart.measures.size());
    EXPECT_EQ(parsed.count("c-dot"), (lights.size() - leftOut) * chart.measures.size());
    // The median of each measure, with the words the caller gave it.
    EXPECT_EQ(parsed.count("c-med"), chart.measures.size());
    EXPECT_TRUE(parsed.texts("c-lab").contains(chart.measures.front().referenceLabel));
    // Where the second night begins, and each night's name under the axis.
    const QStringList ticks = parsed.texts("c-tick");
    ASSERT_EQ(chart.nights.size(), 2u);
    EXPECT_TRUE(ticks.contains(chart.nights[0].label));
    EXPECT_TRUE(ticks.contains(chart.nights[1].label));
    EXPECT_TRUE(ticks.contains(chart.lastLabel));
}

TEST(ReportCharts, ALightWithoutAMeasureBreaksTheLine)
{
    FrameMeasureChart chart;
    chart.description = described();
    chart.id = QStringLiteral("gap");
    ChartMeasure measure;
    measure.title = QStringLiteral("FWHM (px)");
    measure.values = {2.4, 2.6, kNotANumber, 2.5, 2.7, kNotANumber};
    chart.measures = {measure};
    chart.lights.assign(6, ChartLight{});
    // The last light states no measure and was left out: its cross is on the axis.
    chart.lights[5].state = ChartState::RegistrationFailed;
    const Parsed parsed = parsedChart(frameMeasureChart(chart));
    // Two runs of lights that state the measure: two lines, and a wash under each.
    EXPECT_EQ(parsed.count("c-line"), 2u);
    EXPECT_EQ(parsed.count("c-wash"), 2u);
    EXPECT_EQ(parsed.count("c-dot"), 4u);
    EXPECT_EQ(parsed.count("c-x"), 1u);
    EXPECT_EQ(parsed.count("c-hit"), 6u);
    const std::vector<Element> rings = parsed.ofClass("c-xring");
    const std::vector<Element> axes = parsed.ofClass("c-axis");
    ASSERT_EQ(rings.size(), 1u);
    ASSERT_FALSE(axes.empty());
    EXPECT_NEAR(rings.front().number("cy"), axes.front().number("y1"), 0.11);

    // The ticks are round and span the four values that are numbers.
    std::vector<double> ticks;
    for (const QString& tick : parsed.texts("c-tick"))
    {
        ticks.push_back(tickValue(tick));
    }
    EXPECT_TRUE(ticksAreRound(ticks));
    EXPECT_LE(ticks.front(), 2.4);
    EXPECT_GE(ticks.back(), 2.7);
}

TEST(ReportCharts, AMeasureNoLightStatesIsALayerThatSaysSo)
{
    FrameMeasureChart chart;
    chart.description = described();
    chart.id = QStringLiteral("partial");
    chart.measures = {{QStringLiteral("FWHM (px)"), {2.4, 2.6, 2.5}, {}, {}},
                      {QStringLiteral("SNR"), {kNotANumber, kNotANumber, kNotANumber}, {}, {}},
                      {QStringLiteral("Weight"), {0.8}, {}, {}}};
    chart.lights.assign(3, ChartLight{});
    const Parsed parsed = parsedChart(frameMeasureChart(chart));
    // The layers keep their numbers, so the switch still finds each.
    EXPECT_EQ(parsed.count("c-layer"), 3u);
    EXPECT_EQ(parsed.texts("c-empty"), QStringList({nothingToDraw()}));
    // A measure with fewer values than lights is drawn as far as it goes.
    EXPECT_EQ(parsed.count("c-dot"), 3u + 1u);
    EXPECT_FALSE(parsed.root().hasClass("is-empty"));
}

TEST(ReportCharts, AFrameMeasureChartOfOneLightOrOfEqualValuesIsDrawn)
{
    FrameMeasureChart one;
    one.description = described();
    one.id = QStringLiteral("one");
    one.measures = {{QStringLiteral("FWHM (px)"), {2.4}, 2.4, QStringLiteral("median 2.4")}};
    one.lights.assign(1, ChartLight{});
    const Parsed single = parsedChart(frameMeasureChart(one));
    EXPECT_EQ(single.count("c-dot"), 1u);
    EXPECT_EQ(single.count("c-hit"), 1u);

    FrameMeasureChart equal;
    equal.description = described();
    equal.id = QStringLiteral("equal");
    equal.measures = {{QStringLiteral("Weight"), std::vector<double>(8, 1.0), 1.0, QStringLiteral("median 1")}};
    equal.lights.assign(8, ChartLight{});
    const Parsed same = parsedChart(frameMeasureChart(equal));
    EXPECT_EQ(same.count("c-dot"), 8u);
    const std::vector<Element> dots = same.ofClass("c-dot");
    const std::vector<Element> axes = same.ofClass("c-axis");
    ASSERT_FALSE(axes.empty());
    // Equal values lie on one line inside the plot, not on its floor.
    EXPECT_DOUBLE_EQ(dots.front().number("cy"), dots.back().number("cy"));
    EXPECT_LT(dots.front().number("cy"), axes.front().number("y1") - 1.0);
}

TEST(ReportCharts, TheGradientOfAFrameMeasureChartIsNamedAfterTheChart)
{
    const std::vector<SampleLight> lights = lightsOfGroup(sampleLights(2, 12, 1), 0);
    const QString first = frameMeasureChart(measuresOf(lights, QStringLiteral("luminance")));
    const QString second = frameMeasureChart(measuresOf(lights, QStringLiteral("M 20 (Ha) #2")));
    const auto gradientOf = [](const QString& markup) {
        const Parsed parsed = parsedChart(markup);
        QStringList ids;
        for (const Element& element : parsed.elements)
        {
            if (element.name == QStringLiteral("linearGradient"))
            {
                ids << element.attribute("id");
            }
        }
        EXPECT_EQ(ids.size(), 1);
        const QString id = ids.value(0);
        // Every wash of the chart is filled with the chart's own gradient.
        for (const Element& wash : parsed.ofClass("c-wash"))
        {
            EXPECT_EQ(wash.attribute("fill"), QStringLiteral("url(#%1)").arg(id));
        }
        EXPECT_GT(parsed.count("c-wash"), 0u);
        return id;
    };
    const QString firstId = gradientOf(first);
    const QString secondId = gradientOf(second);
    EXPECT_NE(firstId, secondId);
    static const QRegularExpression kName(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]*$"));
    EXPECT_TRUE(kName.match(firstId).hasMatch()) << firstId.toStdString();
    EXPECT_TRUE(kName.match(secondId).hasMatch()) << secondId.toStdString();
    // The same chart drawn twice is the same markup: a report rendered again from its model does not change.
    EXPECT_EQ(first, frameMeasureChart(measuresOf(lights, QStringLiteral("luminance"))));
}

TEST(ReportCharts, ADenseFrameMeasureChartDrawsNoMarkAndNoBandPerLight)
{
    const std::vector<SampleLight> lights = sampleLights(1, 301, 1);
    const FrameMeasureChart chart = measuresOf(lights, QStringLiteral("dense"));
    const Parsed dense = parsedChart(frameMeasureChart(chart));
    EXPECT_EQ(dense.count("c-dot"), 0u);
    EXPECT_EQ(dense.count("c-hit"), 0u);
    EXPECT_GE(dense.count("c-line"), chart.measures.size());
    // Nor a wash under its lines, which would state every light's place a second time.
    EXPECT_EQ(dense.count("c-wash"), 0u);
    EXPECT_FALSE(frameMeasureChart(chart).contains(QStringLiteral("linearGradient")));
    const std::vector<Element> rings = dense.ofClass("c-xring");
    ASSERT_EQ(rings.size(), leftOutOf(lights) * chart.measures.size());
    for (const Element& ring : rings)
    {
        EXPECT_FALSE(ring.attribute("data-tip").isEmpty());
    }

    const std::vector<SampleLight> limit = sampleLights(1, 300, 1);
    const Parsed marked = parsedChart(frameMeasureChart(measuresOf(limit, QStringLiteral("limit"))));
    EXPECT_EQ(marked.count("c-hit"), 300u);
}

// ---- Where each light sat.

TEST(ReportCharts, TheDitherChartMarksEachNightInItsOwnShape)
{
    const std::vector<SampleLight> lights = sampleLights(3, 36, 6);
    const DitherChart chart = ditherOf(lights, 6);
    const Parsed parsed = parsedChart(ditherChart(chart));
    std::size_t placed = 0;
    for (const SampleLight& light : lights)
    {
        placed += std::isfinite(light.shiftX) ? 1u : 0u;
    }
    ASSERT_LT(placed, lights.size());
    // A mark and a tip per light that has a place; a light with none is not in the chart.
    const std::vector<Element> marks = parsed.ofClass("c-n");
    ASSERT_EQ(marks.size(), placed);
    const std::vector<Element> hits = parsed.ofClass("c-hitc");
    ASSERT_EQ(hits.size(), placed);
    for (const Element& hit : hits)
    {
        EXPECT_FALSE(hit.attribute("data-tip").isEmpty());
    }
    // Six nights, six pairs of shape and colour: a night is not told by colour alone.
    QStringList looks;
    for (const Element& mark : marks)
    {
        const QString look = mark.attribute("class");
        if (!looks.contains(look))
        {
            looks << look;
        }
    }
    EXPECT_EQ(looks.size(), 6);
    QStringList shapes;
    for (const QString& look : looks)
    {
        for (const QString& part : look.split(QLatin1Char(' ')))
        {
            if (part.startsWith(QStringLiteral("c-shape-")) && !shapes.contains(part))
            {
                shapes << part;
            }
        }
    }
    EXPECT_EQ(shapes.size(), 4);

    // The plot is square, on one scale, with the reference at its centre.
    const std::vector<Element> zero = parsed.ofClass("c-zero");
    ASSERT_EQ(zero.size(), 2u);
    std::vector<double> ticks;
    for (const Element& tick : parsed.ofClass("c-tick"))
    {
        if (tick.attribute("text-anchor") == QStringLiteral("middle"))
        {
            ticks.push_back(tickValue(tick.text));
        }
    }
    EXPECT_TRUE(ticksAreRound(ticks));
    EXPECT_DOUBLE_EQ(ticks.front(), -ticks.back());
    double reach = 0.0;
    for (const SampleLight& light : lights)
    {
        if (std::isfinite(light.shiftX))
        {
            reach = (std::max)({reach, std::abs(light.shiftX), std::abs(light.shiftY)});
        }
    }
    EXPECT_GE(ticks.back(), reach);
    EXPECT_EQ(parsed.texts("c-lab"), QStringList({chart.xAxis.title, chart.yAxis.title}));
}

TEST(ReportCharts, ADitherChartOfOneLightAtTheReferenceIsDrawn)
{
    DitherChart chart;
    chart.description = described();
    chart.nights = {{QStringLiteral("14 July"), {{0.0, 0.0, ChartState::Used, {}}}}};
    const Parsed parsed = parsedChart(ditherChart(chart));
    EXPECT_EQ(parsed.count("c-n"), 1u);
    EXPECT_EQ(parsed.count("c-zero"), 2u);
}

TEST(ReportCharts, ADenseDitherChartDrawsEachNightAsOnePath)
{
    const std::vector<SampleLight> lights = sampleLights(1, 400, 2);
    const Parsed parsed = parsedChart(ditherChart(ditherOf(lights, 2)));
    EXPECT_EQ(parsed.count("c-hitc"), 0u);
    const std::vector<Element> nights = parsed.ofClass("c-n");
    ASSERT_EQ(nights.size(), 2u);
    std::size_t placed = 0;
    for (const SampleLight& light : lights)
    {
        placed += std::isfinite(light.shiftX) ? 1u : 0u;
    }
    EXPECT_EQ(static_cast<std::size_t>(subpaths(nights[0]) + subpaths(nights[1])), placed);
    EXPECT_TRUE(nights[0].hasClass("c-n0"));
    EXPECT_TRUE(nights[1].hasClass("c-n1"));
}

// ---- A master's samples.

TEST(ReportCharts, TheSampleDistributionHasAPanelPerChannelOnALogarithmicAxis)
{
    const SampleDistributionChart mono = samplesOf(1);
    const Parsed one = parsedChart(sampleDistributionChart(mono));
    EXPECT_EQ(one.count("c-hist"), 1u);
    EXPECT_EQ(one.count("c-med"), 1u);
    EXPECT_TRUE(one.texts("c-lab").contains(mono.channels.front().medianLabel));
    EXPECT_TRUE(one.texts("c-lab").contains(mono.axis.title));
    // The axis is marked at the powers of ten from the first bin to the last.
    EXPECT_EQ(one.texts("c-tick"), QStringList({QStringLiteral("0.001"), QStringLiteral("0.01"), QStringLiteral("0.1"),
                                                QStringLiteral("1")}));
    // One channel needs no name.
    EXPECT_TRUE(one.texts("c-ptitle").isEmpty());

    // The median's line is where the median lies between the powers of ten.
    const std::vector<Element> grid = one.ofClass("c-grid");
    ASSERT_EQ(grid.size(), 4u);
    const double left = grid.front().number("x1");
    const double right = grid.back().number("x1");
    const double expected = left + (right - left) * (std::log10(*mono.channels.front().median) + 3.0) / 3.0;
    EXPECT_NEAR(one.ofClass("c-med").front().number("x1"), expected, 0.11);

    const SampleDistributionChart colour = samplesOf(3);
    const Parsed three = parsedChart(sampleDistributionChart(colour));
    EXPECT_EQ(three.count("c-hist"), 3u);
    EXPECT_EQ(three.count("c-med"), 3u);
    EXPECT_EQ(three.texts("c-ptitle"),
              QStringList({QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")}));
    // The panels share one axis, named once.
    EXPECT_EQ(three.texts("c-lab").count(colour.axis.title), 1);
    EXPECT_EQ(three.texts("c-tick").size(), 4);
}

TEST(ReportCharts, AChannelWithNothingToDrawIsLeftOut)
{
    SampleDistributionChart chart = samplesOf(3);
    std::fill(chart.channels[1].counts.begin(), chart.channels[1].counts.end(), 0u);
    chart.channels[2].lowest = 0.0;
    const Parsed parsed = parsedChart(sampleDistributionChart(chart));
    EXPECT_EQ(parsed.count("c-hist"), 1u);
    EXPECT_EQ(parsed.texts("c-ptitle"), QStringList({QStringLiteral("Red")}));
}

// ---- Coverage, and a share in a cell.

TEST(ReportCharts, TheCoverageBarRunsFromTheLeastCoveredPixelWithAMarkAtTheMedian)
{
    const CoverageBar bar = coverage();
    const Parsed parsed = parsedChart(coverageBar(bar));
    const std::vector<Element> track = parsed.ofClass("c-track");
    const std::vector<Element> span = parsed.ofClass("c-span");
    const std::vector<Element> mark = parsed.ofClass("c-mark");
    ASSERT_EQ(track.size(), 1u);
    ASSERT_EQ(span.size(), 1u);
    ASSERT_EQ(mark.size(), 1u);
    const double width = track.front().number("width");
    EXPECT_NEAR(span.front().number("x"), track.front().number("x") + width * 41.0 / 60.0, 0.11);
    EXPECT_NEAR(span.front().number("x") + span.front().number("width"), track.front().number("x") + width, 0.11);
    EXPECT_NEAR(mark.front().number("x") + mark.front().number("width") / 2.0,
                track.front().number("x") + width * 58.0 / 60.0, 0.11);
    EXPECT_EQ(parsed.texts("c-tick"), bar.notes);
    // The whole bar is one thing to point at, and can be reached with the keyboard.
    const std::vector<Element> hits = parsed.ofClass("c-hit");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_TRUE(hits.front().attribute("data-tip").startsWith(QStringLiteral("Coverage\n")));
    EXPECT_EQ(hits.front().attribute("tabindex").toStdString(), "0");

    // A bar whose least or median is not stated draws what it has.
    CoverageBar partial = bar;
    partial.least = kNotANumber;
    partial.median = kNotANumber;
    const Parsed drawn = parsedChart(coverageBar(partial));
    EXPECT_EQ(drawn.count("c-track"), 1u);
    EXPECT_EQ(drawn.count("c-span") + drawn.count("c-mark"), 0u);
}

TEST(ReportCharts, ACellBarIsItsShareOfTheCell)
{
    const Parsed plain = parsedChart(cellBar(0.25));
    ASSERT_EQ(plain.count("c-track"), 1u);
    ASSERT_EQ(plain.count("c-fill"), 1u);
    EXPECT_EQ(plain.count("c-mark"), 0u);
    const double width = plain.ofClass("c-track").front().number("width");
    EXPECT_NEAR(plain.ofClass("c-fill").front().number("width"), width * 0.25, 0.11);

    const Parsed marked = parsedChart(cellBar(0.5, 0.75));
    ASSERT_EQ(marked.count("c-mark"), 1u);
    const Element mark = marked.ofClass("c-mark").front();
    EXPECT_NEAR(mark.number("x") + mark.number("width") / 2.0, width * 0.75, 0.6);

    // A share outside zero to one is drawn at the nearer end; one that is not a number as none.
    EXPECT_NEAR(parsedChart(cellBar(1.7)).ofClass("c-fill").front().number("width"), width, 0.11);
    EXPECT_EQ(parsedChart(cellBar(-0.3)).count("c-fill"), 0u);
    EXPECT_EQ(parsedChart(cellBar(kNotANumber, kNotANumber)).count("c-fill"), 0u);
    EXPECT_EQ(parsedChart(cellBar(kNotANumber, kNotANumber)).count("c-mark"), 0u);
}

// ---- The stages.

TEST(ReportCharts, TheTimelineHasARowPerStageAndABarPerStageThatWasTimed)
{
    const TimelineChart chart = timeline();
    const Parsed parsed = parsedChart(timelineChart(chart));
    // A row per stage: its name, its icon in its state's class, its value, and one element to point at.
    QStringList names;
    QStringList values;
    std::size_t timed = 0;
    double total = 0.0;
    for (const ChartStage& stage : chart.stages)
    {
        names << stage.name;
        values << stage.valueText;
        timed += stage.durationMs.has_value() ? 1u : 0u;
        total += stage.durationMs.value_or(0.0);
    }
    EXPECT_EQ(parsed.texts("c-name"), names);
    EXPECT_EQ(parsed.texts("c-val"), values);
    EXPECT_EQ(parsed.count("c-ico"), chart.stages.size());
    const std::vector<Element> rows = parsed.ofClass("c-hit");
    ASSERT_EQ(rows.size(), chart.stages.size());
    EXPECT_EQ(rows.front().attribute("tabindex").toStdString(), "0");
    EXPECT_TRUE(rows[2].attribute("data-tip").startsWith(QStringLiteral("Dark\n")));

    // A bar per stage that was timed, and a small hollow one for the stage that was reused; the skipped one has
    // none.
    const std::vector<Element> barsDrawn = parsed.ofClass("c-stagebar");
    ASSERT_EQ(barsDrawn.size(), timed + 1u);
    EXPECT_EQ(parsed.count("is-reused"), 2u);
    EXPECT_EQ(parsed.ofClass("c-ico")[1].hasClass("is-reused"), true);
    EXPECT_TRUE(parsed.ofClass("c-ico")[5].hasClass("is-partial"));
    EXPECT_TRUE(parsed.ofClass("c-ico").back().hasClass("is-ran"));
    EXPECT_TRUE(parsed.ofClass("c-name").front().hasClass("is-minor"));

    // The stages follow one another on the axis: each bar starts where the one before ended, and their widths
    // are in proportion to their times.
    std::vector<Element> timedBars;
    for (const Element& bar : barsDrawn)
    {
        if (!bar.hasClass("is-reused"))
        {
            timedBars.push_back(bar);
        }
    }
    ASSERT_EQ(timedBars.size(), timed);
    const double scale = timedBars[2].number("width") / 52000.0;
    for (std::size_t index = 1; index < timedBars.size(); ++index)
    {
        const double previousEnd = timedBars[index - 1].number("x") + timedBars[index - 1].number("width");
        // A stage too short to see is drawn a little wider than its time, so the next may start inside it.
        EXPECT_NEAR(timedBars[index].number("x"), previousEnd, 3.1);
    }
    EXPECT_NEAR(timedBars[3].number("width"), 190000.0 * scale, 0.3);

    // The axis is in round times from zero that span the run.
    const QStringList ticks = parsed.texts("c-tick");
    EXPECT_TRUE(ticks.contains(QStringLiteral("0")));
    EXPECT_TRUE(ticks.contains(chart.stageHeading));
    EXPECT_TRUE(ticks.contains(chart.valueHeading));
    static const QRegularExpression kMinutes(QStringLiteral("^([0-9]+) m$"));
    double lastMinutes = 0.0;
    int minuteTicks = 0;
    for (const QString& tick : ticks)
    {
        const QRegularExpressionMatch match = kMinutes.match(tick);
        if (match.hasMatch())
        {
            lastMinutes = (std::max)(lastMinutes, match.captured(1).toDouble());
            ++minuteTicks;
        }
    }
    EXPECT_GE(minuteTicks, 2);
    EXPECT_GE(lastMinutes * 60000.0, total);
    EXPECT_LT((lastMinutes - 5.0) * 60000.0, total);
}

TEST(ReportCharts, TheTimelineOfARunThatStoppedShowsWhereItStopped)
{
    TimelineChart chart = timeline();
    chart.stages.resize(4);
    ChartStage failed;
    failed.name = QStringLiteral("Lights stacking");
    failed.state = ChartStageState::Failed;
    failed.durationMs = 20000.0;
    failed.valueText = QStringLiteral("failed");
    ChartStage cancelled = failed;
    cancelled.state = ChartStageState::Cancelled;
    ChartStage notReached;
    notReached.name = QStringLiteral("Lights drizzle");
    notReached.state = ChartStageState::NotReached;
    notReached.valueText = QStringLiteral("not reached");
    chart.stages.push_back(failed);
    chart.stages.push_back(cancelled);
    chart.stages.push_back(notReached);
    const Parsed parsed = parsedChart(timelineChart(chart));
    const std::vector<Element> icons = parsed.ofClass("c-ico");
    ASSERT_EQ(icons.size(), 7u);
    EXPECT_TRUE(icons[4].hasClass("is-failed"));
    EXPECT_TRUE(icons[5].hasClass("is-cancelled"));
    EXPECT_TRUE(icons[6].hasClass("is-notreached"));
    // A state is told by its icon's shape as well as its class: the stage that ran, the one that failed and the
    // one not reached are three shapes.
    QStringList shapes;
    for (const std::size_t index : {std::size_t{2}, std::size_t{4}, std::size_t{6}})
    {
        const QString shape = icons[index].attribute("d");
        EXPECT_FALSE(shape.isEmpty());
        if (!shapes.contains(shape))
        {
            shapes << shape;
        }
    }
    EXPECT_EQ(shapes.size(), 3);
    // The failed stage has its bar; the one not reached has none.
    std::size_t failedBars = 0;
    for (const Element& bar : parsed.ofClass("c-stagebar"))
    {
        failedBars += bar.hasClass("is-failed") ? 1u : 0u;
        EXPECT_FALSE(bar.hasClass("is-notreached"));
    }
    EXPECT_EQ(failedBars, 1u);
    EXPECT_TRUE(parsed.texts("c-val").contains(QStringLiteral("not reached")));
}

TEST(ReportCharts, ATimelineWithNoTimeStillNamesItsStages)
{
    TimelineChart chart;
    chart.description = described();
    ChartStage reused;
    reused.name = QStringLiteral("Lights stacking");
    reused.state = ChartStageState::Reused;
    reused.valueText = QStringLiteral("reused");
    chart.stages = {reused, reused};
    const Parsed parsed = parsedChart(timelineChart(chart));
    EXPECT_FALSE(parsed.root().hasClass("is-empty"));
    EXPECT_EQ(parsed.texts("c-name").size(), 2);
    EXPECT_EQ(parsed.count("c-stagebar"), 2u);
}

TEST(ReportCharts, TheTimelineOfAShortRunIsMarkedInSecondsOrMilliseconds)
{
    TimelineChart chart;
    chart.description = described();
    ChartStage quick;
    quick.name = QStringLiteral("Lights stacking");
    quick.durationMs = 420.0;
    chart.stages = {quick};
    const QStringList milliseconds = parsedChart(timelineChart(chart)).texts("c-tick");
    EXPECT_TRUE(milliseconds.contains(QStringLiteral("500 ms"))) << milliseconds.join(QLatin1Char('|')).toStdString();

    chart.stages.front().durationMs = 42000.0;
    const QStringList seconds = parsedChart(timelineChart(chart)).texts("c-tick");
    EXPECT_TRUE(seconds.contains(QStringLiteral("30 s"))) << seconds.join(QLatin1Char('|')).toStdString();

    chart.stages.front().durationMs = 5.0 * 3600.0 * 1000.0;
    const QStringList hours = parsedChart(timelineChart(chart)).texts("c-tick");
    EXPECT_TRUE(hours.contains(QStringLiteral("2 h"))) << hours.join(QLatin1Char('|')).toStdString();
}

// ---- Plain bars, and the runs of the folder.

TEST(ReportCharts, TheBarsAreInProportionWithAValueAtEachEnd)
{
    const BarsChart chart = bars();
    const Parsed parsed = parsedChart(barsChart(chart));
    const std::vector<Element> drawn = parsed.ofClass("c-bar");
    ASSERT_EQ(drawn.size(), chart.bars.size());
    // The longest bar is the largest value; the others are in proportion to it.
    const double longest = drawn[0].number("width");
    EXPECT_NEAR(drawn[1].number("width") / longest, 20.8 / 21.3, 0.01);
    EXPECT_NEAR(drawn[2].number("width") / longest, 1.9 / 21.3, 0.01);
    // A bar too short to see is still drawn.
    EXPECT_GE(drawn.back().number("width"), 2.0);
    QStringList values;
    for (const ChartBar& bar : chart.bars)
    {
        values << bar.valueText;
    }
    EXPECT_EQ(parsed.texts("c-val"), values);
    const std::vector<Element> rows = parsed.ofClass("c-hit");
    ASSERT_EQ(rows.size(), chart.bars.size());
    EXPECT_EQ(rows.front().attribute("tabindex").toStdString(), "0");
    // A label too long for its column is cut short and ends in an ellipsis; the tip has it whole.
    const QStringList labels = parsed.texts("c-lab");
    ASSERT_EQ(labels.size(), static_cast<qsizetype>(chart.bars.size()));
    EXPECT_EQ(labels[0], chart.bars[0].label);
    EXPECT_TRUE(labels[4].endsWith(QChar(0x2026))) << labels[4].toStdString();
    EXPECT_LT(labels[4].size(), chart.bars[4].label.size());
    EXPECT_TRUE(rows[4].attribute("data-tip").startsWith(chart.bars[4].label));
}

TEST(ReportCharts, ABarWithoutANumberIsARowWithNoBar)
{
    BarsChart chart;
    chart.description = described();
    chart.bars = {{QStringLiteral("Masters"), 4.0, QStringLiteral("4 GB"), {}},
                  {QStringLiteral("Maps"), kNotANumber, QStringLiteral("not measured"), {}},
                  {QStringLiteral("Archive"), 0.0, QStringLiteral("0 B"), {}}};
    const Parsed parsed = parsedChart(barsChart(chart));
    EXPECT_EQ(parsed.count("c-bar"), 1u);
    EXPECT_EQ(parsed.texts("c-val").size(), 3);

    // Bars that are all zero draw none, and divide by nothing.
    chart.bars = {{QStringLiteral("Masters"), 0.0, QStringLiteral("0 B"), {}}};
    const Parsed zero = parsedChart(barsChart(chart));
    EXPECT_EQ(zero.count("c-bar"), 0u);
    EXPECT_EQ(zero.texts("c-val"), QStringList({QStringLiteral("0 B")}));
}

TEST(ReportCharts, TheRunsAreAColumnEachWithThisRunFilledAndAStoppedRunACross)
{
    const RunsChart chart = runs();
    const Parsed parsed = parsedChart(runsChart(chart));
    const std::vector<Element> earlier = parsed.ofClass("c-colprev");
    const std::vector<Element> now = parsed.ofClass("c-colnow");
    EXPECT_EQ(earlier.size(), 3u);
    ASSERT_EQ(now.size(), 1u);
    // The run that stopped has no column: a cross stands on the axis in its place.
    EXPECT_EQ(parsed.count("c-x"), 1u);
    const std::vector<Element> hits = parsed.ofClass("c-hit");
    ASSERT_EQ(hits.size(), chart.runs.size());
    EXPECT_EQ(hits[2].attribute("data-tip"), QStringLiteral("15 Jul, 09:12\nThe run stopped"));
    // This run's figure stands over its column in the heavier text; the earlier runs' in the lighter.
    EXPECT_EQ(parsed.texts("c-val"), QStringList({chart.runs.back().valueText}));
    const QStringList ticks = parsed.texts("c-tick");
    EXPECT_TRUE(ticks.contains(chart.runs.front().valueText));
    EXPECT_TRUE(ticks.contains(QStringLiteral("16 Jul")));
    EXPECT_TRUE(ticks.contains(QStringLiteral("09:42")));
    // The columns are as tall as their figures against the largest.
    EXPECT_NEAR(earlier.front().number("height") / now.front().number("height"), 2.1 / 7.2, 0.02);
}

TEST(ReportCharts, RunsThatAllStoppedAreAllCrosses)
{
    RunsChart chart;
    chart.description = described();
    chart.runs.assign(3, ChartRun{});
    const Parsed parsed = parsedChart(runsChart(chart));
    EXPECT_EQ(parsed.count("c-x"), 3u);
    EXPECT_EQ(parsed.count("c-colprev") + parsed.count("c-colnow"), 0u);

    // Twenty runs still fit: every run has its column, and the names under them are thinned, not overlapped.
    chart.runs.clear();
    for (int index = 0; index < 20; ++index)
    {
        ChartRun run;
        run.label = {QStringLiteral("%1 Jul").arg(index + 1), QStringLiteral("09:00")};
        run.value = 1.0 + index;
        run.valueText = QString::number(index + 1);
        run.thisRun = index == 19;
        chart.runs.push_back(run);
    }
    const Parsed twenty = parsedChart(runsChart(chart));
    EXPECT_EQ(twenty.count("c-colprev") + twenty.count("c-colnow"), 20u);
    EXPECT_LT(twenty.texts("c-tick").size(), 40);
    EXPECT_TRUE(twenty.texts("c-tick").contains(QStringLiteral("20 Jul")));
}

// ---- The legend.

TEST(ReportCharts, TheLegendHasAMarkAndItsWordsPerEntry)
{
    const std::vector<SampleLight> lights = sampleLights(5, 34, 2);
    const std::vector<ChartLegendItem> items = stateLegendOf(lights);
    ASSERT_EQ(items.size(), 5u);
    const Parsed states = parse(chartLegend(items));
    ASSERT_TRUE(states.error.isEmpty()) << states.error.toStdString();
    EXPECT_EQ(states.root().name.toStdString(), "div");
    EXPECT_TRUE(states.root().hasClass("legend"));
    EXPECT_EQ(states.count("c-key"), 5u);
    EXPECT_EQ(states.ofClass("c-key")[1].hasClass("s-registration"), true);
    std::size_t entries = 0;
    std::size_t figures = 0;
    for (const Element& element : states.elements)
    {
        entries += element.name == QStringLiteral("span") ? 1u : 0u;
        figures += element.name == QStringLiteral("b") ? 1u : 0u;
        if (element.name == QStringLiteral("svg"))
        {
            EXPECT_EQ(element.attribute("aria-hidden").toStdString(), "true");
        }
    }
    EXPECT_EQ(entries, 5u);
    EXPECT_EQ(figures, 5u);

    // A night's mark in the legend is the mark the dither chart draws that night in.
    const Parsed nights = parse(chartLegend(nightLegend(6)));
    ASSERT_TRUE(nights.error.isEmpty()) << nights.error.toStdString();
    const Parsed dither = parsedChart(ditherChart(ditherOf(sampleLights(3, 36, 6), 6)));
    QStringList drawn;
    for (const Element& mark : dither.ofClass("c-n"))
    {
        if (!drawn.contains(mark.attribute("class")))
        {
            drawn << mark.attribute("class");
        }
    }
    QStringList explained;
    for (const Element& mark : nights.ofClass("c-n"))
    {
        explained << mark.attribute("class");
    }
    EXPECT_EQ(explained, drawn);

    // A dot and a cross, as the charts draw a light used and one left out.
    const Parsed marks =
        parse(chartLegend({{ChartLegendItem::Mark::Dot, ChartState::Used, 0, QStringLiteral("Used"), {}},
                           {ChartLegendItem::Mark::Cross,
                            ChartState::ExcludedOnRestack,
                            0,
                            QStringLiteral("Excluded on the restack"),
                            {}}}));
    ASSERT_TRUE(marks.error.isEmpty()) << marks.error.toStdString();
    EXPECT_EQ(marks.count("c-dot"), 1u);
    ASSERT_EQ(marks.count("c-x"), 1u);
    EXPECT_TRUE(marks.ofClass("c-x").front().hasClass("s-restack"));

    EXPECT_TRUE(chartLegend({}).isEmpty());
}

// ---- The page of samples.

TEST(ReportCharts, ThePageOfSamplesIsOneWellFormedDocument)
{
    const QString page = samplePage();
    const Parsed parsed = parse(page);
    ASSERT_TRUE(parsed.error.isEmpty()) << parsed.error.toStdString();
    std::size_t charts = 0;
    for (const Element& element : parsed.elements)
    {
        charts += element.name == QStringLiteral("svg") && element.hasClass("chart") ? 1u : 0u;
    }
    EXPECT_EQ(charts, sampleCharts().size() - 1u);
    // Nothing on the page reaches outside it.
    EXPECT_FALSE(page.contains(QStringLiteral("http:")));
    EXPECT_FALSE(page.contains(QStringLiteral("https:")));
    EXPECT_FALSE(page.contains(QStringLiteral("src=")));
}

// Writes the page of samples into the folder AFTERNIGHT_WRITE_REPORT_SAMPLES names, for a person to look at in a
// browser. Skipped without it.
TEST(ReportChartSamples, WritesThePageOfEveryChart)
{
    const QString folder = qEnvironmentVariable("AFTERNIGHT_WRITE_REPORT_SAMPLES").trimmed();
    if (folder.isEmpty())
    {
        GTEST_SKIP() << "set AFTERNIGHT_WRITE_REPORT_SAMPLES to a folder to write the page of chart samples into";
    }
    ASSERT_TRUE(QDir().mkpath(folder)) << folder.toStdString();
    QFile file(QDir(folder).filePath(QStringLiteral("report_charts.html")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate)) << file.fileName().toStdString();
    const QByteArray page = samplePage().toUtf8();
    ASSERT_EQ(file.write(page), page.size());
    file.close();
    std::cout << "[report-samples] " << QDir::toNativeSeparators(file.fileName()).toStdString() << '\n';
}

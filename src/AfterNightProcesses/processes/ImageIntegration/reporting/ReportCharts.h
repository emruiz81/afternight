// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The charts of the integration report: each a function from plain series to one inline SVG element.
///
/// A chart is drawn when the report is written, so the document needs no script and no network to show it. Nothing
/// here reads a run or the report model: a function takes the values, the texts and the tips it is to draw and
/// returns markup. The rules every chart keeps:
///
/// - **It says no word of its own.** Every title, label and tip arrives in its input, so the report's words stay in
///   one place. The only texts a chart makes are the numbers on its axes.
/// - **It names no colour.** Marks carry CSS classes (`c-line`, `c-dot`, `c-x`, a state's `s-...`), and the report's
///   stylesheet gives them their colours in each theme. The markup holds no colour literal and no `style`.
/// - **Its ticks are round numbers that span the data** (`chartTicks`).
/// - **A state is never told by colour alone.** A light the stacks left out is a cross, a night a shape, a stage's
///   state an icon.
/// - **It is well-formed XML that is also valid inside an HTML document:** attributes quoted, text escaped, an
///   element with no content closed in its own tag. A character XML forbids is replaced by U+FFFD.
/// - **Nothing to draw is said, not drawn wrong.** With no value to show, a chart is a short SVG holding the
///   sentence its description gives for that case. A value that is not a number is left out of lines, marks and
///   scales, never drawn at zero.
/// - **A tip is one attribute.** An element a reader can point at carries `data-tip`: the tip's heading and its
///   lines, separated by line feeds. The report's script shows it; without the script the values are still in the
///   report's tables.
/// - **A dense chart keeps its markup small.** With more than `kChartDenseAbove` lights a chart draws its lines and
///   its crosses, and neither a mark nor a hover band per light; a cross then carries its light's tip itself.

#include "AfterNightProcessesExport.h"

#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace ImageIntegration::Reporting
{
/// The number of lights above which a chart is dense: it draws no mark and no hover band per light.
inline constexpr std::size_t kChartDenseAbove = 300;

/// What became of a light, as a chart marks it. A light that was used is a dot; every other state is a cross whose
/// class names the state, so the stylesheet can colour the states apart.
enum class ChartState
{
    /// Combined into its master. Class `s-used`.
    Used,

    /// Left out by the user, in the lights' list or from the stack alone. Class `s-user`.
    ExcludedByUser,

    /// Its registration failed. Class `s-registration`.
    RegistrationFailed,

    /// Dropped by the stack on its own. Class `s-stack`.
    LeftOutByStack,

    /// Excluded when the stack was made a second time. Class `s-restack`.
    ExcludedOnRestack,

    /// Its master was not made. Class `s-notstacked`.
    NotStacked
};

/// How a stage of the run ended, as the timeline marks it: each state has an icon of its own shape.
enum class ChartStageState
{
    /// The stage ran to its end. Class `is-ran`.
    Ran,

    /// The stage ran and some of its work failed. Class `is-partial`.
    Partial,

    /// Everything the stage makes was still valid. Class `is-reused`.
    Reused,

    /// The stage failed. Class `is-failed`.
    Failed,

    /// The run was cancelled inside the stage. Class `is-cancelled`.
    Cancelled,

    /// The run stopped before the stage. Class `is-notreached`.
    NotReached,

    /// The run had no work for the stage. Class `is-skipped`.
    Skipped
};

/// What a reader is shown on pointing at a mark: a heading and the lines under it.
struct ChartTip
{
    QString heading;
    QStringList lines;

    /// Whether there is nothing to show.
    [[nodiscard]] bool isEmpty() const
    {
        return heading.isEmpty() && lines.isEmpty();
    }
};

/// What a chart is, in words: read to a person who cannot see it, and said in its place when it has nothing to draw.
struct ChartDescription
{
    /// What the chart shows, as its `aria-label`.
    QString label;

    /// The sentence the chart holds when there is no value to draw.
    QString whenEmpty;
};

/// One axis of a chart.
struct ChartAxis
{
    /// The axis's title, with its unit: `FWHM (px)`. May be empty.
    QString title;

    /// A value the axis reaches down to and one it reaches up to, whatever the data. The axis always spans the
    /// data; these widen it, as for a weight that is read against zero and one.
    std::optional<double> lowest;
    std::optional<double> highest;
};

/// One mark of a chart that places its marks by two values.
struct ChartPoint
{
    /// The mark's place on each axis. A point with a value that is not a number is not drawn.
    double x = std::numeric_limits<double>::quiet_NaN();
    double y = std::numeric_limits<double>::quiet_NaN();

    ChartState state = ChartState::Used;
    ChartTip tip;
};

/// The marks that belong together and are told from the other series by shape and colour.
struct ChartSeries
{
    /// The series's name: a night.
    QString name;

    std::vector<ChartPoint> points;
};

// ---- Figures a chart needs before it is drawn.

/// Round numbers that span a range: multiples of a step of 1, 2, 2.5 or 5 times a power of ten, from the last one
/// at or under the lowest value to the first at or over the highest.
///
/// @param lowest the lowest value to span.
/// @param highest the highest value to span.
/// @param steps how many steps to aim for; the result has at least two ticks and seldom more than `steps + 2`.
/// @return the ticks, ascending. Equal values get ticks around them; a bound that is not a number gives zero and one.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<double> chartTicks(double lowest, double highest, int steps);

/// The five figures a range chart draws of one group's values.
struct ChartRange
{
    double lowest = 0.0;
    double lowerQuartile = 0.0;
    double median = 0.0;
    double upperQuartile = 0.0;
    double highest = 0.0;
};

/// The range, the quartiles and the median of some values. The median of an even count is the mean of the two in
/// the middle, as the report's medians are; a quartile lies between its two neighbours in proportion.
///
/// @param values the values; one that is not a number is left out.
/// @return the figures, or nothing when no value is a number.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<ChartRange> chartRange(std::vector<double> values);

/// One bar of a distribution: how many values lie from `low` up to `high`.
struct ChartBin
{
    double low = 0.0;
    double high = 0.0;
    std::uint64_t count = 0;
    ChartTip tip;
};

/// Values counted into bins of equal width. Without stated bounds the bins span round numbers around the values
/// (`chartTicks`); a value on the top edge is counted in the last bin, one outside stated bounds in the nearest.
///
/// @param values the values; one that is not a number is left out.
/// @param count how many bins.
/// @param lowest the first bin's lower edge, when the caller states it.
/// @param highest the last bin's upper edge, when the caller states it.
/// @return the bins, their tips empty for the caller to fill; none when `count` is not positive or no value is a
///     number.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::vector<ChartBin> chartBins(const std::vector<double>& values, int count,
                                                                      std::optional<double> lowest = std::nullopt,
                                                                      std::optional<double> highest = std::nullopt);

// ---- The nights: every light at the time it was taken.

/// One light of the conditions chart.
struct ConditionsLight
{
    /// Which night it was taken in, as an index into `ConditionsChart::nights`.
    int night = 0;

    /// Which group it belongs to, as an index into `ConditionsChart::groups`.
    int group = 0;

    /// When its exposure began, in minutes after the midnight that begins its night's date: 20:00 is 1200, and
    /// 02:00 of the morning after is 1560. Not a number when the light states no time.
    double startMinutes = std::numeric_limits<double>::quiet_NaN();

    /// How long its exposure was, in minutes.
    double exposureMinutes = 0.0;

    /// Its value of each measure, in the order of `ConditionsChart::measures`; not a number where it states none.
    std::vector<double> values;

    ChartState state = ChartState::Used;
    ChartTip tip;
};

/// How the conditions chart lays its lights out.
enum class ConditionsLayout
{
    /// One column of panels per night, on a shared clock axis.
    ByClock,

    /// One column of panels, the lights in the order they are given.
    ByCaptureOrder
};

/// The measures of every light of a run against the time each was taken.
struct ConditionsChart
{
    ChartDescription description;

    /// One row of panels per measure. The axes' bounds are not read: each measure spans its values.
    std::vector<ChartAxis> measures;

    /// The nights' names, in order.
    QStringList nights;

    /// The groups' names. A group's consecutive lights are a band of time with its name over it.
    QStringList groups;

    /// The lights, in capture order.
    std::vector<ConditionsLight> lights;

    /// The chart's width, in the units its lengths are given in; the report shows it at this width or scrolls it.
    int width = 1080;
};

/// How `conditionsChart` lays a chart out: by the clock while there are at most four nights and every light states
/// its time and a night of the list, and otherwise in capture order.
[[nodiscard]] AFTERNIGHTPROCESSES_API ConditionsLayout conditionsLayout(const ConditionsChart& chart);

/// The conditions of the nights: a row of panels per measure, each light a mark at its time joined to its group's
/// other lights, a cross on each light left out. A light left out that states no value of a measure carries its
/// cross on that panel's axis.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString conditionsChart(const ConditionsChart& chart);

// ---- Lights used and left out.

/// One part of a segmented bar.
struct ChartSegment
{
    ChartState state = ChartState::Used;

    /// How much of the bar it takes. A segment with nothing is not drawn.
    double value = 0.0;

    /// The value as the report prints it, set under the segment where there is room.
    QString valueText;

    ChartTip tip;
};

/// The lights of a run as one bar, a segment per state.
struct SegmentedBar
{
    ChartDescription description;
    std::vector<ChartSegment> segments;
    int width = 600;
};

/// One bar divided in proportion to its segments' values.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString segmentedBar(const SegmentedBar& bar);

// ---- A measure's range, group by group.

/// One row of a range chart.
struct ChartRangeRow
{
    /// The group's name, at the row's left.
    QString label;

    ChartRange range;

    /// The median as the report prints it, at the row's right.
    QString valueText;

    ChartTip tip;
};

/// The range, the middle half and the median of one measure for each of several groups.
struct RangeChart
{
    ChartDescription description;
    ChartAxis axis;
    std::vector<ChartRangeRow> rows;
    int width = 460;
};

/// A row per group on one axis: a line for the whole range, a bar for the middle half, a dot for the median.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString rangeChart(const RangeChart& chart);

// ---- One measure against another.

/// One mark per light, placed by two measures.
struct ScatterChart
{
    ChartDescription description;
    ChartAxis xAxis;
    ChartAxis yAxis;
    std::vector<ChartPoint> points;
    int width = 460;
    int height = 300;
};

/// A dot per light that was used and a cross per light left out. A light that lacks either value is not drawn.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString scatterChart(const ScatterChart& chart);

// ---- How a measure is distributed.

/// How many lights lie in each range of a measure.
struct DistributionChart
{
    ChartDescription description;

    /// The measure's axis. Its bounds are not read: the bins state the range.
    ChartAxis axis;

    /// The bins, ascending and adjoining (`chartBins`).
    std::vector<ChartBin> bins;

    int width = 460;
    int height = 220;
};

/// A bar per bin over a count axis, the tallest with its count over it.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString distributionChart(const DistributionChart& chart);

// ---- A master's lights, one by one.

/// One measure of a master's lights.
struct ChartMeasure
{
    /// The measure's name with its unit, over its layer.
    QString title;

    /// The measure of each light, in the order of `FrameMeasureChart::lights`; not a number where a light states
    /// none.
    std::vector<double> values;

    /// A value drawn as a line across the layer, the median of the lights used, and the words set on it.
    std::optional<double> reference;
    QString referenceLabel;
};

/// One light of a master.
struct ChartLight
{
    ChartState state = ChartState::Used;

    /// The light's tip, which states every measure: the chart has one hover band per light for all its layers.
    ChartTip tip;
};

/// A place along a chart's lights that is named: where a night begins.
struct ChartMarker
{
    /// The index of the first light from which the name holds.
    std::size_t index = 0;

    QString label;
};

/// Several measures over one master's lights, in capture order, in one SVG.
struct FrameMeasureChart
{
    ChartDescription description;

    /// A name no other chart of the document has: the master's slug. The chart's gradient is named after it, so two
    /// charts of one document never share an id.
    QString id;

    /// One layer per measure, each with an axis of its own. Layer `n` is a group of class `c-layer c-layer-n`; the
    /// stylesheet shows one layer at a time, the first when printing.
    std::vector<ChartMeasure> measures;

    /// The lights, in capture order.
    std::vector<ChartLight> lights;

    /// Where each night begins, ascending by index.
    std::vector<ChartMarker> nights;

    /// What stands at the right end of the axis: the last light's time.
    QString lastLabel;

    int width = 760;
    int height = 230;
};

/// A line over a wash per measure, a dot at each light used, a cross at each light left out, and one set of hover
/// bands for every layer. A light with no value of a measure breaks that layer's line; left out, it carries its
/// cross on the axis. A dense chart draws its lines and its crosses alone, without the wash.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString frameMeasureChart(const FrameMeasureChart& chart);

// ---- Where each light sat against the reference.

/// The shift of each light against its registration reference, a series per night.
struct DitherChart
{
    ChartDescription description;
    ChartAxis xAxis;
    ChartAxis yAxis;

    /// One series per night. Night `n` is drawn in the shape and the class `chartLegend` gives night `n`.
    std::vector<ChartSeries> nights;

    /// The chart's width; it is as tall as its square plot needs.
    int size = 300;
};

/// A mark per light on a square plot whose two axes share one scale, with the reference at its centre.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString ditherChart(const DitherChart& chart);

// ---- The samples of a master.

/// How the samples of one channel are spread, as the report model holds it.
struct SampleChannel
{
    /// The channel's name, set over its panel; empty for a master of one channel, whose panel needs none.
    QString name;

    /// The lower edge of the first bin and the upper edge of the last, both above zero.
    double lowest = 0.0;
    double highest = 0.0;

    /// The samples in each bin; the bins are of equal width on a logarithmic axis.
    std::vector<std::uint64_t> counts;

    /// The channel's median, marked by a line, and the words set beside it.
    std::optional<double> median;
    QString medianLabel;
};

/// The distribution of a master's samples on a logarithmic axis.
struct SampleDistributionChart
{
    ChartDescription description;

    /// The value axis. Its bounds are not read: the channels state their range.
    ChartAxis axis;

    /// One panel per channel, on one axis.
    std::vector<SampleChannel> channels;

    int width = 420;

    /// The height of one channel's panel.
    int panelHeight = 130;
};

/// An outline per channel whose height is the square root of each bin's count, so the faint end stays visible.
/// A channel with no sample, or whose bounds are not above zero and ascending, is left out.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString sampleDistributionChart(const SampleDistributionChart& chart);

// ---- Coverage.

/// How many lights contributed to a pixel: the least, the median and the most, on one bar from zero.
struct CoverageBar
{
    ChartDescription description;
    double least = 0.0;
    double median = 0.0;
    double most = 0.0;

    /// Words set under the bar, spread from its left end to its right.
    QStringList notes;

    ChartTip tip;
    int width = 320;
};

/// A bar from zero to the most covered pixel, filled from the least covered one, with a mark at the median.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString coverageBar(const CoverageBar& bar);

// ---- A figure's share, inside a table cell.

/// A small bar that shows a figure as a share of its best or of its ideal. It is decoration beside the figure the
/// cell states, so it is hidden from a reader who cannot see it.
///
/// @param fraction the share, from zero to one; a share outside is drawn at the nearer end, one that is not a
///     number as none.
/// @param mark a second share drawn as a line across the bar, when there is one.
/// @return the SVG.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString cellBar(double fraction, std::optional<double> mark = std::nullopt);

// ---- The stages of the run.

/// One row of the timeline.
struct ChartStage
{
    /// The stage's name, and a line under it: its groups.
    QString name;
    QString note;

    ChartStageState state = ChartStageState::Ran;

    /// Whether the row is the run's own work around the stages - preparation, the report - drawn recessive.
    bool minor = false;

    /// How long it took; nothing when it was not timed. A row begins where the rows before it ended.
    std::optional<double> durationMs;

    /// What stands at the row's right: its time, or its state in a word.
    QString valueText;

    ChartTip tip;
};

/// The stages of a run on one time axis.
struct TimelineChart
{
    ChartDescription description;

    /// The headings of the name column and of the value column.
    QString stageHeading;
    QString valueHeading;

    std::vector<ChartStage> stages;
    int width = 1000;
};

/// A row per stage: its state's icon, its name, a bar where it lies in the run's time, and its value.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString timelineChart(const TimelineChart& chart);

// ---- Plain bars.

/// One bar.
struct ChartBar
{
    QString label;

    /// The bar's length against the longest; not a number draws none.
    double value = 0.0;

    /// The value as the report prints it, at the bar's end.
    QString valueText;

    ChartTip tip;
};

/// Bars from one baseline, in one hue.
struct BarsChart
{
    ChartDescription description;
    std::vector<ChartBar> bars;
    int width = 460;

    /// The width kept for the labels; a longer label is cut short and ends in an ellipsis.
    int labelWidth = 150;
};

/// A row per bar: its label, the bar, and its value at the right.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString barsChart(const BarsChart& chart);

// ---- The runs of the folder.

/// One run.
struct ChartRun
{
    /// What stands under the column, a line each: the run's date, its time.
    QStringList label;

    /// The figure; nothing for a run that stopped, which is a cross on the axis.
    std::optional<double> value;

    /// The figure as the report prints it, over the column.
    QString valueText;

    /// Whether this is the run the report is of, drawn in the series colour.
    bool thisRun = false;

    ChartTip tip;
};

/// One figure over the runs of an output folder, oldest first.
struct RunsChart
{
    ChartDescription description;
    std::vector<ChartRun> runs;
    int width = 320;
    int height = 170;
};

/// A column per run, as tall as its figure against the largest.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString runsChart(const RunsChart& chart);

// ---- What the marks mean.

/// One entry of a legend.
struct ChartLegendItem
{
    /// The mark the entry explains.
    enum class Mark
    {
        /// A swatch of a state's colour, as a segmented bar fills it.
        Swatch,

        /// The dot of a light that was used.
        Dot,

        /// The cross of a light left out, in its state's class.
        Cross,

        /// The mark of a night, in the shape and class `ditherChart` draws that night in.
        Night
    };

    Mark mark = Mark::Swatch;

    /// The state of a swatch or a cross.
    ChartState state = ChartState::Used;

    /// The night's index, for a night's mark.
    int night = 0;

    QString label;

    /// A figure after the label; may be empty.
    QString value;
};

/// The legend of a chart: the one function here that returns HTML, a `div` of class `legend` with an entry per
/// item, each a small SVG mark and its words. It wraps as text does, which a legend inside an SVG could not.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString chartLegend(const std::vector<ChartLegendItem>& items);
} // namespace ImageIntegration::Reporting

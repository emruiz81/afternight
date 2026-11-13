// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ReportChartSamples.h"

#include "processes/ImageIntegration/reporting/ReportFormat.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <utility>

using namespace ImageIntegration::Reporting;

namespace ReportChartSamples
{
namespace
{
constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();

/// A fixed sequence of values from zero up to one: the same on every host, so a chart of them never changes.
class Sequence
{
public:
    explicit Sequence(std::uint32_t seed) : m_state(seed) {}

    [[nodiscard]] double next()
    {
        m_state = m_state * 1664525u + 1013904223u;
        return static_cast<double>(m_state >> 8) / static_cast<double>(1u << 24);
    }

    /// A value from minus one up to one.
    [[nodiscard]] double signedNext()
    {
        return next() * 2.0 - 1.0;
    }

private:
    std::uint32_t m_state;
};

[[nodiscard]] QString groupName(int index)
{
    static const QStringList kNames = {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("G"),
                                       QStringLiteral("B"), QStringLiteral("Ha")};
    return index < kNames.size() ? kNames.at(index) : QStringLiteral("F%1").arg(index + 1);
}

[[nodiscard]] QString nightName(int night)
{
    return QStringLiteral("%1 July").arg(14 + night);
}

[[nodiscard]] QString clockText(double minutes)
{
    const int whole = static_cast<int>(std::floor(minutes));
    return QStringLiteral("%1:%2")
        .arg((whole / 60) % 24, 2, 10, QLatin1Char('0'))
        .arg(whole % 60, 2, 10, QLatin1Char('0'));
}

[[nodiscard]] QString stateName(ChartState state)
{
    switch (state)
    {
    case ChartState::Used:
        return QStringLiteral("Used");
    case ChartState::ExcludedByUser:
        return QStringLiteral("Excluded by you");
    case ChartState::RegistrationFailed:
        return QStringLiteral("Registration failed");
    case ChartState::LeftOutByStack:
        return QStringLiteral("Left out by the stack");
    case ChartState::ExcludedOnRestack:
        return QStringLiteral("Excluded on the restack");
    case ChartState::NotStacked:
        return QStringLiteral("Not stacked");
    }
    return {};
}

[[nodiscard]] QString fixed(double value, int decimals)
{
    return std::isfinite(value) ? QString::number(value, 'f', decimals) : noValueText();
}

[[nodiscard]] QString lightHeading(const SampleLight& light)
{
    return QStringLiteral("%1 · light %2 · %3").arg(light.group).arg(light.number).arg(clockText(light.startMinutes));
}

[[nodiscard]] ChartTip lightTip(const SampleLight& light)
{
    ChartTip tip;
    tip.heading = lightHeading(light);
    tip.lines = {QStringLiteral("FWHM %1 px").arg(fixed(light.fwhm, 2)),
                 QStringLiteral("Eccentricity %1").arg(fixed(light.eccentricity, 2)),
                 QStringLiteral("%1 stars").arg(fixed(light.stars, 0)),
                 QStringLiteral("SNR %1").arg(fixed(light.snr, 1)),
                 QStringLiteral("Weight %1").arg(fixed(light.weight, 3)),
                 stateName(light.state)};
    return tip;
}

[[nodiscard]] double medianOf(std::vector<double> values)
{
    const std::optional<ChartRange> range = chartRange(std::move(values));
    return range.has_value() ? range->median : kNotANumber;
}

[[nodiscard]] ChartDescription describe(const QString& label)
{
    return {label, nothingToDraw()};
}

/// The escape every text of the page goes through.
[[nodiscard]] QString escaped(const QString& text)
{
    QString out;
    for (const QChar character : text)
    {
        switch (character.unicode())
        {
        case u'&':
            out += QStringLiteral("&amp;");
            break;
        case u'<':
            out += QStringLiteral("&lt;");
            break;
        case u'>':
            out += QStringLiteral("&gt;");
            break;
        case u'"':
            out += QStringLiteral("&quot;");
            break;
        default:
            out += character;
        }
    }
    return out;
}

} // namespace

std::vector<SampleLight> sampleLights(const int groups, const int perGroup, const int nights)
{
    std::vector<SampleLight> lights;
    Sequence sequence(20260714u);
    const int perNight = (std::max)(1, perGroup / (std::max)(1, nights));
    std::vector<int> numbers(static_cast<std::size_t>(groups), 0);
    for (int night = 0; night < nights; ++night)
    {
        double cursor = 20.0 * 60.0 + 30.0 + 7.0 * night;
        // Where the mount pointed on the night, against the reference.
        const double framingX = 9.0 * std::cos(1.7 * night);
        const double framingY = 7.0 * std::sin(2.3 * night + 0.4);
        for (int group = 0; group < groups; ++group)
        {
            const bool lastNight = night == nights - 1;
            const int count = lastNight ? perGroup - perNight * (nights - 1) : perNight;
            const double exposure = perNight > 200 ? 0.5 : (group == 0 ? 2.0 : (group == 4 ? 5.0 : 3.0));
            for (int index = 0; index < count; ++index)
            {
                SampleLight light;
                light.group = groupName(group);
                light.groupIndex = group;
                light.night = night;
                light.number = ++numbers[static_cast<std::size_t>(group)];
                light.startMinutes = cursor;
                light.exposureMinutes = exposure;
                cursor += exposure + 0.25;

                const double hour = (light.startMinutes - 20.0 * 60.0) / 60.0;
                light.fwhm = 2.7 + 0.25 * group * 0.3 + 0.35 * std::sin(hour * 0.9 + night) + 0.22 * sequence.next();
                light.eccentricity = 0.34 + 0.05 * (light.fwhm - 2.7) + 0.08 * sequence.next();
                light.stars = std::round(980.0 - 190.0 * (light.fwhm - 2.7) - 60.0 * group + 40.0 * sequence.next());
                light.snr = 31.0 - 4.0 * (light.fwhm - 2.7) + 2.0 * sequence.next();
                light.background =
                    0.0118 * (1.0 + 0.2 * group) * (1.0 + 0.06 * hour * (night + 1)) + 0.0004 * sequence.next();
                light.weight = std::clamp(1.0 - 0.55 * (light.fwhm - 2.6) + 0.05 * sequence.signedNext(), 0.05, 1.0);
                light.rejected = 0.45 + 0.3 * sequence.next();
                light.shiftX = framingX + 6.0 * sequence.signedNext();
                light.shiftY = framingY + 6.0 * sequence.signedNext();

                if (group == 0 && night == 0 && count >= 4)
                {
                    const int cloudFrom = (std::max)(3, count / 3);
                    const int cloudLights = (std::max)(1, count / 8);
                    if (index == 1)
                    {
                        // Excluded in the lights' list: nothing was measured of it.
                        light.state = ChartState::ExcludedByUser;
                        light.fwhm = light.eccentricity = light.stars = light.snr = kNotANumber;
                        light.background = light.weight = light.rejected = kNotANumber;
                        light.shiftX = light.shiftY = kNotANumber;
                    }
                    else if (index == 2)
                    {
                        light.state = ChartState::LeftOutByStack;
                        light.weight = light.rejected = kNotANumber;
                    }
                    else if (count >= 6 && index >= cloudFrom && index < cloudFrom + cloudLights)
                    {
                        // Cloud: few stars, soft, a bright sky, and no registration.
                        light.state = ChartState::RegistrationFailed;
                        light.fwhm = 5.2 + 0.6 * sequence.next();
                        light.eccentricity = 0.55 + 0.1 * sequence.next();
                        light.stars = std::round(90.0 + 60.0 * sequence.next());
                        light.snr = 9.0 + 3.0 * sequence.next();
                        light.background *= 1.9;
                        light.weight = light.rejected = kNotANumber;
                        light.shiftX = light.shiftY = kNotANumber;
                    }
                    else if (index == count - 1)
                    {
                        light.state = ChartState::ExcludedOnRestack;
                        light.rejected = 6.4;
                        light.weight = kNotANumber;
                    }
                }
                lights.push_back(light);
            }
        }
    }
    return lights;
}

std::size_t leftOutOf(const std::vector<SampleLight>& lights)
{
    return static_cast<std::size_t>(std::count_if(
        lights.begin(), lights.end(), [](const SampleLight& light) { return light.state != ChartState::Used; }));
}

std::vector<SampleLight> lightsOfGroup(const std::vector<SampleLight>& lights, const int groupIndex)
{
    std::vector<SampleLight> mine;
    std::copy_if(lights.begin(), lights.end(), std::back_inserter(mine),
                 [groupIndex](const SampleLight& light) { return light.groupIndex == groupIndex; });
    return mine;
}

QString nothingToDraw()
{
    return QStringLiteral("Nothing was measured to draw here.");
}

ConditionsChart conditionsOf(const std::vector<SampleLight>& lights, const int nights)
{
    ConditionsChart chart;
    chart.description =
        describe(QStringLiteral("Sharpness, star count and sky background of every light, at the time it was taken"));
    chart.measures = {{QStringLiteral("FWHM (px)"), {}, {}},
                      {QStringLiteral("Stars detected"), {}, {}},
                      {QStringLiteral("Sky background (× the group's median)"), {}, {}}};
    for (int night = 0; night < nights; ++night)
    {
        chart.nights << nightName(night);
    }
    std::map<int, std::vector<double>> backgrounds;
    int groups = 0;
    for (const SampleLight& light : lights)
    {
        backgrounds[light.groupIndex].push_back(light.background);
        groups = (std::max)(groups, light.groupIndex + 1);
    }
    for (int group = 0; group < groups; ++group)
    {
        chart.groups << groupName(group);
    }
    std::map<int, double> medians;
    for (auto& [group, values] : backgrounds)
    {
        medians[group] = medianOf(values);
    }
    for (const SampleLight& light : lights)
    {
        ConditionsLight entry;
        entry.night = light.night;
        entry.group = light.groupIndex;
        entry.startMinutes = light.startMinutes;
        entry.exposureMinutes = light.exposureMinutes;
        const double relative = light.background / medians[light.groupIndex];
        entry.values = {light.fwhm, light.stars, relative};
        entry.state = light.state;
        entry.tip.heading = lightHeading(light);
        entry.tip.lines = {QStringLiteral("FWHM %1 px").arg(fixed(light.fwhm, 2)),
                           QStringLiteral("%1 stars").arg(fixed(light.stars, 0)),
                           QStringLiteral("Background × %1").arg(fixed(relative, 2)), stateName(light.state)};
        chart.lights.push_back(entry);
    }
    return chart;
}

SegmentedBar usedAndLeftOutOf(const std::vector<SampleLight>& lights)
{
    SegmentedBar bar;
    bar.description = describe(QStringLiteral("Lights used and left out"));
    bar.width = 420;
    const std::array<ChartState, 6> order = {
        ChartState::Used,           ChartState::RegistrationFailed, ChartState::ExcludedByUser,
        ChartState::LeftOutByStack, ChartState::ExcludedOnRestack,  ChartState::NotStacked};
    for (const ChartState state : order)
    {
        const auto count = std::count_if(lights.begin(), lights.end(),
                                         [state](const SampleLight& light) { return light.state == state; });
        ChartSegment segment;
        segment.state = state;
        segment.value = static_cast<double>(count);
        segment.valueText = formatCount(count);
        segment.tip.heading = stateName(state);
        segment.tip.lines = {
            QStringLiteral("%1 lights").arg(formatCount(count)),
            QStringLiteral("%1 % of %2")
                .arg(fixed(lights.empty() ? 0.0 : 100.0 * static_cast<double>(count) / lights.size(), 1))
                .arg(formatCount(static_cast<qint64>(lights.size())))};
        bar.segments.push_back(segment);
    }
    return bar;
}

std::vector<ChartLegendItem> stateLegendOf(const std::vector<SampleLight>& lights)
{
    std::vector<ChartLegendItem> items;
    for (const ChartSegment& segment : usedAndLeftOutOf(lights).segments)
    {
        if (segment.value <= 0.0)
        {
            continue;
        }
        ChartLegendItem item;
        item.mark = ChartLegendItem::Mark::Swatch;
        item.state = segment.state;
        item.label = stateName(segment.state);
        item.value = segment.valueText;
        items.push_back(item);
    }
    return items;
}

RangeChart rangeOf(const std::vector<SampleLight>& lights, const int groups)
{
    RangeChart chart;
    chart.description = describe(QStringLiteral("FWHM by filter: range, middle half and median"));
    chart.axis.title = QStringLiteral("FWHM (px), lights used");
    for (int group = 0; group < groups; ++group)
    {
        std::vector<double> values;
        for (const SampleLight& light : lights)
        {
            if (light.groupIndex == group && light.state == ChartState::Used)
            {
                values.push_back(light.fwhm);
            }
        }
        const std::optional<ChartRange> range = chartRange(values);
        if (!range.has_value())
        {
            continue;
        }
        ChartRangeRow row;
        row.label = groupName(group);
        row.range = *range;
        row.valueText = fixed(range->median, 2);
        row.tip.heading = QStringLiteral("%1 · FWHM").arg(row.label);
        row.tip.lines = {
            QStringLiteral("Median %1 px").arg(fixed(range->median, 2)),
            QStringLiteral("Middle half %1 to %2").arg(fixed(range->lowerQuartile, 2), fixed(range->upperQuartile, 2)),
            QStringLiteral("Range %1 to %2").arg(fixed(range->lowest, 2), fixed(range->highest, 2))};
        chart.rows.push_back(row);
    }
    return chart;
}

ScatterChart scatterOf(const std::vector<SampleLight>& lights)
{
    ScatterChart chart;
    chart.description = describe(QStringLiteral("Eccentricity against FWHM, one mark per light"));
    chart.xAxis.title = QStringLiteral("FWHM (px)");
    chart.yAxis.title = QStringLiteral("Eccentricity");
    for (const SampleLight& light : lights)
    {
        ChartPoint point;
        point.x = light.fwhm;
        point.y = light.eccentricity;
        point.state = light.state;
        point.tip.heading = lightHeading(light);
        point.tip.lines = {QStringLiteral("FWHM %1 px").arg(fixed(light.fwhm, 2)),
                           QStringLiteral("Eccentricity %1").arg(fixed(light.eccentricity, 2)), stateName(light.state)};
        chart.points.push_back(point);
    }
    return chart;
}

DistributionChart weightsOf(const std::vector<SampleLight>& lights)
{
    DistributionChart chart;
    chart.description = describe(QStringLiteral("Distribution of the weights the stacks used"));
    chart.axis.title = QStringLiteral("Weight");
    std::vector<double> weights;
    for (const SampleLight& light : lights)
    {
        if (light.state == ChartState::Used)
        {
            weights.push_back(light.weight);
        }
    }
    chart.bins = chartBins(weights, 10, 0.0, 1.0);
    for (ChartBin& bin : chart.bins)
    {
        bin.tip.heading = QStringLiteral("Weight %1 to %2").arg(fixed(bin.low, 1), fixed(bin.high, 1));
        bin.tip.lines = {QStringLiteral("%1 lights").arg(formatCount(static_cast<qint64>(bin.count)))};
    }
    return chart;
}

FrameMeasureChart measuresOf(const std::vector<SampleLight>& lights, const QString& id)
{
    FrameMeasureChart chart;
    chart.description = describe(QStringLiteral("Each measure of the master's lights, in capture order"));
    chart.id = id;

    struct Measure
    {
        QString title;
        double SampleLight::* member;
        int decimals;
    };
    const std::vector<Measure> measures = {
        {QStringLiteral("FWHM (px)"), &SampleLight::fwhm, 2},
        {QStringLiteral("Eccentricity"), &SampleLight::eccentricity, 2},
        {QStringLiteral("Stars detected"), &SampleLight::stars, 0},
        {QStringLiteral("SNR"), &SampleLight::snr, 1},
        {QStringLiteral("Background"), &SampleLight::background, 4},
        {QStringLiteral("Weight"), &SampleLight::weight, 3},
        {QStringLiteral("Rejected (%)"), &SampleLight::rejected, 2},
    };
    for (const Measure& measure : measures)
    {
        ChartMeasure layer;
        layer.title = measure.title;
        std::vector<double> used;
        for (const SampleLight& light : lights)
        {
            layer.values.push_back(light.*measure.member);
            if (light.state == ChartState::Used)
            {
                used.push_back(light.*measure.member);
            }
        }
        const double median = medianOf(used);
        if (std::isfinite(median))
        {
            layer.reference = median;
            layer.referenceLabel = QStringLiteral("median %1").arg(fixed(median, measure.decimals));
        }
        chart.measures.push_back(layer);
    }

    int night = -1;
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const SampleLight& light = lights[index];
        chart.lights.push_back({light.state, lightTip(light)});
        if (light.night != night)
        {
            night = light.night;
            chart.nights.push_back(
                {index, QStringLiteral("%1 · %2").arg(nightName(night), clockText(light.startMinutes))});
        }
    }
    if (!lights.empty())
    {
        chart.lastLabel = clockText(lights.back().startMinutes);
    }
    return chart;
}

DitherChart ditherOf(const std::vector<SampleLight>& lights, const int nights)
{
    DitherChart chart;
    chart.description = describe(QStringLiteral("Shift of each light against the reference, in pixels"));
    chart.xAxis.title = QStringLiteral("Shift in x (px)");
    chart.yAxis.title = QStringLiteral("Shift in y (px)");
    for (int night = 0; night < nights; ++night)
    {
        ChartSeries series;
        series.name = nightName(night);
        for (const SampleLight& light : lights)
        {
            if (light.night != night)
            {
                continue;
            }
            ChartPoint point;
            point.x = light.shiftX;
            point.y = light.shiftY;
            point.state = light.state;
            point.tip.heading = lightHeading(light);
            point.tip.lines = {QStringLiteral("Shift %1, %2 px").arg(fixed(light.shiftX, 1), fixed(light.shiftY, 1))};
            series.points.push_back(point);
        }
        chart.nights.push_back(series);
    }
    return chart;
}

std::vector<ChartLegendItem> nightLegend(const int nights)
{
    std::vector<ChartLegendItem> items;
    for (int night = 0; night < nights; ++night)
    {
        ChartLegendItem item;
        item.mark = ChartLegendItem::Mark::Night;
        item.night = night;
        item.label = nightName(night);
        items.push_back(item);
    }
    return items;
}

SampleDistributionChart samplesOf(const int channels)
{
    SampleDistributionChart chart;
    chart.description = describe(QStringLiteral("Distribution of the master's samples"));
    chart.axis.title = QStringLiteral("Sample value, fraction of full scale (logarithmic)");
    chart.panelHeight = channels > 1 ? 96 : 130;
    static const QStringList kNames = {QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")};
    for (int channel = 0; channel < channels; ++channel)
    {
        SampleChannel samples;
        samples.name = channels > 1 ? kNames.at(channel % kNames.size()) : QString();
        samples.lowest = 0.001;
        samples.highest = 1.0;
        const double centre = 0.018 * (1.0 + 0.35 * channel);
        const int bins = 96;
        for (int bin = 0; bin < bins; ++bin)
        {
            const double value = samples.lowest * std::pow(samples.highest / samples.lowest, (bin + 0.5) / bins);
            const double distance = std::log10(value / centre);
            // The sky's bump, and the long faint tail of stars and nebula above it.
            const double sky = 4.0e6 * std::exp(-distance * distance / (2.0 * 0.07 * 0.07));
            const double tail = distance > 0.0 ? 2.0e5 * std::exp(-distance * 3.2) : 0.0;
            samples.counts.push_back(static_cast<std::uint64_t>(sky + tail));
        }
        samples.median = centre * 1.02;
        samples.medianLabel = QStringLiteral("median %1").arg(formatFigure(*samples.median));
        chart.channels.push_back(samples);
    }
    return chart;
}

CoverageBar coverage()
{
    CoverageBar bar;
    bar.description = describe(QStringLiteral("Lights per pixel: least, median and most"));
    bar.least = 41.0;
    bar.median = 58.0;
    bar.most = 60.0;
    bar.notes = {QStringLiteral("0"), QStringLiteral("least 41"), QStringLiteral("median 58")};
    bar.tip.heading = QStringLiteral("Coverage");
    bar.tip.lines = {QStringLiteral("Least covered pixel: 41 lights"), QStringLiteral("Median: 58 lights"),
                     QStringLiteral("Most: 60 lights")};
    return bar;
}

namespace
{
[[nodiscard]] ChartStage stage(const QString& name, const QString& note, ChartStageState state,
                               std::optional<double> durationMs, const QString& value, bool minor = false)
{
    ChartStage row;
    row.name = name;
    row.note = note;
    row.state = state;
    row.minor = minor;
    row.durationMs = durationMs;
    row.valueText = value;
    row.tip.heading = name;
    row.tip.lines = {value};
    if (!note.isEmpty())
    {
        row.tip.lines << note;
    }
    return row;
}

[[nodiscard]] TimelineChart timelineOf(bool stopped)
{
    TimelineChart chart;
    chart.description = describe(QStringLiteral("The stages of the run on one time axis"));
    chart.stageHeading = QStringLiteral("Stage");
    chart.valueHeading = QStringLiteral("Time");
    auto timed = [](double seconds) { return formatDurationMs(static_cast<qint64>(seconds * 1000.0)); };
    chart.stages = {
        stage(QStringLiteral("Preparation"), {}, ChartStageState::Ran, 2100.0, timed(2.1), true),
        stage(QStringLiteral("Bias"), QStringLiteral("1 group"), ChartStageState::Reused, std::nullopt,
              QStringLiteral("reused")),
        stage(QStringLiteral("Dark"), QStringLiteral("3 groups"), ChartStageState::Ran, 38000.0, timed(38.0)),
        stage(QStringLiteral("Flat"), QStringLiteral("4 groups"), ChartStageState::Ran, 52000.0, timed(52.0)),
        stage(QStringLiteral("Lights calibration"), QStringLiteral("5 groups · 174 lights"), ChartStageState::Ran,
              190000.0, timed(190.0)),
        stage(QStringLiteral("Lights registration"), QStringLiteral("2 groups · 170 of 174 registered"),
              ChartStageState::Partial, 245000.0, timed(245.0)),
        stage(QStringLiteral("Lights normalization"), QStringLiteral("5 groups"), ChartStageState::Ran, 41000.0,
              timed(41.0)),
    };
    if (stopped)
    {
        chart.stages.push_back(stage(QStringLiteral("Lights stacking"), QStringLiteral("3 of 5 groups, then it failed"),
                                     ChartStageState::Failed, 150000.0, QStringLiteral("failed · 2 m 30 s")));
        chart.stages.push_back(stage(QStringLiteral("Lights drizzle"), QStringLiteral("1 group"),
                                     ChartStageState::NotReached, std::nullopt, QStringLiteral("not reached")));
    }
    else
    {
        chart.stages.push_back(stage(QStringLiteral("Lights stacking"), QStringLiteral("5 groups and the RGB"),
                                     ChartStageState::Ran, 380000.0, timed(380.0)));
        chart.stages.push_back(stage(QStringLiteral("Lights drizzle"), {}, ChartStageState::Skipped, std::nullopt,
                                     QStringLiteral("no work")));
    }
    chart.stages.push_back(
        stage(QStringLiteral("Report and archive"), {}, ChartStageState::Ran, 2400.0, timed(2.4), true));
    return chart;
}
} // namespace

TimelineChart timeline()
{
    return timelineOf(false);
}

BarsChart bars()
{
    BarsChart chart;
    chart.description = describe(QStringLiteral("What the run wrote to disk, by kind"));
    chart.labelWidth = 190;
    const std::vector<std::pair<QString, double>> rows = {
        {QStringLiteral("Registered lights"), 21.3},
        {QStringLiteral("Calibrated lights"), 20.8},
        {QStringLiteral("Masters"), 1.9},
        {QStringLiteral("Calibration masters"), 1.4},
        {QStringLiteral("Rejection and quality maps of every master"), 0.62},
        {QStringLiteral("The run's archive"), 0.05},
    };
    for (const auto& [label, gigabytes] : rows)
    {
        ChartBar bar;
        bar.label = label;
        bar.value = gigabytes;
        bar.valueText = QStringLiteral("%1 GB").arg(fixed(gigabytes, 2));
        bar.tip.heading = label;
        bar.tip.lines = {bar.valueText};
        chart.bars.push_back(bar);
    }
    return chart;
}

RunsChart runs()
{
    RunsChart chart;
    chart.description = describe(QStringLiteral("Integration time over the runs of this folder"));
    struct Run
    {
        const char* day;
        const char* time;
        std::optional<double> hours;
    };
    const std::vector<Run> earlier = {{"2 Jul", "23:10", 2.1},
                                      {"9 Jul", "08:02", 3.4},
                                      {"15 Jul", "09:12", std::nullopt},
                                      {"15 Jul", "10:40", 4.9},
                                      {"16 Jul", "09:42", 7.2}};
    for (std::size_t index = 0; index < earlier.size(); ++index)
    {
        const Run& run = earlier[index];
        ChartRun column;
        column.label = {QLatin1String(run.day), QLatin1String(run.time)};
        column.value = run.hours;
        column.thisRun = index + 1 == earlier.size();
        column.tip.heading = QStringLiteral("%1, %2").arg(QLatin1String(run.day), QLatin1String(run.time));
        if (run.hours.has_value())
        {
            column.valueText = formatDurationMs(static_cast<qint64>(*run.hours * 3600.0 * 1000.0));
            column.tip.lines = {column.valueText};
        }
        else
        {
            column.tip.lines = {QStringLiteral("The run stopped")};
        }
        chart.runs.push_back(column);
    }
    return chart;
}

std::vector<SampleChart> everySampleChart()
{
    std::vector<SampleChart> charts;
    const auto add = [&charts](const QString& title, const QString& note, const QString& markup, bool wide = false,
                               const QString& legend = {}) {
        SampleChart chart;
        chart.title = title;
        chart.note = note;
        chart.markup = markup;
        chart.legend = legend;
        chart.wide = wide;
        charts.push_back(chart);
    };

    const std::vector<SampleLight> session = sampleLights(5, 34, 2);
    const std::vector<SampleLight> luminance = lightsOfGroup(session, 0);
    const std::vector<SampleLight> sixNights = sampleLights(3, 36, 6);
    const std::vector<SampleLight> dense = sampleLights(1, 2000, 6);
    const std::vector<SampleLight> few = sampleLights(1, 9, 1);

    add(QStringLiteral("The nights"), QStringLiteral("170 lights, five filters, two nights; cloud over the first"),
        conditionsChart(conditionsOf(session, 2)), true);
    add(QStringLiteral("The nights, in capture order"), QStringLiteral("108 lights over six nights"),
        conditionsChart(conditionsOf(sixNights, 6)), true);
    add(QStringLiteral("The nights of a large group"), QStringLiteral("2,000 lights: lines and crosses only"),
        conditionsChart(conditionsOf(dense, 6)), true);

    add(QStringLiteral("Used and left out"), QStringLiteral("One bar, a segment per state"),
        segmentedBar(usedAndLeftOutOf(session)), false, chartLegend(stateLegendOf(session)));
    add(QStringLiteral("Sharpness by filter"), QStringLiteral("Range, middle half and median of FWHM"),
        rangeChart(rangeOf(session, 5)));
    add(QStringLiteral("Sharp and round"), QStringLiteral("Eccentricity against FWHM"),
        scatterChart(scatterOf(session)), false,
        chartLegend(
            {{ChartLegendItem::Mark::Dot, ChartState::Used, 0, QStringLiteral("Used"), {}},
             {ChartLegendItem::Mark::Cross,
              ChartState::RegistrationFailed,
              0,
              QStringLiteral("Registration failed"),
              {}},
             {ChartLegendItem::Mark::Cross, ChartState::LeftOutByStack, 0, QStringLiteral("Left out by the stack"), {}},
             {ChartLegendItem::Mark::Cross,
              ChartState::ExcludedOnRestack,
              0,
              QStringLiteral("Excluded on the restack"),
              {}}}));
    add(QStringLiteral("Weights the stacks used"), QStringLiteral("Ten bins from zero to one"),
        distributionChart(weightsOf(session)));
    add(QStringLiteral("Sharp and round, 2,000 lights"), QStringLiteral("No hover target per light"),
        scatterChart(scatterOf(dense)));

    add(QStringLiteral("A master's lights"), QStringLiteral("68 luminance lights, seven measures in one SVG"),
        frameMeasureChart(measuresOf(luminance, QStringLiteral("l"))), true);
    add(QStringLiteral("A master of nine lights"), QStringLiteral("One night"),
        frameMeasureChart(measuresOf(few, QStringLiteral("few"))), true);
    add(QStringLiteral("A master of 2,000 lights"), QStringLiteral("No mark and no hover band per light"),
        frameMeasureChart(measuresOf(dense, QStringLiteral("dense"))), true);

    add(QStringLiteral("Where each light sat"), QStringLiteral("Two nights"), ditherChart(ditherOf(luminance, 2)),
        false, chartLegend(nightLegend(2)));
    add(QStringLiteral("Where each light sat, six nights"), QStringLiteral("A shape per night"),
        ditherChart(ditherOf(sixNights, 6)), false, chartLegend(nightLegend(6)));

    add(QStringLiteral("A master's samples"), QStringLiteral("One channel"), sampleDistributionChart(samplesOf(1)));
    add(QStringLiteral("A colour master's samples"), QStringLiteral("A panel per channel"),
        sampleDistributionChart(samplesOf(3)));
    add(QStringLiteral("Coverage"), QStringLiteral("Lights per pixel"), coverageBar(coverage()));
    add(QStringLiteral("A share in a cell"), QStringLiteral("0.82 of the best; 0.64 against an ideal of 0.9"),
        cellBar(0.82) + QStringLiteral(" ") + cellBar(0.64, 0.9));

    add(QStringLiteral("Stage by stage"), QStringLiteral("A run that completed"), timelineChart(timeline()), true);
    add(QStringLiteral("Stage by stage, a run that stopped"), QStringLiteral("A stage failed; one was not reached"),
        timelineChart(timelineOf(true)), true);
    add(QStringLiteral("Written to disk"), QStringLiteral("Plain bars"), barsChart(bars()));
    add(QStringLiteral("Over the runs of the folder"),
        QStringLiteral("This run filled; a cross is a run that failed or was cancelled"), runsChart(runs()));

    FrameMeasureChart nothing;
    nothing.description = describe(QStringLiteral("A chart with nothing to draw"));
    nothing.id = QStringLiteral("nothing");
    nothing.width = 420;
    add(QStringLiteral("Nothing to draw"), QStringLiteral("An empty series"), frameMeasureChart(nothing));
    return charts;
}

QString samplePageStylesheet()
{
    return reportStylesheet() + QStringLiteral(R"css(
body{padding:24px}
h1{font-size:22px;margin:0 0 4px}
.page-lead{color:var(--text-2);margin:0 0 20px;font-size:14px}
.page{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px;max-width:1400px;margin:0 auto}
.page>.card{margin-top:0}
.page>.card.wide{grid-column:1/-1}
.page>.card h2{font-size:14px;margin:0;font-weight:600}
.page>.card .note{font-size:12.5px;color:var(--text-3);margin:2px 0 12px}
.page .scrollx>svg.chart-measures{max-width:900px}
@media (max-width:900px){.page{grid-template-columns:minmax(0,1fr)}body{padding:14px}}
)css");
}

QString samplePage()
{
    QString body;
    int switches = 0;
    for (const SampleChart& chart : everySampleChart())
    {
        body += QStringLiteral("<section class=\"card%1\"><h2>%2</h2><p class=\"note\">%3</p>")
                    .arg(chart.wide ? QStringLiteral(" wide") : QString(), escaped(chart.title), escaped(chart.note));
        const qsizetype layers = chart.markup.count(QStringLiteral("class=\"c-layer c-layer-"));
        if (layers > 1)
        {
            // The switch between a chart's layers: a radio per layer, which needs no script.
            const int number = switches++;
            body += QStringLiteral("<div class=\"fchart\">");
            QString labels;
            for (qsizetype layer = 0; layer < layers; ++layer)
            {
                body += QStringLiteral("<input type=\"radio\" class=\"pick pick-%1\" name=\"pick-%2\" "
                                       "id=\"pick-%2-%1\"%3/>")
                            .arg(layer)
                            .arg(number)
                            .arg(layer == 0 ? QStringLiteral(" checked=\"checked\"") : QString());
                labels += QStringLiteral("<label for=\"pick-%2-%1\">Measure %3</label>")
                              .arg(layer)
                              .arg(number)
                              .arg(layer + 1);
            }
            body += QStringLiteral("<div class=\"switch\">%1</div><div class=\"scrollx\">%2</div></div>")
                        .arg(labels, chart.markup);
        }
        else
        {
            // A wide chart scrolls sideways on a phone; a small one is shown at the width its card has.
            body += QStringLiteral("<div%1>%2</div>")
                        .arg(chart.wide ? QStringLiteral(" class=\"scrollx\"") : QString(), chart.markup);
        }
        body += chart.legend;
        body += QStringLiteral("</section>\n");
    }

    return QStringLiteral("<!DOCTYPE html>\n"
                          "<html lang=\"en\">\n<head>\n"
                          "<meta charset=\"utf-8\"/>\n"
                          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"
                          "<title>Integration report charts</title>\n"
                          "<style>/*<![CDATA[*/\n%1/*]]>*/</style>\n"
                          "<script>//<![CDATA[\n%2//]]></script>\n"
                          "</head>\n<body>\n"
                          "<h1>The charts of the integration report</h1>\n"
                          "<p class=\"page-lead\">Every chart on invented series, under the report's stylesheet. "
                          "Point at a mark for its values.</p>\n"
                          "<div class=\"page\">\n%3</div>\n</body>\n</html>\n")
        .arg(samplePageStylesheet(), reportScript(), body);
}
} // namespace ReportChartSamples

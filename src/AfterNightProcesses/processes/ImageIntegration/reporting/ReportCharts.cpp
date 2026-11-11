// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportCharts.h"

#include "ReportFormat.h"

#include <QByteArray>

#include <algorithm>
#include <array>
#include <cmath>

namespace ImageIntegration::Reporting
{
namespace
{
constexpr QChar kReplacementCharacter{0xFFFD};
constexpr QChar kEllipsis{0x2026};
constexpr QChar kMinusSign{0x2212};

/// The sizes the stylesheet sets the charts' texts in, for judging whether a text fits.
constexpr double kTickFontPx = 10.5;
constexpr double kTitleFontPx = 11.5;
constexpr double kNameFontPx = 12.5;

/// How wide a character is against its font's size, on average.
constexpr double kCharacterWidth = 0.56;

// ---- Text.

/// Text made safe for markup: the five characters XML reserves as references, and a character XML forbids as
/// U+FFFD. In an attribute a tab and a line break are references too, since a parser would otherwise read each as
/// a space.
[[nodiscard]] QString escaped(const QString& text, bool attribute = false)
{
    QString out;
    out.reserve(text.size() + 8);
    for (qsizetype index = 0; index < text.size(); ++index)
    {
        const QChar character = text.at(index);
        const char16_t unit = character.unicode();
        switch (unit)
        {
        case u'&':
            out += QLatin1String("&amp;");
            continue;
        case u'<':
            out += QLatin1String("&lt;");
            continue;
        case u'>':
            out += QLatin1String("&gt;");
            continue;
        case u'"':
            out += QLatin1String("&quot;");
            continue;
        case u'\'':
            out += QLatin1String("&#39;");
            continue;
        case u'\t':
            out += attribute ? QString(QLatin1String("&#9;")) : QString(character);
            continue;
        case u'\n':
            out += attribute ? QString(QLatin1String("&#10;")) : QString(character);
            continue;
        case u'\r':
            out += attribute ? QString(QLatin1String("&#13;")) : QString(character);
            continue;
        default:
            break;
        }
        if (unit < 0x20 || unit == 0xFFFE || unit == 0xFFFF)
        {
            out += kReplacementCharacter;
        }
        else if (character.isHighSurrogate())
        {
            if (index + 1 < text.size() && text.at(index + 1).isLowSurrogate())
            {
                out += character;
                out += text.at(++index);
            }
            else
            {
                out += kReplacementCharacter;
            }
        }
        else if (character.isLowSurrogate())
        {
            out += kReplacementCharacter;
        }
        else
        {
            out += character;
        }
    }
    return out;
}

/// A length or a place, to a tenth. A value that is not a number is never printed: it is zero.
[[nodiscard]] QString num(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("0");
    }
    QString text = QString::number(value, 'f', 1);
    if (text.endsWith(QLatin1String(".0")))
    {
        text.chop(2);
    }
    return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

/// How wide a text is, about, at a font size.
[[nodiscard]] double textWidth(const QString& text, double fontPx)
{
    return static_cast<double>(text.size()) * fontPx * kCharacterWidth;
}

/// A text cut to a width, ending in an ellipsis where it was cut.
[[nodiscard]] QString fitText(const QString& text, double maxWidth, double fontPx)
{
    if (textWidth(text, fontPx) <= maxWidth)
    {
        return text;
    }
    qsizetype kept = static_cast<qsizetype>(std::floor(maxWidth / (fontPx * kCharacterWidth))) - 1;
    kept = std::clamp<qsizetype>(kept, 1, text.size());
    if (text.at(kept - 1).isHighSurrogate())
    {
        --kept;
    }
    return text.left(kept).trimmed() + kEllipsis;
}

/// A tip as its attribute: the heading and the lines, a line feed between them. Nothing for an empty tip.
[[nodiscard]] QString tipAttribute(const ChartTip& tip)
{
    if (tip.isEmpty())
    {
        return {};
    }
    QStringList parts;
    if (!tip.heading.isEmpty())
    {
        parts << tip.heading;
    }
    parts += tip.lines;
    for (QString& part : parts)
    {
        // A line feed separates the parts, so one inside a part is a space.
        part.replace(QLatin1Char('\n'), QLatin1Char(' '));
        part.replace(QLatin1Char('\r'), QLatin1Char(' '));
        part.replace(QLatin1Char('\t'), QLatin1Char(' '));
    }
    return QStringLiteral(" data-tip=\"%1\"").arg(escaped(parts.join(QLatin1Char('\n')), true));
}

[[nodiscard]] QLatin1String stateClass(ChartState state)
{
    switch (state)
    {
    case ChartState::Used:
        return QLatin1String("s-used");
    case ChartState::ExcludedByUser:
        return QLatin1String("s-user");
    case ChartState::RegistrationFailed:
        return QLatin1String("s-registration");
    case ChartState::LeftOutByStack:
        return QLatin1String("s-stack");
    case ChartState::ExcludedOnRestack:
        return QLatin1String("s-restack");
    case ChartState::NotStacked:
        return QLatin1String("s-notstacked");
    }
    return QLatin1String("s-used");
}

[[nodiscard]] QLatin1String stageClass(ChartStageState state)
{
    switch (state)
    {
    case ChartStageState::Ran:
        return QLatin1String("is-ran");
    case ChartStageState::Partial:
        return QLatin1String("is-partial");
    case ChartStageState::Reused:
        return QLatin1String("is-reused");
    case ChartStageState::Failed:
        return QLatin1String("is-failed");
    case ChartStageState::Cancelled:
        return QLatin1String("is-cancelled");
    case ChartStageState::NotReached:
        return QLatin1String("is-notreached");
    case ChartStageState::Skipped:
        return QLatin1String("is-skipped");
    }
    return QLatin1String("is-ran");
}

/// A stage state's icon, drawn in a box of sixteen: a shape per state, so the state is not told by colour alone.
[[nodiscard]] QLatin1String stageIcon(ChartStageState state)
{
    switch (state)
    {
    case ChartStageState::Ran:
        // A tick in a circle.
        return QLatin1String("M8 1.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 0 0 0-13ZM5.2 8.2l2 2l3.6-4");
    case ChartStageState::Partial:
        // A warning triangle.
        return QLatin1String("M8 2L1.6 13.2h12.8L8 2ZM8 6.4v3.2M8 11.4v.2");
    case ChartStageState::Reused:
        // An arrow that comes round again.
        return QLatin1String("M13.5 8a5.5 5.5 0 1 1-1.7-4M13.6 2.2v3h-3");
    case ChartStageState::Failed:
        // A cross in a circle.
        return QLatin1String("M8 1.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 0 0 0-13ZM5.8 5.8l4.4 4.4M10.2 5.8l-4.4 4.4");
    case ChartStageState::Cancelled:
        // A stop square in a circle.
        return QLatin1String("M8 1.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 0 0 0-13ZM5.8 5.8h4.4v4.4h-4.4Z");
    case ChartStageState::NotReached:
        // An empty circle with a point at its centre.
        return QLatin1String("M8 1.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 0 0 0-13ZM8 7.9v.2");
    case ChartStageState::Skipped:
        // A dash in a circle.
        return QLatin1String("M8 1.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 0 0 0-13ZM5 8h6");
    }
    return QLatin1String();
}

// ---- Elements.

enum class Anchor
{
    Start,
    Middle,
    End
};

void appendLine(QString& out, QLatin1String cssClass, double x1, double y1, double x2, double y2)
{
    out += QStringLiteral("<line class=\"%1\" x1=\"%2\" y1=\"%3\" x2=\"%4\" y2=\"%5\"/>")
               .arg(cssClass, num(x1), num(y1), num(x2), num(y2));
}

void appendText(QString& out, QLatin1String cssClass, double x, double y, Anchor anchor, const QString& content)
{
    if (content.isEmpty())
    {
        return;
    }
    const QLatin1String anchored =
        anchor == Anchor::End ? QLatin1String(" text-anchor=\"end\"")
                              : (anchor == Anchor::Middle ? QLatin1String(" text-anchor=\"middle\"") : QLatin1String());
    out += QStringLiteral("<text class=\"%1\" x=\"%2\" y=\"%3\"%4>%5</text>")
               .arg(cssClass, num(x), num(y), anchored, escaped(content));
}

/// A text that reads upward, centred on a point: the title of a vertical axis.
void appendUprightText(QString& out, QLatin1String cssClass, double x, double y, const QString& content)
{
    if (content.isEmpty())
    {
        return;
    }
    out += QStringLiteral("<text class=\"%1\" x=\"%2\" y=\"%3\" text-anchor=\"middle\" "
                          "transform=\"rotate(-90 %2 %3)\">%4</text>")
               .arg(cssClass, num(x), num(y), escaped(content));
}

/// A rectangle a reader can point at. One with `focusable` can be reached with the keyboard as well.
void appendHit(QString& out, double x, double y, double width, double height, const ChartTip& tip, bool focusable)
{
    out += QStringLiteral("<rect class=\"c-hit\" x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\"%5%6/>")
               .arg(num(x), num(y), num(width), num(height),
                    focusable ? QLatin1String(" tabindex=\"0\"") : QLatin1String(), tipAttribute(tip));
}

/// The mark of a light left out: a cross over a ring of the surface's colour, which sets it apart from the line
/// under it. The cross's class names the state; the ring carries the tip where the chart has no hover band.
void appendCross(QString& out, double cx, double cy, double arm, ChartState state, const ChartTip* tip)
{
    out += QStringLiteral("<circle class=\"c-xring\" cx=\"%1\" cy=\"%2\" r=\"%3\"%4/>")
               .arg(num(cx), num(cy), num(arm + 3.0), tip != nullptr ? tipAttribute(*tip) : QString());
    out += QStringLiteral("<path class=\"c-x %1\" d=\"M%2 %3l%4 %4M%5 %3l-%4 %4\"/>")
               .arg(stateClass(state), num(cx - arm), num(cy - arm), num(arm * 2.0), num(cx + arm));
}

/// The opening tag of a chart.
void openChart(QString& out, QLatin1String kind, double width, double height, const ChartDescription& description)
{
    out += QStringLiteral("<svg class=\"chart chart-%1\" viewBox=\"0 0 %2 %3\" role=\"img\" aria-label=\"%4\">")
               .arg(kind, num(width), num(height), escaped(description.label, true));
}

/// A chart with nothing to draw: the sentence its description gives for that, and nothing else.
[[nodiscard]] QString emptyChart(QLatin1String kind, double width, const ChartDescription& description)
{
    const double shown = (std::max)(width, 120.0);
    return QStringLiteral("<svg class=\"chart chart-%1 is-empty\" viewBox=\"0 0 %2 56\" role=\"img\" "
                          "aria-label=\"%3\"><text class=\"c-empty\" x=\"%4\" y=\"32\" text-anchor=\"middle\">%5"
                          "</text></svg>")
        .arg(kind, num(shown), escaped(description.label, true), num(shown / 2.0), escaped(description.whenEmpty));
}

/// A column with a rounded top, standing on a base line.
[[nodiscard]] QString columnPath(double x, double width, double top, double base)
{
    const double radius = (std::max)(0.0, (std::min)({4.0, width / 2.0, base - top}));
    return QStringLiteral("M%1 %2V%3q0 -%4 %4 -%4h%5q%4 0 %4 %4V%2Z")
        .arg(num(x), num(base), num(top + radius), num(radius), num(width - 2.0 * radius));
}

// ---- Scales.

/// The lowest and the highest of the values that are numbers.
struct Extent
{
    double lowest = std::numeric_limits<double>::infinity();
    double highest = -std::numeric_limits<double>::infinity();

    void take(double value)
    {
        if (std::isfinite(value))
        {
            lowest = (std::min)(lowest, value);
            highest = (std::max)(highest, value);
        }
    }

    void take(const std::optional<double>& value)
    {
        if (value.has_value())
        {
            take(*value);
        }
    }

    [[nodiscard]] bool valid() const
    {
        return lowest <= highest;
    }
};

/// A value freed of the noise its arithmetic left: twelve significant digits, and no negative zero.
[[nodiscard]] double tidy(double value)
{
    const double tidied = QByteArray::number(value, 'g', 12).toDouble();
    return tidied == 0.0 ? 0.0 : tidied;
}

[[nodiscard]] std::vector<double> ticksOf(double lowest, double highest, int steps, bool wholeNumbers)
{
    if (!std::isfinite(lowest) || !std::isfinite(highest))
    {
        return {0.0, 1.0};
    }
    if (lowest > highest)
    {
        std::swap(lowest, highest);
    }
    steps = (std::max)(1, steps);
    double span = highest - lowest;
    if (!(span > 0.0))
    {
        // Equal values: a range around them, so they are drawn inside the plot.
        span = lowest != 0.0 ? std::abs(lowest) * 0.2 : 1.0;
        lowest -= span / 2.0;
        highest += span / 2.0;
    }
    const double raw = span / steps;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    static constexpr std::array<double, 5> kMultiples = {1.0, 2.0, 2.5, 5.0, 10.0};
    double step = 10.0 * magnitude;
    for (const double multiple : kMultiples)
    {
        if (wholeNumbers && multiple == 2.5 && magnitude <= 1.0)
        {
            continue;
        }
        if (multiple * magnitude >= raw * (1.0 - 1e-9))
        {
            step = multiple * magnitude;
            break;
        }
    }
    if (wholeNumbers)
    {
        step = (std::max)(1.0, std::round(step));
    }

    const double first = std::floor(lowest / step + 1e-9) * step;
    std::vector<double> ticks;
    for (int index = 0; index < 1000; ++index)
    {
        const double tick = first + step * index;
        ticks.push_back(tidy(tick));
        if (index > 0 && tick >= highest - step * 1e-9)
        {
            break;
        }
    }
    return ticks;
}

/// The decimals a tick needs so that ticks a step apart read apart.
[[nodiscard]] int decimalsOf(double step)
{
    double scaled = std::abs(step);
    for (int decimals = 0; decimals < 9; ++decimals)
    {
        if (std::abs(scaled - std::round(scaled)) < 1e-6 * (std::max)(1.0, scaled))
        {
            return decimals;
        }
        scaled *= 10.0;
    }
    return 9;
}

/// A tick's number: grouped in threes where it is whole, with the minus sign.
[[nodiscard]] QString tickText(double value, int decimals)
{
    if (decimals <= 0)
    {
        return formatCount(std::llround(value));
    }
    const QString magnitude = QString::number(std::abs(value), 'f', decimals);
    // A value that rounds to zero carries no sign.
    const bool negative = value < 0.0 && magnitude.toDouble() != 0.0;
    return negative ? kMinusSign + magnitude : magnitude;
}

/// The ticks of an axis and how its values become places.
struct Scale
{
    std::vector<double> ticks;
    int decimals = 0;

    /// The places of the first tick and of the last.
    double from = 0.0;
    double to = 1.0;

    [[nodiscard]] double at(double value) const
    {
        return from + (to - from) * (value - ticks.front()) / (ticks.back() - ticks.front());
    }

    [[nodiscard]] QString text(double tick) const
    {
        return tickText(tick, decimals);
    }
};

[[nodiscard]] Scale scaleOf(Extent extent, const ChartAxis& axis, int steps, double from, double to,
                            bool wholeNumbers = false)
{
    extent.take(axis.lowest);
    extent.take(axis.highest);
    Scale scale;
    scale.ticks = ticksOf(extent.lowest, extent.highest, steps, wholeNumbers);
    scale.decimals = decimalsOf(scale.ticks[1] - scale.ticks[0]);
    scale.from = from;
    scale.to = to;
    return scale;
}

/// Round times that span a duration, from zero: the steps a clock is read in.
[[nodiscard]] std::vector<double> durationTicks(double totalMs)
{
    if (!(totalMs > 0.0))
    {
        return {0.0};
    }
    static constexpr double kSecond = 1000.0;
    static constexpr double kMinute = 60.0 * kSecond;
    static constexpr double kHour = 60.0 * kMinute;
    static constexpr std::array<double, 27> kSteps = {1.0,
                                                      2.0,
                                                      5.0,
                                                      10.0,
                                                      20.0,
                                                      50.0,
                                                      100.0,
                                                      200.0,
                                                      500.0,
                                                      kSecond,
                                                      2.0 * kSecond,
                                                      5.0 * kSecond,
                                                      10.0 * kSecond,
                                                      15.0 * kSecond,
                                                      30.0 * kSecond,
                                                      kMinute,
                                                      2.0 * kMinute,
                                                      5.0 * kMinute,
                                                      10.0 * kMinute,
                                                      15.0 * kMinute,
                                                      30.0 * kMinute,
                                                      kHour,
                                                      2.0 * kHour,
                                                      3.0 * kHour,
                                                      6.0 * kHour,
                                                      12.0 * kHour,
                                                      24.0 * kHour};
    double step = kSteps.back() * std::ceil(totalMs / (5.0 * kSteps.back()));
    for (const double candidate : kSteps)
    {
        if (totalMs / candidate <= 5.0)
        {
            step = candidate;
            break;
        }
    }
    std::vector<double> ticks;
    for (int index = 0; index < 1000; ++index)
    {
        ticks.push_back(step * index);
        if (ticks.back() >= totalMs)
        {
            break;
        }
    }
    return ticks;
}

/// A round time on the timeline's axis, in the one unit it is whole in.
[[nodiscard]] QString durationTickText(double ms)
{
    const qint64 whole = std::llround(ms);
    if (whole == 0)
    {
        return QStringLiteral("0");
    }
    if (whole % 3600000 == 0)
    {
        return QStringLiteral("%1 h").arg(formatCount(whole / 3600000));
    }
    if (whole % 60000 == 0 && whole < 3600000)
    {
        return QStringLiteral("%1 m").arg(whole / 60000);
    }
    if (whole % 1000 == 0 && whole < 60000)
    {
        return QStringLiteral("%1 s").arg(whole / 1000);
    }
    return formatDurationMs(whole);
}

// ---- The nights' marks.

/// The class of a night's marks: one of two colours and one of four shapes, eight looks before one repeats.
[[nodiscard]] QString nightClass(int night)
{
    const int index = (std::max)(0, night);
    const int colour = (index % 2) ^ ((index / 4) % 2);
    return QStringLiteral("c-n c-n%1 c-shape-%2").arg(colour).arg(index % 4);
}

/// A night's mark at a place: a circle, a square, a diamond or a triangle.
[[nodiscard]] QString nightShape(int night, double x, double y)
{
    switch ((std::max)(0, night) % 4)
    {
    case 0:
        return QStringLiteral("M%1 %2a3.5 3.5 0 1 0 7 0a3.5 3.5 0 1 0 -7 0Z").arg(num(x - 3.5), num(y));
    case 1:
        return QStringLiteral("M%1 %2h6.2v6.2h-6.2Z").arg(num(x - 3.1), num(y - 3.1));
    case 2:
        return QStringLiteral("M%1 %2l4.3 4.3l-4.3 4.3l-4.3 -4.3Z").arg(num(x), num(y - 4.3));
    default:
        return QStringLiteral("M%1 %2l4.2 7.2h-8.4Z").arg(num(x), num(y - 4.4));
    }
}

/// A name made safe to be part of an id: letters, digits, hyphens and underscores.
[[nodiscard]] QString nameToken(const QString& name)
{
    QString token;
    for (const QChar character : name)
    {
        const char16_t unit = character.unicode();
        const bool kept = (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') ||
                          (unit >= u'0' && unit <= u'9') || unit == u'_' || unit == u'-';
        token += kept ? character : QLatin1Char('-');
    }
    return token.isEmpty() ? QStringLiteral("chart") : token;
}

/// A light's value of a measure; not a number where it states none.
[[nodiscard]] double valueAt(const std::vector<double>& values, std::size_t index)
{
    return index < values.size() ? values[index] : std::numeric_limits<double>::quiet_NaN();
}

/// The lines of a series that has gaps: one polyline through each run of at least two points.
void appendLines(QString& out, QLatin1String cssClass, const std::vector<std::pair<double, double>>& points,
                 const std::vector<bool>& present)
{
    QString run;
    int length = 0;
    const auto flush = [&]() {
        if (length >= 2)
        {
            out += QStringLiteral("<polyline class=\"%1\" points=\"%2\"/>").arg(cssClass, run.trimmed());
        }
        run.clear();
        length = 0;
    };
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        if (!present[index])
        {
            flush();
            continue;
        }
        run += QStringLiteral("%1,%2 ").arg(num(points[index].first), num(points[index].second));
        ++length;
    }
    flush();
}
} // namespace

// ---- Figures a chart needs before it is drawn.

std::vector<double> chartTicks(double lowest, double highest, int steps)
{
    return ticksOf(lowest, highest, steps, false);
}

std::optional<ChartRange> chartRange(std::vector<double> values)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double value) { return !std::isfinite(value); }),
                 values.end());
    if (values.empty())
    {
        return std::nullopt;
    }
    std::sort(values.begin(), values.end());
    const auto quantile = [&values](double share) {
        const double place = share * static_cast<double>(values.size() - 1);
        const std::size_t under = static_cast<std::size_t>(std::floor(place));
        const std::size_t over = (std::min)(under + 1, values.size() - 1);
        return values[under] + (values[over] - values[under]) * (place - static_cast<double>(under));
    };
    ChartRange range;
    range.lowest = values.front();
    range.lowerQuartile = quantile(0.25);
    range.median = quantile(0.5);
    range.upperQuartile = quantile(0.75);
    range.highest = values.back();
    return range;
}

std::vector<ChartBin> chartBins(const std::vector<double>& values, int count, std::optional<double> lowest,
                                std::optional<double> highest)
{
    Extent extent;
    for (const double value : values)
    {
        extent.take(value);
    }
    if (count <= 0 || !extent.valid())
    {
        return {};
    }
    const std::vector<double> round = ticksOf(extent.lowest, extent.highest, count, false);
    double low = lowest.has_value() && std::isfinite(*lowest) ? *lowest : round.front();
    double high = highest.has_value() && std::isfinite(*highest) ? *highest : round.back();
    if (!(high > low))
    {
        low = round.front();
        high = round.back();
    }

    std::vector<ChartBin> bins(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        bins[static_cast<std::size_t>(index)].low = tidy(low + (high - low) * index / count);
        bins[static_cast<std::size_t>(index)].high = tidy(low + (high - low) * (index + 1) / count);
    }
    for (const double value : values)
    {
        if (!std::isfinite(value))
        {
            continue;
        }
        const double place = std::floor((value - low) / (high - low) * count);
        const int index = static_cast<int>(std::clamp(place, 0.0, static_cast<double>(count - 1)));
        ++bins[static_cast<std::size_t>(index)].count;
    }
    return bins;
}

// ---- The nights.

ConditionsLayout conditionsLayout(const ConditionsChart& chart)
{
    if (chart.nights.isEmpty() || chart.nights.size() > 4)
    {
        return ConditionsLayout::ByCaptureOrder;
    }
    for (const ConditionsLight& light : chart.lights)
    {
        if (!std::isfinite(light.startMinutes) || light.night < 0 || light.night >= chart.nights.size())
        {
            return ConditionsLayout::ByCaptureOrder;
        }
    }
    return ConditionsLayout::ByClock;
}

QString conditionsChart(const ConditionsChart& chart)
{
    static const QLatin1String kKind("conditions");
    const std::size_t measures = chart.measures.size();
    const std::size_t count = chart.lights.size();
    std::vector<Extent> extents(measures);
    bool anyValue = false;
    for (const ConditionsLight& light : chart.lights)
    {
        for (std::size_t measure = 0; measure < measures; ++measure)
        {
            extents[measure].take(valueAt(light.values, measure));
        }
    }
    for (const Extent& extent : extents)
    {
        anyValue = anyValue || extent.valid();
    }
    if (count == 0 || !anyValue)
    {
        return emptyChart(kKind, chart.width, chart.description);
    }

    const bool byClock = conditionsLayout(chart) == ConditionsLayout::ByClock;
    const bool dense = count > kChartDenseAbove;
    const double left = 48.0;
    const double right = 8.0;
    const double gap = 30.0;
    const double top = 58.0;
    const double panelHeight = 84.0;
    const double panelGap = 32.0;
    const double bottom = 22.0;
    const double width = chart.width;
    const double height = top + panelHeight * measures + panelGap * (measures - 1) + bottom;
    const double bandTop = top - 22.0;
    const double floorY = height - bottom;

    // Where each light lies: its column, the band of the axis it takes, and its mark's place.
    const int columns = byClock ? static_cast<int>(chart.nights.size()) : 1;
    const double columnWidth = (width - left - right - gap * (columns - 1)) / columns;
    const auto columnLeft = [&](int column) { return left + column * (columnWidth + gap); };
    double firstMinute = 0.0;
    double lastMinute = 60.0;
    if (byClock)
    {
        Extent clock;
        for (const ConditionsLight& light : chart.lights)
        {
            clock.take(light.startMinutes);
            clock.take(light.startMinutes + (std::max)(0.0, light.exposureMinutes));
        }
        firstMinute = 60.0 * std::floor(clock.lowest / 60.0);
        lastMinute = (std::max)(firstMinute + 60.0, 60.0 * std::ceil(clock.highest / 60.0));
    }
    struct Placed
    {
        int column = 0;
        double start = 0.0;
        double width = 0.0;
        double centre = 0.0;
    };
    std::vector<Placed> placed(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        const ConditionsLight& light = chart.lights[index];
        Placed& place = placed[index];
        if (byClock)
        {
            const double perMinute = columnWidth / (lastMinute - firstMinute);
            const double exposure = (std::max)(0.0, light.exposureMinutes);
            place.column = light.night;
            place.start = columnLeft(light.night) + (light.startMinutes - firstMinute) * perMinute;
            place.width = (std::max)(1.5, exposure * perMinute);
            place.centre = place.start + exposure * perMinute / 2.0;
        }
        else
        {
            const double step = columnWidth / static_cast<double>(count);
            place.start = left + step * static_cast<double>(index);
            place.width = step;
            place.centre = place.start + step / 2.0;
        }
    }

    // A group's consecutive lights of one night are a run: a band of time, and one line in each panel.
    struct Run
    {
        std::size_t first = 0;
        std::size_t last = 0;
    };
    std::vector<Run> runs;
    for (std::size_t index = 0; index < count; ++index)
    {
        const ConditionsLight& light = chart.lights[index];
        if (!runs.empty() && chart.lights[runs.back().last].group == light.group &&
            chart.lights[runs.back().last].night == light.night)
        {
            runs.back().last = index;
        }
        else
        {
            runs.push_back({index, index});
        }
    }

    QString out;
    openChart(out, kKind, width, height, chart.description);

    // The bands, every other one shaded, each with its group's name where the name fits. Groups taken in turn make
    // too many bands to read, and then none is drawn: the tips name each light's group.
    std::vector<int> runsInColumn(static_cast<std::size_t>(columns), 0);
    for (const Run& run : runs)
    {
        ++runsInColumn[static_cast<std::size_t>(placed[run.first].column)];
    }
    std::vector<int> orderInColumn(static_cast<std::size_t>(columns), 0);
    for (const Run& run : runs)
    {
        const int column = placed[run.first].column;
        const int order = orderInColumn[static_cast<std::size_t>(column)]++;
        if (runsInColumn[static_cast<std::size_t>(column)] > 24)
        {
            continue;
        }
        const double from = placed[run.first].start;
        const double to = placed[run.last].start + placed[run.last].width;
        if (order % 2 == 0)
        {
            out += QStringLiteral("<rect class=\"c-band\" x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\"/>")
                       .arg(num(from), num(bandTop), num(to - from), num(floorY - bandTop));
        }
        const int group = chart.lights[run.first].group;
        const QString name = group >= 0 && group < chart.groups.size() ? chart.groups.at(group) : QString();
        if (textWidth(name, 11.0) <= to - from + 4.0)
        {
            appendText(out, QLatin1String("c-bandname"), (from + to) / 2.0, top - 28.0, Anchor::Middle, name);
        }
    }

    // The nights' names: over each column by the clock, and where each night begins in capture order.
    if (byClock)
    {
        for (int night = 0; night < columns; ++night)
        {
            appendText(out, QLatin1String("c-ptitle"), columnLeft(night) + columnWidth, top - 44.0, Anchor::End,
                       chart.nights.at(night));
        }
    }
    else
    {
        double taken = -std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < count; ++index)
        {
            const int night = chart.lights[index].night;
            if (index > 0 && chart.lights[index - 1].night == night)
            {
                continue;
            }
            const QString name = night >= 0 && night < chart.nights.size() ? chart.nights.at(night) : QString();
            const double x = placed[index].start + (index > 0 ? 4.0 : 0.0);
            if (!name.isEmpty() && x >= taken)
            {
                appendText(out, QLatin1String("c-ptitle"), x, top - 44.0, Anchor::Start, name);
                taken = x + textWidth(name, kTitleFontPx) + 8.0;
            }
        }
    }

    for (std::size_t measure = 0; measure < measures; ++measure)
    {
        const double panelTop = top + (panelHeight + panelGap) * measure;
        const double panelFloor = panelTop + panelHeight;
        appendText(out, QLatin1String("c-lab"), left, panelTop - 8.0, Anchor::Start, chart.measures[measure].title);
        if (!extents[measure].valid())
        {
            for (int column = 0; column < columns; ++column)
            {
                appendLine(out, QLatin1String("c-axis"), columnLeft(column), panelFloor,
                           columnLeft(column) + columnWidth, panelFloor);
            }
            appendText(out, QLatin1String("c-empty"), left + (width - left - right) / 2.0, panelTop + panelHeight / 2.0,
                       Anchor::Middle, chart.description.whenEmpty);
            continue;
        }
        const Scale scale = scaleOf(extents[measure], {}, 2, panelFloor, panelTop);
        for (int column = 0; column < columns; ++column)
        {
            const double x0 = columnLeft(column);
            for (const double tick : scale.ticks)
            {
                appendLine(out, QLatin1String("c-grid"), x0, scale.at(tick), x0 + columnWidth, scale.at(tick));
            }
            appendLine(out, QLatin1String("c-axis"), x0, panelFloor, x0 + columnWidth, panelFloor);
        }
        for (const double tick : scale.ticks)
        {
            appendText(out, QLatin1String("c-tick"), left - 6.0, scale.at(tick) + 3.5, Anchor::End, scale.text(tick));
        }

        for (const Run& run : runs)
        {
            std::vector<std::pair<double, double>> points;
            std::vector<bool> present;
            for (std::size_t index = run.first; index <= run.last; ++index)
            {
                const double value = valueAt(chart.lights[index].values, measure);
                present.push_back(std::isfinite(value));
                points.emplace_back(placed[index].centre, present.back() ? scale.at(value) : panelFloor);
            }
            appendLines(out, QLatin1String("c-line15"), points, present);
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            const ConditionsLight& light = chart.lights[index];
            const double value = valueAt(light.values, measure);
            if (light.state != ChartState::Used)
            {
                // A light left out that states no value is still shown at its time, on the axis.
                appendCross(out, placed[index].centre, std::isfinite(value) ? scale.at(value) : panelFloor, 3.0,
                            light.state, dense ? &light.tip : nullptr);
            }
            else if (!dense && std::isfinite(value))
            {
                out += QStringLiteral("<circle class=\"c-dotS\" cx=\"%1\" cy=\"%2\" r=\"2\"/>")
                           .arg(num(placed[index].centre), num(scale.at(value)));
            }
        }
    }

    if (byClock)
    {
        // The hours, as far apart as their names need.
        const double hours = (lastMinute - firstMinute) / 60.0;
        int hourStep = 12;
        for (const int candidate : {1, 2, 3, 4, 6, 12})
        {
            if (columnWidth / (hours / candidate) >= 46.0)
            {
                hourStep = candidate;
                break;
            }
        }
        const int firstHour = static_cast<int>(std::llround(firstMinute / 60.0));
        for (int column = 0; column < columns; ++column)
        {
            for (int hour = 0; hour <= static_cast<int>(std::llround(hours)); hour += hourStep)
            {
                const double x = columnLeft(column) + columnWidth * hour / hours;
                const int onTheClock = ((firstHour + hour) % 24 + 24) % 24;
                // The hours at a column's two ends are set inside it, so two nights' hours never meet.
                const Anchor anchor =
                    hour == 0 ? Anchor::Start
                              : (x > columnLeft(column) + columnWidth - 20.0 ? Anchor::End : Anchor::Middle);
                appendText(out, QLatin1String("c-tick"), x, height - 6.0, anchor,
                           QStringLiteral("%1:00").arg(onTheClock, 2, 10, QLatin1Char('0')));
            }
        }
    }
    else
    {
        // Where each night after the first begins.
        for (std::size_t index = 1; index < count; ++index)
        {
            if (chart.lights[index - 1].night != chart.lights[index].night)
            {
                appendLine(out, QLatin1String("c-axis"), placed[index].start, bandTop, placed[index].start, floorY);
            }
        }
    }

    if (!dense)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            appendHit(out, placed[index].start, top, placed[index].width, floorY - top, chart.lights[index].tip, false);
        }
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- Lights used and left out.

QString segmentedBar(const SegmentedBar& bar)
{
    static const QLatin1String kKind("segments");
    std::vector<const ChartSegment*> drawn;
    double total = 0.0;
    for (const ChartSegment& segment : bar.segments)
    {
        if (std::isfinite(segment.value) && segment.value > 0.0)
        {
            drawn.push_back(&segment);
            total += segment.value;
        }
    }
    if (drawn.empty())
    {
        return emptyChart(kKind, bar.width, bar.description);
    }

    const double width = bar.width;
    const double gap = 2.0;
    const double barHeight = 16.0;
    const double smallest = 6.0;
    const double usable = width - gap * static_cast<double>(drawn.size() - 1);

    // Each segment in proportion, but none too thin to see or to point at: the thin ones take the least width, and
    // the others share what is left in proportion.
    std::vector<double> widths(drawn.size(), 0.0);
    std::vector<bool> held(drawn.size(), false);
    for (std::size_t pass = 0; pass < drawn.size(); ++pass)
    {
        double heldWidth = 0.0;
        double freeValue = 0.0;
        for (std::size_t index = 0; index < drawn.size(); ++index)
        {
            heldWidth += held[index] ? smallest : 0.0;
            freeValue += held[index] ? 0.0 : drawn[index]->value;
        }
        bool changed = false;
        for (std::size_t index = 0; index < drawn.size(); ++index)
        {
            if (held[index])
            {
                widths[index] = smallest;
                continue;
            }
            widths[index] = (usable - heldWidth) * drawn[index]->value / freeValue;
            if (widths[index] < smallest)
            {
                held[index] = true;
                changed = true;
            }
        }
        if (!changed)
        {
            break;
        }
    }

    QString out;
    openChart(out, kKind, width, barHeight + 18.0, bar.description);
    double x = 0.0;
    for (std::size_t index = 0; index < drawn.size(); ++index)
    {
        const ChartSegment& segment = *drawn[index];
        out +=
            QStringLiteral("<rect class=\"c-seg %1\" x=\"%2\" y=\"0\" width=\"%3\" height=\"%4\" rx=\"3\" "
                           "tabindex=\"0\"%5/>")
                .arg(stateClass(segment.state), num(x), num(widths[index]), num(barHeight), tipAttribute(segment.tip));
        if (textWidth(segment.valueText, 11.0) + 4.0 <= widths[index])
        {
            appendText(out, QLatin1String("c-val"), x + widths[index] / 2.0, barHeight + 14.0, Anchor::Middle,
                       segment.valueText);
        }
        x += widths[index] + gap;
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- A measure's range, group by group.

QString rangeChart(const RangeChart& chart)
{
    static const QLatin1String kKind("range");
    std::vector<const ChartRangeRow*> rows;
    Extent extent;
    double labelWidth = 0.0;
    for (const ChartRangeRow& row : chart.rows)
    {
        const ChartRange& range = row.range;
        if (std::isfinite(range.lowest) && std::isfinite(range.lowerQuartile) && std::isfinite(range.median) &&
            std::isfinite(range.upperQuartile) && std::isfinite(range.highest))
        {
            rows.push_back(&row);
            extent.take(range.lowest);
            extent.take(range.highest);
            labelWidth = (std::max)(labelWidth, textWidth(row.label, kTickFontPx));
        }
    }
    if (rows.empty())
    {
        return emptyChart(kKind, chart.width, chart.description);
    }

    const double width = chart.width;
    const double left = std::clamp(labelWidth + 14.0, 34.0, 120.0);
    const double right = 52.0;
    const double top = 10.0;
    const double bottom = 34.0;
    const double rowHeight = 30.0;
    const double plotWidth = width - left - right;
    const double plotHeight = rowHeight * static_cast<double>(rows.size());
    const double height = top + plotHeight + bottom;
    const Scale scale = scaleOf(extent, chart.axis, 4, left, left + plotWidth);

    QString out;
    openChart(out, kKind, width, height, chart.description);
    for (const double tick : scale.ticks)
    {
        appendLine(out, QLatin1String("c-grid"), scale.at(tick), top, scale.at(tick), top + plotHeight);
        appendText(out, QLatin1String("c-tick"), scale.at(tick), top + plotHeight + 14.0, Anchor::Middle,
                   scale.text(tick));
    }
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const ChartRangeRow& row = *rows[index];
        const double y = top + rowHeight * (static_cast<double>(index) + 0.5);
        appendText(out, QLatin1String("c-lab"), left - 8.0, y + 4.0, Anchor::End,
                   fitText(row.label, left - 12.0, kTickFontPx));
        appendLine(out, QLatin1String("c-whisk"), scale.at(row.range.lowest), y, scale.at(row.range.highest), y);
        out += QStringLiteral("<rect class=\"c-iqr\" x=\"%1\" y=\"%2\" width=\"%3\" height=\"12\" rx=\"3\"/>")
                   .arg(num(scale.at(row.range.lowerQuartile)), num(y - 6.0),
                        num(scale.at(row.range.upperQuartile) - scale.at(row.range.lowerQuartile)));
        out += QStringLiteral("<circle class=\"c-dot\" cx=\"%1\" cy=\"%2\" r=\"4.5\"/>")
                   .arg(num(scale.at(row.range.median)), num(y));
        appendText(out, QLatin1String("c-val"), width - right + 8.0, y + 4.0, Anchor::Start, row.valueText);
        appendHit(out, left, y - rowHeight / 2.0, plotWidth + right, rowHeight, row.tip, true);
    }
    appendText(out, QLatin1String("c-lab"), left + plotWidth / 2.0, height - 4.0, Anchor::Middle, chart.axis.title);
    out += QLatin1String("</svg>");
    return out;
}

// ---- One measure against another.

QString scatterChart(const ScatterChart& chart)
{
    static const QLatin1String kKind("scatter");
    std::vector<const ChartPoint*> points;
    Extent xExtent;
    Extent yExtent;
    for (const ChartPoint& point : chart.points)
    {
        if (std::isfinite(point.x) && std::isfinite(point.y))
        {
            points.push_back(&point);
            xExtent.take(point.x);
            yExtent.take(point.y);
        }
    }
    if (points.empty())
    {
        return emptyChart(kKind, chart.width, chart.description);
    }

    const double width = chart.width;
    const double height = chart.height;
    const double left = 44.0;
    const double right = 12.0;
    const double top = 12.0;
    const double bottom = 36.0;
    const double plotWidth = width - left - right;
    const double plotHeight = height - top - bottom;
    const Scale xScale = scaleOf(xExtent, chart.xAxis, 4, left, left + plotWidth);
    const Scale yScale = scaleOf(yExtent, chart.yAxis, 4, top + plotHeight, top);
    const bool dense = points.size() > kChartDenseAbove;

    QString out;
    openChart(out, kKind, width, height, chart.description);
    for (const double tick : yScale.ticks)
    {
        appendLine(out, QLatin1String("c-grid"), left, yScale.at(tick), left + plotWidth, yScale.at(tick));
        appendText(out, QLatin1String("c-tick"), left - 6.0, yScale.at(tick) + 3.5, Anchor::End, yScale.text(tick));
    }
    for (const double tick : xScale.ticks)
    {
        appendLine(out, QLatin1String("c-grid"), xScale.at(tick), top, xScale.at(tick), top + plotHeight);
        appendText(out, QLatin1String("c-tick"), xScale.at(tick), top + plotHeight + 14.0, Anchor::Middle,
                   xScale.text(tick));
    }
    appendLine(out, QLatin1String("c-axis"), left, top + plotHeight, left + plotWidth, top + plotHeight);
    appendText(out, QLatin1String("c-lab"), left + plotWidth / 2.0, height - 4.0, Anchor::Middle, chart.xAxis.title);
    appendUprightText(out, QLatin1String("c-lab"), 12.0, top + plotHeight / 2.0, chart.yAxis.title);

    // The lights used first, so a cross is never under a dot.
    if (dense)
    {
        // One path for every dot: a move and a line of no length each, which a round line cap draws as a dot.
        QString dots;
        for (const ChartPoint* point : points)
        {
            if (point->state == ChartState::Used)
            {
                dots += QStringLiteral("M%1 %2h0").arg(num(xScale.at(point->x)), num(yScale.at(point->y)));
            }
        }
        if (!dots.isEmpty())
        {
            out += QStringLiteral("<path class=\"c-pts\" d=\"%1\"/>").arg(dots);
        }
    }
    else
    {
        for (const ChartPoint* point : points)
        {
            if (point->state == ChartState::Used)
            {
                out += QStringLiteral("<circle class=\"c-pt\" cx=\"%1\" cy=\"%2\" r=\"3.5\"/>")
                           .arg(num(xScale.at(point->x)), num(yScale.at(point->y)));
            }
        }
    }
    for (const ChartPoint* point : points)
    {
        if (point->state != ChartState::Used)
        {
            appendCross(out, xScale.at(point->x), yScale.at(point->y), 3.5, point->state,
                        dense ? &point->tip : nullptr);
        }
    }
    if (!dense)
    {
        for (const ChartPoint* point : points)
        {
            out += QStringLiteral("<circle class=\"c-hitc\" cx=\"%1\" cy=\"%2\" r=\"7\"%3/>")
                       .arg(num(xScale.at(point->x)), num(yScale.at(point->y)), tipAttribute(point->tip));
        }
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- How a measure is distributed.

QString distributionChart(const DistributionChart& chart)
{
    static const QLatin1String kKind("distribution");
    std::uint64_t peak = 0;
    for (const ChartBin& bin : chart.bins)
    {
        peak = (std::max)(peak, bin.count);
    }
    if (peak == 0)
    {
        return emptyChart(kKind, chart.width, chart.description);
    }

    const double width = chart.width;
    const double height = chart.height;
    const double left = 36.0;
    const double right = 10.0;
    const double top = 16.0;
    const double bottom = 36.0;
    const double plotWidth = width - left - right;
    const double plotHeight = height - top - bottom;
    const double base = top + plotHeight;
    const std::size_t bins = chart.bins.size();
    const double slot = plotWidth / static_cast<double>(bins);
    const double barWidth = std::clamp(slot - 2.0, 1.0, 24.0);
    Extent counts;
    counts.take(0.0);
    counts.take(static_cast<double>(peak));
    const Scale scale = scaleOf(counts, {}, 3, base, top, true);

    QString out;
    openChart(out, kKind, width, height, chart.description);
    for (const double tick : scale.ticks)
    {
        appendLine(out, QLatin1String("c-grid"), left, scale.at(tick), left + plotWidth, scale.at(tick));
        appendText(out, QLatin1String("c-tick"), left - 6.0, scale.at(tick) + 3.5, Anchor::End, scale.text(tick));
    }
    const int edgeDecimals = (std::min)(4, decimalsOf(chart.bins.front().high - chart.bins.front().low));
    bool peakNamed = false;
    for (std::size_t index = 0; index < bins; ++index)
    {
        const ChartBin& bin = chart.bins[index];
        const double centre = left + slot * (static_cast<double>(index) + 0.5);
        if (bin.count > 0)
        {
            const double barTop = scale.at(static_cast<double>(bin.count));
            out += QStringLiteral("<path class=\"c-bar\" d=\"%1\"/>")
                       .arg(columnPath(centre - barWidth / 2.0, barWidth, barTop, base));
            if (bin.count == peak && !peakNamed)
            {
                appendText(out, QLatin1String("c-val"), centre, barTop - 5.0, Anchor::Middle,
                           formatCount(static_cast<qint64>(bin.count)));
                peakNamed = true;
            }
        }
        // Every other edge is named, and the last; an edge beside the last gives way to it.
        if (index % 2 == 0 && index + 1 < bins)
        {
            appendText(out, QLatin1String("c-tick"), left + slot * static_cast<double>(index), base + 14.0,
                       Anchor::Middle, tickText(bin.low, edgeDecimals));
        }
    }
    appendText(out, QLatin1String("c-tick"), left + plotWidth, base + 14.0, Anchor::Middle,
               tickText(chart.bins.back().high, edgeDecimals));
    appendLine(out, QLatin1String("c-axis"), left, base, left + plotWidth, base);
    appendText(out, QLatin1String("c-lab"), left + plotWidth / 2.0, height - 4.0, Anchor::Middle, chart.axis.title);
    for (std::size_t index = 0; index < bins; ++index)
    {
        appendHit(out, left + slot * static_cast<double>(index), top, slot, plotHeight, chart.bins[index].tip, true);
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- A master's lights, one by one.

QString frameMeasureChart(const FrameMeasureChart& chart)
{
    static const QLatin1String kKind("measures");
    const std::size_t count = chart.lights.size();
    std::vector<Extent> extents(chart.measures.size());
    bool anyValue = false;
    for (std::size_t layer = 0; layer < chart.measures.size(); ++layer)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            extents[layer].take(valueAt(chart.measures[layer].values, index));
        }
        anyValue = anyValue || extents[layer].valid();
    }
    if (count == 0 || !anyValue)
    {
        return emptyChart(kKind, chart.width, chart.description);
    }

    const double width = chart.width;
    const double height = chart.height;
    const double left = 46.0;
    const double right = 14.0;
    const double top = 26.0;
    const double bottom = 26.0;
    const double plotWidth = width - left - right;
    const double plotHeight = height - top - bottom;
    const double base = top + plotHeight;
    const double step = plotWidth / static_cast<double>(count);
    const bool dense = count > kChartDenseAbove;
    const double dotRadius = count <= 30 ? 3.0 : 2.4;
    const QString gradient = QStringLiteral("wash-") + nameToken(chart.id);
    const auto placeOf = [&](std::size_t index) { return left + step * (static_cast<double>(index) + 0.5); };

    QString out;
    openChart(out, kKind, width, height, chart.description);
    if (!dense)
    {
        out += QStringLiteral("<defs><linearGradient id=\"%1\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">"
                              "<stop class=\"c-wash-0\" offset=\"0\"/><stop class=\"c-wash-1\" offset=\"1\"/>"
                              "</linearGradient></defs>")
                   .arg(gradient);
    }

    // What every layer shares: the axis, where each night begins, and the names under the axis.
    appendLine(out, QLatin1String("c-axis"), left, base, left + plotWidth, base);
    double rightLimit = width - right;
    if (!chart.lastLabel.isEmpty())
    {
        appendText(out, QLatin1String("c-tick"), width - right, height - 7.0, Anchor::End, chart.lastLabel);
        rightLimit -= textWidth(chart.lastLabel, kTickFontPx) + 8.0;
    }
    double taken = -std::numeric_limits<double>::infinity();
    for (const ChartMarker& night : chart.nights)
    {
        if (night.index >= count)
        {
            continue;
        }
        const double boundary = left + step * static_cast<double>(night.index);
        if (night.index > 0)
        {
            appendLine(out, QLatin1String("c-axis"), boundary, top, boundary, base);
        }
        const double x = night.index > 0 ? boundary + 5.0 : left + 2.0;
        if (x >= taken && x + textWidth(night.label, kTickFontPx) <= rightLimit)
        {
            appendText(out, QLatin1String("c-tick"), x, height - 7.0, Anchor::Start, night.label);
            taken = x + textWidth(night.label, kTickFontPx) + 8.0;
        }
    }

    for (std::size_t layer = 0; layer < chart.measures.size(); ++layer)
    {
        const ChartMeasure& measure = chart.measures[layer];
        out += QStringLiteral("<g class=\"c-layer c-layer-%1\">").arg(layer);
        appendText(out, QLatin1String("c-ptitle"), left, 14.0, Anchor::Start, measure.title);
        if (!extents[layer].valid())
        {
            appendText(out, QLatin1String("c-empty"), left + plotWidth / 2.0, top + plotHeight / 2.0, Anchor::Middle,
                       chart.description.whenEmpty);
            out += QLatin1String("</g>");
            continue;
        }
        Extent extent = extents[layer];
        extent.take(measure.reference);
        const Scale scale = scaleOf(extent, {}, 4, base, top);
        for (const double tick : scale.ticks)
        {
            appendLine(out, QLatin1String("c-grid"), left, scale.at(tick), left + plotWidth, scale.at(tick));
            appendText(out, QLatin1String("c-tick"), left - 8.0, scale.at(tick) + 3.5, Anchor::End, scale.text(tick));
        }

        std::vector<std::pair<double, double>> points;
        std::vector<bool> present;
        for (std::size_t index = 0; index < count; ++index)
        {
            const double value = valueAt(measure.values, index);
            present.push_back(std::isfinite(value));
            points.emplace_back(placeOf(index), present.back() ? scale.at(value) : base);
        }
        // A wash under each run of lights that state the measure. A dense chart has none: it would state every
        // light's place a second time.
        for (std::size_t index = 0; !dense && index < count;)
        {
            if (!present[index])
            {
                ++index;
                continue;
            }
            std::size_t end = index;
            QString outline;
            while (end < count && present[end])
            {
                outline += QStringLiteral("%1,%2 ").arg(num(points[end].first), num(points[end].second));
                ++end;
            }
            if (end - index >= 2)
            {
                out += QStringLiteral("<polygon class=\"c-wash\" fill=\"url(#%1)\" points=\"%2,%3 %4%5,%3\"/>")
                           .arg(gradient, num(points[index].first), num(base), outline, num(points[end - 1].first));
            }
            index = end;
        }
        const bool referenced = measure.reference.has_value() && std::isfinite(*measure.reference);
        if (referenced)
        {
            appendLine(out, QLatin1String("c-med"), left, scale.at(*measure.reference), left + plotWidth,
                       scale.at(*measure.reference));
        }
        appendLines(out, QLatin1String("c-line"), points, present);
        for (std::size_t index = 0; index < count; ++index)
        {
            const ChartLight& light = chart.lights[index];
            if (light.state != ChartState::Used)
            {
                appendCross(out, points[index].first, points[index].second, 4.0, light.state,
                            dense ? &light.tip : nullptr);
            }
            else if (!dense && present[index])
            {
                out += QStringLiteral("<circle class=\"c-dot\" cx=\"%1\" cy=\"%2\" r=\"%3\"/>")
                           .arg(num(points[index].first), num(points[index].second), num(dotRadius));
            }
        }
        if (referenced)
        {
            appendText(out, QLatin1String("c-lab"), left + plotWidth - 2.0, scale.at(*measure.reference) - 5.0,
                       Anchor::End, measure.referenceLabel);
        }
        out += QLatin1String("</g>");
    }

    if (!dense)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            appendHit(out, left + step * static_cast<double>(index), top, step, plotHeight, chart.lights[index].tip,
                      false);
        }
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- Where each light sat against the reference.

QString ditherChart(const DitherChart& chart)
{
    static const QLatin1String kKind("dither");
    double reach = 0.0;
    std::size_t placed = 0;
    for (const ChartSeries& night : chart.nights)
    {
        for (const ChartPoint& point : night.points)
        {
            if (std::isfinite(point.x) && std::isfinite(point.y))
            {
                reach = (std::max)({reach, std::abs(point.x), std::abs(point.y)});
                ++placed;
            }
        }
    }
    if (placed == 0)
    {
        return emptyChart(kKind, chart.size, chart.description);
    }

    const double left = 40.0;
    const double right = 12.0;
    const double top = 12.0;
    const double bottom = 34.0;
    const double side = chart.size - left - right;
    const double height = top + side + bottom;
    Extent extent;
    extent.take(-reach);
    extent.take(reach);
    // One scale for both axes, so a shift reads the same in either direction.
    const Scale xScale = scaleOf(extent, {}, 4, left, left + side);
    const Scale yScale = scaleOf(extent, {}, 4, top + side, top);
    const bool dense = placed > kChartDenseAbove;

    QString out;
    openChart(out, kKind, chart.size, height, chart.description);
    for (const double tick : xScale.ticks)
    {
        const QLatin1String cssClass = tick == 0.0 ? QLatin1String("c-zero") : QLatin1String("c-grid");
        appendLine(out, cssClass, xScale.at(tick), top, xScale.at(tick), top + side);
        appendLine(out, cssClass, left, yScale.at(tick), left + side, yScale.at(tick));
        appendText(out, QLatin1String("c-tick"), xScale.at(tick), top + side + 14.0, Anchor::Middle, xScale.text(tick));
        appendText(out, QLatin1String("c-tick"), left - 6.0, yScale.at(tick) + 3.5, Anchor::End, yScale.text(tick));
    }
    appendText(out, QLatin1String("c-lab"), left + side / 2.0, height - 4.0, Anchor::Middle, chart.xAxis.title);
    appendUprightText(out, QLatin1String("c-lab"), 11.0, top + side / 2.0, chart.yAxis.title);

    for (std::size_t night = 0; night < chart.nights.size(); ++night)
    {
        const QString cssClass = nightClass(static_cast<int>(night));
        QString together;
        for (const ChartPoint& point : chart.nights[night].points)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
            {
                continue;
            }
            const QString shape = nightShape(static_cast<int>(night), xScale.at(point.x), yScale.at(point.y));
            if (dense)
            {
                together += shape;
            }
            else
            {
                out += QStringLiteral("<path class=\"%1\" d=\"%2\"/>").arg(cssClass, shape);
            }
        }
        if (dense && !together.isEmpty())
        {
            out += QStringLiteral("<path class=\"%1 c-ns\" d=\"%2\"/>").arg(cssClass, together);
        }
    }
    if (!dense)
    {
        for (const ChartSeries& night : chart.nights)
        {
            for (const ChartPoint& point : night.points)
            {
                if (std::isfinite(point.x) && std::isfinite(point.y))
                {
                    out += QStringLiteral("<circle class=\"c-hitc\" cx=\"%1\" cy=\"%2\" r=\"7\"%3/>")
                               .arg(num(xScale.at(point.x)), num(yScale.at(point.y)), tipAttribute(point.tip));
                }
            }
        }
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- The samples of a master.

QString sampleDistributionChart(const SampleDistributionChart& chart)
{
    static const QLatin1String kKind("samples");
    struct Panel
    {
        const SampleChannel* channel = nullptr;
        std::uint64_t peak = 0;
    };
    std::vector<Panel> panels;
    Extent decades;
    for (const SampleChannel& channel : chart.channels)
    {
        std::uint64_t peak = 0;
        for (const std::uint64_t samples : channel.counts)
        {
            peak = (std::max)(peak, samples);
        }
        if (peak == 0 || !std::isfinite(channel.lowest) || !std::isfinite(channel.highest) || !(channel.lowest > 0.0) ||
            !(channel.highest > channel.lowest))
        {
            continue;
        }
        panels.push_back({&channel, peak});
        decades.take(std::log10(channel.lowest));
        decades.take(std::log10(channel.highest));
    }
    if (panels.empty())
    {
        return emptyChart(kKind, chart.width, chart.description);
    }

    const double width = chart.width;
    const double left = 10.0;
    const double right = 10.0;
    const double top = 8.0;
    const double panelGap = 8.0;
    const double nameHeight = 14.0;
    const double bottom = chart.axis.title.isEmpty() ? 18.0 : 32.0;
    const double plotWidth = width - left - right;
    const double panelHeight = (std::max)(40.0, static_cast<double>(chart.panelHeight));
    const double height = top + panelHeight * static_cast<double>(panels.size()) +
                          panelGap * static_cast<double>(panels.size() - 1) + bottom;
    const auto placeOf = [&](double logValue) {
        return left + plotWidth * (logValue - decades.lowest) / (decades.highest - decades.lowest);
    };

    // The axis is marked at each power of ten the bins reach; a range with none inside is marked at its ends.
    std::vector<double> marks;
    for (double exponent = std::ceil(decades.lowest - 1e-9); exponent <= decades.highest + 1e-9; exponent += 1.0)
    {
        marks.push_back(exponent);
    }
    if (marks.empty())
    {
        marks = {decades.lowest, decades.highest};
    }

    QString out;
    openChart(out, kKind, width, height, chart.description);
    for (std::size_t index = 0; index < panels.size(); ++index)
    {
        const SampleChannel& channel = *panels[index].channel;
        const double panelTop = top + (panelHeight + panelGap) * static_cast<double>(index);
        const double plotTop = panelTop + (channel.name.isEmpty() ? 0.0 : nameHeight);
        const double base = panelTop + panelHeight;
        const double plotHeight = base - plotTop;
        appendText(out, QLatin1String("c-ptitle"), left, panelTop + 10.0, Anchor::Start, channel.name);
        for (const double mark : marks)
        {
            appendLine(out, QLatin1String("c-grid"), placeOf(mark), plotTop, placeOf(mark), base);
        }

        // The outline: each bin at its middle on the logarithmic axis, as high as the square root of its share of
        // the fullest bin.
        const double low = std::log10(channel.lowest);
        const double high = std::log10(channel.highest);
        const std::size_t bins = channel.counts.size();
        QString outline = QStringLiteral("%1,%2 ").arg(num(placeOf(low)), num(base));
        for (std::size_t bin = 0; bin < bins; ++bin)
        {
            const double middle = low + (high - low) * (static_cast<double>(bin) + 0.5) / static_cast<double>(bins);
            const double share =
                std::sqrt(static_cast<double>(channel.counts[bin]) / static_cast<double>(panels[index].peak));
            outline += QStringLiteral("%1,%2 ").arg(num(placeOf(middle)), num(base - plotHeight * share));
        }
        outline += QStringLiteral("%1,%2").arg(num(placeOf(high)), num(base));
        out += QStringLiteral("<polygon class=\"c-hist\" points=\"%1\"/>").arg(outline);
        appendLine(out, QLatin1String("c-axis"), left, base, left + plotWidth, base);

        if (channel.median.has_value() && std::isfinite(*channel.median) && *channel.median > 0.0)
        {
            const double x = std::clamp(placeOf(std::log10(*channel.median)), left, left + plotWidth);
            appendLine(out, QLatin1String("c-med"), x, plotTop, x, base);
            const bool leftHalf = x < left + plotWidth * 0.6;
            appendText(out, QLatin1String("c-lab"), leftHalf ? x + 5.0 : x - 5.0, plotTop + 10.0,
                       leftHalf ? Anchor::Start : Anchor::End, channel.medianLabel);
        }
    }
    const double lastBase = height - bottom;
    for (const double mark : marks)
    {
        const double x = placeOf(mark);
        const Anchor anchor =
            x <= left + 1.0 ? Anchor::Start : (x >= left + plotWidth - 1.0 ? Anchor::End : Anchor::Middle);
        appendText(out, QLatin1String("c-tick"), x, lastBase + 14.0, anchor, formatFigure(std::pow(10.0, mark)));
    }
    appendText(out, QLatin1String("c-lab"), left + plotWidth / 2.0, height - 3.0, Anchor::Middle, chart.axis.title);
    out += QLatin1String("</svg>");
    return out;
}

// ---- Coverage.

QString coverageBar(const CoverageBar& bar)
{
    static const QLatin1String kKind("coverage");
    if (!std::isfinite(bar.most) || !(bar.most > 0.0))
    {
        return emptyChart(kKind, bar.width, bar.description);
    }
    const double width = bar.width;
    const double height = bar.notes.isEmpty() ? 16.0 : 34.0;
    QString out;
    openChart(out, kKind, width, height, bar.description);
    out +=
        QStringLiteral("<rect class=\"c-track\" x=\"0\" y=\"3\" width=\"%1\" height=\"10\" rx=\"5\"/>").arg(num(width));
    if (std::isfinite(bar.least))
    {
        const double from = width * std::clamp(bar.least / bar.most, 0.0, 1.0);
        out += QStringLiteral("<rect class=\"c-span\" x=\"%1\" y=\"3\" width=\"%2\" height=\"10\" rx=\"5\"/>")
                   .arg(num(from), num(width - from));
    }
    if (std::isfinite(bar.median))
    {
        const double at = std::clamp(width * bar.median / bar.most - 1.5, 0.0, width - 3.0);
        out += QStringLiteral("<rect class=\"c-mark\" x=\"%1\" y=\"0\" width=\"3\" height=\"16\" rx=\"1.5\"/>")
                   .arg(num(at));
    }
    for (qsizetype index = 0; index < bar.notes.size(); ++index)
    {
        const bool first = index == 0;
        const bool last = index + 1 == bar.notes.size();
        const double x = bar.notes.size() == 1
                             ? 0.0
                             : width * static_cast<double>(index) / static_cast<double>(bar.notes.size() - 1);
        appendText(out, QLatin1String("c-tick"), x, 30.0, first ? Anchor::Start : (last ? Anchor::End : Anchor::Middle),
                   bar.notes.at(index));
    }
    appendHit(out, 0.0, 0.0, width, 16.0, bar.tip, true);
    out += QLatin1String("</svg>");
    return out;
}

// ---- A figure's share, inside a table cell.

QString cellBar(double fraction, std::optional<double> mark)
{
    const double width = 72.0;
    QString out = QStringLiteral("<svg class=\"cellbar\" viewBox=\"0 0 72 6\" width=\"72\" height=\"6\" "
                                 "aria-hidden=\"true\"><rect class=\"c-track\" x=\"0\" y=\"0\" width=\"72\" "
                                 "height=\"6\" rx=\"3\"/>");
    if (std::isfinite(fraction) && fraction > 0.0)
    {
        out += QStringLiteral("<rect class=\"c-fill\" x=\"0\" y=\"0\" width=\"%1\" height=\"6\" rx=\"3\"/>")
                   .arg(num(width * (std::min)(1.0, fraction)));
    }
    if (mark.has_value() && std::isfinite(*mark))
    {
        const double at = std::clamp(width * *mark - 1.0, 0.0, width - 2.0);
        out += QStringLiteral("<rect class=\"c-mark\" x=\"%1\" y=\"0\" width=\"2\" height=\"6\"/>").arg(num(at));
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- The stages of the run.

QString timelineChart(const TimelineChart& chart)
{
    static const QLatin1String kKind("timeline");
    if (chart.stages.empty())
    {
        return emptyChart(kKind, chart.width, chart.description);
    }
    const auto timeOf = [](const ChartStage& stage) {
        return stage.durationMs.has_value() && std::isfinite(*stage.durationMs) && *stage.durationMs > 0.0
                   ? *stage.durationMs
                   : 0.0;
    };
    double total = 0.0;
    for (const ChartStage& stage : chart.stages)
    {
        total += timeOf(stage);
    }
    const std::vector<double> ticks = durationTicks(total);
    const double axisEnd = (std::max)(1.0, ticks.back());

    const double width = chart.width;
    const double nameWidth = width < 700.0 ? 150.0 : 230.0;
    const double valueWidth = 104.0;
    const double gutter = 14.0;
    const double laneLeft = nameWidth + gutter;
    const double laneWidth = (std::max)(40.0, width - nameWidth - valueWidth - 2.0 * gutter);
    const double head = 26.0;
    const auto rowHeightOf = [](const ChartStage& stage) { return stage.note.isEmpty() ? 28.0 : 40.0; };
    double height = head;
    for (const ChartStage& stage : chart.stages)
    {
        height += rowHeightOf(stage);
    }
    const auto placeOf = [&](double ms) { return laneLeft + laneWidth * ms / axisEnd; };

    QString out;
    openChart(out, kKind, width, height, chart.description);
    appendText(out, QLatin1String("c-tick"), 0.0, 14.0, Anchor::Start, chart.stageHeading);
    appendText(out, QLatin1String("c-tick"), width, 14.0, Anchor::End, chart.valueHeading);
    for (std::size_t index = 0; index < ticks.size(); ++index)
    {
        const double x = placeOf(ticks[index]);
        appendLine(out, QLatin1String("c-grid"), x, head - 4.0, x, height);
        appendText(out, QLatin1String("c-tick"), x, 14.0,
                   index == 0 ? Anchor::Start : (index + 1 == ticks.size() ? Anchor::End : Anchor::Middle),
                   durationTickText(ticks[index]));
    }
    appendLine(out, QLatin1String("c-axis"), 0.0, head - 4.0, width, head - 4.0);

    double y = head;
    double elapsed = 0.0;
    for (std::size_t index = 0; index < chart.stages.size(); ++index)
    {
        const ChartStage& stage = chart.stages[index];
        const double rowHeight = rowHeightOf(stage);
        const double middle = y + rowHeight / 2.0;
        const QLatin1String state = stageClass(stage.state);
        const QLatin1String minor = stage.minor ? QLatin1String(" is-minor") : QLatin1String();
        out += QStringLiteral("<path class=\"c-ico %1\" transform=\"translate(0 %2)\" d=\"%3\"/>")
                   .arg(state, num(middle - 8.0), stageIcon(stage.state));
        const QString name = fitText(stage.name, nameWidth - 28.0, kNameFontPx);
        if (!name.isEmpty())
        {
            out += QStringLiteral("<text class=\"c-name%1\" x=\"24\" y=\"%2\">%3</text>")
                       .arg(minor, num(stage.note.isEmpty() ? middle + 4.0 : y + 17.0), escaped(name));
        }
        appendText(out, QLatin1String("c-tick"), 24.0, y + 31.0, Anchor::Start,
                   fitText(stage.note, nameWidth - 28.0, kTickFontPx));

        const double time = timeOf(stage);
        if (time > 0.0)
        {
            out += QStringLiteral("<rect class=\"c-stagebar %1%2\" x=\"%3\" y=\"%4\" width=\"%5\" height=\"12\" "
                                  "rx=\"4\"/>")
                       .arg(state, minor, num(placeOf(elapsed)), num(middle - 6.0),
                            num((std::max)(3.0, laneWidth * time / axisEnd)));
            elapsed += time;
        }
        else if (stage.state == ChartStageState::Reused)
        {
            // Nothing was made, so no time was spent: a small hollow bar where the stage would have run.
            out += QStringLiteral("<rect class=\"c-stagebar %1\" x=\"%2\" y=\"%3\" width=\"12\" height=\"12\" "
                                  "rx=\"4\"/>")
                       .arg(state, num((std::min)(placeOf(elapsed), laneLeft + laneWidth - 12.0)), num(middle - 6.0));
        }
        appendText(out, QLatin1String("c-val"), width, middle + 4.0, Anchor::End, stage.valueText);
        if (index + 1 < chart.stages.size())
        {
            appendLine(out, QLatin1String("c-grid"), 0.0, y + rowHeight, width, y + rowHeight);
        }
        appendHit(out, 0.0, y, width, rowHeight, stage.tip, true);
        y += rowHeight;
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- Plain bars.

QString barsChart(const BarsChart& chart)
{
    static const QLatin1String kKind("bars");
    if (chart.bars.empty())
    {
        return emptyChart(kKind, chart.width, chart.description);
    }
    double longest = 0.0;
    double valueWidth = 40.0;
    for (const ChartBar& bar : chart.bars)
    {
        if (std::isfinite(bar.value))
        {
            longest = (std::max)(longest, bar.value);
        }
        valueWidth = (std::max)(valueWidth, textWidth(bar.valueText, 11.0) + 10.0);
    }
    const double width = chart.width;
    const double labelWidth = std::clamp(static_cast<double>(chart.labelWidth), 0.0, width / 2.0);
    valueWidth = (std::min)(valueWidth, width / 3.0);
    const double laneWidth = (std::max)(10.0, width - labelWidth - valueWidth);
    const double rowHeight = 22.0;
    const double height = rowHeight * static_cast<double>(chart.bars.size());

    QString out;
    openChart(out, kKind, width, height, chart.description);
    for (std::size_t index = 0; index < chart.bars.size(); ++index)
    {
        const ChartBar& bar = chart.bars[index];
        const double y = rowHeight * static_cast<double>(index);
        const double middle = y + rowHeight / 2.0;
        appendText(out, QLatin1String("c-lab"), 0.0, middle + 4.0, Anchor::Start,
                   fitText(bar.label, labelWidth - 8.0, kTickFontPx));
        if (std::isfinite(bar.value) && bar.value > 0.0 && longest > 0.0)
        {
            out += QStringLiteral("<rect class=\"c-bar\" x=\"%1\" y=\"%2\" width=\"%3\" height=\"12\" rx=\"3\"/>")
                       .arg(num(labelWidth), num(middle - 6.0), num((std::max)(2.0, laneWidth * bar.value / longest)));
        }
        appendText(out, QLatin1String("c-val"), width, middle + 4.0, Anchor::End, bar.valueText);
        appendHit(out, 0.0, y, width, rowHeight, bar.tip, true);
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- The runs of the folder.

QString runsChart(const RunsChart& chart)
{
    static const QLatin1String kKind("runs");
    if (chart.runs.empty())
    {
        return emptyChart(kKind, chart.width, chart.description);
    }
    const auto figureOf = [](const ChartRun& run) {
        return run.value.has_value() && std::isfinite(*run.value) ? run.value : std::nullopt;
    };
    double largest = 0.0;
    for (const ChartRun& run : chart.runs)
    {
        largest = (std::max)(largest, figureOf(run).value_or(0.0));
    }

    const double width = chart.width;
    const double height = chart.height;
    const double left = 8.0;
    const double right = 8.0;
    const double top = 20.0;
    const double bottom = 34.0;
    const double plotWidth = width - left - right;
    const double plotHeight = height - top - bottom;
    const double base = top + plotHeight;
    const std::size_t count = chart.runs.size();
    const double slot = plotWidth / static_cast<double>(count);
    const double columnWidth = std::clamp(slot - 8.0, 3.0, 22.0);
    // The names under the columns are thinned to what fits, counted back from the latest run.
    const std::size_t every = static_cast<std::size_t>((std::max)(1.0, std::ceil(46.0 / slot)));

    QString out;
    openChart(out, kKind, width, height, chart.description);
    appendLine(out, QLatin1String("c-axis"), left, base, width - right, base);
    for (std::size_t index = 0; index < count; ++index)
    {
        const ChartRun& run = chart.runs[index];
        const double centre = left + slot * (static_cast<double>(index) + 0.5);
        const bool named = (count - 1 - index) % every == 0;
        const std::optional<double> figure = figureOf(run);
        if (figure.has_value())
        {
            const double columnHeight =
                (std::max)(2.0, largest > 0.0 ? plotHeight * (std::max)(0.0, *figure) / largest : 0.0);
            out += QStringLiteral("<rect class=\"%1\" x=\"%2\" y=\"%3\" width=\"%4\" height=\"%5\" rx=\"3\"/>")
                       .arg(run.thisRun ? QLatin1String("c-colnow") : QLatin1String("c-colprev"),
                            num(centre - columnWidth / 2.0), num(base - columnHeight), num(columnWidth),
                            num(columnHeight));
            if (named || run.thisRun)
            {
                appendText(out, run.thisRun ? QLatin1String("c-val") : QLatin1String("c-tick"), centre,
                           base - columnHeight - 5.0, Anchor::Middle, run.valueText);
            }
        }
        else
        {
            // A run that stopped has no figure: a cross stands on the axis where its column would.
            out += QStringLiteral("<path class=\"c-x\" d=\"M%1 %2l8 8M%3 %2l-8 8\"/>")
                       .arg(num(centre - 4.0), num(base - 12.0), num(centre + 4.0));
        }
        if (named)
        {
            for (qsizetype line = 0; line < run.label.size(); ++line)
            {
                appendText(out, QLatin1String("c-tick"), centre, base + 14.0 + 13.0 * static_cast<double>(line),
                           Anchor::Middle, run.label.at(line));
            }
        }
        appendHit(out, left + slot * static_cast<double>(index), top - 14.0, slot, plotHeight + 14.0, run.tip, true);
    }
    out += QLatin1String("</svg>");
    return out;
}

// ---- What the marks mean.

QString chartLegend(const std::vector<ChartLegendItem>& items)
{
    if (items.empty())
    {
        return {};
    }
    QString out = QStringLiteral("<div class=\"legend\">");
    for (const ChartLegendItem& item : items)
    {
        out += QLatin1String("<span><svg viewBox=\"0 0 12 12\" width=\"12\" height=\"12\" aria-hidden=\"true\">");
        switch (item.mark)
        {
        case ChartLegendItem::Mark::Swatch:
            out += QStringLiteral("<rect class=\"c-key %1\" x=\"1\" y=\"1\" width=\"10\" height=\"10\" rx=\"3\"/>")
                       .arg(stateClass(item.state));
            break;
        case ChartLegendItem::Mark::Dot:
            out += QLatin1String("<circle class=\"c-dot\" cx=\"6\" cy=\"6\" r=\"4\"/>");
            break;
        case ChartLegendItem::Mark::Cross:
            out +=
                QStringLiteral("<path class=\"c-x %1\" d=\"M2.5 2.5l7 7M9.5 2.5l-7 7\"/>").arg(stateClass(item.state));
            break;
        case ChartLegendItem::Mark::Night:
            out += QStringLiteral("<path class=\"%1\" d=\"%2\"/>")
                       .arg(nightClass(item.night), nightShape(item.night, 6.0, 6.0));
            break;
        }
        out += QLatin1String("</svg>");
        out += escaped(item.label);
        if (!item.value.isEmpty())
        {
            out += QStringLiteral(" <b class=\"num\">%1</b>").arg(escaped(item.value));
        }
        out += QLatin1String("</span>");
    }
    out += QLatin1String("</div>");
    return out;
}
} // namespace ImageIntegration::Reporting

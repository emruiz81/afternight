// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The second part of the report's document: a file per master.
///
/// A master's file is where that master's facts live: the picture with its maps and how to read each, the
/// statistics and the spread of its samples, what the stack recorded of itself, the astrometry and the photometry,
/// where its time went, its lights one by one, the settings of its own groups and the files it wrote. What it
/// shares with other masters - a calibration master, a registration group - it links to and does not repeat.
///
/// A master the run did not make has a file that says why; a reused one says which run made it; a combined colour
/// image has the picture, its channels and what it was made from, and no lights of its own.

#include "processes/ImageIntegration/reporting/ReportCharts.h"
#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>

namespace ImageIntegration::Reporting
{
namespace
{
/// How many views a master's picture switches between, and how many measures a chart of its lights: the
/// stylesheet states a rule for each, so neither may run past it.
constexpr std::size_t kViewsAtMost = 10;
constexpr std::size_t kMeasuresAtMost = 9;

/// How many clusters of rejected samples a master's file lists: the largest ones.
constexpr std::size_t kClustersListed = 20;

[[nodiscard]] bool isUsed(const ReportFrame& frame)
{
    return frame.state == ReportFrameState::Used;
}

/// The issues of the run that are about one master and need a second look. That a master was not made is not
/// among them: its file says so first, in its own words.
[[nodiscard]] std::vector<const ReportIssue*> flagsOf(const ReportModel& model, const ReportMaster& master)
{
    std::vector<const ReportIssue*> flags;
    for (const ReportIssue& issue : model.issues)
    {
        if (issue.kind != ReportIssueKind::RunMessage && issue.kind != ReportIssueKind::MasterFailed &&
            issue.kind != ReportIssueKind::MasterNotReached && issue.severity != ReportIssueSeverity::Note &&
            issue.groupKey == master.key)
        {
            flags.push_back(&issue);
        }
    }
    return flags;
}

/// A master's lights in the order they were taken; the lights that state no time follow, in the model's order.
[[nodiscard]] std::vector<const ReportFrame*> lightsInCaptureOrder(const ReportMaster& master)
{
    std::vector<const ReportFrame*> lights;
    lights.reserve(master.frames.size());
    for (const ReportFrame& frame : master.frames)
    {
        lights.push_back(&frame);
    }
    std::stable_sort(lights.begin(), lights.end(), [](const ReportFrame* left, const ReportFrame* right) {
        const bool leftTimed = left->capturedAt.has_value();
        const bool rightTimed = right->capturedAt.has_value();
        if (leftTimed != rightTimed)
        {
            return leftTimed;
        }
        return leftTimed && *left->capturedAt < *right->capturedAt;
    });
    return lights;
}

/// An instant in a few characters, in the archive's zone: `23 Mar 21:40`.
[[nodiscard]] QString shortInstant(const QDateTime& instant, const ReportTimeZone& zone)
{
    return instant.toTimeZone(zone.resolved()).toString(QStringLiteral("d MMM HH:mm"));
}

/// The exposures of a group of lights in a few words: `120 s each`, or each exposure with its count.
[[nodiscard]] QString exposuresText(const ReportExposure& exposure)
{
    if (exposure.bins.empty())
    {
        return {};
    }
    if (exposure.bins.size() == 1)
    {
        return documentText("%1 s each").arg(formatFigure(exposure.bins.front().seconds));
    }
    QStringList parts;
    for (const ReportExposureBin& bin : exposure.bins)
    {
        parts << QStringLiteral("%1 × %2 s").arg(formatCount(bin.frames), formatFigure(bin.seconds));
    }
    return parts.join(QStringLiteral(", "));
}

/// What a channel of a master is called: red, green and blue for three, its number otherwise, and nothing for one.
[[nodiscard]] QString channelName(const std::size_t channel, const std::size_t channels)
{
    if (channels <= 1)
    {
        return {};
    }
    if (channels == 3)
    {
        switch (channel)
        {
        case 0:
            return documentText("Red");
        case 1:
            return documentText("Green");
        default:
            return documentText("Blue");
        }
    }
    return documentText("Channel %1").arg(formatCount(static_cast<qint64>(channel + 1)));
}

/// A right ascension in hours, minutes and seconds.
[[nodiscard]] QString rightAscensionText(const double degrees)
{
    double hours = std::fmod(degrees, 360.0) / 15.0;
    if (hours < 0.0)
    {
        hours += 24.0;
    }
    const qint64 seconds = std::llround(hours * 3600.0) % (24 * 3600);
    return QStringLiteral("%1h %2m %3s")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

/// A declination in degrees, minutes and seconds of arc.
[[nodiscard]] QString declinationText(const double degrees)
{
    const qint64 seconds = std::llround(std::abs(degrees) * 3600.0);
    return QStringLiteral("%1%2° %3′ %4″")
        .arg(degrees < 0.0 ? QStringLiteral("−") : QStringLiteral("+"))
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

// ---- The picture.

/// One view of a master: the master itself, its detail, or a map of it.
struct View
{
    QString label;
    QString source;
    QString words;

    /// How to read the view, in a sentence or two.
    QString reading;
};

/// What a map shows and how to read it, by the map's name.
[[nodiscard]] QString mapReading(const ReportMap& map)
{
    const QString title = map.title.toLower();
    QString reading;
    if (map.kind == ReportMapKind::Rejection)
    {
        if (title.contains(QStringLiteral("low")))
        {
            reading = documentText("The samples the stack rejected for being too dark, pixel by pixel: the brighter a "
                                   "pixel, the more lights lost their sample there. Scattered points are noise and "
                                   "dead pixels; a patch is something only some lights hold.");
        }
        else if (title.contains(QStringLiteral("high")))
        {
            reading = documentText("The samples the stack rejected for being too bright, pixel by pixel: the brighter "
                                   "a pixel, the more lights lost their sample there. Scattered points are noise and "
                                   "cosmic rays; a line is a satellite or an aircraft; a patch is something only "
                                   "some lights hold.");
        }
        else
        {
            reading = documentText("The samples the stack rejected, pixel by pixel: the brighter a pixel, the more "
                                   "lights lost their sample there. Scattered points are noise; a line is a "
                                   "satellite or an aircraft; a patch is something only some lights hold.");
        }
        if (map.clusters > 0)
        {
            reading +=
                QLatin1Char(' ') +
                counted(map.clusters,
                        QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 cluster of rejected samples is outlined."),
                        QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 clusters of rejected samples are outlined."));
        }
        return reading;
    }
    if (title.contains(QStringLiteral("coverage")))
    {
        return documentText("How many lights reached each pixel, against the most any pixel got. Even: every pixel "
                            "took the same lights. Dark edges: dithering or registration left fewer lights there. "
                            "Dark spots inside: rejection removed lights at those places.");
    }
    if (title.contains(QStringLiteral("variance")))
    {
        return documentText("How far the lights' values spread at each pixel. Bright: a wider spread - noise, "
                            "nebulosity, or a gradient the lights do not share. Even and dark: the lights agree.");
    }
    if (title.contains(QStringLiteral("snr")))
    {
        return documentText("The noise reduction the stack reached at each pixel, the square root of the coverage, "
                            "against the best. It follows the coverage with less contrast: nine lights give three "
                            "times the signal to noise.");
    }
    if (map.weightSum || title.contains(QStringLiteral("weight")))
    {
        return documentText("The weight that reached each pixel, against the most. Bright: more weight, from better "
                            "lights. Its texture follows the grid the weights were measured on.");
    }
    return documentText("A figure of the stack at each pixel, from the least it takes, dark, to the most, bright.");
}

[[nodiscard]] std::vector<View> viewsOf(const ReportDocumentContext& context, const ReportMaster& master)
{
    std::vector<View> views;
    if (const QString source = context.assets.image(master.preview); !source.isEmpty())
    {
        views.push_back(View{.label = documentText("Master"),
                             .source = source,
                             .words = documentText("%1, stretched preview").arg(master.name),
                             .reading = documentText("The whole frame under a display stretch of its own. The "
                                                     "figures beside it are measured on the linear master.")});
    }
    if (const QString source = context.assets.image(master.detail); !source.isEmpty())
    {
        views.push_back(View{.label = documentText("Detail"),
                             .source = source,
                             .words = documentText("%1, the centre at full scale").arg(master.name),
                             .reading = documentText("The centre of the frame, one master pixel to one picture "
                                                     "pixel, under the whole frame's stretch: where the noise and "
                                                     "the shape of the stars show.")});
    }
    for (const ReportMap& map : master.maps)
    {
        if (views.size() >= kViewsAtMost)
        {
            break;
        }
        if (const QString source = context.assets.image(map.preview); !source.isEmpty())
        {
            views.push_back(View{.label = map.title,
                                 .source = source,
                                 .words = QStringLiteral("%1, %2").arg(master.name, map.title),
                                 .reading = mapReading(map)});
        }
    }
    return views;
}

/// The master's picture and the maps beside it, one shown at a time: a radio button per view picks which, with no
/// script, and each view says how to read it.
void writeViewer(ReportMarkup& out, const ReportMaster& master, const std::vector<View>& views)
{
    out.open("div", {{"class", QStringLiteral("viewer")}});
    const QString name = QStringLiteral("v-") + master.slug;
    if (views.size() > 1)
    {
        for (std::size_t index = 0; index < views.size(); ++index)
        {
            out.leaf("input", {{"type", QStringLiteral("radio")},
                               {"class", QStringLiteral("pick pick-%1").arg(index)},
                               {"name", name},
                               {"id", QStringLiteral("%1-%2").arg(name).arg(index)},
                               {"checked", index == 0 ? QStringLiteral("checked") : QString()}});
        }
    }
    out.open("div", {{"class", QStringLiteral("well")}});
    for (std::size_t index = 0; index < views.size(); ++index)
    {
        out.leaf("img", {{"class", QStringLiteral("view view-%1").arg(index)},
                         {"src", views[index].source},
                         {"alt", views[index].words},
                         {"loading", QStringLiteral("lazy")}});
    }
    out.close("div");
    if (views.size() > 1)
    {
        out.open("div", {{"class", QStringLiteral("views noprint")}});
        for (std::size_t index = 0; index < views.size(); ++index)
        {
            out.element("label", {{"for", QStringLiteral("%1-%2").arg(name).arg(index)}}, views[index].label);
        }
        out.close("div");
    }
    out.open("p", {{"class", QStringLiteral("reading")}});
    for (std::size_t index = 0; index < views.size(); ++index)
    {
        out.element("span", {{"class", QStringLiteral("read read-%1").arg(index)}}, views[index].reading);
    }
    out.close("p");
    out.close("div");
}

/// One row of a master's statistics: its name, what it means, and its value of a channel.
struct StatisticRow
{
    const char* name;
    const char* meaning;
    std::function<QString(const ReportChannelStatistics&)> value;
};

[[nodiscard]] const std::vector<StatisticRow>& statisticRows()
{
    static const std::vector<StatisticRow> rows = {
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Samples"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "One per pixel."),
         [](const ReportChannelStatistics& channel) { return formatCount(static_cast<qint64>(channel.samples)); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Samples measured"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport",
                           "The samples the figures are over: those strictly inside the range, the clipped ones left "
                           "out."),
         [](const ReportChannelStatistics& channel) {
             return formatCount(static_cast<qint64>(channel.unclippedSamples));
         }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Minimum"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.minimum); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Maximum"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.maximum); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Mean"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.mean); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Median"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.median); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Background noise"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport",
                           "The noise of the background, taken from the differences between neighbouring samples of "
                           "each row."),
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.backgroundNoise); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Standard deviation"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.standardDeviation); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Average deviation"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "The mean distance of a sample from the mean."),
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.averageDeviation); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Median deviation"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "The median distance of a sample from the median, unscaled."),
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.medianAbsoluteDeviation); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Variance"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.variance); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Biweight midvariance"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "A variance about the median that outliers barely move."),
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.biweightMidvariance); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "IKSS location"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport",
                           "The level of the background by iterative k-sigma clipping, which normalization uses."),
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.ikssLocation); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "IKSS scale"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport",
                           "The spread of the background by iterative k-sigma clipping, which normalization uses."),
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.ikssScale); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Mean of squares"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.meanOfSquares); }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Sum of squares"), "",
         [](const ReportChannelStatistics& channel) { return formatFigure(channel.sumOfSquares); }},
    };
    return rows;
}

/// A master's statistics: a row per figure, a column per channel.
void writeStatistics(ReportMarkup& out, const ReportMaster& master)
{
    const ReportImageStatistics& statistics = *master.statistics;
    const std::size_t channels = statistics.channelStats.size();
    std::vector<ReportColumn> columns = {{documentText("Statistic")}};
    for (std::size_t channel = 0; channel < channels; ++channel)
    {
        const QString name = channelName(channel, channels);
        columns.push_back({name.isEmpty() ? documentText("Value") : name, true});
    }
    openTable(out, QStringLiteral("stats"), documentText("Statistics of %1").arg(master.name), columns);
    for (const StatisticRow& row : statisticRows())
    {
        out.open("tr");
        out.open("td");
        if (row.meaning[0] == '\0')
        {
            out.text(documentText(row.name));
        }
        else
        {
            writeTerm(out, documentText(row.name), documentText(row.meaning));
        }
        out.close("td");
        for (const ReportChannelStatistics& channel : statistics.channelStats)
        {
            out.element("td", {{"class", QStringLiteral("r num")}}, row.value(channel));
        }
        out.close("tr");
    }
    closeTable(out);
    if (master.snapshot.dynamicRange.has_value())
    {
        writeCaption(out, documentText("Dynamic range, the largest sample less the smallest: %1.")
                              .arg(formatFigure(*master.snapshot.dynamicRange)));
    }
}

/// How a master's samples are spread, a panel per channel on one axis.
[[nodiscard]] QString distributionOf(const ReportMaster& master)
{
    SampleDistributionChart chart;
    chart.description = {documentText("How the samples of %1 are spread").arg(master.name),
                         documentText("The spread of this master's samples was not measured.")};
    chart.axis.title = documentText("Sample value, fraction of full scale, on a logarithmic axis");
    const std::size_t channels = master.distribution.size();
    chart.panelHeight = channels > 1 ? 96 : 130;
    for (std::size_t index = 0; index < channels; ++index)
    {
        const ReportSampleDistribution& spread = master.distribution[index];
        SampleChannel channel;
        channel.name = channelName(index, channels);
        channel.lowest = spread.lowest;
        channel.highest = spread.highest;
        channel.counts = spread.counts;
        if (master.statistics.has_value() && index < master.statistics->channelStats.size())
        {
            channel.median = master.statistics->channelStats[index].median;
            channel.medianLabel = documentText("median %1").arg(formatFigure(*channel.median));
        }
        chart.channels.push_back(channel);
    }
    return sampleDistributionChart(chart);
}

void openSection(ReportMarkup& out, const QString& anchor, const QString& title, const QString& aside)
{
    out.open("div", {{"class", QStringLiteral("fsec")}, {"id", anchor}});
    out.open("h4");
    out.element("span", title);
    if (!aside.isEmpty())
    {
        out.element("small", aside);
    }
    out.close("h4");
}

void writePicture(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    const std::vector<View> views = viewsOf(context, master);
    const bool measured = master.statistics.has_value() && !master.statistics->channelStats.empty();

    QStringList aside;
    if (measured)
    {
        aside << QStringLiteral("%1 × %2").arg(formatCount(static_cast<qint64>(master.statistics->width)),
                                               formatCount(static_cast<qint64>(master.statistics->height)))
              << counted(static_cast<qint64>(master.statistics->channels),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 channel"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 channels"))
              << documentText("measured on the linear master");
    }
    openSection(out, masterFileAnchor(master) + QStringLiteral("-picture"), documentText("The picture"),
                aside.join(dotSeparator()));

    if (views.empty() && !measured)
    {
        out.open("p", {{"class", QStringLiteral("cap")}});
        out.text(documentText("The report holds no picture and no measurement of this master."));
        if (context.hasAnchor(QStringLiteral("not-stated")))
        {
            out.text(QStringLiteral(" "));
            out.element("a", {{"href", QStringLiteral("#not-stated")}},
                        documentText("What the report could not state"));
        }
        out.close("p");
        out.close("div");
        return;
    }

    const bool severalChannels = master.distribution.size() > 1;
    out.open("div", {{"class", views.empty() || !measured ? QStringLiteral("ftop one") : QStringLiteral("ftop")}});
    if (!views.empty())
    {
        out.open("div");
        writeViewer(out, master, views);
        if (!master.distribution.empty())
        {
            // The spread of the samples stands under the picture, a panel per channel, so the section's two
            // columns are of a height.
            openPanel(out, "h5", documentText("How its samples are spread"),
                      severalChannels ? documentText("a panel per channel, on one axis") : QString());
            out.chart(distributionOf(master));
            out.close("div");
        }
        out.close("div");
    }
    if (measured)
    {
        openPanel(out, "h5", severalChannels ? documentText("Statistics by channel") : documentText("Statistics"),
                  documentText("fraction of full scale"));
        writeStatistics(out, master);
        if (!master.distribution.empty() && views.empty())
        {
            writePanelTitle(out, "h5", documentText("How its samples are spread"));
            out.chart(distributionOf(master));
        }
        out.close("div");
    }
    out.close("div");
    out.close("div");
}

// ---- The stack.

/// The lights that reached a pixel, as one bar.
[[nodiscard]] QString coverageOf(const ReportMaster& master, const ReportStack& stack)
{
    CoverageBar bar;
    bar.description = {documentText("How many lights reached a pixel of %1").arg(master.name),
                       documentText("The stack recorded no coverage.")};
    bar.least = stack.lowestCoverage;
    bar.median = stack.medianCoverage;
    bar.most = stack.highestCoverage;
    bar.notes = {documentText("least %1").arg(formatFigure(stack.lowestCoverage)),
                 documentText("median %1").arg(formatFigure(stack.medianCoverage)),
                 documentText("most %1").arg(formatFigure(stack.highestCoverage))};
    bar.tip.heading = documentText("Lights per pixel");
    bar.tip.lines = {documentText("Least %1").arg(formatFigure(stack.lowestCoverage)),
                     documentText("Median %1").arg(formatFigure(stack.medianCoverage)),
                     documentText("Most %1").arg(formatFigure(stack.highestCoverage)),
                     documentText("Spread %1").arg(formatFigure(stack.coverageDeviation))};
    return coverageBar(bar);
}

void writeStackFacts(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                     const ReportStack& stack)
{
    std::vector<ReportFact> facts;
    if (stack.inputFrames > 0)
    {
        facts.push_back(
            {documentText("Effective frames"),
             documentText("%1 of %2")
                 .arg(formatFigure(stack.effectiveFrames), formatCount(static_cast<qint64>(stack.inputFrames))),
             documentText("How many lights of equal weight and no rejection would give the same noise.")});
        facts.push_back(
            {documentText("SNR gain"),
             documentText("%1 of %2, %3")
                 .arg(formatFigure(stack.measuredSnrGain) + QStringLiteral("×"),
                      formatFigure(stack.theoreticalSnrGain) + QStringLiteral("×"), percentText(stack.efficiency)),
             documentText("The gain in signal to noise the stack measured, of the one its light count promises.")});
    }
    if (stack.rejection.has_value())
    {
        const ReportStackRejection& rejection = *stack.rejection;
        facts.push_back({documentText("Rejected"), percentText(rejection.global),
                         documentText("The share of all the samples the stack rejected.")});
        facts.push_back({documentText("Rejected per light"),
                         documentText("%1 to %2, median %3")
                             .arg(percentText(rejection.lowestFrame), percentText(rejection.highestFrame),
                                  percentText(rejection.medianFrame)),
                         {}});
    }
    if (stack.clusters.has_value())
    {
        const ReportStackClusters& clusters = *stack.clusters;
        facts.push_back({documentText("Rejection clusters"),
                         clusters.count == 0 ? documentText("None found")
                                             : documentText("%1, %2 px in all (%3 of the master), the largest %4 px")
                                                   .arg(formatCount(static_cast<qint64>(clusters.count)),
                                                        formatCount(static_cast<qint64>(clusters.pixels)),
                                                        percentText(clusters.fraction),
                                                        formatCount(static_cast<qint64>(clusters.largestPixels))),
                         documentText("Rejected samples that lie together: a trail, a passing cloud, a reflection.")});
        if (clusters.rejectedElements > 0)
        {
            facts.push_back({documentText("Reached by a rejection map"),
                             documentText("%1 px, %2 of the master")
                                 .arg(formatCount(static_cast<qint64>(clusters.rejectedElements)),
                                      percentText(clusters.rejectedElementFraction)),
                             documentText("The pixels at which either rejection map shows a rejected sample, "
                                          "clustered or not.")});
        }
    }
    if (stack.restack.has_value())
    {
        const ReportRestack& restack = *stack.restack;
        facts.push_back(
            {documentText("Stacked again"),
             restack.applied
                 ? counted(static_cast<qint64>(restack.excluded.size()),
                           QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                             "Yes, without %1 light that lost more than %2 of its samples"),
                           QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                             "Yes, without %1 lights that each lost more than %2 of their samples"))
                       .arg(percentText(restack.flaggedFrameThreshold))
                 : documentText("No: no light lost more than %1 of its samples")
                       .arg(percentText(restack.flaggedFrameThreshold)),
             documentText("A stack that finds a light with too many rejected samples is made a second time "
                          "without it.")});
    }
    if (master.weighting.has_value())
    {
        const ReportMasterWeighting& weighting = *master.weighting;
        facts.push_back({documentText("Weighting"), weighting.mode, {}});
        if (weighting.floorApplies)
        {
            facts.push_back({documentText("Lights at the weight floor"),
                             documentText("%1 at %2")
                                 .arg(formatCount(weighting.framesAtFloor), formatFigure(weighting.minimumWeight)),
                             documentText("The least a light may weigh, relative to the heaviest.")});
        }
        if (weighting.weightedIntegrationMs > 0)
        {
            facts.push_back({documentText("Weighted integration"), formatDurationMs(weighting.weightedIntegrationMs),
                             documentText("The lights' exposure, each counted at its weight.")});
        }
        if (weighting.medianQualityScore.has_value())
        {
            facts.push_back({documentText("Median quality score"), formatFigure(*weighting.medianQualityScore), {}});
        }
    }
    if (stack.weights.has_value())
    {
        const ReportStackWeights& weights = *stack.weights;
        facts.push_back(
            {documentText("Weights at a mean of one"),
             documentText("%1 to %2, spread %3")
                 .arg(formatFigure(weights.lowest), formatFigure(weights.highest), formatFigure(weights.deviation)),
             {}});
        facts.push_back({documentText("Weight efficiency"), percentText(weights.efficiency),
                         documentText("The share of the lights the weights leave in effect: all of them when every "
                                      "light weighs the same.")});
    }
    if (stack.localWeights.has_value())
    {
        const ReportLocalWeights& local = *stack.localWeights;
        facts.push_back(
            {documentText("Local weighting"),
             documentText("window %1 px, grid %2 px, SNR exponent %3")
                 .arg(formatCount(local.windowSize), formatCount(local.gridSpacing), formatFigure(local.snrExponent)),
             documentText("A weight for each part of each light, in place of one for the whole light.")});
        facts.push_back({documentText("Local weights"),
                         documentText("%1 to %2, mean %3, spread %4, over %5 grids")
                             .arg(formatFigure(local.lowest), formatFigure(local.highest), formatFigure(local.mean),
                                  formatFigure(local.deviation), formatCount(static_cast<qint64>(local.grids))),
                         {}});
    }
    if (!stack.normalization.trimmed().isEmpty())
    {
        facts.push_back({documentText("Normalization"), context.said(stack.normalization), {}});
    }
    if (master.overlapRetained.has_value())
    {
        facts.push_back({documentText("Overlap retained"), percentText(*master.overlapRetained),
                         documentText("The share of the capture's area the master covers.")});
    }
    writeFacts(out, facts);
}

/// Which rejection map a cluster was found in.
[[nodiscard]] QString clusterMapName(const ReportClusterMap map)
{
    switch (map)
    {
    case ReportClusterMap::Merged:
        return documentText("Merged");
    case ReportClusterMap::Low:
        return documentText("Low");
    case ReportClusterMap::High:
        return documentText("High");
    }
    return {};
}

/// The clusters of rejected samples a stack found, the largest first, folded away under the stack's figures.
void writeClusters(ReportMarkup& out, const ReportMaster& master, const ReportStackClusters& clusters)
{
    if (clusters.clusters.empty())
    {
        return;
    }
    const std::size_t listed = (std::min)(clusters.clusters.size(), kClustersListed);
    openFold(out, documentText("The clusters, largest first"),
             listed < clusters.clusters.size() ? documentText("the %1 largest of %2")
                                                     .arg(formatCount(static_cast<qint64>(listed)),
                                                          formatCount(static_cast<qint64>(clusters.clusters.size())))
                                               : formatCount(static_cast<qint64>(listed)));
    openTable(
        out, QStringLiteral("clusters"), documentText("The clusters of rejected samples of %1").arg(master.name),
        {{documentText("Map")},
         {documentText("Channel"), true},
         {documentText("Pixels"), true},
         {documentText("From"), true, documentText("The corner of the cluster's box nearest the origin, x and y.")},
         {documentText("To"), true, documentText("The corner of the cluster's box farthest from the origin.")},
         {documentText("Centre"), true}});
    for (std::size_t index = 0; index < listed; ++index)
    {
        const ReportRejectionCluster& cluster = clusters.clusters[index];
        out.open("tr");
        out.element("td", clusterMapName(cluster.map));
        out.element("td", {{"class", QStringLiteral("r num")}}, formatCount(static_cast<qint64>(cluster.channel + 1)));
        out.element("td", {{"class", QStringLiteral("r num")}}, formatCount(static_cast<qint64>(cluster.pixels)));
        out.element("td", {{"class", QStringLiteral("r num")}},
                    QStringLiteral("%1, %2").arg(formatCount(cluster.minX), formatCount(cluster.minY)));
        out.element("td", {{"class", QStringLiteral("r num")}},
                    QStringLiteral("%1, %2").arg(formatCount(cluster.maxX), formatCount(cluster.maxY)));
        out.element("td", {{"class", QStringLiteral("r num")}},
                    QStringLiteral("%1, %2").arg(formatFigure(cluster.centroidX), formatFigure(cluster.centroidY)));
        out.close("tr");
    }
    closeTable(out);
    closeFold(out);
}

/// What became of the astrometric solution, in the words a pill says.
[[nodiscard]] ReportStateWords astrometryPill(const ReportAstrometryOutcome outcome)
{
    switch (outcome)
    {
    case ReportAstrometryOutcome::Inherited:
        return {"good", documentText("Astrometry inherited")};
    case ReportAstrometryOutcome::Solved:
        return {"good", documentText("Astrometry solved")};
    case ReportAstrometryOutcome::None:
        return {"warning", documentText("No astrometry")};
    case ReportAstrometryOutcome::NotRecorded:
        return {};
    }
    return {};
}

[[nodiscard]] QString astrometryMeaning(const ReportAstrometryOutcome outcome)
{
    switch (outcome)
    {
    case ReportAstrometryOutcome::Inherited:
        return documentText("The master carries the solution of its registration reference. Every light was aligned "
                            "to that reference, so its solution holds for the stack; the master itself was not "
                            "plate-solved.");
    case ReportAstrometryOutcome::Solved:
        return documentText("There was no solution to carry from the registration, so the finished master was "
                            "plate-solved.");
    case ReportAstrometryOutcome::None:
        return documentText("The master was written without an astrometric solution.");
    case ReportAstrometryOutcome::NotRecorded:
        return {};
    }
    return {};
}

[[nodiscard]] bool hasAstrometry(const ReportMaster& master)
{
    return master.astrometry != ReportAstrometryOutcome::NotRecorded || master.solution.has_value() ||
           master.photometry.has_value();
}

void writeAstrometry(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    const ReportStateWords words = astrometryWords(master.astrometry);
    openPanel(out, "h5", documentText("Astrometry"), words.kind != nullptr ? words.text.toLower() : QString());
    if (const QString meaning = astrometryMeaning(master.astrometry); !meaning.isEmpty())
    {
        out.element("p", {{"class", QStringLiteral("said")}}, meaning);
    }
    if (!master.astrometryDetail.trimmed().isEmpty())
    {
        out.element("p", {{"class", QStringLiteral("said muted")}}, context.said(master.astrometryDetail.trimmed()));
    }
    std::vector<ReportFact> facts;
    if (master.solution.has_value())
    {
        const ReportAstrometrySolution& solution = *master.solution;
        facts.push_back({documentText("Centre"),
                         QStringLiteral("%1, %2").arg(rightAscensionText(solution.centreRightAscension),
                                                      declinationText(solution.centreDeclination)),
                         {}});
        facts.push_back({documentText("Scale"),
                         documentText("%1 arcseconds per pixel").arg(formatFigure(solution.pixelScale)),
                         {}});
        facts.push_back({documentText("Field"),
                         documentText("%1 by %2 arcminutes")
                             .arg(formatFigure(solution.fieldWidth), formatFigure(solution.fieldHeight)),
                         {}});
        facts.push_back({documentText("Orientation"),
                         solution.mirrored
                             ? documentText("%1 degrees, mirrored").arg(formatFigure(solution.orientation))
                             : documentText("%1 degrees").arg(formatFigure(solution.orientation)),
                         {}});
    }
    if (master.photometry.has_value())
    {
        const ReportMasterPhotometry& photometry = *master.photometry;
        facts.push_back(
            {documentText("Lights with a zero point"), formatCount(photometry.frames),
             documentText("The lights the registration matched to a catalog and measured the brightness scale of.")});
        if (photometry.medianZeroPoint.has_value())
        {
            facts.push_back({documentText("Zero point, median"),
                             documentText("%1 mag").arg(formatFigure(*photometry.medianZeroPoint)),
                             {}});
        }
        if (photometry.medianZeroPointScatter.has_value())
        {
            facts.push_back({documentText("Zero point scatter, median"),
                             documentText("%1 mag").arg(formatFigure(*photometry.medianZeroPointScatter)),
                             {}});
        }
        if (photometry.medianLimitingMagnitude.has_value())
        {
            facts.push_back({documentText("Limiting magnitude, median"),
                             documentText("%1 mag").arg(formatFigure(*photometry.medianLimitingMagnitude)),
                             {}});
        }
        if (photometry.medianCatalogMatches.has_value())
        {
            facts.push_back(
                {documentText("Catalog matches, median"), formatFigure(*photometry.medianCatalogMatches), {}});
        }
    }
    if (master.weighting.has_value() && master.weighting->medianAstrometricRmsArcsec.has_value())
    {
        facts.push_back({documentText("Astrometric residual, median"),
                         documentText("%1 arcseconds").arg(formatFigure(*master.weighting->medianAstrometricRmsArcsec)),
                         documentText("How far the lights' stars lie from the catalog's, after the fit.")});
    }
    writeFacts(out, facts);
    out.close("div");
}

/// The weights of a master's lights that went into the stack.
[[nodiscard]] std::vector<double> usedWeights(const ReportMaster& master)
{
    std::vector<double> weights;
    for (const ReportFrame& frame : master.frames)
    {
        if (isUsed(frame) && frame.weight.has_value())
        {
            weights.push_back(*frame.weight);
        }
    }
    return weights;
}

[[nodiscard]] bool hasTime(const ReportMaster& master)
{
    return master.durationMs.has_value() || !master.finalization.empty();
}

/// The phases of a master's finalization as bars, each with its time or, for one that did not complete, how it
/// ended.
void writeTime(ReportMarkup& out, const ReportMaster& master)
{
    openPanel(out, "h5", documentText("Where the time went"),
              master.durationMs.has_value() ? formatDurationMs(*master.durationMs) : QString());
    if (master.finalization.empty())
    {
        writeCaption(out, documentText("The phases of this master's finalization were not timed."));
    }
    else
    {
        BarsChart bars;
        bars.description = {documentText("The time each phase of finishing %1 took").arg(master.name),
                            documentText("No phase was timed.")};
        bars.width = 340;
        bars.labelWidth = 150;
        for (const ReportPhase& phase : master.finalization)
        {
            const bool completed = phase.status.compare(QStringLiteral("completed"), Qt::CaseInsensitive) == 0;
            ChartBar bar;
            bar.label = phase.name;
            bar.value = static_cast<double>(phase.durationMs);
            bar.valueText = completed ? formatDurationMs(phase.durationMs) : phase.status;
            bar.tip.heading = phase.name;
            bar.tip.lines = {formatDurationMs(phase.durationMs), phase.status};
            bars.bars.push_back(bar);
        }
        out.chart(barsChart(bars));
    }

    if (const std::vector<double> weights = usedWeights(master); !weights.empty())
    {
        DistributionChart chart;
        chart.description = {documentText("Distribution of the weights of the lights of %1").arg(master.name),
                             documentText("No light states a weight.")};
        chart.axis.title = documentText("Weight");
        chart.width = 340;
        chart.height = 170;
        chart.bins = chartBins(weights, 10, 0.0, 1.0);
        for (ChartBin& bin : chart.bins)
        {
            bin.tip.heading = documentText("Weight %1 to %2").arg(formatFigure(bin.low), formatFigure(bin.high));
            bin.tip.lines = {counted(static_cast<qint64>(bin.count),
                                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights"))};
        }
        writePanelTitle(out, "h5", documentText("Weights of its lights"),
                        counted(static_cast<qint64>(weights.size()),
                                QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 used"),
                                QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 used")));
        out.chart(distributionChart(chart));
    }
    out.close("div");
}

void writeStack(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    openSection(out, masterFileAnchor(master) + QStringLiteral("-stack"), documentText("The stack"),
                master.stack.has_value() && master.stack->inputFrames > 0
                    ? counted(static_cast<qint64>(master.stack->inputFrames),
                              QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light combined"),
                              QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights combined"))
                    : QString());
    const bool astrometry = hasAstrometry(master);
    const bool time = hasTime(master);
    if (!master.stack.has_value())
    {
        writeCaption(out, isThere(master) ? documentText("What the stack recorded of itself is not stated for this "
                                                         "master.")
                                          : documentText("The master was not made, so there is no stack to "
                                                         "describe."));
    }
    if (master.stack.has_value() || astrometry || time)
    {
        out.open("div", {{"class", QStringLiteral("g3")}});
        if (master.stack.has_value())
        {
            openPanel(out, "h5", documentText("Coverage"), documentText("lights per pixel"));
            out.chart(coverageOf(master, *master.stack));
            writeStackFacts(out, context, master, *master.stack);
            if (master.stack->clusters.has_value())
            {
                writeClusters(out, master, *master.stack->clusters);
            }
            out.close("div");
        }
        if (astrometry)
        {
            writeAstrometry(out, context, master);
        }
        if (time)
        {
            writeTime(out, master);
        }
        out.close("div");
    }
    out.close("div");
}

// ---- Its lights.

/// One measure of a master's lights: its name on the switch and over its layer, how a tip states it, and its
/// value of a light.
struct Measure
{
    const char* label;
    const char* title;
    const char* inTip;
    std::function<std::optional<double>(const ReportFrame&)> value;
};

[[nodiscard]] const std::vector<Measure>& measures()
{
    static const std::vector<Measure> all = {
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "FWHM"), QT_TRANSLATE_NOOP("ImageIntegrationReport", "FWHM (px)"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "FWHM %1 px"),
         [](const ReportFrame& frame) { return frame.fwhm; }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Eccentricity"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Eccentricity"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Eccentricity %1"),
         [](const ReportFrame& frame) { return frame.eccentricity; }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Stars"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Stars detected"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Stars %1"),
         [](const ReportFrame& frame) {
             return frame.stars.has_value() ? std::optional<double>(static_cast<double>(*frame.stars)) : std::nullopt;
         }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "SNR"), QT_TRANSLATE_NOOP("ImageIntegrationReport", "SNR"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "SNR %1"), [](const ReportFrame& frame) { return frame.snr; }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Background"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Background noise"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Background noise %1"),
         [](const ReportFrame& frame) { return frame.backgroundNoise; }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Zero point"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Zero point (mag)"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Zero point %1 mag"),
         [](const ReportFrame& frame) { return frame.zeroPoint; }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Weight"), QT_TRANSLATE_NOOP("ImageIntegrationReport", "Weight"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Weight %1"),
         [](const ReportFrame& frame) { return frame.weight; }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Rejected"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Samples rejected (%)"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Rejected %1 %"),
         [](const ReportFrame& frame) {
             return frame.rejectedFraction.has_value() ? std::optional<double>(100.0 * *frame.rejectedFraction)
                                                       : std::nullopt;
         }},
        {QT_TRANSLATE_NOOP("ImageIntegrationReport", "Quality"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Quality score"),
         QT_TRANSLATE_NOOP("ImageIntegrationReport", "Quality score %1"),
         [](const ReportFrame& frame) { return frame.qualityScore; }},
    };
    return all;
}

/// The measures at least one light of a master states.
[[nodiscard]] std::vector<const Measure*> measuresOf(const std::vector<const ReportFrame*>& lights)
{
    std::vector<const Measure*> stated;
    for (const Measure& measure : measures())
    {
        if (stated.size() < kMeasuresAtMost &&
            std::any_of(lights.begin(), lights.end(),
                        [&measure](const ReportFrame* frame) { return measure.value(*frame).has_value(); }))
        {
            stated.push_back(&measure);
        }
    }
    return stated;
}

/// The chart of a master's lights, measure by measure, with the switch that shows one measure at a time.
void writeMeasures(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                   const std::vector<const ReportFrame*>& lights, const std::vector<const Measure*>& stated)
{
    FrameMeasureChart chart;
    chart.description = {
        documentText("The lights of %1 in the order they were taken, measure by measure").arg(master.name),
        documentText("No light of this master states a measure to draw.")};
    chart.id = master.slug;
    for (const Measure* measure : stated)
    {
        ChartMeasure layer;
        layer.title = documentText(measure->title);
        std::vector<double> used;
        for (const ReportFrame* frame : lights)
        {
            const std::optional<double> value = measure->value(*frame);
            layer.values.push_back(valueOf(value));
            if (value.has_value() && isUsed(*frame))
            {
                used.push_back(*value);
            }
        }
        if (const std::optional<double> median = medianOfValues(used); median.has_value())
        {
            layer.reference = *median;
            layer.referenceLabel = documentText("median %1").arg(formatFigure(*median));
        }
        chart.measures.push_back(layer);
    }
    std::optional<QDate> night;
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const ReportFrame& frame = *lights[index];
        ChartLight light;
        light.state = chartStateOf(frame);
        light.tip.heading = frame.fileName;
        if (frame.capturedAt.has_value())
        {
            light.tip.lines << formatDateTime(*frame.capturedAt, context.zone);
        }
        for (const Measure* measure : stated)
        {
            if (const std::optional<double> value = measure->value(frame); value.has_value())
            {
                light.tip.lines << documentText(measure->inTip).arg(formatFigure(*value));
            }
        }
        light.tip.lines << fateLine(frame);
        chart.lights.push_back(light);
        if (frame.night.has_value() && frame.night != night)
        {
            night = frame.night;
            chart.nights.push_back(ChartMarker{.index = index, .label = formatDate(*frame.night)});
        }
    }
    if (!lights.empty() && lights.back()->capturedAt.has_value())
    {
        chart.lastLabel = clockText(*lights.back()->capturedAt, context.zone);
    }

    out.open("div", {{"class", QStringLiteral("fchart")}});
    const QString name = QStringLiteral("m-") + master.slug;
    if (stated.size() > 1)
    {
        for (std::size_t index = 0; index < stated.size(); ++index)
        {
            out.leaf("input", {{"type", QStringLiteral("radio")},
                               {"class", QStringLiteral("pick pick-%1").arg(index)},
                               {"name", name},
                               {"id", QStringLiteral("%1-%2").arg(name).arg(index)},
                               {"checked", index == 0 ? QStringLiteral("checked") : QString()}});
        }
        out.open("div", {{"class", QStringLiteral("switch noprint")}});
        for (std::size_t index = 0; index < stated.size(); ++index)
        {
            out.element("label", {{"for", QStringLiteral("%1-%2").arg(name).arg(index)}},
                        documentText(stated[index]->label));
        }
        out.close("div");
    }
    writeScrollingChart(out, frameMeasureChart(chart), chart.description.label);
    out.close("div");
    writeCaption(out, lights.size() > kChartDenseAbove
                          ? documentText("A cross is a light left out. With this many lights the chart draws its "
                                         "line alone: each light's figures are in the table below.")
                          : documentText("A cross is a light left out. Point at a light for all its figures."));
}

/// What a light left out has to say for itself, beyond the name of its state.
[[nodiscard]] QString leftOutDetail(const ReportFrame& frame)
{
    QStringList parts;
    if (frame.state == ReportFrameState::ExcludedByUser)
    {
        parts << documentText("Left out in the lights' list");
    }
    else if (frame.state == ReportFrameState::StackExcludedByUser)
    {
        parts << documentText("Left out of the stack alone: it was still registered");
    }
    if (!frame.stateDetail.trimmed().isEmpty())
    {
        parts << frame.stateDetail.trimmed();
    }
    if (frame.state == ReportFrameState::RegistrationFailed && frame.stars.has_value())
    {
        parts << counted(*frame.stars, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 star detected"),
                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 stars detected"));
    }
    if (frame.state == ReportFrameState::ExcludedOnRestack && frame.rejectedFraction.has_value())
    {
        parts << documentText("the first stack rejected %1 of its samples").arg(percentText(*frame.rejectedFraction));
    }
    return parts.join(dotSeparator());
}

void writeLeftOut(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                  const std::vector<const ReportFrame*>& lights)
{
    std::vector<std::pair<std::size_t, const ReportFrame*>> leftOut;
    bool stated = false;
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        stated = stated || lights[index]->state.has_value();
        if (lights[index]->state.has_value() && !isUsed(*lights[index]))
        {
            leftOut.emplace_back(index, lights[index]);
        }
    }
    openPanel(out, "h5", documentText("Left out"),
              stated ? documentText("%1 of %2")
                           .arg(formatCount(static_cast<qint64>(leftOut.size())),
                                formatCount(static_cast<qint64>(lights.size())))
                     : QString());
    if (!stated)
    {
        writeCaption(out, documentText("The run recorded no light's fate for this master."));
    }
    else if (!isThere(master))
    {
        writeCaption(out, documentText("The master was not made, so none of its lights was stacked."));
    }
    else if (leftOut.empty())
    {
        writeCaption(out, documentText("Every light of this group went into the master."));
    }
    else
    {
        openTable(out, QStringLiteral("leftout"), documentText("The lights of %1 that were left out").arg(master.name),
                  {{documentText("Light"), true},
                   {documentText("Taken")},
                   {documentText("Why")},
                   {documentText("Detail")},
                   {documentText("File")}});
        for (const auto& [index, frame] : leftOut)
        {
            out.open("tr");
            out.element("td", {{"class", QStringLiteral("r num")}}, formatCount(static_cast<qint64>(index + 1)));
            out.element("td", {{"class", QStringLiteral("nowrap")}},
                        frame->capturedAt.has_value() ? shortInstant(*frame->capturedAt, context.zone) : noValueText());
            out.open("td", {{"class", QStringLiteral("nowrap")}});
            writeStateName(out, chartStateOf(*frame));
            out.close("td");
            out.element("td", context.said(leftOutDetail(*frame)));
            out.open("td");
            out.element("code", frame->fileName);
            out.close("td");
            out.close("tr");
        }
        closeTable(out);
    }
}

/// One column of the table of a master's lights: its heading, what it means, and a light's value as the cell
/// prints it and as the table is sorted by it.
struct LightColumn
{
    QString heading;
    QString meaning;
    bool text = false;

    /// Whether the column is the lights' exposure, which a group of one exposure states in its heading instead.
    bool exposure = false;

    std::function<bool(const ReportFrame&)> stated;
    std::function<QString(const ReportFrame&)> shown;
    std::function<QString(const ReportFrame&)> sortedBy;
};

/// A figure as a table is sorted by it: plain digits a script reads as a number.
[[nodiscard]] QString sortValue(const double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 6) : QString();
}

[[nodiscard]] LightColumn figureColumn(const QString& heading, const QString& meaning,
                                       const std::function<std::optional<double>(const ReportFrame&)>& value)
{
    return LightColumn{
        .heading = heading,
        .meaning = meaning,
        .text = false,
        .exposure = false,
        .stated = [value](const ReportFrame& frame) { return value(frame).has_value(); },
        .shown = [value](const ReportFrame& frame) { return formatFigure(value(frame)); },
        .sortedBy = [value](const ReportFrame& frame) { return sortValue(valueOf(value(frame))); },
    };
}

[[nodiscard]] std::vector<LightColumn> lightColumns(const ReportDocumentContext& context)
{
    const ReportTimeZone zone = context.zone;
    std::vector<LightColumn> columns;
    columns.push_back(LightColumn{
        .heading = documentText("Taken"),
        .meaning = {},
        .text = true,
        .exposure = false,
        .stated = [](const ReportFrame& frame) { return frame.capturedAt.has_value(); },
        .shown =
            [zone](const ReportFrame& frame) {
                return frame.capturedAt.has_value() ? shortInstant(*frame.capturedAt, zone) : noValueText();
            },
        .sortedBy =
            [](const ReportFrame& frame) {
                return frame.capturedAt.has_value() ? QString::number(frame.capturedAt->toSecsSinceEpoch()) : QString();
            },
    });
    columns.push_back(figureColumn(documentText("Exp. s"), documentText("The exposure, in seconds."),
                                   [](const ReportFrame& frame) { return frame.exposureSeconds; }));
    columns.back().exposure = true;
    columns.push_back(figureColumn(documentText("FWHM"),
                                   documentText("The width of a star at half its height, in pixels."),
                                   [](const ReportFrame& frame) { return frame.fwhm; }));
    columns.push_back(figureColumn(documentText("Ecc."),
                                   documentText("How far from round the stars are: zero is round."),
                                   [](const ReportFrame& frame) { return frame.eccentricity; }));
    columns.push_back(figureColumn(
        documentText("Stars"), documentText("The stars the registration detected."), [](const ReportFrame& frame) {
            return frame.stars.has_value() ? std::optional<double>(static_cast<double>(*frame.stars)) : std::nullopt;
        }));
    columns.push_back(figureColumn(documentText("SNR"), {}, [](const ReportFrame& frame) { return frame.snr; }));
    columns.push_back(figureColumn(documentText("Background"), documentText("The noise of the light's background."),
                                   [](const ReportFrame& frame) { return frame.backgroundNoise; }));
    columns.push_back(figureColumn(documentText("Quality"), documentText("The registration's quality score."),
                                   [](const ReportFrame& frame) { return frame.qualityScore; }));
    columns.push_back(figureColumn(documentText("Zero pt"), documentText("The photometric zero point, in magnitudes."),
                                   [](const ReportFrame& frame) { return frame.zeroPoint; }));
    columns.push_back(figureColumn(documentText("Lim. mag"), documentText("The faintest magnitude the light reaches."),
                                   [](const ReportFrame& frame) { return frame.limitingMagnitude; }));
    columns.push_back(figureColumn(
        documentText("Matches"), documentText("The stars matched to the catalog."), [](const ReportFrame& frame) {
            return frame.catalogMatches.has_value() ? std::optional<double>(static_cast<double>(*frame.catalogMatches))
                                                    : std::nullopt;
        }));
    columns.push_back(figureColumn(documentText("Residual"),
                                   documentText("How far the light's stars lie from the catalog's, in arcseconds."),
                                   [](const ReportFrame& frame) { return frame.astrometricRmsArcsec; }));
    columns.push_back(figureColumn(documentText("Shift x"), documentText("The shift to the reference, in pixels."),
                                   [](const ReportFrame& frame) { return frame.shiftX; }));
    columns.push_back(figureColumn(documentText("Shift y"), documentText("The shift to the reference, in pixels."),
                                   [](const ReportFrame& frame) { return frame.shiftY; }));
    columns.push_back(figureColumn(documentText("Scale"),
                                   documentText("The normalization's scale, of the first channel."),
                                   [](const ReportFrame& frame) {
                                       return frame.normalizationScale.empty()
                                                  ? std::nullopt
                                                  : std::optional<double>(frame.normalizationScale.front());
                                   }));
    columns.push_back(figureColumn(documentText("Rejected %"),
                                   documentText("The share of the light's samples the stack rejected."),
                                   [](const ReportFrame& frame) {
                                       return frame.rejectedFraction.has_value()
                                                  ? std::optional<double>(100.0 * *frame.rejectedFraction)
                                                  : std::nullopt;
                                   }));
    columns.push_back(figureColumn(documentText("Weight"),
                                   documentText("The light's weight, relative to the heaviest light."),
                                   [](const ReportFrame& frame) { return frame.weight; }));
    columns.push_back(figureColumn(documentText("Share %"),
                                   documentText("The light's share of the stack: its exposure at its weight, of "
                                                "the group's."),
                                   [](const ReportFrame& frame) {
                                       return frame.stackShare.has_value()
                                                  ? std::optional<double>(100.0 * *frame.stackShare)
                                                  : std::nullopt;
                                   }));
    columns.push_back(LightColumn{
        .heading = documentText("State"),
        .meaning = {},
        .text = true,
        .exposure = false,
        .stated = [](const ReportFrame&) { return true; },
        .shown =
            [](const ReportFrame& frame) {
                return frame.state.has_value() ? stateName(chartStateOf(frame)) : documentText("Not recorded");
            },
        .sortedBy =
            [](const ReportFrame& frame) {
                return frame.state.has_value() ? stateName(chartStateOf(frame)) : documentText("Not recorded");
            },
    });
    columns.push_back(LightColumn{
        .heading = documentText("File"),
        .meaning = {},
        .text = true,
        .exposure = false,
        .stated = [](const ReportFrame&) { return true; },
        .shown = [](const ReportFrame& frame) { return frame.fileName; },
        .sortedBy = [](const ReportFrame& frame) { return frame.fileName; },
    });
    return columns;
}

/// Every light of a master, a row each, with every measure a light of the group states: a column no light states
/// is left out. A heading sorts the table by its column.
void writeLightsTable(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                      const std::vector<const ReportFrame*>& lights)
{
    // A column no light states is left out, and so is the exposure where every light has the same: the section's
    // heading says it.
    const bool oneExposure =
        master.exposure.bins.size() <= 1 && master.exposure.framesWithExposure == master.exposure.frames;
    std::vector<LightColumn> columns;
    for (const LightColumn& column : lightColumns(context))
    {
        if (column.exposure && oneExposure)
        {
            continue;
        }
        if (std::any_of(lights.begin(), lights.end(),
                        [&column](const ReportFrame* frame) { return column.stated(*frame); }))
        {
            columns.push_back(column);
        }
    }

    out.open("div", {{"class", QStringLiteral("tablewrap tall")},
                     {"tabindex", QStringLiteral("0")},
                     {"role", QStringLiteral("group")},
                     {"aria-label", documentText("Every light of %1").arg(master.name)}});
    out.open("table", {{"class", QStringLiteral("sortable lights")}});
    out.open("thead");
    out.open("tr");
    const auto writeHeading = [&out](const QString& heading, const QString& meaning, const bool text) {
        out.open("th", {{"scope", QStringLiteral("col")},
                        {"class", text ? QStringLiteral("t") : QString()},
                        {"tabindex", QStringLiteral("0")},
                        {"data-tip", meaning.isEmpty() ? QString() : heading + QLatin1Char('\n') + meaning}});
        out.text(heading);
        out.close("th");
    };
    writeHeading(QStringLiteral("#"), documentText("The light's place in the order the lights were taken."), false);
    for (const LightColumn& column : columns)
    {
        writeHeading(column.heading, column.meaning, column.text);
    }
    out.close("tr");
    out.close("thead");
    out.open("tbody");
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const ReportFrame& frame = *lights[index];
        const bool leftOut = frame.state.has_value() && !isUsed(frame);
        out.open("tr", {{"class", leftOut ? QStringLiteral("out") : QString()}});
        out.element("td", QString::number(index + 1));
        for (const LightColumn& column : columns)
        {
            // A cell is sorted by its text, read as a number where it is one. It states another key only where
            // the text does not say it: a time, a figure with a thin space or a power of ten in it.
            const QString shown = column.shown(frame);
            const QString sortedBy = column.sortedBy(frame);
            bool plain = false;
            shown.toDouble(&plain);
            const bool keyed = !sortedBy.isEmpty() && sortedBy != shown && !plain;
            out.element(
                "td",
                {{"class", column.text ? QStringLiteral("t") : QString()}, {"data-v", keyed ? sortedBy : QString()}},
                shown);
        }
        out.close("tr");
    }
    out.close("tbody");
    out.close("table");
    out.close("div");
}

void writeLights(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    const std::vector<const ReportFrame*> lights = lightsInCaptureOrder(master);
    const std::vector<const Measure*> stated = measuresOf(lights);
    openSection(out, masterFileAnchor(master) + QStringLiteral("-lights"), documentText("Its lights"),
                exposuresText(master.exposure));

    const std::optional<ReportDither> dither = ditherOf(context, {&master}, 280);
    if (!stated.empty() || dither.has_value())
    {
        out.open("div", {{"class", QStringLiteral("fgrid")}});
        if (!stated.empty())
        {
            openPanel(out, "h5", {});
            writeMeasures(out, context, master, lights, stated);
            out.close("div");
        }
        if (dither.has_value())
        {
            openPanel(out, "h5", documentText("Where each light sat"), documentText("against the reference"));
            out.chart(dither->chart);
            out.chart(dither->legend);
            out.close("div");
        }
        out.close("div");
    }
    else
    {
        writeCaption(out, documentText("The run recorded no measure of these lights."));
    }

    writeLeftOut(out, context, master, lights);
    openFold(out,
             counted(static_cast<qint64>(lights.size()),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "The %1 light, every measure"),
                     QT_TRANSLATE_NOOP("ImageIntegrationReport", "All %1 lights, every measure")),
             documentText("select a heading to sort"));
    writeLightsTable(out, context, master, lights);
    closeFold(out);
    out.close("div");

    out.close("div");
}

// ---- How it was made.

/// What a master's file says of the copy the run's archive keeps of it.
[[nodiscard]] QString retentionText(const ReportMaster& master)
{
    switch (master.retention)
    {
    case ReportRetention::Retained:
        return documentText("a copy is in the run's archive");
    case ReportRetention::NotRetainedByPolicy:
        return master.retentionReason.trimmed().isEmpty()
                   ? documentText("no copy in the run's archive: the history's limits left no room")
                   : documentText("no copy in the run's archive: %1").arg(master.retentionReason.trimmed());
    case ReportRetention::NotRetainedByArchiveFailure:
        return master.retentionReason.trimmed().isEmpty()
                   ? documentText("the copy for the run's archive could not be written")
                   : documentText("the copy for the run's archive could not be written: %1")
                         .arg(master.retentionReason.trimmed());
    case ReportRetention::Unknown:
        return {};
    }
    return {};
}

void writeFiles(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    // A master that was not made wrote no file of its name.
    const bool written = isThere(master) && !master.fileName.isEmpty();
    if (!written && master.maps.empty())
    {
        return;
    }
    out.open("div", {{"class", QStringLiteral("setcard")}});
    out.open("h5", {{"class", QStringLiteral("ptitle")}});
    out.element("span", documentText("Files written"));
    out.close("h5");
    openTable(out, QStringLiteral("files"), documentText("The files written for %1").arg(master.name),
              {{documentText("File")}, {documentText("What")}, {documentText("Size"), true}});
    if (written)
    {
        out.open("tr");
        out.open("td");
        out.element("code", master.fileName);
        out.close("td");
        const QString retention = retentionText(master);
        out.element("td", retention.isEmpty() ? documentText("The master")
                                              : documentText("The master") + dotSeparator() + context.said(retention));
        out.element("td", {{"class", QStringLiteral("r num")}},
                    master.bytes.has_value() ? formatByteSize(*master.bytes) : noValueText());
        out.close("tr");
    }
    for (const ReportMap& map : master.maps)
    {
        out.open("tr");
        out.open("td");
        out.element("code", QFileInfo(map.file).fileName());
        out.close("td");
        out.element("td", map.title);
        out.element("td", {{"class", QStringLiteral("r num")}},
                    map.bytes.has_value() ? formatByteSize(*map.bytes) : noValueText());
        out.close("tr");
    }
    closeTable(out);
    // The rejection maps the settings ask for, where the run wrote fewer.
    const int rejectionMaps =
        static_cast<int>(std::count_if(master.maps.begin(), master.maps.end(),
                                       [](const ReportMap& map) { return map.kind == ReportMapKind::Rejection; }));
    if (isThere(master) && master.rejectionMapsExpected.has_value() && rejectionMaps < *master.rejectionMapsExpected)
    {
        writeCaption(out, counted(*master.rejectionMapsExpected,
                                  QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                                    "The settings ask for %1 rejection map; %2 were written."),
                                  QT_TRANSLATE_NOOP("ImageIntegrationReport",
                                                    "The settings ask for %1 rejection maps; %2 were written."))
                              .arg(formatCount(rejectionMaps)));
    }
    out.close("div");
}

void writeLightsCalibration(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                            const ReportSettingsMajority& majority)
{
    for (const QString& key : master.lightGroups)
    {
        const ReportLightGroup* group = lightGroupOfKey(context.model, key);
        if (group == nullptr)
        {
            continue;
        }
        openSettings(out,
                     master.lightGroups.size() > 1 ? documentText("Lights calibration") + dotSeparator() + group->name
                                                   : documentText("Lights calibration"),
                     "h5");
        out.open("div");
        out.element("dt", documentText("Lights"));
        out.element("dd", formatCount(group->lights));
        out.close("div");
        if (group->preCalibrated)
        {
            out.open("div");
            out.element("dt", documentText("Calibration"));
            out.element("dd", documentText("Marked as calibrated already: no master is applied"));
            out.close("div");
        }
        else
        {
            const auto link = [&](const QString& name, const QString& file, const QString& slug) {
                const ReportCalibrationMaster* taken = calibrationOfSlug(context.model, slug);
                writeSettingLink(out, context, name, slug, file,
                                 taken != nullptr ? calibrationStateNote(taken->state) : QString());
            };
            link(documentText("Master bias"), group->biasMaster, group->biasMasterSlug);
            link(documentText("Master dark"), group->darkMaster, group->darkMasterSlug);
            link(documentText("Master flat"), group->flatMaster, group->flatMasterSlug);
        }
        for (const ReportSettingsBlock& block : group->settings)
        {
            writeSettingRows(out, context, block, &majority);
        }
        closeSettings(out);
    }
}

void writeRegisteredIn(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    const ReportRegistrationGroup* group = registrationOfKey(context.model, master.registrationGroupKey);
    if (group == nullptr && master.registrationInputPolicy.trimmed().isEmpty())
    {
        return;
    }
    openSettings(out, documentText("Registered in"), "h5");
    if (group != nullptr)
    {
        writeSettingLink(out, context, documentText("Registration group"), group->slug, group->name);
    }
    if (!master.registrationInputPolicy.trimmed().isEmpty())
    {
        out.open("div");
        out.element("dt", documentText("The stack took"));
        out.element("dd", context.said(master.registrationInputPolicy.trimmed()));
        out.close("div");
    }
    closeSettings(out);
}

void writeMade(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
               const ReportSettingsMajority& stacking, const ReportSettingsMajority& calibration)
{
    const bool several = context.model.masters.size() > 1;
    openSection(out, masterFileAnchor(master) + QStringLiteral("-made"), documentText("How it was made"),
                several ? documentText("this master's own settings; what it shares with others is stated once, "
                                       "further down")
                        : QString());
    out.open("div", {{"class", QStringLiteral("made")}});
    out.open("div");
    writeLightsCalibration(out, context, master, calibration);
    writeRegisteredIn(out, context, master);
    writeFiles(out, context, master);
    out.close("div");
    out.open("div");
    for (const ReportSettingsBlock& block : master.settings)
    {
        writeSettingsBlock(out, context, block, {}, "h5", &stacking);
    }
    out.close("div");
    out.close("div");
    if (stacking.groups() > 2 || calibration.groups() > 2)
    {
        writeMarkKey(out, documentText("marks a value most masters do not use; point at it for what they use."));
    }
    out.close("div");
}

// ---- A combined colour image.

/// The stacks a combined colour image is made from: the red, green and blue masters of its registration group.
[[nodiscard]] std::vector<const ReportMaster*> sourcesOf(const ReportModel& model, const ReportMaster& combined)
{
    static const std::array<QStringList, 3> kNames = {{{QStringLiteral("red"), QStringLiteral("r")},
                                                       {QStringLiteral("green"), QStringLiteral("g")},
                                                       {QStringLiteral("blue"), QStringLiteral("b")}}};
    std::vector<const ReportMaster*> sources;
    for (const QStringList& names : kNames)
    {
        const auto found = std::find_if(model.masters.begin(), model.masters.end(), [&](const ReportMaster& master) {
            return master.kind == ReportMasterKind::Stack &&
                   master.registrationGroupKey == combined.registrationGroupKey &&
                   names.contains(master.filter.trimmed().toLower());
        });
        if (found != model.masters.end())
        {
            sources.push_back(&*found);
        }
    }
    return sources;
}

void writeCombination(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    openSection(out, masterFileAnchor(master) + QStringLiteral("-made"), documentText("How it was made"), {});
    out.open("div", {{"class", QStringLiteral("g3")}});

    openPanel(out, "h5", documentText("Made from"));
    const std::vector<const ReportMaster*> sources = sourcesOf(context.model, master);
    out.open("p", {{"class", QStringLiteral("said")}});
    if (sources.size() == 3)
    {
        out.text(documentText("The stacks of"));
        for (std::size_t index = 0; index < sources.size(); ++index)
        {
            out.text(index == 0 ? QStringLiteral(" ") : QStringLiteral(", "));
            writeMasterLink(out, context, *sources[index], context.labelOf(*sources[index]));
        }
        out.text(documentText(", one channel each. It has no lights of its own."));
    }
    else
    {
        out.text(documentText("The red, green and blue stacks of its registration group, one channel each. It has "
                              "no lights of its own."));
    }
    out.close("p");
    openFacts(out);
    if (const ReportRegistrationGroup* group = registrationOfKey(context.model, master.registrationGroupKey);
        group != nullptr)
    {
        openFact(out, documentText("Registration group"));
        writeAnchorLink(out, context, group->slug, group->name);
        closeFact(out);
    }
    if (master.lightsCombined.has_value())
    {
        writeFact(out, documentText("Lights behind it"), formatCount(*master.lightsCombined));
    }
    closeFacts(out);
    out.close("div");

    if (hasAstrometry(master))
    {
        writeAstrometry(out, context, master);
    }
    if (hasTime(master))
    {
        writeTime(out, master);
    }
    out.close("div");
    writeFiles(out, context, master);
    out.close("div");
}

// ---- The file.

/// The line under a master's name: its lights and its integration, or what it is made from.
[[nodiscard]] QString fileLine(const ReportMaster& master)
{
    QStringList parts;
    if (master.kind == ReportMasterKind::CombinedRgb)
    {
        parts << documentText("Combined from the red, green and blue masters");
    }
    else
    {
        const MasterUse use = useOf(master);
        if (!isThere(master))
        {
            parts << counted(use.taken, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light, not stacked"),
                             QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights, not stacked"));
        }
        else if (use.used.has_value())
        {
            parts << documentText("%1 of %2 lights").arg(formatCount(*use.used), formatCount(use.taken))
                  << formatDurationMs(*use.integratedMs);
        }
        else
        {
            parts << counted(use.taken, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                             QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights"))
                  << formatDurationMs(master.exposure.integrationMs);
        }
    }
    if (isThere(master) && !master.fileName.isEmpty())
    {
        parts << master.fileName;
    }
    return parts.join(dotSeparator());
}

void writeFileHeading(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                      const std::vector<const ReportIssue*>& flags)
{
    out.open("summary");
    out.icon("chevron", QStringLiteral("chev"));
    if (const QString source = context.assets.thumbnail(master.preview); !source.isEmpty())
    {
        out.leaf("img", {{"class", QStringLiteral("thumb")},
                         {"src", source},
                         {"alt", QStringLiteral("")},
                         {"loading", QStringLiteral("lazy")}});
    }
    else
    {
        out.element("span", {{"class", QStringLiteral("thumb nopic")}}, documentText("No picture"));
    }
    out.open("h3");
    out.element("span", context.labelOf(master));
    if (master.kind == ReportMasterKind::CombinedRgb)
    {
        out.element("small", documentText("Combination"));
    }
    else if (context.labelOf(master) != master.name)
    {
        out.element("small", master.name);
    }
    out.close("h3");
    out.element("span", {{"class", QStringLiteral("fline")}}, fileLine(master));

    out.open("span", {{"class", QStringLiteral("pills")}});
    if (const ReportStateWords words = masterStateWords(master.state); words.kind != nullptr)
    {
        writePill(out, words.kind, words.text);
    }
    if (!flags.empty())
    {
        const bool critical = std::any_of(flags.begin(), flags.end(), [](const ReportIssue* issue) {
            return issue->severity == ReportIssueSeverity::Critical;
        });
        writePill(out, critical ? "critical" : "warning",
                  counted(static_cast<qint64>(flags.size()),
                          QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 to look at"),
                          QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 to look at")));
    }
    if (const ReportStateWords words = astrometryPill(master.astrometry); words.kind != nullptr && isThere(master))
    {
        out.open("span", {{"data-tip", words.text + QLatin1Char('\n') + astrometryMeaning(master.astrometry)}});
        writePill(out, words.kind, words.text);
        out.close("span");
    }
    out.close("span");
    out.close("summary");
}

/// What a file says first of a master the run did not make, or made earlier.
void writeStateNote(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master)
{
    const char* kind = nullptr;
    QString text;
    switch (master.state)
    {
    case ReportMasterState::Made:
        return;
    case ReportMasterState::Reused:
        kind = "reused";
        text = master.madeByRun.isEmpty()
                   ? documentText("This master was still valid, so the run did not make it again. The history no "
                                  "longer holds the report of the run that made it: the picture and the statistics "
                                  "below are read from the master's file, and what its stack recorded of itself is "
                                  "not stated.")
                   : documentText("This master was still valid, so the run did not make it again. The picture, the "
                                  "figures and the lights below are those of the run that made it.");
        break;
    case ReportMasterState::Failed:
        kind = "critical";
        text = documentText("The run could not make this master.");
        break;
    case ReportMasterState::NotReached:
        kind = "skipped";
        text = documentText("The run stopped before it reached this master, so nothing of it was made.");
        break;
    case ReportMasterState::Cancelled:
        kind = "skipped";
        text = documentText("The run was cancelled before it reached this master, so nothing of it was made.");
        break;
    }
    // Why a master was not made is the model's sentence where it has one: a master the run never reached has no
    // more to say than that.
    const QString failure = context.said(master.failure.trimmed());
    const bool reached = master.state == ReportMasterState::Failed || master.state == ReportMasterState::Reused;
    out.open("div", {{"class", QStringLiteral("alert is-") + QLatin1String(kind)}});
    out.icon(kind);
    out.open("div");
    out.element("p", !reached && !failure.isEmpty() ? failure : text);
    if (reached && !failure.isEmpty())
    {
        out.element("p", {{"class", QStringLiteral("muted")}}, failure);
    }
    if (!master.madeByRun.isEmpty())
    {
        // The run that made it, as a link to its report where the history holds one. A document that travels alone
        // names no other file.
        const auto earlier = std::find_if(context.model.history.begin(), context.model.history.end(),
                                          [&master](const ReportHistoryEntry& entry) {
                                              return entry.runId == master.madeByRun && !entry.report.isEmpty();
                                          });
        out.open("p", {{"class", QStringLiteral("muted")}});
        out.text(documentText("Made by run") + QLatin1Char(' '));
        if (earlier != context.model.history.end() && context.assets.form() == ReportForm::Linked)
        {
            out.element("a", {{"href", earlier->report}}, master.madeByRun);
        }
        else
        {
            out.element("b", master.madeByRun);
        }
        out.close("p");
    }
    out.close("div");
    out.close("div");
}

void writeFlags(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<const ReportIssue*>& flags)
{
    if (flags.empty())
    {
        return;
    }
    out.open("ul", {{"class", QStringLiteral("flags")}});
    for (const ReportIssue* issue : flags)
    {
        const char* kind = issue->severity == ReportIssueSeverity::Critical ? "critical" : "warning";
        out.open("li", {{"class", QStringLiteral("is-") + QLatin1String(kind)}});
        out.icon(kind);
        out.element("span", context.assets.message(issue->title));
        out.close("li");
    }
    out.close("ul");
    if (context.hasAnchor(QStringLiteral("attention")))
    {
        out.open("p", {{"class", QStringLiteral("cap noprint")}});
        out.element("a", {{"href", QStringLiteral("#attention")}}, documentText("What each means, and what to do"));
        out.close("p");
    }
}

void writeFile(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master, const bool open,
               const ReportSettingsMajority& stacking, const ReportSettingsMajority& calibration)
{
    const std::vector<const ReportIssue*> flags = flagsOf(context.model, master);
    out.open("details", {{"class", QStringLiteral("file")},
                         {"id", masterFileAnchor(master)},
                         {"open", open ? QStringLiteral("open") : QString()}});
    writeFileHeading(out, context, master, flags);
    out.open("div", {{"class", QStringLiteral("fbody")}});
    writeStateNote(out, context, master);
    writeFlags(out, context, flags);

    if (master.kind == ReportMasterKind::CombinedRgb)
    {
        if (isThere(master))
        {
            writePicture(out, context, master);
        }
        writeCombination(out, context, master);
    }
    else
    {
        // A master that is on disk has its four sections whatever the report could measure; one that was not made
        // has the ones that have something to say.
        if (isThere(master))
        {
            writePicture(out, context, master);
        }
        if (isThere(master) || hasTime(master))
        {
            writeStack(out, context, master);
        }
        if (!master.frames.empty())
        {
            writeLights(out, context, master);
        }
        writeMade(out, context, master, stacking, calibration);
    }
    out.close("div");
    out.close("details");
}
} // namespace

// ---- What the writer calls.

QString masterFileAnchor(const ReportMaster& master)
{
    return QStringLiteral("file-") + master.slug;
}

QStringList mastersAnchors(const ReportModel& model)
{
    QStringList anchors;
    if (model.masters.empty())
    {
        return anchors;
    }
    anchors << QStringLiteral("masters");
    for (const ReportMaster& master : model.masters)
    {
        anchors << masterFileAnchor(master);
    }
    return anchors;
}

ReportPart writeReportMasters(const ReportDocumentContext& context)
{
    const ReportModel& model = context.model;
    ReportPart part;
    if (model.masters.empty())
    {
        return part;
    }
    const bool one = model.masters.size() == 1;
    part.heading =
        QStringLiteral("2") + dotSeparator() + (one ? documentText("The master") : documentText("Master by master"));

    // What most groups of a stage use, so that a value most do not use can say so.
    ReportSettingsMajority stacking;
    for (const ReportMaster& master : model.masters)
    {
        if (master.kind == ReportMasterKind::Stack)
        {
            stacking.add(master.settings);
        }
    }
    ReportSettingsMajority calibration;
    for (const ReportLightGroup& group : model.lightGroups)
    {
        calibration.add(group.settings);
    }

    ReportMarkup& out = part.body;
    openPart(out, "masters", 2, one ? documentText("The master") : documentText("Master by master"),
             one ? documentText("The picture and its maps, the stack, its lights one by one, and the settings of its "
                                "groups.")
                 : documentText("One file per master. Each opens into the picture and its maps, the stack, its "
                                "lights one by one, and the settings of its own groups."));

    const auto writeEntry = [&](const ReportMaster& master) {
        const MasterUse use = useOf(master);
        const bool attention = !isThere(master) || !flagsOf(model, master).empty();
        part.entries.push_back({masterFileAnchor(master), one ? documentText("Its file") : context.labelOf(master),
                                use.used.has_value() && isThere(master)
                                    ? QStringLiteral("%1/%2").arg(formatCount(*use.used), formatCount(use.taken))
                                    : QString(),
                                attention});
    };

    bool first = true;
    const auto writeFilesOf = [&](const QString& target, const bool byTarget) {
        for (const ReportMaster& master : model.masters)
        {
            if (byTarget && master.target != target)
            {
                continue;
            }
            // The first file is open, so the part shows what a file holds; a run of one master has nothing to fold.
            writeFile(out, context, master, first, stacking, calibration);
            writeEntry(master);
            first = false;
        }
    };
    if (model.targets.size() > 1)
    {
        for (const ReportTarget& target : model.targets)
        {
            out.open("p", {{"class", QStringLiteral("targethead")}});
            out.element("b", target.name);
            out.element("small", counted(target.frames, QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 light"),
                                         QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 lights")) +
                                     dotSeparator() + formatDurationMs(target.integrationMs));
            out.close("p");
            writeFilesOf(target.name, true);
        }
        // A master of no listed target still has its file.
        for (const ReportMaster& master : model.masters)
        {
            const bool listed =
                std::any_of(model.targets.begin(), model.targets.end(),
                            [&master](const ReportTarget& target) { return target.name == master.target; });
            if (!listed)
            {
                writeFile(out, context, master, first, stacking, calibration);
                writeEntry(master);
                first = false;
            }
        }
    }
    else
    {
        writeFilesOf({}, false);
    }

    out.close("section");
    return part;
}
} // namespace ImageIntegration::Reporting

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The readout's parts: the figures it reads out and how it writes them; the icon button its headers and tools are; a
// card's header, which folds it, and its surface; the Statistics card's six figures at three ranges, grey or in a
// colour table; the Histogram card's plot and the histogram window's tools acting on it; the Astrometry card, solved
// and not, its chips bound to the actions that draw the overlays; the Image card's rows; the column that holds the
// four, its width, its folds and their remembered line; and a task's compact readout in both widths. A value being
// measured again is drawn at 55 %.

#include "RenderTestSupport.h"

#include "components/AnAstrometryCard.h"
#include "components/AnChip.h"
#include "components/AnCompactReadout.h"
#include "components/AnElidedLabel.h"
#include "components/AnHistogramCard.h"
#include "components/AnIconButton.h"
#include "components/AnImageFactsCard.h"
#include "components/AnReadoutCard.h"
#include "components/AnReadoutColumn.h"
#include "components/AnReadoutGrid.h"
#include "components/AnSegmentedControl.h"
#include "components/AnStatisticsCard.h"
#include "components/ReadoutFigures.h"
#include "theme/ComponentParts.h"
#include "fixtures/ObservatoryLook.h"
#include "widgets/HistogramWidget.h"

#include <QAction>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <memory>
#include <string>

namespace
{

using an::tests::widgets::accessibleName;
using an::tests::widgets::accessibleRole;
using an::tests::widgets::expectColour;
using an::tests::widgets::forEachLook;
using an::tests::widgets::forEachRatio;
using an::tests::widgets::recordedText;
using an::tests::widgets::recordedTexts;
using an::tests::widgets::RenderedPicture;
using an::tests::widgets::showOffScreen;
using an::ui::DesignTokens;
using an::ui::ReadoutStatistic;
using an::ui::StatisticsScale;

/// A channel's figures, all of them distinct and inside the range.
an::ui::ImageStatisticsChannel channelAround(const double centre)
{
    an::ui::ImageStatisticsChannel channel;
    channel.count = 40u;
    channel.pixels = 40u;
    channel.mean = centre;
    channel.median = centre + 0.01;
    channel.minimum = centre - 0.1;
    channel.maximum = centre + 0.2;
    channel.standardDeviation = 0.05;
    channel.averageDeviation = 0.04;
    channel.biweightMidvariance = 0.001;
    channel.variance = 0.0025;
    channel.medianAbsoluteDeviation = 0.03;
    channel.backgroundNoise = 0.002;
    return channel;
}

an::ui::ImageStatisticsReport reportOf(const std::vector<double>& centres)
{
    an::ui::ImageStatisticsReport report;
    for (const double centre : centres)
    {
        report.channels.push_back(channelAround(centre));
    }
    return report;
}

/// The figure of a channel the statistics window shows in the row of a statistic, at a scale.
double windowFigure(const an::ui::ImageStatisticsChannel& channel, const ReadoutStatistic statistic,
                    const StatisticsScale scale)
{
    const auto rows = an::ui::scaledStatisticsFigures(channel, scale);
    switch (statistic)
    {
    case ReadoutStatistic::Minimum:
        return rows[3];
    case ReadoutStatistic::Maximum:
        return rows[4];
    case ReadoutStatistic::Mean:
        return rows[0];
    case ReadoutStatistic::Median:
        return rows[2];
    case ReadoutStatistic::StandardDeviation:
        return rows[5];
    case ReadoutStatistic::MedianAbsoluteDeviation:
        return rows[9];
    }
    return NAN;
}

an::ui::AstrometryReadout solvedReadout()
{
    return an::ui::AstrometryReadout{.solved = true,
                                     .rightAscension = QStringLiteral("13:25:27.60"),
                                     .declination = QStringLiteral("-43:01:09.0"),
                                     .scale = QStringLiteral("0.772"),
                                     .rotation = QStringLiteral("68.97"),
                                     .solver = QStringLiteral("Internal Gaia"),
                                     .distortion = QStringLiteral("SIP 4/4")};
}

/// Counts of one channel: `samples` of which `low` at zero and `high` at one.
std::shared_ptr<const an::ui::HistogramDisplayData> countsOf(const std::uint64_t samples, const std::uint64_t low,
                                                             const std::uint64_t high)
{
    auto counts = std::make_shared<an::ui::HistogramDisplayData>();
    an::ui::HistogramDisplayChannel channel;
    channel.bins.assign(256u, 1u);
    channel.samples = samples;
    channel.clippedLow = low;
    channel.clippedHigh = high;
    counts->channels.push_back(channel);
    return counts;
}

QStringList cellLabels(const an::ui::AnReadoutGrid& grid)
{
    QStringList labels;
    for (const an::ui::ReadoutCell& cell : grid.cells())
    {
        labels.append(cell.label);
    }
    return labels;
}

/// Clicks a widget at its left, away from anything laid in it.
void clickAtLeft(QWidget& widget)
{
    QTest::mouseClick(&widget, Qt::LeftButton, {}, QPoint(4, widget.height() / 2));
}

} // namespace

// ---- The figures and how they are written

// Each statistic is the statistics window's figure of the same row, at the same scale.
TEST(ReadoutFigures, EachStatisticIsTheWindowsFigureOfItsRow)
{
    const an::ui::ImageStatisticsChannel channel = channelAround(0.4);
    for (const StatisticsScale scale :
         {StatisticsScale::Normalized, StatisticsScale::Bits8, StatisticsScale::Bits16, StatisticsScale::Bits32})
    {
        for (const ReadoutStatistic statistic : an::ui::kReadoutStatistics)
        {
            EXPECT_DOUBLE_EQ(an::ui::readoutFigure(channel, statistic, scale), windowFigure(channel, statistic, scale))
                << an::ui::readoutStatisticName(statistic).toStdString();
        }
    }
    EXPECT_EQ(an::ui::readoutStatisticName(ReadoutStatistic::MedianAbsoluteDeviation), QStringLiteral("MAD"));
}

// A figure is written to four decimals as a fraction, two as an eight-bit code, one as a sixteen-bit code; a change
// carries its sign, and one under 0.00005 either way is none.
TEST(ReadoutFigures, FiguresAndChangesAreWrittenAsTheReadoutWritesThem)
{
    EXPECT_EQ(an::ui::formatReadoutFigure(0.04123, StatisticsScale::Normalized), QStringLiteral("0.0412"));
    EXPECT_EQ(an::ui::formatReadoutFigure(10.514, StatisticsScale::Bits8), QStringLiteral("10.51"));
    EXPECT_EQ(an::ui::formatReadoutFigure(2701.34, StatisticsScale::Bits16), QStringLiteral("2701.3"));
    EXPECT_EQ(an::ui::formatReadoutFigure(NAN, StatisticsScale::Normalized), QStringLiteral("-"));
    EXPECT_EQ(an::ui::formatReadoutDelta(0.0123), QStringLiteral("+0.0123"));
    EXPECT_EQ(an::ui::formatReadoutDelta(-0.0123), QStringLiteral("−0.0123"));
    EXPECT_EQ(an::ui::formatReadoutDelta(0.00004), QStringLiteral("±0"));
    EXPECT_EQ(an::ui::formatReadoutDelta(-0.00004), QStringLiteral("±0"));
}

// A colour image's figure is the mean of its three colour channels' figures; a grey image's, its channel's.
TEST(ReadoutFigures, AnImagesFigureIsItsColourChannelsMean)
{
    const an::ui::ImageStatisticsReport colour = reportOf({0.2, 0.4, 0.6, 0.9});
    EXPECT_EQ(an::ui::readoutChannelCount(colour), 3u);
    EXPECT_NEAR(an::ui::readoutImageFigure(colour, ReadoutStatistic::Mean, StatisticsScale::Normalized), 0.4, 1e-12);
    const an::ui::ImageStatisticsReport grey = reportOf({0.25});
    EXPECT_EQ(an::ui::readoutChannelCount(grey), 1u);
    EXPECT_NEAR(an::ui::readoutImageFigure(grey, ReadoutStatistic::Maximum, StatisticsScale::Normalized), 0.45, 1e-12);
    EXPECT_TRUE(std::isnan(an::ui::readoutImageFigure(an::ui::ImageStatisticsReport{}, ReadoutStatistic::Mean,
                                                      StatisticsScale::Normalized)));
}

// The clipping is the share of every channel's samples at either end; resolutions are named as the window names them.
TEST(ReadoutFigures, ClippingSharesAndResolutionNames)
{
    const an::ui::ClippingShare share = an::ui::clippingShare(*countsOf(2000u, 47u, 4u));
    EXPECT_DOUBLE_EQ(share.low, 2.35);
    EXPECT_DOUBLE_EQ(share.high, 0.2);
    EXPECT_EQ(an::ui::formatClipShare(share.low), QStringLiteral("2.350 %"));
    EXPECT_DOUBLE_EQ(an::ui::clippingShare(an::ui::HistogramDisplayData{}).low, 0.0);
    EXPECT_EQ(an::ui::histogramResolutionName(an::ui::HistogramResolution::Bits8), QStringLiteral("8-bit (256)"));
    EXPECT_EQ(an::ui::histogramResolutionShortName(an::ui::HistogramResolution::Bits14), QStringLiteral("14-bit"));
    EXPECT_EQ(an::ui::histogramResolutionShortName(an::ui::HistogramResolution::Levels1000),
              QStringLiteral("1000 levels"));
}

// ---- The icon button

// An icon button is a strip's icon button square in both densities, and names itself only in its tooltip and to
// assistive technology; its toggle form is a check box there.
TEST(IconButton, IsAStripsIconButtonSquareAndNamesItself)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnIconButton button(QStringLiteral("zoom_in"), QStringLiteral("Zoom in"));
        EXPECT_EQ(button.sizeHint(), QSize(t.metrics.controlXSmall, t.metrics.controlXSmall));
        EXPECT_EQ(button.toolTip(), QStringLiteral("Zoom in"));
        EXPECT_EQ(accessibleName(button), QStringLiteral("Zoom in"));
        EXPECT_EQ(accessibleRole(button), QAccessible::Button);
        button.resize(button.sizeHint());
        EXPECT_TRUE(recordedTexts(button).empty()) << "an icon button draws no text";
        button.setCheckable(true);
        EXPECT_EQ(accessibleRole(button), QAccessible::CheckBox);
    });
}

// Checked, an icon button is the accent's container behind its icon; unchecked, it shows what lies under it.
TEST(IconButton, PaintsItsCheckedState)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnIconButton button(QStringLiteral("grid"), QStringLiteral("Show grid"));
        button.setCheckable(true);
        button.resize(button.sizeHint());
        const QColor bg = t.layers.surface2;
        forEachRatio(button, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(2, button.height() / 2.0), bg, "unchecked, left of the icon");
        });
        button.setChecked(true);
        forEachRatio(button, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(2, button.height() / 2.0), t.colors.accentContainer, "checked, left of the icon");
        });
    });
}

// ---- A card

// A card's header is a section header high, its title in caps; a click on it folds the card, hiding its body, and a
// second opens it; each is told once, and assistive technology hears "folded".
TEST(ReadoutCard, AClickOnTheHeaderFoldsTheCard)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnStatisticsCard card;
        card.resize(360, 300);
        showOffScreen(card);
        an::ui::AnReadoutCardHeader* header = card.header();
        EXPECT_EQ(header->height(), t.metrics.sectionHeader);
        const an::ui::testing::RecordedText title = recordedText(*header, QStringLiteral("Statistics"));
        ASSERT_EQ(title.text, QStringLiteral("Statistics"));
        EXPECT_EQ(title.font.capitalization(), QFont::AllUppercase);
        EXPECT_EQ(accessibleName(*header), QStringLiteral("Statistics"));

        QSignalSpy folded(&card, &an::ui::AnReadoutCard::foldedChanged);
        clickAtLeft(*header);
        EXPECT_TRUE(card.isFolded());
        EXPECT_FALSE(card.body()->isVisible());
        EXPECT_EQ(accessibleName(*header), QStringLiteral("Statistics, folded"));
        clickAtLeft(*header);
        EXPECT_FALSE(card.isFolded());
        EXPECT_TRUE(card.body()->isVisible());
        ASSERT_EQ(folded.count(), 2);
        EXPECT_TRUE(folded.at(0).at(0).toBool());
        EXPECT_FALSE(folded.at(1).at(0).toBool());
    });
}

// Space or Enter on the header folds the card; a click on one of the header's tools does not.
TEST(ReadoutCard, TheKeyboardFoldsTheCardAndAToolDoesNot)
{
    an::ui::AnStatisticsCard card;
    card.resize(360, 300);
    showOffScreen(card);
    QTest::keyClick(card.header(), Qt::Key_Space);
    EXPECT_TRUE(card.isFolded());
    QTest::keyClick(card.header(), Qt::Key_Return);
    EXPECT_FALSE(card.isFolded());
    QSignalSpy asked(&card, &an::ui::AnStatisticsCard::statisticsWindowRequested);
    QTest::mouseClick(card.windowButton(), Qt::LeftButton);
    EXPECT_EQ(asked.count(), 1);
    EXPECT_FALSE(card.isFolded());
}

// A card is `surface2` with a 12 px radius on what lies under it.
TEST(ReadoutCard, PaintsItsSurfaceWithItsRadius)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnImageFactsCard card;
        card.resize(360, card.sizeHint().height());
        showOffScreen(card);
        const QColor bg = t.layers.surface1;
        forEachRatio(card, bg, [&](const RenderedPicture& picture) {
            expectColour(picture.at(0.2, 0.2), bg, "the corner, outside the radius");
            expectColour(picture.at(card.width() / 2.0, card.height() - 3), t.layers.surface2, "inside the foot");
            // Half a radius in: clear of the rows' text, which starts further in.
            expectColour(picture.at(an::ui::parts::kReadoutCardRadius / 2.0, card.height() / 2.0), t.layers.surface2,
                         "inside the left side");
        });
    });
}

// ---- The Statistics card

// A grey image reads out Min, Max, Mean, Median, StdDev and MAD in three columns, each its channel's figure.
TEST(StatisticsCard, AGreyImageReadsOutTheSixFigures)
{
    const an::ui::ImageStatisticsReport report = reportOf({0.4});
    an::ui::AnStatisticsCard card;
    card.setReport(report);
    EXPECT_FALSE(card.showsTable());
    EXPECT_EQ(card.greyReadouts()->columns(), 3);
    EXPECT_EQ(cellLabels(*card.greyReadouts()),
              (QStringList{QStringLiteral("Min"), QStringLiteral("Max"), QStringLiteral("Mean"),
                           QStringLiteral("Median"), QStringLiteral("StdDev"), QStringLiteral("MAD")}));
    for (std::size_t index = 0u; index < an::ui::kReadoutStatistics.size(); ++index)
    {
        const ReadoutStatistic statistic = an::ui::kReadoutStatistics[index];
        EXPECT_DOUBLE_EQ(card.figure(statistic),
                         windowFigure(report.channels[0], statistic, StatisticsScale::Normalized));
        EXPECT_EQ(card.greyReadouts()->cells()[index].value, card.figureText(statistic));
    }
    EXPECT_EQ(card.figureText(ReadoutStatistic::Mean), QStringLiteral("0.4000"));
}

// The header's three segments - Normalized, 8-bit, 16-bit - show the same figures at the window's three scales; a
// change of range is told and writes them again.
TEST(StatisticsCard, TheThreeRangesScaleTheFiguresAsTheWindowDoes)
{
    const an::ui::ImageStatisticsReport report = reportOf({0.4});
    an::ui::AnStatisticsCard card;
    card.setReport(report);
    ASSERT_EQ(card.rangePicker()->count(), 3);
    EXPECT_EQ(card.rangePicker()->segmentText(0), QStringLiteral("Normalized"));
    EXPECT_EQ(card.rangePicker()->segmentText(1), QStringLiteral("8-bit"));
    EXPECT_EQ(card.rangePicker()->segmentText(2), QStringLiteral("16-bit"));
    QSignalSpy changed(&card, &an::ui::AnStatisticsCard::scaleChanged);
    const std::array<StatisticsScale, 3> scales{StatisticsScale::Normalized, StatisticsScale::Bits8,
                                                StatisticsScale::Bits16};
    for (int index = 2; index >= 0; --index)
    {
        card.rangePicker()->segmentButton(index)->click();
        const StatisticsScale scale = scales[static_cast<std::size_t>(index)];
        EXPECT_EQ(card.scale(), scale);
        for (const ReadoutStatistic statistic : an::ui::kReadoutStatistics)
        {
            EXPECT_DOUBLE_EQ(card.figure(statistic), windowFigure(report.channels[0], statistic, scale));
            EXPECT_EQ(card.figureText(statistic), an::ui::formatReadoutFigure(card.figure(statistic), scale));
        }
    }
    EXPECT_EQ(changed.count(), 3);
    EXPECT_EQ(card.figureText(ReadoutStatistic::Maximum), QStringLiteral("0.6000"));
}

// A colour image shows a table: a column per colour channel, headed by its letter, a row per statistic; an alpha
// channel is not shown.
TEST(StatisticsCard, AColourImageShowsATableOfItsThreeChannels)
{
    forEachLook([](const DesignTokens& t) {
        const an::ui::ImageStatisticsReport report = reportOf({0.2, 0.4, 0.6, 0.9});
        an::ui::AnStatisticsCard card;
        card.setReport(report);
        card.resize(360, 400);
        showOffScreen(card);
        EXPECT_TRUE(card.showsTable());
        EXPECT_TRUE(card.colourTable()->isVisible());
        EXPECT_FALSE(card.greyReadouts()->isVisible());
        EXPECT_EQ(card.colourTable()->height(), 7 * t.metrics.rowKeyValue);
        const std::vector<an::ui::testing::RecordedText> texts = recordedTexts(*card.colourTable());
        const auto painted = [&texts](const QString& text) {
            return std::any_of(texts.begin(), texts.end(),
                               [&text](const an::ui::testing::RecordedText& run) { return run.text == text; });
        };
        for (const QString& letter : {QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")})
        {
            EXPECT_TRUE(painted(letter)) << letter.toStdString();
        }
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            for (const ReadoutStatistic statistic : an::ui::kReadoutStatistics)
            {
                EXPECT_DOUBLE_EQ(card.figure(statistic, channel),
                                 windowFigure(report.channels[channel], statistic, StatisticsScale::Normalized));
                EXPECT_TRUE(painted(card.figureText(statistic, channel)))
                    << card.figureText(statistic, channel).toStdString();
            }
        }
        EXPECT_TRUE(std::isnan(card.figure(ReadoutStatistic::Mean, 3u))) << "the alpha channel is not shown";
        EXPECT_EQ(recordedText(*card.colourTable(), card.figureText(ReadoutStatistic::Mean, 1u)).colour,
                  t.colors.textPrimary);
    });
}

// Stale figures are drawn at 55 %, their names as they were; fresh ones are drawn whole again.
TEST(StatisticsCard, StaleFiguresAreDrawnAtFiftyFivePercent)
{
    an::ui::AnStatisticsCard card;
    card.setReport(reportOf({0.4}));
    card.resize(360, 300);
    showOffScreen(card);
    an::ui::AnReadoutGrid& grid = *card.greyReadouts();
    card.setStale(true);
    EXPECT_NEAR(recordedText(grid, QStringLiteral("0.4000")).opacity, an::ui::parts::kReadoutStaleOpacity, 1e-6);
    EXPECT_NEAR(recordedText(grid, QStringLiteral("Mean")).opacity, 1.0, 1e-6);
    card.setStale(false);
    EXPECT_NEAR(recordedText(grid, QStringLiteral("0.4000")).opacity, 1.0, 1e-6);
}

// ---- The Histogram card

// The tools start as the histogram window's - the grid, the clipping and the areas on, absolute scales off, 8-bit
// counts - and each acts on the histogram widget as the window's does.
TEST(HistogramCard, TheToolsStartAsTheWindowsAndActOnItsWidget)
{
    using an::ui::HistogramCardTool;
    an::ui::AnHistogramCard card;
    card.resize(360, 320);
    showOffScreen(card);
    HistogramWidget& plot = *card.histogramWidget();
    EXPECT_TRUE(plot.getGridEnabled());
    EXPECT_TRUE(plot.getClippingInfoEnabled());
    EXPECT_FALSE(plot.getLocalChannelNormalization());
    EXPECT_TRUE(plot.getAreaModeEnabled());
    EXPECT_EQ(plot.getHistogramResolution(), an::ui::HistogramResolution::Bits8);
    EXPECT_EQ(card.resolutionPicker()->currentText(), QStringLiteral("8-bit (256)"));
    EXPECT_EQ(card.resolutionPicker()->count(), 11);
    EXPECT_TRUE(card.tool(HistogramCardTool::Grid)->isChecked());
    EXPECT_EQ(card.tool(HistogramCardTool::AbsoluteScales)->toolTip(), QStringLiteral("Absolute scales"));

    card.tool(HistogramCardTool::Grid)->click();
    EXPECT_FALSE(plot.getGridEnabled());
    card.tool(HistogramCardTool::ClippingInfo)->click();
    EXPECT_FALSE(plot.getClippingInfoEnabled());
    card.tool(HistogramCardTool::AbsoluteScales)->click();
    EXPECT_TRUE(plot.getLocalChannelNormalization());
    card.tool(HistogramCardTool::Areas)->click();
    EXPECT_FALSE(plot.getAreaModeEnabled());

    const QSize fitted = plot.size();
    card.tool(HistogramCardTool::ZoomIn)->click();
    EXPECT_GT(plot.width(), fitted.width());
    card.tool(HistogramCardTool::ZoomOut)->click();
    card.tool(HistogramCardTool::ZoomOut)->click();
    EXPECT_LT(plot.width(), fitted.width());
    card.tool(HistogramCardTool::ZoomReset)->click();
    EXPECT_EQ(plot.size(), plot.parentWidget()->size());
}

// Another resolution is told and named to the widget, and counts nothing: the counts shown stay until whoever counts
// makes them again.
TEST(HistogramCard, AnotherResolutionIsToldAndCountsNothing)
{
    an::ui::AnHistogramCard card;
    const auto counts = countsOf(256u, 0u, 0u);
    card.setCounts(counts);
    QSignalSpy changed(&card, &an::ui::AnHistogramCard::resolutionChanged);
    card.resolutionPicker()->setCurrentText(QStringLiteral("12-bit (4096)"));
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(card.resolution(), an::ui::HistogramResolution::Bits12);
    EXPECT_EQ(card.histogramWidget()->getHistogramResolution(), an::ui::HistogramResolution::Bits12);
    EXPECT_EQ(card.histogramWidget()->getHistogram(), counts);
}

// The header says "RGB" or "Grayscale", and its tool asks for the histogram window; the curves drawn follow the
// channels the view shows.
TEST(HistogramCard, TheHeaderNamesTheChannelsAndItsToolAsksForTheWindow)
{
    an::ui::AnHistogramCard card;
    card.setChannelCount(3u);
    EXPECT_EQ(card.summary(), QStringLiteral("RGB"));
    card.setChannelCount(1u);
    EXPECT_EQ(card.summary(), QStringLiteral("Grayscale"));
    card.setChannelCount(0u);
    EXPECT_TRUE(card.summary().isEmpty());
    EXPECT_EQ(card.windowButton()->toolTip(), QStringLiteral("Open the Histogram window (Ctrl+H)"));
    QSignalSpy asked(&card, &an::ui::AnHistogramCard::histogramWindowRequested);
    card.windowButton()->click();
    EXPECT_EQ(asked.count(), 1);

    auto counts = std::make_shared<an::ui::HistogramDisplayData>();
    for (const std::uint64_t peak : {10u, 20u, 30u})
    {
        an::ui::HistogramDisplayChannel channel;
        channel.bins.assign(256u, 1u);
        channel.bins[100] = peak;
        counts->channels.push_back(channel);
    }
    card.setCounts(counts);
    card.setShownChannels(true, false, false);
    EXPECT_DOUBLE_EQ(card.histogramWidget()->getMaxDisplayedHistogramValue(), 10.0);
    card.setShownChannels(true, true, true);
    EXPECT_DOUBLE_EQ(card.histogramWidget()->getMaxDisplayedHistogramValue(), 30.0);
}

// The plot sits in a well at least 100 px high, `bgWell` with an 8 px radius, its square corners inside the round ones.
TEST(HistogramCard, ThePlotSitsInAWell)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnHistogramCard card;
        card.resize(360, 260);
        showOffScreen(card);
        an::ui::AnReadoutWell& well = *card.well();
        EXPECT_GE(well.height(), an::ui::parts::kReadoutWellMinimum);
        const QPoint origin = well.mapTo(&card, QPoint(0, 0));
        forEachRatio(card, t.layers.surface1, [&](const RenderedPicture& picture) {
            expectColour(picture.at(origin.x() + 0.5, origin.y() + 0.5), t.layers.surface2, "the well's corner");
            expectColour(picture.at(origin.x() + 1.5, origin.y() + well.height() / 2.0), t.colors.bgWell,
                         "the well's side, outside the plot");
            expectColour(picture.at(origin.x() + well.width() / 2.0, origin.y() + 1.5), t.colors.bgWell,
                         "the well's top, outside the plot");
        });
    });
}

// Stale counts are drawn at 55 %: the plot is laid through an opacity effect, and drawn whole again once fresh.
TEST(HistogramCard, StaleCountsAreDrawnAtFiftyFivePercent)
{
    an::ui::AnHistogramCard card;
    auto* scroll = card.findChild<QScrollArea*>(QStringLiteral("readoutHistogramScrollArea"));
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(scroll->graphicsEffect(), nullptr);
    card.setStale(true);
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(scroll->graphicsEffect());
    ASSERT_NE(effect, nullptr);
    EXPECT_NEAR(effect->opacity(), an::ui::parts::kReadoutStaleOpacity, 1e-6);
    card.setStale(false);
    EXPECT_EQ(scroll->graphicsEffect(), nullptr);
}

// ---- The Astrometry card

// Solved, the card reads out the field centre, the scale and the rotation with their units, says "Solved", the solver
// and the distortion in its header, and offers its five chips, Plate Solve, Annotate... and Info.
TEST(AstrometryCard, SolvedItReadsOutTheSolutionAndOffersItsTools)
{
    an::ui::AnAstrometryCard card;
    card.resize(360, 400);
    showOffScreen(card);
    card.setReadout(solvedReadout());
    EXPECT_EQ(card.summary(), QStringLiteral("Solved · Internal Gaia · SIP 4/4"));
    ASSERT_TRUE(card.readouts()->isVisible());
    const std::vector<an::ui::ReadoutCell>& cells = card.readouts()->cells();
    ASSERT_EQ(cells.size(), 4u);
    EXPECT_EQ(cells[0].label, QStringLiteral("Field centre RA"));
    EXPECT_EQ(cells[0].value, QStringLiteral("13:25:27.60"));
    EXPECT_EQ(cells[1].label, QStringLiteral("Field centre Dec"));
    EXPECT_EQ(cells[2].label, QStringLiteral("Scale"));
    EXPECT_EQ(cells[2].value, QStringLiteral("0.772"));
    EXPECT_EQ(cells[2].unit, QStringLiteral("″/px"));
    EXPECT_EQ(cells[3].unit, QStringLiteral("deg"));
    const QStringList chips{QStringLiteral("Grid"), QStringLiteral("Annotations"), QStringLiteral("Star labels"),
                            QStringLiteral("Detected stars"), QStringLiteral("SIP map")};
    for (std::size_t index = 0u; index < an::ui::kAstrometryOverlays; ++index)
    {
        an::ui::AnChip* chip = card.overlayChip(static_cast<an::ui::AstrometryOverlay>(index));
        EXPECT_EQ(chip->text(), chips[static_cast<int>(index)]);
        EXPECT_TRUE(chip->isCheckable());
        EXPECT_TRUE(chip->isVisible());
        EXPECT_TRUE(chip->toolTip().contains(QStringLiteral("(Ctrl+%1)").arg(index + 1u)))
            << chip->toolTip().toStdString();
    }
    EXPECT_TRUE(card.plateSolveButton()->isVisible());
    EXPECT_TRUE(card.annotateButton()->isVisible());
    EXPECT_TRUE(card.infoButton()->isVisible());
    EXPECT_FALSE(card.unsolvedText()->isVisible());
    EXPECT_EQ(card.plateSolveButton()->property("anButtonVariant").toString(), QStringLiteral("tonal"));
}

// Not solved, the card says so and offers Plate Solve alone.
TEST(AstrometryCard, NotSolvedItSaysSoAndOffersPlateSolve)
{
    an::ui::AnAstrometryCard card;
    card.resize(360, 400);
    showOffScreen(card);
    card.setReadout(solvedReadout());
    card.setReadout(an::ui::AstrometryReadout{});
    EXPECT_EQ(card.summary(), QStringLiteral("Not solved"));
    EXPECT_TRUE(card.unsolvedText()->isVisible());
    EXPECT_EQ(card.unsolvedText()->text(), QStringLiteral("No astrometric solution"));
    EXPECT_FALSE(card.readouts()->isVisible());
    EXPECT_FALSE(card.overlayChip(an::ui::AstrometryOverlay::Grid)->isVisible());
    EXPECT_TRUE(card.plateSolveButton()->isVisible());
    EXPECT_FALSE(card.annotateButton()->isVisible());
    EXPECT_FALSE(card.infoButton()->isVisible());
}

// A chip is bound to the action that draws its overlay: a click toggles the action and is told; the action toggled
// another way - the view's menu, its shortcut - moves the chip and tells nothing; with no action the chip is disabled.
TEST(AstrometryCard, EachChipTogglesItsOverlayAndFollowsTheAction)
{
    an::ui::AnAstrometryCard card;
    card.setReadout(solvedReadout());
    an::ui::AnChip* grid = card.overlayChip(an::ui::AstrometryOverlay::Grid);
    EXPECT_FALSE(grid->isEnabled());
    QAction drawsTheGrid;
    drawsTheGrid.setCheckable(true);
    card.bindOverlay(an::ui::AstrometryOverlay::Grid, &drawsTheGrid);
    EXPECT_TRUE(grid->isEnabled());
    EXPECT_FALSE(grid->isChecked());

    QSignalSpy toggled(&card, &an::ui::AnAstrometryCard::overlayToggled);
    grid->click();
    EXPECT_TRUE(drawsTheGrid.isChecked());
    ASSERT_EQ(toggled.count(), 1);
    EXPECT_EQ(toggled.at(0).at(0).toInt(), static_cast<int>(an::ui::AstrometryOverlay::Grid));
    EXPECT_TRUE(toggled.at(0).at(1).toBool());

    drawsTheGrid.toggle();
    EXPECT_FALSE(grid->isChecked());
    EXPECT_EQ(toggled.count(), 1) << "a chip that follows its action tells nothing";

    QAction other;
    other.setCheckable(true);
    other.setChecked(true);
    card.bindOverlay(an::ui::AstrometryOverlay::Grid, &other);
    EXPECT_TRUE(grid->isChecked());
    drawsTheGrid.toggle();
    EXPECT_TRUE(grid->isChecked()) << "the chip follows only the action it is bound to";
    card.bindOverlay(an::ui::AstrometryOverlay::Grid, nullptr);
    EXPECT_FALSE(grid->isEnabled());
}

// Plate Solve, Annotate... and Info ask for their windows; the header's tool opens the settings menu it is handed.
TEST(AstrometryCard, TheButtonsAskForTheirWindowsAndTheToolOpensTheSettings)
{
    an::ui::AnAstrometryCard card;
    card.setReadout(solvedReadout());
    QSignalSpy solve(&card, &an::ui::AnAstrometryCard::plateSolveRequested);
    QSignalSpy annotate(&card, &an::ui::AnAstrometryCard::annotateRequested);
    QSignalSpy info(&card, &an::ui::AnAstrometryCard::infoRequested);
    card.plateSolveButton()->click();
    card.annotateButton()->click();
    card.infoButton()->click();
    EXPECT_EQ(solve.count(), 1);
    EXPECT_EQ(annotate.count(), 1);
    EXPECT_EQ(info.count(), 1);

    EXPECT_FALSE(card.settingsButton()->isEnabled());
    QMenu settings;
    settings.addAction(QStringLiteral("Level of Detail"));
    card.setSettingsMenu(&settings);
    EXPECT_TRUE(card.settingsButton()->isEnabled());
    card.settingsButton()->click();
    EXPECT_TRUE(settings.isVisible());
    settings.close();
}

// ---- The Image card

// The facts are key and value rows a key-value row high, a figure's value in the monospace; folded, the header
// carries them in one line; its tools ask for the FITS Header, EXIF Viewer and Summary windows.
TEST(ImageFactsCard, TheRowsAreTheFactsAndTheFoldedHeaderCarriesThem)
{
    forEachLook([](const DesignTokens& t) {
        const an::ui::ImageFactsReadout facts{.dimensions = QStringLiteral("4432 × 3840"),
                                              .format = QStringLiteral("16 bit unsigned integer"),
                                              .sizeInMemory = QStringLiteral("97.38 MB"),
                                              .channels = QStringLiteral("RGB"),
                                              .summary = QStringLiteral("4432x3840 · 16 bit uint · 97.38 MB")};
        an::ui::AnImageFactsCard card;
        card.setFacts(facts);
        card.resize(360, 300);
        showOffScreen(card);
        EXPECT_EQ(card.value(QStringLiteral("Dimensions")), facts.dimensions);
        EXPECT_EQ(card.value(QStringLiteral("Format")), facts.format);
        EXPECT_EQ(card.value(QStringLiteral("Size in memory")), facts.sizeInMemory);
        EXPECT_EQ(card.value(QStringLiteral("Channels")), facts.channels);
        EXPECT_EQ(card.rows()->height(), 4 * t.metrics.rowKeyValue);
        EXPECT_EQ(recordedText(*card.rows(), QStringLiteral("97.38 MB")).font.families(),
                  t.typography.monospace.families());
        EXPECT_TRUE(card.summary().isEmpty());
        card.setFolded(true);
        EXPECT_EQ(card.summary(), facts.summary);
        card.setFolded(false);
        EXPECT_TRUE(card.summary().isEmpty());
    });
}

TEST(ImageFactsCard, ItsToolsAskForTheirWindows)
{
    an::ui::AnImageFactsCard card;
    QSignalSpy header(&card, &an::ui::AnImageFactsCard::fitsHeaderRequested);
    QSignalSpy exif(&card, &an::ui::AnImageFactsCard::exifRequested);
    QSignalSpy summary(&card, &an::ui::AnImageFactsCard::summaryRequested);
    card.fitsHeaderButton()->click();
    card.exifButton()->click();
    card.summaryButton()->click();
    EXPECT_EQ(header.count(), 1);
    EXPECT_EQ(exif.count(), 1);
    EXPECT_EQ(summary.count(), 1);
    EXPECT_EQ(card.fitsHeaderButton()->toolTip(), QStringLiteral("FITS Header (Ctrl+Shift+H)"));
    EXPECT_EQ(card.exifButton()->toolTip(), QStringLiteral("EXIF Viewer (Ctrl+Shift+X)"));
    EXPECT_EQ(card.summaryButton()->toolTip(), QStringLiteral("Summary (Ctrl+Shift+M)"));
}

// ---- The column

// The column's header names the view, elided in the middle; the four cards follow in their order, a card gap apart.
TEST(ReadoutColumn, TheHeaderNamesTheViewAndTheCardsFollowInOrder)
{
    forEachLook([](const DesignTokens& /*t*/) {
        an::ui::AnReadoutColumn column;
        column.setWindowSize(QSize(1600, 1000));
        column.resize(column.columnWidth(), 960);
        showOffScreen(column);
        column.setViewName(QStringLiteral("stacked_3008x3004_Red_Caldwell_77"));
        EXPECT_EQ(column.viewName(), QStringLiteral("stacked_3008x3004_Red_Caldwell_77"));
        EXPECT_EQ(column.nameLabel()->elideMode(), Qt::ElideMiddle);
        int bottom = -1;
        for (const an::ui::ReadoutCardId id : {an::ui::ReadoutCardId::Histogram, an::ui::ReadoutCardId::Statistics,
                                               an::ui::ReadoutCardId::Astrometry, an::ui::ReadoutCardId::Image})
        {
            const QRect placed(column.card(id)->mapTo(&column, QPoint(0, 0)), column.card(id)->size());
            if (bottom >= 0)
            {
                EXPECT_EQ(placed.top() - bottom - 1, an::ui::parts::kReadoutCardGap)
                    << an::ui::AnReadoutColumn::cardName(id).toStdString();
            }
            bottom = placed.bottom();
        }
        EXPECT_GE(column.histogramCard()->height(),
                  column.statisticsCard()->header()->height() + an::ui::parts::kReadoutWellMinimum)
            << "the histogram takes the height the others leave";
    });
}

// The column is 360 px wide, 320 in a window under 1200 px wide.
TEST(ReadoutColumn, TheWidthFollowsTheWindow)
{
    an::ui::AnReadoutColumn column;
    column.setWindowSize(QSize(1536, 864));
    EXPECT_EQ(column.columnWidth(), 360);
    EXPECT_EQ(column.width(), 360);
    column.setWindowSize(QSize(1150, 864));
    EXPECT_EQ(column.columnWidth(), 320);
    EXPECT_EQ(column.width(), 320);
}

// What the person folds is told, and is a line of the folded cards' names; a remembered line handed back folds those
// cards and opens the others, ignoring a name it does not know, and tells nothing.
TEST(ReadoutColumn, TheFoldsAreALineOfNames)
{
    an::ui::AnReadoutColumn column;
    column.resize(360, 900);
    showOffScreen(column);
    QSignalSpy changed(&column, &an::ui::AnReadoutColumn::foldsChanged);
    clickAtLeft(*column.statisticsCard()->header());
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(column.foldedCards(), QStringLiteral("statistics"));
    column.setFoldedCards(QStringLiteral("image,astrometry,elsewhere"));
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(column.foldedCards(), QStringLiteral("astrometry,image"));
    EXPECT_FALSE(column.statisticsCard()->isFolded());
    column.setFoldedCards(QString());
    EXPECT_TRUE(column.foldedCards().isEmpty());
}

// Until a line is remembered, the Image card starts folded in a window under 820 px high; once one is, the window's
// height folds nothing.
TEST(ReadoutColumn, TheImageCardStartsFoldedInAShortWindow)
{
    an::ui::AnReadoutColumn column;
    column.setWindowSize(QSize(1536, 800));
    EXPECT_TRUE(column.imageCard()->isFolded());
    column.setWindowSize(QSize(1536, 864));
    EXPECT_FALSE(column.imageCard()->isFolded());

    an::ui::AnReadoutColumn remembered;
    remembered.setFoldedCards(QString());
    remembered.setWindowSize(QSize(1536, 800));
    EXPECT_FALSE(remembered.imageCard()->isFolded());
}

// The collapse button collapses the column to itself and is told; again, it opens it.
TEST(ReadoutColumn, CollapsingLeavesTheButtonAndIsTold)
{
    an::ui::AnReadoutColumn column;
    column.resize(360, 900);
    showOffScreen(column);
    QSignalSpy collapsed(&column, &an::ui::AnReadoutColumn::collapsedChanged);
    column.collapseButton()->click();
    EXPECT_TRUE(column.isCollapsed());
    EXPECT_FALSE(column.histogramCard()->isVisible());
    EXPECT_LT(column.width(), 100);
    EXPECT_EQ(column.collapseButton()->toolTip(), QStringLiteral("Open the readout column"));
    column.collapseButton()->click();
    EXPECT_FALSE(column.isCollapsed());
    EXPECT_TRUE(column.histogramCard()->isVisible());
    EXPECT_EQ(column.width(), 360);
    ASSERT_EQ(collapsed.count(), 2);
    EXPECT_TRUE(collapsed.at(0).at(0).toBool());
}

// The column is `surface1` with a hairline at its left.
TEST(ReadoutColumn, PaintsItsSurfaceAndAHairlineAtItsLeft)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnReadoutColumn column;
        column.resize(360, 900);
        showOffScreen(column);
        const QPoint statistics = column.statisticsCard()->mapTo(&column, QPoint(0, 0));
        const qreal between = statistics.y() - an::ui::parts::kReadoutCardGap / 2.0;
        forEachRatio(column, t.layers.surface0, [&](const RenderedPicture& picture) {
            expectColour(picture.at(0.5, between), an::ui::compositeOver(t.colors.borderSubtle, t.layers.surface1),
                         "the hairline");
            expectColour(picture.at(column.width() / 2.0, between), t.layers.surface1, "between two cards");
        });
    });
}

// ---- A task's compact readout

// Narrow, the five cells take a row of their own under the histogram; wide, they sit beside it and the readout is
// lower.
TEST(CompactReadout, TheCellsGoUnderTheHistogramOrBesideIt)
{
    an::ui::AnCompactReadout narrow;
    narrow.resize(360, narrow.sizeHint().height());
    showOffScreen(narrow);
    narrow.resize(360, narrow.sizeHint().height());
    EXPECT_FALSE(narrow.isWide());
    const QRect narrowWell(narrow.well()->mapTo(&narrow, QPoint(0, 0)), narrow.well()->size());
    const QRect narrowCells(narrow.cells()->mapTo(&narrow, QPoint(0, 0)), narrow.cells()->size());
    EXPECT_EQ(narrowWell.height(), an::ui::parts::kCompactReadoutWell);
    EXPECT_GT(narrowCells.top(), narrowWell.bottom());

    an::ui::AnCompactReadout wide;
    wide.resize(520, wide.sizeHint().height());
    showOffScreen(wide);
    wide.resize(520, wide.sizeHint().height());
    EXPECT_TRUE(wide.isWide());
    const QRect wideWell(wide.well()->mapTo(&wide, QPoint(0, 0)), wide.well()->size());
    const QRect wideCells(wide.cells()->mapTo(&wide, QPoint(0, 0)), wide.cells()->size());
    EXPECT_GT(wideCells.left(), wideWell.right());
    EXPECT_LT(wideCells.top(), wideWell.bottom());
    EXPECT_LT(wide.sizeHint().height(), narrow.sizeHint().height());
}

// Five cells - Min, Max, Mean, Median, StdDev - of the preview, each with its change against the original under it;
// with no original, no change is written.
TEST(CompactReadout, TheFiveCellsCarryTheirChangeAgainstTheOriginal)
{
    an::ui::AnCompactReadout readout;
    an::ui::ImageStatisticsReport original = reportOf({0.3});
    an::ui::ImageStatisticsReport preview = reportOf({0.4});
    preview.channels[0].standardDeviation = original.channels[0].standardDeviation;
    readout.setFigures(preview, original);
    const std::vector<an::ui::ReadoutCell>& cells = readout.cells()->cells();
    ASSERT_EQ(cells.size(), 5u);
    EXPECT_EQ(cellLabels(*readout.cells()),
              (QStringList{QStringLiteral("Min"), QStringLiteral("Max"), QStringLiteral("Mean"),
                           QStringLiteral("Median"), QStringLiteral("StdDev")}));
    EXPECT_EQ(cells[2].value, QStringLiteral("0.4000"));
    EXPECT_EQ(cells[2].note, QStringLiteral("Δ +0.1000"));
    EXPECT_EQ(cells[4].note, QStringLiteral("Δ ±0"));
    readout.setFigures(original, preview);
    EXPECT_EQ(readout.cells()->cells()[0].note, QStringLiteral("Δ −0.1000"));
    readout.setFigures(preview, std::nullopt);
    EXPECT_TRUE(readout.cells()->cells()[0].note.isEmpty());
}

// The mode says the channels and the resolution; the clipping is the counts' share at either end; the astrometry line
// shows the target's solution and nothing when it has none; the histogram is tagged "Preview"; the chevron asks for the
// full readout.
TEST(CompactReadout, TheModeTheClippingAndTheAstrometryLine)
{
    an::ui::AnCompactReadout readout;
    readout.setMode(3u, an::ui::HistogramResolution::Bits8);
    EXPECT_EQ(readout.modeText(), QStringLiteral("RGB · 8-bit"));
    readout.setMode(1u, an::ui::HistogramResolution::Bits12);
    EXPECT_EQ(readout.modeText(), QStringLiteral("Grayscale · 12-bit"));
    EXPECT_EQ(readout.histogramWidget()->getHistogramResolution(), an::ui::HistogramResolution::Bits12);

    readout.setCounts(countsOf(2000u, 47u, 4u));
    ASSERT_EQ(readout.clipping()->cells().size(), 2u);
    EXPECT_EQ(readout.clipping()->cells()[0].label, QStringLiteral("Clip low"));
    EXPECT_EQ(readout.clipping()->cells()[0].value, QStringLiteral("2.350 %"));
    EXPECT_EQ(readout.clipping()->cells()[1].value, QStringLiteral("0.200 %"));

    EXPECT_EQ(readout.well()->tag(), QStringLiteral("Preview"));
    readout.setAstrometry(QStringLiteral("0.520″/px · SIP 4/4"));
    EXPECT_GT(readout.astrometryLine()->sizeHint().width(), 0);
    EXPECT_EQ(accessibleName(*readout.astrometryLine()), QStringLiteral("Solved, 0.520″/px · SIP 4/4"));
    readout.setAstrometry(QString());
    EXPECT_EQ(readout.astrometryLine()->sizeHint().width(), 0);

    QSignalSpy asked(&readout, &an::ui::AnCompactReadout::fullReadoutRequested);
    readout.expandButton()->click();
    EXPECT_EQ(asked.count(), 1);
}

// The compact readout is `surface2`; its stale values are drawn at 55 %.
TEST(CompactReadout, PaintsItsSurfaceAndItsStaleValues)
{
    forEachLook([](const DesignTokens& t) {
        an::ui::AnCompactReadout readout;
        readout.setFigures(reportOf({0.4}), reportOf({0.3}));
        readout.resize(360, readout.sizeHint().height());
        showOffScreen(readout);
        forEachRatio(readout, t.layers.surface1, [&](const RenderedPicture& picture) {
            expectColour(picture.at(1.5, readout.height() - 1.5), t.layers.surface2, "the foot's corner");
        });
        // A narrow cell elides its value under the headless font: the mean is read by its start.
        readout.setStale(true);
        EXPECT_NEAR(recordedText(*readout.cells(), QStringLiteral("0.4")).opacity, an::ui::parts::kReadoutStaleOpacity,
                    1e-6);
        readout.setStale(false);
        EXPECT_NEAR(recordedText(*readout.cells(), QStringLiteral("0.4")).opacity, 1.0, 1e-6);
    });
}

// ---- Night

// Under Night the readout - the column with every card filled, a colour image's counts and a solved image's chips, and
// a task's compact readout - paints no pixel with green or blue.
TEST(ReadoutColumn, PaintsNoGreenOrBlueAtNight)
{
    const auto firstHue = [](const RenderedPicture& picture) -> std::string {
        const QImage& image = picture.image();
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.green() != 0 || pixel.blue() != 0)
                {
                    return "(" + std::to_string(x) + ", " + std::to_string(y) + ") is " +
                           an::ui::testing::colourName(pixel);
                }
            }
        }
        return {};
    };
    auto counts = std::make_shared<an::ui::HistogramDisplayData>();
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        an::ui::HistogramDisplayChannel shown;
        shown.bins.assign(256u, 2u);
        shown.bins[60u + 40u * channel] = 90u;
        shown.samples = 600u;
        counts->channels.push_back(shown);
    }
    an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[0]);
    for (const std::size_t look : {std::size_t{0}, std::size_t{5}})
    {
        SCOPED_TRACE(an::ui::testing::observatoryLooks()[look].name);
        scope.setLook(an::ui::testing::observatoryLooks()[look]);
        scope.setNight(true);
        const DesignTokens& t = an::ui::tokens();

        an::ui::AnReadoutColumn column;
        column.setViewName(QStringLiteral("M20_FINAL_PNG_05"));
        column.histogramCard()->setChannelCount(3u);
        column.histogramCard()->setCounts(counts);
        column.statisticsCard()->setReport(reportOf({0.2, 0.4, 0.6}));
        column.astrometryCard()->setReadout(solvedReadout());
        QAction grid;
        grid.setCheckable(true);
        grid.setChecked(true);
        column.astrometryCard()->bindOverlay(an::ui::AstrometryOverlay::Grid, &grid);
        column.imageCard()->setFacts(an::ui::ImageFactsReadout{.dimensions = QStringLiteral("4432 × 3840"),
                                                               .format = QStringLiteral("16 bit unsigned integer"),
                                                               .sizeInMemory = QStringLiteral("97.38 MB"),
                                                               .channels = QStringLiteral("RGB")});
        column.resize(360, 1000);
        showOffScreen(column);

        an::ui::AnCompactReadout compact;
        compact.setCounts(counts);
        compact.setMode(3u, an::ui::HistogramResolution::Bits8);
        compact.setFigures(reportOf({0.4}), reportOf({0.3}));
        compact.setAstrometry(QStringLiteral("0.520″/px · SIP 4/4"));
        compact.resize(360, compact.sizeHint().height());
        showOffScreen(compact);

        for (QWidget* painted : {static_cast<QWidget*>(&column), static_cast<QWidget*>(&compact)})
        {
            SCOPED_TRACE(painted->metaObject()->className());
            for (const qreal ratio : {1.0, 2.0})
            {
                const RenderedPicture picture = RenderedPicture::ofWidget(*painted, ratio, t.layers.surface1);
                EXPECT_EQ(firstHue(picture), std::string()) << "at a device pixel ratio of " << ratio;
            }
        }
        scope.setNight(false);
    }
}

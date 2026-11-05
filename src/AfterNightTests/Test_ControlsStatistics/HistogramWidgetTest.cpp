// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The histogram widget draws the counts its owner hands it and counts nothing itself: the counts it draws, the
/// resolution it names, counts handed over from a worker shown only on the GUI thread, the clipping readout as a share
/// of the samples the image's validity admitted, the shared scale over the displayed channels only, and a dense
/// histogram prepared for the width the widget - or a widget derived from it - was last resized to.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramGHTWidget.h"
#include "widgets/HistogramMTFWidget.h"
#include "widgets/HistogramOverlay.h"
#include "widgets/HistogramWidget.h"

#include <QCoreApplication>
#include <QImage>
#include <QResizeEvent>
#include <QScrollArea>

#include <gtest/gtest.h>

#include <array>
#include <future>
#include <memory>
#include <vector>

namespace
{

using an::tests::statistics::pumpUntil;

/// One channel per entry, each of `bins` bins, every bin `fill` and the bin `peakBin` holding `peak`.
[[nodiscard]] std::shared_ptr<const an::ui::HistogramDisplayData> countsOf(const std::vector<std::uint64_t>& peaks,
                                                                           const std::size_t bins = 256u,
                                                                           const std::size_t peakBin = 100u,
                                                                           const std::uint64_t fill = 0u)
{
    auto data = std::make_shared<an::ui::HistogramDisplayData>();
    for (const std::uint64_t peak : peaks)
    {
        an::ui::HistogramDisplayChannel channel;
        channel.bins.assign(bins, fill);
        channel.bins[peakBin] = peak;
        channel.samples = 1000u;
        data->channels.push_back(std::move(channel));
    }
    return data;
}

/// Reaches what a histogram widget prepared for drawing a histogram denser than its width.
template <typename WidgetT> class Peeking : public WidgetT
{
public:
    using WidgetT::WidgetT;

    [[nodiscard]] std::size_t preparedColumns() const
    {
        return this->m_HistogramsToRender.empty() ? 0u : this->m_HistogramsToRender.front().histogram_data->size();
    }
};

/// Resizes a widget the way a layout does: the geometry changes and the widget is told.
void resizeTo(QWidget& widget, const QSize& size)
{
    const QSize old = widget.size();
    widget.resize(size);
    QResizeEvent event(size, old);
    QCoreApplication::sendEvent(&widget, &event);
}

[[nodiscard]] HistogramOverlay* clippingOverlayOf(QWidget& widget)
{
    for (QObject* child : widget.children())
    {
        if (auto* overlay = dynamic_cast<HistogramOverlay*>(child))
        {
            return overlay;
        }
    }
    return nullptr;
}

} // namespace

TEST(HistogramWidgetTest, ANewWidgetDrawsNothing)
{
    HistogramWidget widget(nullptr);
    ASSERT_NE(widget.getHistogram(), nullptr);
    EXPECT_FALSE(widget.getHistogram()->isValid());
    EXPECT_EQ(widget.getHistogramResolution(), an::ui::HistogramResolution::Bits8);
    widget.resize(256, 100);
    EXPECT_FALSE(widget.grab().toImage().isNull());

    widget.setHistogram(nullptr);
    ASSERT_NE(widget.getHistogram(), nullptr);
    EXPECT_FALSE(widget.getHistogram()->isValid());
}

// A peak is drawn at the top of the plot over its bin, and nothing else reaches the top.
TEST(HistogramWidgetTest, TheWidgetDrawsTheCountsItIsGiven)
{
    HistogramWidget widget(nullptr);
    widget.setGridEnabled(false);
    widget.setGradientAreaEnabled(false);
    widget.setClippingInfoEnabled(false);
    resizeTo(widget, QSize(256, 100));
    widget.setHistogram(countsOf({100u}));

    const QImage image = widget.grab().toImage();
    ASSERT_FALSE(image.isNull());
    const auto litAtTop = [&image](const int x) {
        for (int y = 0; y <= 2; ++y)
        {
            if (image.pixelColor(x, y).lightness() > 150)
            {
                return true;
            }
        }
        return false;
    };
    bool peakDrawn = false;
    for (int x = 98; x <= 102; ++x)
    {
        peakDrawn = peakDrawn || litAtTop(x);
    }
    EXPECT_TRUE(peakDrawn);
    EXPECT_FALSE(litAtTop(30));
    EXPECT_FALSE(litAtTop(200));
}

// The resolution is what the owner counts at; naming one counts nothing and leaves the counts drawn as they were.
TEST(HistogramWidgetTest, NamingAResolutionCountsNothing)
{
    HistogramWidget widget(nullptr);
    const std::shared_ptr<const an::ui::HistogramDisplayData> counts = countsOf({10u});
    widget.setHistogram(counts);
    widget.setHistogramResolution(an::ui::HistogramResolution::Bits12);
    EXPECT_EQ(widget.getHistogramResolution(), an::ui::HistogramResolution::Bits12);
    EXPECT_EQ(widget.getHistogram(), counts);
}

// Counts handed over from a worker are drawn only once the GUI thread takes them, and the worker never waits for it.
TEST(HistogramWidgetTest, CountsFromAWorkerAreTakenOnTheGuiThreadWithoutWaiting)
{
    HistogramWidget widget(nullptr);
    widget.resize(640, 320);
    const std::shared_ptr<const an::ui::HistogramDisplayData> before = widget.getHistogram();
    const std::shared_ptr<const an::ui::HistogramDisplayData> counts = countsOf({10u, 20u, 30u});

    std::async(std::launch::async, [&widget, counts]() { widget.setHistogram(counts); }).get();

    EXPECT_EQ(widget.getHistogram(), before) << "a worker changed what the paint reads";
    ASSERT_TRUE(pumpUntil([&widget, &counts]() { return widget.getHistogram() == counts; }));
}

// The clipping readout is the clipped share of the samples the image's validity admitted, over every channel.
TEST(HistogramWidgetTest, TheClippingReadoutIsAShareOfTheAdmittedSamples)
{
    HistogramWidget widget(nullptr);
    widget.setClippingInfoEnabled(true);
    auto data = std::make_shared<an::ui::HistogramDisplayData>();
    for (int channel = 0; channel < 2; ++channel)
    {
        an::ui::HistogramDisplayChannel counts;
        counts.bins.assign(64u, 1u);
        counts.samples = 200u;
        counts.clippedLow = 4u;
        counts.clippedHigh = channel == 0 ? 10u : 0u;
        data->channels.push_back(std::move(counts));
    }
    widget.setHistogram(data);

    HistogramOverlay* overlay = clippingOverlayOf(widget);
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->text(), QStringLiteral("Clipping Low: 2.000%\nClipping High: 2.500%"));
}

// The clipping readout is laid out again only when its text changes: a new count that clips the same shares, as most
// counts of a drag do, leaves it as it is.
TEST(HistogramWidgetTest, TheClippingReadoutIsLaidOutOnlyWhenItsTextChanges)
{
    HistogramWidget widget(nullptr);
    widget.setClippingInfoEnabled(true);
    const auto clipping = [](std::uint64_t low) {
        auto data = std::make_shared<an::ui::HistogramDisplayData>();
        an::ui::HistogramDisplayChannel counts;
        counts.bins.assign(64u, 1u);
        counts.samples = 200u;
        counts.clippedLow = low;
        data->channels.push_back(std::move(counts));
        return data;
    };
    widget.setHistogram(clipping(4u));
    HistogramOverlay* overlay = clippingOverlayOf(widget);
    ASSERT_NE(overlay, nullptr);
    const QString first = overlay->text();
    overlay->resize(1, 1);

    widget.setHistogram(clipping(4u));
    EXPECT_EQ(overlay->text(), first);
    EXPECT_EQ(overlay->size(), QSize(1, 1)) << "the same text was laid out again";

    widget.setHistogram(clipping(6u));
    EXPECT_NE(overlay->text(), first);
    EXPECT_EQ(overlay->size(), overlay->sizeHint()) << "a changed text is laid out for its size";
}

// The shared scale is the largest bin of the displayed channels alone.
TEST(HistogramWidgetTest, TheSharedScaleUsesOnlyTheDisplayedChannels)
{
    const std::shared_ptr<const an::ui::HistogramDisplayData> counts = countsOf({10u, 100u, 25u}, 64u, 32u);
    EXPECT_DOUBLE_EQ(an::ui::detail::getMaxDisplayedHistogramValue(counts, {true, false, false, false}), 10.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::getMaxDisplayedHistogramValue(counts, {true, true, true, false}), 100.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::getMaxDisplayedHistogramValue(nullptr, {true, true, true, false}), 0.0);

    HistogramWidget widget(nullptr);
    widget.setHistogram(counts);
    widget.setDisplayChannels(false, false, true);
    EXPECT_DOUBLE_EQ(widget.getMaxDisplayedHistogramValue(), 25.0);
}

// A histogram denser than the widget is folded into one column per pixel of the width the widget was last resized to -
// the plain widget, and the two transfer-curve widgets derived from it, which resize their range selector as well.
TEST(HistogramWidgetTest, ADenseHistogramIsPreparedForTheWidthAfterAResize)
{
    QScrollArea area;
    Peeking<HistogramWidget> plain(nullptr);
    Peeking<HistogramMTFWidget> midtones(&area);
    Peeking<HistogramGHTWidget> generalized(&area);
    const auto prepared = [](auto& widget) {
        resizeTo(widget, QSize(300, 120));
        widget.setHistogram(countsOf({50u}, 4096u, 2000u, 1u));
        return widget.preparedColumns();
    };
    EXPECT_EQ(prepared(plain), 300u);
    EXPECT_EQ(prepared(midtones), 300u);
    EXPECT_EQ(prepared(generalized), 300u);
}

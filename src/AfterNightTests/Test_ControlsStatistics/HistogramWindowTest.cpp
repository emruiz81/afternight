// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The histogram window over a view: the view's image counted on a worker at the resolution chosen and shown when it
/// lands, counted again when the view shows another image or another resolution is chosen, and a channel selector that
/// follows the image's channels. A window closed while its count waits is `Test_ControlsSeams`', whose quarantine needs
/// the window compiled into it.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "controls/ImageHistogramControl.h"
#include "widgets/HistogramWidget.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace
{

using an::tests::statistics::Channels;
using an::tests::statistics::InstalledContext;
using an::tests::statistics::makeDocument;
using an::tests::statistics::makeView;
using an::tests::statistics::openStatisticsContext;
using an::tests::statistics::pumpQuietly;
using an::tests::statistics::pumpUntil;
using an::tests::statistics::settle;
using an::tests::statistics::StatisticsTestContext;
using an::tests::statistics::ViewHandle;

/// The cases' context and a view, installed for the case.
class HistogramWindowTest : public ::testing::Test
{
protected:
    void open(const std::uint32_t width)
    {
        m_context = openStatisticsContext(width);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A view showing a document of `channels` whose every sample of channel c is `levels[c]`.
    [[nodiscard]] ViewHandle viewOf(const Channels channels, const std::array<float, 4> levels)
    {
        ViewHandle view = makeView("histogram");
        std::optional<an::ui::ImageDocument> document =
            makeDocument<float>(session(), 16u, 8u, channels,
                                [levels](std::uint32_t, std::uint32_t, std::uint32_t c) { return levels[c]; });
        EXPECT_TRUE(document.has_value());
        if (document.has_value())
        {
            view->setDocument(std::move(*document));
        }
        return view;
    }

    std::unique_ptr<StatisticsTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

[[nodiscard]] QStringList itemsOf(const QComboBox& box)
{
    QStringList items;
    for (int index = 0; index < box.count(); ++index)
    {
        items.push_back(box.itemText(index));
    }
    return items;
}

/// The bin each channel's samples landed in.
[[nodiscard]] std::vector<std::size_t> filledBins(const HistogramWidget& widget)
{
    std::vector<std::size_t> filled;
    const std::shared_ptr<const an::ui::HistogramDisplayData> counts = widget.getHistogram();
    for (const an::ui::HistogramDisplayChannel& channel : counts->channels)
    {
        const auto found = std::find_if(channel.bins.begin(), channel.bins.end(), [](auto bin) { return bin != 0u; });
        filled.push_back(static_cast<std::size_t>(found - channel.bins.begin()));
    }
    return filled;
}

} // namespace

TEST_F(HistogramWindowTest, TheWindowShowsTheViewsImageCountedOnAWorker)
{
    open(2u);
    ViewHandle view = viewOf(Channels::Rgb, {0.25f, 0.5f, 0.75f, 0.0f});
    auto* window = new ImageHistogramControl(view.get());
    QPointer<ImageHistogramControl> guard(window);
    window->show();
    EXPECT_EQ(window->windowTitle(), QStringLiteral("Histogram <histogram>"));

    ASSERT_TRUE(pumpUntil([window]() { return window->scheduler().isIdle(); }));
    HistogramWidget* widget = window->histogramWidget();
    ASSERT_NE(widget, nullptr);
    ASSERT_EQ(widget->getHistogram()->channels.size(), 3u);
    EXPECT_EQ(filledBins(*widget), (std::vector<std::size_t>{63u, 127u, 191u}));

    auto* channels = window->findChild<QComboBox*>(QStringLiteral("channelSelector"));
    ASSERT_NE(channels, nullptr);
    EXPECT_EQ(itemsOf(*channels), (QStringList{"RGB", "Red", "Green", "Blue"}));
    EXPECT_TRUE(channels->isEnabled());
    channels->setCurrentText(QStringLiteral("Green"));
    EXPECT_DOUBLE_EQ(widget->getMaxDisplayedHistogramValue(), 128.0);

    window->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(guard.isNull());
    ASSERT_TRUE(settle(*view));
}

// Another resolution is counted again at that resolution, and another image the view shows is counted again.
TEST_F(HistogramWindowTest, AnotherResolutionOrAnotherImageIsCountedAgain)
{
    open(2u);
    ViewHandle view = viewOf(Channels::Grey, {0.5f, 0.0f, 0.0f, 0.0f});
    auto* window = new ImageHistogramControl(view.get());
    ASSERT_TRUE(pumpUntil([window]() { return window->scheduler().isIdle(); }));
    EXPECT_EQ(filledBins(*window->histogramWidget()), (std::vector<std::size_t>{127u}));

    auto* resolution = window->findChild<QComboBox*>(QStringLiteral("plotResolution"));
    ASSERT_NE(resolution, nullptr);
    resolution->setCurrentText(QStringLiteral("1000 levels"));
    ASSERT_TRUE(pumpUntil([window]() { return window->scheduler().isIdle(); }));
    EXPECT_EQ(window->histogramWidget()->getHistogram()->channels.front().bins.size(), 1000u);
    EXPECT_EQ(filledBins(*window->histogramWidget()), (std::vector<std::size_t>{499u}));

    std::optional<an::ui::ImageDocument> brighter = makeDocument<float>(
        session(), 16u, 8u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.9f; });
    ASSERT_TRUE(brighter.has_value());
    view->setDocument(std::move(*brighter));
    ASSERT_TRUE(pumpUntil([window]() { return window->scheduler().isIdle(); }));
    EXPECT_EQ(filledBins(*window->histogramWidget()), (std::vector<std::size_t>{899u}));
    ASSERT_TRUE(settle(*view));
}

// A grey image offers its one channel; a colour image with alpha offers its three colours, as one without does.
TEST_F(HistogramWindowTest, TheChannelSelectorFollowsTheImagesChannels)
{
    open(2u);
    ViewHandle grey = viewOf(Channels::Grey, {0.5f, 0.0f, 0.0f, 0.0f});
    auto* greyWindow = new ImageHistogramControl(grey.get());
    auto* greyChannels = greyWindow->findChild<QComboBox*>(QStringLiteral("channelSelector"));
    ASSERT_NE(greyChannels, nullptr);
    EXPECT_EQ(itemsOf(*greyChannels), (QStringList{"Grayscale"}));
    EXPECT_FALSE(greyChannels->isEnabled());

    ViewHandle withAlpha = viewOf(Channels::Rgba, {0.2f, 0.4f, 0.6f, 1.0f});
    auto* alphaWindow = new ImageHistogramControl(withAlpha.get());
    auto* alphaChannels = alphaWindow->findChild<QComboBox*>(QStringLiteral("channelSelector"));
    ASSERT_NE(alphaChannels, nullptr);
    EXPECT_EQ(itemsOf(*alphaChannels), (QStringList{"RGB", "Red", "Green", "Blue"}));
    ASSERT_TRUE(pumpUntil(
        [greyWindow, alphaWindow]() { return greyWindow->scheduler().isIdle() && alphaWindow->scheduler().isIdle(); }));
    ASSERT_TRUE(settle(*grey));
    ASSERT_TRUE(settle(*withAlpha));
}

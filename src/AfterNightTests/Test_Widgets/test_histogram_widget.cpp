// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QScrollArea>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramOverlay.h"
#include "widgets/HistogramWidget.h"

TEST(WidgetsHistogram, DefaultStateMatchesExpectedFlags)
{
    HistogramWidget widget(nullptr);

    EXPECT_TRUE(widget.getGridEnabled());
    EXPECT_FALSE(widget.getLocalChannelNormalization());
    EXPECT_FALSE(widget.getClippingInfoEnabled());
    EXPECT_TRUE(widget.getGradientAreaEnabled());
    EXPECT_FALSE(widget.getGuideLineEnabled());
    EXPECT_FALSE(widget.getInteractiveZoomEnabled());
}

TEST(WidgetsHistogram, ToggleFlagsRoundTrip)
{
    HistogramWidget widget(nullptr);

    widget.setGridEnabled(false);
    widget.setLocalChannelNormalization(true);
    widget.setClippingInfoEnabled(true);
    widget.setGradientAreaEnabled(false);
    widget.setGuideLineEnabled(true);
    widget.setInteractiveZoomEnabled(true);

    EXPECT_FALSE(widget.getGridEnabled());
    EXPECT_TRUE(widget.getLocalChannelNormalization());
    EXPECT_TRUE(widget.getClippingInfoEnabled());
    EXPECT_FALSE(widget.getGradientAreaEnabled());
    EXPECT_TRUE(widget.getGuideLineEnabled());
    EXPECT_TRUE(widget.getInteractiveZoomEnabled());
}

TEST(WidgetsHistogram, AreaOpacityIsClampedToUnitRange)
{
    HistogramWidget widget(nullptr);

    widget.setAreaOpacity(-1.0f);
    EXPECT_FLOAT_EQ(widget.getAreaOpacity(), 0.0f);

    widget.setAreaOpacity(2.0f);
    EXPECT_FLOAT_EQ(widget.getAreaOpacity(), 1.0f);
}

TEST(WidgetsHistogram, ParentScrollAreaCanBeAssigned)
{
    HistogramWidget widget(nullptr);
    QScrollArea area;

    widget.setParentScrollArea(&area);
    EXPECT_EQ(widget.getParentScrollArea(), &area);
}

TEST(WidgetsHistogram, ClippingOverlayRemainsAttachedToHistogramWidget)
{
    HistogramWidget widget(nullptr);
    QScrollArea area;

    HistogramOverlay* overlay = nullptr;
    for (QObject* child : widget.children())
    {
        overlay = dynamic_cast<HistogramOverlay*>(child);
        if (overlay)
        {
            break;
        }
    }

    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->parentWidget(), &widget);

    widget.setParentScrollArea(&area);

    EXPECT_EQ(widget.getParentScrollArea(), &area);
    EXPECT_EQ(overlay->parentWidget(), &widget);
}

TEST(WidgetsHistogram, WorkerRenderingDoesNotWaitForGuiEventLoop)
{
    HistogramWidget widget(nullptr);
    widget.resize(640, 320);
    auto counts = std::make_shared<an::ui::HistogramDisplayData>();
    counts->channels.resize(1);
    counts->channels.front().bins.assign(256, 1u);
    const std::shared_ptr<const an::ui::HistogramDisplayData> histogram = counts;

    auto render_frames = std::async(std::launch::async, [&widget, histogram]() {
        for (int frame = 0; frame < 64; ++frame)
        {
            widget.setHistogram(histogram);
        }
    });

    const auto completion = render_frames.wait_for(std::chrono::seconds(1));
    if (completion != std::future_status::ready)
    {
        // Release every blocking queued connection the worker waits on before
        // reporting the regression, keeping the failing test process deterministic.
        const auto cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (render_frames.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
               std::chrono::steady_clock::now() < cleanup_deadline)
        {
            QCoreApplication::processEvents();
            std::this_thread::yield();
        }
    }

    EXPECT_EQ(completion, std::future_status::ready)
        << "Histogram preparation must use cached geometry; an RT worker must never wait for the GUI event loop";
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The comparison view the blink comparator shows its frames in: its modes, its synchronized zoom and pan, the
/// demosaic of a mosaic it is handed, and the difference it shows over the left image. The images are assets of an
/// engine session the installed application context lends.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/ImageViewControl.h"
#include "widgets/ComparisonViewWidget.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QApplication>
#include <QCoreApplication>
#include <QScrollBar>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace
{

using namespace an::tests::viewer;

/// The cases' application context: one session, installed for the case and cleared when it ends.
class ComparisonViewWidgetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openViewerContext();
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

    /// A grey Float32 image whose sample at (x, y) is (x + y) mod 256 as a fraction of 255.
    [[nodiscard]] std::optional<an::consumer::ImageAsset> rampImage(std::uint32_t width, std::uint32_t height,
                                                                    an::CfaDescriptor cfa = an::CfaDescriptor{}) const
    {
        return makeAsset<float>(
            session(), width, height, Channels::Grey,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>((x + y) % 256u) / 255.0f; },
            an::NumericDomain::NormalizedLinear, cfa);
    }

    /// A grey Float32 image of one row holding two samples.
    [[nodiscard]] std::optional<an::consumer::ImageAsset> twoSampleImage(float left, float right) const
    {
        return makeAsset<float>(
            session(), 2u, 1u, Channels::Grey,
            [left, right](std::uint32_t x, std::uint32_t, std::uint32_t) { return x == 0u ? left : right; });
    }

    /// Turns the event loop until the comparison and both its views are idle.
    [[nodiscard]] static bool settleComparison(const an::ui::ComparisonViewWidget& widget)
    {
        const auto idle = [&widget]() {
            return widget.isComparisonIdle() && widget.leftControl()->isWorkIdle() &&
                   widget.rightControl()->isWorkIdle();
        };
        if (!pumpUntil(idle))
        {
            return false;
        }
        pumpQuietly();
        return idle();
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST_F(ComparisonViewWidgetTest, ModeTransitionsShowAndHidePanels)
{
    an::ui::ComparisonViewWidget widget;
    widget.show();
    QApplication::processEvents();

    EXPECT_EQ(widget.mode(), an::ui::ComparisonViewMode::Single);
    EXPECT_FALSE(widget.leftControl()->isHidden());
    EXPECT_TRUE(widget.rightControl()->isHidden());

    widget.setMode(an::ui::ComparisonViewMode::SideBySide);
    QApplication::processEvents();
    EXPECT_FALSE(widget.rightControl()->isHidden());

    widget.setMode(an::ui::ComparisonViewMode::Overlay);
    QApplication::processEvents();
    EXPECT_TRUE(widget.rightControl()->isHidden());

    widget.setMode(an::ui::ComparisonViewMode::Difference);
    QApplication::processEvents();
    EXPECT_TRUE(widget.rightControl()->isHidden());
}

TEST_F(ComparisonViewWidgetTest, PreviewControlsAutoDebayerCfaImages)
{
    an::ui::ComparisonViewWidget widget;

    ASSERT_NE(widget.leftControl(), nullptr);
    ASSERT_NE(widget.rightControl(), nullptr);
    EXPECT_TRUE(widget.leftControl()->isAutoDebayerCfaImagePreviewsEnabled());
    EXPECT_TRUE(widget.rightControl()->isAutoDebayerCfaImagePreviewsEnabled());
}

TEST_F(ComparisonViewWidgetTest, SetImagesDebayersCfaPreviewInputs)
{
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    std::optional<an::consumer::ImageAsset> left = rampImage(8u, 8u, *rggb);
    std::optional<an::consumer::ImageAsset> right = rampImage(8u, 8u, *rggb);
    ASSERT_TRUE(left.has_value() && right.has_value());

    an::ui::ComparisonViewWidget widget;
    widget.setLeftImage(std::move(*left));
    widget.setRightImage(std::move(*right));
    ASSERT_TRUE(settleComparison(widget));

    const std::optional<an::ImageDescriptor> shownLeft = widget.leftControl()->imageDescriptor();
    const std::optional<an::ImageDescriptor> shownRight = widget.rightControl()->imageDescriptor();
    ASSERT_TRUE(shownLeft.has_value());
    ASSERT_TRUE(shownRight.has_value());
    EXPECT_FALSE(shownLeft->isMosaiced());
    EXPECT_FALSE(shownRight->isMosaiced());
    EXPECT_EQ(shownLeft->extent().channelCount(), 3u);
    EXPECT_EQ(shownRight->extent().channelCount(), 3u);
}

TEST_F(ComparisonViewWidgetTest, ZoomAndPanChangesStaySynchronized)
{
    an::ui::ComparisonViewWidget widget;
    widget.setMode(an::ui::ComparisonViewMode::SideBySide);
    widget.show();
    QApplication::processEvents();

    auto* leftView = widget.leftControl()->getViewWidget();
    ASSERT_NE(leftView, nullptr);

    leftView->scaleImageView(2.0, true);
    QApplication::processEvents();
    EXPECT_FLOAT_EQ(widget.rightControl()->getCurrentZoom(), 200.0f);
}

TEST_F(ComparisonViewWidgetTest, SynchronizedZoomScalesDifferentResolutionImagesToSameFieldSize)
{
    an::ui::ComparisonViewWidget widget;
    widget.setMode(an::ui::ComparisonViewMode::SideBySide);
    widget.resize(800, 400);
    widget.show();
    QApplication::processEvents();

    std::optional<an::consumer::ImageAsset> left = rampImage(1000u, 800u);
    std::optional<an::consumer::ImageAsset> right = rampImage(2000u, 1600u);
    ASSERT_TRUE(left.has_value() && right.has_value());
    widget.setLeftImage(std::move(*left));
    widget.setRightImage(std::move(*right));
    ASSERT_TRUE(settleComparison(widget));

    auto* leftView = widget.leftControl()->getViewWidget();
    auto* rightView = widget.rightControl()->getViewWidget();
    ASSERT_NE(leftView, nullptr);
    ASSERT_NE(rightView, nullptr);

    leftView->scaleImageView(2.0, true);
    QApplication::processEvents();
    EXPECT_FLOAT_EQ(widget.leftControl()->getCurrentZoom(), 200.0f);
    EXPECT_FLOAT_EQ(widget.rightControl()->getCurrentZoom(), 100.0f);

    rightView->scaleImageView(1.5, true);
    QApplication::processEvents();
    EXPECT_FLOAT_EQ(widget.rightControl()->getCurrentZoom(), 150.0f);
    EXPECT_FLOAT_EQ(widget.leftControl()->getCurrentZoom(), 300.0f);
}

// The difference is shown as the left view's alternate image, so the left image itself is never replaced.
TEST_F(ComparisonViewWidgetTest, DifferenceModeEventuallyDisplaysGeneratedDifferenceImage)
{
    an::ui::ComparisonViewWidget widget;
    widget.setMode(an::ui::ComparisonViewMode::Difference);

    std::optional<an::consumer::ImageAsset> left = twoSampleImage(0.2f, 0.8f);
    std::optional<an::consumer::ImageAsset> right = twoSampleImage(0.5f, 0.1f);
    ASSERT_TRUE(left.has_value() && right.has_value());
    const std::uint64_t leftIdentity = left->identity();

    widget.setLeftImage(std::move(*left));
    widget.setRightImage(std::move(*right));

    ASSERT_TRUE(an::tests::viewer::pumpUntil([&widget]() {
        return widget.isComparisonIdle() && widget.leftControl()->hasAlternateImage() &&
               widget.leftControl()->alternateAsset() != nullptr;
    }));
    const std::shared_ptr<const an::consumer::ImageAsset> difference = widget.leftControl()->alternateAsset();
    ASSERT_NE(difference, nullptr);
    const std::optional<float> first = an::tests::viewer::sampleOf<float>(*difference, 0u, 0u);
    const std::optional<float> second = an::tests::viewer::sampleOf<float>(*difference, 1u, 0u);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NEAR(*first, 0.3f, 1.0e-6f);
    EXPECT_NEAR(*second, 0.7f, 1.0e-6f);
    ASSERT_NE(widget.leftControl()->document().asset(), nullptr);
    EXPECT_EQ(widget.leftControl()->document().asset()->identity(), leftIdentity);
}

TEST_F(ComparisonViewWidgetTest, OverlayOpacityRoundTripsThroughPublicApi)
{
    an::ui::ComparisonViewWidget widget;
    widget.setOverlayOpacity(0.35);
    EXPECT_DOUBLE_EQ(widget.overlayOpacity(), 0.35);
}

TEST_F(ComparisonViewWidgetTest, ScrollBarValueChangedPropagatesPanToOtherControl)
{
    an::ui::ComparisonViewWidget widget;
    widget.setMode(an::ui::ComparisonViewMode::SideBySide);
    widget.show();
    widget.resize(600, 400);
    QApplication::processEvents();

    auto* leftScrollArea = widget.leftControl()->getScrollArea();
    auto* rightScrollArea = widget.rightControl()->getScrollArea();
    ASSERT_NE(leftScrollArea, nullptr);
    ASSERT_NE(rightScrollArea, nullptr);

    auto* hBar = leftScrollArea->horizontalScrollBar();
    auto* vBar = leftScrollArea->verticalScrollBar();
    ASSERT_NE(hBar, nullptr);
    ASSERT_NE(vBar, nullptr);

    // Force a scroll range by temporarily expanding it so we can verify the sync
    // path fires regardless of whether images are loaded.
    hBar->setRange(0, 500);
    vBar->setRange(0, 500);
    rightScrollArea->horizontalScrollBar()->setRange(0, 500);
    rightScrollArea->verticalScrollBar()->setRange(0, 500);

    hBar->setValue(120);
    vBar->setValue(80);

    // Check sync immediately – processEvents() would trigger layout that resets
    // scroll bar ranges to [0,0] because no images are loaded in the controls.
    EXPECT_EQ(widget.rightControl()->getViewportOffset().x(), 120);
    EXPECT_EQ(widget.rightControl()->getViewportOffset().y(), 80);
}

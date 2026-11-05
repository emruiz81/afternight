// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "controls/ImageHistoryTrackerUtils.h"
#include "widgets/QuickZoomController.h"
#include "widgets/ViewPresentation.h"

namespace
{

/// The lens's readout of one pixel of a uniform image of a channel count, read as the lens reads it.
[[nodiscard]] std::optional<an::ui::PixelReadout> readUniform(an::tests::viewer::Channels channels, float red,
                                                              float green, float blue, std::int64_t x, std::int64_t y)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = an::tests::viewer::openProvider(1u);
    if (!provider.has_value())
    {
        return std::nullopt;
    }
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        provider->engineSession(), 8u, 8u, channels, [red, green, blue](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return c == 0u ? red : (c == 1u ? green : blue);
        });
    if (!asset.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    return lease.hasValue() ? an::ui::readPixel(*lease, x, y) : std::nullopt;
}

} // namespace

TEST(ImageViewControlImageLoading, TheLensReadsEveryColourChannelOfAPixel)
{
    const std::optional<an::ui::PixelReadout> sampled =
        readUniform(an::tests::viewer::Channels::Rgb, 0.2f, 0.5f, 0.8f, 4, 5);
    ASSERT_TRUE(sampled.has_value());
    EXPECT_EQ(sampled->channels, 3u);
    EXPECT_NEAR(sampled->values[0], 0.2, 1e-6);
    EXPECT_NEAR(sampled->values[1], 0.5, 1e-6);
    EXPECT_NEAR(sampled->values[2], 0.8, 1e-6);
}

TEST(ImageViewControlImageLoading, TheLensClampsAPositionOutsideTheImage)
{
    const std::optional<an::ui::PixelReadout> sampled =
        readUniform(an::tests::viewer::Channels::Rgb, 0.7f, 0.3f, 0.1f, 1000, 1000);
    ASSERT_TRUE(sampled.has_value());
    EXPECT_EQ(sampled->channels, 3u);
    EXPECT_NEAR(sampled->values[0], 0.7, 1e-6);
    EXPECT_NEAR(sampled->values[1], 0.3, 1e-6);
    EXPECT_NEAR(sampled->values[2], 0.1, 1e-6);
}

TEST(ImageViewControlImageLoading, TheLensReadsNothingOfALeaseOnNothing)
{
    EXPECT_FALSE(an::ui::readPixel(an::consumer::AssetReadLease{}, 1, 1).has_value());
}

TEST(ImageViewControlImageLoading, TheLensReadsOneValueOfAGreyImage)
{
    const std::optional<an::ui::PixelReadout> sampled =
        readUniform(an::tests::viewer::Channels::Grey, 0.42f, 0.0f, 0.0f, 2, 2);
    ASSERT_TRUE(sampled.has_value());
    EXPECT_EQ(sampled->channels, 1u);
    EXPECT_NEAR(sampled->values[0], 0.42, 1e-6);
    EXPECT_DOUBLE_EQ(sampled->values[1], 0.0);
    EXPECT_DOUBLE_EQ(sampled->values[2], 0.0);
}

TEST(ImageViewControlImageLoading, CalculateQuickZoomRegionRejectsInvalidInput)
{
    const auto invalidScale = an::ui::calculateQuickZoomRegion(QPoint(10, 10), QPoint(), 0.0, 100, 100);
    const auto invalidWidth = an::ui::calculateQuickZoomRegion(QPoint(10, 10), QPoint(), 1.0, 0, 100);

    EXPECT_TRUE(invalidScale.cropRect.isNull());
    EXPECT_TRUE(invalidWidth.cropRect.isNull());
}

TEST(ImageViewControlImageLoading, CalculateQuickZoomRegionClampsToImageBounds)
{
    const auto region = an::ui::calculateQuickZoomRegion(QPoint(-15, -20), QPoint(), 1.0, 50, 50, 24);

    EXPECT_EQ(region.cropRect, QRect(0, 0, 24, 24));
}

TEST(ImageViewControlImageLoading, CalculateQuickZoomRegionUsesScaledCoordinates)
{
    const auto region = an::ui::calculateQuickZoomRegion(QPoint(40, 20), QPoint(10, 0), 2.0, 100, 80, 24);

    EXPECT_EQ(region.cropRect, QRect(3, 0, 24, 24));
    EXPECT_DOUBLE_EQ(region.absolutePosition.x(), 15.0);
    EXPECT_DOUBLE_EQ(region.absolutePosition.y(), 10.0);
}

TEST(ImageViewControlImageLoading, CalculateQuickZoomLensPositionFlipsAtEdges)
{
    const QPoint lensPos = an::ui::calculateQuickZoomLensPosition(QPoint(180, 90), QSize(80, 80), QSize(220, 140));

    EXPECT_EQ(lensPos, QPoint(75, -15));
}

TEST(ImageHistoryTrackerUtils, StepNavigationHonorsBounds)
{
    EXPECT_FALSE(an::ui::history::previousStep(0).has_value());

    const auto previous = an::ui::history::previousStep(3);
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(*previous, 2U);

    EXPECT_FALSE(an::ui::history::nextStep(4, 3).has_value());

    const auto next = an::ui::history::nextStep(5, 3);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, 4U);
}

TEST(ImageHistoryTrackerUtils, CanJumpAndRemoveRespectInitialStepRules)
{
    EXPECT_TRUE(an::ui::history::canJumpToStep(4, 2, 1));
    EXPECT_FALSE(an::ui::history::canJumpToStep(4, 2, 2));
    EXPECT_FALSE(an::ui::history::canJumpToStep(4, 2, 8));

    EXPECT_FALSE(an::ui::history::canRemoveStep(1, 0));
    EXPECT_FALSE(an::ui::history::canRemoveStep(4, 0));
    EXPECT_TRUE(an::ui::history::canRemoveStep(4, 1));
    EXPECT_TRUE(an::ui::history::canRemoveStep(4, 3));
    EXPECT_FALSE(an::ui::history::canRemoveStep(4, 4));
}

TEST(ImageHistoryTrackerUtils, RemovingCurrentStepMovesSelectionToPreviousActiveStep)
{
    const auto plan = an::ui::history::planStepRemoval(5, 3, 3);

    EXPECT_EQ(plan.next_current_step, 2U);
    EXPECT_TRUE(plan.should_restore_after_removal);
}

TEST(ImageHistoryTrackerUtils, RemovingOlderStepKeepsSameSnapshotSelectedByShiftingIndex)
{
    const auto plan = an::ui::history::planStepRemoval(6, 4, 2);

    EXPECT_EQ(plan.next_current_step, 3U);
    EXPECT_FALSE(plan.should_restore_after_removal);
}

TEST(ImageHistoryTrackerUtils, RemovingFutureStepLeavesCurrentStepUnchanged)
{
    const auto plan = an::ui::history::planStepRemoval(6, 2, 4);

    EXPECT_EQ(plan.next_current_step, 2U);
    EXPECT_FALSE(plan.should_restore_after_removal);
}

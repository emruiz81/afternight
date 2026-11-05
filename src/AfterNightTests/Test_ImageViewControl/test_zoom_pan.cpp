// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <limits>
#include <optional>

#include "controls/ToolbarOverflowLayout.h"
#include "widgets/QuickZoomController.h"
#include "widgets/ViewPresentation.h"
#include "widgets/ZoomManager.h"

TEST(ImageViewControlZoomPan, ZoomManagerClampsMinimumScale)
{
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::clampScale(0.0), an::ui::ZoomManager::MIN_SCALE);
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::clampScale(-10.0), an::ui::ZoomManager::MIN_SCALE);
}

TEST(ImageViewControlZoomPan, ZoomManagerClampsMaximumScale)
{
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::clampScale(1.0e12), an::ui::ZoomManager::MAX_SCALE);
}

TEST(ImageViewControlZoomPan, ZoomManagerRejectsNaNAndInfinity)
{
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::clampScale(std::numeric_limits<double>::quiet_NaN()), 1.0);
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::clampScale(std::numeric_limits<double>::infinity()), 1.0);
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::clampScale(-std::numeric_limits<double>::infinity()), 1.0);
}

TEST(ImageViewControlZoomPan, ZoomManagerApplyRelativeZoomHandlesInvalidFactor)
{
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::applyRelativeZoom(2.0, 0.0), 2.0);
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::applyRelativeZoom(2.0, -1.0), 2.0);
}

TEST(ImageViewControlZoomPan, ZoomManagerApplyRelativeZoomHandlesInvalidCurrentScale)
{
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::applyRelativeZoom(0.0, 1.5), 1.5);
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::applyRelativeZoom(-10.0, 2.0), 2.0);
}

TEST(ImageViewControlZoomPan, ZoomManagerEffectiveRelativeFactorStopsAtMaximumScale)
{
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::effectiveRelativeZoomFactor(an::ui::ZoomManager::MAX_SCALE, 1.25), 1.0);
}

TEST(ImageViewControlZoomPan, ZoomManagerEffectiveRelativeFactorUsesClampedPartialStep)
{
    const double startingScale = 63.0;
    EXPECT_DOUBLE_EQ(an::ui::ZoomManager::effectiveRelativeZoomFactor(startingScale, 1.25),
                     an::ui::ZoomManager::MAX_SCALE / startingScale);
}

TEST(ImageViewControlZoomPan, ZoomManagerStaysInBoundsUnderRepeatedUpdates)
{
    double scale = 1.0;
    for (int i = 0; i < 1000; ++i)
    {
        const double factor = (i % 2 == 0) ? 1.25 : 0.8;
        scale = an::ui::ZoomManager::applyRelativeZoom(scale, factor);
        EXPECT_GE(scale, an::ui::ZoomManager::MIN_SCALE);
        EXPECT_LE(scale, an::ui::ZoomManager::MAX_SCALE);
    }
}

// Zoomed out, a view presents its display proxy at the largest factor that leaves it no smaller than what is painted,
// in device pixels, whatever its mode; at or above full zoom it presents native pixels.
TEST(ImageViewControlZoomPan, ZoomedOutTheProxyIsTheLargestReductionNoSmallerThanWhatIsPainted)
{
    const auto factorAt = [](double scale, double ratio = 1.0) {
        return an::ui::viewDisplayGeometry(6016u, 4016u, scale, ratio, std::nullopt).proxyFactor;
    };
    EXPECT_EQ(factorAt(0.133), 7u) << "a 6016-pixel frame fitted to 802 pixels";
    EXPECT_EQ(factorAt(0.125), 8u) << "exactly one eighth is a factor of eight";
    EXPECT_EQ(factorAt(1.0 / 3.0), 3u);
    EXPECT_EQ(factorAt(0.6), 1u) << "less than two is no reduction";
    EXPECT_EQ(factorAt(1.0), 1u);
    EXPECT_EQ(factorAt(4.0), 1u);
    EXPECT_EQ(factorAt(0.25, 2.0), 2u) << "a scaled display paints twice the pixels";
    EXPECT_EQ(an::ui::viewDisplayGeometry(5u, 3u, 0.01, 1.0, std::nullopt).proxyFactor, 3u)
        << "a proxy keeps at least one pixel each way";
    for (const double scale : {0.05, 0.133, 0.21, 0.4, 0.49, 0.51, 0.99})
    {
        const std::uint32_t factor = factorAt(scale);
        EXPECT_GE(6016.0 / factor, 6016.0 * scale) << scale;
        EXPECT_LT(6016.0 / (factor + 1u), 6016.0 * scale) << scale << ": a larger factor would paint less";
    }
    EXPECT_FALSE(an::ui::viewDisplayGeometry(6016u, 4016u, 0.133, 1.0, std::nullopt).region.has_value())
        << "nothing known of what is shown presents the whole image";
}

// The region a view presents is what it shows with half its size again each way, aligned to whole blocks of the
// factor; the whole image when that covers it.
TEST(ImageViewControlZoomPan, TheRegionIsWhatIsShownWithAMarginAlignedToTheFactor)
{
    const an::ui::ViewRegion shown{.x = 1000u, .y = 800u, .width = 800u, .height = 536u};
    const an::ui::ViewDisplayGeometry full = an::ui::viewDisplayGeometry(6000u, 4000u, 1.0, 1.0, shown);
    ASSERT_TRUE(full.region.has_value());
    EXPECT_EQ(full.proxyFactor, 1u);
    EXPECT_EQ(*full.region, (an::ui::ViewRegion{.x = 600u, .y = 532u, .width = 1600u, .height = 1072u}));

    const an::ui::ViewRegion corner{.x = 5900u, .y = 3950u, .width = 100u, .height = 50u};
    const an::ui::ViewDisplayGeometry edge = an::ui::viewDisplayGeometry(6000u, 4000u, 2.0, 1.0, corner);
    ASSERT_TRUE(edge.region.has_value());
    EXPECT_EQ(edge.region->x + edge.region->width, 6000u) << "clipped to the image";
    EXPECT_EQ(edge.region->y + edge.region->height, 4000u);

    const an::ui::ViewRegion part{.x = 1001u, .y = 501u, .width = 801u, .height = 401u};
    const an::ui::ViewDisplayGeometry reduced = an::ui::viewDisplayGeometry(6000u, 4000u, 0.4, 1.0, part);
    ASSERT_EQ(reduced.proxyFactor, 2u);
    ASSERT_TRUE(reduced.region.has_value());
    for (const std::uint32_t value :
         {reduced.region->x, reduced.region->y, reduced.region->width, reduced.region->height})
    {
        EXPECT_EQ(value % 2u, 0u) << "whole blocks of the factor";
    }
    EXPECT_LE(reduced.region->x, part.x);
    EXPECT_GE(reduced.region->x + reduced.region->width, part.x + part.width);

    const an::ui::ViewRegion everything{.x = 0u, .y = 0u, .width = 6000u, .height = 4000u};
    EXPECT_FALSE(an::ui::viewDisplayGeometry(6000u, 4000u, 0.133, 1.0, everything).region.has_value())
        << "an image shown whole is presented whole";
    const an::ui::ViewRegion most{.x = 200u, .y = 100u, .width = 5000u, .height = 3500u};
    EXPECT_FALSE(an::ui::viewDisplayGeometry(6000u, 4000u, 0.6, 1.0, most).region.has_value())
        << "a margin that reaches every edge covers the image";
}

TEST(ImageViewControlZoomPan, QuickZoomLensStaysOffsetWhenSpaceAvailable)
{
    const QPoint lensPos = an::ui::calculateQuickZoomLensPosition(QPoint(10, 10), QSize(80, 80), QSize(500, 400));

    EXPECT_EQ(lensPos, QPoint(35, 35));
}

TEST(ImageViewControlZoomPan, ToolbarOverflowLayoutReturnsNoOverflowWhenItemsFit)
{
    const std::vector<int> widths{40, 50, 60};
    EXPECT_EQ(an::ui::controls::firstOverflowIndex(widths, 200, 28), -1);
}

TEST(ImageViewControlZoomPan, ToolbarOverflowLayoutReservesOverflowButtonSpace)
{
    const std::vector<int> widths{70, 70, 70};
    EXPECT_EQ(an::ui::controls::firstOverflowIndex(widths, 200, 28), 2);
}

TEST(ImageViewControlZoomPan, ToolbarOverflowLayoutOverflowsFromFirstItemWhenTooNarrow)
{
    const std::vector<int> widths{50, 50};
    EXPECT_EQ(an::ui::controls::firstOverflowIndex(widths, 20, 28), 0);
}

TEST(ImageViewControlZoomPan, ToolbarOverflowLayoutClampsInvalidWidths)
{
    const std::vector<int> widths{-10, 0, 30};
    EXPECT_EQ(an::ui::controls::firstOverflowIndex(widths, 28, 20), 2);
}

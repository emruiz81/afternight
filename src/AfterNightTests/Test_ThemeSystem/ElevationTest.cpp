// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Elevation: how a raised part stands off what is under it under the Observatory profile. A 1 px line of the edge
// colour under the part and a shadow 10 px down whose edge fades over 28 px, from a nine-patch image rendered once per
// corner radius, device pixel ratio and colour, kept, and let go when the theme changes. A raised part that is a window
// keeps the room the shadow needs around its surface.

#include "fixtures/ObservatoryLook.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "theme/Elevation.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRectF>

#include <gtest/gtest.h>

namespace
{

using an::ui::testing::observatoryLooks;
using an::ui::testing::ObservatoryStyleScope;

/// The opacity of a pixel of an image, under a logical point.
int alphaAt(const QImage& image, const qreal x, const qreal y)
{
    const qreal ratio = image.devicePixelRatio();
    return qAlpha(image.pixel(static_cast<int>(x * ratio), static_cast<int>(y * ratio)));
}

} // namespace

// The room a raised window keeps: half the spread at the sides, less the offset above, more the offset below.
TEST(Elevation, WindowMarginsHoldTheShadow)
{
    EXPECT_EQ(an::ui::kElevationShadowOffset, 10);
    EXPECT_EQ(an::ui::kElevationShadowSpread, 28);
    EXPECT_EQ(an::ui::elevationWindowMargins(), QMargins(14, 4, 14, 24));
}

// One nine-patch per corner radius and device pixel ratio: asked again, the kept image is answered and nothing is
// rendered; another radius or ratio is another image, of its own size.
TEST(Elevation, OneImagePerRadiusAndRatio)
{
    const ObservatoryStyleScope scope(observatoryLooks()[0]);
    an::ui::dropElevationImages();
    ASSERT_EQ(an::ui::testing::keptElevationImages(), 0u);
    const std::uint64_t before = an::ui::testing::elevationRenders();

    const QImage first = an::ui::elevationShadow(8, 1.0);
    ASSERT_FALSE(first.isNull());
    EXPECT_EQ(an::ui::testing::elevationRenders(), before + 1u);
    const QImage again = an::ui::elevationShadow(8, 1.0);
    EXPECT_EQ(again.cacheKey(), first.cacheKey());
    EXPECT_EQ(an::ui::testing::elevationRenders(), before + 1u) << "a kept image was rendered again";

    const QImage larger = an::ui::elevationShadow(12, 1.0);
    const QImage sharper = an::ui::elevationShadow(8, 2.0);
    EXPECT_EQ(an::ui::testing::elevationRenders(), before + 3u);
    EXPECT_EQ(an::ui::testing::keptElevationImages(), 3u);

    // A rounded square of the radius with the whole spread all round, at the ratio: the fade lies on both sides of the
    // square's edge and never reaches its middle, which is stretched under a part of any size.
    EXPECT_EQ(first.size(), QSize(2 * (8 + 28) + 1, 2 * (8 + 28) + 1));
    EXPECT_EQ(larger.size(), QSize(2 * (12 + 28) + 1, 2 * (12 + 28) + 1));
    EXPECT_EQ(sharper.size(), QSize(2 * (2 * (8 + 28) + 1), 2 * (2 * (8 + 28) + 1)));
    EXPECT_EQ(first.devicePixelRatio(), 1.0);
    EXPECT_EQ(sharper.devicePixelRatio(), 2.0);

    // Its middle is the shadow's colour, its corner nothing.
    const QColor shadow = an::ui::tokens().colors.shadowOverlay;
    const QColor middle = first.pixelColor(first.width() / 2, first.height() / 2);
    EXPECT_NEAR(middle.alpha(), shadow.alpha(), 2);
    EXPECT_EQ(qAlpha(first.pixel(0, 0)), 0);
}

// A change of theme lets every kept image go: the next ask renders the shadow in the new theme's colour.
TEST(Elevation, DroppedOnAThemeChange)
{
    ObservatoryStyleScope scope(observatoryLooks()[0]);
    (void)an::ui::elevationShadow(8, 1.0);
    ASSERT_GT(an::ui::testing::keptElevationImages(), 0u);
    scope.setLook(observatoryLooks()[1]);
    EXPECT_EQ(an::ui::testing::keptElevationImages(), 0u);
    const QImage light = an::ui::elevationShadow(8, 1.0);
    EXPECT_NEAR(light.pixelColor(light.width() / 2, light.height() / 2).alpha(),
                an::ui::tokens().colors.shadowOverlay.alpha(), 2);
}

// Painted under a part, the shadow lies below it - its edge fading out half the spread past the part moved down by the
// offset, nothing above that - and the edge line is the pixel row just under the part.
TEST(Elevation, TheShadowLiesBelowThePartAndTheEdgeUnderIt)
{
    const ObservatoryStyleScope scope(observatoryLooks()[0]);
    const an::ui::ColorTokens& c = an::ui::tokens().colors;
    for (const qreal ratio : an::ui::testing::kRenderRatios)
    {
        SCOPED_TRACE(ratio);
        QImage image(QSize(qRound(200 * ratio), qRound(200 * ratio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::transparent);
        const QRectF part(50, 40, 100, 60);
        {
            QPainter painter(&image);
            an::ui::paintElevation(painter, part, 8);
        }
        // The edge line: under the part's bottom edge, away from its corners.
        EXPECT_NEAR(alphaAt(image, 100, part.bottom() + 0.5),
                    255 - (255 - c.edge.alpha()) * (255 - c.shadowOverlay.alpha()) / 255, 40)
            << "the edge line";
        // Under the part's middle, below it: the shadow at its full colour.
        EXPECT_GT(alphaAt(image, 100, part.bottom() + 6), c.shadowOverlay.alpha() / 2);
        // Past the fade, nothing: to the side, above, and below.
        EXPECT_EQ(alphaAt(image, part.right() + 15, 70), 0);
        EXPECT_EQ(alphaAt(image, 100, part.top() - 5), 0);
        EXPECT_EQ(alphaAt(image, 100, part.bottom() + 10 + 15), 0);
        // Within the fade, some.
        EXPECT_GT(alphaAt(image, part.right() + 4, 80), 0);
        EXPECT_LT(alphaAt(image, part.right() + 4, 80), c.shadowOverlay.alpha());
    }
}

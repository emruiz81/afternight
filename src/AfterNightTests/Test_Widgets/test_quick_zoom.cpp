// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "fixtures/TestImageGenerators.h"
#include "widgets/QuickZoomController.h"
#include "widgets/ViewPresentation.h"

#include <optional>

TEST(WidgetsQuickZoom, LensPositionUsesDefaultOffset)
{
    const QPoint pos = an::ui::calculateQuickZoomLensPosition(QPoint(5, 7), QSize(64, 64), QSize(400, 300));
    EXPECT_EQ(pos, QPoint(30, 32));
}

TEST(WidgetsQuickZoom, LensPositionFlipsNearBoundaries)
{
    const QPoint pos = an::ui::calculateQuickZoomLensPosition(QPoint(195, 145), QSize(64, 64), QSize(220, 160));
    EXPECT_LT(pos.x(), 195);
    EXPECT_LT(pos.y(), 145);
}

// What the lens reads out of a pixel is the presentation's readout of the image under the cursor.
TEST(WidgetsQuickZoom, PixelSamplingReturnsExpectedChannels)
{
    an::Result<an::consumer::EngineSession> session = an::tests::fixtures::engine::createFixtureSession(1u);
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    an::Result<an::consumer::ImageAsset> image =
        an::tests::fixtures::createRgbFloatAsset(*session, 10u, 10u, 0.1, 0.3, 0.9);
    ASSERT_TRUE(image.hasValue()) << image.status().message();
    an::Result<an::consumer::AssetReadLease> lease = image->read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();

    const std::optional<an::ui::PixelReadout> values = an::ui::readPixel(*lease, 4, 4);
    ASSERT_TRUE(values.has_value());
    EXPECT_EQ(values->channels, 3u);
    EXPECT_NEAR(values->values[0], 0.1, 1e-6);
    EXPECT_NEAR(values->values[1], 0.3, 1e-6);
    EXPECT_NEAR(values->values[2], 0.9, 1e-6);
}

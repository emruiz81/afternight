// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QTimeZone>

#include <optional>
#include <utility>

#include "processes/ImageIntegration/model/FrameMetadata.h"

TEST(FrameMetadata, StoresCoreFields)
{
    const QDateTime captured = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
    FrameMetadata metadata(FrameType::Light, QSize(6000, 4000), 120.5, 5.0, 100, 10, QStringLiteral("Ha"),
                           QStringLiteral("M42"), captured, QStringLiteral("HighGain"));

    EXPECT_EQ(metadata.type(), FrameType::Light);
    EXPECT_EQ(metadata.dimensions(), QSize(6000, 4000));
    EXPECT_DOUBLE_EQ(metadata.exposureSeconds(), 120.5);
    ASSERT_TRUE(metadata.temperatureC().has_value());
    EXPECT_DOUBLE_EQ(metadata.temperatureC().value(), 5.0);
    ASSERT_TRUE(metadata.isoGain().has_value());
    EXPECT_EQ(metadata.isoGain().value(), 100);
    ASSERT_TRUE(metadata.offset().has_value());
    EXPECT_EQ(metadata.offset().value(), 10);
    EXPECT_EQ(metadata.filter(), QStringLiteral("Ha"));
    EXPECT_EQ(metadata.objectName(), QStringLiteral("M42"));
    EXPECT_EQ(metadata.capturedAtUtc(), captured);
    EXPECT_EQ(metadata.readoutMode(), QStringLiteral("HighGain"));
}

TEST(FrameMetadata, AllowsMissingOptionalFields)
{
    const QDateTime captured = QDateTime::fromSecsSinceEpoch(0, QTimeZone::UTC);
    FrameMetadata metadata(FrameType::Bias, QSize(100, 100), 0.0, std::nullopt, std::nullopt, std::nullopt, QString(),
                           QString(), captured, QString());

    EXPECT_FALSE(metadata.temperatureC().has_value());
    EXPECT_FALSE(metadata.isoGain().has_value());
    EXPECT_FALSE(metadata.offset().has_value());
}

TEST(FrameMetadata, ResolutionStringFormats)
{
    FrameMetadata metadata(FrameType::Dark, QSize(2000, 1500), 10.0, std::nullopt, 200, 20, QStringLiteral(""),
                           QStringLiteral(""), QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC),
                           QStringLiteral("LowNoise"));

    EXPECT_EQ(metadata.resolutionString(), QStringLiteral("2000x1500"));
}

TEST(FrameMetadata, CopyAndMoveSemantics)
{
    FrameMetadata metadata(FrameType::Flat, QSize(300, 200), 1.25, 2.0, 50, 5, QStringLiteral("L"), QStringLiteral(""),
                           QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC), QStringLiteral(""));

    FrameMetadata copy = metadata;
    FrameMetadata moved = std::move(copy);

    EXPECT_EQ(moved.dimensions(), metadata.dimensions());
    EXPECT_DOUBLE_EQ(moved.exposureSeconds(), metadata.exposureSeconds());
    EXPECT_EQ(moved.resolutionString(), metadata.resolutionString());
}

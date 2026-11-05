// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QTimeZone>

#include "processes/ImageIntegration/model/FrameTreeFilter.h"
#include "processes/ImageIntegration/model/FrameId.h"

namespace
{
FrameInfo frameWithName(const QString& name)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("Obj"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());

    const QString path = QStringLiteral("C:/test/%1").arg(name);
    return FrameInfo(FrameId(path), path, meta);
}

FrameInfo frameWithExposure(double seconds)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), seconds, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("Obj"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());

    const QString path = QStringLiteral("C:/test/exposure_%1.fit").arg(seconds, 0, 'f', 1);
    return FrameInfo(FrameId(path), path, meta);
}

FrameInfo frameWithFilter(const QString& filterName)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt, filterName,
                       QStringLiteral("Obj"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());

    const QString path = QStringLiteral("C:/test/%1.fit").arg(filterName);
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

TEST(FrameTreeFilter, FiltersByFilename)
{
    FrameTreeFilter filter;
    filter.setSearchText(QStringLiteral("M42"));

    EXPECT_TRUE(filter.matches(frameWithName(QStringLiteral("M42_Light_001.fit"))));
    EXPECT_FALSE(filter.matches(frameWithName(QStringLiteral("M31_Light_001.fit"))));
}

TEST(FrameTreeFilter, CaseInsensitive)
{
    FrameTreeFilter filter;
    filter.setSearchText(QStringLiteral("m42"));

    EXPECT_TRUE(filter.matches(frameWithName(QStringLiteral("M42_Light_001.fit"))));
}

TEST(FrameTreeFilter, FiltersByExposure)
{
    FrameTreeFilter filter;
    filter.setExposureRange(100.0, 200.0);

    EXPECT_TRUE(filter.matches(frameWithExposure(150.0)));
    EXPECT_FALSE(filter.matches(frameWithExposure(50.0)));
}

TEST(FrameTreeFilter, FiltersByFilterName)
{
    FrameTreeFilter filter;
    filter.setFilterName(QStringLiteral("Ha"));

    EXPECT_TRUE(filter.matches(frameWithFilter(QStringLiteral("Ha"))));
    EXPECT_FALSE(filter.matches(frameWithFilter(QStringLiteral("OIII"))));
}

TEST(FrameTreeFilter, CombinesFilters)
{
    FrameTreeFilter filter;
    filter.setSearchText(QStringLiteral("M42"));
    filter.setFilterName(QStringLiteral("Ha"));

    FrameInfo good = frameWithName(QStringLiteral("M42_Light.fit"));
    good.metadata = FrameMetadata(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                                  QStringLiteral("Ha"), QStringLiteral("Obj"),
                                  QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());

    FrameInfo wrongFilter = good;
    wrongFilter.metadata = FrameMetadata(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt,
                                         std::nullopt, QStringLiteral("OIII"), QStringLiteral("Obj"),
                                         QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());

    FrameInfo wrongName = frameWithName(QStringLiteral("M31_Light.fit"));
    wrongName.metadata = good.metadata;

    EXPECT_TRUE(filter.matches(good));
    EXPECT_FALSE(filter.matches(wrongFilter));
    EXPECT_FALSE(filter.matches(wrongName));
}

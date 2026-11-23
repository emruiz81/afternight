// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The cache of decoded thumbnails Home's recent files and featured image share: an entry found as it was put in, none
/// for a key never put in, the oldest let go past the capacity, every one let go on clear, and a key for each tile
/// size.

#include "controls/DashboardThumbnailCache.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <gtest/gtest.h>

TEST(DashboardThumbnailCacheTest, InsertAndFindReturnsCachedEntry)
{
    DashboardThumbnailCache cache(4);

    QImage image(10, 10, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const QSize sourceSize(100, 200);
    const QString key = DashboardThumbnailCache::cacheKey(QStringLiteral("/tmp/test.fits"), 300);

    cache.insert(key, image, sourceSize, 12345);

    const auto* entry = cache.find(key);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->sourceSize, sourceSize);
    EXPECT_EQ(entry->lastModifiedMs, 12345);
    EXPECT_FALSE(entry->displayImage.isNull());
    EXPECT_EQ(entry->displayImage.size(), image.size());
}

TEST(DashboardThumbnailCacheTest, FindReturnsNullptrForMissingKey)
{
    DashboardThumbnailCache cache(4);
    EXPECT_EQ(cache.find(QStringLiteral("missing")), nullptr);
}

TEST(DashboardThumbnailCacheTest, EvictsOldestEntriesWhenOverCapacity)
{
    DashboardThumbnailCache cache(2);

    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::blue);

    cache.insert(QStringLiteral("a"), image, QSize(1, 1), 1);
    cache.insert(QStringLiteral("b"), image, QSize(2, 2), 2);
    cache.insert(QStringLiteral("c"), image, QSize(3, 3), 3);

    // The oldest, "a", is let go.
    EXPECT_EQ(cache.find(QStringLiteral("a")), nullptr);
    EXPECT_NE(cache.find(QStringLiteral("b")), nullptr);
    EXPECT_NE(cache.find(QStringLiteral("c")), nullptr);
}

TEST(DashboardThumbnailCacheTest, ClearRemovesAllEntries)
{
    DashboardThumbnailCache cache(4);
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::green);

    cache.insert(QStringLiteral("x"), image, QSize(10, 20), 99);
    ASSERT_NE(cache.find(QStringLiteral("x")), nullptr);

    cache.clear();
    EXPECT_EQ(cache.find(QStringLiteral("x")), nullptr);
}

TEST(DashboardThumbnailCacheTest, CacheKeyIncludesTileSize)
{
    const QString key1 = DashboardThumbnailCache::cacheKey(QStringLiteral("/path/img.fits"), 300);
    const QString key2 = DashboardThumbnailCache::cacheKey(QStringLiteral("/path/img.fits"), 400);
    EXPECT_NE(key1, key2);

    const QString key3 = DashboardThumbnailCache::cacheKey(QStringLiteral("/path/img.fits"), 300);
    EXPECT_EQ(key1, key3);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QtTest/QSignalSpy>
#include <QTimeZone>

#include <optional>

#include <QElapsedTimer>

#include "processes/ImageIntegration/model/FrameCollection.h"
#include "processes/ImageIntegration/model/GroupingEngine.h"

namespace
{
FrameInfo makeFrame(FrameType type, const QString& filename, const QString& display_path, const QSize& size,
                    double exposure, const QDateTime& captured_at, std::optional<double> temperature,
                    std::optional<int> iso_gain, std::optional<int> offset, const QString& filter,
                    const QString& object_name, const QString& session, const QString& custom_tag)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, size, exposure, temperature, iso_gain, offset, filter, object_name, captured_at,
                           QStringLiteral("Default"));

    return FrameInfo(FrameId(path), display_path, metadata, session, custom_tag);
}

std::vector<FrameInfo> makeFrames(int count)
{
    std::vector<FrameInfo> frames;
    frames.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        frames.push_back(
            makeFrame(FrameType::Light, QStringLiteral("light_%1.fit").arg(i), QStringLiteral("light_%1.fit").arg(i),
                      QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(3 + i, QTimeZone::UTC), 0.0, 100, 10,
                      QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"), QString()));
    }
    return frames;
}
} // namespace

TEST(FrameCollection, AddFrameEmitsSignal)
{
    FrameCollection collection;
    QSignalSpy added_spy(&collection, &FrameCollection::framesAdded);

    collection.addFrame(makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QStringLiteral("bias.fit"),
                                  QSize(100, 100), 0.0, QDateTime::fromSecsSinceEpoch(1, QTimeZone::UTC), std::nullopt,
                                  100, 10, QString(), QString(), QString(), QString()));

    EXPECT_EQ(added_spy.count(), 1);
}

TEST(FrameCollection, RemoveFrameEmitsSignal)
{
    FrameCollection collection;
    FrameInfo frame = makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QStringLiteral("dark.fit"),
                                QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(2, QTimeZone::UTC), 0.0, 100, 10,
                                QString(), QString(), QString(), QString());

    collection.addFrame(frame);

    QSignalSpy removed_spy(&collection, &FrameCollection::framesRemoved);
    EXPECT_TRUE(collection.removeFrame(frame.id));
    EXPECT_EQ(removed_spy.count(), 1);
}

TEST(FrameCollection, BatchAddEmitsSingleSignal)
{
    FrameCollection collection;
    QSignalSpy added_spy(&collection, &FrameCollection::framesAdded);
    QSignalSpy reset_spy(&collection, &FrameCollection::collectionReset);

    collection.beginBatchUpdate();
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QStringLiteral("light1.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(3, QTimeZone::UTC), 0.0, 100, 10,
                                  QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                                  QStringLiteral("")));
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QStringLiteral("light2.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(4, QTimeZone::UTC), 0.0, 100, 10,
                                  QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                                  QStringLiteral("")));
    collection.endBatchUpdate();

    EXPECT_EQ(added_spy.count(), 0);
    EXPECT_EQ(reset_spy.count(), 1);
}

TEST(FrameCollection, AddFramesEmitsSingleSignal)
{
    FrameCollection collection;
    QSignalSpy added_spy(&collection, &FrameCollection::framesAdded);
    QSignalSpy reset_spy(&collection, &FrameCollection::collectionReset);

    collection.addFrames(makeFrames(100));

    EXPECT_EQ(added_spy.count(), 1);
    EXPECT_EQ(reset_spy.count(), 0);
}

TEST(FrameCollection, BatchAddAllFramesPresent)
{
    FrameCollection collection;
    const auto frames = makeFrames(100);
    collection.addFrames(frames);

    for (const auto& frame : frames)
    {
        EXPECT_TRUE(collection.findById(frame.id).has_value());
    }
}

TEST(Performance, BatchAdd10000FramesUnder500ms)
{
    FrameCollection collection;
    const auto frames = makeFrames(10000);

    QElapsedTimer timer;
    timer.start();
    collection.addFrames(frames);

    const qint64 elapsed_ms = timer.nsecsElapsed() / 1000000;
    EXPECT_LT(elapsed_ms, 500);
}

TEST(FrameCollection, FilterByType)
{
    FrameCollection collection;
    collection.addFrame(makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QStringLiteral("bias.fit"),
                                  QSize(100, 100), 0.0, QDateTime::fromSecsSinceEpoch(5, QTimeZone::UTC), std::nullopt,
                                  100, 10, QString(), QString(), QString(), QString()));
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("light.fit"), QStringLiteral("light.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(6, QTimeZone::UTC), 0.0, 100, 10,
                                  QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                                  QStringLiteral("")));

    auto lights = collection.filterByType(FrameType::Light);
    EXPECT_EQ(lights.size(), 1u);
    EXPECT_EQ(lights.front().displayPath, QStringLiteral("light.fit"));
}

TEST(FrameCollection, FilterByGroup)
{
    FrameCollection collection;
    GroupingOptions options;
    options.byFilter = true;

    FrameInfo light_a =
        makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QStringLiteral("light_a.fit"), QSize(100, 100), 10.0,
                  QDateTime::fromSecsSinceEpoch(7, QTimeZone::UTC), 0.0, 100, 10, QStringLiteral("L"),
                  QStringLiteral("M42"), QStringLiteral("2025-01-01"), QStringLiteral(""));
    FrameInfo light_b =
        makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QStringLiteral("light_b.fit"), QSize(100, 100), 10.0,
                  QDateTime::fromSecsSinceEpoch(8, QTimeZone::UTC), 0.0, 100, 10, QStringLiteral("Ha"),
                  QStringLiteral("M42"), QStringLiteral("2025-01-01"), QStringLiteral(""));

    collection.addFrame(light_a);
    collection.addFrame(light_b);

    GroupingEngine engine;
    GroupKey target_key = engine.computeGroupKey(light_a, FrameType::Light, options);
    auto matches = collection.filterByGroup(target_key, FrameType::Light, options);

    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches.front().displayPath, QStringLiteral("light_a.fit"));
}

TEST(FrameCollection, SortByPath)
{
    FrameCollection collection;
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("b.fit"), QStringLiteral("b.fit"), QSize(100, 100),
                                  10.0, QDateTime::fromSecsSinceEpoch(9, QTimeZone::UTC), 0.0, 100, 10, QString(),
                                  QString(), QString(), QString()));
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("a.fit"), QStringLiteral("a.fit"), QSize(100, 100),
                                  10.0, QDateTime::fromSecsSinceEpoch(10, QTimeZone::UTC), 0.0, 100, 10, QString(),
                                  QString(), QString(), QString()));

    auto sorted = collection.sortedByPath();
    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted.front().displayPath, QStringLiteral("a.fit"));
}

TEST(FrameCollection, SortByDate)
{
    FrameCollection collection;
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("later.fit"), QStringLiteral("later.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(20, QTimeZone::UTC), 0.0, 100,
                                  10, QString(), QString(), QString(), QString()));
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("earlier.fit"), QStringLiteral("earlier.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(1, QTimeZone::UTC), 0.0, 100, 10,
                                  QString(), QString(), QString(), QString()));

    auto sorted = collection.sortedByDate();
    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted.front().displayPath, QStringLiteral("earlier.fit"));
}

TEST(FrameCollection, SortByExposure)
{
    FrameCollection collection;
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("long.fit"), QStringLiteral("long.fit"),
                                  QSize(100, 100), 20.0, QDateTime::fromSecsSinceEpoch(11, QTimeZone::UTC), 0.0, 100,
                                  10, QString(), QString(), QString(), QString()));
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("short.fit"), QStringLiteral("short.fit"),
                                  QSize(100, 100), 5.0, QDateTime::fromSecsSinceEpoch(12, QTimeZone::UTC), 0.0, 100, 10,
                                  QString(), QString(), QString(), QString()));

    auto sorted = collection.sortedByExposure();
    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted.front().displayPath, QStringLiteral("short.fit"));
}

TEST(FrameCollection, FindByFrameId)
{
    FrameCollection collection;
    FrameInfo frame = makeFrame(FrameType::Light, QStringLiteral("find.fit"), QStringLiteral("find.fit"),
                                QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(13, QTimeZone::UTC), 0.0, 100, 10,
                                QString(), QString(), QString(), QString());

    collection.addFrame(frame);

    auto found = collection.findById(frame.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->displayPath, QStringLiteral("find.fit"));
}

TEST(FrameCollection, IteratorWorks)
{
    FrameCollection collection;
    collection.addFrame(makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QStringLiteral("bias.fit"),
                                  QSize(100, 100), 0.0, QDateTime::fromSecsSinceEpoch(14, QTimeZone::UTC), std::nullopt,
                                  100, 10, QString(), QString(), QString(), QString()));
    collection.addFrame(makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QStringLiteral("dark.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(15, QTimeZone::UTC), 0.0, 100,
                                  10, QString(), QString(), QString(), QString()));

    auto snapshot = collection.frames();
    size_t count = 0;
    for (const auto& frame : snapshot)
    {
        (void)frame;
        ++count;
    }

    EXPECT_EQ(count, 2u);
}

TEST(FrameCollection, ClearEmitsSignal)
{
    FrameCollection collection;
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("clear.fit"), QStringLiteral("clear.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(16, QTimeZone::UTC), 0.0, 100,
                                  10, QString(), QString(), QString(), QString()));

    QSignalSpy cleared_spy(&collection, &FrameCollection::collectionCleared);
    collection.clear();

    EXPECT_EQ(cleared_spy.count(), 1);
}

TEST(FrameCollection, CountsByType)
{
    FrameCollection collection;
    collection.addFrame(makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QStringLiteral("bias.fit"),
                                  QSize(100, 100), 0.0, QDateTime::fromSecsSinceEpoch(17, QTimeZone::UTC), std::nullopt,
                                  100, 10, QString(), QString(), QString(), QString()));
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("light.fit"), QStringLiteral("light.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(18, QTimeZone::UTC), 0.0, 100,
                                  10, QString(), QString(), QString(), QString()));
    collection.addFrame(makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QStringLiteral("light2.fit"),
                                  QSize(100, 100), 10.0, QDateTime::fromSecsSinceEpoch(19, QTimeZone::UTC), 0.0, 100,
                                  10, QString(), QString(), QString(), QString()));

    auto counts = collection.countsByType();
    EXPECT_EQ(counts[FrameType::Bias], 1u);
    EXPECT_EQ(counts[FrameType::Light], 2u);
}

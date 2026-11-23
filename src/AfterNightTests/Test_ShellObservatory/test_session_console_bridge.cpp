// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QtTest/QSignalSpy>

#include <AfterNightPlatform/Logging.h>

#include <thread>

#include "app/SessionConsoleBridge.h"

namespace
{

using an::ui::SessionConsoleBridge;

} // namespace

TEST(SessionConsoleBridge, RecordsEntriesAndEmitsSignal)
{
    SessionConsoleBridge bridge(nullptr, 8);
    auto sink = bridge.createSink();
    sink->set_pattern("%v");

    QSignalSpy appendedSpy(&bridge, &SessionConsoleBridge::sessionEntryAppended);

    an::platform::logToSink(sink, an::platform::LogLevel::Info, "hello session");
    QCoreApplication::processEvents();

    const auto snapshot = bridge.entriesSnapshot();
    ASSERT_EQ(snapshot.size(), 1);
    EXPECT_EQ(snapshot.front().text, QStringLiteral("hello session"));
    EXPECT_EQ(snapshot.front().level, spdlog::level::info);
    EXPECT_EQ(appendedSpy.count(), 1);
}

TEST(SessionConsoleBridge, ClearHistoryDropsBacklog)
{
    SessionConsoleBridge bridge(nullptr, 8);
    auto sink = bridge.createSink();
    sink->set_pattern("%v");

    QSignalSpy clearedSpy(&bridge, &SessionConsoleBridge::sessionHistoryCleared);

    an::platform::logToSink(sink, an::platform::LogLevel::Warn, "one");
    an::platform::logToSink(sink, an::platform::LogLevel::Error, "two");
    QCoreApplication::processEvents();
    ASSERT_EQ(bridge.entriesSnapshot().size(), 2);

    bridge.clearHistory();
    QCoreApplication::processEvents();

    EXPECT_TRUE(bridge.entriesSnapshot().isEmpty());
    EXPECT_EQ(clearedSpy.count(), 1);
}

TEST(SessionConsoleBridge, KeepsOnlyMostRecentEntries)
{
    SessionConsoleBridge bridge(nullptr, 2);
    auto sink = bridge.createSink();
    sink->set_pattern("%v");

    an::platform::logToSink(sink, an::platform::LogLevel::Info, "first");
    an::platform::logToSink(sink, an::platform::LogLevel::Warn, "second");
    an::platform::logToSink(sink, an::platform::LogLevel::Error, "third");
    QCoreApplication::processEvents();

    const auto snapshot = bridge.entriesSnapshot();
    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_EQ(snapshot.at(0).text, QStringLiteral("second"));
    EXPECT_EQ(snapshot.at(1).text, QStringLiteral("third"));
}

TEST(SessionConsoleBridge, CrossThreadEntriesRemainChronologicalInUiQueue)
{
    SessionConsoleBridge bridge(nullptr, 8);
    auto sink = bridge.createSink();
    sink->set_pattern("%v");

    std::thread worker([&sink]() { an::platform::logToSink(sink, an::platform::LogLevel::Info, "worker-first"); });
    worker.join();

    an::platform::logToSink(sink, an::platform::LogLevel::Info, "main-second");
    QCoreApplication::processEvents();

    const auto snapshot = bridge.entriesSnapshot();
    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_EQ(snapshot.at(0).text, QStringLiteral("worker-first"));
    EXPECT_EQ(snapshot.at(1).text, QStringLiteral("main-second"));
}

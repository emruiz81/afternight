// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The persistence executor every settings store and the integration's databases go through: requests from any thread
/// run one at a time on one thread of its own, and a shutdown runs what was queued before it and refuses what comes
/// after.

#include "controls/PersistenceDbExecutor.h"

#include <AfterNightPlatform/Logging.h>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{

class PersistenceDbExecutorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!an::platform::isLoggingInitialized())
        {
            an::platform::initializeLogging("test_thread_safety.log", 1, 1, 100, an::platform::LogLevel::Error);
        }
        m_tmpDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_tmpDir->isValid());
        m_dbPath = m_tmpDir->filePath(QStringLiteral("test_executor.sqlite"));
    }

    std::unique_ptr<QTemporaryDir> m_tmpDir;
    QString m_dbPath;
};

} // namespace

// Requests from two threads at once each run whole, one at a time: every insert lands and a later read counts them.
TEST_F(PersistenceDbExecutorTest, SerializesRequests)
{
    PersistenceDbExecutor exec(m_dbPath, QStringLiteral("test_serialize"));
    ASSERT_TRUE(exec.isOpen());
    exec.execute([](QSqlDatabase& db) {
        QSqlQuery q(db);
        EXPECT_TRUE(q.exec(QStringLiteral("CREATE TABLE t(val INTEGER)")));
    });

    std::atomic<int> insertsDone{0};
    std::atomic<int> inFlight{0};
    std::atomic<int> mostInFlight{0};
    constexpr int kInserts = 20;
    const auto inserter = [&]() {
        for (int i = 0; i < kInserts; ++i)
        {
            exec.execute([&, i](QSqlDatabase& db) {
                const int now = ++inFlight;
                int seen = mostInFlight.load();
                while (now > seen && !mostInFlight.compare_exchange_weak(seen, now))
                {
                }
                QSqlQuery q(db);
                q.prepare(QStringLiteral("INSERT INTO t(val) VALUES(?)"));
                q.addBindValue(i);
                EXPECT_TRUE(q.exec());
                ++insertsDone;
                --inFlight;
            });
        }
    };
    std::thread first(inserter);
    std::thread second(inserter);
    first.join();
    second.join();
    EXPECT_EQ(insertsDone.load(), kInserts * 2);
    EXPECT_EQ(mostInFlight.load(), 1);

    int rowCount = 0;
    exec.execute([&](QSqlDatabase& db) {
        QSqlQuery q(db);
        EXPECT_TRUE(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")));
        EXPECT_TRUE(q.next());
        rowCount = q.value(0).toInt();
    });
    EXPECT_EQ(rowCount, kInserts * 2);
}

// Every request runs on the executor's own thread, the same one each time, never the caller's.
TEST_F(PersistenceDbExecutorTest, UsesSingleThread)
{
    PersistenceDbExecutor exec(m_dbPath, QStringLiteral("test_singlethread"));
    ASSERT_TRUE(exec.isOpen());
    std::thread::id firstId;
    std::thread::id secondId;
    exec.execute([&](QSqlDatabase&) { firstId = std::this_thread::get_id(); });
    exec.execute([&](QSqlDatabase&) { secondId = std::this_thread::get_id(); });
    EXPECT_EQ(firstId, secondId);
    EXPECT_NE(firstId, std::this_thread::get_id());
}

// A shutdown runs every request queued before it, and a request after it is refused.
TEST_F(PersistenceDbExecutorTest, ShutdownFlushesQueue)
{
    PersistenceDbExecutor exec(m_dbPath, QStringLiteral("test_shutdown"));
    ASSERT_TRUE(exec.isOpen());
    exec.execute([](QSqlDatabase& db) {
        QSqlQuery q(db);
        EXPECT_TRUE(q.exec(QStringLiteral("CREATE TABLE t(val INTEGER)")));
    });

    std::atomic<int> executed{0};
    constexpr int kInserts = 10;
    for (int i = 0; i < kInserts; ++i)
    {
        exec.execute([&, i](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("INSERT INTO t(val) VALUES(?)"));
            q.addBindValue(i);
            q.exec();
            ++executed;
        });
    }
    exec.shutdown();
    EXPECT_EQ(executed.load(), kInserts);
    EXPECT_THROW(exec.execute([](QSqlDatabase&) {}), std::runtime_error);
}

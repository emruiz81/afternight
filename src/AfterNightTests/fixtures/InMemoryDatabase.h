// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/PersistenceDbExecutor.h"

#include <atomic>
#include <memory>

#include <QString>

/**
 * @brief In-memory SQLite database for unit tests.
 *
 * Wraps PersistenceDbExecutor with an SQLite :memory: connection so tests
 * do not touch the file system. Each instance receives a unique connection
 * name, making it safe to create multiple instances in the same process.
 *
 * Example:
 * @code
 *   InMemoryDatabase db;
 *   ASSERT_TRUE(db.isOpen());
 *   GlobalSettingsPersistence repo(db.executor());
 *   ASSERT_TRUE(repo.initializeTable());
 * @endcode
 */
class InMemoryDatabase
{
public:
    InMemoryDatabase()
        : m_executor(std::make_unique<PersistenceDbExecutor>(QStringLiteral(":memory:"), makeConnectionName()))
    {
    }

    ~InMemoryDatabase() = default;

    InMemoryDatabase(const InMemoryDatabase&) = delete;
    InMemoryDatabase& operator=(const InMemoryDatabase&) = delete;

    [[nodiscard]] PersistenceDbExecutor& executor() noexcept
    {
        return *m_executor;
    }
    [[nodiscard]] bool isOpen() const noexcept
    {
        return m_executor->isOpen();
    }

private:
    static QString makeConnectionName()
    {
        static std::atomic<int> s_counter{0};
        return QStringLiteral("inmemory_%1").arg(s_counter.fetch_add(1, std::memory_order_relaxed));
    }

    std::unique_ptr<PersistenceDbExecutor> m_executor;
};

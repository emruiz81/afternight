// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtSql/QSqlDatabase>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

/**
 * @brief Serialises all SQL work for a single SQLite database onto one dedicated thread.
 *
 * QSqlDatabase connections are thread-affine: they can only be used from the thread
 * that created them. PersistenceDbExecutor opens the connection on a private
 * background thread and routes every SQL call through a blocking task queue.
 * Callers post work via execute() and block until the result is available.
 *
 * Usage:
 *   PersistenceDbExecutor exec("path/to/db.sqlite", "my_connection");
 *   exec.execute([](QSqlDatabase& db) { QSqlQuery q(db); q.exec("SELECT 1"); });
 */
class AFTERNIGHTCONTROLS_API PersistenceDbExecutor
{
public:
    explicit PersistenceDbExecutor(const QString& dbPath, const QString& connectionName);
    ~PersistenceDbExecutor();

    // Non-copyable, non-movable (owns a thread and a DB connection).
    PersistenceDbExecutor(const PersistenceDbExecutor&) = delete;
    PersistenceDbExecutor& operator=(const PersistenceDbExecutor&) = delete;
    PersistenceDbExecutor(PersistenceDbExecutor&&) = delete;
    PersistenceDbExecutor& operator=(PersistenceDbExecutor&&) = delete;

    /**
     * @brief Synchronously execute @p fn on the dedicated DB thread.
     *
     * Blocks the calling thread until @p fn has finished. All reads and writes to
     * the underlying QSqlDatabase MUST go through this method.
     *
     * @throws std::runtime_error if the executor has already been shut down.
     */
    void execute(std::function<void(QSqlDatabase&)> fn);

    /**
     * @brief Drain any pending work and stop the background thread.
     *
     * Pending tasks already in the queue are completed before the thread exits.
     * New calls to execute() after shutdown() throw std::runtime_error.
     */
    void shutdown();

    [[nodiscard]] bool isOpen() const noexcept;

    [[nodiscard]] QString connectionName() const noexcept;

private:
    void run();

    QString m_dbPath;
    QString m_connectionName;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_queue;
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_open{false};
    std::promise<bool> m_ready_promise;
    std::shared_future<bool> m_ready_future;

    // Owned exclusively by m_thread after run() initialises it.
    QSqlDatabase m_db;
};

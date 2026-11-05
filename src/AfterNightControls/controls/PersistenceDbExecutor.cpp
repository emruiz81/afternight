// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PersistenceDbExecutor.h"

#include <AfterNightPlatform/Logging.h>

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <stdexcept>

PersistenceDbExecutor::PersistenceDbExecutor(const QString& dbPath, const QString& connectionName)
    : m_dbPath(dbPath), m_connectionName(connectionName), m_ready_future(m_ready_promise.get_future())
{
    m_thread = std::thread([this]() { run(); });
    // Block until the DB thread has finished opening (or failing to open) the database.
    // This guarantees isOpen() returns a reliable value immediately after construction.
    m_ready_future.wait();
}

PersistenceDbExecutor::~PersistenceDbExecutor()
{
    shutdown();
}

void PersistenceDbExecutor::execute(std::function<void(QSqlDatabase&)> fn)
{
    // Share the promise so the capturing lambda is copyable (required by std::function).
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> future = done->get_future();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Check shutdown *inside* the lock to avoid a TOCTOU race where shutdown()
        // sets the flag and joins *between* our relaxed check and our enqueue.
        if (m_shutdown.load(std::memory_order_relaxed))
        {
            throw std::runtime_error("PersistenceDbExecutor: execute() called after shutdown");
        }
        m_queue.push([this, fn = std::move(fn), done]() mutable {
            try
            {
                fn(m_db);
                done->set_value();
            }
            catch (...)
            {
                done->set_exception(std::current_exception());
            }
        });
    }
    m_cv.notify_one();
    future.get(); // re-throws any exception propagated from fn
}

void PersistenceDbExecutor::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown.load(std::memory_order_relaxed))
            return; // already shut down
        m_shutdown.store(true, std::memory_order_relaxed);
    }
    m_cv.notify_one();
    if (m_thread.joinable())
        m_thread.join();
}

bool PersistenceDbExecutor::isOpen() const noexcept
{
    return m_open.load(std::memory_order_relaxed);
}

QString PersistenceDbExecutor::connectionName() const noexcept
{
    return m_connectionName;
}

void PersistenceDbExecutor::run()
{
    // Open the database on this dedicated thread.
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open())
    {
        LOG_ERROR("PersistenceDbExecutor: failed to open '{}': {}", m_dbPath.toUtf8().toStdString(),
                  m_db.lastError().text().toUtf8().toStdString());
        m_ready_promise.set_value(false);
    }
    else
    {
        m_open.store(true, std::memory_order_release);
        LOG_DEBUG("PersistenceDbExecutor: opened '{}' on dedicated thread", m_dbPath.toUtf8().toStdString());
        m_ready_promise.set_value(true);
    }

    // Process tasks until shutdown AND the queue is drained.
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_shutdown.load(std::memory_order_relaxed); });

        // Drain all pending work even when shutting down.
        while (!m_queue.empty())
        {
            auto task = std::move(m_queue.front());
            m_queue.pop();
            lock.unlock();
            task();
            lock.lock();
        }

        if (m_shutdown.load(std::memory_order_relaxed))
            break;
    }

    m_open.store(false, std::memory_order_relaxed);
    m_db.close();
    m_db = QSqlDatabase(); // release the shared data before removing the connection
    QSqlDatabase::removeDatabase(m_connectionName);
}

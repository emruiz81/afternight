// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/core/DatabaseUtils.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace core
{
[[nodiscard]] inline bool executeSqliteStatement(QSqlQuery& query,
                                                 WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (counters != nullptr)
    {
        counters->recordSqliteStatementExecution();
    }
    return query.exec();
}

[[nodiscard]] inline bool executeSqliteStatement(QSqlQuery& query, const QString& statement,
                                                 WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (counters != nullptr)
    {
        counters->recordSqliteStatementExecution();
    }
    return query.exec(statement);
}

[[nodiscard]] inline bool beginSqliteTransaction(QSqlDatabase& db,
                                                 WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (!db.transaction())
    {
        return false;
    }
    if (counters != nullptr)
    {
        counters->recordSqliteTransactionStarted();
    }
    return true;
}

[[nodiscard]] inline bool commitSqliteTransaction(QSqlDatabase& db,
                                                  WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (!db.commit())
    {
        return false;
    }
    if (counters != nullptr)
    {
        counters->recordSqliteTransactionCommitted();
    }
    return true;
}

[[nodiscard]] inline bool rollbackSqliteTransaction(QSqlDatabase& db,
                                                    WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (!db.rollback())
    {
        return false;
    }
    if (counters != nullptr)
    {
        counters->recordSqliteTransactionRolledBack();
    }
    return true;
}

class SqliteConnection
{
public:
    SqliteConnection(QString databasePath, QString connectionPrefix,
                     WorkflowExecutionCounterAccumulator* counters = nullptr)
        : m_databasePath(std::move(databasePath)), m_connectionName(nextConnectionName(std::move(connectionPrefix))),
          m_counters(counters)
    {
    }

    ~SqliteConnection()
    {
        close();
    }

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    [[nodiscard]] bool open(QString* error = nullptr)
    {
        return openImpl(false, error);
    }

    /** Open an existing database without creating or modifying it. */
    [[nodiscard]] bool openReadOnly(QString* error = nullptr)
    {
        return openImpl(true, error);
    }

private:
    [[nodiscard]] bool openImpl(const bool read_only, QString* error)
    {
        if (isOpen())
        {
            return true;
        }

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_databasePath);
        if (read_only)
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open())
        {
            if (error)
            {
                *error = db.lastError().text();
            }

            // Ensure no broken connection lingers.
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(m_connectionName);
            return false;
        }

        m_db = db;
        return true;
    }

public:
    void close()
    {
        if (!m_db.isValid())
        {
            return;
        }

        m_db.close();

        const QString name = m_connectionName;
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }

    [[nodiscard]] bool isOpen() const
    {
        return m_db.isValid() && m_db.isOpen();
    }

    [[nodiscard]] QSqlDatabase& db()
    {
        return m_db;
    }
    [[nodiscard]] const QSqlDatabase& db() const
    {
        return m_db;
    }

    [[nodiscard]] const QString& connectionName() const
    {
        return m_connectionName;
    }

    [[nodiscard]] bool execute(QSqlQuery& query) const
    {
        return executeSqliteStatement(query, m_counters);
    }

    [[nodiscard]] bool execute(QSqlQuery& query, const QString& statement) const
    {
        return executeSqliteStatement(query, statement, m_counters);
    }

    [[nodiscard]] bool beginTransaction()
    {
        return beginSqliteTransaction(m_db, m_counters);
    }

    [[nodiscard]] bool commitTransaction()
    {
        return commitSqliteTransaction(m_db, m_counters);
    }

    [[nodiscard]] bool rollbackTransaction()
    {
        return rollbackSqliteTransaction(m_db, m_counters);
    }

    [[nodiscard]] WorkflowExecutionCounterAccumulator* counterAccumulator() const noexcept
    {
        return m_counters;
    }

private:
    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_db;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
};
} // namespace core

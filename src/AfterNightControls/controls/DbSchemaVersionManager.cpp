// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "DbSchemaVersionManager.h"
#include "PersistenceDbExecutor.h"

#include <AfterNightPlatform/Logging.h>

#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QString>

#include <utility>

DbSchemaVersionManager::DbSchemaVersionManager(PersistenceDbExecutor& executor, const int format, CreateFn create)
    : m_executor(executor), m_format(format), m_create(std::move(create))
{
}

DbSchemaVersionManager::Result DbSchemaVersionManager::initialize()
{
    Result result;
    m_executor.execute([this, &result](QSqlDatabase& db) {
        // What the database holds is read before anything is written, so one of another format is never touched.
        QSqlQuery tables(db);
        if (!tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE "
                                        "'sqlite_%'")))
        {
            LOG_ERROR("DbSchemaVersionManager: the database's tables could not be read: {}",
                      tables.lastError().text().toUtf8().toStdString());
            return;
        }
        bool holdsTables = false;
        bool statesFormat = false;
        while (tables.next())
        {
            holdsTables = true;
            statesFormat = statesFormat || tables.value(0).toString() == QStringLiteral("migrations");
        }

        if (!holdsTables)
        {
            result.foundFormat = m_format;
            result.outcome = create(db) ? Outcome::Created : Outcome::Failed;
            return;
        }

        QSqlQuery version(db);
        if (!statesFormat || !version.exec(QStringLiteral("SELECT version FROM migrations LIMIT 1")) || !version.next())
        {
            result.outcome = Outcome::OtherFormat;
            return;
        }
        result.foundFormat = version.value(0).toInt();
        result.outcome = result.foundFormat == m_format ? Outcome::Current : Outcome::OtherFormat;
    });
    return result;
}

bool DbSchemaVersionManager::create(QSqlDatabase& db) const
{
    if (!db.transaction())
    {
        LOG_ERROR("DbSchemaVersionManager: cannot begin the transaction that makes the database at format {}",
                  m_format);
        return false;
    }

    bool made = false;
    try
    {
        made = m_create && m_create(db);
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR("DbSchemaVersionManager: making the database at format {} threw: {}", m_format, ex.what());
    }
    catch (...)
    {
        LOG_ERROR("DbSchemaVersionManager: making the database at format {} threw an unknown exception", m_format);
    }

    if (made)
    {
        QSqlQuery state(db);
        made = state.exec(QStringLiteral("CREATE TABLE migrations (version INTEGER NOT NULL DEFAULT 0)"));
        if (made)
        {
            state.prepare(QStringLiteral("INSERT INTO migrations (version) VALUES (?)"));
            state.addBindValue(m_format);
            made = state.exec();
        }
        if (!made)
        {
            LOG_ERROR("DbSchemaVersionManager: the database's format could not be stated: {}",
                      state.lastError().text().toUtf8().toStdString());
        }
    }

    if (!made)
    {
        db.rollback();
        LOG_ERROR("DbSchemaVersionManager: the database could not be made at format {}; nothing was kept", m_format);
        return false;
    }
    if (!db.commit())
    {
        LOG_ERROR("DbSchemaVersionManager: the commit that makes the database at format {} failed: {}", m_format,
                  db.lastError().text().toUtf8().toStdString());
        return false;
    }
    LOG_DEBUG("DbSchemaVersionManager: made the database at format {}", m_format);
    return true;
}

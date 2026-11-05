// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProcessSettingsPersistence.h"
#include "PersistenceDbExecutor.h"

#include <QtSql/QSqlError>

ProcessSettingsPersistence::ProcessSettingsPersistence(PersistenceDbExecutor& executor)
    : PersistenceBaseRepository(executor)
{
}

bool ProcessSettingsPersistence::initializeTable()
{
    bool result = false;
    m_executor.execute([this, &result](QSqlDatabase& db) {
        m_PreparedCreateTableQuery = QSqlQuery(db);
        m_PreparedUpsertQuery = QSqlQuery(db);
        m_PreparedSelectQuery = QSqlQuery(db);
        m_PreparedDeleteQuery = QSqlQuery(db);
        m_PreparedClearQuery = QSqlQuery(db);

        if (!m_PreparedCreateTableQuery.prepare(
                "CREATE TABLE IF NOT EXISTS process_settings "
                "(process_name TEXT PRIMARY KEY, window_state INTEGER, window_width INTEGER, "
                "window_height INTEGER, json_settings TEXT)"))
        {
            LOG_ERROR("Failed to create process_settings table: {}",
                      m_PreparedCreateTableQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        if (!m_PreparedCreateTableQuery.exec())
        {
            LOG_ERROR("Failed to create process_settings table: {}",
                      m_PreparedCreateTableQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        if (!m_PreparedUpsertQuery.prepare(R"(
                INSERT INTO process_settings (process_name, window_state, window_width, window_height, json_settings)
                VALUES (?, ?, ?, ?, ?)
                ON CONFLICT(process_name) DO UPDATE SET
                    window_state=excluded.window_state,
                    window_width=excluded.window_width,
                    window_height=excluded.window_height,
                    json_settings=excluded.json_settings
            )"))
        {
            LOG_ERROR("Failed to initialize process_settings table: {}",
                      m_PreparedUpsertQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        if (!m_PreparedSelectQuery.prepare("SELECT window_state, window_width, window_height, json_settings "
                                           "FROM process_settings WHERE process_name = ?"))
        {
            LOG_ERROR("Failed to initialize process_settings table: {}",
                      m_PreparedSelectQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        if (!m_PreparedDeleteQuery.prepare("DELETE FROM process_settings WHERE process_name = ?"))
        {
            LOG_ERROR("Failed to initialize process_settings table: {}",
                      m_PreparedDeleteQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        if (!m_PreparedClearQuery.prepare("DELETE FROM process_settings"))
        {
            LOG_ERROR("Failed to initialize process_settings table: {}",
                      m_PreparedClearQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        result = true;
    });

    if (result)
        m_queriesInitialized = true;

    return result;
}

bool ProcessSettingsPersistence::upsert(const QString& processName, const int wstate, const int width, const int height,
                                        const QString& json_settings)
{
    bool result = false;
    m_executor.execute([this, &processName, wstate, width, height, &json_settings, &result](QSqlDatabase&) {
        m_PreparedUpsertQuery.bindValue(0, processName);
        m_PreparedUpsertQuery.bindValue(1, wstate);
        m_PreparedUpsertQuery.bindValue(2, width);
        m_PreparedUpsertQuery.bindValue(3, height);
        m_PreparedUpsertQuery.bindValue(4, json_settings);

        if (!m_PreparedUpsertQuery.exec())
        {
            LOG_WARNING("Failed to upsert settings for process {}: {}", processName.toUtf8().toStdString(),
                        m_PreparedUpsertQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        result = true;
    });
    return result;
}

bool ProcessSettingsPersistence::select(const QString& processName, int& win_state, int& width, int& height,
                                        QString& json_settings)
{
    bool result = false;
    m_executor.execute([this, &processName, &win_state, &width, &height, &json_settings, &result](QSqlDatabase&) {
        m_PreparedSelectQuery.bindValue(0, processName);
        if (!m_PreparedSelectQuery.exec())
        {
            LOG_WARNING("Failed to select {}: {}", processName.toUtf8().toStdString(),
                        m_PreparedSelectQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        if (m_PreparedSelectQuery.next())
        {
            win_state = m_PreparedSelectQuery.value(0).toInt();
            width = m_PreparedSelectQuery.value(1).toInt();
            height = m_PreparedSelectQuery.value(2).toInt();
            json_settings = m_PreparedSelectQuery.value(3).toString();
        }
        else
        {
            win_state = 0;
            width = 0;
            height = 0;
            json_settings = QString();
        }

        result = true;
    });
    return result;
}

bool ProcessSettingsPersistence::remove(const QString& processName)
{
    bool result = false;
    m_executor.execute([this, &processName, &result](QSqlDatabase&) {
        m_PreparedDeleteQuery.bindValue(0, processName);
        if (!m_PreparedDeleteQuery.exec())
        {
            LOG_WARNING("Failed to remove process settings for process {}: {}", processName.toUtf8().toStdString(),
                        m_PreparedDeleteQuery.lastError().text().toUtf8().toStdString());
            return;
        }
        result = true;
    });
    return result;
}

bool ProcessSettingsPersistence::clearAll()
{
    bool result = false;
    m_executor.execute([this, &result](QSqlDatabase&) {
        if (!m_PreparedClearQuery.exec())
        {
            LOG_ERROR("Failed to clear the process_settings table: {}",
                      m_PreparedClearQuery.lastError().text().toUtf8().toStdString());
            return;
        }
        result = true;
    });
    return result;
}

ProcessSettingsPersistence::~ProcessSettingsPersistence()
{
    destroyQueries();
}

void ProcessSettingsPersistence::destroyQueries()
{
    if (!m_queriesInitialized)
        return;

    // Destroy QSqlQuery objects on the DB thread that created them,
    // preventing Qt "connection is still in use" warnings.
    try
    {
        m_executor.execute([this](QSqlDatabase&) {
            m_PreparedCreateTableQuery = QSqlQuery();
            m_PreparedUpsertQuery = QSqlQuery();
            m_PreparedSelectQuery = QSqlQuery();
            m_PreparedDeleteQuery = QSqlQuery();
            m_PreparedClearQuery = QSqlQuery();
        });
    }
    catch (const std::exception& ex)
    {
        LOG_WARNING("ProcessSettingsPersistence: destroyQueries failed (executor shut down?): {}", ex.what());
    }
    m_queriesInitialized = false;
}

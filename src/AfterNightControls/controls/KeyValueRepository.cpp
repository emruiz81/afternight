// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "KeyValueRepository.h"
#include "PersistenceDbExecutor.h"

#include <AfterNightPlatform/Logging.h>

#include <QtSql/QSqlError>

KeyValueRepository::KeyValueRepository(PersistenceDbExecutor& executor, TableSchema schema)
    : PersistenceBaseRepository(executor), m_schema(schema)
{
}

bool KeyValueRepository::initializeTable()
{
    bool result = false;
    m_executor.execute([this, &result](QSqlDatabase& db) {
        // --- CREATE TABLE ---
        QSqlQuery createQuery(db);
        const QString createSql = QString("CREATE TABLE IF NOT EXISTS %1 (%2 TEXT PRIMARY KEY, %3 TEXT)")
                                      .arg(m_schema.tableName, m_schema.keyColumn, m_schema.valueColumn);

        if (!createQuery.exec(createSql))
        {
            LOG_ERROR("Failed to create table '{}': {}", m_schema.tableName,
                      createQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        // --- UPSERT ---
        m_upsertQuery = QSqlQuery(db);
        const QString upsertSql = QString("INSERT INTO %1 (%2, %3) VALUES (?, ?) "
                                          "ON CONFLICT(%2) DO UPDATE SET %3=excluded.%3")
                                      .arg(m_schema.tableName, m_schema.keyColumn, m_schema.valueColumn);

        if (!m_upsertQuery.prepare(upsertSql))
        {
            LOG_ERROR("Failed to prepare upsert for '{}': {}", m_schema.tableName,
                      m_upsertQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        // --- SELECT ---
        m_selectQuery = QSqlQuery(db);
        const QString selectSql =
            QString("SELECT %1 FROM %2 WHERE %3 = ?").arg(m_schema.valueColumn, m_schema.tableName, m_schema.keyColumn);

        if (!m_selectQuery.prepare(selectSql))
        {
            LOG_ERROR("Failed to prepare select for '{}': {}", m_schema.tableName,
                      m_selectQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        // --- SELECT KEYS WITH PREFIX ---
        m_select_keys_with_prefix_query = QSqlQuery(db);
        const QString select_keys_with_prefix_sql =
            QString("SELECT %1 FROM %2 WHERE %1 LIKE ? ORDER BY %1").arg(m_schema.keyColumn, m_schema.tableName);

        if (!m_select_keys_with_prefix_query.prepare(select_keys_with_prefix_sql))
        {
            LOG_ERROR("Failed to prepare prefix select for '{}': {}", m_schema.tableName,
                      m_select_keys_with_prefix_query.lastError().text().toUtf8().toStdString());
            return;
        }

        // --- DELETE ---
        m_deleteQuery = QSqlQuery(db);
        const QString deleteSql = QString("DELETE FROM %1 WHERE %2 = ?").arg(m_schema.tableName, m_schema.keyColumn);

        if (!m_deleteQuery.prepare(deleteSql))
        {
            LOG_ERROR("Failed to prepare delete for '{}': {}", m_schema.tableName,
                      m_deleteQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        // --- CLEAR ---
        m_clearQuery = QSqlQuery(db);
        const QString clearSql = QString("DELETE FROM %1").arg(m_schema.tableName);

        if (!m_clearQuery.prepare(clearSql))
        {
            LOG_ERROR("Failed to prepare clear for '{}': {}", m_schema.tableName,
                      m_clearQuery.lastError().text().toUtf8().toStdString());
            return;
        }

        result = true;
    });

    if (result)
        m_queriesInitialized = true;

    return result;
}

bool KeyValueRepository::upsert(const QString& key, const QString& value)
{
    bool result = false;
    m_executor.execute([this, &key, &value, &result](QSqlDatabase&) {
        m_upsertQuery.bindValue(0, key);
        m_upsertQuery.bindValue(1, value);
        if (!m_upsertQuery.exec())
        {
            LOG_WARNING("Failed to upsert '{}' in '{}': {}", key.toUtf8().toStdString(), m_schema.tableName,
                        m_upsertQuery.lastError().text().toUtf8().toStdString());
            return;
        }
        result = true;
    });
    return result;
}

QString KeyValueRepository::select(const QString& key, const QString& defaultValue)
{
    QString result = defaultValue;
    m_executor.execute([this, &key, &result](QSqlDatabase&) {
        m_selectQuery.bindValue(0, key);
        if (!m_selectQuery.exec())
        {
            LOG_WARNING("Failed to select '{}' from '{}': {}", key.toUtf8().toStdString(), m_schema.tableName,
                        m_selectQuery.lastError().text().toUtf8().toStdString());
            return;
        }
        if (m_selectQuery.next())
            result = m_selectQuery.value(0).toString();
    });
    return result;
}

std::optional<QString> KeyValueRepository::selectOptional(const QString& key)
{
    std::optional<QString> result;
    m_executor.execute([this, &key, &result](QSqlDatabase&) {
        m_selectQuery.bindValue(0, key);
        if (!m_selectQuery.exec())
        {
            LOG_WARNING("Failed to select '{}' from '{}': {}", key.toUtf8().toStdString(), m_schema.tableName,
                        m_selectQuery.lastError().text().toUtf8().toStdString());
            return;
        }
        if (m_selectQuery.next())
        {
            result = m_selectQuery.value(0).toString();
        }
    });
    return result;
}

QStringList KeyValueRepository::selectKeysWithPrefix(const QString& prefix)
{
    QStringList results;
    m_executor.execute([this, &prefix, &results](QSqlDatabase&) {
        m_select_keys_with_prefix_query.bindValue(0, prefix + QStringLiteral("%"));
        if (!m_select_keys_with_prefix_query.exec())
        {
            LOG_WARNING("Failed to select keys with prefix '{}' from '{}': {}", prefix.toUtf8().toStdString(),
                        m_schema.tableName, m_select_keys_with_prefix_query.lastError().text().toUtf8().toStdString());
            return;
        }

        while (m_select_keys_with_prefix_query.next())
        {
            results.push_back(m_select_keys_with_prefix_query.value(0).toString());
        }
    });
    return results;
}

bool KeyValueRepository::remove(const QString& key)
{
    bool result = false;
    m_executor.execute([this, &key, &result](QSqlDatabase&) {
        m_deleteQuery.bindValue(0, key);
        if (!m_deleteQuery.exec())
        {
            LOG_WARNING("Failed to remove '{}' from '{}': {}", key.toUtf8().toStdString(), m_schema.tableName,
                        m_deleteQuery.lastError().text().toUtf8().toStdString());
            return;
        }
        result = true;
    });
    return result;
}

bool KeyValueRepository::clearAll()
{
    bool result = false;
    m_executor.execute([this, &result](QSqlDatabase&) {
        if (!m_clearQuery.exec())
        {
            LOG_ERROR("Failed to clear table '{}': {}", m_schema.tableName,
                      m_clearQuery.lastError().text().toUtf8().toStdString());
            return;
        }
        result = true;
    });
    return result;
}

KeyValueRepository::~KeyValueRepository()
{
    destroyQueries();
}

void KeyValueRepository::destroyQueries()
{
    if (!m_queriesInitialized)
        return;

    try
    {
        m_executor.execute([this](QSqlDatabase&) {
            m_upsertQuery = QSqlQuery();
            m_selectQuery = QSqlQuery();
            m_select_keys_with_prefix_query = QSqlQuery();
            m_deleteQuery = QSqlQuery();
            m_clearQuery = QSqlQuery();
        });
    }
    catch (const std::exception& ex)
    {
        LOG_WARNING("KeyValueRepository: destroyQueries failed (executor shut down?): {}", ex.what());
    }

    m_queriesInitialized = false;
}

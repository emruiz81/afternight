// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "PersistenceBaseRepository.h"

#include <optional>
#include <QStringList>
#include <QtSql/QSqlQuery>

class PersistenceDbExecutor;

/**
 * @brief Generic key-value persistence repository for SQLite tables with a text
 *        primary-key column and a text value column.
 *
 * Centralises the boilerplate shared by GlobalSettingsPersistence,
 * FolderTrackerPersistence, and ThumbnailPersistence.  Concrete subclasses
 * only need to forward the table schema to the constructor.
 *
 * Usage:
 * @code
 *   class MyRepo final : public KeyValueRepository {
 *   public:
 *       explicit MyRepo(PersistenceDbExecutor& ex)
 *           : KeyValueRepository(ex, {"my_table", "my_key", "my_value"}) {}
 *   };
 * @endcode
 */
class AFTERNIGHTCONTROLS_API KeyValueRepository : public PersistenceBaseRepository
{
public:
    struct TableSchema
    {
        const char* tableName;
        const char* keyColumn;
        const char* valueColumn;
    };

    explicit KeyValueRepository(PersistenceDbExecutor& executor, TableSchema schema);
    ~KeyValueRepository() override;

    KeyValueRepository(const KeyValueRepository&) = delete;
    KeyValueRepository& operator=(const KeyValueRepository&) = delete;
    KeyValueRepository(KeyValueRepository&&) = delete;
    KeyValueRepository& operator=(KeyValueRepository&&) = delete;

    bool initializeTable();

    bool upsert(const QString& key, const QString& value);

    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {});
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key);
    [[nodiscard]] QStringList selectKeysWithPrefix(const QString& prefix);

    bool remove(const QString& key);

    bool clearAll();

private:
    void destroyQueries();

    TableSchema m_schema;
    QSqlQuery m_upsertQuery;
    QSqlQuery m_selectQuery;
    QSqlQuery m_select_keys_with_prefix_query;
    QSqlQuery m_deleteQuery;
    QSqlQuery m_clearQuery;
    bool m_queriesInitialized = false;
};

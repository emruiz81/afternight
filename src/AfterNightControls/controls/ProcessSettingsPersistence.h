// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "PersistenceBaseRepository.h"

#include <QtSql/QSqlQuery>
#include <QString>

class AFTERNIGHTCONTROLS_API ProcessSettingsPersistence : public PersistenceBaseRepository
{
public:
    explicit ProcessSettingsPersistence(PersistenceDbExecutor& executor);
    ~ProcessSettingsPersistence() override;

    bool initializeTable();

    bool upsert(const QString& processName, int win_state, int width, int height, const QString& json_settings);

    [[nodiscard]] bool select(const QString& processName, int& win_state, int& width, int& height,
                              QString& json_settings);

    bool remove(const QString& processName);

    bool clearAll();

private:
    void destroyQueries();

    QSqlQuery m_PreparedCreateTableQuery;
    QSqlQuery m_PreparedUpsertQuery;
    QSqlQuery m_PreparedSelectQuery;
    QSqlQuery m_PreparedDeleteQuery;
    QSqlQuery m_PreparedClearQuery;
    bool m_queriesInitialized = false;
};

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>

#include <QtSql/QSqlDatabase>

class PersistenceDbExecutor;

/**
 * @brief The format a database held by a PersistenceDbExecutor states, and the one place such a database is made.
 *
 * A database states its format in a table of one row, `migrations (version)`. A database this version opens is either
 * new - it holds no table, and it is made at the format this version writes in one transaction, the format's tables
 * created and the format stated - or at that format, and is opened as it is. Any other database, of an older format or
 * a newer one, or one that holds tables but states no format, is another format's: it is neither read nor written,
 * and the caller sets it aside and opens a new one in its place.
 *
 * Usage:
 * @code
 *   DbSchemaVersionManager format(executor, 2, [](QSqlDatabase& db) {
 *       QSqlQuery q(db);
 *       return q.exec("CREATE TABLE my_table (id INTEGER PRIMARY KEY)");
 *   });
 *   const DbSchemaVersionManager::Result opened = format.initialize();
 *   if (opened.outcome == DbSchemaVersionManager::Outcome::OtherFormat)
 *       // set the database aside and open a new one
 * @endcode
 */
class AFTERNIGHTCONTROLS_API DbSchemaVersionManager
{
public:
    /// Creates the tables of the format in a new database; answers whether every table was made.
    using CreateFn = std::function<bool(QSqlDatabase&)>;

    /// What opening the database found.
    enum class Outcome
    {
        /// It held no table and was made at the format.
        Created,
        /// It was at the format, and was opened as it is.
        Current,
        /// It states another format, or none: nothing was read or written.
        OtherFormat,
        /// It could not be inspected or made.
        Failed
    };

    /// What opening the database found, and the format it states: zero for one that states none.
    struct Result
    {
        Outcome outcome = Outcome::Failed;
        int foundFormat = 0;
    };

    /// @param executor the database's executor.
    /// @param format the format this version writes.
    /// @param create creates the format's tables in a new database.
    DbSchemaVersionManager(PersistenceDbExecutor& executor, int format, CreateFn create);

    /// Inspects the database and, when it holds no table, makes it at the format.
    [[nodiscard]] Result initialize();

private:
    [[nodiscard]] bool create(QSqlDatabase& db) const;

    PersistenceDbExecutor& m_executor;
    int m_format;
    CreateFn m_create;
};

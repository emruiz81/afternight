// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file DatabaseSchema.h
 * @brief The integration session database's format: its tables and the format it states.
 */
#pragma once

#include <QStringList>

/**
 * @namespace persistence
 * @brief The integration session database's format.
 *
 * Provides:
 * - kSchemaVersion: the format a session database states and is written in
 * - createStatements(): the tables a new database is made with
 */
namespace persistence
{
/// The format a session database states in `schema_info`: each session a format 3 document (`SessionDocument.h`) -
/// every engine-facing choice by its stable key, every image point in engine pixel coordinates.
inline constexpr int kSchemaVersion = 3;

inline QStringList createStatements()
{
    return {QStringLiteral("CREATE TABLE IF NOT EXISTS schema_info (version INTEGER NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS sessions ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "name TEXT NOT NULL UNIQUE,"
                           "data_json TEXT NOT NULL,"
                           "updated_at INTEGER NOT NULL)")};
}
} // namespace persistence

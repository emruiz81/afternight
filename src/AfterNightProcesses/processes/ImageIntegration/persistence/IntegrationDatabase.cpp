// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "IntegrationDatabase.h"

#include "DatabaseSchema.h"
#include "SessionDocument.h"
#include "app/ApplicationStorage.h"
#include "processes/ImageIntegration/core/DatabaseUtils.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"

#include <AfterNightPlatform/Logging.h>

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace
{

[[nodiscard]] QString sessionContext(const QString& name)
{
    return QStringLiteral("session '%1'").arg(name);
}

} // namespace

IntegrationDatabase::IntegrationDatabase(const QString& databasePath, WorkflowExecutionCounterAccumulator* counters)
    : m_databasePath(databasePath), m_connectionName(core::nextConnectionName(QStringLiteral("IntegrationDatabase"))),
      m_counters(counters)
{
}

IntegrationDatabase::~IntegrationDatabase()
{
    close();
    if (!m_connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool IntegrationDatabase::open()
{
    if (m_db && m_db->isOpen())
    {
        return true;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_databasePath);
    if (!db.open())
    {
        m_lastError = db.lastError().text();
        return false;
    }

    m_db = std::make_unique<QSqlDatabase>(db);
    return true;
}

void IntegrationDatabase::close()
{
    if (!m_db)
    {
        return;
    }

    m_db->close();
    m_db.reset();
    m_writeAheadLogging = false;
}

bool IntegrationDatabase::initialize()
{
    if (!open())
    {
        return false;
    }

    if (!ensureSchema())
    {
        return false;
    }
    // Only a database this version reads is switched to write-ahead logging, once per connection; a refused one is
    // never written.
    if (!m_writeAheadLogging)
    {
        QSqlQuery pragma(*m_db);
        m_writeAheadLogging =
            core::executeSqliteStatement(pragma, QStringLiteral("PRAGMA journal_mode=WAL"), m_counters);
    }
    return true;
}

DatabaseResult IntegrationDatabase::saveSessionResult(const QString& name, const QJsonObject& data)
{
    if (!initialize())
    {
        return {false, m_lastError.isEmpty() ? QStringLiteral("Database initialization failed") : m_lastError};
    }

    const ImageIntegration::SessionDocumentResult document =
        ImageIntegration::writeSessionDocument(data, sessionContext(name));
    if (!document.ok())
    {
        m_lastError = document.refusal;
        return {false, m_lastError};
    }

    QSqlQuery query(*m_db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO sessions(name, data_json, updated_at) "
                                 "VALUES (:name, :data, :updated_at)"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":data"),
                    QString::fromUtf8(QJsonDocument(*document.document).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentSecsSinceEpoch());

    if (!core::executeSqliteStatement(query, m_counters))
    {
        m_lastError = query.lastError().text();
        return {false, m_lastError};
    }

    m_lastError.clear();
    return {true, QString()};
}

bool IntegrationDatabase::saveSession(const QString& name, const QJsonObject& data)
{
    return saveSessionResult(name, data).ok;
}

std::optional<QJsonObject> IntegrationDatabase::loadSession(const QString& name) const
{
    return loadSessionResult(name).model;
}

SessionLoadResult IntegrationDatabase::loadSessionResult(const QString& name) const
{
    SessionLoadResult result;
    if (!m_db || !m_db->isOpen())
    {
        return result;
    }

    QSqlQuery query(*m_db);
    query.prepare(QStringLiteral("SELECT data_json FROM sessions WHERE name = :name"));
    query.bindValue(QStringLiteral(":name"), name);

    if (!core::executeSqliteStatement(query, m_counters) || !query.next())
    {
        return result;
    }

    const QString data = query.value(0).toString();
    const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (doc.isNull() || !doc.isObject())
    {
        result.error = QStringLiteral("The %1 is not a document this version can read.").arg(sessionContext(name));
        return result;
    }

    const ImageIntegration::SessionDocumentResult read =
        ImageIntegration::readSessionDocument(doc.object(), sessionContext(name));
    if (!read.ok())
    {
        result.error = read.refusal;
        return result;
    }
    result.model = *read.document;
    return result;
}

bool IntegrationDatabase::deleteSession(const QString& name)
{
    if (!initialize())
    {
        return false;
    }

    QSqlQuery query(*m_db);
    query.prepare(QStringLiteral("DELETE FROM sessions WHERE name = :name"));
    query.bindValue(QStringLiteral(":name"), name);
    return core::executeSqliteStatement(query, m_counters);
}

QStringList IntegrationDatabase::listSessions() const
{
    QStringList sessions;
    if (!m_db || !m_db->isOpen())
    {
        return sessions;
    }

    QSqlQuery query(*m_db);
    if (!core::executeSqliteStatement(query, QStringLiteral("SELECT name FROM sessions ORDER BY name"), m_counters))
    {
        return sessions;
    }

    while (query.next())
    {
        sessions << query.value(0).toString();
    }

    return sessions;
}

QJsonObject IntegrationDatabase::exportToJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), persistence::kSchemaVersion);

    QJsonArray sessions;
    if (m_db && m_db->isOpen())
    {
        QSqlQuery query(*m_db);
        if (core::executeSqliteStatement(query, QStringLiteral("SELECT name, data_json FROM sessions ORDER BY name"),
                                         m_counters))
        {
            while (query.next())
            {
                const QString name = query.value(0).toString();
                const QString data = query.value(1).toString();
                const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());

                QJsonObject entry;
                entry.insert(QStringLiteral("name"), name);
                entry.insert(QStringLiteral("data"), doc.isObject() ? doc.object() : QJsonObject());
                sessions.append(entry);
            }
        }
    }

    root.insert(QStringLiteral("sessions"), sessions);
    return root;
}

bool IntegrationDatabase::importFromJson(const QJsonObject& json)
{
    if (!initialize())
    {
        return false;
    }

    const QJsonArray sessions = json.value(QStringLiteral("sessions")).toArray();
    for (const auto& entryValue : sessions)
    {
        const QJsonObject entry = entryValue.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QJsonObject data = entry.value(QStringLiteral("data")).toObject();
        if (name.isEmpty())
        {
            continue;
        }

        const ImageIntegration::SessionDocumentResult read =
            ImageIntegration::readSessionDocument(data, sessionContext(name));
        if (!read.ok())
        {
            m_lastError = read.refusal;
            return false;
        }
        if (!saveSession(name, *read.document))
        {
            return false;
        }
    }

    return true;
}

int IntegrationDatabase::schemaVersion() const
{
    if (!m_db || !m_db->isOpen())
    {
        return 0;
    }

    QSqlQuery query(*m_db);
    if (!core::executeSqliteStatement(query, QStringLiteral("SELECT version FROM schema_info LIMIT 1"), m_counters))
    {
        return 0;
    }

    if (!query.next())
    {
        return 0;
    }

    return query.value(0).toInt();
}

const QString& IntegrationDatabase::lastError() const noexcept
{
    return m_lastError;
}

std::optional<int> IntegrationDatabase::otherFormat() const noexcept
{
    return m_otherFormat;
}

QString IntegrationDatabase::setAside()
{
    if (!m_otherFormat.has_value())
    {
        return {};
    }
    close();
    QSqlDatabase::removeDatabase(m_connectionName);
    const QString path = an::ui::setAsideStore(m_databasePath, *m_otherFormat);
    if (path.isEmpty())
    {
        m_lastError = QStringLiteral("The session database '%1' could not be set aside, and was left as it is.")
                          .arg(QDir::toNativeSeparators(m_databasePath));
        LOG_WARNING("IntegrationDatabase: {}", m_lastError.toStdString());
        return {};
    }
    LOG_WARNING("IntegrationDatabase: the session database '{}' of format {} was set aside as '{}'",
                m_databasePath.toStdString(), *m_otherFormat, path.toStdString());
    m_otherFormat.reset();
    return path;
}

bool IntegrationDatabase::ensureSchema()
{
    // What the database holds is read before anything is written, so one of another format is never touched.
    m_otherFormat.reset();
    bool holdsTables = false;
    bool statesFormat = false;
    {
        QSqlQuery tables(*m_db);
        if (!core::executeSqliteStatement(tables,
                                          QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND "
                                                         "name NOT LIKE 'sqlite_%'"),
                                          m_counters))
        {
            m_lastError = tables.lastError().text();
            return false;
        }
        while (tables.next())
        {
            holdsTables = true;
            statesFormat = statesFormat || tables.value(0).toString() == QStringLiteral("schema_info");
        }
    }

    if (!holdsTables)
    {
        // A new database, made at the format in one transaction: its tables, then the format they are.
        if (!core::beginSqliteTransaction(*m_db, m_counters))
        {
            m_lastError = m_db->lastError().text();
            return false;
        }
        if (!execStatements(persistence::createStatements()))
        {
            (void)core::rollbackSqliteTransaction(*m_db, m_counters);
            return false;
        }
        QSqlQuery insert(*m_db);
        insert.prepare(QStringLiteral("INSERT INTO schema_info(version) VALUES (:version)"));
        insert.bindValue(QStringLiteral(":version"), persistence::kSchemaVersion);
        if (!core::executeSqliteStatement(insert, m_counters))
        {
            m_lastError = insert.lastError().text();
            (void)core::rollbackSqliteTransaction(*m_db, m_counters);
            return false;
        }
        if (!core::commitSqliteTransaction(*m_db, m_counters))
        {
            m_lastError = m_db->lastError().text();
            (void)core::rollbackSqliteTransaction(*m_db, m_counters);
            return false;
        }
        m_lastError.clear();
        return true;
    }

    int format = 0;
    if (statesFormat)
    {
        QSqlQuery query(*m_db);
        if (core::executeSqliteStatement(query, QStringLiteral("SELECT version FROM schema_info LIMIT 1"),
                                         m_counters) &&
            query.next())
        {
            format = query.value(0).toInt();
        }
    }
    if (format == persistence::kSchemaVersion)
    {
        m_lastError.clear();
        return true;
    }

    m_otherFormat = format;
    const QString path = QDir::toNativeSeparators(m_databasePath);
    m_lastError = format > 0 ? QStringLiteral("The session database '%1' was written in format %2; this version of "
                                              "AfterNight reads format %3.")
                                   .arg(path)
                                   .arg(format)
                                   .arg(persistence::kSchemaVersion)
                             : QStringLiteral("The session database '%1' states no format version; this version of "
                                              "AfterNight reads format %2.")
                                   .arg(path)
                                   .arg(persistence::kSchemaVersion);
    LOG_WARNING("IntegrationDatabase: {}", m_lastError.toStdString());
    return false;
}

bool IntegrationDatabase::execStatements(const QStringList& statements)
{
    for (const auto& statement : statements)
    {
        QSqlQuery query(*m_db);
        if (!core::executeSqliteStatement(query, statement, m_counters))
        {
            m_lastError = query.lastError().text();
            return false;
        }
    }

    return true;
}

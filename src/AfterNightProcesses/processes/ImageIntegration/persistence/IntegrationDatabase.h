// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file IntegrationDatabase.h
 * @brief SQLite database for integration session persistence.
 *
 * Provides durable storage for integration sessions, allowing users
 * to save, load, and resume work across application restarts.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class QSqlDatabase;

/**
 * @class IntegrationDatabase
 * @brief SQLite-backed storage for integration sessions.
 *
 * Stores each session as a format 3 document (`SessionDocument.h`) in a SQLite database at format 3. Supports:
 * - saveSession(): persist model state by name
 * - loadSession(): retrieve saved state
 * - listSessions(): enumerate available sessions
 * - deleteSession(): remove old sessions
 *
 * The database states its format (DatabaseSchema.h). A new one is made at format 3; one of another format - older,
 * newer, or holding tables but stating none - is refused with the format found and never read or written, and
 * setAside() moves it beside itself so a new one can be made in its place.
 *
 * Open/initialize precondition - the read and write paths are deliberately asymmetric:
 *
 * - The mutating entry points (saveSession(), saveSessionResult(), deleteSession(),
 *   importFromJson()) call initialize() themselves, so they open the connection and make a new
 *   database on demand. Calling them on a fresh object is fine.
 * - The const read accessors (loadSession(), listSessions(), exportToJson(), schemaVersion()) do
 *   NOT. They are const, initialize() mutates the connection, and a read has no business creating a
 *   database file as a side effect. On a connection that was never opened
 *   they fail soft, returning exactly what they return for an open database with no such data:
 *   std::nullopt, an empty list, an object with an empty "sessions" array, and version 0.
 *
 * Consequence: an empty read result does not distinguish "nothing saved" from "never opened".
 * Callers that need to tell those apart must call initialize() (or open()) first and check its
 * result.
 */
struct DatabaseResult
{
    bool ok = false;
    QString errorMessage;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return ok;
    }
};

/// What loading a session gave: the model's JSON, or why the stored session cannot be read.
struct SessionLoadResult
{
    /// The model's JSON, or nothing when there is no such session or it was refused.
    std::optional<QJsonObject> model;

    /// Why the stored session was refused; empty when it was read or absent.
    QString error;
};

class AFTERNIGHTPROCESSES_API IntegrationDatabase
{
public:
    explicit IntegrationDatabase(const QString& databasePath, WorkflowExecutionCounterAccumulator* counters = nullptr);
    ~IntegrationDatabase();

    bool open();
    void close();
    [[nodiscard]] bool initialize();

    // Writes auto-initialize; the const reads below require a prior initialize()/open() and fail
    // soft otherwise. See the class comment above.
    [[nodiscard]] DatabaseResult saveSessionResult(const QString& name, const QJsonObject& data);
    [[nodiscard]] bool saveSession(const QString& name, const QJsonObject& data);
    /// Requires a prior initialize()/open(); returns std::nullopt if the database was never opened or the session
    /// was refused.
    [[nodiscard]] std::optional<QJsonObject> loadSession(const QString& name) const;
    /// Requires a prior initialize()/open(); the session, or why it was refused.
    [[nodiscard]] SessionLoadResult loadSessionResult(const QString& name) const;
    [[nodiscard]] bool deleteSession(const QString& name);
    /// Requires a prior initialize()/open(); returns an empty list if the database was never opened.
    [[nodiscard]] QStringList listSessions() const;

    /// Requires a prior initialize()/open(); returns an empty "sessions" array if never opened. Each entry's "data" is
    /// the stored format 3 document.
    [[nodiscard]] QJsonObject exportToJson() const;
    /// Reads each entry's stored document strictly and saves it; a refused entry stops the import.
    [[nodiscard]] bool importFromJson(const QJsonObject& json);

    /// Requires a prior initialize()/open(); returns 0 if the database was never opened.
    [[nodiscard]] int schemaVersion() const;

    /// Why the last initialize, save or import failed; empty after one that succeeded.
    [[nodiscard]] const QString& lastError() const noexcept;

    /// The format the database states when the last initialize refused it as another format's - zero for one that
    /// states none - or nothing.
    [[nodiscard]] std::optional<int> otherFormat() const noexcept;

    /// Closes the database and sets it aside beside itself, under a name that states its format, so the next initialize
    /// makes a new one in its place; nothing is read, written or deleted.
    ///
    /// @return the path it was set aside at, or an empty string when it was not moved.
    [[nodiscard]] QString setAside();

private:
    [[nodiscard]] bool ensureSchema();
    [[nodiscard]] bool execStatements(const QStringList& statements);

    QString m_databasePath;
    QString m_connectionName;
    QString m_lastError;
    std::unique_ptr<QSqlDatabase> m_db;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
    std::optional<int> m_otherFormat;
    bool m_writeAheadLogging = false;
};

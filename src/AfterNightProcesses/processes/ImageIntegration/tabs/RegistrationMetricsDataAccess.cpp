// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationMetricsDataAccess.h"

#include "controls/PersistenceDbExecutor.h"
#include "processes/ImageIntegration/core/DatabaseUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <exception>

RegistrationMetricsDataAccess::RegistrationMetricsDataAccess() = default;

RegistrationMetricsDataAccess::~RegistrationMetricsDataAccess()
{
    closeMetricsDb();
}

void RegistrationMetricsDataAccess::closeMetricsDb()
{
    if (m_executor)
    {
        m_executor->shutdown();
        m_executor.reset();
    }

    m_cachedConnectionName.clear();
    m_cachedDbPath.clear();
}

bool RegistrationMetricsDataAccess::ensureExecutor(const QString& outputDirectory)
{
    if (outputDirectory.isEmpty())
    {
        closeMetricsDb();
        return false;
    }

    const QString dbPath = QDir(outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    if (!m_cachedDbPath.isEmpty() && m_cachedDbPath != dbPath)
    {
        closeMetricsDb();
    }

    if (!QFileInfo::exists(dbPath))
    {
        closeMetricsDb();
        return false;
    }

    if (m_executor && m_cachedDbPath == dbPath && m_executor->isOpen())
    {
        return true;
    }

    closeMetricsDb();

    m_cachedConnectionName = core::nextConnectionName(QStringLiteral("RegistrationTabMetrics"));
    m_cachedDbPath = dbPath;
    m_executor = std::make_unique<PersistenceDbExecutor>(dbPath, m_cachedConnectionName);
    if (!m_executor->isOpen())
    {
        m_lastError = QStringLiteral("Failed to open metrics database executor");
        closeMetricsDb();
        return false;
    }

    return true;
}

bool RegistrationMetricsDataAccess::execute(const QString& outputDirectory,
                                            const std::function<void(QSqlDatabase&)>& fn)
{
    m_lastError.clear();

    if (!fn)
    {
        return false;
    }

    if (!ensureExecutor(outputDirectory))
    {
        return false;
    }

    try
    {
        m_executor->execute(fn);
        return true;
    }
    catch (const std::exception& e)
    {
        m_lastError = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        m_lastError = QStringLiteral("Unknown database executor failure");
    }

    return false;
}

int RegistrationMetricsDataAccess::resolveEffectiveVersion(const QString& outputDirectory, const QString& groupKey,
                                                           int desiredVersion)
{
    int resolvedVersion = -1;
    const bool ok = execute(outputDirectory, [&](QSqlDatabase& db) {
        const auto hasRowsForVersion = [&](const int version) -> bool {
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                "SELECT 1 FROM registration_metrics WHERE group_key = :group_key AND settings_version = :v LIMIT 1"));
            query.bindValue(QStringLiteral(":group_key"), groupKey);
            query.bindValue(QStringLiteral(":v"), version);
            return query.exec() && query.next();
        };

        if (hasRowsForVersion(desiredVersion))
        {
            resolvedVersion = desiredVersion;
            return;
        }

        QSqlQuery latestVersionQuery(db);
        latestVersionQuery.prepare(
            QStringLiteral("SELECT MAX(settings_version) FROM registration_metrics WHERE group_key = :group_key"));
        latestVersionQuery.bindValue(QStringLiteral(":group_key"), groupKey);
        if (latestVersionQuery.exec() && latestVersionQuery.next())
        {
            const int latestVersion = latestVersionQuery.value(0).toInt();
            if (hasRowsForVersion(latestVersion))
            {
                resolvedVersion = latestVersion;
            }
        }
    });

    if (!ok)
    {
        return -1;
    }

    return resolvedVersion;
}

const QString& RegistrationMetricsDataAccess::lastError() const noexcept
{
    return m_lastError;
}

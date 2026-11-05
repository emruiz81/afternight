// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <functional>
#include <memory>

#include <QString>

class PersistenceDbExecutor;
class QSqlDatabase;

class RegistrationMetricsDataAccess
{
public:
    RegistrationMetricsDataAccess();
    ~RegistrationMetricsDataAccess();

    RegistrationMetricsDataAccess(const RegistrationMetricsDataAccess&) = delete;
    RegistrationMetricsDataAccess& operator=(const RegistrationMetricsDataAccess&) = delete;

    void closeMetricsDb();

    /// Execute @p fn on the dedicated metrics DB thread for the current output directory.
    /// Returns false if no DB is available or the executor fails.
    [[nodiscard]] bool execute(const QString& outputDirectory, const std::function<void(QSqlDatabase&)>& fn);

    [[nodiscard]] int resolveEffectiveVersion(const QString& outputDirectory, const QString& groupKey,
                                              int desiredVersion);

    [[nodiscard]] const QString& lastError() const noexcept;

private:
    [[nodiscard]] bool ensureExecutor(const QString& outputDirectory);

    QString m_cachedDbPath;
    QString m_cachedConnectionName;
    QString m_lastError;
    std::unique_ptr<PersistenceDbExecutor> m_executor;
};

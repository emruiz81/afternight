// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file AutosaveManager.h
 * @brief Automatic periodic session saving for crash recovery.
 *
 * Periodically saves the current IntegrationDataModel state to
 * IntegrationDatabase, providing recovery after unexpected exits.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <verdigris/wobjectdefs.h>

class IntegrationDataModel;
class IntegrationDatabase;

/**
 * @class AutosaveManager
 * @brief Timer-driven automatic session persistence.
 *
 * Monitors the IntegrationDataModel for changes and periodically
 * saves the current state to the database. Features:
 * - Configurable interval (default: 60 seconds)
 * - Dirty tracking to avoid unnecessary writes
 * - Manual triggerNow() for immediate save
 * - Signals for save success/failure feedback
 */
class AFTERNIGHTPROCESSES_API AutosaveManager : public QObject
{
    W_OBJECT(AutosaveManager)

public:
    AutosaveManager(IntegrationDataModel* model, IntegrationDatabase* database, const QString& sessionName,
                    QObject* parent = nullptr);

    void start(int intervalMs);
    void stop();
    void triggerNow();

signals:
    void autosaveCompleted() W_SIGNAL(autosaveCompleted);
    void autosaveFailed(const QString& message) W_SIGNAL(autosaveFailed, message);

private:
    void onTimeout();

    IntegrationDataModel* m_model = nullptr;
    IntegrationDatabase* m_database = nullptr;
    QString m_sessionName;
    QTimer m_timer;
    QByteArray m_lastAutosavedPayload;

    int m_failureCount = 0;
    qint64 m_nextAttemptUtcMs = 0;
};

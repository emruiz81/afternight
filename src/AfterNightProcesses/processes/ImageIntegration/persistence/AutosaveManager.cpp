// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AutosaveManager.h"

#include "IntegrationDatabase.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QDateTime>
#include <QJsonDocument>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(AutosaveManager)

AutosaveManager::AutosaveManager(IntegrationDataModel* model, IntegrationDatabase* database, const QString& sessionName,
                                 QObject* parent)
    : QObject(parent), m_model(model), m_database(database), m_sessionName(sessionName)
{
    connect(&m_timer, &QTimer::timeout, this, &AutosaveManager::onTimeout);
}

void AutosaveManager::start(int intervalMs)
{
    m_timer.start(intervalMs);
}

void AutosaveManager::stop()
{
    m_timer.stop();
}

void AutosaveManager::triggerNow()
{
    onTimeout();
}

void AutosaveManager::onTimeout()
{
    if (!m_model || !m_database)
    {
        return;
    }

    if (!m_model->hasUnsavedChanges())
    {
        m_failureCount = 0;
        m_nextAttemptUtcMs = 0;
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_nextAttemptUtcMs > now)
    {
        return;
    }

    const QJsonObject modelJson = m_model->toJson();
    const QByteArray payload = QJsonDocument(modelJson).toJson(QJsonDocument::Compact);
    if (payload == m_lastAutosavedPayload)
    {
        m_failureCount = 0;
        m_nextAttemptUtcMs = 0;
        return;
    }

    const auto result = m_database->saveSessionResult(m_sessionName, modelJson);
    if (result.ok)
    {
        m_lastAutosavedPayload = payload;
        m_failureCount = 0;
        m_nextAttemptUtcMs = 0;
        emit autosaveCompleted();
        return;
    }

    ++m_failureCount;
    const int shift = (std::min)(m_failureCount - 1, 4); // 1s..16s
    const int backoffMs = (std::min)(30000, 1000 * (1 << shift));
    m_nextAttemptUtcMs = now + backoffMs;

    emit autosaveFailed(result.errorMessage.isEmpty() ? QStringLiteral("Autosave failed") : result.errorMessage);
}

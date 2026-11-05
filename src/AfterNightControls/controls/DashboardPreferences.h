// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

/// Pure helper functions for dashboard preference serialization and
/// canonicalization. All functions are stateless and operate on value types,
/// making them easy to unit-test independently.
namespace DashboardPreferences
{

constexpr int RECENT_SORT_RECENT = 0;
constexpr int RECENT_SORT_NAME = 1;
constexpr int RECENT_SORT_TYPE = 2;

// ---------------------------------------------------------------------------
// Action IDs
// ---------------------------------------------------------------------------

inline QString normalizeActionIdToken(const QString& actionId)
{
    return actionId.trimmed().toLower();
}

inline QStringList deserializeActionIdList(const QString& serializedValue)
{
    QString normalizedSerialized = serializedValue;
    normalizedSerialized.replace('\r', '\n');
    normalizedSerialized.replace(',', '\n');
    normalizedSerialized.replace(';', '\n');

    QStringList actionIds;
    const QStringList parts = normalizedSerialized.split('\n', Qt::SkipEmptyParts);
    actionIds.reserve(parts.size());
    for (const QString& part : parts)
    {
        const QString normalizedId = normalizeActionIdToken(part);
        if (!normalizedId.isEmpty())
        {
            actionIds.push_back(normalizedId);
        }
    }
    return actionIds;
}

inline QString serializeActionIdList(const QStringList& actionIds)
{
    return actionIds.join('\n');
}

inline QStringList canonicalizeActionOrder(const QStringList& allActionIds, const QStringList& preferredActionIds)
{
    QSet<QString> seen;
    QStringList orderedActionIds;
    orderedActionIds.reserve(allActionIds.size());

    for (const QString& candidateId : preferredActionIds)
    {
        const QString normalizedId = normalizeActionIdToken(candidateId);
        if (normalizedId.isEmpty() || !allActionIds.contains(normalizedId) || seen.contains(normalizedId))
        {
            continue;
        }

        seen.insert(normalizedId);
        orderedActionIds.push_back(normalizedId);
    }

    for (const QString& actionId : allActionIds)
    {
        if (seen.contains(actionId))
        {
            continue;
        }

        seen.insert(actionId);
        orderedActionIds.push_back(actionId);
    }

    return orderedActionIds;
}

inline QSet<QString> canonicalizeHiddenActionIds(const QStringList& allActionIds, const QStringList& hiddenActionIds)
{
    QSet<QString> normalizedHiddenIds;
    for (const QString& candidateId : hiddenActionIds)
    {
        const QString normalizedId = normalizeActionIdToken(candidateId);
        if (!normalizedId.isEmpty() && allActionIds.contains(normalizedId))
        {
            normalizedHiddenIds.insert(normalizedId);
        }
    }
    return normalizedHiddenIds;
}

inline QStringList hiddenActionIdsInCanonicalOrder(const QStringList& allActionIds,
                                                   const QSet<QString>& hiddenActionIds)
{
    QStringList serializedHiddenIds;
    serializedHiddenIds.reserve(hiddenActionIds.size());
    for (const QString& actionId : allActionIds)
    {
        if (hiddenActionIds.contains(actionId))
        {
            serializedHiddenIds.push_back(actionId);
        }
    }
    return serializedHiddenIds;
}

// ---------------------------------------------------------------------------
// Dashboard action IDs
// ---------------------------------------------------------------------------

constexpr auto ACTION_ID_OPEN_FILE = "open_file";
constexpr auto ACTION_ID_NEW_SEQUENCE = "new_sequence";
constexpr auto ACTION_ID_IMAGE_INTEGRATION = "image_integration";
constexpr auto ACTION_ID_EXTENSION_MANAGER = "extension_manager";
constexpr auto ACTION_ID_CLEAR_RECENTS = "clear_recents";
constexpr auto ACTION_ID_SETTINGS = "settings";
constexpr auto ACTION_ID_SYSTEM_INFO = "system_info";
constexpr auto ACTION_ID_ABOUT = "about";
constexpr auto ACTION_ID_HELP = "help";

inline const QStringList& dashboardActionIds()
{
    static const QStringList ids = {
        QString::fromUtf8(ACTION_ID_OPEN_FILE),         QString::fromUtf8(ACTION_ID_NEW_SEQUENCE),
        QString::fromUtf8(ACTION_ID_IMAGE_INTEGRATION), QString::fromUtf8(ACTION_ID_EXTENSION_MANAGER),
        QString::fromUtf8(ACTION_ID_CLEAR_RECENTS),     QString::fromUtf8(ACTION_ID_SYSTEM_INFO)};
    return ids;
}

inline bool isPrimaryAction(const QString& actionId)
{
    const QString normalizedId = normalizeActionIdToken(actionId);
    return normalizedId == QLatin1String(ACTION_ID_OPEN_FILE) ||
           normalizedId == QLatin1String(ACTION_ID_NEW_SEQUENCE) ||
           normalizedId == QLatin1String(ACTION_ID_IMAGE_INTEGRATION);
}

inline QString actionTitleForId(const QString& actionId)
{
    const QString normalizedId = normalizeActionIdToken(actionId);
    if (normalizedId == QLatin1String(ACTION_ID_OPEN_FILE))
        return QObject::tr("Open file");
    if (normalizedId == QLatin1String(ACTION_ID_NEW_SEQUENCE))
        return QObject::tr("New sequence");
    if (normalizedId == QLatin1String(ACTION_ID_IMAGE_INTEGRATION))
        return QObject::tr("Image integration");
    if (normalizedId == QLatin1String(ACTION_ID_EXTENSION_MANAGER))
        return QObject::tr("Extension Manager");
    if (normalizedId == QLatin1String(ACTION_ID_CLEAR_RECENTS))
        return QObject::tr("Clear recents");
    if (normalizedId == QLatin1String(ACTION_ID_SETTINGS))
        return QObject::tr("Settings");
    if (normalizedId == QLatin1String(ACTION_ID_SYSTEM_INFO))
        return QObject::tr("System info");
    if (normalizedId == QLatin1String(ACTION_ID_ABOUT))
        return QObject::tr("About");
    if (normalizedId == QLatin1String(ACTION_ID_HELP))
        return QObject::tr("Help");
    return normalizedId;
}

inline QString actionDescriptionForId(const QString& actionId)
{
    const QString normalizedId = normalizeActionIdToken(actionId);
    if (normalizedId == QLatin1String(ACTION_ID_OPEN_FILE))
        return QObject::tr("Open an image or project file and switch to the normal processing workspace.");
    if (normalizedId == QLatin1String(ACTION_ID_NEW_SEQUENCE))
        return QObject::tr("Start a new processing sequence from the dashboard.");
    if (normalizedId == QLatin1String(ACTION_ID_IMAGE_INTEGRATION))
        return QObject::tr("Open the image integration workflow for calibrated frame stacking and reporting.");
    if (normalizedId == QLatin1String(ACTION_ID_EXTENSION_MANAGER))
        return QObject::tr(
            "Open the Extension Manager to browse available packages, install or update extensions, repair "
            "environments, and review installed extension status.");
    if (normalizedId == QLatin1String(ACTION_ID_CLEAR_RECENTS))
        return QObject::tr("Clear the dashboard's recent file list after confirmation.");
    if (normalizedId == QLatin1String(ACTION_ID_SETTINGS))
        return QObject::tr("Open the application settings dialog.");
    if (normalizedId == QLatin1String(ACTION_ID_SYSTEM_INFO))
        return QObject::tr("Open system information and runtime diagnostic details.");
    if (normalizedId == QLatin1String(ACTION_ID_ABOUT))
        return QObject::tr("Show the application version and project information.");
    if (normalizedId == QLatin1String(ACTION_ID_HELP))
        return QObject::tr("Open AfterNight help when it is available.");
    return actionTitleForId(actionId);
}

inline QString shortcutHintForActionId(const QString& actionId)
{
    const QString normalizedId = normalizeActionIdToken(actionId);
    if (normalizedId == QLatin1String(ACTION_ID_OPEN_FILE))
        return QStringLiteral("Ctrl+O");
    if (normalizedId == QLatin1String(ACTION_ID_SETTINGS))
        return QStringLiteral("Ctrl+,");
    if (normalizedId == QLatin1String(ACTION_ID_HELP))
        return QStringLiteral("F1");
    return {};
}

// ---------------------------------------------------------------------------
// Recent sort mode
// ---------------------------------------------------------------------------

inline QString serializeRecentSortMode(int sortMode)
{
    switch (sortMode)
    {
    case RECENT_SORT_NAME:
        return QStringLiteral("name");
    case RECENT_SORT_TYPE:
        return QStringLiteral("type");
    case RECENT_SORT_RECENT:
    default:
        return QStringLiteral("recent");
    }
}

inline int deserializeRecentSortMode(const QString& persistedValue)
{
    const QString normalized = persistedValue.trimmed().toLower();
    if (normalized == QStringLiteral("name"))
    {
        return RECENT_SORT_NAME;
    }

    if (normalized == QStringLiteral("type"))
    {
        return RECENT_SORT_TYPE;
    }

    return RECENT_SORT_RECENT;
}

} // namespace DashboardPreferences

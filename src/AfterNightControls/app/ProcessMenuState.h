// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <QString>
#include <QStringList>

#include "ProcessRegistry.h"

namespace an::ui
{

struct RecentProcessEntryData
{
    std::string id;
    std::string displayName;
};

using ProcessDescriptorLookup = std::function<const process_registry::ProcessDescriptor*(const std::string& processId)>;

struct FavoriteProcessMenuLayout
{
    bool addSeparatorBeforeManage = false;
    bool clearEnabled = false;
};

inline std::set<std::string> deserializeProcessIdSet(QString serializedValue)
{
    serializedValue.replace('\r', '\n');
    serializedValue.replace(',', '\n');
    serializedValue.replace(';', '\n');

    std::set<std::string> ids;
    const QStringList parts = serializedValue.split('\n', Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        const QString normalizedId = part.trimmed();
        if (!normalizedId.isEmpty())
        {
            ids.insert(normalizedId.toUtf8().toStdString());
        }
    }

    return ids;
}

inline QString serializeProcessIdSet(const std::set<std::string>& ids)
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(ids.size()));
    for (const std::string& id : ids)
    {
        if (!id.empty())
        {
            parts.push_back(QString::fromUtf8(id));
        }
    }

    return parts.join('\n');
}

inline std::vector<std::string> deserializeOrderedProcessIdList(QString serializedValue)
{
    serializedValue.replace('\r', '\n');
    serializedValue.replace(',', '\n');
    serializedValue.replace(';', '\n');

    std::vector<std::string> ids;
    std::set<std::string> seenIds;
    const QStringList parts = serializedValue.split('\n', Qt::SkipEmptyParts);
    ids.reserve(static_cast<size_t>(parts.size()));
    for (const QString& part : parts)
    {
        const QString normalizedId = part.trimmed();
        if (normalizedId.isEmpty())
        {
            continue;
        }

        const std::string id = normalizedId.toUtf8().toStdString();
        if (!seenIds.insert(id).second)
        {
            continue;
        }

        ids.push_back(id);
    }

    return ids;
}

inline QString serializeOrderedProcessIdList(const std::vector<std::string>& ids)
{
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(ids.size()));
    for (const std::string& id : ids)
    {
        if (!id.empty())
        {
            parts.push_back(QString::fromUtf8(id));
        }
    }

    return parts.join('\n');
}

inline QString processDisplayNameFromDescriptor(const process_registry::ProcessDescriptor* descriptor,
                                                const std::string& fallbackId)
{
    if (!descriptor)
    {
        return QString::fromUtf8(fallbackId);
    }

    QString displayName = QString::fromUtf8(descriptor->display_name);
    if (displayName.isEmpty())
    {
        displayName = QString::fromUtf8(descriptor->process_name);
    }
    if (displayName.isEmpty())
    {
        displayName = QString::fromUtf8(descriptor->action_object_name);
    }
    if (displayName.isEmpty())
    {
        displayName = QString::fromUtf8(descriptor->id);
    }

    return displayName;
}

inline std::deque<RecentProcessEntryData> buildRecentProcessEntries(const std::vector<std::string>& orderedProcessIds,
                                                                    const ProcessDescriptorLookup& findDescriptor)
{
    std::deque<RecentProcessEntryData> entries;
    for (const std::string& processId : orderedProcessIds)
    {
        if (processId.empty())
        {
            continue;
        }

        const auto* descriptor = findDescriptor ? findDescriptor(processId) : nullptr;
        entries.push_back({processId, processDisplayNameFromDescriptor(descriptor, processId).toUtf8().toStdString()});
    }

    return entries;
}

inline void enforceRecentProcessLimit(std::deque<RecentProcessEntryData>& recentProcesses,
                                      const size_t maxRecentProcesses)
{
    while (recentProcesses.size() > maxRecentProcesses)
    {
        recentProcesses.pop_back();
    }
}

inline bool pruneMissingRecentProcessEntries(std::deque<RecentProcessEntryData>& recentProcesses,
                                             const ProcessDescriptorLookup& findDescriptor)
{
    if (!findDescriptor)
    {
        return false;
    }

    const auto originalSize = recentProcesses.size();
    recentProcesses.erase(std::remove_if(recentProcesses.begin(), recentProcesses.end(),
                                         [&findDescriptor](const RecentProcessEntryData& entry) {
                                             return entry.id.empty() || findDescriptor(entry.id) == nullptr;
                                         }),
                          recentProcesses.end());
    return recentProcesses.size() != originalSize;
}

inline bool updateRecentProcessList(std::deque<RecentProcessEntryData>& recentProcesses, const std::string& processId,
                                    const std::string& displayName, const size_t maxRecentProcesses)
{
    if (processId.empty())
    {
        return false;
    }

    auto it = std::find_if(recentProcesses.begin(), recentProcesses.end(),
                           [&processId](const RecentProcessEntryData& entry) { return entry.id == processId; });
    if (it != recentProcesses.end())
    {
        recentProcesses.erase(it);
    }

    recentProcesses.push_front({processId, displayName.empty() ? processId : displayName});
    enforceRecentProcessLimit(recentProcesses, maxRecentProcesses);
    return true;
}

inline std::vector<std::pair<QString, std::string>>
buildFavoriteProcessEntries(const std::set<std::string>& favoriteProcessIds,
                            const ProcessDescriptorLookup& findDescriptor,
                            std::set<std::string>* validFavoriteIds = nullptr)
{
    std::set<std::string> validIds;
    std::vector<std::pair<QString, std::string>> favoriteEntries;
    for (const std::string& processId : favoriteProcessIds)
    {
        const auto* descriptor = findDescriptor ? findDescriptor(processId) : nullptr;
        if (!descriptor)
        {
            continue;
        }

        validIds.insert(processId);
        favoriteEntries.emplace_back(processDisplayNameFromDescriptor(descriptor, processId), processId);
    }

    std::sort(favoriteEntries.begin(), favoriteEntries.end(), [](const auto& lhs, const auto& rhs) {
        return QString::compare(lhs.first, rhs.first, Qt::CaseInsensitive) < 0;
    });

    if (validFavoriteIds)
    {
        *validFavoriteIds = std::move(validIds);
    }

    return favoriteEntries;
}

inline FavoriteProcessMenuLayout buildFavoriteProcessMenuLayout(const size_t favoriteEntriesCount)
{
    FavoriteProcessMenuLayout layout;
    layout.addSeparatorBeforeManage = favoriteEntriesCount > 0;
    layout.clearEnabled = favoriteEntriesCount > 0;
    return layout;
}

} // namespace an::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/RecentProcessesModel.h"

#include "app/ProcessRegistry.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <utility>
#include <vector>

namespace an::ui::shell
{

namespace
{

constexpr std::size_t kDefaultLimit = SettingsDefaults::GENERAL_RECENT_PROCESSES_LIMIT;

} // namespace

RecentProcessesModel::RecentProcessesModel(an::ui::ProcessDescriptorLookup lookup) : m_lookup(std::move(lookup))
{
    if (!m_lookup)
    {
        m_lookup = [](const std::string& processId) {
            return an::ui::process_registry::ProcessRegistry::instance().find(processId);
        };
    }
}

void RecentProcessesModel::setSettings(an::ui::IGlobalSettingsProvider* settings)
{
    m_settings = settings;
}

std::size_t RecentProcessesModel::limit() const
{
    if (m_settings != nullptr)
    {
        const QString limitStr =
            m_settings->select(SettingsKeys::GENERAL_RECENT_PROCESSES_LIMIT, QString::number(kDefaultLimit));
        bool ok = false;
        const int limit = limitStr.toInt(&ok);
        if (ok && limit > 0)
        {
            return static_cast<std::size_t>(limit);
        }
    }

    return kDefaultLimit;
}

const std::deque<an::ui::RecentProcessEntryData>& RecentProcessesModel::entries() const noexcept
{
    return m_entries;
}

void RecentProcessesModel::load()
{
    if (m_settings == nullptr)
    {
        m_entries.clear();
        return;
    }

    const QString serializedIds = m_settings->select(SettingsKeys::INTERFACE_RECENT_PROCESS_IDS,
                                                     QString(SettingsDefaults::INTERFACE_RECENT_PROCESS_IDS));
    const std::vector<std::string> processIds = an::ui::deserializeOrderedProcessIdList(serializedIds);
    m_entries = an::ui::buildRecentProcessEntries(processIds, m_lookup);
}

void RecentProcessesModel::save() const
{
    if (m_settings == nullptr)
    {
        return;
    }

    std::vector<std::string> processIds;
    processIds.reserve(m_entries.size());
    for (const auto& recentProcess : m_entries)
    {
        if (!recentProcess.id.empty())
        {
            processIds.push_back(recentProcess.id);
        }
    }

    m_settings->upsert(SettingsKeys::INTERFACE_RECENT_PROCESS_IDS, an::ui::serializeOrderedProcessIdList(processIds));
}

void RecentProcessesModel::enforceLimit()
{
    an::ui::enforceRecentProcessLimit(m_entries, limit());
}

bool RecentProcessesModel::record(const std::string& processId, const std::string& displayName)
{
    const bool changed = an::ui::updateRecentProcessList(m_entries, processId, displayName, limit());
    if (!changed)
    {
        return false;
    }

    save();
    return true;
}

bool RecentProcessesModel::pruneMissing()
{
    return an::ui::pruneMissingRecentProcessEntries(m_entries, m_lookup);
}

void RecentProcessesModel::clear()
{
    m_entries.clear();
    save();
}

} // namespace an::ui::shell

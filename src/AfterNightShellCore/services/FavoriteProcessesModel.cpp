// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/FavoriteProcessesModel.h"

#include "app/ProcessRegistry.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

namespace an::ui::shell
{

FavoriteProcessesModel::FavoriteProcessesModel(an::ui::ProcessDescriptorLookup lookup) : m_lookup(std::move(lookup))
{
    if (!m_lookup)
    {
        m_lookup = [](const std::string& processId) {
            return an::ui::process_registry::ProcessRegistry::instance().find(processId);
        };
    }
}

void FavoriteProcessesModel::setSettings(an::ui::IGlobalSettingsProvider* settings)
{
    m_settings = settings;
}

const std::set<std::string>& FavoriteProcessesModel::ids() const noexcept
{
    return m_ids;
}

std::vector<std::pair<QString, std::string>> FavoriteProcessesModel::entries() const
{
    return an::ui::buildFavoriteProcessEntries(m_ids, m_lookup);
}

void FavoriteProcessesModel::load()
{
    m_ids.clear();
    if (m_settings == nullptr)
    {
        return;
    }

    const QString serializedIds = m_settings->select(SettingsKeys::INTERFACE_FAVORITE_PROCESS_IDS,
                                                     QString(SettingsDefaults::INTERFACE_FAVORITE_PROCESS_IDS));
    m_ids = an::ui::deserializeProcessIdSet(serializedIds);
}

void FavoriteProcessesModel::save() const
{
    if (m_settings == nullptr)
    {
        return;
    }

    m_settings->upsert(SettingsKeys::INTERFACE_FAVORITE_PROCESS_IDS, an::ui::serializeProcessIdSet(m_ids));
}

void FavoriteProcessesModel::setIds(std::set<std::string> ids)
{
    m_ids = std::move(ids);
    save();
}

bool FavoriteProcessesModel::pruneMissing()
{
    std::set<std::string> validFavoriteIds;
    (void)an::ui::buildFavoriteProcessEntries(m_ids, m_lookup, &validFavoriteIds);
    if (validFavoriteIds == m_ids)
    {
        return false;
    }

    m_ids = std::move(validFavoriteIds);
    save();
    return true;
}

void FavoriteProcessesModel::clear()
{
    m_ids.clear();
    save();
}

} // namespace an::ui::shell

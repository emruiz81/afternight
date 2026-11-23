// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/DashboardSettings.h"

#include "controls/DashboardPreferences.h"
#include "controls/DashboardUtils.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QMenu>

#include <algorithm>

namespace an::ui
{

namespace
{

using DashboardPreferences::canonicalizeActionOrder;
using DashboardPreferences::canonicalizeHiddenActionIds;
using DashboardPreferences::dashboardActionIds;
using DashboardPreferences::deserializeActionIdList;
using DashboardPreferences::hiddenActionIdsInCanonicalOrder;
using DashboardPreferences::serializeActionIdList;
using DashboardUtils::normalizeRecentFilePath;

constexpr int kSettingsFormat = 4;

/// Every setting the dashboard's format governs, the format itself among them.
constexpr const char* kDashboardKeys[] = {
    SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION, SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER,
    SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS,          SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS,
    SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE,        SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE,
    SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT,      SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION,
    SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT};

IGlobalSettingsProvider* globalSettings()
{
    if (auto* context = appContext())
    {
        return context->getGlobalSettings();
    }
    return nullptr;
}

QString selectSetting(const char* key, const QString& defaultValue = {})
{
    if (auto* settings = globalSettings())
    {
        return settings->select(QString::fromUtf8(key), defaultValue);
    }
    return defaultValue;
}

void upsertSetting(const char* key, const QString& value)
{
    if (auto* settings = globalSettings())
    {
        settings->upsert(QString::fromUtf8(key), value);
    }
}

/// Home's words, under the translation context AfterNight 1.1's dashboard gave them, which the translations keep.
QString dashboardText(const char* text)
{
    return QCoreApplication::translate("MainDashboard", text);
}

} // namespace

void ensureDashboardSettingsFormat()
{
    IGlobalSettingsProvider* settings = globalSettings();
    if (settings == nullptr)
    {
        return;
    }

    const QString current = QString::number(kSettingsFormat);
    const QString stated = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION).trimmed();
    if (stated == current)
    {
        return;
    }

    // Settings of another format - older, newer, or keys that state none - are never read: each one stored is set aside
    // under its own name with the format it states beside it, and the dashboard starts from its defaults.
    bool numbered = false;
    const int statedFormat = stated.toInt(&numbered);
    const QString suffix =
        numbered && statedFormat > 0 ? QStringLiteral(".format%1").arg(statedFormat) : QStringLiteral(".unversioned");
    QStringList setAside;
    for (const char* key : kDashboardKeys)
    {
        const QString name = QString::fromUtf8(key);
        const QString value = settings->select(name, QString());
        if (value.isEmpty())
        {
            continue;
        }
        settings->upsert(name + suffix, value);
        settings->upsert(name, QString());
        setAside << name;
    }
    if (!setAside.isEmpty())
    {
        LOG_WARNING("Home: the dashboard's settings {}; this version of AfterNight reads format {}. They were "
                    "set aside under their names with '{}' beside them, and the dashboard starts from its defaults.",
                    numbered && statedFormat > 0 ? fmt::format("were written in format {}", statedFormat)
                                                 : std::string("state no format version"),
                    kSettingsFormat, suffix.toStdString());
    }
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION, current);
}

QSet<QString> loadPinnedRecentFiles()
{
    QSet<QString> pinned;
    ensureDashboardSettingsFormat();
    if (globalSettings() == nullptr)
    {
        return pinned;
    }
    const QString serialized = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS);
    for (const QString& entry : serialized.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
    {
        const QString path = normalizeRecentFilePath(entry);
        if (!path.isEmpty())
        {
            pinned.insert(path);
        }
    }
    return pinned;
}

void storePinnedRecentFiles(const QSet<QString>& pinned)
{
    if (globalSettings() == nullptr)
    {
        return;
    }
    QStringList entries(pinned.cbegin(), pinned.cend());
    std::sort(entries.begin(), entries.end(),
              [](const QString& lhs, const QString& rhs) { return QString::localeAwareCompare(lhs, rhs) < 0; });
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS, entries.join(QLatin1Char('\n')));
}

QString loadPinnedFeaturedFile()
{
    ensureDashboardSettingsFormat();
    if (globalSettings() == nullptr)
    {
        return {};
    }
    // Only normalized: a file that is gone is shown as missing where the card is drawn, so no stat is made here.
    const QString stored = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE);
    const QString normalized = normalizeRecentFilePath(stored);
    if (stored.trimmed() != normalized)
    {
        storePinnedFeaturedFile(normalized);
    }
    return normalized;
}

void storePinnedFeaturedFile(const QString& path)
{
    if (globalSettings() == nullptr)
    {
        return;
    }
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE, path.trimmed());
}

int loadRecentSortMode()
{
    ensureDashboardSettingsFormat();
    if (globalSettings() == nullptr)
    {
        return DashboardPreferences::RECENT_SORT_RECENT;
    }
    const QString stored = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE);
    const int sortMode = DashboardPreferences::deserializeRecentSortMode(stored);
    const QString canonical = DashboardPreferences::serializeRecentSortMode(sortMode);
    if (stored.trimmed().toLower() != canonical)
    {
        upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE, canonical);
    }
    return sortMode;
}

void storeRecentSortMode(const int sortMode)
{
    if (globalSettings() == nullptr)
    {
        return;
    }
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE,
                  DashboardPreferences::serializeRecentSortMode(sortMode));
}

QString loadRecentSearchText()
{
    ensureDashboardSettingsFormat();
    if (globalSettings() == nullptr)
    {
        return {};
    }
    const QString stored = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT);
    const QString trimmed = stored.trimmed();
    if (stored != trimmed)
    {
        upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT, trimmed);
    }
    return trimmed;
}

void storeRecentSearchText(const QString& text)
{
    if (globalSettings() == nullptr)
    {
        return;
    }
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT, text.trimmed());
}

int loadRecentArrangement()
{
    ensureDashboardSettingsFormat();
    if (globalSettings() == nullptr)
    {
        return DashboardPreferences::RECENT_ARRANGEMENT_GRID;
    }
    const QString stored = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT);
    const int arrangement = DashboardPreferences::deserializeRecentArrangement(stored);
    const QString canonical = DashboardPreferences::serializeRecentArrangement(arrangement);
    if (stored != canonical)
    {
        upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT, canonical);
    }
    return arrangement;
}

void storeRecentArrangement(const int arrangement)
{
    if (globalSettings() == nullptr)
    {
        return;
    }
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_RECENT_ARRANGEMENT,
                  DashboardPreferences::serializeRecentArrangement(arrangement));
}

void recordDashboardLastAction(const QString& action)
{
    const QString trimmed = action.trimmed();
    if (trimmed.isEmpty() || globalSettings() == nullptr)
    {
        return;
    }
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION, trimmed);
}

// ---- DashboardQuickActionLayout

DashboardQuickActionLayout::DashboardQuickActionLayout()
    : m_order(canonicalizeActionOrder(dashboardActionIds(), dashboardActionIds()))
{
}

void DashboardQuickActionLayout::load()
{
    const QStringList& all = dashboardActionIds();
    m_order = canonicalizeActionOrder(all, all);
    m_hidden.clear();

    ensureDashboardSettingsFormat();
    if (globalSettings() == nullptr)
    {
        return;
    }

    const QString storedOrder = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER);
    const QString storedHidden = selectSetting(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS);
    m_order = canonicalizeActionOrder(all, deserializeActionIdList(storedOrder));
    m_hidden = canonicalizeHiddenActionIds(all, deserializeActionIdList(storedHidden));
    if (storedOrder != serializeActionIdList(m_order) ||
        storedHidden != serializeActionIdList(hiddenActionIdsInCanonicalOrder(all, m_hidden)))
    {
        store();
    }
}

void DashboardQuickActionLayout::store() const
{
    if (globalSettings() == nullptr)
    {
        return;
    }
    const QStringList& all = dashboardActionIds();
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER,
                  serializeActionIdList(canonicalizeActionOrder(all, m_order)));
    upsertSetting(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS,
                  serializeActionIdList(hiddenActionIdsInCanonicalOrder(all, m_hidden)));
}

const QStringList& DashboardQuickActionLayout::order() const noexcept
{
    return m_order;
}

const QSet<QString>& DashboardQuickActionLayout::hidden() const noexcept
{
    return m_hidden;
}

QStringList DashboardQuickActionLayout::shownPrimary() const
{
    QStringList shown;
    for (const QString& id : m_order)
    {
        if (DashboardPreferences::isPrimaryAction(id) && !m_hidden.contains(id))
        {
            shown << id;
        }
    }
    return shown;
}

QStringList DashboardQuickActionLayout::shownOverflow() const
{
    QStringList shown;
    for (const QString& id : m_order)
    {
        if (!DashboardPreferences::isPrimaryAction(id) && !m_hidden.contains(id))
        {
            shown << id;
        }
    }
    return shown;
}

void DashboardQuickActionLayout::canonicalize()
{
    const QStringList& all = dashboardActionIds();
    m_order = canonicalizeActionOrder(all, m_order);
    m_hidden = canonicalizeHiddenActionIds(all, hiddenActionIdsInCanonicalOrder(all, m_hidden));
}

void DashboardQuickActionLayout::populateCustomizationMenu(QMenu& menu, QObject* receiver,
                                                           const std::function<void()>& changed)
{
    canonicalize();
    int shownCount = 0;
    for (const QString& id : m_order)
    {
        if (!m_hidden.contains(id))
        {
            ++shownCount;
        }
    }

    const auto applied = [this, changed]() {
        canonicalize();
        store();
        if (changed)
        {
            changed();
        }
    };

    for (const QString& id : m_order)
    {
        const qsizetype index = m_order.indexOf(id);
        QMenu* actionMenu = menu.addMenu(DashboardPreferences::actionTitleForId(id));
        QAction* earlier = actionMenu->addAction(dashboardText("Move earlier"));
        QAction* later = actionMenu->addAction(dashboardText("Move later"));
        earlier->setEnabled(index > 0);
        later->setEnabled(index >= 0 && index < m_order.size() - 1);

        const bool isHidden = m_hidden.contains(id);
        QAction* hideOrShow =
            actionMenu->addAction(isHidden ? dashboardText("Show action") : dashboardText("Hide action"));
        hideOrShow->setEnabled(isHidden || shownCount > 1);

        QObject::connect(earlier, &QAction::triggered, receiver, [this, index, applied]() {
            if (index <= 0 || index >= m_order.size())
            {
                return;
            }
            m_order.swapItemsAt(index, index - 1);
            applied();
        });
        QObject::connect(later, &QAction::triggered, receiver, [this, index, applied]() {
            if (index < 0 || index >= m_order.size() - 1)
            {
                return;
            }
            m_order.swapItemsAt(index, index + 1);
            applied();
        });
        QObject::connect(hideOrShow, &QAction::triggered, receiver, [this, id, isHidden, shownCount, applied]() {
            if (isHidden)
            {
                m_hidden.remove(id);
                applied();
                return;
            }
            if (shownCount <= 1)
            {
                return;
            }
            m_hidden.insert(id);
            applied();
        });
    }

    QMenu* hiddenMenu = menu.addMenu(dashboardText("Show hidden action"));
    hiddenMenu->setEnabled(!m_hidden.isEmpty());
    for (const QString& id : m_order)
    {
        if (!m_hidden.contains(id))
        {
            continue;
        }
        QAction* reveal = hiddenMenu->addAction(DashboardPreferences::actionTitleForId(id));
        QObject::connect(reveal, &QAction::triggered, receiver, [this, id, applied]() {
            m_hidden.remove(id);
            applied();
        });
    }

    menu.addSeparator();
    QAction* reset = menu.addAction(dashboardText("Reset quick actions"));
    QObject::connect(reset, &QAction::triggered, receiver, [this, applied]() {
        const QStringList& all = dashboardActionIds();
        const QStringList canonical = canonicalizeActionOrder(all, all);
        if (m_order == canonical && m_hidden.isEmpty())
        {
            return;
        }
        m_order = canonical;
        m_hidden.clear();
        applied();
    });
}

void DashboardQuickActionLayout::addCustomizationMenu(QMenu& menu, QObject* receiver,
                                                      const std::function<void()>& changed)
{
    if (QMenu* customize = menu.addMenu(dashboardText("Customize quick actions")))
    {
        populateCustomizationMenu(*customize, receiver, changed);
    }
}

} // namespace an::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/DashboardToolbar.h"

#include "components/AnButtonGroup.h"
#include "controls/DashboardPreferences.h"
#include "theme/AnDesignTokens.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QStyle>
#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::DashboardToolbar)

using DashboardPreferences::ACTION_ID_ABOUT;
using DashboardPreferences::ACTION_ID_CLEAR_RECENTS;
using DashboardPreferences::ACTION_ID_EXTENSION_MANAGER;
using DashboardPreferences::ACTION_ID_HELP;
using DashboardPreferences::ACTION_ID_IMAGE_INTEGRATION;
using DashboardPreferences::ACTION_ID_NEW_SEQUENCE;
using DashboardPreferences::ACTION_ID_OPEN_FILE;
using DashboardPreferences::ACTION_ID_SETTINGS;
using DashboardPreferences::ACTION_ID_SYSTEM_INFO;
using DashboardPreferences::dashboardActionIds;
using DashboardPreferences::normalizeActionIdToken;

namespace an::ui
{

namespace
{

QString tooltipForActionId(const QString& actionId)
{
    const QString description = DashboardPreferences::actionDescriptionForId(actionId);
    const QString shortcut = DashboardPreferences::shortcutHintForActionId(actionId);
    if (shortcut.isEmpty())
    {
        return description;
    }

    return QObject::tr("%1\nShortcut: %2").arg(description, shortcut);
}

} // anonymous namespace

DashboardToolbar::DashboardToolbar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("dashboardToolbar"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.sm);

    m_PrimaryButtons = new an::ui::AnButtonGroup(this);
    m_PrimaryButtons->setObjectName(QStringLiteral("dashboardToolbarPrimaryButtons"));
    layout->addWidget(m_PrimaryButtons, 1);

    m_OverflowButton = new QPushButton(QStringLiteral("···"), this);
    m_OverflowButton->setObjectName(QStringLiteral("dashboardToolbarOverflowButton"));
    m_OverflowButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_OverflowButton->setProperty("dashboardActionPlacement", QStringLiteral("overflow"));
    m_OverflowButton->setToolTip(tr("More actions"));
    m_OverflowButton->setMinimumWidth(40);
    layout->addWidget(m_OverflowButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_OverflowMenu = new QMenu(m_OverflowButton);
    m_OverflowMenu->setObjectName(QStringLiteral("dashboardToolbarOverflowMenu"));
    m_OverflowButton->setMenu(m_OverflowMenu);

    m_ActionOrder = dashboardActionIds();
    for (const QString& actionId : m_ActionOrder)
    {
        m_ActionEnabled.insert(actionId, true);
    }

    rebuildPrimaryButtons();
    rebuildOverflowMenu();
}

void DashboardToolbar::setActionOrder(const QStringList& orderedActionIds)
{
    m_ActionOrder = orderedActionIds;
    rebuildPrimaryButtons();
    rebuildOverflowMenu();
}

void DashboardToolbar::setHiddenActionIds(const QSet<QString>& hiddenActionIds)
{
    m_HiddenActionIds = hiddenActionIds;
    rebuildPrimaryButtons();
    rebuildOverflowMenu();
}

void DashboardToolbar::setActionEnabled(const QString& actionId, const bool enabled)
{
    const QString normalizedId = normalizeActionIdToken(actionId);
    m_ActionEnabled.insert(normalizedId, enabled);

    if (QPushButton* button = m_PrimaryButtonsById.value(normalizedId, nullptr))
    {
        button->setEnabled(enabled);
    }

    if (!isPrimaryAction(normalizedId))
    {
        rebuildOverflowMenu();
    }
}

void DashboardToolbar::setActionIcon(const QString& actionId, const QIcon& icon)
{
    const QString normalizedId = normalizeActionIdToken(actionId);
    m_ActionIcons.insert(normalizedId, icon);

    if (QPushButton* button = m_PrimaryButtonsById.value(normalizedId, nullptr))
    {
        button->setIcon(icon);
    }

    if (!isPrimaryAction(normalizedId))
    {
        rebuildOverflowMenu();
    }
}

void DashboardToolbar::setCustomizationMenuBuilder(std::function<void(QMenu& menu)> menuBuilder)
{
    m_CustomizationMenuBuilder = std::move(menuBuilder);
    rebuildOverflowMenu();
}

QString DashboardToolbar::actionTitleForId(const QString& actionId) const
{
    return DashboardPreferences::actionTitleForId(actionId);
}

QPushButton* DashboardToolbar::actionButton(const QString& actionId) const
{
    return m_PrimaryButtonsById.value(normalizeActionIdToken(actionId), nullptr);
}

bool DashboardToolbar::isPrimaryAction(const QString& actionId) const
{
    return DashboardPreferences::isPrimaryAction(actionId);
}

QStringList DashboardToolbar::orderedPrimaryActionIds() const
{
    QStringList actionIds;
    actionIds.reserve(m_ActionOrder.size());
    for (const QString& actionId : m_ActionOrder)
    {
        if (isPrimaryAction(actionId))
        {
            actionIds.push_back(actionId);
        }
    }
    return actionIds;
}

QStringList DashboardToolbar::orderedSecondaryActionIds() const
{
    QStringList actionIds;
    actionIds.reserve(m_ActionOrder.size());
    for (const QString& actionId : m_ActionOrder)
    {
        if (!isPrimaryAction(actionId))
        {
            actionIds.push_back(actionId);
        }
    }
    return actionIds;
}

void DashboardToolbar::emitActionForId(const QString& actionId)
{
    const QString normalizedId = normalizeActionIdToken(actionId);
    if (normalizedId == QLatin1String(ACTION_ID_OPEN_FILE))
    {
        emit openFileRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_NEW_SEQUENCE))
    {
        emit newSequenceRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_IMAGE_INTEGRATION))
    {
        emit imageIntegrationRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_EXTENSION_MANAGER))
    {
        emit openExtensionManagerRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_CLEAR_RECENTS))
    {
        emit clearRecentFilesRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_SETTINGS))
    {
        emit openSettingsRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_SYSTEM_INFO))
    {
        emit systemInformationRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_ABOUT))
    {
        emit aboutRequested();
        return;
    }
    if (normalizedId == QLatin1String(ACTION_ID_HELP))
    {
        emit openHelpRequested();
    }
}

void DashboardToolbar::rebuildPrimaryButtons()
{
    if (!m_PrimaryButtons)
    {
        return;
    }

    m_PrimaryButtons->clearButtons();
    m_PrimaryButtonsById.clear();

    for (const QString& actionId : orderedPrimaryActionIds())
    {
        if (m_HiddenActionIds.contains(actionId))
        {
            continue;
        }

        QPushButton* button =
            m_PrimaryButtons->addButton(actionTitleForId(actionId), an::ui::AnButtonGroup::ButtonVariant::Primary);
        if (!button)
        {
            continue;
        }

        button->setObjectName(QStringLiteral("dashboardActionButton"));
        button->setProperty("dashboardActionId", normalizeActionIdToken(actionId));
        button->setProperty("dashboardActionTitle", actionTitleForId(actionId));
        button->setProperty("dashboardActionPlacement", QStringLiteral("primary"));
        button->setProperty("buttonVariant", QStringLiteral("primary"));
        button->setCursor(Qt::PointingHandCursor);

        button->setToolTip(tooltipForActionId(actionId));

        if (m_ActionIcons.contains(actionId))
        {
            button->setIcon(m_ActionIcons.value(actionId));
            button->setIconSize(QSize(18, 18));
        }

        button->setEnabled(m_ActionEnabled.value(actionId, true));
        connect(button, &QPushButton::clicked, this, [this, actionId]() { emitActionForId(actionId); });

        m_PrimaryButtonsById.insert(actionId, button);
    }
}

void DashboardToolbar::rebuildOverflowMenu()
{
    if (!m_OverflowMenu)
    {
        return;
    }

    m_OverflowMenu->clear();

    for (const QString& actionId : orderedSecondaryActionIds())
    {
        if (m_HiddenActionIds.contains(actionId))
        {
            continue;
        }

        QAction* action = m_OverflowMenu->addAction(m_ActionIcons.value(actionId), actionTitleForId(actionId));
        action->setData(normalizeActionIdToken(actionId));
        action->setEnabled(m_ActionEnabled.value(actionId, true));

        action->setToolTip(tooltipForActionId(actionId));

        connect(action, &QAction::triggered, this, [this, actionId]() { emitActionForId(actionId); });
    }

    if (m_CustomizationMenuBuilder)
    {
        if (!m_OverflowMenu->actions().isEmpty())
        {
            m_OverflowMenu->addSeparator();
        }
        m_CustomizationMenuBuilder(*m_OverflowMenu);
    }
}

} // namespace an::ui

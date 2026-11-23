// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "workspaces/WorkspaceToolbar.h"

#include "actions/ActionIds.h"
#include "theme/ComponentParts.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QToolButton>

#include <utility>

namespace an::ui::observatory
{

namespace
{

[[nodiscard]] QToolButton* makeButton(QWidget* parent, const QString& name)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(name);
    button->setAutoRaise(true);
    button->setFixedSize(an::ui::parts::kTopBarButton, an::ui::parts::kTopBarButton);
    button->setIconSize(QSize(an::ui::parts::kTopBarIcon, an::ui::parts::kTopBarIcon));
    return button;
}

} // namespace

WorkspaceToolbar::WorkspaceToolbar(Dependencies dependencies, QWidget* parent)
    : QWidget(parent), m_dependencies(std::move(dependencies))
{
    setObjectName(QStringLiteral("observatoryLayoutIcons"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    m_lock = makeButton(this, QStringLiteral("layoutLockButton"));
    connect(m_lock, &QToolButton::clicked, this, [this]() {
        const bool locked = m_dependencies.isLocked && m_dependencies.isLocked();
        if (m_dependencies.setLocked)
        {
            m_dependencies.setLocked(!locked);
        }
        refreshLock();
    });
    row->addWidget(m_lock);

    const auto actionButton = [this, row](const std::string_view id, const char* name) {
        QToolButton* const button = makeButton(this, QString::fromLatin1(name));
        if (QAction* const action = m_dependencies.actions ? m_dependencies.actions(id) : nullptr)
        {
            button->setDefaultAction(action);
        }
        row->addWidget(button);
        return button;
    };
    m_group = actionButton(an::ui::shell::ids::kGroupAllImages, "layoutGroupButton");
    m_split = actionButton(an::ui::shell::ids::kSplitAllImages, "layoutSplitButton");

    auto* separator = new QFrame(this);
    separator->setObjectName(QStringLiteral("layoutIconsSeparator"));
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    row->addWidget(separator);

    m_fit = actionButton(an::ui::shell::ids::kFitImagesToWindow, "layoutFitButton");
    refreshLock();
}

QToolButton* WorkspaceToolbar::lockButton() const noexcept
{
    return m_lock;
}

QToolButton* WorkspaceToolbar::groupButton() const noexcept
{
    return m_group;
}

QToolButton* WorkspaceToolbar::splitButton() const noexcept
{
    return m_split;
}

QToolButton* WorkspaceToolbar::fitButton() const noexcept
{
    return m_fit;
}

void WorkspaceToolbar::refreshLock()
{
    const bool locked = m_dependencies.isLocked && m_dependencies.isLocked();
    // The button shows what a click does.
    const QString iconName = locked ? QStringLiteral("unlock") : QStringLiteral("lock");
    m_lock->setIcon(m_dependencies.icon ? m_dependencies.icon(iconName) : QIcon());
    m_lock->setToolTip(locked ? tr("Unlock layout") : tr("Lock layout"));
    m_lock->setAccessibleName(m_lock->toolTip());
}

} // namespace an::ui::observatory

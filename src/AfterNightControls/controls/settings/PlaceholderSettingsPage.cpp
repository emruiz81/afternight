// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PlaceholderSettingsPage.h"
#include "theme/AnDesignTokens.h"
#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPalette>

W_OBJECT_IMPL(PlaceholderSettingsPage)

PlaceholderSettingsPage::PlaceholderSettingsPage(const QString& title, const QIcon& icon, bool enabled, QWidget* parent)
    : SettingsPage(parent), m_Title(title), m_Icon(icon), m_Enabled(enabled)
{
    setupUi();
}

QString PlaceholderSettingsPage::pageTitle() const
{
    return m_Title;
}

QIcon PlaceholderSettingsPage::pageIcon() const
{
    return m_Icon;
}

bool PlaceholderSettingsPage::isEnabled() const
{
    return m_Enabled;
}

void PlaceholderSettingsPage::loadSettings()
{
    // No-op for placeholder
}

void PlaceholderSettingsPage::saveSettings()
{
    // No-op for placeholder
}

void PlaceholderSettingsPage::resetToDefaults()
{
    // No-op for placeholder
}

void PlaceholderSettingsPage::setupUi()
{
    const auto& tokenSet = an::ui::tokens();

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(tokenSet.spacing.xl);
    layout->setContentsMargins(tokenSet.spacing.xl, tokenSet.spacing.xl, tokenSet.spacing.xl, tokenSet.spacing.xl);

    // Spacer to center content
    layout->addStretch(1);

    // Container for message
    auto* container = new QFrame(this);
    container->setObjectName("placeholderContainer");
    container->setFrameShape(QFrame::StyledPanel);

    // Use palette for background/border colors
    QPalette containerPal = container->palette();
    containerPal.setColor(QPalette::Window, container->palette().color(QPalette::AlternateBase));
    container->setPalette(containerPal);
    container->setAutoFillBackground(true);

    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setSpacing(tokenSet.spacing.lg);
    containerLayout->setAlignment(Qt::AlignCenter);

    // Icon (scaled up)
    auto* iconLabel = new QLabel(container);
    QPixmap iconPixmap = m_Icon.pixmap(64, 64);
    iconLabel->setPixmap(iconPixmap);
    iconLabel->setAlignment(Qt::AlignCenter);
    containerLayout->addWidget(iconLabel);

    // Message
    m_MessageLabel = new QLabel(container);
    if (m_Enabled)
    {
        m_MessageLabel->setText(tr("This settings page is under development.\n\nSettings will be available soon."));
    }
    else
    {
        m_MessageLabel->setText(tr("This feature is planned for a future release.\n\nConfigure related tools in the "
                                   "External Tools section\nto prepare for this feature."));
    }
    m_MessageLabel->setAlignment(Qt::AlignCenter);
    m_MessageLabel->setWordWrap(true);

    m_MessageLabel->setFont(tokenSet.typography.heading3);
    containerLayout->addWidget(m_MessageLabel);

    layout->addWidget(container, 0, Qt::AlignCenter);

    // Spacer to center content
    layout->addStretch(1);
}

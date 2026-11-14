// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/DashboardIdentityBar.h"
#include "controls/EngineVersionReport.h"
#include "theme/AnDesignTokens.h"
#include "widgets/NightPictureLabel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QCoreApplication>
#include <QStyle>
#include <verdigris/wobjectimpl.h>

#include <cmath>

W_OBJECT_IMPL(an::ui::DashboardIdentityBar)

namespace an::ui
{

DashboardIdentityBar::DashboardIdentityBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("dashboardIdentityBar"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.sm);

    m_AppIcon = new NightPictureLabel(this);
    m_AppIcon->setObjectName(QStringLiteral("dashboardIdentityBarIcon"));
    QPixmap appPixmap(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_256x256_blue.png"));
    if (!appPixmap.isNull())
    {
        const qreal dpr = devicePixelRatioF();
        const int iconSize = static_cast<int>(std::lround(40.0 * dpr));
        QPixmap scaledIcon = appPixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaledIcon.setDevicePixelRatio(dpr);
        m_AppIcon->setPicture(scaledIcon);
    }
    layout->addWidget(m_AppIcon, 0, Qt::AlignVCenter);

    auto* nameColumn = new QVBoxLayout();
    nameColumn->setContentsMargins(0, 0, 0, 0);
    nameColumn->setSpacing(0);

    auto* nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(an::ui::tokens().spacing.sm);

    m_AppName = new QLabel(QStringLiteral("AfterNight"), this);
    m_AppName->setObjectName(QStringLiteral("dashboardIdentityBarName"));
    nameRow->addWidget(m_AppName, 0, Qt::AlignBaseline);

    // The product's version is the application's to state.
    QString versionString = an::ui::productVersion();
    if (versionString.isEmpty())
    {
        versionString = QStringLiteral("dev");
    }
    else if (!versionString.startsWith("v", Qt::CaseInsensitive))
    {
        versionString = QStringLiteral("v") + versionString;
    }

    m_AppVersion = new QLabel(versionString, this);
    m_AppVersion->setObjectName(QStringLiteral("dashboardIdentityBarVersion"));
    nameRow->addWidget(m_AppVersion, 0, Qt::AlignBaseline);
    nameRow->addStretch();
    nameColumn->addLayout(nameRow);

    m_AppTagline = new QLabel(tr("Deep-sky imaging, calibrated and refined"), this);
    m_AppTagline->setObjectName(QStringLiteral("dashboardIdentityBarTagline"));
    nameColumn->addWidget(m_AppTagline);

    layout->addLayout(nameColumn, 1);

    auto createUtilityButton = [this](const QString& toolTip, const QString& objectName) -> QPushButton* {
        auto* button = new QPushButton(this);
        button->setObjectName(objectName);
        button->setProperty("anButtonVariant", QStringLiteral("secondary"));
        button->setProperty("dashboardActionPlacement", QStringLiteral("utility"));
        button->setToolTip(toolTip);
        button->setFixedSize(32, 32);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };

    m_SettingsButton =
        createUtilityButton(tr("Settings (Ctrl+,)"), QStringLiteral("dashboardIdentityBarSettingsButton"));
    layout->addWidget(m_SettingsButton, 0, Qt::AlignVCenter);
    connect(m_SettingsButton, &QPushButton::clicked, this, &DashboardIdentityBar::openSettingsRequested);

    m_HelpButton = createUtilityButton(tr("User Guide (F1)"), QStringLiteral("dashboardIdentityBarHelpButton"));
    layout->addWidget(m_HelpButton, 0, Qt::AlignVCenter);
    connect(m_HelpButton, &QPushButton::clicked, this, &DashboardIdentityBar::openHelpRequested);

    m_AboutButton = createUtilityButton(tr("About"), QStringLiteral("dashboardIdentityBarAboutButton"));
    layout->addWidget(m_AboutButton, 0, Qt::AlignVCenter);
    connect(m_AboutButton, &QPushButton::clicked, this, &DashboardIdentityBar::aboutRequested);

    refreshTheme();
}

void DashboardIdentityBar::setSettingsIcon(const QIcon& icon)
{
    if (m_SettingsButton)
        m_SettingsButton->setIcon(icon);
}

void DashboardIdentityBar::setHelpIcon(const QIcon& icon)
{
    if (m_HelpButton)
        m_HelpButton->setIcon(icon);
}

void DashboardIdentityBar::setAboutIcon(const QIcon& icon)
{
    if (m_AboutButton)
        m_AboutButton->setIcon(icon);
}

void DashboardIdentityBar::refreshTheme()
{
    const auto& tokenSet = an::ui::tokens();

    if (m_AppName)
    {
        m_AppName->setFont(tokenSet.typography.heading1);
        QPalette pal = m_AppName->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textPrimary);
        m_AppName->setPalette(pal);
    }

    if (m_AppVersion)
    {
        m_AppVersion->setFont(tokenSet.typography.heading3);
        QPalette pal = m_AppVersion->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textTertiary);
        m_AppVersion->setPalette(pal);
    }

    if (m_AppTagline)
    {
        m_AppTagline->setFont(tokenSet.typography.caption);
        QPalette pal = m_AppTagline->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        m_AppTagline->setPalette(pal);
    }
}

} // namespace an::ui

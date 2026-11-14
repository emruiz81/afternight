// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <verdigris/wobjectdefs.h>

class QLabel;
class QPushButton;

namespace an::ui
{

class NightPictureLabel;

class AFTERNIGHTCONTROLS_API DashboardIdentityBar : public QWidget
{
    W_OBJECT(DashboardIdentityBar)

public:
    explicit DashboardIdentityBar(QWidget* parent = nullptr);
    ~DashboardIdentityBar() override = default;

    void refreshTheme();
    void setSettingsIcon(const QIcon& icon);
    void setHelpIcon(const QIcon& icon);
    void setAboutIcon(const QIcon& icon);

signals:
    void openSettingsRequested() W_SIGNAL(openSettingsRequested);
    void openHelpRequested() W_SIGNAL(openHelpRequested);
    void aboutRequested() W_SIGNAL(aboutRequested);

private:
    NightPictureLabel* m_AppIcon = nullptr;
    QLabel* m_AppName = nullptr;
    QLabel* m_AppVersion = nullptr;
    QLabel* m_AppTagline = nullptr;

    QPushButton* m_SettingsButton = nullptr;
    QPushButton* m_HelpButton = nullptr;
    QPushButton* m_AboutButton = nullptr;
};

} // namespace an::ui

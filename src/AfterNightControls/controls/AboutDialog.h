// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QDialog>
#include <QString>
#include <verdigris/wobjectdefs.h>

class QLabel;
class QTextBrowser;
class QPushButton;
class QTabWidget;

/// Modern, visually appealing About dialog for AfterNight
class AFTERNIGHTCONTROLS_API AboutDialog : public QDialog
{
    W_OBJECT(AboutDialog)

public:
    explicit AboutDialog(QWidget* parent = nullptr, bool darkMode = false);
    ~AboutDialog() override = default;

private:
    struct ThemePalette
    {
        QString windowBackground;
        QString surfaceBackground;
        QString surfaceBorder;
        QString textPrimary;
        QString textSecondary;
        QString textMuted;
        QString accent;
        QString accentHover;
        QString accentSoft;
        QString highlightBackground;
        QString highlightBorder;
        QString codeBackground;
        QString warningBackground;
        QString warningBorder;
        QString warningText;
        QString dangerBackground;
        QString dangerBorder;
        QString dangerText;
    };

    ThemePalette resolveThemePalette() const;

    void setupUi();
    void setupAboutTab();
    void setupLibrariesTab();
    void setupLicenseTab();

    [[nodiscard]] QString getAboutText() const;
    [[nodiscard]] QString getLibrariesText() const;
    [[nodiscard]] QString getLicenseText() const;

    QTabWidget* m_TabWidget{nullptr};
    QTextBrowser* m_AboutBrowser{nullptr};
    QTextBrowser* m_LibrariesBrowser{nullptr};
    QTextBrowser* m_LicenseBrowser{nullptr};
    QPushButton* m_CloseButton{nullptr};
    bool m_DarkMode{false};
};

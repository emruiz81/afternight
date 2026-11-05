// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"

class QLabel;

/// Placeholder settings page shown for categories that are not yet implemented.
/// Displays a "Coming Soon" message with an icon.
class AFTERNIGHTCONTROLS_API PlaceholderSettingsPage : public SettingsPage
{
    W_OBJECT(PlaceholderSettingsPage)

public:
    /// Create a placeholder page with the given title and icon
    /// @param title The page title to display in navigation
    /// @param icon The icon for the navigation list
    /// @param enabled Whether this page should appear enabled (selectable) in the list
    /// @param parent Parent widget
    PlaceholderSettingsPage(const QString& title, const QIcon& icon, bool enabled = false, QWidget* parent = nullptr);
    ~PlaceholderSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] bool isEnabled() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;

private:
    void setupUi();

    QString m_Title;
    QIcon m_Icon;
    bool m_Enabled;
    QLabel* m_MessageLabel = nullptr;
};

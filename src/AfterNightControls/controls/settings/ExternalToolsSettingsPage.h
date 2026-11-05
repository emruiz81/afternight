// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "SettingsPage.h"

class QLabel;
class PathPickerWidget;
class GlobalSettingsPersistence;

/// External tools settings page.
/// Consolidates configuration for built-in external tools.
class ExternalToolsSettingsPage : public SettingsPage
{
    W_OBJECT(ExternalToolsSettingsPage)

public:
    /// Create the External Tools settings page
    /// @param settingsPersistence Pointer to the settings persistence layer
    /// @param parent Parent widget
    explicit ExternalToolsSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~ExternalToolsSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;

    [[nodiscard]] ValidationResult validateSettings() const override;

private:
    void setupUi();
    void connectSignals();
    void disconnectSignals();
    void resetToDefaultsInternal();
    void triggerAsyncValidation();

    // Detection functions
    [[nodiscard]] QString detectStarnet() const;
    [[nodiscard]] QString detectDeepSNR() const;

    // Status update helpers (synchronous - called on path change)
    void updateStarnetStatus();
    void updateDeepSNRStatus();

    GlobalSettingsPersistence* m_Settings = nullptr;

    // Built-in external tool: StarNet2
    PathPickerWidget* m_StarnetPathPicker = nullptr;

    // Built-in external tool: DeepSNR
    PathPickerWidget* m_DeepSNRPathPicker = nullptr;
};

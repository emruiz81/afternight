// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "SettingsPage.h"

class QComboBox;
class QSpinBox;
class QPushButton;
class PathPickerWidget;
class GlobalSettingsPersistence;

/// Logging settings page.
/// Controls log file location, log level, and rotation settings.
/// Settings are stored in a text config file (logging.conf) that can be read
/// before Qt/engine initialization.
/// Log level changes take effect immediately at runtime.
class LoggingSettingsPage : public SettingsPage
{
    W_OBJECT(LoggingSettingsPage)

public:
    /// Create the Logging settings page
    /// @param settingsPersistence Pointer to the settings persistence layer (unused, kept for interface compatibility)
    /// @param parent Parent widget
    explicit LoggingSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~LoggingSettingsPage() override = default;

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
    void onOpenLogDirectory();

    /// Get the default log directory path
    [[nodiscard]] QString getDefaultLogDirectory() const;

    GlobalSettingsPersistence* m_Settings = nullptr;

    // Log location
    PathPickerWidget* m_LogDirectoryPicker = nullptr;
    QPushButton* m_OpenLogDirButton = nullptr;

    // Log level
    QComboBox* m_LogLevelCombo = nullptr;

    // Log rotation
    QSpinBox* m_MaxFileSizeSpin = nullptr;
    QSpinBox* m_MaxFilesSpin = nullptr;
};

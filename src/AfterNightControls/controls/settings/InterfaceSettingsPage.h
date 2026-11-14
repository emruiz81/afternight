// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <vector>

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"
#include "controls/IAppContext.h"

class QComboBox;
class QGroupBox;
class QSlider;
class QCheckBox;
class QLabel;
class GlobalSettingsPersistence;

/// Interface settings page.
/// Controls theme, zoom behavior, mouse interaction, and layout preferences.
class AFTERNIGHTCONTROLS_API InterfaceSettingsPage : public SettingsPage
{
    W_OBJECT(InterfaceSettingsPage)

public:
    /// Create the Interface settings page
    /// @param settingsPersistence Pointer to the settings persistence layer
    /// @param parent Parent widget
    explicit InterfaceSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~InterfaceSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;
    void refreshStyles() override;

    [[nodiscard]] ValidationResult validateSettings() const override;

    /// The shells a person can choose among. The Shell row is shown only when there is more than one; with one or none
    /// the page writes no shell.
    void setAvailableShells(std::vector<an::ui::ShellOption> shells);

private:
    /// Shows the shell the settings store names, or the classic shell when it names none the page lists.
    void selectShell(const QString& id);

    void setupUi();
    void connectSignals();

    /// Disable the OLED contrast option when the Light theme is selected
    /// and fall back to Balanced if OLED was previously chosen.
    void syncOledAvailability();

    /// Update the sensitivity percentage label based on slider value
    void updateSensitivityLabel();

    GlobalSettingsPersistence* m_Settings = nullptr;

    // Appearance controls
    QComboBox* m_ThemeCombo = nullptr;
    QComboBox* m_ContrastProfileCombo = nullptr;
    QComboBox* m_AccentFamilyCombo = nullptr;
    QComboBox* m_SurfaceTintFamilyCombo = nullptr;
    QCheckBox* m_AnimationsEnabledCheck = nullptr;

    // Image viewing controls
    QSlider* m_ZoomSensitivitySlider = nullptr;
    QLabel* m_ZoomSensitivityLabel = nullptr;
    QLabel* m_SlowLabel = nullptr;
    QLabel* m_FastLabel = nullptr;
    QCheckBox* m_ZoomRequiresCtrlCheck = nullptr;

    // File browser controls
    QCheckBox* m_UseSystemFileBrowserCheck = nullptr;

    // Window layout controls
    QCheckBox* m_LockLayoutByDefaultCheck = nullptr;
    QCheckBox* m_RememberWindowSizeCheck = nullptr;

    // The shell, chosen for the next start
    QGroupBox* m_ShellGroup = nullptr;
    QComboBox* m_ShellCombo = nullptr;
    std::vector<an::ui::ShellOption> m_Shells;
};

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <array>
#include <vector>

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QGroupBox;
class QSlider;
class QCheckBox;
class QLabel;
class GlobalSettingsPersistence;

namespace an::ui
{
class ThemePresetTile;
}

/// Interface settings page.
/// Controls theme, zoom behavior, mouse interaction, and layout preferences.
///
/// Under the Observatory style profile it also offers the colour presets, the tint strength, the density, Night and
/// starting in it, and every appearance row acts at once: what is chosen is put in force as it is chosen, Apply keeps
/// it, and Cancel puts back what was in force. Under the Classic profile those rows are not shown and a choice acts
/// when it is applied, as it always has.
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
    void discardChanges() override;
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

    /// The rows of the Observatory profile: the presets, the strength, the density, Night.
    void setupObservatoryRows(QWidget* parent);

    /// Whether the appearance rows act at once: under the Observatory profile.
    [[nodiscard]] bool previewsAtOnce() const;

    /// The strength and the density the rows choose.
    [[nodiscard]] an::ui::ThemeTintStrength chosenTintStrength() const;
    [[nodiscard]] an::ui::ThemeDensity chosenDensity() const;

    /// The preset the accent, tint and strength rows make.
    [[nodiscard]] an::ui::ThemePreset chosenPreset() const;

    /// Shows a strength and a density on their rows.
    void showTintStrength(an::ui::ThemeTintStrength strength);
    void showDensity(an::ui::ThemeDensity density);

    /// Checks the tile of the preset the rows make, showing the Custom tile when they make none.
    void showPreset();

    /// Sets the accent, the tint and the strength rows to a preset's parts.
    void choosePreset(an::ui::ThemePreset preset);

    /// Puts what the appearance rows choose in force at once, when the page previews.
    void previewAppearance();

    /// A change of an appearance row: marked as unsaved, the preset's tiles shown again, and previewed.
    void appearanceChanged();

    /// What was in force when the page last loaded or applied: what Cancel puts back.
    struct InForce
    {
        an::ui::ThemeContrastProfile contrast = an::ui::ThemeContrastProfile::Balanced;
        an::ui::ThemeAccentFamily accent = an::ui::ThemeAccentFamily::Blue;
        an::ui::ThemeSurfaceTintFamily tint = an::ui::ThemeSurfaceTintFamily::Neutral;
        an::ui::ThemeTintStrength strength = an::ui::ThemeTintStrength::Balanced;
        an::ui::ThemeDensity density = an::ui::ThemeDensity::Compact;
        bool night = false;
        QString theme;
    };

    /// Remembers what is in force now, for Cancel.
    void rememberInForce();

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

    // The Observatory profile's rows
    QGroupBox* m_ObservatoryGroup = nullptr;
    std::array<an::ui::ThemePresetTile*, 7u> m_PresetTiles{};
    QButtonGroup* m_PresetButtons = nullptr;
    QButtonGroup* m_TintStrengthButtons = nullptr;
    QButtonGroup* m_DensityButtons = nullptr;
    QCheckBox* m_NightSwitch = nullptr;
    QCheckBox* m_StartInNightCheck = nullptr;
    InForce m_InForce;
    /// Whether the page is setting its own rows, which is no person's change.
    bool m_Showing = false;
};

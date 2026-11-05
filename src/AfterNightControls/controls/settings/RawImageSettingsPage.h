// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"

class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QWidget;
class GlobalSettingsPersistence;

namespace an::ui
{
struct RawOpenSettings;
} // namespace an::ui

/// Raw image loading settings page.
///
/// The white balance, its custom gains and auto-rotate are the RAW policy every read of a camera file is submitted
/// under: saving the page stores them and publishes their policy through the application's RAW policy service, so the
/// files read afterwards use them and a read on its way keeps the policy it was asked under. The demosaic method is the
/// one setting every open path demosaics a mosaic with.
class AFTERNIGHTCONTROLS_API RawImageSettingsPage : public SettingsPage
{
    W_OBJECT(RawImageSettingsPage)

public:
    /// Create the Raw Image settings page
    /// @param settingsPersistence Pointer to the settings persistence layer
    /// @param parent Parent widget
    explicit RawImageSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~RawImageSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;

    [[nodiscard]] ValidationResult validateSettings() const override;

    /// The RAW settings the page's controls state now.
    [[nodiscard]] an::ui::RawOpenSettings stagedSettings() const;

private:
    void setupUi();
    void connectSignals();
    void updateCustomWbEnabled();
    void showSettings(const an::ui::RawOpenSettings& settings);

    GlobalSettingsPersistence* m_Settings = nullptr;

    // White Balance controls
    QComboBox* m_WhiteBalanceModeCombo = nullptr;
    QWidget* m_CustomWbContainer = nullptr;
    QDoubleSpinBox* m_CustomWbR = nullptr;
    QDoubleSpinBox* m_CustomWbG = nullptr;
    QDoubleSpinBox* m_CustomWbB = nullptr;

    // Behavior controls
    QCheckBox* m_AutoRotateCheck = nullptr;

    // Demosaic controls
    QComboBox* m_DemosaicAlgorithmCombo = nullptr;
};

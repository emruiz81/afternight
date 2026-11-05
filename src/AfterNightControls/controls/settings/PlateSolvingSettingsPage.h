// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "SettingsPage.h"
#include "app/PlateSolveTypes.h"

class QCheckBox;
class QLineEdit;
class QLabel;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class PathPickerWidget;
class GlobalSettingsPersistence;

/// Plate solving settings page.
/// Configures plate solving tools (ASTAP, Astrometry.net) and solver parameters.
class PlateSolvingSettingsPage : public SettingsPage
{
    W_OBJECT(PlateSolvingSettingsPage)

public:
    explicit PlateSolvingSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~PlateSolvingSettingsPage() override = default;

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
    void refreshDefaultSolverOptions(const QString& preferredSolver = QString());
    [[nodiscard]] an::ui::PlateSolveSettings currentUiSettings() const;

    // Detection functions
    [[nodiscard]] QString detectAstap() const;
    [[nodiscard]] QString detectAstrometryLocal() const;

    // Status update helpers
    void updateAstapStatus();
    void updateAstrometryLocalStatus();

    GlobalSettingsPersistence* m_Settings = nullptr;

    // General
    QComboBox* m_DefaultSolverCombo = nullptr;
    QDoubleSpinBox* m_SearchRadiusSpin = nullptr;
    QSpinBox* m_DownsampleSpin = nullptr;
    QSpinBox* m_TimeoutSpin = nullptr;

    // ASTAP
    PathPickerWidget* m_AstapPathPicker = nullptr;

    // Astrometry.net
    PathPickerWidget* m_AstrometryLocalPathPicker = nullptr;
    PathPickerWidget* m_IndexPathPicker = nullptr;
    QLineEdit* m_AstrometryApiKeyEdit = nullptr;
};

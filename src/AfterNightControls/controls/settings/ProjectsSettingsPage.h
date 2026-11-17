// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Projects page of the Settings dialog.

#include "AfterNightControlsExport.h"
#include "ProjectSettings.h"
#include "SettingsPage.h"

class GlobalSettingsPersistence;
class PathPickerWidget;
class QCheckBox;
class QComboBox;
class QSpinBox;

namespace an::ui
{

/// What a new project starts from, and how a history is written: the folder a new project is offered in; whether an
/// integration job run with no project open makes one in its output folder; how the steps of a history are compressed
/// - LZ4, none or Zstandard - which every history written from then on takes, in a project or not, and which the New
/// Project dialog starts from; above how many gigabytes a project's history is asked to be compacted; and whether the
/// project used last is opened as the application starts. A note says what holds without a project: the history is
/// kept in the temporary folder and cleared on exit.
///
/// The page shows what the settings store states (`readProjectSettings`) and stores what it shows
/// (`writeProjectSettings`); a location that is empty or is no folder's path is refused when the dialog applies.
class AFTERNIGHTCONTROLS_API ProjectsSettingsPage : public ::SettingsPage
{
    W_OBJECT(ProjectsSettingsPage)

public:
    /// @param settings the settings store; null, the page shows the defaults and stores nothing.
    explicit ProjectsSettingsPage(GlobalSettingsPersistence* settings, QWidget* parent = nullptr);
    ~ProjectsSettingsPage() override = default;

    /// The page's title, which the Settings dialog opens it by.
    [[nodiscard]] static QString title();

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;

    [[nodiscard]] ValidationResult validateSettings() const override;

    /// What the page shows now.
    [[nodiscard]] ProjectSettings shownSettings() const;

    [[nodiscard]] PathPickerWidget* locationPicker() const noexcept;
    [[nodiscard]] QCheckBox* createForIntegrationJobsCheck() const noexcept;
    [[nodiscard]] QComboBox* compressionCombo() const noexcept;
    [[nodiscard]] QSpinBox* compactAskThresholdSpin() const noexcept;
    [[nodiscard]] QCheckBox* openLastOnStartupCheck() const noexcept;

private:
    void setupUi();
    void showSettings(const ProjectSettings& settings);

    GlobalSettingsPersistence* m_settings = nullptr;
    PathPickerWidget* m_location = nullptr;
    QCheckBox* m_createForJobs = nullptr;
    QComboBox* m_compression = nullptr;
    QSpinBox* m_compactThreshold = nullptr;
    QCheckBox* m_openLast = nullptr;
};

} // namespace an::ui

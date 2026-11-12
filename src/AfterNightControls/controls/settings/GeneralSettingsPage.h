// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "SettingsPage.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QSaveFile>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class PathPickerWidget;
class GlobalSettingsPersistence;

namespace an::platform
{
struct LoggingConfig;
} // namespace an::platform

namespace an::ui::settings
{
/// Applies a logging configuration to the running logger and says, for the person who set it, why it could not be
/// applied. A log folder that cannot be written leaves the log in the file it was in, and until now only a line of
/// that log said so: both settings pages that apply a configuration say it instead.
///
/// @param config the configuration to apply.
/// @return nothing when it was applied; else the sentence to show, naming the folder.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString applyLoggingConfigOrSayWhy(const an::platform::LoggingConfig& config);

/// Says a problem of the logging configuration to the person who set it, through the application's dialogs, without
/// waiting for an answer; nothing for an empty problem.
///
/// @param parent the page the problem belongs to.
/// @param problem the sentence `applyLoggingConfigOrSayWhy` gave.
AFTERNIGHTCONTROLS_API void sayLoggingConfigProblem(QWidget* parent, const QString& problem);

[[nodiscard]] inline bool containsRecognizedImmediateImportSection(const QJsonObject& root)
{
    return root.value(QStringLiteral("logging")).isObject();
}

[[nodiscard]] inline bool writeSettingsJsonFile(const QString& filePath, const QByteArray& contents,
                                                QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }

    if (file.write(contents) != contents.size())
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        file.cancelWriting();
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
}
} // namespace an::ui::settings

/// General application settings page.
/// Includes language, save format, temp files, and settings import/export.
class AFTERNIGHTCONTROLS_API GeneralSettingsPage : public SettingsPage
{
    W_OBJECT(GeneralSettingsPage)

public:
    /// Create the General settings page
    /// @param settingsPersistence Pointer to the settings persistence layer
    /// @param parent Parent widget
    explicit GeneralSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~GeneralSettingsPage() override = default;

    [[nodiscard]] QString pageTitle() const override;
    [[nodiscard]] QIcon pageIcon() const override;
    [[nodiscard]] QStringList searchKeywords() const override;

    void loadSettings() override;
    void saveSettings() override;
    void resetToDefaults() override;

    [[nodiscard]] ValidationResult validateSettings() const override;

    /// Stages the settings a settings file states: the General controls take its values and every other recognized
    /// section is held until the page is saved, except Logging, which is written and applied at once. Import Settings
    /// reads the file and calls this.
    ///
    /// @param root the file's JSON object.
    /// @return whether the file stated any setting the page recognizes.
    bool stageImportedSettings(const QJsonObject& root);

    /// Reads a settings file and stages what it states, as Import Settings does after the file is chosen; a file that
    /// cannot be read, or that states no setting the page recognizes, stages nothing.
    ///
    /// @param filePath the file.
    /// @param error receives why nothing was staged.
    /// @return whether the file was read and staged.
    bool importSettingsFile(const QString& filePath, QString* error);

    /// Every setting the page exports, as Export Settings writes them to a settings file.
    [[nodiscard]] QJsonObject exportedSettings() const;

private:
    void setupUi();
    void connectSignals();

    // Settings import/export
    void onImportSettings();
    void onExportSettings();

    // Get default temp location (platform-specific)
    [[nodiscard]] QString getDefaultTempLocation() const;

    GlobalSettingsPersistence* m_Settings = nullptr;

    // UI Controls
    QComboBox* m_LanguageCombo = nullptr;
    QComboBox* m_DefaultSaveFormatCombo = nullptr;
    QSpinBox* m_RecentFilesLimitSpin = nullptr;
    QSpinBox* m_RecentProcessesLimitSpin = nullptr;
    QCheckBox* m_ConfirmCloseUnsavedCheck = nullptr;
    PathPickerWidget* m_TempLocationPicker = nullptr;
    QCheckBox* m_ClearTempOnExitCheck = nullptr;
    QCheckBox* m_CheckAppUpdatesOnStartupCheck = nullptr;
    QCheckBox* m_CheckExtensionUpdatesOnStartupCheck = nullptr;
    QPushButton* m_ImportButton = nullptr;
    QPushButton* m_ExportButton = nullptr;

    QHash<QString, QString> m_PendingImportUpserts;
};

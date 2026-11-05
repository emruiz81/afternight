// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/ExtensionManager.h"
#include "app/ExtensionSdkService.h"
#include "controls/ExtensionUiSchemaUtils.h"
#include "controls/ExtensionUiWidgetHost.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QCborMap>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <verdigris/wobjectdefs.h>

class GlobalSettingsPersistence;
class QDialogButtonBox;
class QProgressBar;
class QVBoxLayout;
template <typename T> class QFutureWatcher;

namespace an::ui
{
class AnInfoBanner;
}

class AFTERNIGHTCONTROLS_API ExtensionSettingsSection : public QWidget
{
    W_OBJECT(ExtensionSettingsSection)

public:
    struct SettingField
    {
        QString id;
        QString type;
        QString label;
        QString group;
        QString tooltip;
        QString visible_when;
        QString enabled_when;
        QString group_style;
        QCborValue default_value;
        bool visible = true;
        bool enabled = true;
        bool secret = false;
        bool sensitive = false;
        bool persist = true;
        bool action = false;
        bool autorun = false;
        bool show_status = true;
        int timeout_ms = 60000;
        QJsonObject definition;
    };

    struct ExternalToolConfiguration
    {
        bool enabled = false;
        bool append_platform_suffix = true;
        bool require_executable = true;
        QString setting_key;
        QString label;
        QString not_configured_text;
        QString configured_text;
        QString invalid_text;
        QString primary_executable;
        QStringList executable_candidates;
        QStringList required_files;
        QStringList candidate_directories;
        QStringList candidate_subdirectories;
        QStringList version_arguments;
        QString version_pattern;
        QCborMap resolved_record;
        bool show_configured_banner = true;
    };

    struct ExternalToolResolution
    {
        bool valid = false;
        QString configured_folder;
        QString executable;
        QString source;
        QString version;
        QString diagnostic;
    };

    using ParsedCondition = an::ui::extensions::schema_utils::SchemaCondition;
    using SettingsActionRunner = std::function<an::ui::extensions::ExtensionSettingsActionResult(
        const an::ui::extensions::ExtensionSettingsActionRequest&)>;

    explicit ExtensionSettingsSection(GlobalSettingsPersistence* settingsPersistence, QWidget* parent = nullptr);
    ~ExtensionSettingsSection() override = default;

    [[nodiscard]] bool bindExtension(const an::ui::extensions::ExtensionLoadResult& result,
                                     std::string* error = nullptr);
    [[nodiscard]] bool hasSchema() const noexcept;
    [[nodiscard]] an::ui::extensions::ExtensionUiWidgetHost* widgetHost() const noexcept;
    [[nodiscard]] std::string extensionId() const noexcept;

    void loadPersistedValues();
    [[nodiscard]] bool saveSettings(std::string* error = nullptr);
    void resetToDefaults();
    void refreshTheme();
    void setSettingsActionRunner(SettingsActionRunner runner);
    [[nodiscard]] bool actionInProgress() const noexcept;

    void settingsModified() W_SIGNAL(settingsModified);

private:
    [[nodiscard]] bool buildDescription(const QJsonArray& schema,
                                        an::ui::extensions::ExtensionUiDescription* out_description,
                                        std::string* error = nullptr) const;
    [[nodiscard]] bool buildDescriptionForFields(const std::vector<SettingField>& fields, const QString& session_id,
                                                 const QString& request_id,
                                                 an::ui::extensions::ExtensionUiDescription* out_description,
                                                 std::string* error = nullptr) const;
    [[nodiscard]] bool loadWidgetState(bool use_defaults, std::string* error = nullptr);
    [[nodiscard]] bool evaluateAndApplyConditions(std::string* error = nullptr);
    [[nodiscard]] static std::optional<ParsedCondition> parseCondition(const QString& expression);
    [[nodiscard]] static bool evaluateCondition(const ParsedCondition& condition, const QCborMap& params);
    [[nodiscard]] const SettingField* fieldById(const QString& id) const;
    [[nodiscard]] bool shouldPersistField(const SettingField& field) const noexcept;
    [[nodiscard]] QCborMap actionSnapshot(std::string* error = nullptr) const;
    [[nodiscard]] QString redactSensitiveText(QString text, const QCborMap& snapshot) const;
    [[nodiscard]] QCborValue redactSensitiveValue(const QCborValue& value, const QCborMap& snapshot) const;
    void setActionWidgetsEnabled(bool enabled);
    void resetExternalToolConfiguration();
    void parseExternalToolConfiguration(const QJsonObject& config);
    void refreshExternalToolConfiguration();
    [[nodiscard]] QString currentExternalToolPath(const QCborMap& params) const;
    [[nodiscard]] QString currentExternalToolDirectory(const QCborMap& params) const;
    [[nodiscard]] QString platformAdjustedRequiredFile(const QString& fileName) const;
    [[nodiscard]] QString externalToolExecutableInDirectory(const QString& directory) const;
    [[nodiscard]] bool validateExternalToolExecutable(const QString& executablePath, QString* outMessage) const;
    [[nodiscard]] bool validateExternalToolDirectory(const QString& directory, QString* outMessage) const;
    [[nodiscard]] ExternalToolResolution resolveExternalTool(const QCborMap& params) const;
    [[nodiscard]] QString queryExternalToolVersion(const QString& executablePath) const;
    void applyExternalToolRecordToFields(const QCborMap& record);
    void startAutorunSettingsActions();
    [[nodiscard]] std::vector<SettingField> dialogFieldsForAction(const SettingField& field, const QCborMap& snapshot,
                                                                  std::string* error = nullptr) const;
    void openSettingsActionDialog(const SettingField& field);
    void startSettingsAction(const SettingField& field, std::optional<QCborMap> snapshot_override = std::nullopt);
    void applySettingsActionUpdate(const an::ui::extensions::ExtensionSettingsActionResult& result);
    void finishSettingsAction(const an::ui::extensions::ExtensionSettingsActionResult& result,
                              const QCborMap& snapshot);
    void applyActionUpdates(const QCborMap& updates, bool persist_updates, const QCborMap& snapshot,
                            std::string* error);
    void applyActionUpdatesToHost(an::ui::extensions::ExtensionUiWidgetHost* host,
                                  const std::vector<SettingField>& fields, const QCborMap& updates,
                                  bool persist_updates, bool persist_to_settings, const QCborMap& snapshot,
                                  std::string* error);
    void clearTransientActionFields(an::ui::extensions::ExtensionUiWidgetHost* host,
                                    const std::vector<SettingField>& fields);
    void clearTransientActionFields();

    void onWidgetEvent(const QString& widgetId, const QString& eventName, const QCborValue& value, bool coalesced);

    GlobalSettingsPersistence* settings_persistence_ = nullptr;
    an::ui::extensions::ExtensionSdkService sdk_service_;
    QVBoxLayout* root_layout_ = nullptr;
    an::ui::AnInfoBanner* action_status_banner_ = nullptr;
    QProgressBar* action_progress_bar_ = nullptr;
    an::ui::AnInfoBanner* tool_status_banner_ = nullptr;
    an::ui::extensions::ExtensionUiWidgetHost* widget_host_ = nullptr;
    an::ui::extensions::ExtensionLoadResult load_result_;
    SettingsActionRunner settings_action_runner_;
    QFutureWatcher<an::ui::extensions::ExtensionSettingsActionResult>* action_watcher_ = nullptr;
    an::ui::extensions::ExtensionOperationCancellationToken action_cancellation_token_;
    QCborMap active_action_snapshot_;
    QString active_action_id_;
    int active_action_generation_ = 0;
    bool active_action_show_status_ = true;
    QPointer<an::ui::extensions::ExtensionUiWidgetHost> active_action_dialog_host_;
    QPointer<QDialogButtonBox> active_action_dialog_buttons_;
    std::vector<SettingField> active_action_dialog_fields_;
    std::string extension_id_;
    QString extension_name_;
    QString object_token_;
    std::vector<SettingField> fields_;
    ExternalToolConfiguration external_tool_config_;
    bool suppress_events_ = false;
    int patch_sequence_ = 0;
};

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionManager.h"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class GlobalSettingsPersistence;

namespace an::ui::extensions
{

struct ExtensionLogEvent
{
    std::string extension_id;
    std::string session_id;
    std::string request_id;
    std::string severity;
    std::string component;
    std::string message;
    QCborValue payload;
};

struct ExtensionDiagnosticCheckpoint
{
    std::string extension_id;
    std::string session_id;
    std::string request_id;
    std::string name;
    std::string detail;
    QCborValue fields;
};

struct ExtensionViewState
{
    std::string view_name;
    std::string file_name;
    std::string selector_tooltip;
    std::string view_mode;
    std::string image_display_mode;
    std::string channel_view_mode;
    std::string mask_rendering_mode;
    int width = 0;
    int height = 0;
    int channels = 0;
    double zoom_percent = 0.0;
    int viewport_offset_x = 0;
    int viewport_offset_y = 0;
    bool is_active = false;
    bool has_image = false;
    bool has_sequence = false;
    bool has_mask = false;
    bool mask_enabled = false;
    bool show_mask = false;
    bool mask_inverted = false;
    bool has_alternate_image = false;
    bool selection_area_enabled = false;
    int selection_area_x = 0;
    int selection_area_y = 0;
    int selection_area_width = 0;
    int selection_area_height = 0;
};

struct ExtensionThemeSnapshot
{
    std::string theme_name;
    QCborMap tokens;
};

struct ExtensionViewSnapshot
{
    SharedBufferDescriptor source_buffer;
    std::vector<SharedBufferDescriptor> auxiliary_buffers;
};

using ExtensionSdkViewStateProvider = std::function<std::vector<ExtensionViewState>()>;
using ExtensionSdkViewSnapshotProvider =
    std::function<bool(std::string_view session_id, std::string_view request_id, std::string_view view_name,
                       ExtensionViewSnapshot* out_snapshot, std::string* error)>;
using ExtensionSdkViewBindingHandler =
    std::function<bool(std::string_view session_id, std::string_view view_name, std::string* error)>;
using ExtensionSdkThemeSnapshotProvider = std::function<ExtensionThemeSnapshot()>;
using ExtensionSdkUiNotificationHandler =
    std::function<bool(std::string_view session_id, std::string_view request_id, const QString& title,
                       const QString& message, std::string* error)>;
using ExtensionSdkUiConfirmCompletion = std::function<void(bool confirmed, const std::string& error)>;
using ExtensionSdkUiConfirmHandler = std::function<bool(
    std::string_view session_id, std::string_view request_id, const QString& title, const QString& message, bool warn,
    ExtensionSdkUiConfirmCompletion completion, std::string* error)>;
using ExtensionSdkOpenExtensionSettingsCompletion = std::function<void(const std::string& error)>;
using ExtensionSdkOpenExtensionSettingsHandler =
    std::function<bool(std::string_view session_id, std::string_view request_id, std::string_view extension_id,
                       ExtensionSdkOpenExtensionSettingsCompletion completion, std::string* error)>;
using ExtensionSdkUiOpenImageHandler =
    std::function<bool(std::string_view session_id, std::string_view request_id, const SharedBufferDescriptor& buffer,
                       const QString& title, std::string* error)>;

struct ExtensionSdkUiInteractionHandler
{
    ExtensionSdkUiNotificationHandler show_error;
    ExtensionSdkUiNotificationHandler show_warning;
    ExtensionSdkUiNotificationHandler show_info;
    ExtensionSdkUiConfirmHandler confirm;
    ExtensionSdkOpenExtensionSettingsHandler open_extension_settings;
};

struct ExtensionUiDescription
{
    std::string extension_id;
    std::string session_id;
    std::string request_id;
    QString root_widget;
    int widget_count = 0;
    QStringList widget_ids;
    bool supports_ui_events = true;
    bool supports_ui_validation = true;
    QCborMap tree;
};

struct ExtensionUiPatchOperation
{
    QString widget_id;
    QString property_name;
    QCborValue value;
};

struct ExtensionUiPatchState
{
    std::string extension_id;
    std::string session_id;
    std::string request_id;
    int patch_count = 0;
    std::vector<ExtensionUiPatchOperation> operations;
};

struct ExtensionSdkNamespaceAvailability
{
    std::string namespace_name;
    bool available = false;
    std::string diagnostic;
};

struct ExtensionWorkflowOutput
{
    std::string extension_id;
    std::string session_id;
    std::string request_id;
    std::string kind;
    std::filesystem::path path;
};

using ExtensionWorkflowOutputHandler = std::function<bool(
    std::string_view session_id, const std::vector<ExtensionWorkflowOutput>& outputs, std::string* error)>;

class AFTERNIGHTCONTROLS_API ExtensionSdkService
{
public:
    explicit ExtensionSdkService(GlobalSettingsPersistence* global_settings_persistence = nullptr);

    void setGlobalSettingsPersistence(GlobalSettingsPersistence* global_settings_persistence);
    void setViewStateProvider(ExtensionSdkViewStateProvider view_state_provider);
    void setViewSnapshotProvider(ExtensionSdkViewSnapshotProvider view_snapshot_provider);
    void setViewBindingHandler(ExtensionSdkViewBindingHandler view_binding_handler);
    void setThemeSnapshotProvider(ExtensionSdkThemeSnapshotProvider theme_snapshot_provider);
    void setUiInteractionHandler(ExtensionSdkUiInteractionHandler ui_interaction_handler);
    void setUiOpenImageHandler(ExtensionSdkUiOpenImageHandler ui_open_image_handler);
    void setWorkflowOutputHandler(ExtensionWorkflowOutputHandler workflow_output_handler);

    [[nodiscard]] bool buildThemeChangedMessage(std::string_view session_id, ThemeChangedMessage* out_message,
                                                bool* out_has_message, std::string* error = nullptr) const;
    [[nodiscard]] bool describeActiveViewState(ExtensionViewState* out_view_state, bool* out_has_view_state,
                                               std::string* error = nullptr) const;
    [[nodiscard]] bool describeViewState(std::string_view view_name, ExtensionViewState* out_view_state,
                                         bool* out_has_view_state, std::string* error = nullptr) const;
    [[nodiscard]] bool buildInitialThemeDescribeMessage(std::string_view session_id, ThemeDescribeMessage* out_message,
                                                        bool* out_has_message, std::string* error = nullptr) const;
    [[nodiscard]] bool buildInitialViewDescribeMessage(std::string_view session_id, ViewDescribeMessage* out_message,
                                                       bool* out_has_message, std::string* error = nullptr) const;
    [[nodiscard]] bool buildInitialViewDescribeMessage(std::string_view session_id,
                                                       std::string_view preferred_view_name,
                                                       ViewDescribeMessage* out_message, bool* out_has_message,
                                                       std::string* error = nullptr) const;
    [[nodiscard]] bool buildViewUpdatedMessage(std::string_view session_id, std::string_view view_id,
                                               std::string_view view_name, ViewUpdatedMessage* out_message,
                                               bool* out_has_message, std::string* error = nullptr) const;

    [[nodiscard]] bool upsertExtensionSetting(std::string_view extension_id, std::string_view setting_key,
                                              const QString& value, std::string* error = nullptr);
    [[nodiscard]] bool upsertExtensionSetting(std::string_view extension_id, std::string_view setting_key,
                                              const QCborValue& value, std::string* error = nullptr);
    [[nodiscard]] bool selectExtensionSetting(std::string_view extension_id, std::string_view setting_key,
                                              QString* out_value, const QString& default_value = {},
                                              std::string* error = nullptr);
    [[nodiscard]] bool selectExtensionSetting(std::string_view extension_id, std::string_view setting_key,
                                              QCborValue* out_value, const QCborValue& default_value = QCborValue(),
                                              std::string* error = nullptr);
    [[nodiscard]] bool removeExtensionSetting(std::string_view extension_id, std::string_view setting_key,
                                              std::string* error = nullptr);
    [[nodiscard]] bool selectExtensionSettingKeys(std::string_view extension_id, QStringList* out_keys,
                                                  std::string* error = nullptr);
    [[nodiscard]] bool dispatchProtocolMessage(const ExtensionProtocolMessage& request, std::string_view extension_id,
                                               ExtensionLaunchMode launch_mode, ExtensionProtocolMessage* response,
                                               std::string* error = nullptr);
    // Must be called from the main thread (pump timer context).
    [[nodiscard]] std::vector<ExtensionProtocolMessage> takeDeferredProtocolResponses(std::string_view session_id);
    [[nodiscard]] const std::vector<ExtensionLogEvent>& recentLogEvents() const noexcept;
    [[nodiscard]] const std::vector<ExtensionDiagnosticCheckpoint>& diagnosticCheckpoints() const noexcept;
    [[nodiscard]] const std::vector<ExtensionWorkflowOutput>& workflowOutputs() const noexcept;
    [[nodiscard]] bool presentWorkflowOutputs(std::string_view session_id, std::string* error = nullptr);
    [[nodiscard]] bool discardWorkflowSession(std::string_view session_id, std::string* error = nullptr);
    [[nodiscard]] const std::optional<ExtensionUiDescription>& latestUiDescription() const noexcept;
    [[nodiscard]] const std::optional<ExtensionUiPatchState>& latestUiPatchState() const noexcept;

    [[nodiscard]] static ExtensionSdkNamespaceAvailability
    resolveNamespaceAvailability(std::string_view message_type, ExtensionLaunchMode launch_mode);
    [[nodiscard]] static bool buildExtensionSettingStorageKey(std::string_view extension_id,
                                                              std::string_view setting_key, QString* out_key,
                                                              std::string* error = nullptr);

private:
    struct DeferredProtocolResponse
    {
        std::string session_id;
        ExtensionProtocolMessage message;
    };

    struct WorkflowSessionState
    {
        std::filesystem::path working_dir;
        std::filesystem::path temp_dir;
        std::filesystem::path output_dir;
    };

    // Called only from the main thread (pump timer context). The deferred
    // response vector has no internal synchronization.
    void enqueueDeferredProtocolResponse(std::string_view session_id, ExtensionProtocolMessage message);
    [[nodiscard]] bool ensurePersistenceReady(std::string* error = nullptr);
    [[nodiscard]] bool collectViewStates(std::vector<ExtensionViewState>* out_views,
                                         std::string* error = nullptr) const;
    [[nodiscard]] bool exportViewSnapshot(std::string_view session_id, std::string_view request_id,
                                          std::string_view view_name, ExtensionViewSnapshot* out_snapshot,
                                          std::string* error = nullptr) const;
    WorkflowSessionState& workflowSessionState(std::string_view session_id);
    [[nodiscard]] bool cleanupWorkflowSessionState(std::string_view session_id, bool remove_outputs,
                                                   std::string* error = nullptr);

    GlobalSettingsPersistence* global_settings_persistence_ = nullptr;
    bool persistence_initialized_ = false;
    ExtensionSdkViewStateProvider view_state_provider_;
    ExtensionSdkViewSnapshotProvider view_snapshot_provider_;
    ExtensionSdkViewBindingHandler view_binding_handler_;
    ExtensionSdkThemeSnapshotProvider theme_snapshot_provider_;
    ExtensionSdkUiInteractionHandler ui_interaction_handler_;
    ExtensionSdkUiOpenImageHandler ui_open_image_handler_;
    ExtensionWorkflowOutputHandler workflow_output_handler_;
    std::vector<ExtensionLogEvent> recent_log_events_;
    std::vector<ExtensionDiagnosticCheckpoint> diagnostic_checkpoints_;
    std::vector<ExtensionWorkflowOutput> workflow_outputs_;
    std::map<std::string, WorkflowSessionState> workflow_sessions_;
    std::optional<ExtensionUiDescription> latest_ui_description_;
    std::optional<ExtensionUiPatchState> latest_ui_patch_state_;
    std::vector<DeferredProtocolResponse> deferred_protocol_responses_;
};

} // namespace an::ui::extensions

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "HostPreviewFrames.h"
#include "controls/ExtensionUiSchemaUtils.h"
#include "controls/ImageRTPreviewControl.h"
#include "components/AnInfoBanner.h"
#include "widgets/CurvesWidget.h"
#include "widgets/HistogramWidget.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <QList>
#include <QMetaObject>
#include <QRect>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <verdigris/wobjectdefs.h>

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

class ImageViewControl;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QEvent;
class QFileDialog;
class QBoxLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QProcess;
class QScrollArea;
class QSplitter;
class QSpinBox;
class QToolButton;
class QDialogButtonBox;
class QHBoxLayout;
class QShowEvent;
class QVBoxLayout;
class QWidget;

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui
{
class HistogramCountScheduler;
} // namespace an::ui

namespace an::ui::drawing
{
class DrawingCanvasController;
class DrawingDocument;
class DrawingEditQueue;
} // namespace an::ui::drawing

namespace an::ui::extensions
{

/// Standalone process window hosted in the **extension host** process.
///
/// When the extension host advertises `native_process_window = true` in its
/// runtime.hello message and the host mirrors it in the ack, the main
/// application skips creating an `ExtensionProcessControl` for this session.
/// Instead this window — shown directly from the extension host process — owns
/// the visible process UI surface.
///
/// The contents area hosts the preview view plus the shipped ParamDef-driven native parameter panel exposed through
/// `ProcessExtension.get_params()`.
///
/// **Its preview is the framework's.** The window is a real-time preview window whose target is the host's mirror of
/// the image the application bound (`setSourceView`), and whose body hands each frame's source to the extension's
/// `execute_preview` (`setPreviewBody`) and shows what it made: the frames a drag asks for and the settled one that
/// follows it, paced at the Processing page's rate, run as one operation each and are shown through the preview view's
/// presentation, as every native process window's are. What an interactive frame may run the extension over is the
/// extension's manifest's to declare (`setPreviewScaling`): the whole image, unless it states its body maps every
/// pixel alone or reads no farther than a reach it states. An Apply runs the extension's `execute` over the mirror's
/// own image, never over a preview's.
class ExtensionNativeProcessWindow : public ImageRTPreviewControl, public CurvesWidgetListener
{
    W_OBJECT(ExtensionNativeProcessWindow)
public:
    /// Callback invoked when the user presses the Execute button.
    using ExecuteCallback = std::function<void()>;

    /// A ParamDef action's callback that answers at once with the parameters to update.
    using ParamActionCallback = std::function<QCborMap(const std::string& action_id, const QCborMap& params)>;

    /// What a ParamDef action's callback answers through later: called once, on the GUI thread, with the parameters to
    /// update; the window refuses another action until it is called.
    using ParamActionCompletion = std::function<void(QCborMap updates)>;

    /// A ParamDef action's callback that answers later - once the frame on its way has landed, say - through the
    /// completion it is handed. It refuses nothing of the window's meanwhile: the window does.
    using AsyncParamActionCallback =
        std::function<void(const std::string& action_id, const QCborMap& params, ParamActionCompletion done)>;

    /// Callback invoked after native parameter values change.
    using ParamChangedCallback =
        std::function<void(const QCborMap& params, const QStringList& changed_keys, const QString& reason)>;

    /// Callback invoked when helper UI should open the extension's full settings dialog.
    using OpenSettingsCallback = std::function<bool(std::string* error)>;

    /// Callback invoked when the window is closed (via button or OS close).
    using CloseCallback = std::function<void()>;

    /// Callback invoked when the user cancels an active native execution.
    using CancelCallback = std::function<void()>;

    /// Callback invoked when the target image selector changes.
    using TargetChangedCallback = std::function<void(const std::string& view_name)>;

    /// Callback invoked when helper UI reads an extension-scoped setting.
    using SettingsGetCallback = std::function<QCborValue(const QString& key, const QCborValue& default_value)>;

    /// Callback invoked when helper UI writes an extension-scoped setting.
    using SettingsSetCallback = std::function<bool(const QString& key, const QCborValue& value, std::string* error)>;

    /// @param display_name  Human-readable extension name, used as the window title.
    /// @param session_id    Active session identifier — kept for diagnostics.
    /// @param source_view   Session source ImageViewControl (not reparented here; may be null).
    ExtensionNativeProcessWindow(const std::string& display_name, const std::string& session_id,
                                 ImageViewControl* source_view);

    /// Refuses the frames on their way; a frame whose worker still runs must have landed first, which the host's
    /// teardown waits for.
    ~ExtensionNativeProcessWindow() override;

    void refreshTheme() override;

    void showProcessMessage(const QString& title, const QString& message,
                            ProcessMessageTone tone = ProcessMessageTone::Info,
                            std::optional<double> progressPercent = std::nullopt);
    void setHeaderProgressVisible(bool visible);
    [[nodiscard]] bool isHeaderProgressVisible() const noexcept
    {
        return header_progress_visible_;
    }

    /// Set the callback invoked when the Execute button is clicked.
    /// The execute button is only enabled when a non-null callback is installed.
    void setExecuteCallback(ExecuteCallback callback);

    /// Set the callback invoked when this window is closed.
    void setCloseCallback(CloseCallback callback);

    /// Closes the window as its Close button does, whether or not it was ever shown. Qt sends no close event to a
    /// top-level window that has a window handle and no platform window, which a window an extension hid before it
    /// was first shown can be, so `close()` alone can leave the session running.
    void requestClose();

    /// Set the callback invoked when the Close button is acting as Cancel.
    void setCancelCallback(CancelCallback callback);

    /// Remove callbacks and disable execution/preview behavior.
    void clearCallbacks();

    /// Configure the button bar for keep-open mode. When enabled, "Apply"
    /// resets params and keeps the window open. When disabled (default),
    /// "Apply" commits and closes.
    void setKeepOpenMode(bool keep_open);

    /// Sets the extension's preview: the window previews while it has a body, a source view showing an image and its
    /// view area shown, through a preview view of its own, and runs the body over every frame's source on the frame's
    /// worker. Null previews nothing.
    ///
    /// @param body the body; it is copied into every frame issued from now on, so it must outlive every frame that may
    ///        still run it.
    void setPreviewBody(ExtensionPreviewBody body);

    /// Whether the window has a preview body, so it previews once it has an image.
    [[nodiscard]] bool hasPreviewBody() const noexcept
    {
        return static_cast<bool>(preview_body_);
    }

    /// States what an interactive frame may run the extension over, as its manifest declares it:
    /// `PreviewScaling::FullOnly`, the default, every frame over the whole image; `PreviewScaling::Decimated` for a
    /// body that maps every pixel alone whatever the image's size, which then runs over what the preview view paints;
    /// `PreviewScaling::NativeRegion` for one whose answer at a pixel reads nothing farther than `regionMargin` pixels
    /// around it, which then runs at full scale over the part of the image the view shows, grown by that reach.
    ///
    /// @param scaling the scaling.
    /// @param regionMargin how far a pixel's answer reads around it; read for a body over a part of the image.
    void setPreviewScaling(an::ui::PreviewScaling scaling, std::uint32_t regionMargin = 0u);

    /// What an interactive frame may run the extension over, as `setPreviewScaling` stated it.
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;

    /// True: the extension's preview asks the engine for work of its own through its SDK.
    [[nodiscard]] bool bodyAdmitsWorkOfItsOwn() const override;

    /// Set the callback invoked by native ParamDef action buttons: one that answers at once, or one that answers later
    /// through the completion it is handed. The window applies the updates when the answer comes, asks for a frame when
    /// there are none, and refuses another action until then.
    void setParamActionCallback(ParamActionCallback callback);
    void setParamActionCallback(AsyncParamActionCallback callback);

    /// Set the callback invoked after native parameter widgets change.
    void setParamChangedCallback(ParamChangedCallback callback);

    /// Populate the native parameter panel from @p defs (output of
    /// `ProcessExtension.get_params()`).  Passing an empty array hides the
    /// panel.  Interactive entries require "id" + "type"; display-only
    /// helpers may use `section` / `info` rows.
    void setParamDefs(const QJsonArray& defs);

    /// Collect the current values from the parameter panel widgets.
    /// Returns an empty map when no parameter definitions have been set.
    [[nodiscard]] QCborMap currentParams() const;
    void applyParamUpdates(const QCborMap& updates, const QString& reason = QStringLiteral("programmatic"));

    /// Makes a view the window's source: the host's mirror of the image the application bound, which the preview runs
    /// over and the Execute button applies the extension to. The window's target follows it. The view is not
    /// reparented while a preview view shows the window's frames; without one it is embedded in the view area. Null
    /// lets the current one go - call it before deleting the view - and stops the preview.
    void setSourceView(ImageViewControl* view);

    /// Show or hide the view area (the preview view, or the source image when the window does not preview).
    ///
    /// When hidden the parameter panel and buttons fill the window.  Non-
    /// preview processes should call setViewAreaVisible(false) so the image
    /// view does not consume layout space.
    void setViewAreaVisible(bool visible);

    /// An image of the process's session, shared: what the window keeps and hands on is never written in place, so a
    /// view's document asset can be kept as it is - a later change replaces the document's asset with another.
    using SharedAsset = std::shared_ptr<const an::consumer::ImageAsset>;

    /// Update the committed baseline after Apply: the source view shows the result, which the next preview and
    /// execute start from.
    void applyCommittedResult(an::consumer::ImageAsset&& image);

    /// The image previews and executes start from: what the source view shows; null without one.
    [[nodiscard]] SharedAsset sourceImage() const;

    /// A copy of the manual mask while one is drawn and used, else null.
    [[nodiscard]] SharedAsset manualMaskImage() const;
    [[nodiscard]] bool hasManualMask() const;
    [[nodiscard]] bool manualMaskDisplayModeSelected() const;

    [[nodiscard]] const std::string& sessionId() const noexcept
    {
        return session_id_;
    }
    [[nodiscard]] ImageViewControl* sourceView() const noexcept
    {
        return source_view_;
    }
    [[nodiscard]] bool isViewAreaVisible() const noexcept
    {
        return view_area_visible_;
    }

    /// Reset all parameter widgets to their default values and ask for one preview of them, matching
    /// ImageRTPreviewControl behavior after Apply.
    void resetParamsToDefaults();

    /// Lock/unlock interactive controls while a native extension job is running.
    /// The Close button remains enabled so the user can cancel the active job. The preview's frames are refused while
    /// it runs, and one frame of the parameters it leaves follows it.
    void setExecutionInProgress(bool in_progress);
    [[nodiscard]] bool isExecutionInProgress() const noexcept
    {
        return execution_in_progress_;
    }
    void markExecutionCancelRequested() noexcept;
    void clearExecutionCancelRequested() noexcept;
    void requestCloseAfterExecutionCancel() noexcept;
    [[nodiscard]] bool executionCancelRequested() const noexcept
    {
        return execution_cancel_requested_;
    }
    [[nodiscard]] bool shouldCloseAfterExecutionCancel() const noexcept
    {
        return close_after_execution_cancel_;
    }

    /// Whether a frame of the preview runs now.
    [[nodiscard]] bool isPreviewInProgress() const noexcept
    {
        return preview_in_progress_;
    }

    /// The manual preview's refresh landed: its status says so, and the button waits for the next change.
    void markManualPreviewRefreshCompleted(bool success);

    /// Refuses the frames on their way and every one waiting, and stops the extension's run over the one on its way.
    /// The host calls it before it ends the session.
    void stopPreviews();

    /// Stops the frames as `stopPreviews` does and holds every request back until `resumePreviews`, which asks for one
    /// frame of the parameters the window holds then: around a run of the extension the GUI thread makes itself. While
    /// paused the Apply button is disabled and a click on it refused, since a run started then would take the
    /// extension's lane ahead of the GUI thread's own run and leave it waiting on a worker.
    void pausePreviews();
    void resumePreviews();

    /// Whether `pausePreviews` holds the frames now. A frame's run may be asking the GUI thread for something - a
    /// setting, a log line - while it holds the extension, so the GUI thread never waits for a frame: what it runs once
    /// the frame on its way has landed, it hands to `whenRunIdle`, with the frames paused first.
    [[nodiscard]] bool previewsPaused() const noexcept;

    // --- Preview area ---

    /// Turns the preview area on, at a size and with a label, as the window's meta parameters ask; nothing when the
    /// meta parameters put the area away.
    void enableSubArea(const QSize& initial_size, const QString& label = {});

    /// Turns the preview area off.
    void disableSubArea();

    /// Whether the preview runs over a preview area.
    [[nodiscard]] bool isSubAreaEnabled() const;

    void onCurvesUpdated(const an::ui::ToneCurveModel& curve) override;

    // --- Target image selector ---

    /// Show a target image selector combo populated from the host's view list.
    /// @p views  List of view dicts (as returned by ``list_views()``).
    /// @p filter Optional callable-style filter: a list of required channel
    ///           counts.  Empty means accept all.
    void enableTargetSelector(const std::vector<std::map<std::string, QVariant>>& views,
                              const std::vector<int>& channel_filter = {});

    /// Add or update one target-selector entry from a host view descriptor.
    void addOrUpdateTargetSelectorView(const std::map<std::string, QVariant>& view);

    /// Remove one target-selector entry by display name.
    void removeTargetSelectorView(const QString& view_name);

    /// Rename one target-selector entry while preserving current selection.
    void renameTargetSelectorView(const QString& old_name, const QString& new_name, const QString& file_name = {},
                                  const QString& selector_tooltip = {});

    /// Populate dynamic view-selector parameters from the host's view list.
    void setAvailableViewSelectorViews(const std::vector<std::map<std::string, QVariant>>& views);
    void addOrUpdateViewSelectorView(const std::map<std::string, QVariant>& view);
    void removeViewSelectorView(const QString& view_name);
    void renameViewSelectorView(const QString& old_name, const QString& new_name, const QString& file_name = {},
                                const QString& selector_tooltip = {});

    /// Set the callback invoked when the user picks a different target image.
    void setTargetChangedCallback(TargetChangedCallback callback);

    /// Provide extension-scoped settings access for native process-window helper UI.
    void setSettingsCallbacks(SettingsGetCallback getCallback, SettingsSetCallback setCallback);
    void setOpenSettingsCallback(OpenSettingsCallback callback);

    /// Refresh native helper UI whose state depends on extension settings.
    void refreshExternalToolConfiguration();
    [[nodiscard]] QCborMap resolvedExternalToolRecord() const;
    [[nodiscard]] QCborValue transientExternalToolSettingValue(const QString& key,
                                                               const QCborValue& defaultValue) const;

    /// Return the currently selected target view name (empty if none).
    [[nodiscard]] std::string selectedTargetView() const;

    /// True when the target selector widget has been created.
    [[nodiscard]] bool hasTargetSelector() const noexcept
    {
        return target_selector_ != nullptr;
    }
    [[nodiscard]] bool hasViewSelectorParams() const noexcept
    {
        return !view_selector_params_.empty();
    }

    /// True when the meta params requested a target image selector (pending enableTargetSelector call).
    [[nodiscard]] bool hasPendingTargetSelector() const noexcept
    {
        return pending_target_selector_;
    }
    [[nodiscard]] const std::vector<int>& pendingTargetChannelFilter() const noexcept
    {
        return pending_target_channel_filter_;
    }
    void clearPendingTargetSelector() noexcept
    {
        pending_target_selector_ = false;
    }

    /// True when the most recent Apply should close the window after commit.
    [[nodiscard]] bool shouldCloseAfterExecute() const noexcept
    {
        return close_after_execute_ && !execution_cancel_requested_;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void onContentLayoutReplaced(QBoxLayout* contentLayout) override;

    // The real-time preview's body and declarations.
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    [[nodiscard]] an::Status executeProcessImpl(const an::ui::ProcessSource& source,
                                                an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
                                                const ProcessExecutionParams* params,
                                                an::ui::ExecutionScope& scope) override;
    [[nodiscard]] ProcessExecutionParamsPtr getProcessParamsForExecution() override;
    [[nodiscard]] an::ui::ProcessBodyCoverage processBodyCoverage(const ProcessExecutionParams* params) const override;
    [[nodiscard]] bool isIdentity(const ProcessExecutionParams* params) const override;
    [[nodiscard]] std::uint32_t previewRegionMargin(const ProcessExecutionParams* params) const override;
    void settingsUpdatedImpl() override;
    void executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode) override;
    void executeProcessEndedImpl(const RTPreviewMode previewMode, const bool sequenceMode) override;
    void executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode) override;
    void previewControlStarted() override;
    void previewControlClosed() override;
    void enableProcessControls(bool enabled) override;

public:
    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onAlternateImageEnabled(ImageViewControl* ctrl) override;
    void onAlternateImageDisabled(ImageViewControl* ctrl) override;

private:
    struct ExternalToolResolution;
    struct PreviewParams;

    void applyNativeWindowSizeConstraints();
    void fitWindowToVisibleContents();
    void setPreviewPaneVisible(bool visible);
    void syncEmbeddedViewLayout();

    /// Makes or lets go of the preview view as the window now previews or does not, and keeps the view area showing
    /// what the window shows: the preview view while it previews, the source view otherwise.
    void syncPreviewControl();

    /// Whether the window previews now: a body, a source view showing an image, the view area shown, and the window
    /// not closing.
    [[nodiscard]] bool wantsPreview() const;

    /// The source view shows another image, or its first: the preview follows it, and so do the histograms, the
    /// original compared with and the manual mask's drawing.
    void onSourceImageChanged();

    /// Asks for a frame with the parameters the window holds now, unless the manual preview's refresh runs first.
    void requestFrame();

    /// The preview view, while the window previews.
    [[nodiscard]] ImageViewControl* previewPresentationView() const noexcept;
    void showAssetInView(ImageViewControl* view, an::consumer::ImageAsset&& image);
    [[nodiscard]] an::ui::HistogramCountScheduler* histogramCountsFor(HistogramWidget* widget);
    [[nodiscard]] bool targetSelectorAcceptsView(const std::map<std::string, QVariant>& view) const;
    void refreshViewSelectorParams();
    void applyPreviewMeta();

    enum class PanelVariant
    {
        native,
        custom
    };

    void onExecuteClicked();
    void onCancelClicked();
    void onExternalToolConfigClicked();
    void onParamActionClicked(const QString& actionId);
    void onInteractiveParamChanged(const QString& paramId = {});
    void registerManualPreviewRefreshButton(QPushButton* button, const QString& actionId);
    bool triggerInitialManualPreviewRefreshIfNeeded();
    void markManualPreviewDirty(const QString& paramId = {});
    void updateManualPreviewRefreshUi();
    [[nodiscard]] bool paramInvalidatesManualPreview(const QString& paramId) const;
    void setupManualMaskEditor(const QJsonObject& obj, QVBoxLayout* parentLayout);
    void setManualMaskEditingEnabled(bool enabled);
    void cancelManualMaskEditing();
    void clearManualMask();
    void updateManualMaskUiState();
    [[nodiscard]] bool manualMaskUseChecked() const;
    void setManualMaskUseChecked(bool checked, bool notify);
    void syncManualMaskDocumentToSource();
    void syncManualMaskCanvas();
    void destroyManualMaskCanvas();
    void onManualMaskChanged();
    void submitManualMaskEdit(
        std::function<an::Status(an::ui::drawing::DrawingDocument&, const an::ExecutionContext&)> edit);
    [[nodiscard]] QComboBox* findChoiceParamCombo(const QString& id) const;
    bool setChoiceParamValueSilently(const QString& id, const QVariant& value);
    void setChoiceParamOptionEnabled(const QString& id, const QVariant& value, bool enabled);
    bool setChoiceParamToDefaultSilently(const QString& id);
    void updateConditionalWidgets();
    void updatePercentageMixLabels();
    void updateValueDescriptionLabels();
    void updateVectorSlopeGraphs();
    void syncCurveEditorHistograms(const SharedAsset& explicitSource = {});
    bool setParamValue(const QString& id, const QCborValue& value);
    void updateExecuteButtonEnabled();
    void resetExternalToolConfiguration();
    void parseExternalToolConfiguration(const QJsonObject& config);
    void ensureExternalToolConfigurationWidgets(QVBoxLayout* panelLayout);
    void setExternalToolConfigurationStatus(bool valid, const QString& message, an::ui::AnInfoBanner::Tone tone);
    [[nodiscard]] QString currentExternalToolDirectory() const;
    [[nodiscard]] QString platformAdjustedRequiredFile(const QString& fileName) const;
    [[nodiscard]] QString externalToolExecutableInDirectory(const QString& directory) const;
    [[nodiscard]] bool validateExternalToolDirectory(const QString& directory, QString* outMessage) const;
    [[nodiscard]] ExternalToolResolution resolveExternalTool() const;
    void startExternalToolVersionQuery(const QString& executablePath);
    void completeExternalToolVersionQuery(QProcess* process, const QString& executablePath, int queryGeneration);

    /// One rendered parameter widget plus its schema metadata.
    struct ParamEntry
    {
        std::string id;
        std::string type;
        QWidget* widget = nullptr;
        QVariant default_value;
    };
    struct ConditionalWidget
    {
        QPointer<QWidget> widget;
        QString param_id;
        QVariant equals;
        bool has_equals = false;
        QVariant not_equals;
        bool has_not_equals = false;
        std::optional<an::ui::extensions::schema_utils::SchemaCondition> visible_condition;
        std::optional<an::ui::extensions::schema_utils::SchemaCondition> enabled_condition;
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
    struct PercentageMixLabel
    {
        QPointer<QLabel> label;
        QString source_param_id;
        QString left_label;
        QString right_label;
    };
    struct ValueDescriptionRange
    {
        double min = 0.0;
        double max = 0.0;
        QString text;
    };
    struct ValueDescriptionLabel
    {
        QPointer<QLabel> label;
        QString source_param_id;
        int decimals = 2;
        QString prefix;
        QString suffix;
        QString separator = QStringLiteral(" - ");
        std::vector<ValueDescriptionRange> ranges;
    };
    struct ViewSelectorParam
    {
        QString id;
        QPointer<QComboBox> combo;
        QVariant default_value;
        QString placeholder;
        std::vector<int> channel_filter;
        bool match_target_geometry = false;
        bool match_target_channels = false;
        bool exclude_target = false;
    };
    struct RtHistogramParam
    {
        QString id;
        QPointer<HistogramWidget> histogram;
        QPointer<QScrollArea> scroll_area;
        QPointer<QComboBox> plot_resolution;
        QPointer<QToolButton> zoom_in;
        QPointer<QToolButton> zoom_out;
        QPointer<QToolButton> zoom_reset;
        QPointer<QToolButton> grid_button;
        QPointer<QToolButton> local_channel_normalization;
        QPointer<QToolButton> show_clipping_info;
        QPointer<QToolButton> show_histogram_areas;
    };
    struct VectorSlopeParam
    {
        QString id;
        QPointer<QWidget> graph;
    };

    std::string session_id_;
    QPointer<ImageViewControl> source_view_;

    QHBoxLayout* root_layout_ = nullptr;
    QSplitter* main_splitter_ = nullptr;
    QWidget* controls_panel_ = nullptr;
    QWidget* preview_panel_ = nullptr;
    QVBoxLayout* controls_layout_ = nullptr;
    QWidget* view_area_widget_ = nullptr;
    QVBoxLayout* view_area_layout_ = nullptr;
    QLabel* placeholder_label_ = nullptr;
    bool view_area_visible_ = true;
    QScrollArea* param_scroll_area_ = nullptr;
    QWidget* param_panel_ = nullptr;
    QWidget* action_bar_ = nullptr;
    QDialogButtonBox* button_box_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    QPushButton* execute_button_ = nullptr;
    QPushButton* close_button_ = nullptr;
    bool close_after_execute_ = false;
    bool keep_open_mode_ = false;
    bool execution_in_progress_ = false;
    int previews_paused_ = 0;
    bool execution_cancel_requested_ = false;
    bool close_after_execution_cancel_ = false;
    bool preview_in_progress_ = false;
    bool last_frame_committed_ = false;
    SharedAsset last_histogram_source_;
    bool manual_preview_enabled_ = false;
    bool manual_preview_dirty_ = false;
    bool manual_preview_refresh_in_flight_ = false;
    bool manual_preview_refresh_on_open_pending_ = false;
    quint64 manual_preview_dirty_generation_ = 0;
    quint64 manual_preview_refresh_generation_ = 0;
    QString manual_preview_refresh_action_id_;
    QString manual_preview_stale_text_;
    QString manual_preview_ready_text_;
    QString manual_preview_updating_text_;
    QPointer<QPushButton> manual_preview_refresh_button_;
    QPointer<an::ui::AnInfoBanner> manual_preview_status_banner_;
    std::shared_ptr<an::ui::drawing::DrawingDocument> manual_mask_document_;
    std::unique_ptr<an::ui::drawing::DrawingCanvasController> manual_mask_canvas_;
    std::unique_ptr<an::ui::drawing::DrawingEditQueue> manual_mask_edits_;
    QPointer<QGroupBox> manual_mask_group_;
    QPointer<QWidget> manual_mask_controls_widget_;
    QPointer<QCheckBox> manual_mask_use_checkbox_;
    QPointer<QPushButton> manual_mask_edit_button_;
    QPointer<QPushButton> manual_mask_cancel_button_;
    QPointer<QPushButton> manual_mask_clear_button_;
    QPointer<QComboBox> manual_mask_display_combo_;
    SharedAsset manual_mask_edit_snapshot_;
    QString manual_mask_display_param_id_;
    QString manual_mask_use_param_id_;
    QVariant manual_mask_display_value_;
    QVariant manual_mask_previous_display_value_;
    bool manual_mask_editing_enabled_ = false;
    bool manual_mask_had_previous_display_value_ = false;
    bool manual_mask_use_default_ = false;
    bool manual_mask_edit_snapshot_use_checked_ = false;
    int manual_mask_default_brush_size_ = 50;

    /// The copy of the manual mask the last frame was handed, and the drawing's revision it was copied at; a frame
    /// issued at the same revision reads the same copy.
    mutable SharedAsset manual_mask_copy_;
    mutable const an::ui::drawing::DrawingDocument* manual_mask_copy_document_ = nullptr;
    mutable std::uint64_t manual_mask_copy_revision_ = 0;

    bool curve_edit_in_progress_ = false;
    bool curve_edit_pending_preview_ = false;
    QString close_button_default_text_;
    std::vector<std::pair<QPointer<QWidget>, bool>> execution_locked_widgets_;
    QList<int> last_splitter_sizes_;

    std::vector<ParamEntry> param_entries_;
    std::vector<ConditionalWidget> conditional_widgets_;
    std::vector<PercentageMixLabel> percentage_mix_labels_;
    std::vector<ValueDescriptionLabel> value_description_labels_;
    std::vector<RtHistogramParam> rt_histogram_params_;
    std::vector<VectorSlopeParam> vector_slope_params_;
    std::map<const HistogramWidget*, std::unique_ptr<an::ui::HistogramCountScheduler>> histogram_counts_;
    std::unordered_set<std::string> manual_preview_invalidating_param_ids_;
    PanelVariant panel_variant_ = PanelVariant::native;
    bool preview_area_enabled_ = true;
    bool updating_param_defs_ = false;
    bool configured_preview_autostretch_ = false;
    bool configured_preview_autostretch_default_ = false;
    bool preview_autostretch_default_applied_ = false;
    bool header_progress_visible_ = true;

    /// The extension's preview, shared with every frame issued while it was set; and what its frames may run over.
    std::shared_ptr<const ExtensionPreviewBody> preview_body_;
    an::ui::PreviewScaling preview_scaling_ = an::ui::PreviewScaling::FullOnly;
    std::uint32_t preview_region_margin_ = 0u;

    /// Whether the framework asked for the preview to stop - its view gone - until the window previews again.
    bool preview_disabled_ = false;

    /// Whether the window was closed and its session is ending: no frame starts until it is shown again.
    bool closing_ = false;

    ExecuteCallback execute_callback_;
    CloseCallback close_callback_;
    CancelCallback cancel_callback_;
    AsyncParamActionCallback param_action_callback_;
    /// Whether an action's callback has yet to answer; another action is refused until it does.
    bool param_action_pending_ = false;
    ParamChangedCallback param_changed_callback_;
    TargetChangedCallback target_changed_callback_;
    SettingsGetCallback settings_get_callback_;
    SettingsSetCallback settings_set_callback_;
    OpenSettingsCallback open_settings_callback_;

    // Preview-area configuration from meta params.
    bool sub_area_wanted_ = false;
    QSize configured_sub_area_size_;
    QString configured_sub_area_label_;
    int configured_controls_panel_width_ = 0;
    bool pending_target_selector_ = false;
    std::vector<int> pending_target_channel_filter_;
    QString pending_target_selector_label_;
    QString pending_target_selector_position_;
    QSize configured_window_size_;

    // Target image selector state.
    QWidget* target_selector_section_ = nullptr;
    QLabel* target_selector_label_ = nullptr;
    QComboBox* target_selector_ = nullptr;
    std::vector<int> target_channel_filter_;
    std::vector<ViewSelectorParam> view_selector_params_;
    std::vector<std::map<std::string, QVariant>> available_view_selector_views_;

    // External-tool configuration state declared by process-window metadata.
    bool external_tool_config_enabled_ = false;
    bool external_tool_config_valid_ = true;
    bool external_tool_append_platform_suffix_ = true;
    bool external_tool_require_executable_ = true;
    bool external_tool_show_configured_banner_ = true;
    bool external_tool_open_extension_settings_ = false;
    QString external_tool_setting_key_;
    QString external_tool_label_;
    QString external_tool_button_label_;
    QString external_tool_dialog_title_;
    QSize external_tool_dialog_size_;
    QString external_tool_not_configured_text_;
    QString external_tool_configured_text_;
    QString external_tool_invalid_text_;
    QString external_tool_download_page_url_;
    QString external_tool_download_button_label_;
    QString external_tool_update_page_url_;
    QString external_tool_update_api_url_;
    QString external_tool_install_instructions_;
    QString external_tool_download_note_;
    QStringList external_tool_required_files_;
    QStringList external_tool_candidate_directories_;
    QStringList external_tool_candidate_subdirectories_;
    QString external_tool_primary_executable_;
    QStringList external_tool_executable_candidates_;
    QStringList external_tool_version_arguments_;
    QString external_tool_version_pattern_;
    QCborMap external_tool_resolved_record_;
    int external_tool_version_query_generation_ = 0;
    QPointer<QProcess> external_tool_version_process_;
    QString external_tool_version_query_executable_;
    an::ui::AnInfoBanner* external_tool_status_banner_ = nullptr;
    QPushButton* external_tool_config_button_ = nullptr;
    QPointer<QDialog> external_tool_config_dialog_;
};

} // namespace an::ui::extensions

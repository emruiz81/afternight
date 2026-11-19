// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/ExtensionCommon.h"
#include "app/ExtensionManager.h"
#include "app/ProcessRegistry.h"
#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionSdkService.h"
#include "app/ExtensionSessionFeed.h"
#include "app/SharedBufferManager.h"
#include "controls/ImageCropController.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>

#include <QByteArray>
#include <QCborValue>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QRect>
#include <QSize>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <QString>
#include <unordered_map>
#include <vector>

class ImageViewControl;

namespace an::ui::extensions
{

class ExtensionHostedProcess;
class ExtensionHostedSession;

struct ExtensionHostLaunchRequest
{
    std::string extension_id;
    std::string display_name;
    std::string sdk_backend = "runtime";
    ExtensionLaunchMode launch_mode = ExtensionLaunchMode::SingleImage;
    std::string host_version = "dev";
    std::vector<std::string> capabilities;
    std::vector<std::string> host_arguments;
    std::string inspect_settings_snapshot_cbor_base64;
    std::filesystem::path host_executable;
    /// The bound on each handshake message, and on the connection of a host that connects before it starts its runtime.
    int handshake_timeout_ms = 5000;
    int heartbeat_interval_ms = 5000;
    int heartbeat_miss_limit = 3;
    int cancel_grace_period_ms = 1000;
    /// The host connects and says hello before it starts its runtime - the backend, the interpreter and the extension's
    /// import - and reports the start with `runtime.ready` or `runtime.bootstrap_failed`, so the launch returns once
    /// the handshake is acknowledged. Otherwise the host starts its runtime first, and the launch waits for it to
    /// connect for as long as it runs, up to the runtime's start bound, and returns with the runtime ready.
    bool defer_runtime_bootstrap = false;
    /// The application draws the session's panel itself (`kHostedPanelCapability`): the acknowledgement states it when
    /// the host's hello does, and the host then shows no window of its own.
    bool hosted_panel = false;
};

struct ExtensionHostPreparedSession
{
    std::filesystem::path host_executable;
    std::string extension_id;
    std::string display_name;
    std::string session_id;
    std::string endpoint_name;
    std::string session_secret;
    std::string sdk_backend;
    ExtensionLaunchMode launch_mode = ExtensionLaunchMode::SingleImage;
    std::string host_version;
    std::vector<std::string> capabilities;
    std::vector<std::string> host_arguments;
    std::string inspect_settings_snapshot_cbor_base64;
    int handshake_timeout_ms = 5000;
    bool defer_runtime_bootstrap = false;
    bool hosted_panel = false;
    RuntimeHelloAckMessage ack_template;
};

struct ExtensionHostHandshakeResult
{
    RuntimeHelloMessage hello;
    RuntimeHelloAckMessage ack;
    int process_exit_code = -1;
    std::string process_output;
};

enum class ExtensionHostSessionState
{
    Inactive,
    Active,
    Crashed,
    Disconnected,
    Unresponsive,
    Failed,
    Exited,
    Stopped
};

enum class ExtensionHostExecutionDispatchState
{
    Started,
    Queued,
    Rejected,
    Waiting ///< Not sent: the target image is being written, and the first pump after its writer lets go sends it.
};

struct ExtensionHostRunningSession
{
    ExtensionHostPreparedSession prepared_session;
    ExtensionHostHandshakeResult handshake;
    std::unique_ptr<QProcess> process;
#ifdef _WIN32
    void* process_job_handle = nullptr;
#endif
    std::unique_ptr<QLocalSocket> socket;
    QByteArray buffered_input;
    QElapsedTimer session_clock;
    ExtensionHostSessionState state = ExtensionHostSessionState::Inactive;
    qint64 last_heartbeat_elapsed_ms = 0;
    qint64 last_ping_sent_elapsed_ms = -1;
    int successful_heartbeat_count = 0;
    int missed_heartbeats = 0;
    int next_ping_sequence = 1;
    bool ping_outstanding = false;
    std::string outstanding_ping_request_id;
    std::string in_flight_request_id;
    std::string in_flight_request_kind;
    std::string in_flight_params_token;
    std::vector<SharedBufferDescriptor> in_flight_request_buffers;
    qint64 cancel_requested_elapsed_ms = -1;
    bool cancel_requested = false;
    int completed_request_count = 0;
    int serviced_sdk_message_count = 0;
    bool initial_view_state_sent = false;
    bool initial_theme_state_sent = false;
    bool initial_sdk_state_attempted = false;
    std::string initial_view_name;
    // A host that connects before it starts its runtime is acknowledged before it
    // imports the extension. Runtime traffic remains gated until runtime.ready arrives.
    bool runtime_ready = true;
    bool runtime_bootstrap_state_ready = false;
    bool native_window_close_requested = false;
    bool native_process_window_rt_preview = false;
    // Preview requests still complete via execution.result.
    std::optional<ExecutionResultMessage> last_preview_result;
    // Execute requests complete via view.commit.
    std::optional<ViewCommitMessage> last_view_commit;
    /// What the host last told of the run of the window it shows (`session.activity`); none before it tells.
    std::optional<SessionActivityMessage> run_activity;
    /// Whether the application draws the session's panel: both the hello and the acknowledgement stated
    /// `kHostedPanelCapability`. Such a host answers an execute that failed or was cancelled with `execution.result`,
    /// and a cancel it takes its time over ends nothing: a frame of a preview may run on until the extension next
    /// looks.
    bool hosted_panel = false;
    /// What a host serving a hosted panel told of it (`ui.describe`, `ui.patch`) and nobody has taken yet, in order;
    /// read unchecked, for the panel to refuse what is not whole.
    std::vector<ExtensionProtocolMessage> panel_inbox;
    std::string latest_ui_validation_request_id;
    std::optional<UiValidateResultMessage> last_ui_validation_result;
    std::string last_error;
    // What an extension stored through the SDK's settings while no settings service answered it, by the SDK
    // session that stored it. It lives with the running session, so a finished session's settings go with it.
    std::unordered_map<std::string, std::unordered_map<std::string, QCborValue>> fallback_settings;
};

struct ExtensionHostQueuedExecutionRequest
{
    std::string queue_id;
    ExtensionHostRunningSession* session = nullptr;
    std::string request_kind;
    std::string params_token;
    QCborMap params;
    std::vector<SharedBufferDescriptor> buffers;
};

struct ExtensionHostExecutionDispatchResult
{
    ExtensionHostExecutionDispatchState state = ExtensionHostExecutionDispatchState::Rejected;
    std::string queue_id;
    std::string request_id;
    int queue_position = 0;
};

struct ExtensionHostExecutionGateState
{
    int max_concurrent_executions = 2;
    bool queue_when_saturated = true;
    std::vector<ExtensionHostRunningSession*> active_execution_sessions;
    std::vector<ExtensionHostQueuedExecutionRequest> queued_requests;
};

struct ExtensionHostLaunchBinding
{
    std::string descriptor_id;
    std::string extension_id;
    std::string display_name;
    std::string sdk_backend = "runtime";
    ExtensionLaunchMode launch_mode = ExtensionLaunchMode::SingleImage;
    std::string host_version = "dev";
    std::vector<std::string> capabilities;
    ExtensionCapabilities process_capabilities;
    std::filesystem::path host_executable;
    std::vector<std::string> host_arguments;
    int cancel_grace_period_ms = -1;
    // Native full-host sessions build and execute their parameter UI locally.
    // They must not start a disposable --inspect host before the live host.
    bool extension_host_owns_process_window = false;
    bool has_cached_native_param_schema = false;
    QJsonArray cached_native_param_schema;
};

struct ExtensionHostLaunchOptions
{
    bool close_after_successful_execute = false;
    bool create_result_view_after_successful_execute = false;
    std::string initial_view_name;
    /// A hosted process whose host failed, started again: the new session is bound to it, and no other process is made.
    ExtensionHostedProcess* hosted_process = nullptr;
};

// An image the application lends a request beside the source - a mask or a weight map - exported under a read lease
// the seam takes when the request is prepared. Holding the asset keeps it alive and grants nothing else.
struct ManagedAuxiliaryImage
{
    SharedBufferRole buffer_role = SharedBufferRole::Mask;
    SharedBufferAccessMode access_mode = SharedBufferAccessMode::ReadOnly;
    std::string symbolic_name;
    std::shared_ptr<const an::consumer::ImageAsset> image;
};

// A named image an execution produced beside its result - an output mask - imported as an asset of its own.
struct ManagedNamedImageResult
{
    std::string symbolic_name;
    an::consumer::ImageAsset image;
};

struct AFTERNIGHTCONTROLS_API ExtensionSessionController : public ImageAreaSelectorControl
{
    struct ProcessMessageState
    {
        enum class Tone
        {
            Info,
            Warning,
            Error
        };

        enum class Source
        {
            Lifecycle,
            Validation
        };

        QString title;
        QString message;
        Tone tone = Tone::Info;
        Source source = Source::Lifecycle;
    };

    enum class PumpAction
    {
        None,
        StopSession
    };

    enum class PreviewSubmissionAction
    {
        StartPreview,
        AwaitInFlightRequest
    };

    // What preparing a request's buffers came to: ready to send; the image is being written and the request waits
    // for it; or a refusal the error names.
    enum class PrepareOutcome
    {
        Ready,
        Busy,
        Failed
    };

    enum class MaterializationKind
    {
        Preview,
        Execute
    };

    // A result imported on a worker, landed on the GUI thread for the pump to present: the image, or why it could
    // not be made, and for an execute the output masks beside it.
    struct LandedMaterialization
    {
        MaterializationKind kind = MaterializationKind::Preview;
        an::Result<an::consumer::ImageAsset> image;
        std::vector<ManagedNamedImageResult> masks;
    };

    struct Materialization;

    // Session lifecycle/state for the active native-window runtime.
    std::string descriptor_id;
    std::string display_name;
    ImageViewControl* target_control = nullptr;
    std::string target_view_id;
    std::string target_view_name;
    std::string target_file_name;
    QPointer<ImageViewControl> preview_control;
    ExtensionCapabilities process_capabilities;
    ExtensionHostRunningSession running_session;
    std::vector<SharedImageBuffer> owned_buffers;
    // Preview completions are tracked through execution.result, execute completions through view.commit.
    std::string last_consumed_execute_commit_request_id;
    std::string last_consumed_preview_result_request_id;
    std::optional<ExtensionHostExecutionDispatchResult> last_preview_dispatch_result;
    std::optional<ExtensionHostExecutionDispatchResult> last_execute_dispatch_result;
    std::optional<ProcessMessageState> current_process_message;
    bool execute_enabled = false;
    bool close_after_successful_execute = false;
    bool create_result_view_after_successful_execute = false;
    std::function<void()> forward_close_request;
    std::function<bool(std::string*)> forward_execute_request;
    std::function<bool(std::string*)> forward_preview_request;
    QCborMap seeded_execution_params;
    bool preview_request_pending = false;
    /// An execute asked for while the target image was being written: sent at the first pump that finds the image
    /// readable and nothing in flight, and let go with the session.
    bool execute_request_pending = false;
    bool managed_preview_sub_area_enabled = false;
    bool launch_outcome_recorded = false;
    /// The state the session was in when the application stopped it: `Active` for one it stopped while it ran, a
    /// terminal state for one that had ended; `Inactive` until it is stopped.
    ExtensionHostSessionState ended_as = ExtensionHostSessionState::Inactive;
    QSize managed_preview_sub_area_size;
    QString managed_preview_sub_area_label;
    std::unique_ptr<ImageCropController> managed_preview_crop_controller;
    std::function<void()> managed_preview_sub_area_changed;
    // The import of a result on its way, or landed; null while none was started.
    std::shared_ptr<Materialization> materialization;
    /// For a session whose panel the application draws (`ExtensionHostRunningSession::hosted_panel`): what its process
    /// shares with it, bound while the session runs, and the process; null for another.
    std::shared_ptr<ExtensionHostedSession> hosted;
    QPointer<QWidget> hosted_process;

    ExtensionSessionController() = default;
    ExtensionSessionController(const ExtensionSessionController&) = delete;
    ExtensionSessionController& operator=(const ExtensionSessionController&) = delete;
    ExtensionSessionController(ExtensionSessionController&&) noexcept = default;
    ExtensionSessionController& operator=(ExtensionSessionController&&) noexcept = default;
    ~ExtensionSessionController();

    void detachUiHandles();
    void configureManagedPreviewSubArea();
    void clearManagedPreviewSubArea();
    void discardMaterializedBuffers() noexcept;
    void markPreviewRefreshPending();
    /// Records that an execute waits for the target image being written, and says so in the process message.
    void markExecuteWaitingForImage();
    /// Ends the wait of an execute that could not be sent once its image was readable, and reports `error`.
    void reportPendingExecuteFailure(const std::string& error);
    void stopRunningSession(const std::function<void(ExtensionHostRunningSession*)>& stop_session);
    [[nodiscard]] bool initializeSessionParams(const QJsonArray& auto_param_schema,
                                               const std::function<bool(std::string*)>& execute_request,
                                               const std::function<bool(std::string*)>& preview_request,
                                               const std::function<void()>& close_session, std::string* error);
    [[nodiscard]] bool
    preparePreviewSubmission(const std::function<void(ExtensionHostRunningSession*)>& remove_queued_preview_requests,
                             const std::function<bool(ExtensionHostRunningSession*, std::string*)>& send_cancel_request,
                             PreviewSubmissionAction* action, std::string* error);
    // The request's buffers are exported from the target view's document under a read lease taken here, without
    // waiting: a document a worker is writing answers Busy, and the caller asks again.
    [[nodiscard]] PrepareOutcome preparePreviewStart(
        const std::function<void(ExtensionHostRunningSession*)>& remove_queued_preview_requests,
        const std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>& auxiliary_buffer_provider,
        const std::function<std::optional<QRect>(ImageViewControl*)>& preview_sub_area_provider,
        QCborMap* execution_params, std::vector<SharedBufferDescriptor>* request_buffers,
        std::vector<SharedImageBuffer>* prepared_owned_buffers, std::string* error);
    [[nodiscard]] PrepareOutcome preparePreviewRequest(
        const std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>& auxiliary_buffer_provider,
        const std::function<std::optional<QRect>(ImageViewControl*)>& preview_sub_area_provider,
        QCborMap* execution_params, std::vector<SharedBufferDescriptor>* request_buffers,
        std::vector<SharedImageBuffer>* prepared_owned_buffers, std::string* error) const;
    void applyPreviewDispatchResult(const ExtensionHostExecutionDispatchResult& dispatch_result,
                                    std::vector<SharedImageBuffer> owned_buffers);
    [[nodiscard]] PrepareOutcome prepareExecuteRequest(
        const std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>& auxiliary_buffer_provider,
        const std::function<std::optional<QRect>(ImageViewControl*)>& preview_sub_area_provider,
        QCborMap* execution_params, std::vector<SharedBufferDescriptor>* request_buffers,
        std::vector<SharedImageBuffer>* prepared_owned_buffers, std::string* error) const;
    void applyExecuteDispatchResult(const ExtensionHostExecutionDispatchResult& dispatch_result,
                                    std::vector<SharedImageBuffer> owned_buffers);
    void notifyExecuteStarted() const;
    void captureInitialViewStateIfAvailable(
        const std::function<bool(ExtensionViewState*, bool*, std::string*)>& describe_active_view_state);
    // A result is imported on a worker - the preview buffer composited into the target's image when it is a
    // sub-area, the execute buffer and its masks each a new asset - and lands here through the GUI thread; `wake`
    // is called there so the pump presents it. One import at a time; a session stopped meanwhile discards it.
    [[nodiscard]] bool materializationInFlight() const noexcept;
    [[nodiscard]] bool hasLandedMaterialization() const noexcept;
    void beginPreviewMaterialization(const an::consumer::EngineSession& session, std::function<void()> wake);
    void beginExecuteMaterialization(const an::consumer::EngineSession& session, std::function<void()> wake);
    [[nodiscard]] std::optional<LandedMaterialization> takeLandedMaterialization();
    void cancelMaterialization() noexcept;
    void clearProcessMessageForSource(ProcessMessageState::Source source);
    void applyPreviewPresentation(
        an::consumer::ImageAsset&& output_image,
        const std::function<ImageViewControl*(ImageViewControl*, ImageViewControl*, an::consumer::ImageAsset&&)>&
            preview_presenter);
    void applyExecutePresentation(
        an::consumer::ImageAsset&& output_image, std::vector<ManagedNamedImageResult>&& output_masks,
        const std::function<void(ImageViewControl*, an::consumer::ImageAsset&&, const std::string&,
                                 const std::string&)>& result_presenter,
        const std::function<void(ImageViewControl*, std::vector<ManagedNamedImageResult>&&)>& output_mask_presenter,
        const std::function<ImageViewControl*(ImageViewControl*, an::consumer::ImageAsset&&, const std::string&,
                                              const std::string&)>& generated_result_presenter);
    void reportPreviewMaterializationFailure(const std::string& result_error) const;
    void reportExecuteMaterializationFailure(const std::string& result_error);
    void recordPumpFailure(const std::string& error) const;
    void advanceActivePump(const std::function<bool(ExtensionHostRunningSession*, std::string*)>& pump_session);
    void recordTerminalTransition() const;
    [[nodiscard]] bool shouldStopAfterSuccessfulExecutePresentation(bool headless_qt_platform) const noexcept;
    [[nodiscard]] PumpAction
    advanceAfterMaterialization(const std::function<bool(ExtensionSessionController*, std::string*)>& start_preview);

    [[nodiscard]] bool isActive() const noexcept
    {
        return running_session.state == ExtensionHostSessionState::Active;
    }

    [[nodiscard]] bool isStopped() const noexcept
    {
        return running_session.state == ExtensionHostSessionState::Stopped;
    }

    [[nodiscard]] bool matchesDescriptor(const std::string_view candidate_descriptor_id,
                                         const ImageViewControl* control) const noexcept
    {
        return descriptor_id == candidate_descriptor_id && isBoundTo(control) && isActive();
    }

    void captureInitialViewState(const ExtensionViewState& view_state)
    {
        if (target_view_id.empty() && !view_state.view_name.empty())
        {
            target_view_id = view_state.view_name;
        }
        if (!view_state.view_name.empty())
        {
            target_view_name = view_state.view_name;
            running_session.initial_view_name = view_state.view_name;
        }
        if (!view_state.file_name.empty())
        {
            target_file_name = view_state.file_name;
        }
    }

    void updateBoundViewIdentity(const std::string_view view_name, const std::string_view file_name)
    {
        if (!view_name.empty())
        {
            target_view_name = std::string(view_name);
            if (!running_session.initial_sdk_state_attempted)
            {
                running_session.initial_view_name = target_view_name;
            }
        }
        if (!file_name.empty())
        {
            target_file_name = std::string(file_name);
        }
    }

    [[nodiscard]] bool isBoundTo(const ImageViewControl* control) const noexcept
    {
        return target_control == control;
    }

    [[nodiscard]] bool matchesSessionId(const std::string_view session_id) const noexcept
    {
        return running_session.prepared_session.session_id == session_id;
    }

    [[nodiscard]] const std::string& sessionId() const noexcept
    {
        return running_session.prepared_session.session_id;
    }

    [[nodiscard]] std::string currentViewId() const
    {
        if (!target_view_id.empty())
        {
            return target_view_id;
        }
        return target_view_name;
    }

    [[nodiscard]] std::string resolveNotificationViewId(const ImageViewControl* control,
                                                        const std::string_view fallback_view_id) const
    {
        if (isBoundTo(control))
        {
            const std::string current_view_id = currentViewId();
            if (!current_view_id.empty())
            {
                return current_view_id;
            }
        }

        return std::string(fallback_view_id);
    }

    [[nodiscard]] std::string resolveNotificationFileName(const ImageViewControl* control,
                                                          const std::string_view fallback_file_name) const
    {
        if (isBoundTo(control) && !target_file_name.empty())
        {
            return target_file_name;
        }

        return std::string(fallback_file_name);
    }

    [[nodiscard]] bool hasPendingPreviewMaterialization() const noexcept
    {
        const auto& preview_result = running_session.last_preview_result;
        return isActive() && preview_result.has_value() && preview_result->result_kind == "preview" &&
               preview_result->target_request_id != last_consumed_preview_result_request_id;
    }

    [[nodiscard]] bool shouldApplyPreviewResult() const noexcept
    {
        const auto& preview_result = running_session.last_preview_result;
        return hasPendingPreviewMaterialization() && !preview_request_pending && preview_result.has_value() &&
               !preview_result->output_buffers.empty();
    }

    void markPreviewResultConsumed();

    /// Whether the session ended because its native window closed and has not been stopped yet: a window that does
    /// not keep open closes right after its Apply commits, so its commit and its close arrive together, and the
    /// committed result is still the session's to land.
    [[nodiscard]] bool closedByItsWindow() const noexcept
    {
        return running_session.native_window_close_requested && !isActive() && !isStopped();
    }

    [[nodiscard]] bool hasPendingExecuteMaterialization() const noexcept
    {
        const auto& view_commit = running_session.last_view_commit;
        return (isActive() || closedByItsWindow()) && view_commit.has_value() && !view_commit->result_buffers.empty() &&
               view_commit->request_id != last_consumed_execute_commit_request_id;
    }

    /// Whether a commit the session made is still to land: not imported yet, being imported, or imported and not shown.
    [[nodiscard]] bool owesExecuteCommit() const noexcept
    {
        return hasPendingExecuteMaterialization() || materializationInFlight() || hasLandedMaterialization();
    }

    void markExecuteCommitConsumed();

    void clearExecuteCommit() noexcept;

    [[nodiscard]] bool acceptsCommittedViewId(const std::string_view committed_view_id) const noexcept
    {
        if (committed_view_id.empty())
        {
            return true;
        }

        const std::string expected_view_id = currentViewId();
        return expected_view_id.empty() || committed_view_id == expected_view_id || committed_view_id == sessionId();
    }

    [[nodiscard]] bool shouldRestartPendingPreview() const noexcept
    {
        return isActive() && preview_request_pending && running_session.in_flight_request_id.empty();
    }

    /// Whether an execute that waited for the target image is to be asked again now: the session is active and nothing
    /// is in flight, which the host requires before it takes an execute.
    [[nodiscard]] bool shouldRetryPendingExecute() const noexcept
    {
        return isActive() && execute_request_pending && running_session.in_flight_request_id.empty();
    }

    [[nodiscard]] bool shouldStopForTerminalState() const noexcept
    {
        return !isActive() && !isStopped();
    }

    [[nodiscard]] bool shouldPrune() const noexcept
    {
        return isStopped();
    }

    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;
    void newSelectionFinished() override;
};

/// Takes a `session.activity` a host sent into its session: what the run of the window it shows is now. Refused for a
/// host that did not state `kSessionActivityCapability` in its hello, for one that is not the session's, and for a
/// message that is not a valid `session.activity`.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool takeSessionActivity(ExtensionHostRunningSession* running_session,
                                                              const ExtensionProtocolMessage& message,
                                                              std::string* error = nullptr);

/// Where a session stands, as a shell shows it: its host starting, ready or not responding; the run of the window it
/// shows, as its host tells it or as the application's execute in flight is; and whether its host raises its window.
[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionSessionStanding standingOf(const ExtensionSessionController& session);

/// How a session the application stopped ended: failed when its host had crashed, stopped answering, refused what it
/// was sent or failed to start - with why - and as it should otherwise.
[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionSessionEnd endOf(const ExtensionSessionController& session);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<QRect>
normalizeManagedPreviewSubArea(bool selection_enabled, const QSize& source_image_size, const QRect& selection_area);

/// What an extension's Apply did, as its history step says it: "Parameters used:" and each parameter a person sees, a
/// line each, in the order of their names; "Parameters used: none" for none.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string extensionHistoryParametersText(const QCborMap& params);

/// The longest path a local socket can have where local sockets are files: a socket address holds 108 bytes of path,
/// its terminator among them.
inline constexpr std::size_t kLocalSocketPathLimit = 107u;

/// The name a session's endpoint listens under where local sockets are files. A name that is not a path is the socket
/// `temporary_directory/name`, and that whole path has to fit a socket address: while it does, the name is returned as
/// it is; otherwise the endpoint is the path `directory/name` under the first of `short_directories` where that fits,
/// and the name as it is when it fits under none, for the listen to refuse.
///
/// @param name the endpoint's name.
/// @param temporary_directory the directory a name that is not a path is resolved under.
/// @param short_directories where the endpoint may go instead, in order of preference.
/// @return the name or path to listen under.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string localEndpointName(const std::string& name,
                                                                   const std::string& temporary_directory,
                                                                   const std::vector<std::string>& short_directories);

/// The arguments a full host's native window previews by: `--preview-frame-rate` with the rate the Processing page
/// states, which the host's own settings never hold; and, for an extension whose manifest lets a frame of a drag run
/// over less than the whole image, `--preview-scaling` with the manifest's word and - for `native_region` -
/// `--preview-region-margin` with the reach it states.
///
/// @param capabilities the process's capabilities, as its manifest states them.
/// @param frameRate the previews a second the Processing page states.
/// @return the arguments, in order.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<std::string>
nativePreviewHostArguments(const ExtensionCapabilities& capabilities, std::uint32_t frameRate);

class AFTERNIGHTCONTROLS_API ExtensionHostManager
{
public:
    void setSdkService(ExtensionSdkService* sdk_service) noexcept;

    [[nodiscard]] ExtensionHostPreparedSession prepareSession(const ExtensionHostLaunchRequest& request,
                                                              const std::filesystem::path& app_dir = {}) const;

    [[nodiscard]] bool launchAndHandshake(const ExtensionHostPreparedSession& session,
                                          ExtensionHostHandshakeResult* result, std::string* error = nullptr) const;
    [[nodiscard]] bool launchSession(const ExtensionHostPreparedSession& session,
                                     ExtensionHostRunningSession* running_session, std::string* error = nullptr) const;
    [[nodiscard]] bool pumpSession(ExtensionHostRunningSession* running_session, int timeout_ms,
                                   std::string* error = nullptr) const;
    [[nodiscard]] bool sendExecuteRequest(ExtensionHostRunningSession* running_session, const std::string& params_token,
                                          const std::vector<SharedBufferDescriptor>& buffers = {},
                                          std::string* request_id = nullptr, std::string* error = nullptr) const;
    [[nodiscard]] bool sendExecuteRequest(ExtensionHostRunningSession* running_session, const std::string& params_token,
                                          const QCborMap& params, const std::vector<SharedBufferDescriptor>& buffers,
                                          std::string* request_id = nullptr, std::string* error = nullptr) const;
    [[nodiscard]] bool sendPreviewRequest(ExtensionHostRunningSession* running_session, const std::string& params_token,
                                          const std::vector<SharedBufferDescriptor>& buffers = {},
                                          std::string* request_id = nullptr, std::string* error = nullptr) const;
    [[nodiscard]] bool sendPreviewRequest(ExtensionHostRunningSession* running_session, const std::string& params_token,
                                          const QCborMap& params, const std::vector<SharedBufferDescriptor>& buffers,
                                          std::string* request_id = nullptr, std::string* error = nullptr) const;
    [[nodiscard]] bool sendCancelRequest(ExtensionHostRunningSession* running_session,
                                         std::string* request_id = nullptr, std::string* error = nullptr) const;
    [[nodiscard]] bool
    submitExecuteOrQueue(ExtensionHostExecutionGateState* gate_state, ExtensionHostRunningSession* running_session,
                         const std::string& params_token, const std::vector<SharedBufferDescriptor>& buffers,
                         ExtensionHostExecutionDispatchResult* dispatch_result, std::string* error = nullptr) const;
    [[nodiscard]] bool submitExecuteOrQueue(ExtensionHostExecutionGateState* gate_state,
                                            ExtensionHostRunningSession* running_session,
                                            const std::string& params_token, const QCborMap& params,
                                            const std::vector<SharedBufferDescriptor>& buffers,
                                            ExtensionHostExecutionDispatchResult* dispatch_result,
                                            std::string* error = nullptr) const;
    [[nodiscard]] bool
    submitPreviewOrQueue(ExtensionHostExecutionGateState* gate_state, ExtensionHostRunningSession* running_session,
                         const std::string& params_token, const std::vector<SharedBufferDescriptor>& buffers,
                         ExtensionHostExecutionDispatchResult* dispatch_result, std::string* error = nullptr) const;
    [[nodiscard]] bool submitPreviewOrQueue(ExtensionHostExecutionGateState* gate_state,
                                            ExtensionHostRunningSession* running_session,
                                            const std::string& params_token, const QCborMap& params,
                                            const std::vector<SharedBufferDescriptor>& buffers,
                                            ExtensionHostExecutionDispatchResult* dispatch_result,
                                            std::string* error = nullptr) const;
    [[nodiscard]] bool pumpManagedSession(ExtensionHostExecutionGateState* gate_state,
                                          ExtensionHostRunningSession* running_session, int timeout_ms,
                                          std::string* error = nullptr) const;
    /// Asks a host to show its window in front (`window.raise`); refused for a host that did not state
    /// `kWindowRaiseCapability`, which would refuse the message. Where the window system lets only the application in
    /// front bring another's window forward, the host is let do it first.
    [[nodiscard]] bool sendWindowRaise(ExtensionHostRunningSession* running_session,
                                       std::string* error = nullptr) const;
    /// Tells a host serving a hosted panel what a person did in it (`ui.event`); refused for a session whose panel the
    /// application does not draw (`ExtensionHostRunningSession::hosted_panel`), and for one not running.
    [[nodiscard]] bool sendPanelEvent(ExtensionHostRunningSession* running_session, const UiEventMessage& event,
                                      std::string* error = nullptr) const;
    void stopSession(ExtensionHostRunningSession* running_session) const;

private:
    ExtensionSdkService* sdk_service_ = nullptr;
};

class AFTERNIGHTCONTROLS_API ExtensionHostLaunchCoordinator : public IExtensionSessionFeed
{
public:
    // The application's side of a managed session: how a result lands in the target view (recorded as a history
    // step), in a preview view or in a new view, which masks and weights a request carries beside the source, and
    // which rectangle a preview is computed over. The source itself is the target view's document.
    using ManagedResultPresenter =
        std::function<void(ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&)>;
    using ManagedGeneratedResultPresenter = std::function<ImageViewControl*(
        ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&)>;
    using ManagedPreviewPresenter =
        std::function<ImageViewControl*(ImageViewControl*, ImageViewControl*, an::consumer::ImageAsset&&)>;
    using ManagedAuxiliaryBufferProvider = std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>;
    using ManagedOutputMaskPresenter = std::function<void(ImageViewControl*, std::vector<ManagedNamedImageResult>&&)>;
    using ManagedPreviewSubAreaProvider = std::function<std::optional<QRect>(ImageViewControl*)>;
    using ManagedViewNameProvider = std::function<std::string(ImageViewControl*)>;
    using SessionPumpWakeCallback = std::function<void()>;

    ExtensionHostLaunchCoordinator();
    ExtensionHostLaunchCoordinator(const ExtensionHostLaunchCoordinator&) = delete;
    ExtensionHostLaunchCoordinator& operator=(const ExtensionHostLaunchCoordinator&) = delete;
    ExtensionHostLaunchCoordinator(ExtensionHostLaunchCoordinator&&) noexcept = delete;
    ExtensionHostLaunchCoordinator& operator=(ExtensionHostLaunchCoordinator&&) noexcept = delete;
    ~ExtensionHostLaunchCoordinator() override;

    // ---- IExtensionSessionFeed: the sessions as a shell shows them. A session is told of once it starts, again each
    // time where it stands changes - at a launch, at each pump - and once more when it ends.

    [[nodiscard]] std::vector<ExtensionSessionStanding> sessionStandings() const override;
    std::size_t observeSessions(Observer observer) override;
    void stopObservingSessions(std::size_t token) override;
    bool raiseSessionWindow(std::string_view session_id, std::string* error = nullptr) override;
    void closeSession(std::string_view session_id) override;

    void configureSdkService(GlobalSettingsPersistence* global_settings_persistence,
                             ExtensionSdkViewStateProvider view_state_provider = {},
                             ExtensionSdkViewSnapshotProvider view_snapshot_provider = {},
                             ExtensionSdkThemeSnapshotProvider theme_snapshot_provider = {},
                             ExtensionSdkUiInteractionHandler ui_interaction_handler = {},
                             ExtensionSdkViewBindingHandler view_binding_handler = {},
                             ExtensionSdkUiOpenImageHandler ui_open_image_handler = {},
                             ExtensionWorkflowOutputHandler workflow_output_handler = {});
    void setManagedImageHooks(ManagedResultPresenter result_presenter, ManagedPreviewPresenter preview_presenter = {},
                              ManagedAuxiliaryBufferProvider auxiliary_buffer_provider = {},
                              ManagedOutputMaskPresenter output_mask_presenter = {},
                              ManagedPreviewSubAreaProvider preview_sub_area_provider = {},
                              ManagedGeneratedResultPresenter generated_result_presenter = {});
    void setManagedViewNameProvider(ManagedViewNameProvider view_name_provider);
    void setSessionPumpWakeCallback(SessionPumpWakeCallback callback);

    /// States whether a launch of a `runtime` extension that describes its parameters is drawn by the application -
    /// a hosted panel, its process an `ExtensionHostedProcess` a shell shows as a page - rather than in a window of its
    /// host. By default the application draws it when a shell takes processes into pages (`IAppContext`'s process
    /// presenter), and the host draws it otherwise, as it always did. Null states the default again.
    void setHostsPanels(std::function<bool()> policy);

    /// The hosted processes of the sessions running now, in the order they started.
    [[nodiscard]] std::vector<ExtensionHostedProcess*> hostedProcesses() const;

    /// Makes the page of an extension's process whose environment is still being prepared, as a hosted process starts -
    /// its body saying so - over the image it will run on: the launch that follows the preparation starts its session
    /// into it. Null where the process would not be drawn by the application, or with no image.
    ///
    /// @param descriptor_id the process's descriptor.
    /// @param display_name its name.
    /// @param capabilities what its manifest declares of it.
    /// @param control the image it runs on.
    ExtensionHostedProcess* openStartingPage(const std::string& descriptor_id, const std::string& display_name,
                                             const ExtensionCapabilities& capabilities, ImageViewControl* control);

    /// How long a launch's parameter schema read may run before the launch is refused, unless a caller states another:
    /// the bound on a host runtime's start, since the read starts one and ends when its host exits.
    static constexpr int kDefaultParameterSchemaReadBoundMs = 60000;
    /// States how long a parameter schema read begun from now on may run. A bound that is not positive keeps the one
    /// in force.
    void setParameterSchemaReadBoundMs(int bound_ms) noexcept;

    /// Asks the application's settings, on a worker, for the previews a second the Processing page states. The
    /// settings store answers on a thread of its own, so a launch never asks it on the GUI thread: a native window's
    /// launch passes its host what the settings answered last - the default before they have answered - and asks again
    /// for the launches after it. The application asks once its settings store is open and whenever its settings are
    /// applied. A request while a read is on its way is not started twice: the read reads once more before it ends, so
    /// what was applied meanwhile is read. Without an application context, nothing is asked.
    void refreshPreviewFrameRate();

    /// The previews a second the next native window's launch passes its host: what the settings answered last, or the
    /// default before they have answered.
    [[nodiscard]] std::uint32_t previewFrameRate() const noexcept;

    void bindSingleImageDescriptor(process_registry::ProcessDescriptor* descriptor,
                                   const ExtensionHostLaunchBinding& binding);
    void bindWorkflowDescriptor(process_registry::ProcessDescriptor* descriptor,
                                const ExtensionHostLaunchBinding& binding);

    // A launch whose binding builds its parameter window from the extension's schema reads the schema first, through a
    // host of its own that imports the extension, once per binding and settings in a run. The read runs beside the
    // event loop and the launch goes on when it lands, so a launch that starts one answers true at once and its session
    // starts later; a failure after that is recorded, logged and, for a single-image launch, told to the person as the
    // menu tells a refused one. A read counts as the descriptor's session until it lands.
    [[nodiscard]] bool launchBoundDescriptor(const ExtensionHostLaunchBinding& binding, ImageViewControl* control,
                                             std::string* error = nullptr);
    [[nodiscard]] bool launchBoundDescriptor(const ExtensionHostLaunchBinding& binding, ImageViewControl* control,
                                             const ExtensionHostLaunchOptions& options, std::string* error = nullptr);
    [[nodiscard]] bool launchWorkflowDescriptor(const ExtensionHostLaunchBinding& binding,
                                                std::string* error = nullptr);
    [[nodiscard]] bool submitExecuteForDescriptor(std::string_view descriptor_id, ImageViewControl* control,
                                                  ExtensionHostExecutionDispatchResult* dispatch_result = nullptr,
                                                  std::string* error = nullptr);
    void pumpSessions();
    void stopAllSessions();

    [[nodiscard]] bool hasActiveSessionForDescriptor(std::string_view descriptor_id,
                                                     const ImageViewControl* control) const;
    [[nodiscard]] bool hasReadySessionForDescriptor(std::string_view descriptor_id,
                                                    const ImageViewControl* control) const;
    [[nodiscard]] bool hasReadySessionForDescriptor(std::string_view descriptor_id) const;
    [[nodiscard]] std::size_t activeSessionCount() const noexcept;
    // Whether a launch of the descriptor is reading its parameter schema, before its session starts.
    [[nodiscard]] bool isReadingParameterSchemaFor(std::string_view descriptor_id) const;
    [[nodiscard]] bool bindSessionTargetView(std::string_view session_id, std::string_view view_name,
                                             std::string* error = nullptr);
    void handleThemeChanged();
    void handleViewControlCreated(const ImageViewControl* control, std::string_view view_id, std::string_view view_name,
                                  std::string_view file_name = {}, std::string_view selector_tooltip = {});
    void handleViewControlDestroyed(const ImageViewControl* control, std::string_view view_id = {});
    void handleViewControlUpdated(const ImageViewControl* control);
    void handleViewControlRenamed(const std::string& new_name, const std::string& old_name,
                                  const ImageViewControl* control, std::string_view file_name = {},
                                  std::string_view selector_tooltip = {});

private:
    [[nodiscard]] bool startExecuteForSession(ExtensionSessionController* session,
                                              ExtensionHostExecutionDispatchResult* dispatch_result = nullptr,
                                              std::string* error = nullptr);
    [[nodiscard]] bool submitPreviewForDescriptor(std::string_view descriptor_id, ImageViewControl* control,
                                                  std::string* error = nullptr);
    [[nodiscard]] bool startPreviewForSession(ExtensionSessionController* session, std::string* error = nullptr);
    void stopManagedSession(ExtensionSessionController* session, bool record_forced_stop = false);
    void stopManagedSessionBySessionId(std::string_view session_id);
    void recordReadySessionLaunch(ExtensionSessionController* session);
    void armSessionPumpWake(ExtensionSessionController* session);
    /// Tells the observers where every session stands now.
    void reportSessions();

    /// Whether a launch of a binding is a hosted panel's.
    [[nodiscard]] bool hostsPanels(const ExtensionHostLaunchBinding& binding) const;

    /// Binds a session whose panel the application draws to its process - one made now, the one started again, or a
    /// page made while its environment was prepared (`adopted`), which is given the session's hooks - and shows a
    /// process made now.
    void bindHostedProcess(ExtensionSessionController* session, const ExtensionHostLaunchBinding& binding,
                           ImageViewControl* control, ExtensionHostedProcess* restarted, bool adopted = false);

    /// Makes a hosted process of a binding over an image, under a launch scope stating where it is shown.
    [[nodiscard]] ExtensionHostedProcess* makeHostedProcess(const ExtensionHostLaunchBinding& binding,
                                                            ImageViewControl* control);

    /// Takes the page made for a process while its environment was prepared, when it is over the image the launch is
    /// of; null otherwise.
    [[nodiscard]] ExtensionHostedProcess* takeStartingPage(const std::string& descriptor_id, ImageViewControl* control);

    /// The open images as an extension lists them.
    [[nodiscard]] std::vector<std::map<std::string, QVariant>> hostedViewList() const;

    // The parameter schemas launches read and the reads still running; defined beside the launches.
    struct SchemaReads;
    void beginSchemaRead(const ExtensionHostLaunchBinding& binding, ImageViewControl* control,
                         const ExtensionHostLaunchOptions& options, bool workflow);
    void landSchemaRead(std::uint64_t read_id);
    void endSchemaReads(const ImageViewControl* control);

    ExtensionHostManager host_manager_;
    ExtensionSdkService sdk_service_;
    ExtensionHostExecutionGateState execution_gate_;
    std::vector<std::unique_ptr<ExtensionSessionController>> sessions_;
    ExtensionSessionReport session_report_;
    ManagedResultPresenter managed_result_presenter_;
    ManagedGeneratedResultPresenter managed_generated_result_presenter_;
    ManagedPreviewPresenter managed_preview_presenter_;
    ManagedAuxiliaryBufferProvider managed_auxiliary_buffer_provider_;
    ManagedOutputMaskPresenter managed_output_mask_presenter_;
    ManagedPreviewSubAreaProvider managed_preview_sub_area_provider_;
    ManagedViewNameProvider managed_view_name_provider_;
    SessionPumpWakeCallback session_pump_wake_callback_;
    std::function<bool()> hosts_panels_;
    // What opens an extension's settings, kept for the hosted processes, which ask it of no host.
    ExtensionSdkUiInteractionHandler ui_interaction_handler_;
    // Held while the coordinator lives: a hosted process's hooks reach the coordinator only while it does.
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    /// The pages made while their process's environment is prepared, by descriptor, until their launch takes them.
    std::map<std::string, QPointer<QWidget>> starting_pages_;
    int parameter_schema_read_bound_ms_ = kDefaultParameterSchemaReadBoundMs;
    // The rate the settings answered last and whether a read is on its way, shared with the worker that reads them.
    struct PreviewFrameRateRead;
    std::shared_ptr<PreviewFrameRateRead> preview_frame_rate_;
    // Last, so a read still running is stopped, and its landing withdrawn, before anything it would land in goes.
    std::unique_ptr<SchemaReads> schema_reads_;
};

} // namespace an::ui::extensions

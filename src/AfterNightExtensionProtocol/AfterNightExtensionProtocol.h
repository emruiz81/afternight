#pragma once

// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "AfterNightExtensionProtocolExport.h"

#include <QByteArray>
#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::ui::extensions
{

inline constexpr int kExtensionProtocolVersion = 1;
inline constexpr int kExtensionSdkVersion = 1;
inline constexpr std::uint32_t kMaxControlMessageBytes = 16u * 1024u * 1024u;

/// The capability a host states in its `runtime.hello` when it tells the application where the run of the window it
/// shows stands, with `session.activity`. An application never reads that message from a host that did not state it.
inline constexpr std::string_view kSessionActivityCapability = "session_activity";

/// The capability a host states in its `runtime.hello` when it raises the window it shows on `window.raise`. An
/// application never sends that message to a host that did not state it: a host refuses a message it does not know.
inline constexpr std::string_view kWindowRaiseCapability = "window_raise";

/// The longest text a `session.activity` may carry, in characters.
inline constexpr std::size_t kSessionActivityTextLimit = 200u;

/// The capability a host states in its `runtime.hello` when it can serve a panel the application draws, and the
/// application states in its acknowledgement when it draws the session's panel itself. With it in both, the host shows
/// no window of its own: it tells the application the extension's parameters (`ui.describe`) and what its actions
/// answer (`ui.patch`), hears what a person does in the panel (`ui.event`), runs the previews and the executes the
/// application sends, and asks for the panel's task to be put away when the extension hides its process window
/// (`ui.hide`). Without it in both, none of those messages is sent either way. Both hosts
/// state it: the description is protocol data, so the Lite host serves a panel linking nothing it did not link.
inline constexpr std::string_view kHostedPanelCapability = "hosted_panel";

/// The most entries a hosted panel's parameter list (`ui.describe`) may hold.
inline constexpr std::size_t kPanelDescribeEntryLimit = 4096u;

/// The longest reason a hosted panel's action may give for failing (`ui.patch`), in characters.
inline constexpr std::size_t kPanelPatchErrorLimit = 2000u;

/// The capability a host states in its `runtime.hello` when an extension may state, while it runs, the processes it
/// adds to its manifest's (`processes.contribute`). The application refuses that request from a host that did not state
/// it. Both hosts state it: the list is protocol data.
inline constexpr std::string_view kContributedProcessesCapability = "contributed_processes";

/// The most entries an extension's list of contributed processes may hold.
inline constexpr std::size_t kContributedProcessLimit = 512u;

/// The longest name a contributed process may state, in characters.
inline constexpr std::size_t kContributedProcessNameLimit = 80u;

/// The longest group a contributed process may state, in characters.
inline constexpr std::size_t kContributedProcessGroupLimit = 40u;

/// The longest summary a contributed process may state, in characters.
inline constexpr std::size_t kContributedProcessSummaryLimit = 200u;

/// The capabilities a host states in its `runtime.hello`, one for each request an extension may make of the view its
/// session is bound to beyond its pixels: shapes drawn over it (`view.overlay`), its selection area
/// (`view.selection`), how it is shown (`view.display`) and an Undo or a Redo (`view.history`). The application refuses
/// a request from a host that did not state its capability. Both hosts state them: the requests are protocol data.
inline constexpr std::string_view kViewOverlayCapability = "view_overlay";
inline constexpr std::string_view kViewSelectionCapability = "view_selection";
inline constexpr std::string_view kViewDisplayCapability = "view_display";
inline constexpr std::string_view kViewHistoryCapability = "view_history";

/// The most shapes a session may keep over its view.
inline constexpr std::size_t kViewOverlayShapeLimit = 4096u;

/// The most points the shapes a session keeps over its view may hold together.
inline constexpr std::size_t kViewOverlayPointLimit = 262144u;

/// The longest label a shape may carry, in characters.
inline constexpr std::size_t kViewOverlayLabelLimit = 120u;

enum class ProtocolFrameDecodeStatus
{
    NeedMoreData,
    Complete,
    InvalidFrame
};

enum class ProtocolCompatibilityState
{
    Compatible,
    PeerTooOld,
    PeerTooNew,
    Invalid
};

struct ProtocolCompatibilityResult
{
    ProtocolCompatibilityState state = ProtocolCompatibilityState::Invalid;
    int negotiated_version = 0;
    std::string diagnostic;

    [[nodiscard]] bool accepted() const noexcept
    {
        return state == ProtocolCompatibilityState::Compatible;
    }
};

enum class ExtensionProtocolErrorDomain
{
    Runtime,
    Logging,
    Settings,
    Buffers,
    Execution,
    Host,
    Invalid
};

struct AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolError
{
    int code = 0;
    std::string message;
    std::string details;

    [[nodiscard]] bool valid() const noexcept;
};

struct ExtensionProtocolMessage
{
    std::string type;
    std::string session_id;
    std::string request_id;
    QCborMap fields;
};

struct RuntimeHelloMessage
{
    std::string extension_id;
    std::string session_id;
    int protocol_version = kExtensionProtocolVersion;
    int sdk_version = kExtensionSdkVersion;
    std::string sdk_backend = "runtime";
    std::string host_version;
    std::vector<std::string> capabilities;
    std::string session_secret_proof;
    // The extension host advertises it can display a native process window.
    // When true, the host-side session controller stops creating an ExtensionProcessControl
    // and instead lets the extension host own the visible process window.
    bool native_process_window = false;
    // The host emits runtime.ready/runtime.bootstrap_failed after the initial
    // hello acknowledgement instead of completing Python bootstrap first.
    bool bootstrap_status_messages = false;
};

struct RuntimeHelloAckMessage
{
    bool accepted = true;
    std::string session_id;
    int protocol_version = kExtensionProtocolVersion;
    int sdk_version = kExtensionSdkVersion;
    std::string host_version;
    int heartbeat_interval_ms = 5000;
    int heartbeat_miss_limit = 3;
    int cancel_grace_period_ms = 1000;
    std::vector<std::string> capabilities;
    std::optional<ExtensionProtocolError> error;
    // The main app acknowledges the native process window model.
    // When true, the extension host owns the visible process window;
    // no ExtensionProcessControl is created on the host side.
    bool native_process_window = false;
    // Mirrors RuntimeHelloMessage::bootstrap_status_messages when supported.
    bool bootstrap_status_messages = false;
};

struct RuntimePingMessage
{
    std::string session_id;
    std::string request_id;
    int sequence = 0;
};

struct RuntimePongMessage
{
    std::string session_id;
    std::string request_id;
    int sequence = 0;
};

enum class SharedBufferPixelType
{
    UInt8,
    UInt16,
    Float32,
    Float64
};

enum class SharedBufferPixelFormat
{
    Mono,
    RGB,
    BayerRGGB,
    BayerBGGR,
    BayerGRBG,
    BayerGBRG
};

enum class SharedBufferColorSpace
{
    Linear,
    SRGB,
    Grayscale
};

enum class SharedBufferAccessMode
{
    ReadOnly,
    ReadWrite
};

enum class SharedBufferRole
{
    Source,
    Destination,
    Preview,
    Mask,
    Weight
};

struct SharedBufferDescriptor
{
    std::string buffer_id;
    std::string symbolic_name;
    std::string mapping_name;
    std::string mapped_file_path;
    std::uint64_t byte_size = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    SharedBufferPixelType pixel_type = SharedBufferPixelType::Float32;
    SharedBufferPixelFormat pixel_format = SharedBufferPixelFormat::Mono;
    SharedBufferColorSpace color_space = SharedBufferColorSpace::Linear;
    std::uint64_t row_stride = 0;
    std::uint64_t plane_stride = 0;
    SharedBufferAccessMode access_mode = SharedBufferAccessMode::ReadOnly;
    SharedBufferRole buffer_role = SharedBufferRole::Source;
    std::optional<int> offset_x;
    std::optional<int> offset_y;
    std::optional<int> source_width;
    std::optional<int> source_height;
    std::string metadata_sidecar;
};

struct SharedBufferDescriptorValidation
{
    std::string error;

    [[nodiscard]] bool ok() const noexcept
    {
        return error.empty();
    }
};

struct ExecutionExecuteMessage
{
    std::string session_id;
    std::string request_id;
    std::string params_token;
    QCborMap params;
    std::vector<SharedBufferDescriptor> buffers;
};

struct ExecutionPreviewMessage
{
    std::string session_id;
    std::string request_id;
    std::string params_token;
    QCborMap params;
    std::vector<SharedBufferDescriptor> buffers;
};

struct ExecutionCancelMessage
{
    std::string session_id;
    std::string request_id;
    std::string target_request_id;
};

struct ExecutionResultMessage
{
    std::string session_id;
    std::string request_id;
    std::string target_request_id;
    std::string result_kind;
    std::string status;
    std::string detail;
    std::string history_step_name;
    std::string history_step_description;
    std::vector<SharedBufferDescriptor> output_buffers;
};

struct UiEventMessage
{
    std::string session_id;
    std::string widget_id;
    std::string event_name;
    QCborValue value;
};

struct UiValidateMessage
{
    std::string session_id;
    std::string request_id;
    QCborMap params;
};

struct UiValidateResultMessage
{
    std::string session_id;
    std::string request_id;
    bool valid = true;
    QCborMap errors;
};

struct ViewDescribeMessage
{
    std::string session_id;
    std::string request_id;
    std::string view_id;
    std::string view_name;
    std::string file_name;
    SharedBufferDescriptor source_buffer;
    std::vector<SharedBufferDescriptor> auxiliary_buffers;
    QCborMap metadata;
    QCborMap display_settings;
};

struct ViewCommitMessage
{
    std::string session_id;
    std::string request_id;
    std::string view_id;
    std::string history_step_name;
    std::string history_step_description;
    bool allow_resized_output = false;
    QCborMap params;
    std::vector<SharedBufferDescriptor> result_buffers;
};

struct ViewUpdatedMessage
{
    std::string session_id;
    std::string request_id;
    std::string view_id;
    SharedBufferDescriptor source_buffer;
    std::vector<SharedBufferDescriptor> auxiliary_buffers;
    QCborMap metadata;
    QCborMap display_settings;
};

struct ViewCreatedMessage
{
    std::string session_id;
    std::string request_id;
    std::string view_id;
    std::string view_name;
    std::string file_name;
    std::string selector_tooltip;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<int> channels;
};

struct ViewDestroyedMessage
{
    std::string session_id;
    std::string request_id;
    std::string view_id;
};

struct ViewRenamedMessage
{
    std::string session_id;
    std::string request_id;
    std::string view_id;
    std::string view_name;
    std::string file_name;
    std::string selector_tooltip;
};

struct ThemeDescribeMessage
{
    std::string session_id;
    std::string request_id;
    std::string theme_name;
    QCborMap tokens;
};

struct ThemeChangedMessage
{
    std::string session_id;
    std::string request_id;
    std::string theme_name;
    QCborMap tokens;
};

/// What a host tells the application of the run of the window it shows (`session.activity`, host to application): that
/// a run started, how far it has come, and how it ended. Sent only by a host that stated `kSessionActivityCapability`.
struct SessionActivityMessage
{
    std::string session_id;
    std::string request_id;
    /// Whether a run is on its way.
    bool running = false;
    /// How far the run has come, from 0 to 1, when it can tell; a run on its way's alone.
    std::optional<double> progress;
    /// What the run does, or why it failed; plain text of at most `kSessionActivityTextLimit` characters.
    std::string text;
    /// How the run ended - "succeeded", "failed" or "cancelled" - once it is not running; empty while it runs.
    std::string outcome;
};

/// The application asks a host to show the window it shows in front (`window.raise`, application to host). Sent only to
/// a host that stated `kWindowRaiseCapability`.
struct WindowRaiseMessage
{
    std::string session_id;
    std::string request_id;
};

/// What a host serving a hosted panel tells of the extension's parameters once its runtime is ready (`ui.describe`,
/// host to application, answered by nothing): the parameter list the extension's process window reads, each entry a
/// map. Sent only where both sides stated `kHostedPanelCapability`.
struct PanelDescribeMessage
{
    std::string session_id;
    std::string request_id;
    /// The entries, in the extension's order; at most `kPanelDescribeEntryLimit`.
    QCborArray params;
};

/// What a panel's action answered (`ui.patch`, host to application, answered by nothing): the parameters' values the
/// action set, by id, and why it failed when it did. Sent only where both sides stated `kHostedPanelCapability`.
struct PanelPatchMessage
{
    std::string session_id;
    std::string request_id;
    /// The id of the action answered.
    std::string action_id;
    /// The values the action set, by parameter id; none when it set none or failed.
    QCborMap values;
    /// Why the action failed, plain text of at most `kPanelPatchErrorLimit` characters; empty when it did not.
    std::string error;
};

/// The extension hid the process window a hosted panel stands for (`ui.hide`, host to application, answered by
/// nothing): the application puts the panel's task away - its page leaves the screen, the task stays and is shown again
/// as any task is. Sent only where both sides stated `kHostedPanelCapability`. An extension that closes its process
/// window ends its session as one with a window does (`runtime.closed`), and the task closes with it.
struct PanelHideMessage
{
    std::string session_id;
    std::string request_id;
};

/// One process an extension adds while it runs, as it states it: data, never code. Its id is the extension's id, a dot
/// and its `id_suffix`, as a manifest process's is; it runs as the manifest process it names, in that process's host.
struct ContributedProcessEntry
{
    /// A safe identifier - letters, digits, `_` and `.`, no `..` - stated once in the list.
    std::string id_suffix;
    /// What it is listed as: plain text of at most `kContributedProcessNameLimit` characters, not empty.
    std::string display_name;
    /// The submenu of its family's menu it is listed in, plain text of at most `kContributedProcessGroupLimit`
    /// characters; empty for none.
    std::string group;
    /// The registry category whose Process menu it joins, a safe identifier; empty for none.
    std::string category;
    /// What it does, in a line: plain text of at most `kContributedProcessSummaryLimit` characters; empty for none.
    std::string summary;
    /// The `id_suffix` of the manifest process whose class runs it, a safe identifier; empty for the manifest's first.
    std::string host_process;

    [[nodiscard]] bool operator==(const ContributedProcessEntry&) const = default;
};

/// The processes an extension adds while it runs (`processes.contribute`, host to application, answered by
/// `processes.contribute.result` or `processes.contribute.error`): its whole list each time, an empty one withdrawing
/// every entry. Sent only by a host that stated `kContributedProcessesCapability`.
struct ProcessesContributeMessage
{
    std::string session_id;
    std::string request_id;
    /// The entries, in the extension's order; at most `kContributedProcessLimit`.
    std::vector<ContributedProcessEntry> entries;
};

/// A point of a shape, in the pixels of the image it is drawn over: (0, 0) is the image's top-left corner, (w, h) its
/// bottom-right one.
struct ViewOverlayPoint
{
    double x = 0.0;
    double y = 0.0;

    [[nodiscard]] bool operator==(const ViewOverlayPoint&) const = default;
};

/// A shape an extension draws over the view its session is bound to: data, kept by the application for the session.
struct ViewOverlayShape
{
    /// Its points, in order: at least one, each finite. On the wire a flat array, each x followed by its y.
    std::vector<ViewOverlayPoint> points;
    /// A closed polygon (`"polygon"` on the wire), its last point joined to its first; an open line (`"line"`)
    /// otherwise.
    bool closed = false;
    /// Whether a closed polygon is filled with its colour; a line is never filled.
    bool filled = false;
    /// Its colour as 0xAARRGGBB; on the wire `"#rrggbb"`, opaque, or `"#rrggbbaa"`.
    std::uint32_t color = 0xFFFFFFFFu;
    /// Drawn beside the shape: plain text of at most `kViewOverlayLabelLimit` characters; empty for none.
    std::string label;

    [[nodiscard]] bool operator==(const ViewOverlayShape&) const = default;
};

/// What an extension asks of the shapes over its view (`view.overlay`, host to application, answered by
/// `view.overlay.result` - each added shape's id, in order - or `view.overlay.error`), applied in this order: every
/// shape cleared, the shapes of `remove` taken away, the shapes of `add` added. Sent only by a host that stated
/// `kViewOverlayCapability`.
struct ViewOverlayMessage
{
    std::string session_id;
    std::string request_id;
    bool clear = false;
    /// The ids of shapes added earlier, as the application answered them: positive, each stated once.
    std::vector<std::int64_t> remove;
    /// At most `kViewOverlayShapeLimit` shapes holding at most `kViewOverlayPointLimit` points together.
    std::vector<ViewOverlayShape> add;
};

/// A rectangle of the image's pixels: its top-left corner and its size, both sides positive.
struct ViewSelectionRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(const ViewSelectionRect&) const = default;
};

/// What an extension asks of the selection area of the view its session is bound to (`view.selection`, answered by
/// `view.selection.result` - the area as it is now - or `view.selection.error`): on, with the rectangle it is set to or
/// none for the next drag to draw one, or off. Sent only by a host that stated `kViewSelectionCapability`.
struct ViewSelectionMessage
{
    std::string session_id;
    std::string request_id;
    bool enabled = false;
    /// Stated only with `enabled`.
    std::optional<ViewSelectionRect> rect;
};

/// How an extension asks the view its session is bound to be shown (`view.display`, answered by
/// `view.display.result` - the zoom the view took - or `view.display.error`): any of a zoom, a point to centre, fit to
/// window and a display mode, and at least one. Sent only by a host that stated `kViewDisplayCapability`.
struct ViewDisplayMessage
{
    std::string session_id;
    std::string request_id;
    /// The zoom in percent, finite and positive; never stated with `fit`.
    std::optional<double> zoom_percent;
    /// The point of the image to centre, in its pixels; never stated with `fit`.
    std::optional<ViewOverlayPoint> center;
    /// The whole image fitted to the view.
    bool fit = false;
    /// A display mode by the name the view's state reports it under (`image_display_mode`); empty for none.
    std::string mode;
};

/// Which step of its history an extension asks the view its session is bound to go to.
enum class ViewHistoryAction
{
    Undo,
    Redo
};

/// An Undo or a Redo an extension asks of the view its session is bound to (`view.history`, answered by
/// `view.history.result` once it is asked for, or `view.history.error`). Sent only by a host that stated
/// `kViewHistoryCapability`.
struct ViewHistoryMessage
{
    std::string session_id;
    std::string request_id;
    ViewHistoryAction action = ViewHistoryAction::Undo;
};

/// What a person did in a hosted panel, as a host reads the `ui.event` the application sent.
struct HostedPanelEvent
{
    enum class Kind
    {
        /// The parameters changed (`parameters_changed`): the whole set, the keys that changed and why.
        ParametersChanged,
        /// An action was pressed (`clicked`) with the parameters as they stand.
        Action,
        /// The panel's tool configuration resolved a record (`tool_record`).
        ToolRecord
    };

    Kind kind = Kind::ParametersChanged;
    QCborMap params;
    QStringList changedKeys;
    QString reason;
    std::string actionId;
    QCborMap toolRecord;
};

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ProtocolCompatibilityResult
negotiateProtocolVersion(int supported_version, int peer_version);

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API int makeProtocolErrorCode(ExtensionProtocolErrorDomain domain,
                                                                        int local_code) noexcept;
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool isValidProtocolErrorCode(int code) noexcept;
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolErrorDomain classifyProtocolErrorCode(int code) noexcept;
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string formatProtocolErrorCode(int code);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string_view
protocolErrorDomainName(ExtensionProtocolErrorDomain domain) noexcept;

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string_view
sharedBufferPixelTypeName(SharedBufferPixelType value) noexcept;
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string_view
sharedBufferPixelFormatName(SharedBufferPixelFormat value) noexcept;
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string_view
sharedBufferColorSpaceName(SharedBufferColorSpace value) noexcept;
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string_view
sharedBufferAccessModeName(SharedBufferAccessMode value) noexcept;
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string_view sharedBufferRoleName(SharedBufferRole value) noexcept;

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API QCborMap protocolErrorToCborMap(const ExtensionProtocolError& error);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
protocolErrorFromCborValue(const QCborValue& value, ExtensionProtocolError* out_error, std::string* error = nullptr);

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API QCborMap
protocolMessageToCborMap(const ExtensionProtocolMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool protocolMessageFromCborValue(const QCborValue& value,
                                                                                ExtensionProtocolMessage* out_message,
                                                                                std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API QByteArray
serializeProtocolMessage(const ExtensionProtocolMessage& message, std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool deserializeProtocolMessage(const QByteArray& cbor_payload,
                                                                              ExtensionProtocolMessage* out_message,
                                                                              std::string* error = nullptr);

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API QByteArray frameControlPayload(const QByteArray& cbor_payload,
                                                                             std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API QByteArray frameProtocolMessage(const ExtensionProtocolMessage& message,
                                                                              std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ProtocolFrameDecodeStatus
tryConsumeFramedControlPayload(QByteArray* buffered_bytes, QByteArray* cbor_payload, std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ProtocolFrameDecodeStatus tryConsumeFramedProtocolMessage(
    QByteArray* buffered_bytes, ExtensionProtocolMessage* out_message, std::string* error = nullptr);

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const RuntimeHelloMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const RuntimeHelloAckMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const RuntimePingMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const RuntimePongMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ExecutionExecuteMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ExecutionPreviewMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ExecutionCancelMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ExecutionResultMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage toProtocolMessage(const UiEventMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const UiValidateMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const UiValidateResultMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewDescribeMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewCommitMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewUpdatedMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewCreatedMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewDestroyedMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewRenamedMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ThemeDescribeMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ThemeChangedMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const SessionActivityMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const WindowRaiseMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const PanelDescribeMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const PanelPatchMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const PanelHideMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ProcessesContributeMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewOverlayMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewSelectionMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewDisplayMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
toProtocolMessage(const ViewHistoryMessage& message);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
runtimeHelloFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimeHelloMessage* out_message,
                                std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
runtimeHelloAckFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimeHelloAckMessage* out_message,
                                   std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
runtimePingFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimePingMessage* out_message,
                               std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
runtimePongFromProtocolMessage(const ExtensionProtocolMessage& message, RuntimePongMessage* out_message,
                               std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
executionExecuteFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionExecuteMessage* out_message,
                                    std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
executionPreviewFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionPreviewMessage* out_message,
                                    std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
executionCancelFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionCancelMessage* out_message,
                                   std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
executionResultFromProtocolMessage(const ExtensionProtocolMessage& message, ExecutionResultMessage* out_message,
                                   std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool uiEventFromProtocolMessage(const ExtensionProtocolMessage& message,
                                                                              UiEventMessage* out_message,
                                                                              std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
uiValidateFromProtocolMessage(const ExtensionProtocolMessage& message, UiValidateMessage* out_message,
                              std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
uiValidateResultFromProtocolMessage(const ExtensionProtocolMessage& message, UiValidateResultMessage* out_message,
                                    std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewDescribeFromProtocolMessage(const ExtensionProtocolMessage& message, ViewDescribeMessage* out_message,
                                std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewCommitFromProtocolMessage(const ExtensionProtocolMessage& message, ViewCommitMessage* out_message,
                              std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewUpdatedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewUpdatedMessage* out_message,
                               std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewCreatedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewCreatedMessage* out_message,
                               std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewDestroyedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewDestroyedMessage* out_message,
                                 std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewRenamedFromProtocolMessage(const ExtensionProtocolMessage& message, ViewRenamedMessage* out_message,
                               std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
themeDescribeFromProtocolMessage(const ExtensionProtocolMessage& message, ThemeDescribeMessage* out_message,
                                 std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
themeChangedFromProtocolMessage(const ExtensionProtocolMessage& message, ThemeChangedMessage* out_message,
                                std::string* error = nullptr);
/// Reads a `session.activity`, refused whole at the first error: `running` a boolean; a progress, when stated, a number
/// from 0 to 1 and only while running; a text, when stated, plain text of at most `kSessionActivityTextLimit`
/// characters; an outcome, "succeeded", "failed" or "cancelled", once not running and never while running.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
sessionActivityFromProtocolMessage(const ExtensionProtocolMessage& message, SessionActivityMessage* out_message,
                                   std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
windowRaiseFromProtocolMessage(const ExtensionProtocolMessage& message, WindowRaiseMessage* out_message,
                               std::string* error = nullptr);
/// Reads a hosted panel's `ui.describe`, refused whole at the first error: `params` an array of at most
/// `kPanelDescribeEntryLimit` maps.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
panelDescribeFromProtocolMessage(const ExtensionProtocolMessage& message, PanelDescribeMessage* out_message,
                                 std::string* error = nullptr);
/// Reads a hosted panel's `ui.patch`, refused whole at the first error: `action` a string that is not empty, `values` a
/// map, and an `error`, when stated, plain text of at most `kPanelPatchErrorLimit` characters.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
panelPatchFromProtocolMessage(const ExtensionProtocolMessage& message, PanelPatchMessage* out_message,
                              std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool panelHideFromProtocolMessage(const ExtensionProtocolMessage& message,
                                                                                PanelHideMessage* out_message,
                                                                                std::string* error = nullptr);
/// Reads a `processes.contribute`, refused whole at the first error and saying what was wrong: a request id, and
/// `entries` a list `contributedProcessEntriesFromCbor` reads.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
processesContributeFromProtocolMessage(const ExtensionProtocolMessage& message, ProcessesContributeMessage* out_message,
                                       std::string* error = nullptr);

/// A list of contributed processes as CBOR: what `processes.contribute` carries, and what the application keeps. A
/// field left empty is not written.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API QCborArray
contributedProcessEntriesToCbor(const std::vector<ContributedProcessEntry>& entries);

/// Reads a list of contributed processes, refused whole at the first error and saying which entry was wrong and how: an
/// array of at most `kContributedProcessLimit` maps of strings, each naming only the fields `ContributedProcessEntry`
/// has; an `id_suffix` a safe identifier, stated once in the list; a `display_name` that is not empty; the name, the
/// group and the summary plain text within their limits; a category and a host process, when stated, safe identifiers.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
contributedProcessEntriesFromCbor(const QCborValue& value, std::vector<ContributedProcessEntry>* out_entries,
                                  std::string* error = nullptr);

/// Reads a `view.overlay`, refused whole at the first error and saying what was wrong: a request id; no field but
/// `clear` (a bool), `remove` (an array of positive ids, each stated once, at most `kViewOverlayShapeLimit`, never with
/// `clear`) and `add` (an array of at most `kViewOverlayShapeLimit` maps holding at most `kViewOverlayPointLimit`
/// points together); a shape's `kind` `"polygon"` or `"line"`, its `points` a flat array of an even count of finite
/// numbers and at least one point, its `color` `"#rrggbb"` or `"#rrggbbaa"`, `filled` a bool stated only of a polygon,
/// its `label` plain text of at most `kViewOverlayLabelLimit` characters, and no other field.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewOverlayFromProtocolMessage(const ExtensionProtocolMessage& message, ViewOverlayMessage* out_message,
                               std::string* error = nullptr);

/// Reads a `view.selection`, refused whole at the first error and saying what was wrong: a request id; `enabled` a
/// bool; `rect`, stated only with `enabled`, an array of four integers, its width and height positive; no other field.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewSelectionFromProtocolMessage(const ExtensionProtocolMessage& message, ViewSelectionMessage* out_message,
                                 std::string* error = nullptr);

/// Reads a `view.display`, refused whole at the first error and saying what was wrong: a request id; at least one of
/// `zoom` (a finite positive number, in percent), `center` (an array of two finite numbers), `fit` (true) and `mode` (a
/// name of letters, digits and `_`); `fit` with neither `zoom` nor `center`; no other field.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewDisplayFromProtocolMessage(const ExtensionProtocolMessage& message, ViewDisplayMessage* out_message,
                               std::string* error = nullptr);

/// Reads a `view.history`, refused whole at the first error and saying what was wrong: a request id; `action`
/// `"undo"` or `"redo"`; no other field.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
viewHistoryFromProtocolMessage(const ExtensionProtocolMessage& message, ViewHistoryMessage* out_message,
                               std::string* error = nullptr);

/// The name of the capability a host states for a request an extension makes of its view (`view.overlay`,
/// `view.selection`, `view.display`, `view.history`); empty for any other message type.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API std::string_view viewRequestCapability(std::string_view message_type);

/// Whether the application draws the session's panel: its acknowledgement stated `kHostedPanelCapability`.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool servesHostedPanel(const RuntimeHelloAckMessage& ack);

/// Reads a `ui.event` of a hosted panel as either host serves it, refused whole at the first error: an event a host
/// serves - `parameters_changed`, `clicked` or `tool_record` - its value a map of the fields the event states.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
hostedPanelEventFrom(const ExtensionProtocolMessage& message, HostedPanelEvent* event, std::string* error = nullptr);

/// The `ui.describe` telling the application the extension's parameter list.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage
hostedPanelDescription(const std::string& sessionId, const QJsonArray& params);

/// The `ui.patch` answering an action: the values it set, or why it failed - plain text, cut to the protocol's bound.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API ExtensionProtocolMessage hostedActionAnswer(
    const std::string& sessionId, const std::string& actionId, const QCborMap& values, const std::string& error = {});

/// The output the application lent a request of a hosted panel - a writable buffer of the role the request's kind
/// writes, `Preview` for a frame and `Destination` for an Apply - or null when it lent none and the host allocates one,
/// as it does for any other request.
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API const SharedBufferDescriptor*
lentOutputOf(std::string_view requestKind, const std::vector<SharedBufferDescriptor>& buffers);

[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API QCborMap
sharedBufferDescriptorToCborMap(const SharedBufferDescriptor& descriptor);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API bool
sharedBufferDescriptorFromCborValue(const QCborValue& value, SharedBufferDescriptor* out_descriptor,
                                    std::string* error = nullptr);
[[nodiscard]] AFTERNIGHTEXTENSIONPROTOCOL_API SharedBufferDescriptorValidation
validateSharedBufferDescriptor(const SharedBufferDescriptor& descriptor);

} // namespace an::ui::extensions

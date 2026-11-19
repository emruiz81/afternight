// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the full host does differently when the application draws the session's panel (`kHostedPanelCapability`): it
/// shows no window of its own, tells the application the extension's parameter list once its runtime is ready, reads
/// what a person does in the panel, answers an action with what it set, writes a frame or an Apply into the output the
/// application lent it, and answers the extension's settings reads first with what the panel's tool configuration
/// resolved. The window, when there is one, is shown exactly as before.

#include "app/AfterNightExtensionProtocol.h"

#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <string>
#include <string_view>
#include <vector>

namespace an::ui::extensions
{

/// The capabilities the full host states in its hello beside the ones it was launched with: it tells where its window's
/// run stands, raises its window when asked, and can serve a panel the application draws.
[[nodiscard]] std::vector<std::string_view> fullHostCapabilities();

/// Whether the application draws the session's panel: its acknowledgement stated `kHostedPanelCapability`.
[[nodiscard]] bool servesHostedPanel(const RuntimeHelloAckMessage& ack);

/// Whether the host shows its native process window: the application acknowledged the window model, the platform can
/// show a window, and the application does not draw the panel itself.
[[nodiscard]] bool showsNativeWindow(const RuntimeHelloAckMessage& ack, bool headlessPlatform);

/// What a person did in a hosted panel, as the host reads the `ui.event` the application sent.
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

/// Reads a `ui.event` of a hosted panel, refused whole at the first error: an event this host serves, its value a map
/// of the fields the event states.
[[nodiscard]] bool hostedPanelEventFrom(const ExtensionProtocolMessage& message, HostedPanelEvent* event,
                                        std::string* error = nullptr);

/// The `ui.describe` telling the application the extension's parameter list.
[[nodiscard]] ExtensionProtocolMessage hostedPanelDescription(const std::string& sessionId, const QJsonArray& params);

/// The `ui.patch` answering an action: the values it set, or why it failed - plain text, cut to the protocol's bound.
[[nodiscard]] ExtensionProtocolMessage hostedActionAnswer(const std::string& sessionId, const std::string& actionId,
                                                          const QCborMap& values, const std::string& error = {});

/// The output the application lent a request of a hosted panel - a writable buffer of the role the request's kind
/// writes, `Preview` for a frame and `Destination` for an Apply - or null when it lent none and the host allocates one,
/// as it does for any other request.
[[nodiscard]] const SharedBufferDescriptor* lentOutputOf(std::string_view requestKind,
                                                         const std::vector<SharedBufferDescriptor>& buffers);

} // namespace an::ui::extensions

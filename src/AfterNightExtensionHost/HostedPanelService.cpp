// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "HostedPanelService.h"

#include <QCborArray>

#include <algorithm>

namespace an::ui::extensions
{

namespace
{

/// Refuses an event, saying why.
bool refuse(std::string* error, const char* reason)
{
    if (error)
    {
        *error = reason;
    }
    return false;
}

} // namespace

std::vector<std::string_view> fullHostCapabilities()
{
    return {kSessionActivityCapability, kWindowRaiseCapability, kHostedPanelCapability};
}

bool servesHostedPanel(const RuntimeHelloAckMessage& ack)
{
    return std::find(ack.capabilities.begin(), ack.capabilities.end(), kHostedPanelCapability) !=
           ack.capabilities.end();
}

bool showsNativeWindow(const RuntimeHelloAckMessage& ack, const bool headlessPlatform)
{
    return ack.native_process_window && !headlessPlatform && !servesHostedPanel(ack);
}

bool hostedPanelEventFrom(const ExtensionProtocolMessage& message, HostedPanelEvent* event, std::string* error)
{
    UiEventMessage told;
    if (!uiEventFromProtocolMessage(message, &told, error))
    {
        return false;
    }
    if (!told.value.isMap())
    {
        return refuse(error, "a hosted panel's ui.event carries a map");
    }
    const QCborMap value = told.value.toMap();
    HostedPanelEvent read;
    if (told.event_name == "parameters_changed")
    {
        read.kind = HostedPanelEvent::Kind::ParametersChanged;
        const QCborValue params = value.value(QStringLiteral("params"));
        const QCborValue changed = value.value(QStringLiteral("changed"));
        const QCborValue reason = value.value(QStringLiteral("reason"));
        if (!params.isMap() || !changed.isArray() || !reason.isString())
        {
            return refuse(error, "parameters_changed states its params, the keys that changed and why");
        }
        read.params = params.toMap();
        for (const QCborValue& key : changed.toArray())
        {
            if (!key.isString())
            {
                return refuse(error, "parameters_changed names its keys as strings");
            }
            read.changedKeys.push_back(key.toString());
        }
        read.reason = reason.toString();
    }
    else if (told.event_name == "clicked")
    {
        read.kind = HostedPanelEvent::Kind::Action;
        const QCborValue params = value.value(QStringLiteral("params"));
        if (told.widget_id.empty() || !params.isMap())
        {
            return refuse(error, "clicked names its action and states the params");
        }
        read.actionId = told.widget_id;
        read.params = params.toMap();
    }
    else if (told.event_name == "tool_record")
    {
        read.kind = HostedPanelEvent::Kind::ToolRecord;
        read.toolRecord = value;
    }
    else
    {
        return refuse(error, "a hosted panel's ui.event is parameters_changed, clicked or tool_record");
    }
    if (event)
    {
        *event = std::move(read);
    }
    return true;
}

ExtensionProtocolMessage hostedPanelDescription(const std::string& sessionId, const QJsonArray& params)
{
    PanelDescribeMessage described;
    described.session_id = sessionId;
    described.params = QCborArray::fromJsonArray(params);
    return toProtocolMessage(described);
}

ExtensionProtocolMessage hostedActionAnswer(const std::string& sessionId, const std::string& actionId,
                                            const QCborMap& values, const std::string& error)
{
    PanelPatchMessage patch;
    patch.session_id = sessionId;
    patch.action_id = actionId;
    if (error.empty())
    {
        patch.values = values;
    }
    else
    {
        // What the application reads is plain text of a bound: a line break stays, any other control goes.
        QString text = QString::fromStdString(error).left(static_cast<qsizetype>(kPanelPatchErrorLimit));
        text.erase(std::remove_if(text.begin(), text.end(),
                                  [](const QChar character) {
                                      return character.category() == QChar::Other_Control &&
                                             character != QLatin1Char('\n');
                                  }),
                   text.end());
        patch.error = text.isEmpty() ? std::string("The action failed") : text.toStdString();
    }
    return toProtocolMessage(patch);
}

void KeptFrameMappings::keepOnly(const std::vector<SharedBufferDescriptor>& named)
{
    for (auto it = m_kept.begin(); it != m_kept.end();)
    {
        const bool stillNamed = std::any_of(named.begin(), named.end(), [&it](const SharedBufferDescriptor& buffer) {
            return buffer.mapped_file_path == it->first;
        });
        it = stillNamed ? std::next(it) : m_kept.erase(it);
    }
}

bool KeptFrameMappings::open(const SharedBufferManager& manager, const SharedBufferDescriptor& descriptor,
                             SharedImageBuffer* opened, std::string* error)
{
    const auto kept = m_kept.find(descriptor.mapped_file_path);
    an::Result<SharedImageBuffer> buffer =
        kept != m_kept.end() ? manager.openBuffer(descriptor, kept->second) : manager.openBuffer(descriptor);
    if (!buffer.hasValue())
    {
        if (error != nullptr)
        {
            *error = buffer.status().message();
        }
        return false;
    }
    m_kept.insert_or_assign(descriptor.mapped_file_path, buffer->shared());
    if (opened != nullptr)
    {
        *opened = std::move(*buffer);
    }
    return true;
}

bool KeptFrameMappings::keeps(const std::string& mappedFilePath) const
{
    return m_kept.find(mappedFilePath) != m_kept.end();
}

std::size_t KeptFrameMappings::size() const noexcept
{
    return m_kept.size();
}

const SharedBufferDescriptor* lentOutputOf(const std::string_view requestKind,
                                           const std::vector<SharedBufferDescriptor>& buffers)
{
    const SharedBufferRole role = requestKind == "preview" ? SharedBufferRole::Preview : SharedBufferRole::Destination;
    const auto lent = std::find_if(buffers.begin(), buffers.end(), [role](const SharedBufferDescriptor& buffer) {
        return buffer.buffer_role == role && buffer.access_mode == SharedBufferAccessMode::ReadWrite;
    });
    return lent != buffers.end() ? &*lent : nullptr;
}

} // namespace an::ui::extensions

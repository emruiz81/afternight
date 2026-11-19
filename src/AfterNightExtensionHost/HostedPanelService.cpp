// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "HostedPanelService.h"

#include <algorithm>

namespace an::ui::extensions
{

std::vector<std::string_view> fullHostCapabilities()
{
    return {kSessionActivityCapability,      kWindowRaiseCapability, kHostedPanelCapability,
            kContributedProcessesCapability, kViewOverlayCapability, kViewSelectionCapability,
            kViewDisplayCapability,          kViewHistoryCapability};
}

bool showsNativeWindow(const RuntimeHelloAckMessage& ack, const bool headlessPlatform, const bool noProcessWindow)
{
    return ack.native_process_window && !headlessPlatform && !noProcessWindow && !servesHostedPanel(ack);
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

} // namespace an::ui::extensions

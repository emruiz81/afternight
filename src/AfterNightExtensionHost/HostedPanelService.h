// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the full host does differently when the application draws the session's panel (`kHostedPanelCapability`): it
/// shows no window of its own, tells the application the extension's parameter list once its runtime is ready, reads
/// what a person does in the panel, answers an action with what it set, writes a frame or an Apply into the output the
/// application lent it, and answers the extension's settings reads first with what the panel's tool configuration
/// resolved. The window, when there is one, is shown exactly as before. How a panel's events are read, its description
/// and its answers written and its lent output found is the protocol's (`servesHostedPanel`, `hostedPanelEventFrom`,
/// `hostedPanelDescription`, `hostedActionAnswer`, `lentOutputOf`), which the Lite host shares.

#include "app/AfterNightExtensionProtocol.h"
#include "app/SharedBufferManager.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace an::ui::extensions
{

/// The capabilities the full host states in its hello beside the ones it was launched with: it tells where its window's
/// run stands, raises its window when asked, can serve a panel the application draws, and carries the processes the
/// extension contributes.
[[nodiscard]] std::vector<std::string_view> fullHostCapabilities();

/// Whether the host shows its native process window: the application acknowledged the window model, the platform can
/// show a window, the application does not draw the panel itself, and the extension states it has an interface
/// (`capabilities.interface` is not `none`, which the application launches the host with `--no-process-window` for).
[[nodiscard]] bool showsNativeWindow(const RuntimeHelloAckMessage& ack, bool headlessPlatform,
                                     bool noProcessWindow = false);

/// The mappings a host serving a hosted panel keeps of the files the application lends its frames, from one request to
/// the next: the application's frame pool lends the same files frame after frame, and a file mapped once keeps its
/// pages in the host, where one mapped anew for every frame is touched afresh page by page.
///
/// The agreement with the application: a request lets go every file it does not name before it is answered - an
/// Apply every one, since its files are its own - so a file the application's pool let go is unmapped here once a
/// request sent after it is answered, and the application removes it then (`PreviewFrameBufferPool::requestAnswered`).
class KeptFrameMappings
{
public:
    /// Lets go every file the request does not name, before its buffers are opened.
    void keepOnly(const std::vector<SharedBufferDescriptor>& named);

    /// Opens a buffer a frame was lent, through the mapping kept of its file when there is one, and keeps the mapping
    /// for the next request.
    [[nodiscard]] bool open(const SharedBufferManager& manager, const SharedBufferDescriptor& descriptor,
                            SharedImageBuffer* opened, std::string* error);

    /// Whether a file is kept mapped.
    [[nodiscard]] bool keeps(const std::string& mappedFilePath) const;

    /// How many files are kept mapped.
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::map<std::string, SharedImageBuffer, std::less<>> m_kept;
};

} // namespace an::ui::extensions

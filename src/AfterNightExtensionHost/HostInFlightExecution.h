// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The request the application has in flight at the host - an execute, a preview or a workflow - and the native
/// window's frames held for its run.

#pragma once

#include "ExtensionNativeProcessWindow.h"

#include "app/AfterNightExtensionProtocol.h"

#include <QCborMap>
#include <QPointer>
#include <QtGlobal>

#include <string>
#include <utility>
#include <vector>

namespace an::ui::extensions
{

/// The request the application has in flight at the host, from its arrival to its result, and the native window's
/// frames held for its run. The run is the GUI thread's own, with no frame of the window in the extension: the frames
/// are held from the request's due time, so the frame on its way lands and no other starts, and let go right after the
/// extension's run - before a commit may close the window and take the frames' buffers with it - or when the request
/// ends another way. Every end goes through `clear`, which lets the frames go: a cancel that reached the host while
/// the request waited for the frame on its way once left them held, the window's previews paused for good and its
/// Apply disabled with them.
class HostInFlightExecution
{
public:
    /// Whether a request is in flight.
    [[nodiscard]] bool active() const noexcept
    {
        return m_Active;
    }

    /// Records the request that arrived, due when the host's lifetime reaches `dueElapsedMs`.
    void begin(std::string requestId, std::string kind, std::string paramsToken, QCborMap params,
               std::vector<SharedBufferDescriptor> buffers, qint64 dueElapsedMs)
    {
        m_Active = true;
        m_RequestId = std::move(requestId);
        m_Kind = std::move(kind);
        m_ParamsToken = std::move(paramsToken);
        m_Params = std::move(params);
        m_Buffers = std::move(buffers);
        m_DueElapsedMs = dueElapsedMs;
    }

    [[nodiscard]] const std::string& requestId() const noexcept
    {
        return m_RequestId;
    }

    /// "execute", "preview" or "workflow".
    [[nodiscard]] const std::string& kind() const noexcept
    {
        return m_Kind;
    }

    [[nodiscard]] const std::string& paramsToken() const noexcept
    {
        return m_ParamsToken;
    }

    [[nodiscard]] const QCborMap& params() const noexcept
    {
        return m_Params;
    }

    [[nodiscard]] const std::vector<SharedBufferDescriptor>& buffers() const noexcept
    {
        return m_Buffers;
    }

    /// When the request is due, as the host's lifetime counts; negative when none is in flight.
    [[nodiscard]] qint64 dueElapsedMs() const noexcept
    {
        return m_DueElapsedMs;
    }

    /// Holds the window's frames for the run, once: a hold while they are held holds nothing more. Nothing without a
    /// window.
    void holdFrames(ExtensionNativeProcessWindow* window)
    {
        if (m_FramesHeld || window == nullptr)
        {
            return;
        }
        m_Window = window;
        m_FramesHeld = true;
        window->pausePreviews();
    }

    /// Whether the frames are held for the run.
    [[nodiscard]] bool framesHeld() const noexcept
    {
        return m_FramesHeld;
    }

    /// Lets the frames go, once; nothing when they are not held, or the window has gone.
    void releaseFrames()
    {
        if (!m_FramesHeld)
        {
            return;
        }
        m_FramesHeld = false;
        if (m_Window)
        {
            m_Window->resumePreviews();
        }
        m_Window = nullptr;
    }

    /// Ends the request, however it ended: lets the frames go and forgets it.
    void clear()
    {
        releaseFrames();
        m_Active = false;
        m_RequestId.clear();
        m_Kind.clear();
        m_ParamsToken.clear();
        m_Params.clear();
        m_Buffers.clear();
        m_DueElapsedMs = -1;
    }

private:
    bool m_Active = false;
    std::string m_RequestId;
    std::string m_Kind;
    std::string m_ParamsToken;
    QCborMap m_Params;
    std::vector<SharedBufferDescriptor> m_Buffers;
    qint64 m_DueElapsedMs = -1;
    bool m_FramesHeld = false;
    QPointer<ExtensionNativeProcessWindow> m_Window;
};

} // namespace an::ui::extensions

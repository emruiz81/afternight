#pragma once

// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Shared inline framing utility used by both the host-side manager
// (ExtensionHostManager) and the extension host executable
// (AfterNightExtensionHost).  Extracted to a single header so the
// framing contract is maintained in one place.

#include "AfterNightExtensionProtocol.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QLocalSocket>

#include <algorithm>
#include <string>

namespace an::ui::extensions
{

// Reads a single length-prefixed CBOR protocol message from the given
// QLocalSocket, blocking up to timeout_ms.  Any bytes received beyond the
// first complete frame are written to *leftover (if non-null) so the
// caller can prepend them to the next buffered read.
inline bool readFramedMessage(QLocalSocket* socket, const int timeout_ms, ExtensionProtocolMessage* out_message,
                              std::string* error, QByteArray* leftover = nullptr)
{
    if (!socket)
    {
        if (error)
        {
            *error = "Extension host handshake socket is null";
        }
        return false;
    }

    QByteArray buffered = socket->readAll();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() <= timeout_ms)
    {
        const auto status = tryConsumeFramedProtocolMessage(&buffered, out_message, error);
        if (status == ProtocolFrameDecodeStatus::Complete)
        {
            if (leftover)
            {
                *leftover = std::move(buffered);
            }
            return true;
        }
        if (status == ProtocolFrameDecodeStatus::InvalidFrame)
        {
            return false;
        }

        const int remaining_ms = (std::max)(1, timeout_ms - static_cast<int>(timer.elapsed()));
        if (!socket->waitForReadyRead(remaining_ms))
        {
            break;
        }
        buffered.append(socket->readAll());
    }

    if (error)
    {
        *error = "Timed out waiting for the extension host protocol message";
    }
    return false;
}

} // namespace an::ui::extensions

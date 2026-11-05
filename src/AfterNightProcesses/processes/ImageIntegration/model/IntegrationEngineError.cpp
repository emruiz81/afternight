// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An engine failure mapped to the integration workflow's error.

#include "stdafx.hpp"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecError.h>

#include <QStringList>

#include <optional>

namespace ImageIntegration
{

namespace
{

/// The workflow's code for a codec's reason, or nothing when the reason does not refine the stage's.
[[nodiscard]] std::optional<IntegrationErrorCode> codeForCodecReason(const an::CodecFailure reason)
{
    switch (reason)
    {
    case an::CodecFailure::UnknownFormat:
    case an::CodecFailure::Unavailable:
    case an::CodecFailure::UnsupportedOperation:
    case an::CodecFailure::UnsupportedRepresentation:
        return IntegrationErrorCode::InvalidFileFormat;
    case an::CodecFailure::MetadataUnreadable:
    case an::CodecFailure::PayloadUnreadable:
        return IntegrationErrorCode::FileNotReadable;
    case an::CodecFailure::DestinationUnavailable:
    case an::CodecFailure::ShortWrite:
    case an::CodecFailure::FlushFailure:
    case an::CodecFailure::PublishFailure:
    case an::CodecFailure::ExistingDestination:
    case an::CodecFailure::PendingConflict:
        return IntegrationErrorCode::FileNotWritable;
    case an::CodecFailure::InvalidCapabilities:
    case an::CodecFailure::DuplicateFormat:
    case an::CodecFailure::DescriptorMismatch:
    case an::CodecFailure::UnsafeLifetime:
    case an::CodecFailure::ForeignAllocation:
    case an::CodecFailure::InvalidMetadata:
    case an::CodecFailure::CallbackFailure:
    case an::CodecFailure::InvalidIdentity:
        break;
    }
    return std::nullopt;
}

/// The workflow's code one link of the chain states, or nothing.
[[nodiscard]] std::optional<IntegrationErrorCode> codeForLink(const an::Status& link)
{
    switch (link.code())
    {
    case an::ErrorCode::Cancelled:
        return IntegrationErrorCode::Cancelled;
    case an::ErrorCode::DeadlineExceeded:
        return IntegrationErrorCode::Timeout;
    case an::ErrorCode::AllocationFailed:
    case an::ErrorCode::MemoryBudgetExceeded:
        return IntegrationErrorCode::MemoryExhausted;
    default:
        break;
    }
    if (const std::optional<an::CodecFailure> reason = an::codecFailureReason(link); reason.has_value())
    {
        return codeForCodecReason(*reason);
    }
    return std::nullopt;
}

} // namespace

IntegrationErrorCode integrationErrorCodeFor(const an::Status& failure, const IntegrationErrorCode stageFailure)
{
    if (failure.ok())
    {
        return IntegrationErrorCode::None;
    }
    // The layer that noticed first, then what it was caused by: a cancellation reported through a callback is still
    // a cancellation.
    for (an::Status link = failure; link.failed(); link = link.cause())
    {
        if (const std::optional<IntegrationErrorCode> code = codeForLink(link); code.has_value())
        {
            return *code;
        }
    }
    return stageFailure;
}

QString engineFailureText(const an::Status& failure)
{
    QStringList parts;
    for (an::Status link = failure; link.failed(); link = link.cause())
    {
        const QString text = QString::fromUtf8(link.message()).trimmed();
        if (!text.isEmpty() && (parts.isEmpty() || parts.constLast() != text))
        {
            parts << text;
        }
    }
    return parts.join(QStringLiteral(": "));
}

IntegrationError integrationErrorFromEngine(const an::Status& failure, const IntegrationErrorCode stageFailure,
                                            const QString& message, const QString& filePath)
{
    if (failure.ok())
    {
        return IntegrationError::success();
    }
    return IntegrationError(integrationErrorCodeFor(failure, stageFailure), message, filePath,
                            engineFailureText(failure));
}

} // namespace ImageIntegration

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - building a sequence failure, and reading its typed reason back.
//
// The reason travels as a count in the failure's context, so a caller reads it without parsing a
// message. Building a failure never throws: when the detailed failure cannot be allocated, only the
// code is kept.

#include <AfterNightEngine/IO/SequenceError.h>

#include <utility>

namespace an
{
Status sequenceFailure(ErrorCode code, SequenceFailure reason, Status cause) noexcept
{
    try
    {
        return Status::failure(
            code,
            {.message = errorCodeDescription(code),
             .operation = "sequence",
             .domain = ErrorDomain::Io,
             .context = {StatusContextField::ofCount(ContextKey::SequenceReason, static_cast<std::uint64_t>(reason))}},
            std::move(cause));
    }
    catch (...)
    {
        return Status::failure(code);
    }
}

std::optional<SequenceFailure> sequenceFailureReason(const Status& status) noexcept
{
    if (status.ok() || status.domain() != ErrorDomain::Io)
        return std::nullopt;
    const auto* field = status.findContext(ContextKey::SequenceReason);
    if (field == nullptr || field->kind != ContextValueKind::Count ||
        field->count > static_cast<std::uint64_t>(SequenceFailure::EncoderFailure))
        return std::nullopt;
    return static_cast<SequenceFailure>(field->count);
}
} // namespace an

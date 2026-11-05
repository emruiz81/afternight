// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - building a codec refusal with its reason, format and path as typed
// context, and reading the reason back without parsing text.

#include <AfterNightEngine/IO/CodecError.h>

#include <string>
#include <utility>

namespace an
{
Status codecFailure(ErrorCode code, CodecFailure reason, CodecFormat format, std::string_view operation,
                    std::string_view path, Status cause)
{
    return Status::failure(
        code,
        {.message = errorCodeDescription(code),
         .operation = std::string(operation),
         .domain = ErrorDomain::Io,
         .context = {StatusContextField::ofCount(ContextKey::CodecFormat, static_cast<std::uint64_t>(format)),
                     StatusContextField::ofCount(ContextKey::CodecReason, static_cast<std::uint64_t>(reason)),
                     StatusContextField::ofText(ContextKey::Path, std::string(path))}},
        std::move(cause));
}

std::optional<CodecFailure> codecFailureReason(const Status& status) noexcept
{
    if (status.ok() || status.domain() != ErrorDomain::Io)
        return std::nullopt;
    const auto* value = status.findContext(ContextKey::CodecReason);
    if (value == nullptr || value->kind != ContextValueKind::Count ||
        value->count > static_cast<std::uint64_t>(CodecFailure::ExistingDestination))
        return std::nullopt;
    return static_cast<CodecFailure>(value->count);
}
} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - building the immutable codec registry.
//
// The state holds one slot per named format, indexed by the format's value less one, so a lookup is
// an array index with no search, and registration order cannot change what a format finds. Each
// adapter's capability record is read once, across the exception boundary, and copied beside it.

#include <AfterNightEngine/IO/CodecRegistry.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/ImageCodec.h>

#include <array>
#include <new>
#include <utility>

namespace an
{
class CodecRegistryState
{
public:
    std::array<std::shared_ptr<const ImageCodec>, kCodecFormatCount> codecs{};
    std::array<CodecCapabilities, kCodecFormatCount> capabilities{};
    std::uint32_t count = 0u;
};

CodecRegistry::CodecRegistry(std::shared_ptr<const CodecRegistryState> state) noexcept : m_state(std::move(state)) {}
CodecRegistry::~CodecRegistry() = default;
CodecRegistry::CodecRegistry(CodecRegistry&& other) noexcept = default;

Result<std::shared_ptr<const CodecRegistry>>
CodecRegistry::create(std::span<const std::shared_ptr<const ImageCodec>> codecs)
{
    if (codecs.size() > kCodecFormatCount)
        return Status::failure(ErrorCode::LimitExceeded,
                               {.message = "too many codec registrations",
                                .operation = "CodecRegistry::create",
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofCount(ContextKey::Requested, codecs.size()),
                                            StatusContextField::ofCount(ContextKey::Limit, kCodecFormatCount)}});
    try
    {
        auto state = std::make_shared<CodecRegistryState>();
        for (const auto& codec : codecs)
        {
            if (codec == nullptr)
                return Status::failure(ErrorCode::InvalidArgument);
            const auto read = evaluateAcrossBoundary({"ImageCodec", "ImageCodec::capabilities", ErrorDomain::Io},
                                                     [&] { return codec->capabilities(); });
            if (!read.hasValue())
                return read.status();
            const auto capabilities = *read;
            if (!validCodecCapabilities(capabilities))
                return codecFailure(ErrorCode::InvalidArgument, CodecFailure::InvalidCapabilities, capabilities.format,
                                    "CodecRegistry::create");
            const auto index = static_cast<std::uint32_t>(capabilities.format) - 1u;
            if (state->codecs[index] != nullptr)
                return codecFailure(ErrorCode::InvalidArgument, CodecFailure::DuplicateFormat, capabilities.format,
                                    "CodecRegistry::create");
            state->codecs[index] = codec;
            state->capabilities[index] = capabilities;
            ++state->count;
        }
        return std::shared_ptr<const CodecRegistry>(std::make_shared<CodecRegistry>(CodecRegistry{std::move(state)}));
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

std::optional<CodecCapabilities> CodecRegistry::capabilities(CodecFormat format) const noexcept
{
    return find(format) != nullptr ? std::optional{m_state->capabilities[static_cast<std::uint32_t>(format) - 1u]}
                                   : std::nullopt;
}

const ImageCodec* CodecRegistry::find(CodecFormat format) const noexcept
{
    if (m_state == nullptr || !isCodecFormat(format) || format == CodecFormat::Unknown)
        return nullptr;
    return m_state->codecs[static_cast<std::uint32_t>(format) - 1u].get();
}

std::uint32_t CodecRegistry::size() const noexcept
{
    return m_state != nullptr ? m_state->count : 0u;
}
} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/SampleBuffer.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

// Two things in this file are decisions rather than mechanics.
//
// **The base address is cached rather than derived.** Owned bytes could be reached through the
// storage on every call, and then every accessor would branch on the ownership before it could
// answer. Caching it means one member answers for all four kinds, and it is safe because the
// storage is never reshaped or released while this buffer holds it - the only paths that give it up
// are `releaseOwned`, `release` and a move, and each clears the cached address in the same call.
//
// **A null lifetime token is refused rather than accepted as a convenience.** Mapped and
// decoder-owned bytes are released when their token is. Memory whose release nothing here performs
// is what `Borrowed` describes, so letting a caller declare mapped bytes with no token would produce
// a buffer that claims a release it cannot perform.

namespace an
{
namespace
{

/// The refusal for an argument the caller cannot have meant.
///
/// @param message what is wrong, for a person.
/// @param parameter which argument, spelled as the caller wrote it.
/// @param operation the engine operation to name in the failure.
/// @return an `ErrorCode::InvalidArgument` failure naming the argument.
[[nodiscard]] Status refuseArgument(const char* message, const char* parameter, const char* operation)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

} // namespace

const char* sampleBufferOwnershipName(SampleBufferOwnership ownership) noexcept
{
    switch (ownership)
    {
    case SampleBufferOwnership::Owned:
        return "Owned";
    case SampleBufferOwnership::Mapped:
        return "Mapped";
    case SampleBufferOwnership::DecoderOwned:
        return "DecoderOwned";
    case SampleBufferOwnership::Borrowed:
        return "Borrowed";
    }
    return "UnknownSampleBufferOwnership";
}

SampleBuffer::SampleBuffer() noexcept = default;

SampleBuffer::~SampleBuffer() = default;

SampleBuffer::SampleBuffer(SampleBuffer&& other) noexcept
    : m_owned(std::move(other.m_owned)), m_lifetime(std::move(other.m_lifetime)), m_data(other.m_data),
      m_byteCount(other.m_byteCount), m_ownership(other.m_ownership), m_writable(other.m_writable)
{
    other.m_data = nullptr;
    other.m_byteCount = 0u;
    other.m_ownership = SampleBufferOwnership::Borrowed;
    other.m_writable = false;
}

SampleBuffer& SampleBuffer::operator=(SampleBuffer&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    release();
    m_owned = std::move(other.m_owned);
    m_lifetime = std::move(other.m_lifetime);
    m_data = other.m_data;
    m_byteCount = other.m_byteCount;
    m_ownership = other.m_ownership;
    m_writable = other.m_writable;
    other.m_data = nullptr;
    other.m_byteCount = 0u;
    other.m_ownership = SampleBufferOwnership::Borrowed;
    other.m_writable = false;
    return *this;
}

Result<SampleBuffer> SampleBuffer::createOwned(const EngineContext& engine, const ImageLayout& layout,
                                               StorageInitialization initialization)
{
    Result<ImageStorageBytes> storage = ImageStorageBytes::create(engine, layout, initialization);
    if (!storage.hasValue())
    {
        return storage.status();
    }
    return adopt(std::move(*storage));
}

SampleBuffer SampleBuffer::adopt(ImageStorageBytes&& storage) noexcept
{
    SampleBuffer buffer;
    if (!storage.valid())
    {
        return buffer;
    }
    buffer.m_byteCount = storage.capacityBytes();
    buffer.m_owned = std::move(storage);
    buffer.m_data = buffer.m_owned.data();
    buffer.m_ownership = SampleBufferOwnership::Owned;
    buffer.m_writable = true;
    return buffer;
}

Result<SampleBuffer> SampleBuffer::external(SampleBufferOwnership ownership, void* data, std::uint64_t byteCount,
                                            std::shared_ptr<void> lifetime, bool writable)
{
    if (ownership != SampleBufferOwnership::Mapped && ownership != SampleBufferOwnership::DecoderOwned)
    {
        return refuseArgument("external bytes are either mapped or decoder-owned; engine memory is adopted and "
                              "caller-guaranteed memory is borrowed",
                              "ownership", "SampleBuffer::external");
    }
    if (data == nullptr)
    {
        return refuseArgument("external bytes need an address", "data", "SampleBuffer::external");
    }
    if (byteCount == 0u)
    {
        return refuseArgument("a buffer of no bytes describes no frame", "byteCount", "SampleBuffer::external");
    }
    if (lifetime == nullptr)
    {
        return refuseArgument("mapped and decoder-owned bytes are released through a lifetime token; memory whose "
                              "release nothing here performs is borrowed",
                              "lifetime", "SampleBuffer::external");
    }

    SampleBuffer buffer;
    buffer.m_lifetime = std::move(lifetime);
    buffer.m_data = data;
    buffer.m_byteCount = byteCount;
    buffer.m_ownership = ownership;
    buffer.m_writable = writable;
    return buffer;
}

Result<SampleBuffer> SampleBuffer::borrow(std::span<std::byte> bytes)
{
    if (bytes.empty())
    {
        return refuseArgument("a buffer of no bytes describes no frame", "bytes", "SampleBuffer::borrow");
    }
    SampleBuffer buffer;
    buffer.m_data = bytes.data();
    buffer.m_byteCount = static_cast<std::uint64_t>(bytes.size());
    buffer.m_ownership = SampleBufferOwnership::Borrowed;
    buffer.m_writable = true;
    return buffer;
}

Result<SampleBuffer> SampleBuffer::borrowConst(std::span<const std::byte> bytes)
{
    if (bytes.empty())
    {
        return refuseArgument("a buffer of no bytes describes no frame", "bytes", "SampleBuffer::borrowConst");
    }
    SampleBuffer buffer;
    // The const is given up here and taken back by `m_writable`, which every accessor that hands
    // out a mutable span consults first. Storing two pointers to say the same thing would let them
    // disagree; storing one and a flag cannot.
    buffer.m_data = const_cast<std::byte*>(bytes.data());
    buffer.m_byteCount = static_cast<std::uint64_t>(bytes.size());
    buffer.m_ownership = SampleBufferOwnership::Borrowed;
    buffer.m_writable = false;
    return buffer;
}

bool SampleBuffer::valid() const noexcept
{
    return m_data != nullptr;
}

SampleBufferOwnership SampleBuffer::ownership() const noexcept
{
    return m_ownership;
}

std::uint64_t SampleBuffer::byteCount() const noexcept
{
    return m_byteCount;
}

bool SampleBuffer::writable() const noexcept
{
    return m_data != nullptr && m_writable;
}

bool SampleBuffer::adoptable() const noexcept
{
    return m_data != nullptr && m_ownership == SampleBufferOwnership::Owned;
}

std::span<const std::byte> SampleBuffer::bytes() const noexcept
{
    if (m_data == nullptr)
    {
        return std::span<const std::byte>{};
    }
    return std::span<const std::byte>{static_cast<const std::byte*>(m_data), static_cast<std::size_t>(m_byteCount)};
}

std::optional<std::span<std::byte>> SampleBuffer::mutableBytes() noexcept
{
    if (!writable())
    {
        return std::nullopt;
    }
    return std::span<std::byte>{static_cast<std::byte*>(m_data), static_cast<std::size_t>(m_byteCount)};
}

std::uint64_t SampleBuffer::contextId() const noexcept
{
    return m_owned.contextId();
}

Result<ImageStorageBytes> SampleBuffer::releaseOwned()
{
    if (m_data == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "this buffer holds no bytes to hand over",
                                            .operation = "SampleBuffer::releaseOwned",
                                            .domain = ErrorDomain::Image});
    }
    if (m_ownership != SampleBufferOwnership::Owned)
    {
        return Status::failure(
            ErrorCode::InvalidState,
            StatusDetail{.message = std::string("only engine-owned bytes can be handed over; these are ") +
                                    sampleBufferOwnershipName(m_ownership),
                         .operation = "SampleBuffer::releaseOwned",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "ownership")}});
    }

    ImageStorageBytes storage = std::move(m_owned);
    m_data = nullptr;
    m_byteCount = 0u;
    m_ownership = SampleBufferOwnership::Borrowed;
    m_writable = false;
    return storage;
}

void SampleBuffer::release() noexcept
{
    m_owned.release();
    m_lifetime.reset();
    m_data = nullptr;
    m_byteCount = 0u;
    m_ownership = SampleBufferOwnership::Borrowed;
    m_writable = false;
}

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The bytes a boundary hands over, and who is responsible for freeing them.
///
/// A decoder produces bytes in four different circumstances. Treated as one - a pointer, and a
/// convention about who frees it that lives in whoever wrote the call - each is a defect when it is
/// guessed wrong, because the four have genuinely different consequences. This type carries the
/// answer with the bytes instead.
///
/// **Owned** bytes came from an engine context's memory resource. They are counted against its
/// budget, they hold that context open, and - the reason this distinction is load-bearing - they are
/// the only ones an image can *adopt*, taking the decoder's buffer as its own with no copy at all.
///
/// **Mapped** and **decoder-owned** bytes belong to something else that must outlive them, so they
/// carry a lifetime token: a handle whose destruction is what releases the mapping or the decoder's
/// buffer. The token is held here, so the release happens when the last holder of these bytes lets
/// go rather than when a caller remembers to.
///
/// **Borrowed** bytes are a checked view of memory whose lifetime the caller guarantees. Nothing is
/// released, nothing is counted, and the guarantee is the caller's. It is the honest description of
/// a stack buffer or a span into somebody else's frame, and it is deliberately the one kind that
/// cannot be adopted: adopting it would mean an image whose storage disappears without warning.
///
/// The type is untyped on purpose. What these bytes mean is `Image/RawFrame.h`, and the split is
/// what lets a transfer that copies one buffer into another be compiled once rather than once per
/// representation.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace an
{

/// Who is responsible for the memory a sample buffer describes.
///
/// The four cases are not shades of one thing. They differ in who frees the bytes, whether they are
/// counted against a memory budget, and whether an image may take them over without copying, and a
/// caller acts differently on each of those.
enum class SampleBufferOwnership : std::uint8_t
{
    /// The bytes came from an engine context's memory resource and go back to it. Counted against
    /// that context's budget, and the only kind an image can adopt without a copy.
    Owned,

    /// The bytes are a memory-mapped region. Released when the lifetime token this buffer holds is
    /// destroyed, and never counted against a memory budget - the operating system is holding them,
    /// not the engine.
    Mapped,

    /// The bytes belong to a decoding library that will free them itself. Released through the
    /// lifetime token, for the same reason and with the same consequences as a mapping.
    DecoderOwned,

    /// The bytes belong to something whose lifetime the caller guarantees, and nothing here
    /// releases them. The one kind that cannot be adopted, because an image built on them would
    /// have storage that can vanish while it is being read.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Borrowed
};

/// Whether a value is one of the ownerships this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param ownership the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSampleBufferOwnership(SampleBufferOwnership ownership) noexcept
{
    return static_cast<std::uint8_t>(ownership) <= static_cast<std::uint8_t>(SampleBufferOwnership::Borrowed);
}

/// The ownership's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe and allocation-free.
///
/// @param ownership the ownership to name. A value outside the enumeration is named
///        `"UnknownSampleBufferOwnership"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* sampleBufferOwnershipName(SampleBufferOwnership ownership) noexcept;

/// The bytes a boundary hands over, with who releases them and whether they may be written.
///
/// Move-only, because two owners of one release are one release too many. A default-constructed
/// buffer holds nothing, which is also what a move leaves behind, and every accessor answers for
/// that state rather than requiring the question to have been asked first.
///
/// Reading and writing the bytes is not synchronised: a buffer is a description of memory, and two
/// threads writing the same region through it race exactly as they would through a pointer. The
/// description - `ownership`, `byteCount`, `writable` - changes only when the buffer is moved from,
/// released or handed over with `releaseOwned`, so several threads may read it while none of those
/// happens.
class SampleBuffer
{
public:
    /// A buffer that holds nothing.
    ///
    /// Allocates nothing, and is what a move leaves behind.
    AFTERNIGHTENGINE_API SampleBuffer() noexcept;

    /// Releases whatever this buffer is responsible for, which for borrowed bytes is nothing.
    AFTERNIGHTENGINE_API ~SampleBuffer();

    /// Takes over another buffer, leaving it holding nothing.
    ///
    /// Allocation-free. The bytes do not move, so a pointer already handed out stays valid.
    ///
    /// @param other the buffer to take over. Left exactly as a default-constructed one.
    AFTERNIGHTENGINE_API SampleBuffer(SampleBuffer&& other) noexcept;

    /// Releases this buffer and takes over another.
    ///
    /// This buffer's memory is released before the other's is taken over. A self-assignment is a
    /// no-op that keeps the memory rather than releasing it and then using what was released.
    ///
    /// @param other the buffer to take over. Left exactly as a default-constructed one.
    /// @return this buffer.
    AFTERNIGHTENGINE_API SampleBuffer& operator=(SampleBuffer&& other) noexcept;

    /// Copying is deleted: a frame's bytes are expensive and shared responsibility for releasing
    /// them is a decision a call site should have to write.
    SampleBuffer(const SampleBuffer&) = delete;

    /// Copying is deleted. See the copy constructor.
    SampleBuffer& operator=(const SampleBuffer&) = delete;

    /// Takes engine memory for a frame of this layout.
    ///
    /// The entry point a decoder uses when it can write into a buffer the engine chose, which is
    /// the arrangement worth preferring: the bytes are counted, they hold the context open, and the
    /// resulting frame can become an image with no copy at all.
    ///
    /// @param engine the context whose memory resource and budget own the allocation.
    /// @param layout where the samples are to sit, already validated as geometry.
    /// @param initialization what the bytes hold before the decoder writes to them.
    /// @return the buffer, or whatever the allocation reported - a budget refusal carrying the
    ///         request, what was available and the limit, or an allocation failure.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<SampleBuffer>
    createOwned(const EngineContext& engine, const ImageLayout& layout, StorageInitialization initialization);

    /// Wraps memory an image storage already holds.
    ///
    /// For a caller that has engine-owned bytes in hand - a transfer target, a frame being
    /// re-described - and wants them carried as a boundary buffer without a second allocation.
    ///
    /// @param storage the memory to take over. A storage that holds nothing yields a buffer that
    ///        holds nothing.
    /// @return the buffer, owning what the storage owned.
    [[nodiscard]] static AFTERNIGHTENGINE_API SampleBuffer adopt(ImageStorageBytes&& storage) noexcept;

    /// Wraps memory something else owns and will release through a token.
    ///
    /// @param ownership `SampleBufferOwnership::Mapped` or `SampleBufferOwnership::DecoderOwned`.
    ///        Anything else is refused with `ErrorCode::InvalidArgument`, because owned memory is
    ///        adopted and borrowed memory is borrowed.
    /// @param data the first byte. A null address is refused.
    /// @param byteCount how many bytes are readable from it. Zero is refused.
    /// @param lifetime the handle whose destruction releases the region. Held for as long as this
    ///        buffer lives and shared with whatever else holds it. A null token is refused: bytes
    ///        somebody else frees at a time nobody stated are exactly what `Borrowed` is for, and
    ///        saying so is the point of the distinction.
    /// @param writable whether the region may be written through.
    /// @return the buffer, or `ErrorCode::InvalidArgument` naming the argument at fault.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<SampleBuffer> external(SampleBufferOwnership ownership, void* data,
                                                                            std::uint64_t byteCount,
                                                                            std::shared_ptr<void> lifetime,
                                                                            bool writable);

    /// Wraps memory whose lifetime the caller guarantees, through which the bytes may be written.
    ///
    /// @param bytes the region. An empty span is refused with `ErrorCode::InvalidArgument`.
    /// @return the buffer, which releases nothing and is never adoptable.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<SampleBuffer> borrow(std::span<std::byte> bytes);

    /// Wraps memory whose lifetime the caller guarantees, through which nothing may be written.
    ///
    /// @param bytes the region. An empty span is refused with `ErrorCode::InvalidArgument`.
    /// @return the buffer, for which `writable()` is false.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<SampleBuffer> borrowConst(std::span<const std::byte> bytes);

    /// Whether this buffer describes any memory.
    ///
    /// Allocation-free.
    ///
    /// @return false for a default-constructed buffer and one that has been moved from.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// Who releases these bytes.
    ///
    /// @return the ownership, or `SampleBufferOwnership::Borrowed` when this buffer holds nothing -
    ///         which is the honest answer for memory it is not responsible for, and which `valid`
    ///         is the question to ask before reading.
    [[nodiscard]] AFTERNIGHTENGINE_API SampleBufferOwnership ownership() const noexcept;

    /// How many bytes are readable.
    ///
    /// For owned bytes this is what was reserved, not what a layout currently describes: the two
    /// differ after a reshape, and a boundary check has to be against what is actually there.
    ///
    /// @return the byte count, or `0` when this buffer holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t byteCount() const noexcept;

    /// Whether the bytes may be written through this buffer.
    ///
    /// @return false for a buffer built from a const span, for external memory wrapped as not
    ///         writable, and for one that holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API bool writable() const noexcept;

    /// Whether an image may take these bytes over without copying them.
    ///
    /// True only for engine-owned memory. Mapped, decoder-owned and borrowed bytes all have a
    /// release or a lifetime that belongs to something else, so an image built on them would have
    /// storage it does not control.
    ///
    /// @return true when the ownership is `SampleBufferOwnership::Owned` and this buffer holds
    ///         memory.
    [[nodiscard]] AFTERNIGHTENGINE_API bool adoptable() const noexcept;

    /// The bytes, without permission to change them.
    ///
    /// @return the region, or an empty span when this buffer holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::byte> bytes() const noexcept;

    /// The bytes, through which they may be changed.
    ///
    /// @return the region, or an empty optional when this buffer holds nothing or is not writable.
    ///         An optional rather than an empty span, because "you may not write here" and "there
    ///         is nothing here" are different answers and a caller acts differently on each.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::span<std::byte>> mutableBytes() noexcept;

    /// Which engine context's budget these bytes belong to.
    ///
    /// @return the owning context's identity, or `0` for anything but owned memory. No live context
    ///         has an identity of zero.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

    /// Hands the engine-owned memory over, leaving this buffer holding nothing.
    ///
    /// The other half of `adopt`, and the call an image makes when it takes a decoded frame as its
    /// own. It is deliberately not available for the other three ownerships: there is no engine
    /// storage to hand over, and producing one would mean copying behind a name that says it does
    /// not.
    ///
    /// @return the storage, or `ErrorCode::InvalidState` naming the ownership that refused, or
    ///         `ErrorCode::NotInitialized` when this buffer holds nothing. Nothing is mutated on
    ///         refusal.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ImageStorageBytes> releaseOwned();

    /// Releases now rather than at the end of the scope.
    ///
    /// Idempotent, and leaves the buffer exactly as a default-constructed one.
    AFTERNIGHTENGINE_API void release() noexcept;

private:
    /// The engine memory, when the ownership is `Owned`. Holds nothing otherwise.
    ImageStorageBytes m_owned;

    /// What releases the region, when the ownership is `Mapped` or `DecoderOwned`. Null otherwise.
    std::shared_ptr<void> m_lifetime;

    /// The first byte, or null when this buffer holds nothing. For owned memory this is the
    /// storage's own base address, kept here so every accessor reads one member rather than
    /// branching on the ownership.
    void* m_data = nullptr;

    /// How many bytes are readable from `m_data`.
    std::uint64_t m_byteCount = 0u;

    /// Who releases them.
    SampleBufferOwnership m_ownership = SampleBufferOwnership::Borrowed;

    /// Whether they may be written.
    bool m_writable = false;
};

} // namespace an

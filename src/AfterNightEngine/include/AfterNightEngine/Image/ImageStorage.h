// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The memory an image's samples live in, owned, counted, and released.
///
/// Four decisions shape it, and each is worth naming because each has a cost somewhere.
///
/// **What the samples start as is stated, never assumed.** `StorageInitialization::Uninitialized` is
/// available and is the cheap one, but it is a word a caller has to write. That is the whole point:
/// a buffer that a pass writes in part and reads in full gives a result that is wrong in some runs and
/// bit-identically wrong each time - a comet stack wrong in about half of its runs, for one. A caller
/// that means to write every sample says `Uninitialized` and is correct; a caller that does not know
/// says `Zeroed` and is slower.
///
/// **A refusal is a returned failure with the numbers in it.** A frame that does not fit the
/// configured budget is answerable - by tiling, by streaming, by raising the budget - and a frame
/// the machine cannot provide is not, so they are different codes and both carry what was asked for.
///
/// **The storage holds its engine context open.** The bytes came from that context's memory resource
/// and have to go back to it, so a storage keeps a lease and the context refuses to shut down while
/// one exists. That is the difference between this and the raw resource pointer beside it: the
/// context's own documentation says a caller still holding memory from it is a lifetime problem of
/// the caller's own making, and this is the type that makes it a counted, reported condition
/// instead.
///
/// **What is held and what is meant are separate numbers.** `capacityBytes` is what was taken from
/// the resource and is what the budget sees; `logicalBytes` is what the current layout describes.
/// They differ after a reshape, which is the operation that re-describes retained memory rather than
/// asking for more - and which therefore never allocates, never fails for a reason the budget owns,
/// and refuses rather than growing.
///
/// The type is split in two. `ImageStorageBytes` owns the memory and knows nothing about what a
/// sample is; `ImageStorage<SampleT>` is the typed shell over it. That split is what keeps the
/// allocation, the accounting, the lifetime and the failure text compiled once instead of once per
/// sample type, and it is why the eight instantiations cost almost nothing each.

#pragma once

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace an
{

/// The engine context whose memory budget owns a storage. Defined in `Runtime/EngineContext.h`.
class EngineContext;

/// Where an engine context's memory comes from. Defined in `Runtime/MemoryResource.h`.
class MemoryResource;

/// What the samples of newly taken storage hold before anything writes to them.
///
/// Two values rather than three, because a fill needs a value and an enumerator cannot carry one.
/// `ImageStorage::createFilled` is the third policy, and copying is `clone`.
enum class StorageInitialization : std::uint8_t
{
    /// Nothing is written. The contents are whatever the allocator handed over, which on a fresh
    /// page is zeroes and on a reused one is the last thing that lived there - so a buffer that
    /// reads plausibly in one run reads as garbage in the next, which is the failure mode this
    /// enumerator exists to make somebody choose deliberately.
    Uninitialized,

    /// Every byte of the allocation is zero, padding included.
    ///
    /// The padding is deliberate: it costs one memset over a range that was going to be touched
    /// anyway, and it means two storages of the same layout compare equal byte for byte after the
    /// same samples are written to them, which is what makes a byte-level fixture comparison
    /// meaningful.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Zeroed
};

/// Whether a value is one of the policies this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param initialization the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStorageInitialization(StorageInitialization initialization) noexcept
{
    return static_cast<std::uint8_t>(initialization) <= static_cast<std::uint8_t>(StorageInitialization::Zeroed);
}

/// The policy's own name, for a log line, a diagnostic field or a test failure message.
///
/// Thread-safe and allocation-free.
///
/// @param initialization the policy to name. A value outside the enumeration is named
///        `"UnknownStorageInitialization"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* storageInitializationName(StorageInitialization initialization) noexcept;

/// Whether a layout's samples are the size a representation's are.
///
/// The rule that pairs an untyped layout with a typed storage or view. It is a function rather than
/// a line inside the typed template so that the sentence a caller reads is written once instead of
/// once per sample type, and so that a caller pairing the two by hand can ask the same question the
/// engine asks.
///
/// Thread-safe.
///
/// @param elementSizeBytes the layout's element size.
/// @param sampleType the representation the samples are to be read as.
/// @param operation the caller's operation name, copied into a refusal; never null.
/// @return success when the two agree; otherwise `ErrorCode::IncompatibleArguments` with
///         `GeometryError::ElementSizeMismatch` named in its message and both sizes as context.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkStorageSampleSize(std::uint32_t elementSizeBytes, SampleType sampleType,
                                                                 const char* operation = "an::checkStorageSampleSize");

/// Whether an address is one a sample of this representation may be read from.
///
/// The other half of pairing untyped memory with a typed reading of it, and the half that is easy
/// to leave out because it is usually true: an allocation admits any power-of-two alignment, so
/// memory taken with a small one is memory a typed pointer into it cannot legally be dereferenced
/// through, however well the sizes agree.
///
/// Thread-safe.
///
/// @param address the first sample's address. A null address is accepted, because memory that holds
///        nothing is refused by whatever is asking rather than by this rule.
/// @param sampleType the representation the samples are to be read as.
/// @return success when the address is aligned for that representation; otherwise
///         `ErrorCode::IncompatibleArguments` with `GeometryError::BaseAddressMisaligned` named in its
///         message and the required alignment as context, or `ErrorCode::InvalidArgument` naming
///         `sampleType` when the representation is not one of the eight.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkStorageSampleAlignment(const void* address, SampleType sampleType);

/// The memory an image's samples live in, owned and untyped.
///
/// Move-only, because two owners of one allocation would release it twice. A default-constructed
/// one holds nothing, which is also what a move leaves behind and what `release` produces; `valid`
/// is how to ask, and every accessor answers for that state rather than requiring the question to
/// have been asked.
///
/// The typed shell above it is what callers normally hold. This is the type for code that moves
/// bytes without reading them - a decoder writing a frame it has not classified, a transfer that
/// copies one buffer into another - and it is public because the typed shell has one as a member.
class ImageStorageBytes
{
public:
    /// Storage that holds nothing.
    ///
    /// Allocates nothing, and is what a move and a `release` leave behind.
    AFTERNIGHTENGINE_API ImageStorageBytes() noexcept;

    /// Gives the memory back to the resource it came from, and the lease back to the context.
    AFTERNIGHTENGINE_API ~ImageStorageBytes();

    /// Takes over another storage's memory, leaving it holding nothing.
    ///
    /// Allocation-free. Pointers already handed out stay valid: the ownership moves and the bytes
    /// do not.
    ///
    /// @param other the storage to take over. Left exactly as a default-constructed one.
    AFTERNIGHTENGINE_API ImageStorageBytes(ImageStorageBytes&& other) noexcept;

    /// Releases this storage's memory and takes over another's.
    ///
    /// A self-assignment is recognised and does nothing, so it keeps the memory rather than
    /// releasing it and then using what was released.
    ///
    /// @param other the storage to take over. Left exactly as a default-constructed one.
    /// @return this storage.
    AFTERNIGHTENGINE_API ImageStorageBytes& operator=(ImageStorageBytes&& other) noexcept;

    /// Copying is deleted: an image buffer is expensive and shared ownership of one is a decision a
    /// call site should have to write. `clone` is the copy that says so.
    ImageStorageBytes(const ImageStorageBytes&) = delete;

    /// Copying is deleted. See the copy constructor.
    ImageStorageBytes& operator=(const ImageStorageBytes&) = delete;

    /// Takes the memory an image of this layout needs.
    ///
    /// @param engine the context whose memory resource and budget own the allocation. It cannot
    ///        shut down while the storage lives. A context that holds nothing is refused with
    ///        `ErrorCode::NotInitialized`.
    /// @param layout where the samples are to sit, already validated as geometry.
    /// @param initialization what the samples hold before anything writes to them. A value outside
    ///        the enumeration is refused with `ErrorCode::InvalidArgument`.
    /// @param alignmentBytes the boundary the first sample must sit on. Must be a power of two, and
    ///        is refused with `ErrorCode::InvalidArgument` otherwise. The default is what a vector
    ///        kernel needs; whether every *row* also starts on it depends on the layout's row
    ///        stride, which is the layout's business and not this allocation's.
    /// @return the storage, or `ErrorCode::MemoryBudgetExceeded` when the configured budget refuses
    ///         it - carrying the request, what was available and the limit - or
    ///         `ErrorCode::AllocationFailed` when the machine does.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ImageStorageBytes>
    create(const EngineContext& engine, const ImageLayout& layout, StorageInitialization initialization,
           std::uint64_t alignmentBytes = kDefaultBufferAlignmentBytes);

    /// Takes the memory an image of this request needs, validating the request first.
    ///
    /// The entry point for a caller that has an extent and an arrangement rather than a layout. The
    /// geometry is checked against the engine context's own configured maxima, so a request that
    /// overflows, that this platform cannot address, or that exceeds the configured image size is
    /// refused here with the rule that refused it named in the failure.
    ///
    /// The base address is aligned to the larger of the default and the request's own row
    /// alignment, so a layout that asked for aligned rows gets them from the first row onwards
    /// rather than from the second.
    ///
    /// @param engine the context whose limits validate the geometry and whose resource owns the
    ///        allocation.
    /// @param request the extent, sample size, arrangement and alignment to build from.
    /// @param initialization what the samples hold before anything writes to them.
    /// @return the storage; `ErrorCode::NotInitialized` when `engine` holds nothing; for a geometry
    ///         that describes no image, the code its `GeometryError` maps to -
    ///         `ErrorCode::InvalidArgument`, `ErrorCode::IncompatibleArguments` for samples per pixel
    ///         and planes that do not make the channel count, `ErrorCode::LimitExceeded` or
    ///         `ErrorCode::OutOfRange` - with the rule's name in the message; or whatever the
    ///         allocation reported.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ImageStorageBytes>
    create(const EngineContext& engine, const ImageLayoutRequest& request, StorageInitialization initialization);

    /// Whether this storage holds memory.
    ///
    /// Allocation-free.
    ///
    /// @return false for a default-constructed storage, one that has been moved from, and one that
    ///         has been released.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// Where the samples are.
    ///
    /// @return the base address, aligned as `alignmentBytes()` reports, or null when this storage
    ///         holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API void* data() noexcept;

    /// Where the samples are, without permission to change them.
    ///
    /// @return the base address, or null when this storage holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API const void* data() const noexcept;

    /// What the memory currently means.
    ///
    /// An optional rather than a reference because there is no layout of no image: a storage that
    /// holds nothing has nothing to describe, and a zero-valued layout does not exist to return.
    ///
    /// @return the layout, or an empty optional when this storage holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<ImageLayout> layout() const noexcept;

    /// How many bytes this storage holds from its resource.
    ///
    /// The figure that counts against the configured memory budget for as long as the storage
    /// exists, whether or not the current layout uses all of it.
    ///
    /// @return the reserved bytes, or `0` when this storage holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t capacityBytes() const noexcept;

    /// How many bytes the current layout describes.
    ///
    /// Never greater than `capacityBytes()`, and smaller than it exactly after a reshape to a
    /// smaller geometry.
    ///
    /// @return the layout's total bytes, or `0` when this storage holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t logicalBytes() const noexcept;

    /// The boundary the first sample sits on.
    ///
    /// @return the alignment the allocation was made at, or `0` when this storage holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t alignmentBytes() const noexcept;

    /// Which engine context's budget this memory belongs to.
    ///
    /// @return the owning context's identity, or `0` when this storage holds nothing. No live
    ///         context has an identity of zero.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t contextId() const noexcept;

    /// Which resource this memory came from and will go back to.
    ///
    /// Owned by the engine context, which this storage keeps open, so the pointer is valid for at
    /// least as long as the storage is.
    ///
    /// @return the resource, or null when this storage holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API MemoryResource* memoryResource() const noexcept;

    /// A second storage holding a copy of these samples.
    ///
    /// Sized to what this storage means rather than to what it holds: a storage reshaped smaller
    /// clones to the smaller size, and the retained capacity is not carried across. The copy comes
    /// from the same engine context, so it is counted against the same budget and may be refused by
    /// it.
    ///
    /// @return the copy, or `ErrorCode::NotInitialized` when this storage holds nothing, or
    ///         whatever the allocation reported.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<ImageStorageBytes> clone() const;

    /// Re-describes the memory this storage already holds.
    ///
    /// It never allocates and never copies. That is the contract rather than an optimisation: a
    /// reshape that could grow would be an allocation with a different name, and would fail for
    /// reasons - a budget, a machine - that a caller answers differently from "this does not fit
    /// what you are holding". A caller that needs more memory creates a second storage.
    ///
    /// The bytes do not move, so what a sample position names afterwards is generally a different
    /// sample. Nothing is mutated when the reshape is refused.
    ///
    /// The base address keeps the alignment it was allocated with. A layout whose rows need a
    /// stronger one than `alignmentBytes()` is not refused here - the layout cannot know what it
    /// was allocated against - so a caller that needs a particular row alignment asks for it when
    /// the storage is created.
    ///
    /// @param layout the geometry to adopt, already validated.
    /// @return success; `ErrorCode::NotInitialized` when this storage holds nothing; or
    ///         `ErrorCode::OutOfRange` carrying the requested and available byte counts when the
    ///         layout describes more memory than is held.
    [[nodiscard]] AFTERNIGHTENGINE_API Status reshape(const ImageLayout& layout);

    /// Gives the memory back now rather than at the end of the scope.
    ///
    /// The explicit half of what the destructor does, for a caller that has finished with an image
    /// and wants the budget back before the next one is taken. Idempotent, and leaves the storage
    /// exactly as a default-constructed one.
    AFTERNIGHTENGINE_API void release() noexcept;

private:
    /// The allocation, the accounting and the hold on the engine context, defined only inside the
    /// engine so that the published surface says nothing about how a storage is laid out.
    class State;

    /// Takes ownership of a state the factory has already built.
    ///
    /// @param state the storage's memory and accounting. Never null on this path.
    explicit ImageStorageBytes(std::unique_ptr<State> state) noexcept;

    /// What this storage holds, or null when it holds nothing.
    std::unique_ptr<State> m_state;
};

/// The memory an image's samples live in, read and written as one of the eight representations.
///
/// A shell over `ImageStorageBytes` that adds exactly one thing: the promise that the layout's
/// element size is the size of `SampleT`, checked once where the storage is created and never
/// again. That is what lets `data()` be a typed pointer and `view()` a checked view with no
/// validation left in it.
///
/// Move-only, for the reason the untyped storage is. A default-constructed one holds nothing.
///
/// @tparam SampleT one of the eight resident sample types. Const-qualified types are deliberately
///         not accepted: storage owns memory that something has to be able to write.
template <ResidentSample SampleT> class ImageStorage
{
public:
    /// The type of one sample.
    using Sample = SampleT;

    /// Which representation this storage's samples are, as a value.
    static constexpr SampleType kSampleType = kSampleTypeOf<SampleT>;

    /// Storage that holds nothing.
    ImageStorage() noexcept = default;

    /// Takes over another storage's memory, leaving it holding nothing.
    ///
    /// @param other the storage to take over.
    ImageStorage(ImageStorage&& other) noexcept = default;

    /// Releases this storage's memory and takes over another's.
    ///
    /// @param other the storage to take over.
    /// @return this storage.
    ImageStorage& operator=(ImageStorage&& other) noexcept = default;

    /// Copying is deleted. `clone` is the copy that says what it costs.
    ImageStorage(const ImageStorage&) = delete;

    /// Copying is deleted. See the copy constructor.
    ImageStorage& operator=(const ImageStorage&) = delete;

    /// Takes the memory an image of this layout needs, as samples of this type.
    ///
    /// @param engine the context whose memory resource and budget own the allocation.
    /// @param layout where the samples are to sit. Its element size must be `sizeof(SampleT)`.
    /// @param initialization what the samples hold before anything writes to them.
    /// @param alignmentBytes the boundary the first sample must sit on.
    /// @return the storage, `ErrorCode::IncompatibleArguments` when the layout describes samples of
    ///         another size, or whatever the allocation reported.
    [[nodiscard]] static Result<ImageStorage> create(const EngineContext& engine, const ImageLayout& layout,
                                                     StorageInitialization initialization,
                                                     std::uint64_t alignmentBytes = kDefaultBufferAlignmentBytes)
    {
        Status paired = checkStorageSampleSize(layout.elementSizeBytes(), kSampleType, "ImageStorage::create");
        if (paired.failed())
        {
            return paired;
        }
        Result<ImageStorageBytes> bytes = ImageStorageBytes::create(engine, layout, initialization, alignmentBytes);
        if (!bytes.hasValue())
        {
            return bytes.status();
        }
        return ImageStorage{std::move(*bytes)};
    }

    /// Takes the memory an image of this request needs, validating the request first.
    ///
    /// @param engine the context whose limits validate the geometry and whose resource owns the
    ///        allocation.
    /// @param request the extent, sample size, arrangement and alignment to build from. Its element
    ///        size must be `sizeof(SampleT)`.
    /// @param initialization what the samples hold before anything writes to them.
    /// @return the storage, `ErrorCode::IncompatibleArguments` when the request describes samples
    ///         of another size, or whatever the geometry or the allocation reported.
    [[nodiscard]] static Result<ImageStorage> create(const EngineContext& engine, const ImageLayoutRequest& request,
                                                     StorageInitialization initialization)
    {
        Status paired = checkStorageSampleSize(request.elementSizeBytes, kSampleType, "ImageStorage::create");
        if (paired.failed())
        {
            return paired;
        }
        Result<ImageStorageBytes> bytes = ImageStorageBytes::create(engine, request, initialization);
        if (!bytes.hasValue())
        {
            return bytes.status();
        }
        return ImageStorage{std::move(*bytes)};
    }

    /// Takes the memory an image of this layout needs and writes one value into every sample.
    ///
    /// The padding between rows and planes is not written, so a filled storage and a zeroed one
    /// that was then filled by hand hold the same samples and not necessarily the same bytes. That
    /// is deliberate: padding is not part of the image, and writing a fill value into it would make
    /// a byte comparison of two equal images depend on their strides.
    ///
    /// @param engine the context whose memory resource and budget own the allocation.
    /// @param layout where the samples are to sit.
    /// @param value what every sample holds.
    /// @param alignmentBytes the boundary the first sample must sit on.
    /// @return the storage, or whatever `create` reported.
    [[nodiscard]] static Result<ImageStorage> createFilled(const EngineContext& engine, const ImageLayout& layout,
                                                           SampleT value,
                                                           std::uint64_t alignmentBytes = kDefaultBufferAlignmentBytes)
    {
        Result<ImageStorage> storage = create(engine, layout, StorageInitialization::Uninitialized, alignmentBytes);
        if (!storage.hasValue())
        {
            return storage;
        }
        const Status filled = storage->fill(value);
        if (filled.failed())
        {
            return filled;
        }
        return storage;
    }

    /// Takes the memory an image of this request needs and writes one value into every sample.
    ///
    /// @param engine the context whose limits validate the geometry and whose resource owns the
    ///        allocation.
    /// @param request the extent, sample size, arrangement and alignment to build from.
    /// @param value what every sample holds.
    /// @return the storage, or whatever `create` reported.
    [[nodiscard]] static Result<ImageStorage> createFilled(const EngineContext& engine,
                                                           const ImageLayoutRequest& request, SampleT value)
    {
        Result<ImageStorage> storage = create(engine, request, StorageInitialization::Uninitialized);
        if (!storage.hasValue())
        {
            return storage;
        }
        const Status filled = storage->fill(value);
        if (filled.failed())
        {
            return filled;
        }
        return storage;
    }

    /// Takes over untyped memory whose samples are already this representation.
    ///
    /// The way back from `ImageStorageBytes` to a typed storage, and the reason the untyped half is
    /// public. A decoder writes bytes it has not classified into engine memory; something then
    /// establishes what they are and takes them over here, with no copy and no conversion.
    ///
    /// Two things are checked, and the second is not implied by the first. The layout's element
    /// size has to be this representation's, which is the promise every typed accessor rests on;
    /// and the base address has to be one a sample of this type may be read from, because the
    /// untyped allocation admits any power-of-two alignment and a caller that asked for a small one
    /// would otherwise get a typed pointer that cannot legally be dereferenced.
    ///
    /// @param bytes the memory to take over. Untouched when the check refuses.
    /// @return the typed storage; `ErrorCode::NotInitialized` when the memory holds nothing;
    ///         `ErrorCode::IncompatibleArguments` when the layout describes samples of another
    ///         size; or `ErrorCode::InvalidArgument` when the base address is not aligned for this
    ///         representation.
    [[nodiscard]] static Result<ImageStorage> fromBytes(ImageStorageBytes&& bytes)
    {
        const std::optional<ImageLayout> layout = bytes.layout();
        if (!layout.has_value())
        {
            return Status::failure(ErrorCode::NotInitialized,
                                   StatusDetail{.message = "there is no memory here to describe",
                                                .operation = "ImageStorage::fromBytes",
                                                .domain = ErrorDomain::Image});
        }
        Status paired = checkStorageSampleSize(layout->elementSizeBytes(), kSampleType, "ImageStorage::fromBytes");
        if (paired.failed())
        {
            return paired;
        }
        Status reachable = checkStorageSampleAlignment(bytes.data(), kSampleType);
        if (reachable.failed())
        {
            return reachable;
        }
        return ImageStorage{std::move(bytes)};
    }

    /// Whether this storage holds memory.
    ///
    /// @return false for a default-constructed storage, one that has been moved from, and one that
    ///         has been released.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_bytes.valid();
    }

    /// The untyped storage underneath, for code that moves bytes without reading them.
    ///
    /// @return the owned memory and its accounting.
    [[nodiscard]] const ImageStorageBytes& bytes() const noexcept
    {
        return m_bytes;
    }

    /// Where the samples are.
    ///
    /// @return the base address, or null when this storage holds nothing.
    [[nodiscard]] SampleT* data() noexcept
    {
        return static_cast<SampleT*>(m_bytes.data());
    }

    /// Where the samples are, without permission to change them.
    ///
    /// @return the base address, or null when this storage holds nothing.
    [[nodiscard]] const SampleT* data() const noexcept
    {
        return static_cast<const SampleT*>(m_bytes.data());
    }

    /// What the memory currently means.
    ///
    /// @return the layout, or an empty optional when this storage holds nothing.
    [[nodiscard]] std::optional<ImageLayout> layout() const noexcept
    {
        return m_bytes.layout();
    }

    /// How many bytes this storage holds from its resource.
    ///
    /// @return the reserved bytes, or `0` when this storage holds nothing.
    [[nodiscard]] std::uint64_t capacityBytes() const noexcept
    {
        return m_bytes.capacityBytes();
    }

    /// How many bytes the current layout describes.
    ///
    /// @return the layout's total bytes, or `0` when this storage holds nothing.
    [[nodiscard]] std::uint64_t logicalBytes() const noexcept
    {
        return m_bytes.logicalBytes();
    }

    /// How many samples of this type the held memory could hold.
    ///
    /// Capacity rather than content: the current layout usually names fewer, because its strides
    /// describe padding this count does not know about.
    ///
    /// @return the capacity in samples, or `0` when this storage holds nothing.
    [[nodiscard]] std::uint64_t sampleCapacity() const noexcept
    {
        return m_bytes.capacityBytes() / sizeof(SampleT);
    }

    /// The boundary the first sample sits on.
    ///
    /// @return the alignment the allocation was made at, or `0` when this storage holds nothing.
    [[nodiscard]] std::uint64_t alignmentBytes() const noexcept
    {
        return m_bytes.alignmentBytes();
    }

    /// Which engine context's budget this memory belongs to.
    ///
    /// @return the owning context's identity, or `0` when this storage holds nothing.
    [[nodiscard]] std::uint64_t contextId() const noexcept
    {
        return m_bytes.contextId();
    }

    /// Which resource this memory came from and will go back to.
    ///
    /// @return the resource, or null when this storage holds nothing.
    [[nodiscard]] MemoryResource* memoryResource() const noexcept
    {
        return m_bytes.memoryResource();
    }

    /// A view of every sample, through which they may be changed.
    ///
    /// Nothing is validated here that was not validated when the storage was created: the layout's
    /// element size is this type's, and the allocation is at least the layout's total bytes.
    ///
    /// @return the view, or an empty optional when this storage holds nothing. It borrows this
    ///         storage's memory and must not outlive it.
    [[nodiscard]] std::optional<ImageView<SampleT>> view() noexcept
    {
        const std::optional<ImageLayout> current = m_bytes.layout();
        if (!current.has_value())
        {
            return std::nullopt;
        }
        return ImageView<SampleT>::createChecked(data(), *current);
    }

    /// A view of every sample, through which none of them may be changed.
    ///
    /// @return the view, or an empty optional when this storage holds nothing.
    [[nodiscard]] std::optional<ImageView<const SampleT>> view() const noexcept
    {
        const std::optional<ImageLayout> current = m_bytes.layout();
        if (!current.has_value())
        {
            return std::nullopt;
        }
        return ImageView<const SampleT>::createChecked(data(), *current);
    }

    /// A second storage holding a copy of these samples.
    ///
    /// @return the copy, or whatever the untyped storage reported.
    [[nodiscard]] Result<ImageStorage> clone() const
    {
        Result<ImageStorageBytes> copied = m_bytes.clone();
        if (!copied.hasValue())
        {
            return copied.status();
        }
        return ImageStorage{std::move(*copied)};
    }

    /// Re-describes the memory this storage already holds.
    ///
    /// @param layout the geometry to adopt. Its element size must be `sizeof(SampleT)`.
    /// @return success, `ErrorCode::IncompatibleArguments` when the layout describes samples of
    ///         another size, or whatever the untyped reshape reported. Nothing is mutated on
    ///         refusal.
    [[nodiscard]] Status reshape(const ImageLayout& layout)
    {
        Status paired = checkStorageSampleSize(layout.elementSizeBytes(), kSampleType, "ImageStorage::reshape");
        if (paired.failed())
        {
            return paired;
        }
        return m_bytes.reshape(layout);
    }

    /// Writes one value into every sample the current layout names.
    ///
    /// The padding between rows and planes is left alone, for the reason `createFilled` gives.
    ///
    /// @param value what every sample is to hold.
    /// @return success, or `ErrorCode::NotInitialized` when this storage holds nothing.
    [[nodiscard]] Status fill(SampleT value)
    {
        const std::optional<ImageView<SampleT>> target = view();
        if (!target.has_value())
        {
            return Status::failure(ErrorCode::NotInitialized);
        }
        for (std::uint32_t planeIndex = 0u; planeIndex < target->planeCount(); ++planeIndex)
        {
            for (std::uint32_t y = 0u; y < target->height(); ++y)
            {
                const std::optional<std::span<SampleT>> samples = target->row(planeIndex, y);
                for (SampleT& sample : *samples)
                {
                    sample = value;
                }
            }
        }
        return Status::success();
    }

    /// Gives the memory back now rather than at the end of the scope.
    ///
    /// Idempotent, and leaves the storage exactly as a default-constructed one.
    void release() noexcept
    {
        m_bytes.release();
    }

private:
    /// Wraps memory whose element size has already been checked against this type.
    ///
    /// @param bytes the owned memory.
    explicit ImageStorage(ImageStorageBytes&& bytes) noexcept : m_bytes(std::move(bytes)) {}

    /// The memory, its accounting, and its hold on the engine context.
    ImageStorageBytes m_bytes;
};

} // namespace an

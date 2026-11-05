// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The consumer's owner of one image: its samples in any of the eight representations, its optional validity mask,
/// a stable identity and its revision state, reached only through read leases and one exclusive write lease.
///
/// The engine's `Image` is a move-only owner whose write access moves its pixel generation and does not lock. That is
/// right for an engine, which never shares an image between threads behind its caller's back, and wrong for a
/// consumer, where a viewer paints an image while a preview reads it, a history writer copies it and a process
/// replaces its samples. An asset is where the lock lives.
///
/// **Ownership and permission are separate.** Holding an asset - or a shared pointer to one - keeps it alive and
/// grants nothing else. Reading takes a read lease; any number may be held at once. Writing takes the write lease;
/// it excludes every reader and every other writer. No mutable image escapes except through the write lease, and a
/// lease can be released on any thread, so a lease taken on one thread may be handed to a worker and released there.
///
/// **Identity is the consumer's.** The engine's pixel and semantic generations are per-image counters, so two images
/// written once are indistinguishable by them. An asset adds an identity no other asset in the process shares; a
/// snapshot is a new asset with a new identity, so a result derived from an old asset can never be revived by a
/// coincident generation. An asset's samples are never replaced in place: a change of shape or representation makes a
/// new asset.
///
/// **Revisions are published when the writer lets go.** `ImageAsset::revision` answers without a lease, from the last
/// published state, so a GUI thread can compare a captured revision with the current one without waiting for a writer.
/// A lease's own `revision` reads the image under the lease.
///
/// An asset counts against its session for as long as it or any of its leases lives, so the session refuses to
/// replace or shut down a context an asset still holds storage from.
#pragma once

#include <AfterNightConsumerRuntime/ConsumerRuntimeExport.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>
#include <variant>

namespace an::consumer
{

class EngineSession;
class ImageAsset;

namespace detail
{
class AssetState;
} // namespace detail

/// An image in any of the eight resident representations.
using AnyImage =
    std::variant<an::Image<std::uint8_t>, an::Image<std::int8_t>, an::Image<std::uint16_t>, an::Image<std::int16_t>,
                 an::Image<std::uint32_t>, an::Image<std::int32_t>, an::Image<float>, an::Image<double>>;

/// The representation an image holds.
///
/// @param image the image.
/// @return its sample type.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::SampleType sampleTypeOf(const AnyImage& image) noexcept;

/// What a derived result depends on in an asset. Two equal revisions name the same samples, description, metadata
/// and validity; a result keyed by one is stale for any other.
struct AssetRevision
{
    /// The asset's identity, unique in the process.
    std::uint64_t assetIdentity = 0u;

    /// The engine image's pixel generation.
    std::uint64_t pixelGeneration = 0u;

    /// The engine image's semantic generation.
    std::uint64_t semanticGeneration = 0u;

    /// Moves whenever the validity mask is replaced, removed or written.
    std::uint64_t validityGeneration = 0u;

    /// Field-wise equality.
    [[nodiscard]] friend constexpr bool operator==(const AssetRevision& left,
                                                   const AssetRevision& right) noexcept = default;
};

/// Permission to read an asset. Any number may be held at once; none while the write lease is held.
///
/// Move-only. Keeps the asset's state alive, so it may outlive the asset object, and may be released on any thread.
class AssetReadLease
{
public:
    /// A lease on nothing.
    AFTERNIGHTCONSUMERRUNTIME_API AssetReadLease() noexcept;

    /// Releases the lease.
    AFTERNIGHTCONSUMERRUNTIME_API ~AssetReadLease();

    /// Takes over another lease.
    ///
    /// @param other the lease to take over.
    AFTERNIGHTCONSUMERRUNTIME_API AssetReadLease(AssetReadLease&& other) noexcept;

    /// Releases this lease and takes over another.
    ///
    /// @param other the lease to take over.
    /// @return this lease.
    AFTERNIGHTCONSUMERRUNTIME_API AssetReadLease& operator=(AssetReadLease&& other) noexcept;

    AssetReadLease(const AssetReadLease&) = delete;
    AssetReadLease& operator=(const AssetReadLease&) = delete;

    /// Whether this is a lease.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool valid() const noexcept;

    /// The asset's identity, or zero for a lease on nothing.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint64_t assetIdentity() const noexcept;

    /// The image. Calling it on a lease on nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const AnyImage& image() const noexcept;

    /// The validity mask, or null when the asset has none.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const an::ValidityMask* validity() const noexcept;

    /// The revision of what this lease reads.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API AssetRevision revision() const noexcept;

    /// Calls `body` with the typed image: `body(const an::Image<T>&)`.
    ///
    /// @param body a callable accepting every representation.
    /// @return what `body` returns.
    template <typename BodyT> decltype(auto) visit(BodyT&& body) const
    {
        return std::visit(std::forward<BodyT>(body), image());
    }

    /// A copy of the image and its mask as a new asset with a new identity, taken under this lease so no writer can
    /// change the image while it is copied. The copy is an engine allocation in the same context, counted against its
    /// budget.
    ///
    /// @return the new asset, or what the engine refuses when it copies.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<ImageAsset> snapshot() const;

    /// Another lease on the same asset, granted at once. No writer can hold the asset while this lease is held, so the
    /// share waits for nothing - not even a writer waiting for the asset, which `ImageAsset::read` waits behind. A
    /// holder reads an asset it already reads through shares: a second lease asked of the asset would wait for that
    /// writer, and the writer for this lease, for ever.
    ///
    /// @return the share; a lease on nothing when this is one.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API AssetReadLease share() const;

    /// Releases the lease now. Releasing twice releases once.
    AFTERNIGHTCONSUMERRUNTIME_API void release() noexcept;

private:
    friend class ImageAsset;

    explicit AssetReadLease(std::shared_ptr<detail::AssetState> state) noexcept;

    std::shared_ptr<detail::AssetState> m_state;
};

/// Permission to change an asset: its samples, its description and metadata through a stated transition, and its
/// validity mask. Exclusive: no reader and no other writer while it is held. The revision it leaves is published when
/// it is released.
///
/// Move-only. Keeps the asset's state alive, and may be released on any thread.
class AssetWriteLease
{
public:
    /// A lease on nothing.
    AFTERNIGHTCONSUMERRUNTIME_API AssetWriteLease() noexcept;

    /// Publishes the revision and releases the lease.
    AFTERNIGHTCONSUMERRUNTIME_API ~AssetWriteLease();

    /// Takes over another lease.
    ///
    /// @param other the lease to take over.
    AFTERNIGHTCONSUMERRUNTIME_API AssetWriteLease(AssetWriteLease&& other) noexcept;

    /// Releases this lease and takes over another.
    ///
    /// @param other the lease to take over.
    /// @return this lease.
    AFTERNIGHTCONSUMERRUNTIME_API AssetWriteLease& operator=(AssetWriteLease&& other) noexcept;

    AssetWriteLease(const AssetWriteLease&) = delete;
    AssetWriteLease& operator=(const AssetWriteLease&) = delete;

    /// Whether this is a lease.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool valid() const noexcept;

    /// The asset's identity, or zero for a lease on nothing.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint64_t assetIdentity() const noexcept;

    /// The image, for reading. Calling it on a lease on nothing is a defect.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const AnyImage& image() const noexcept;

    /// Write access to the samples, moving the pixel generation as the engine's `Image::write` does.
    ///
    /// @return the access, or empty when the image does not hold `SampleT` samples.
    template <an::ResidentSample SampleT> [[nodiscard]] std::optional<an::ImageWriteAccess<SampleT>> write() noexcept
    {
        an::Image<SampleT>* typed = std::get_if<an::Image<SampleT>>(&mutableImage());
        if (typed == nullptr)
        {
            return std::nullopt;
        }
        return typed->write();
    }

    /// Calls `body` with write access to the typed samples - `body(an::ImageWriteAccess<T>&&)` - moving the pixel
    /// generation.
    ///
    /// @param body a callable accepting every representation.
    /// @return what `body` returns.
    template <typename BodyT> decltype(auto) visitWrite(BodyT&& body)
    {
        return std::visit(
            [&body](auto& typed) -> decltype(auto) { return std::forward<BodyT>(body)(std::move(*typed.write())); },
            mutableImage());
    }

    /// Applies a metadata transition, moving the semantic generation when it changes what the image means.
    ///
    /// @param change the transition.
    /// @return what the engine's `Image::transition` returns.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status transition(const an::MetadataTransition& change);

    /// Re-describes the image and applies a metadata transition, as the engine's `Image::transition` does.
    ///
    /// @param described the new description of the same layout.
    /// @param change the transition, which records the change.
    /// @return what the engine's `Image::transition` returns.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status transition(const an::ImageDescriptor& described,
                                                                      const an::MetadataTransition& change);

    /// The validity mask for reading, or null when the asset has none.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const an::ValidityMask* validity() const noexcept;

    /// The validity mask for writing, moving the validity generation; null when the asset has none.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::ValidityMask* writeValidity() noexcept;

    /// Gives the asset a validity mask, or replaces the one it has.
    ///
    /// @param mask a mask covering the image, from the same engine context.
    /// @return success, or `ErrorCode::IncompatibleArguments` when the mask belongs to another context, or what the
    ///         engine's `checkMaskCovers` refuses.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status setValidity(an::ValidityMask&& mask);

    /// Removes the validity mask, if the asset has one.
    AFTERNIGHTCONSUMERRUNTIME_API void clearValidity() noexcept;

    /// The revision of the image as this lease leaves it now.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API AssetRevision revision() const noexcept;

    /// Publishes the revision and releases the lease now. Releasing twice releases once.
    AFTERNIGHTCONSUMERRUNTIME_API void release() noexcept;

private:
    friend class ImageAsset;

    explicit AssetWriteLease(std::shared_ptr<detail::AssetState> state) noexcept;

    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API AnyImage& mutableImage() noexcept;

    std::shared_ptr<detail::AssetState> m_state;
};

/// The consumer's owner of one image.
///
/// Move-only: one owner, and shared ownership is a shared pointer to it. The leases it hands out keep its state alive.
class ImageAsset
{
public:
    /// An asset that holds nothing.
    AFTERNIGHTCONSUMERRUNTIME_API ImageAsset() noexcept;

    /// Lets the state go; it is destroyed with its last lease.
    AFTERNIGHTCONSUMERRUNTIME_API ~ImageAsset();

    /// Takes over another asset.
    ///
    /// @param other the asset to take over.
    AFTERNIGHTCONSUMERRUNTIME_API ImageAsset(ImageAsset&& other) noexcept;

    /// Lets this asset's state go and takes over another's.
    ///
    /// @param other the asset to take over.
    /// @return this asset.
    AFTERNIGHTCONSUMERRUNTIME_API ImageAsset& operator=(ImageAsset&& other) noexcept;

    ImageAsset(const ImageAsset&) = delete;
    ImageAsset& operator=(const ImageAsset&) = delete;

    /// Takes an image, and optionally its validity mask, over as a new asset of a session.
    ///
    /// @param session the session whose context the image's storage came from.
    /// @param image the image; it must hold samples.
    /// @param validity a mask covering the image, from the same context, or nothing.
    /// @return the asset; `ErrorCode::NotInitialized` for an image that holds nothing;
    /// `ErrorCode::IncompatibleArguments`
    ///         when the image or the mask came from another context than the session's; what `checkMaskCovers`
    ///         refuses for the mask; or what `EngineSession::hold` refuses for the session's state.
    [[nodiscard]] static AFTERNIGHTCONSUMERRUNTIME_API an::Result<ImageAsset>
    adopt(const EngineSession& session, AnyImage&& image, std::optional<an::ValidityMask> validity = std::nullopt);

    /// Whether this holds an asset.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool valid() const noexcept;

    /// The asset's identity, unique in the process; zero for an asset that holds nothing.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint64_t identity() const noexcept;

    /// The last published revision, read without a lease.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API AssetRevision revision() const;

    /// The last published description, read without a lease.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::optional<an::ImageDescriptor> descriptor() const;

    /// Takes a read lease, waiting while a writer holds or is waiting for the asset.
    ///
    /// @param stop stops the wait.
    /// @return the lease, `ErrorCode::Cancelled` when stopped while waiting, or `ErrorCode::NotInitialized` for an
    ///         asset that holds nothing.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<AssetReadLease> read(std::stop_token stop = {}) const;

    /// Takes a read lease only if no writer holds or is waiting for the asset. Never blocks.
    ///
    /// @return the lease, or `ErrorCode::Busy`.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<AssetReadLease> tryRead() const;

    /// Takes the write lease, waiting until no reader or writer holds the asset. Readers arriving while it waits wait
    /// behind it.
    ///
    /// @param stop stops the wait.
    /// @return the lease, `ErrorCode::Cancelled` when stopped while waiting, or `ErrorCode::NotInitialized` for an
    ///         asset that holds nothing.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<AssetWriteLease> write(std::stop_token stop = {});

    /// Takes the write lease only if nothing holds the asset. Never blocks.
    ///
    /// @return the lease, or `ErrorCode::Busy`.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<AssetWriteLease> tryWrite();

    /// A copy as a new asset with a new identity, taken under a read lease held for the copy.
    ///
    /// @param stop stops the wait for the lease.
    /// @return the copy, or what `read` or the copy refuses.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<ImageAsset> snapshot(std::stop_token stop = {}) const;

    /// How many read leases are held now. A diagnostic.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint32_t readers() const;

    /// Whether the write lease is held now. A diagnostic.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API bool writing() const;

private:
    friend class AssetReadLease;

    explicit ImageAsset(std::shared_ptr<detail::AssetState> state) noexcept;

    std::shared_ptr<detail::AssetState> m_state;
};

} // namespace an::consumer

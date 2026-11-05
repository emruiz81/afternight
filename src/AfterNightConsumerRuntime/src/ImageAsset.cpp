// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image asset: its shared state, the reader-writer lock behind its leases, adoption and snapshots.
///
/// The lock is a mutex, a condition and two counts rather than a standard shared mutex, because a lease may be released
/// on another thread than the one that took it - a GUI thread takes a read lease and hands it to a worker - and a
/// standard shared mutex must be unlocked by the thread that locked it. Writers are preferred: a reader arriving while
/// a writer waits waits behind it, so a stream of previews cannot starve an apply.

#include "SessionCore.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace an::consumer
{

namespace detail
{

/// What an asset and its leases share. A plain record behind the asset's interface.
class AssetState
{
public:
    AssetState(std::shared_ptr<SessionCore> owner, std::uint64_t assetIdentity, AnyImage&& adopted,
               std::optional<an::ValidityMask>&& mask)
        : core(std::move(owner)), identity(assetIdentity), image(std::move(adopted)), validity(std::move(mask))
    {
        publishLocked();
    }

    AssetState(const AssetState&) = delete;
    AssetState& operator=(const AssetState&) = delete;
    AssetState(AssetState&&) = delete;
    AssetState& operator=(AssetState&&) = delete;

    /// Releases the storage before the session stops counting the asset, so a replacement the session then permits
    /// finds the engine context with nothing of this asset outstanding.
    ~AssetState()
    {
        image = AnyImage{};
        validity.reset();
        core->releaseAsset();
    }

    /// The revision of the image as it is now. Called under a lease, or with `mutex` held and no writer.
    [[nodiscard]] AssetRevision currentRevision() const noexcept
    {
        AssetRevision revision = std::visit(
            [](const auto& typed) {
                return AssetRevision{.pixelGeneration = typed.pixelGeneration(),
                                     .semanticGeneration = typed.semanticGeneration()};
            },
            image);
        revision.assetIdentity = identity;
        revision.validityGeneration = validityGeneration;
        return revision;
    }

    /// Makes the image's current revision and description what a caller without a lease reads.
    void publishLocked()
    {
        published = currentRevision();
        publishedDescriptor = std::visit([](const auto& typed) { return typed.descriptor(); }, image);
    }

    /// The session that counts this asset.
    std::shared_ptr<SessionCore> core;

    /// Unique in the process.
    std::uint64_t identity = 0u;

    /// Guards the lease counts and the published revision.
    std::mutex mutex;

    /// Signalled when a lease is released or a writer stops waiting.
    std::condition_variable_any changed;

    /// Read leases held. Guarded by `mutex`.
    std::uint32_t readers = 0u;

    /// Whether the write lease is held. Guarded by `mutex`.
    bool writer = false;

    /// Writers waiting for the lease. Guarded by `mutex`.
    std::uint32_t writersWaiting = 0u;

    /// The samples. Read under a lease; written under the write lease.
    AnyImage image;

    /// The validity mask, if any. Read under a lease; written under the write lease.
    std::optional<an::ValidityMask> validity;

    /// Moves when the mask is replaced, removed or written. Written under the write lease.
    std::uint64_t validityGeneration = 0u;

    /// What a caller without a lease reads. Guarded by `mutex`.
    AssetRevision published{};

    /// The description a caller without a lease reads. Guarded by `mutex`.
    std::optional<an::ImageDescriptor> publishedDescriptor;
};

} // namespace detail

namespace
{

/// A process-unique identity for a new asset.
[[nodiscard]] std::uint64_t nextAssetIdentity() noexcept
{
    static std::atomic<std::uint64_t> counter{0u};
    return counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

[[nodiscard]] an::Status failure(an::ErrorCode code, std::string message, const char* operation,
                                 const char* parameter = nullptr)
{
    an::StatusDetail detail{.message = std::move(message), .operation = operation, .domain = an::ErrorDomain::Image};
    if (parameter != nullptr)
    {
        detail.context.push_back(an::StatusContextField::ofText(an::ContextKey::Parameter, parameter));
    }
    return an::Status::failure(code, std::move(detail));
}

[[nodiscard]] bool holdsSamples(const AnyImage& image) noexcept
{
    return std::visit([](const auto& typed) { return typed.valid(); }, image);
}

[[nodiscard]] std::uint64_t contextOf(const AnyImage& image) noexcept
{
    return std::visit([](const auto& typed) { return typed.storage().contextId(); }, image);
}

/// Whether a mask can describe an image's validity: same context, covering its extent.
[[nodiscard]] an::Status checkMaskFor(const AnyImage& image, const an::ValidityMask& mask, const char* operation)
{
    if (!mask.valid())
    {
        return failure(an::ErrorCode::InvalidArgument, "the validity mask holds nothing", operation, "validity");
    }
    if (mask.contextId() != contextOf(image))
    {
        return failure(an::ErrorCode::IncompatibleArguments,
                       "the validity mask was taken from another engine context than the image's", operation,
                       "validity");
    }
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, image);
    const auto view = mask.view();
    if (!described.has_value() || !view.has_value())
    {
        return failure(an::ErrorCode::InvalidArgument, "the image or its mask cannot be viewed", operation, "validity");
    }
    return an::checkMaskCovers(*view, described->extent(), operation);
}

/// Counts and builds an asset over a session's core.
[[nodiscard]] an::Result<std::shared_ptr<detail::AssetState>>
makeState(const std::shared_ptr<detail::SessionCore>& core, AnyImage&& image, std::optional<an::ValidityMask>&& mask,
          const char* operation)
{
    const an::Status registered = core->registerAsset(contextOf(image), operation);
    if (registered.failed())
    {
        return registered;
    }
    try
    {
        return std::make_shared<detail::AssetState>(core, nextAssetIdentity(), std::move(image), std::move(mask));
    }
    catch (const std::bad_alloc&)
    {
        core->releaseAsset();
        return an::Status::failure(an::ErrorCode::AllocationFailed);
    }
}

} // namespace

an::SampleType sampleTypeOf(const AnyImage& image) noexcept
{
    return std::visit([](const auto& typed) { return std::decay_t<decltype(typed)>::kSampleType; }, image);
}

// ---- Read leases

AssetReadLease::AssetReadLease() noexcept = default;

AssetReadLease::~AssetReadLease()
{
    release();
}

AssetReadLease::AssetReadLease(AssetReadLease&& other) noexcept = default;

AssetReadLease& AssetReadLease::operator=(AssetReadLease&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_state = std::move(other.m_state);
    }
    return *this;
}

AssetReadLease::AssetReadLease(std::shared_ptr<detail::AssetState> state) noexcept : m_state(std::move(state)) {}

bool AssetReadLease::valid() const noexcept
{
    return m_state != nullptr;
}

std::uint64_t AssetReadLease::assetIdentity() const noexcept
{
    return m_state != nullptr ? m_state->identity : 0u;
}

const AnyImage& AssetReadLease::image() const noexcept
{
    return m_state->image;
}

const an::ValidityMask* AssetReadLease::validity() const noexcept
{
    return m_state != nullptr && m_state->validity.has_value() ? &*m_state->validity : nullptr;
}

AssetRevision AssetReadLease::revision() const noexcept
{
    return m_state != nullptr ? m_state->currentRevision() : AssetRevision{};
}

an::Result<ImageAsset> AssetReadLease::snapshot() const
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "a lease on nothing has nothing to copy",
                       "AssetReadLease::snapshot");
    }
    an::Result<AnyImage> copied = std::visit(
        [](const auto& typed) -> an::Result<AnyImage> {
            auto clone = typed.clone();
            if (!clone.hasValue())
            {
                return clone.status();
            }
            return AnyImage{std::move(*clone)};
        },
        m_state->image);
    if (!copied.hasValue())
    {
        return copied.status();
    }
    std::optional<an::ValidityMask> mask;
    if (m_state->validity.has_value())
    {
        an::Result<an::ValidityMask> clone = m_state->validity->clone();
        if (!clone.hasValue())
        {
            return clone.status();
        }
        mask.emplace(std::move(*clone));
    }
    an::Result<std::shared_ptr<detail::AssetState>> state =
        makeState(m_state->core, std::move(*copied), std::move(mask), "AssetReadLease::snapshot");
    if (!state.hasValue())
    {
        return state.status();
    }
    return ImageAsset{std::move(*state)};
}

AssetReadLease AssetReadLease::share() const
{
    if (m_state == nullptr)
    {
        return AssetReadLease{};
    }
    {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        ++m_state->readers;
    }
    return AssetReadLease{m_state};
}

void AssetReadLease::release() noexcept
{
    if (m_state == nullptr)
    {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        --m_state->readers;
    }
    m_state->changed.notify_all();
    m_state.reset();
}

// ---- Write leases

AssetWriteLease::AssetWriteLease() noexcept = default;

AssetWriteLease::~AssetWriteLease()
{
    release();
}

AssetWriteLease::AssetWriteLease(AssetWriteLease&& other) noexcept = default;

AssetWriteLease& AssetWriteLease::operator=(AssetWriteLease&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_state = std::move(other.m_state);
    }
    return *this;
}

AssetWriteLease::AssetWriteLease(std::shared_ptr<detail::AssetState> state) noexcept : m_state(std::move(state)) {}

bool AssetWriteLease::valid() const noexcept
{
    return m_state != nullptr;
}

std::uint64_t AssetWriteLease::assetIdentity() const noexcept
{
    return m_state != nullptr ? m_state->identity : 0u;
}

const AnyImage& AssetWriteLease::image() const noexcept
{
    return m_state->image;
}

AnyImage& AssetWriteLease::mutableImage() noexcept
{
    return m_state->image;
}

an::Status AssetWriteLease::transition(const an::MetadataTransition& change)
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "a lease on nothing has nothing to describe",
                       "AssetWriteLease::transition");
    }
    return std::visit([&change](auto& typed) { return typed.transition(change); }, m_state->image);
}

an::Status AssetWriteLease::transition(const an::ImageDescriptor& described, const an::MetadataTransition& change)
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "a lease on nothing has nothing to describe",
                       "AssetWriteLease::transition");
    }
    return std::visit([&described, &change](auto& typed) { return typed.transition(described, change); },
                      m_state->image);
}

const an::ValidityMask* AssetWriteLease::validity() const noexcept
{
    return m_state != nullptr && m_state->validity.has_value() ? &*m_state->validity : nullptr;
}

an::ValidityMask* AssetWriteLease::writeValidity() noexcept
{
    if (m_state == nullptr || !m_state->validity.has_value())
    {
        return nullptr;
    }
    ++m_state->validityGeneration;
    return &*m_state->validity;
}

an::Status AssetWriteLease::setValidity(an::ValidityMask&& mask)
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "a lease on nothing has no image for a mask",
                       "AssetWriteLease::setValidity");
    }
    const an::Status fits = checkMaskFor(m_state->image, mask, "AssetWriteLease::setValidity");
    if (fits.failed())
    {
        return fits;
    }
    m_state->validity.emplace(std::move(mask));
    ++m_state->validityGeneration;
    return an::Status::success();
}

void AssetWriteLease::clearValidity() noexcept
{
    if (m_state != nullptr && m_state->validity.has_value())
    {
        m_state->validity.reset();
        ++m_state->validityGeneration;
    }
}

AssetRevision AssetWriteLease::revision() const noexcept
{
    return m_state != nullptr ? m_state->currentRevision() : AssetRevision{};
}

void AssetWriteLease::release() noexcept
{
    if (m_state == nullptr)
    {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->publishLocked();
        m_state->writer = false;
    }
    m_state->changed.notify_all();
    m_state.reset();
}

// ---- Assets

ImageAsset::ImageAsset() noexcept = default;

ImageAsset::~ImageAsset() = default;

ImageAsset::ImageAsset(ImageAsset&& other) noexcept = default;

ImageAsset& ImageAsset::operator=(ImageAsset&& other) noexcept = default;

ImageAsset::ImageAsset(std::shared_ptr<detail::AssetState> state) noexcept : m_state(std::move(state)) {}

an::Result<ImageAsset> ImageAsset::adopt(const EngineSession& session, AnyImage&& image,
                                         std::optional<an::ValidityMask> validity)
{
    if (session.m_core == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "an asset needs a session, and this one holds nothing",
                       "ImageAsset::adopt", "session");
    }
    if (!holdsSamples(image))
    {
        return failure(an::ErrorCode::NotInitialized, "an asset owns samples, and this image holds none",
                       "ImageAsset::adopt", "image");
    }
    if (validity.has_value())
    {
        const an::Status fits = checkMaskFor(image, *validity, "ImageAsset::adopt");
        if (fits.failed())
        {
            return fits;
        }
    }
    an::Result<std::shared_ptr<detail::AssetState>> state =
        makeState(session.m_core, std::move(image), std::move(validity), "ImageAsset::adopt");
    if (!state.hasValue())
    {
        return state.status();
    }
    return ImageAsset{std::move(*state)};
}

bool ImageAsset::valid() const noexcept
{
    return m_state != nullptr;
}

std::uint64_t ImageAsset::identity() const noexcept
{
    return m_state != nullptr ? m_state->identity : 0u;
}

AssetRevision ImageAsset::revision() const
{
    if (m_state == nullptr)
    {
        return AssetRevision{};
    }
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->published;
}

std::optional<an::ImageDescriptor> ImageAsset::descriptor() const
{
    if (m_state == nullptr)
    {
        return std::nullopt;
    }
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->publishedDescriptor;
}

an::Result<AssetReadLease> ImageAsset::read(std::stop_token stop) const
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "an asset that holds nothing cannot be read", "ImageAsset::read");
    }
    std::unique_lock<std::mutex> lock(m_state->mutex);
    const auto ready = [this] { return !m_state->writer && m_state->writersWaiting == 0u; };
    if (!m_state->changed.wait(lock, stop, ready))
    {
        return failure(an::ErrorCode::Cancelled, "the wait for a read lease was stopped", "ImageAsset::read");
    }
    ++m_state->readers;
    return AssetReadLease{m_state};
}

an::Result<AssetReadLease> ImageAsset::tryRead() const
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "an asset that holds nothing cannot be read",
                       "ImageAsset::tryRead");
    }
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    if (m_state->writer || m_state->writersWaiting != 0u)
    {
        return failure(an::ErrorCode::Busy, "a writer holds or is waiting for the asset", "ImageAsset::tryRead");
    }
    ++m_state->readers;
    return AssetReadLease{m_state};
}

an::Result<AssetWriteLease> ImageAsset::write(std::stop_token stop)
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "an asset that holds nothing cannot be written",
                       "ImageAsset::write");
    }
    std::unique_lock<std::mutex> lock(m_state->mutex);
    const auto ready = [this] { return !m_state->writer && m_state->readers == 0u; };
    ++m_state->writersWaiting;
    const bool acquired = m_state->changed.wait(lock, stop, ready);
    --m_state->writersWaiting;
    if (!acquired)
    {
        // Readers held back behind this writer may go now.
        lock.unlock();
        m_state->changed.notify_all();
        return failure(an::ErrorCode::Cancelled, "the wait for the write lease was stopped", "ImageAsset::write");
    }
    m_state->writer = true;
    return AssetWriteLease{m_state};
}

an::Result<AssetWriteLease> ImageAsset::tryWrite()
{
    if (m_state == nullptr)
    {
        return failure(an::ErrorCode::NotInitialized, "an asset that holds nothing cannot be written",
                       "ImageAsset::tryWrite");
    }
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    if (m_state->writer || m_state->readers != 0u)
    {
        return failure(an::ErrorCode::Busy, "the asset is being read or written", "ImageAsset::tryWrite");
    }
    m_state->writer = true;
    return AssetWriteLease{m_state};
}

an::Result<ImageAsset> ImageAsset::snapshot(std::stop_token stop) const
{
    an::Result<AssetReadLease> lease = read(std::move(stop));
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return lease->snapshot();
}

std::uint32_t ImageAsset::readers() const
{
    if (m_state == nullptr)
    {
        return 0u;
    }
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->readers;
}

bool ImageAsset::writing() const
{
    if (m_state == nullptr)
    {
        return false;
    }
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->writer;
}

} // namespace an::consumer

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The run's cache of decoded and calibrated frames.

#include "stdafx.hpp"
#include "WorkflowImageCache.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
// Conservative allocator/control-block charge for the map node, make_shared<Entry> block, image shared-ownership
// block, tree links, alignment, and allocator headers. Dynamic key storage and concrete objects are charged below.
constexpr std::uint64_t kAccountedAllocatorAndControlBlockBytes = 512u;

void appendIdentityPart(QString& destination, const QString& value)
{
    destination += QString::number(value.size());
    destination += QLatin1Char(':');
    destination += value;
    destination += QLatin1Char('|');
}

QString fileGenerationIdentity(const FrameId& frame, const ExecutionPreparationSnapshot::FileStamp& stamp)
{
    QString identity;
    appendIdentityPart(identity, frame.identityKey());
    appendIdentityPart(identity, stamp.canonicalPath);
    appendIdentityPart(identity, stamp.exists ? QStringLiteral("1") : QStringLiteral("0"));
    appendIdentityPart(identity, stamp.isFile ? QStringLiteral("1") : QStringLiteral("0"));
    appendIdentityPart(identity, QString::number(stamp.sizeBytes));
    appendIdentityPart(identity, QString::number(stamp.modifiedAtMs));
    appendIdentityPart(identity, stamp.identityToken);
    return identity;
}

std::uint64_t checkedAdd(const std::uint64_t left, const std::uint64_t right)
{
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left)
    {
        throw std::overflow_error("Workflow image cache byte accounting overflow");
    }
    return left + right;
}

std::uint64_t checkedMultiply(const std::uint64_t left, const std::uint64_t right)
{
    if (left != 0 && right > (std::numeric_limits<std::uint64_t>::max)() / left)
    {
        throw std::overflow_error("Workflow image cache byte accounting overflow");
    }
    return left * right;
}

[[nodiscard]] bool imageValid(const an::consumer::AnyImage& image) noexcept
{
    return std::visit([](const auto& held) { return held.valid(); }, image);
}

void saturatingIncrement(std::atomic<std::uint64_t>& counter) noexcept
{
    std::uint64_t current = counter.load(std::memory_order_relaxed);
    constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    while (current != maximum && !counter.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
    {
    }
}
} // namespace

WorkflowImageCacheKey::WorkflowImageCacheKey(const WorkflowImageProductKind productKind, QString identity,
                                             QString baseIdentity)
    : m_productKind(productKind), m_identity(std::move(identity)), m_baseIdentity(std::move(baseIdentity))
{
}

WorkflowImageCacheKey WorkflowImageCacheKey::decoded(const FrameId& frame,
                                                     const ExecutionPreparationSnapshot::FileStamp& sourceStamp,
                                                     const QString& pixelVariant)
{
    QString base_identity = QStringLiteral("decoded|");
    appendIdentityPart(base_identity, frame.identityKey());
    QString identity = base_identity;
    appendIdentityPart(identity, fileGenerationIdentity(frame, sourceStamp));
    appendIdentityPart(identity, pixelVariant);
    return {WorkflowImageProductKind::Decoded, std::move(identity), std::move(base_identity)};
}

WorkflowImageCacheKey WorkflowImageCacheKey::calibrated(const FrameId& frame,
                                                        const ExecutionPreparationSnapshot::FileStamp& sourceStamp,
                                                        const QString& calibrationIdentity, const QString& pixelVariant)
{
    QString base_identity = QStringLiteral("calibrated|");
    appendIdentityPart(base_identity, frame.identityKey());
    QString identity = base_identity;
    appendIdentityPart(identity, fileGenerationIdentity(frame, sourceStamp));
    appendIdentityPart(identity, calibrationIdentity);
    appendIdentityPart(identity, pixelVariant);
    return {WorkflowImageProductKind::Calibrated, std::move(identity), std::move(base_identity)};
}

WorkflowImageProductKind WorkflowImageCacheKey::productKind() const noexcept
{
    return m_productKind;
}

const QString& WorkflowImageCacheKey::identity() const noexcept
{
    return m_identity;
}

const QString& WorkflowImageCacheKey::baseIdentity() const noexcept
{
    return m_baseIdentity;
}

struct WorkflowImageCache::State
{
    struct Ledger
    {
        std::atomic<std::uint64_t> currentBytes{0};
        std::atomic<std::uint64_t> peakBytes{0};
    };

    struct Entry
    {
        Entry(std::shared_ptr<Ledger> ledgerValue, WorkflowCachedImage imageValue,
              const std::uint64_t retainedBytesValue, an::consumer::MemoryReservation reservationValue)
            : ledger(std::move(ledgerValue)), image(std::move(imageValue)), retainedBytes(retainedBytesValue),
              reservation(std::move(reservationValue))
        {
        }

        ~Entry()
        {
            if (charged)
            {
                ledger->currentBytes.fetch_sub(retainedBytes, std::memory_order_relaxed);
            }
        }

        std::shared_ptr<Ledger> ledger;
        WorkflowCachedImage image;
        std::uint64_t retainedBytes = 0;
        /// The entry's share of the session's derived-cache allowance, returned with the last borrower.
        an::consumer::MemoryReservation reservation;
        bool charged = false;
    };

    struct Node
    {
        std::shared_ptr<Entry> entry;
        QString baseIdentity;
        std::uint64_t lastUse = 0;
    };

    struct Pending
    {
        std::condition_variable ready;
        WorkflowCachedImage result;
        std::exception_ptr failure;
        std::uint64_t generation = 0;
        bool complete = false;
    };

    State(const std::uint64_t capacityValue, WorkflowImageCache::SameKeyWaitObserver waitObserver,
          an::consumer::ConsumerMemory* memoryValue)
        : capacity(capacityValue), ledger(std::make_shared<Ledger>()), sameKeyWaitObserver(std::move(waitObserver)),
          memory(memoryValue)
    {
    }

    /// Evicts unborrowed entries, oldest first, until `shortfall` bytes of the allowance are free or none is left.
    std::uint64_t evictForAllowance(const std::uint64_t shortfall)
    {
        std::uint64_t freed = 0;
        std::vector<std::shared_ptr<Entry>> released;
        {
            std::lock_guard lock(mutex);
            while (freed < shortfall)
            {
                auto victim = entries.end();
                for (auto it = entries.begin(); it != entries.end(); ++it)
                {
                    if (it->second.entry.use_count() == 1 &&
                        (victim == entries.end() || it->second.lastUse < victim->second.lastUse))
                    {
                        victim = it;
                    }
                }
                if (victim == entries.end())
                {
                    break;
                }
                freed += victim->second.entry->reservation.bytes();
                released.push_back(std::move(victim->second.entry));
                entries.erase(victim);
                saturatingIncrement(evictions);
                saturatingIncrement(allowanceEvictions);
            }
        }
        released.clear();
        return freed;
    }

    std::uint64_t capacity = 0;
    std::shared_ptr<Ledger> ledger;
    mutable std::mutex mutex;
    std::map<QString, Node> entries;
    std::map<QString, std::shared_ptr<Pending>> pending;
    std::uint64_t sequence = 0;
    std::uint64_t generation = 0;
    WorkflowImageCache::SameKeyWaitObserver sameKeyWaitObserver;
    an::consumer::ConsumerMemory* memory = nullptr;
    an::consumer::EvictorRegistration evictor;

    std::atomic<std::uint64_t> physicalDecodedLoads{0};
    std::atomic<std::uint64_t> physicalCalibrations{0};
    std::atomic<std::uint64_t> hits{0};
    std::atomic<std::uint64_t> misses{0};
    std::atomic<std::uint64_t> evictions{0};
    std::atomic<std::uint64_t> capacityBypasses{0};
    std::atomic<std::uint64_t> oversizedBypasses{0};
    std::atomic<std::uint64_t> overflowBypasses{0};
    std::atomic<std::uint64_t> generationInvalidations{0};
    std::atomic<std::uint64_t> publications{0};
    std::atomic<std::uint64_t> failedBuilds{0};
    std::atomic<std::uint64_t> cancelledBuilds{0};
    std::atomic<std::uint64_t> sameKeyWaits{0};
    std::atomic<std::uint64_t> cleanupCalls{0};
    std::atomic<std::uint64_t> allowanceRefusals{0};
    std::atomic<std::uint64_t> allowanceEvictions{0};
};

WorkflowImageCache::WorkflowImageCache(const std::uint64_t capacityBytes, SameKeyWaitObserver sameKeyWaitObserver,
                                       an::consumer::ConsumerMemory* memory)
    : m_state(std::make_shared<State>(capacityBytes, std::move(sameKeyWaitObserver), memory))
{
    if (memory != nullptr)
    {
        const std::weak_ptr<State> weak = m_state;
        auto registration =
            memory->registerEvictor(an::consumer::MemoryCategory::DerivedCaches, [weak](const std::uint64_t shortfall) {
                const auto state = weak.lock();
                return state ? state->evictForAllowance(shortfall) : std::uint64_t{0};
            });
        if (registration.hasValue())
        {
            m_state->evictor = std::move(registration).value();
        }
    }
}

WorkflowImageCache::~WorkflowImageCache()
{
    if (m_state)
    {
        m_state->evictor.release();
    }
    clear();
}

WorkflowCachedImage WorkflowImageCache::load(const WorkflowImageCacheKey& key, const Loader& loader,
                                             const CancellationCheck& shouldCancel,
                                             const CompatibilityCheck& isCompatible,
                                             const GenerationCheck& isGenerationCurrent)
{
    if (!loader)
    {
        return {};
    }

    const auto cancelled = [&]() { return shouldCancel && shouldCancel(); };
    const auto generation_is_current = [&]() { return !isGenerationCurrent || isGenerationCurrent(); };
    if (cancelled())
    {
        saturatingIncrement(m_state->cancelledBuilds);
        return {};
    }
    std::shared_ptr<State::Pending> pending;
    for (;;)
    {
        if (cancelled())
        {
            saturatingIncrement(m_state->cancelledBuilds);
            return {};
        }
        std::unique_lock lock(m_state->mutex);
        if (auto found = m_state->entries.find(key.identity()); found != m_state->entries.end())
        {
            found->second.lastUse = ++m_state->sequence;
            const auto cached_entry = found->second.entry;
            lock.unlock();

            const bool generation_current = generation_is_current();
            const bool compatible = generation_current && cached_entry->image && imageValid(*cached_entry->image) &&
                                    (!isCompatible || isCompatible(*cached_entry->image));
            if (compatible)
            {
                saturatingIncrement(m_state->hits);
                return {cached_entry, cached_entry->image.get()};
            }

            lock.lock();
            found = m_state->entries.find(key.identity());
            if (found != m_state->entries.end() && found->second.entry == cached_entry)
            {
                m_state->entries.erase(found);
            }
            if (!generation_current)
            {
                saturatingIncrement(m_state->generationInvalidations);
                return {};
            }
            continue;
        }

        if (auto building = m_state->pending.find(key.identity()); building != m_state->pending.end())
        {
            saturatingIncrement(m_state->sameKeyWaits);
            if (m_state->sameKeyWaitObserver)
                m_state->sameKeyWaitObserver();
            pending = building->second;
            while (!pending->complete)
            {
                if (cancelled())
                {
                    saturatingIncrement(m_state->cancelledBuilds);
                    return {};
                }
                pending->ready.wait_for(lock, std::chrono::milliseconds(10));
            }
            if (pending->failure)
            {
                const auto failure = pending->failure;
                lock.unlock();
                std::rethrow_exception(failure);
            }
            const auto result = pending->result;
            lock.unlock();
            if (!generation_is_current())
            {
                saturatingIncrement(m_state->generationInvalidations);
                return {};
            }
            if (result && (!isCompatible || isCompatible(*result)))
            {
                return result;
            }
            return {};
        }

        const bool invalidates_generation =
            std::any_of(m_state->entries.cbegin(), m_state->entries.cend(), [&](const auto& item) {
                return item.second.baseIdentity == key.baseIdentity() && item.first != key.identity();
            });
        if (invalidates_generation)
        {
            saturatingIncrement(m_state->generationInvalidations);
        }
        pending = std::make_shared<State::Pending>();
        pending->generation = m_state->generation;
        m_state->pending.emplace(key.identity(), pending);
        saturatingIncrement(m_state->misses);
        break;
    }

    const auto complete_pending = [&](const WorkflowCachedImage& result = {},
                                      const std::exception_ptr& failure = {}) noexcept {
        {
            std::lock_guard lock(m_state->mutex);
            pending->result = result;
            pending->failure = failure;
            pending->complete = true;
            const auto found = m_state->pending.find(key.identity());
            if (found != m_state->pending.end() && found->second == pending)
            {
                m_state->pending.erase(found);
            }
        }
        pending->ready.notify_all();
    };

    WorkflowCachedImage image;
    try
    {
        if (cancelled())
        {
            saturatingIncrement(m_state->cancelledBuilds);
            complete_pending();
            return {};
        }
        if (!generation_is_current())
        {
            saturatingIncrement(m_state->generationInvalidations);
            complete_pending();
            return {};
        }
    }
    catch (...)
    {
        saturatingIncrement(m_state->failedBuilds);
        complete_pending({}, std::current_exception());
        throw;
    }
    if (key.productKind() == WorkflowImageProductKind::Decoded)
    {
        saturatingIncrement(m_state->physicalDecodedLoads);
    }
    else
    {
        saturatingIncrement(m_state->physicalCalibrations);
    }
    try
    {
        image = loader();
    }
    catch (...)
    {
        saturatingIncrement(m_state->failedBuilds);
        complete_pending({}, std::current_exception());
        throw;
    }

    bool suppress_publication = false;
    try
    {
        if (cancelled())
        {
            saturatingIncrement(m_state->cancelledBuilds);
            suppress_publication = true;
        }
        else if (!image || !imageValid(*image) || (isCompatible && !isCompatible(*image)))
        {
            saturatingIncrement(m_state->failedBuilds);
            image.reset();
        }
        else if (!generation_is_current())
        {
            saturatingIncrement(m_state->generationInvalidations);
            image.reset();
        }
    }
    catch (...)
    {
        saturatingIncrement(m_state->failedBuilds);
        image.reset();
        complete_pending({}, std::current_exception());
        throw;
    }

    std::shared_ptr<State::Entry> published_entry;
    try
    {
        if (image && !suppress_publication)
        {
            std::uint64_t retained_bytes = 0;
            try
            {
                retained_bytes = retainedBytes(key, *image);
            }
            catch (const std::overflow_error&)
            {
                saturatingIncrement(m_state->overflowBypasses);
            }

            if (retained_bytes > m_state->capacity)
            {
                saturatingIncrement(m_state->oversizedBypasses);
            }
            else if (retained_bytes != 0)
            {
                // The allowance is asked before the cache's lock is taken: the session may answer by asking this
                // cache's evictor, which takes it.
                an::consumer::MemoryReservation reservation;
                bool admitted = true;
                if (m_state->memory != nullptr)
                {
                    auto reserved =
                        m_state->memory->reserve(an::consumer::MemoryCategory::DerivedCaches, retained_bytes);
                    admitted = reserved.hasValue();
                    if (admitted)
                    {
                        reservation = std::move(reserved).value();
                    }
                    else
                    {
                        saturatingIncrement(m_state->allowanceRefusals);
                        suppress_publication = true;
                    }
                }
                auto candidate_entry =
                    std::make_shared<State::Entry>(m_state->ledger, image, retained_bytes, std::move(reservation));
                std::lock_guard lock(m_state->mutex);
                if (pending->generation != m_state->generation)
                {
                    suppress_publication = true;
                }
                if (suppress_publication)
                {
                    candidate_entry.reset();
                }
                else
                {
                    while (m_state->ledger->currentBytes.load(std::memory_order_relaxed) >
                               m_state->capacity - retained_bytes &&
                           !m_state->entries.empty())
                    {
                        const auto victim = std::min_element(m_state->entries.begin(), m_state->entries.end(),
                                                             [](const auto& left, const auto& right) {
                                                                 if (left.second.lastUse != right.second.lastUse)
                                                                 {
                                                                     return left.second.lastUse < right.second.lastUse;
                                                                 }
                                                                 return left.first < right.first;
                                                             });
                        m_state->entries.erase(victim);
                        saturatingIncrement(m_state->evictions);
                    }

                    std::uint64_t current = m_state->ledger->currentBytes.load(std::memory_order_relaxed);
                    bool charged = false;
                    while (current <= m_state->capacity - retained_bytes)
                    {
                        if (m_state->ledger->currentBytes.compare_exchange_weak(current, current + retained_bytes,
                                                                                std::memory_order_relaxed))
                        {
                            charged = true;
                            break;
                        }
                    }
                    if (charged)
                    {
                        const std::uint64_t charged_bytes = current + retained_bytes;
                        std::uint64_t peak = m_state->ledger->peakBytes.load(std::memory_order_relaxed);
                        while (peak < charged_bytes && !m_state->ledger->peakBytes.compare_exchange_weak(
                                                           peak, charged_bytes, std::memory_order_relaxed))
                        {
                        }
                        published_entry = std::move(candidate_entry);
                        published_entry->charged = true;
                        m_state->entries.emplace(key.identity(),
                                                 State::Node{published_entry, key.baseIdentity(), ++m_state->sequence});
                        saturatingIncrement(m_state->publications);
                    }
                    else
                    {
                        saturatingIncrement(m_state->capacityBypasses);
                    }
                }
            }
        }
    }
    catch (...)
    {
        complete_pending({}, std::current_exception());
        throw;
    }
    const WorkflowCachedImage completed_result =
        published_entry ? WorkflowCachedImage(published_entry, published_entry->image.get()) : image;
    complete_pending(completed_result);

    if (published_entry)
    {
        return {published_entry, published_entry->image.get()};
    }
    return image;
}

void WorkflowImageCache::clear() noexcept
{
    if (!m_state)
    {
        return;
    }
    {
        std::lock_guard lock(m_state->mutex);
        m_state->entries.clear();
        m_state->pending.clear();
        ++m_state->generation;
    }
    saturatingIncrement(m_state->cleanupCalls);
}

std::uint64_t WorkflowImageCache::capacityBytes() const noexcept
{
    return m_state->capacity;
}

WorkflowImageCacheDiagnostics WorkflowImageCache::diagnostics() const noexcept
{
    WorkflowImageCacheDiagnostics result;
    result.physicalDecodedLoads = m_state->physicalDecodedLoads.load(std::memory_order_relaxed);
    result.physicalCalibrations = m_state->physicalCalibrations.load(std::memory_order_relaxed);
    result.hits = m_state->hits.load(std::memory_order_relaxed);
    result.misses = m_state->misses.load(std::memory_order_relaxed);
    result.evictions = m_state->evictions.load(std::memory_order_relaxed);
    result.capacityBypasses = m_state->capacityBypasses.load(std::memory_order_relaxed);
    result.oversizedBypasses = m_state->oversizedBypasses.load(std::memory_order_relaxed);
    result.overflowBypasses = m_state->overflowBypasses.load(std::memory_order_relaxed);
    result.generationInvalidations = m_state->generationInvalidations.load(std::memory_order_relaxed);
    result.publications = m_state->publications.load(std::memory_order_relaxed);
    result.failedBuilds = m_state->failedBuilds.load(std::memory_order_relaxed);
    result.cancelledBuilds = m_state->cancelledBuilds.load(std::memory_order_relaxed);
    result.sameKeyWaits = m_state->sameKeyWaits.load(std::memory_order_relaxed);
    result.cleanupCalls = m_state->cleanupCalls.load(std::memory_order_relaxed);
    result.currentBytes = m_state->ledger->currentBytes.load(std::memory_order_relaxed);
    result.peakBytes = m_state->ledger->peakBytes.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(m_state->mutex);
        result.lookupEntries = static_cast<std::uint64_t>(m_state->entries.size());
    }
    result.allowanceRefusals = m_state->allowanceRefusals.load(std::memory_order_relaxed);
    result.allowanceEvictions = m_state->allowanceEvictions.load(std::memory_order_relaxed);
    return result;
}

std::uint64_t WorkflowImageCache::retainedBytes(const WorkflowImageCacheKey& key, const an::consumer::AnyImage& image)
{
    std::uint64_t bytes = std::visit([](const auto& held) { return held.storage().capacityBytes(); }, image);
    // The cards an image carries travel with it.
    for (const an::CodecCard& card :
         std::visit([](const auto& held) { return held.metadata().codec().cards(); }, image))
    {
        bytes = checkedAdd(bytes, static_cast<std::uint64_t>(sizeof(an::CodecCard) + card.keyword().capacity() +
                                                             card.value().capacity() + card.comment().capacity()));
    }
    bytes = checkedAdd(bytes, static_cast<std::uint64_t>(sizeof(State::Entry)));
    bytes = checkedAdd(bytes, static_cast<std::uint64_t>(sizeof(std::map<QString, State::Node>::value_type)));
    bytes = checkedAdd(bytes, kAccountedAllocatorAndControlBlockBytes);
    bytes =
        checkedAdd(bytes, checkedMultiply(static_cast<std::uint64_t>(key.identity().capacity() + 1), sizeof(QChar)));
    bytes = checkedAdd(bytes,
                       checkedMultiply(static_cast<std::uint64_t>(key.baseIdentity().capacity() + 1), sizeof(QChar)));
    return bytes;
}

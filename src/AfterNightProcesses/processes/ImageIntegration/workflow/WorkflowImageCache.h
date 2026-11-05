// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The run's cache of decoded and calibrated frames, shared by its stages and keyed by the file revision and the
/// treatment that produced each one.
///
/// Every stage that reads a light reads it through this cache, so a frame two stages need is decoded or calibrated
/// once. An entry is an engine image, immutable once published and shared by every borrower; removing it from the
/// lookup returns nothing until the last borrower lets it go. The cache holds no more than its byte cap, and when it is
/// given the session's memory it also holds no more than the session's derived-cache allowance admits: each entry is
/// reserved against that allowance before it is kept, an entry the allowance refuses is handed back uncached, and the
/// session may ask the cache to evict to make room for another reservation.

#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace an::consumer
{
class ConsumerMemory;
} // namespace an::consumer

enum class WorkflowImageProductKind : unsigned char
{
    Decoded,
    Calibrated
};

class AFTERNIGHTPROCESSES_API WorkflowImageCacheKey
{
public:
    [[nodiscard]] static WorkflowImageCacheKey decoded(const FrameId& frame,
                                                       const ExecutionPreparationSnapshot::FileStamp& sourceStamp,
                                                       const QString& pixelVariant);
    [[nodiscard]] static WorkflowImageCacheKey calibrated(const FrameId& frame,
                                                          const ExecutionPreparationSnapshot::FileStamp& sourceStamp,
                                                          const QString& calibrationIdentity,
                                                          const QString& pixelVariant);

    [[nodiscard]] WorkflowImageProductKind productKind() const noexcept;
    [[nodiscard]] const QString& identity() const noexcept;
    [[nodiscard]] const QString& baseIdentity() const noexcept;

private:
    WorkflowImageCacheKey(WorkflowImageProductKind productKind, QString identity, QString baseIdentity);

    WorkflowImageProductKind m_productKind = WorkflowImageProductKind::Decoded;
    QString m_identity;
    QString m_baseIdentity;
};

struct WorkflowImageCacheDiagnostics
{
    std::uint64_t physicalDecodedLoads = 0;
    std::uint64_t physicalCalibrations = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t capacityBypasses = 0;
    std::uint64_t oversizedBypasses = 0;
    std::uint64_t overflowBypasses = 0;
    std::uint64_t generationInvalidations = 0;
    std::uint64_t publications = 0;
    std::uint64_t failedBuilds = 0;
    std::uint64_t cancelledBuilds = 0;
    std::uint64_t sameKeyWaits = 0;
    std::uint64_t cleanupCalls = 0;
    std::uint64_t currentBytes = 0;
    std::uint64_t peakBytes = 0;
    std::uint64_t lookupEntries = 0;

    /// Entries the session's derived-cache allowance refused to admit, handed back uncached.
    std::uint64_t allowanceRefusals = 0;

    /// Entries evicted because the session asked the cache to make room.
    std::uint64_t allowanceEvictions = 0;
};

/// A cached frame: an engine image no borrower may change.
using WorkflowCachedImage = std::shared_ptr<const an::consumer::AnyImage>;

/**
 * Execution-local decoded/calibrated image cache with a hard byte-token cap.
 *
 * Returned images alias an internal immutable entry. Removing an entry from the
 * lookup table does not return its byte token until the final borrower releases
 * the image. Callers that mutate images must copy one first.
 */
class AFTERNIGHTPROCESSES_API WorkflowImageCache final
{
public:
    using Loader = std::function<WorkflowCachedImage()>;
    using CancellationCheck = std::function<bool()>;
    using CompatibilityCheck = std::function<bool(const an::consumer::AnyImage&)>;
    using GenerationCheck = std::function<bool()>;
    using SameKeyWaitObserver = std::function<void()>;

    /// A cache of at most `capacityBytes`.
    ///
    /// @param capacityBytes the cache's own cap.
    /// @param sameKeyWaitObserver told each time a caller waits for another to build the same entry.
    /// @param memory the session's memory, whose derived-cache allowance every entry is reserved against; nothing for
    ///        a cache held to its own cap alone. It outlives the cache.
    explicit WorkflowImageCache(std::uint64_t capacityBytes, SameKeyWaitObserver sameKeyWaitObserver = {},
                                an::consumer::ConsumerMemory* memory = nullptr);
    ~WorkflowImageCache();

    WorkflowImageCache(const WorkflowImageCache&) = delete;
    WorkflowImageCache& operator=(const WorkflowImageCache&) = delete;

    [[nodiscard]] WorkflowCachedImage load(const WorkflowImageCacheKey& key, const Loader& loader,
                                           const CancellationCheck& shouldCancel = {},
                                           const CompatibilityCheck& isCompatible = {},
                                           const GenerationCheck& isGenerationCurrent = {});
    void clear() noexcept;

    [[nodiscard]] std::uint64_t capacityBytes() const noexcept;
    [[nodiscard]] WorkflowImageCacheDiagnostics diagnostics() const noexcept;

    /// The bytes an entry is accounted as: the image's storage capacity and the cache's own bookkeeping for it.
    [[nodiscard]] static std::uint64_t retainedBytes(const WorkflowImageCacheKey& key,
                                                     const an::consumer::AnyImage& image);

private:
    struct State;
    std::shared_ptr<State> m_state;
};

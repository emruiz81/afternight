// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file FrameCollection.h
 * @brief Thread-safe container for managing frame collections.
 *
 * FrameCollection provides a thread-safe, observable container for FrameInfo
 * objects with efficient O(1) lookup by FrameId and support for batch updates
 * with coalesced change notifications.
 *
 * @par Thread Safety
 * All public methods are protected by a shared_mutex, allowing concurrent
 * reads with exclusive writes. Signals are emitted outside the lock.
 *
 * @par Batch Updates
 * Use beginBatchUpdate()/endBatchUpdate() to group multiple mutations and
 * emit a single collectionReset() signal instead of individual notifications.
 *
 * @see IntegrationDataModel, FrameInfo, GroupingEngine
 */

#include "GroupingKeys.h"

#include <QObject>
#include <verdigris/wobjectdefs.h>

#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <functional>
#include <unordered_map>
#include <vector>

/**
 * @brief Thread-safe container for frames with efficient lookup.
 *
 * This class manages a collection of FrameInfo objects for a specific frame
 * type (Bias, Dark, Flat, or Light). It provides:
 * - O(1) lookup by FrameId
 * - Sorted iteration (by path, date, or exposure)
 * - Group-based filtering
 * - Observable change notifications via Qt signals
 *
 * @par Memory Model
 * Frames are stored contiguously in a vector with an auxiliary index map
 * for fast lookup. This provides good cache locality for iteration.
 */
class AFTERNIGHTPROCESSES_API FrameCollection : public QObject
{
    W_OBJECT(FrameCollection)

public:
    explicit FrameCollection(QObject* parent = nullptr);

    // Mutation
    bool addFrame(FrameInfo frame);
    void addFrames(const std::vector<FrameInfo>& frames);
    bool removeFrame(const FrameId& id);
    size_t removeFrames(const std::vector<FrameId>& ids);
    bool updateFrame(const FrameId& id, const std::function<void(FrameInfo&)>& updater);
    void clear();

    void beginBatchUpdate();
    void endBatchUpdate();

    // Accessors
    [[nodiscard]] size_t size() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::vector<FrameInfo> frames() const;
    [[nodiscard]] std::optional<FrameInfo> findById(const FrameId& id) const;
    [[nodiscard]] std::vector<FrameInfo> filterByType(FrameType type) const;
    [[nodiscard]] std::vector<FrameInfo> filterByGroup(const GroupKey& key, FrameType type,
                                                       const GroupingOptions& options) const;
    [[nodiscard]] std::vector<FrameInfo> sortedByPath() const;
    [[nodiscard]] std::vector<FrameInfo> sortedByDate() const;
    [[nodiscard]] std::vector<FrameInfo> sortedByExposure() const;
    [[nodiscard]] std::map<FrameType, size_t> countsByType() const;

signals:
    // Signals (Verdigris).
    void framesAdded() W_SIGNAL(framesAdded);
    void framesRemoved() W_SIGNAL(framesRemoved);
    void collectionCleared() W_SIGNAL(collectionCleared);
    void collectionReset() W_SIGNAL(collectionReset);

private:
    [[nodiscard]] GroupKey resolveGroupKey(const FrameInfo& frame, FrameType type,
                                           const GroupingOptions& options) const;
    void markPendingReset();

    mutable std::shared_mutex m_mutex;
    std::vector<FrameInfo> m_frames;
    std::unordered_map<FrameId, size_t> m_index;
    int m_batchDepth = 0;
    bool m_pendingReset = false;
};

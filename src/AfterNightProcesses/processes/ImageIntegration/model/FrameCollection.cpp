// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameCollection.h"

#include "GroupingEngine.h"

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <utility>

W_OBJECT_IMPL(FrameCollection)

FrameCollection::FrameCollection(QObject* parent) : QObject(parent) {}

bool FrameCollection::addFrame(FrameInfo frame)
{
    bool added = false;
    bool emit_added = false;

    {
        std::unique_lock lock(m_mutex);

        auto it = m_index.find(frame.id);
        if (it != m_index.end())
        {
            m_frames[it->second] = std::move(frame);
        }
        else
        {
            const size_t index = m_frames.size();
            m_frames.push_back(std::move(frame));
            m_index.emplace(m_frames.back().id, index);
            added = true;
        }

        if (m_batchDepth == 0)
        {
            emit_added = true;
        }
        else
        {
            markPendingReset();
        }
    }

    if (emit_added)
    {
        emit framesAdded();
    }

    return added;
}

void FrameCollection::addFrames(const std::vector<FrameInfo>& frames)
{
    if (frames.empty())
    {
        return;
    }

    bool any_changed = false;
    bool emit_added = false;

    {
        std::unique_lock lock(m_mutex);
        m_frames.reserve(m_frames.size() + frames.size());

        for (const auto& frame : frames)
        {
            auto it = m_index.find(frame.id);
            if (it != m_index.end())
            {
                m_frames[it->second] = frame;
                any_changed = true;
            }
            else
            {
                const size_t index = m_frames.size();
                m_frames.push_back(frame);
                m_index.emplace(m_frames.back().id, index);
                any_changed = true;
            }
        }

        if (!any_changed)
        {
            return;
        }

        if (m_batchDepth == 0)
        {
            emit_added = true;
        }
        else
        {
            markPendingReset();
        }
    }

    if (emit_added)
    {
        emit framesAdded();
    }
}

bool FrameCollection::removeFrame(const FrameId& id)
{
    bool removed = false;
    bool emit_removed = false;

    {
        std::unique_lock lock(m_mutex);
        auto it = m_index.find(id);
        if (it == m_index.end())
        {
            return false;
        }

        const size_t index = it->second;
        const size_t last_index = m_frames.size() - 1;
        if (index != last_index)
        {
            m_frames[index] = std::move(m_frames[last_index]);
            m_index[m_frames[index].id] = index;
        }

        m_frames.pop_back();
        m_index.erase(it);
        removed = true;

        if (m_batchDepth == 0)
        {
            emit_removed = true;
        }
        else
        {
            markPendingReset();
        }
    }

    if (emit_removed)
    {
        emit framesRemoved();
    }

    return removed;
}

size_t FrameCollection::removeFrames(const std::vector<FrameId>& ids)
{
    bool started_batch = false;
    {
        std::unique_lock lock(m_mutex);
        if (m_batchDepth == 0)
        {
            ++m_batchDepth;
            started_batch = true;
        }
    }

    size_t removed = 0;
    for (const auto& id : ids)
    {
        if (removeFrame(id))
        {
            ++removed;
        }
    }

    if (started_batch)
    {
        endBatchUpdate();
    }

    return removed;
}

bool FrameCollection::updateFrame(const FrameId& id, const std::function<void(FrameInfo&)>& updater)
{
    std::unique_lock lock(m_mutex);
    auto it = m_index.find(id);
    if (it == m_index.end())
    {
        return false;
    }

    updater(m_frames[it->second]);
    return true;
}

void FrameCollection::clear()
{
    bool emit_cleared = false;

    {
        std::unique_lock lock(m_mutex);
        if (m_frames.empty())
        {
            return;
        }

        m_frames.clear();
        m_index.clear();

        if (m_batchDepth == 0)
        {
            emit_cleared = true;
        }
        else
        {
            markPendingReset();
        }
    }

    if (emit_cleared)
    {
        emit collectionCleared();
    }
}

void FrameCollection::beginBatchUpdate()
{
    std::unique_lock lock(m_mutex);
    ++m_batchDepth;
}

void FrameCollection::endBatchUpdate()
{
    bool emit_reset = false;

    {
        std::unique_lock lock(m_mutex);
        if (m_batchDepth == 0)
        {
            return;
        }

        --m_batchDepth;
        if (m_batchDepth == 0 && m_pendingReset)
        {
            m_pendingReset = false;
            emit_reset = true;
        }
    }

    if (emit_reset)
    {
        emit collectionReset();
    }
}

size_t FrameCollection::size() const
{
    std::shared_lock lock(m_mutex);
    return m_frames.size();
}

bool FrameCollection::empty() const
{
    std::shared_lock lock(m_mutex);
    return m_frames.empty();
}

std::vector<FrameInfo> FrameCollection::frames() const
{
    std::shared_lock lock(m_mutex);
    return m_frames;
}

std::optional<FrameInfo> FrameCollection::findById(const FrameId& id) const
{
    std::shared_lock lock(m_mutex);
    auto it = m_index.find(id);
    if (it == m_index.end())
    {
        return std::nullopt;
    }

    return m_frames[it->second];
}

std::vector<FrameInfo> FrameCollection::filterByType(FrameType type) const
{
    std::vector<FrameInfo> results;
    std::shared_lock lock(m_mutex);

    for (const auto& frame : m_frames)
    {
        if (frame.type() == type)
        {
            results.push_back(frame);
        }
    }

    return results;
}

std::vector<FrameInfo> FrameCollection::filterByGroup(const GroupKey& key, FrameType type,
                                                      const GroupingOptions& options) const
{
    std::vector<FrameInfo> results;
    std::shared_lock lock(m_mutex);

    for (const auto& frame : m_frames)
    {
        if (frame.type() != type)
        {
            continue;
        }

        GroupKey frame_key = resolveGroupKey(frame, type, options);
        if (frame_key == key)
        {
            results.push_back(frame);
        }
    }

    return results;
}

std::vector<FrameInfo> FrameCollection::sortedByPath() const
{
    std::vector<FrameInfo> results = frames();
    std::sort(results.begin(), results.end(),
              [](const FrameInfo& lhs, const FrameInfo& rhs) { return lhs.displayPath < rhs.displayPath; });
    return results;
}

std::vector<FrameInfo> FrameCollection::sortedByDate() const
{
    std::vector<FrameInfo> results = frames();
    std::sort(results.begin(), results.end(), [](const FrameInfo& lhs, const FrameInfo& rhs) {
        return lhs.metadata.capturedAtUtc() < rhs.metadata.capturedAtUtc();
    });
    return results;
}

std::vector<FrameInfo> FrameCollection::sortedByExposure() const
{
    std::vector<FrameInfo> results = frames();
    std::sort(results.begin(), results.end(), [](const FrameInfo& lhs, const FrameInfo& rhs) {
        return lhs.metadata.exposureSeconds() < rhs.metadata.exposureSeconds();
    });
    return results;
}

std::map<FrameType, size_t> FrameCollection::countsByType() const
{
    std::map<FrameType, size_t> counts{
        {FrameType::Bias, 0u},
        {FrameType::Dark, 0u},
        {FrameType::Flat, 0u},
        {FrameType::Light, 0u},
    };

    std::shared_lock lock(m_mutex);
    for (const auto& frame : m_frames)
    {
        ++counts[frame.type()];
    }

    return counts;
}

GroupKey FrameCollection::resolveGroupKey(const FrameInfo& frame, FrameType type, const GroupingOptions& options) const
{
    if (frame.manualGroupKey.has_value())
    {
        return frame.manualGroupKey.value();
    }

    GroupingEngine engine;
    return engine.computeGroupKey(frame, type, options);
}

void FrameCollection::markPendingReset()
{
    m_pendingReset = true;
}

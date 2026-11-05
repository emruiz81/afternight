// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GroupingEngine.h"

#include <algorithm>
#include <ranges>
#include <unordered_map>
#include <vector>

namespace
{
GroupKey buildKeyForFrameType(const FrameInfo& frame, FrameType type, const GroupingOptions& options)
{
    switch (type)
    {
    case FrameType::Bias:
        return BiasGroupKey::fromFrame(frame).toGroupKey(options);
    case FrameType::Dark:
        return DarkGroupKey::fromFrame(frame).toGroupKey(options);
    case FrameType::Flat:
        return FlatGroupKey::fromFrame(frame).toGroupKey(options);
    case FrameType::Light:
        return LightGroupKey::fromFrame(frame).toGroupKey(options);
    }

    return LightGroupKey::fromFrame(frame).toGroupKey(options);
}
} // namespace

std::vector<GroupedFrameSet> GroupingEngine::groupFrames(const std::vector<FrameInfo>& frames, FrameType type,
                                                         const GroupingOptions& options) const
{
    std::unordered_map<GroupKey, std::vector<FrameInfo>> grouped_frames;
    grouped_frames.reserve(frames.size());

    for (const auto& frame : frames)
    {
        if (frame.excluded)
        {
            continue;
        }

        if (frame.type() != type)
        {
            continue;
        }

        GroupKey key = frame.manualGroupKey.has_value() ? frame.manualGroupKey.value()
                                                        : buildKeyForFrameType(frame, type, options);

        auto [it, inserted] = grouped_frames.try_emplace(std::move(key));
        it->second.push_back(frame);
    }

    std::vector<GroupKey> keys;
    keys.reserve(grouped_frames.size());
    for (const auto& key : grouped_frames | std::views::keys)
    {
        keys.push_back(key);
    }
    std::ranges::sort(keys, [](const GroupKey& lhs, const GroupKey& rhs) { return lhs < rhs; });

    std::vector<GroupedFrameSet> result;
    result.reserve(grouped_frames.size());

    for (const auto& key : keys)
    {
        auto it = grouped_frames.find(key);
        if (it == grouped_frames.end())
        {
            continue;
        }

        auto& group_frames = it->second;
        std::sort(group_frames.begin(), group_frames.end(),
                  [](const FrameInfo& lhs, const FrameInfo& rhs) { return lhs.id < rhs.id; });

        result.push_back(GroupedFrameSet{key, std::move(group_frames)});
    }

    return result;
}

GroupKey GroupingEngine::computeGroupKey(const FrameInfo& frame, FrameType type, const GroupingOptions& options) const
{
    return buildKeyForFrameType(frame, type, options);
}

bool GroupingEngine::checkCompatibilityForMove(const FrameInfo& frame, const GroupKey& targetKey, FrameType type,
                                               const GroupingOptions& options) const
{
    QString targetObject, targetSession;
    GroupKey::parseMetadata(targetKey, type, options, &targetObject, &targetSession);

    // Create a modified FrameInfo for compatibility check.
    // We only override fields that are allowed to change (Object and Session).
    FrameInfo modified = frame;
    if (options.byObject)
    {
        modified.userObjectName = targetObject;
    }
    if (options.bySession)
    {
        modified.userSession = targetSession;
    }

    // If the computed key for the modified frame matches the target key,
    // it means all other hardware-related fields (resolution, filter, exposure, etc.)
    // are compatible.
    return computeGroupKey(modified, type, options) == targetKey;
}

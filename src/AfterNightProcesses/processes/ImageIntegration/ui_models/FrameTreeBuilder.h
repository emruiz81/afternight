// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/model/GroupingEngine.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"
#include "processes/ImageIntegration/model/FrameType.h"

#include <memory>
#include <vector>
#include <QString>

struct TreeDisplayNode
{
    enum class Type
    {
        Group,
        Frame
    };
    Type type;

    // For groups
    QString label;
    // The group key of this node (if it's a leaf group) or empty/partial
    // If it's a hierarchical node, it might not have a full GroupKey unless it's the leaf group.
    // However, for stable IDs, we might need to know the path.
    QString keyPath;

    // GroupKey is used for the actual GroupedFrameSet matching.
    std::optional<GroupKey> fullGroupKey;

    int frameCount = 0;

    // For frames (only valid when type == Frame)
    std::optional<FrameInfo> frameInfo;

    // Hierarchy
    TreeDisplayNode* parent = nullptr;
    std::vector<std::unique_ptr<TreeDisplayNode>> children;

    explicit TreeDisplayNode(Type t) : type(t) {}
};

class FrameTreeBuilder
{
public:
    enum class DisplayMode
    {
        Flat,
        Hierarchical
    };

    [[nodiscard]] static std::unique_ptr<TreeDisplayNode> buildTree(const std::vector<GroupedFrameSet>& groups,
                                                                    const GroupingOptions& options, DisplayMode mode,
                                                                    FrameType frameType);
};

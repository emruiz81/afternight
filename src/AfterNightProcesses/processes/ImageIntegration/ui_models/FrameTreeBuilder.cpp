// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameTreeBuilder.h"

#include <algorithm>

namespace
{

QString formatValue(const QString& val)
{
    return val.isEmpty() ? QStringLiteral("-") : val;
}

// Helper to find or create a child group node.
// Uses a stable keyPath (not the display label) to avoid breaking persisted IDs
// if display formatting changes.
TreeDisplayNode* findOrCreateChild(TreeDisplayNode* parent, const QString& label, const QString& pathComponent)
{
    for (auto& child : parent->children)
    {
        if (child->type == TreeDisplayNode::Type::Group && child->keyPath == pathComponent)
        {
            return child.get();
        }
    }

    auto newNode = std::make_unique<TreeDisplayNode>(TreeDisplayNode::Type::Group);
    newNode->type = TreeDisplayNode::Type::Group;
    newNode->label = label;
    newNode->keyPath = pathComponent;
    newNode->parent = parent;

    TreeDisplayNode* rawPtr = newNode.get();
    parent->children.push_back(std::move(newNode));
    return rawPtr;
}

} // namespace

std::unique_ptr<TreeDisplayNode> FrameTreeBuilder::buildTree(const std::vector<GroupedFrameSet>& groups,
                                                             const GroupingOptions& options, DisplayMode mode,
                                                             FrameType frameType)
{
    auto root = std::make_unique<TreeDisplayNode>(TreeDisplayNode::Type::Group);
    root->label = QStringLiteral("Root");

    if (mode == DisplayMode::Flat)
    {
        for (const auto& group : groups)
        {
            auto groupNode = std::make_unique<TreeDisplayNode>(TreeDisplayNode::Type::Group);
            // Use display-friendly label that only shows enabled grouping fields
            groupNode->label = group.key.toDisplayString(options, frameType);
            // Use canonical key string for stable IDs.
            groupNode->keyPath = group.key.toString();
            groupNode->fullGroupKey = group.key;
            groupNode->frameCount = static_cast<int>(group.frames.size());
            groupNode->parent = root.get();

            for (const auto& frame : group.frames)
            {
                auto frameNode = std::make_unique<TreeDisplayNode>(TreeDisplayNode::Type::Frame);
                frameNode->frameInfo = frame;
                frameNode->parent = groupNode.get();
                groupNode->children.push_back(std::move(frameNode));
            }
            root->children.push_back(std::move(groupNode));
        }
    }
    else
    {
        // Hierarchical
        for (const auto& group : groups)
        {
            if (group.frames.empty())
                continue;
            const auto& firstFrame = group.frames.front();
            const auto& meta = firstFrame.metadata;

            TreeDisplayNode* current = root.get();

            // 1. Resolution
            if (options.byResolution)
            {
                current = findOrCreateChild(current, QStringLiteral("Resolution: %1").arg(meta.resolutionString()),
                                            QStringLiteral("res=%1").arg(meta.resolutionString()));
            }

            // 2. Filter
            if (options.byFilter)
            {
                const QString val = formatValue(meta.filter());
                current = findOrCreateChild(current, QStringLiteral("Filter: %1").arg(val),
                                            QStringLiteral("filter=%1").arg(val));
            }

            // 3. Object
            if (options.byObject)
            {
                const QString val = formatValue(firstFrame.effectiveObjectName());
                current = findOrCreateChild(current, QStringLiteral("Object: %1").arg(val),
                                            QStringLiteral("object=%1").arg(val));
            }

            // 4. Session
            if (options.bySession)
            {
                const QString val = formatValue(firstFrame.effectiveSession());
                current = findOrCreateChild(current, QStringLiteral("Session: %1").arg(val),
                                            QStringLiteral("session=%1").arg(val));
            }

            // 5. ISO/Gain
            if (options.byIsoGain)
            {
                QString val =
                    meta.isoGain().has_value() ? QString::number(meta.isoGain().value()) : QStringLiteral("-");
                current =
                    findOrCreateChild(current, QStringLiteral("ISO: %1").arg(val), QStringLiteral("iso=%1").arg(val));
            }

            // 6. Exposure
            if (options.byExposure)
            {
                QString val = QString::number(meta.exposureSeconds(), 'f', 1) + QStringLiteral("s");
                current = findOrCreateChild(current, QStringLiteral("Exposure: %1").arg(val),
                                            QStringLiteral("exp=%1").arg(val));
            }

            // 7. Custom Tag
            if (options.byCustomTag)
            {
                const QString val = formatValue(firstFrame.customTag);
                current =
                    findOrCreateChild(current, QStringLiteral("Tag: %1").arg(val), QStringLiteral("tag=%1").arg(val));
            }

            // Associate the leaf node with the group key
            if (!current->fullGroupKey.has_value())
            {
                current->fullGroupKey = group.key;
            }

            // Leaf node should have canonical keyPath for stable IDs.
            if (current->keyPath.isEmpty())
            {
                current->keyPath = group.key.toString();
            }

            current->frameCount += static_cast<int>(group.frames.size());

            for (const auto& frame : group.frames)
            {
                auto frameNode = std::make_unique<TreeDisplayNode>(TreeDisplayNode::Type::Frame);
                frameNode->frameInfo = frame;
                frameNode->parent = current;
                current->children.push_back(std::move(frameNode));
            }
        }
    }

    return root;
}

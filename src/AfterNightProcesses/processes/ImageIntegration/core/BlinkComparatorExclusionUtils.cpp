// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"

#include "processes/ImageIntegration/model/FrameId.h"

#include <QSet>
#include <QUndoStack>

namespace ImageIntegration::blink
{

std::vector<FrameAttributeChange> buildExclusionChanges(const IntegrationDataModel* model, const FrameType frameType,
                                                        const QStringList& excludedPaths)
{
    std::vector<FrameAttributeChange> changes;
    if (!model || excludedPaths.isEmpty())
    {
        return changes;
    }

    QSet<QString> seenPaths;
    changes.reserve(excludedPaths.size());

    for (const auto& path : excludedPaths)
    {
        if (path.trimmed().isEmpty())
        {
            continue;
        }

        // Construct FrameId once; reuse it for both dedup and model lookup to
        // avoid a second canonicalization inside findById.
        const FrameId frameId(path);
        const QString canonicalPath = frameId.canonicalPath();
        if (canonicalPath.isEmpty() || seenPaths.contains(canonicalPath))
        {
            continue;
        }

        seenPaths.insert(canonicalPath);

        const auto frameOpt = model->frames(frameType).findById(frameId);
        if (!frameOpt.has_value() || frameOpt->excluded)
        {
            continue;
        }

        changes.push_back(FrameAttributeChange{canonicalPath, frameOpt->excluded, true});
    }

    return changes;
}

bool applyExclusions(IntegrationDataModel* model, QUndoStack* undoStack, const FrameType frameType,
                     const QStringList& excludedPaths)
{
    if (!model)
    {
        return false;
    }

    auto changes = buildExclusionChanges(model, frameType, excludedPaths);
    if (changes.empty())
    {
        return false;
    }

    if (undoStack)
    {
        undoStack->push(new SetFrameAttributesCommand(model, FrameAttribute::Excluded, std::move(changes)));
        return true;
    }

    // No undo stack (e.g. unit tests or non-undoable contexts). Use a batch
    // update so observers receive a single coalesced notification. The scope
    // guard ensures endBatchUpdate is called even if an exception is thrown.
    struct BatchGuard
    {
        IntegrationDataModel* m;
        explicit BatchGuard(IntegrationDataModel* model) : m(model)
        {
            m->beginBatchUpdate();
        }
        ~BatchGuard()
        {
            m->endBatchUpdate();
        }
    } guard(model);

    for (const auto& change : changes)
    {
        model->updateFrameAttribute(change.path, FrameAttribute::Excluded, change.newValue);
    }
    return true;
}

} // namespace ImageIntegration::blink

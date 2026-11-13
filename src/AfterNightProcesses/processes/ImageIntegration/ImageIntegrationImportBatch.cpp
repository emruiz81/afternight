// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageIntegrationImportBatch.cpp
 * @brief Turning detected frames into a model change: grouping them by type and applying them.
 *
 * The import dialogs, the frame tree's drag and drop and a measurement driver all add detected frames the same way,
 * so the batch lives apart from the dialogs that drive an import.
 */
#include "stdafx.hpp"
#include "processes/ImageIntegration/ImageIntegrationImportSession.h"

#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/UndoCommands.h"

#include <utility>

namespace ImageIntegration::detail
{

FrameMetadata remapMetadataType(const FrameMetadata& meta, FrameType type)
{
    return FrameMetadata(type, meta.dimensions(), meta.exposureSeconds(), meta.temperatureC(), meta.isoGain(),
                         meta.offset(), meta.filter(), meta.objectName(), meta.capturedAtUtc(), meta.readoutMode(),
                         meta.sampleLayout());
}

FrameInfo remapFrameInfoType(const FrameInfo& info, FrameType type)
{
    if (info.metadata.type() == type)
    {
        return info;
    }

    FrameMetadata metadata = remapMetadataType(info.metadata, type);
    return FrameInfo(info.id, info.displayPath, metadata, info.userSession, info.customTag, info.excluded,
                     info.manualGroupKey, info.userObjectName);
}

[[nodiscard]] PreparedImportBatch prepareImportBatch(std::vector<ImageIntegration::DetectedFrameInfo> detectedFrames,
                                                     FrameType requestedType, bool forceToType)
{
    PreparedImportBatch batch;
    ImageIntegration::sortDetectedFramesForImport(detectedFrames);
    for (auto& detected : detectedFrames)
    {
        FrameInfo info = std::move(detected.info);
        const FrameType finalType = forceToType ? requestedType : detected.type;
        if (forceToType)
        {
            info = remapFrameInfoType(info, finalType);
        }

        if ((finalType == FrameType::Bias || finalType == FrameType::Dark || finalType == FrameType::Flat) &&
            ImageIntegration::isProbablyMasterFilePath(info.displayPath))
        {
            batch.autoMasterCandidates.push_back(info.displayPath);
        }
        batch.groupedInfos[finalType].push_back(std::move(info));
    }

    return batch;
}

void applyImportBatch(IntegrationDataModel* model, QUndoStack* undoStack, const PreparedImportBatch& batch,
                      const QString& commandText)
{
    if (!model || batch.groupedInfos.empty())
    {
        return;
    }

    if (undoStack)
    {
        auto* macro = new QUndoCommand(commandText);
        for (auto& [groupType, infos] : batch.groupedInfos)
        {
            if (!infos.empty())
            {
                new AddFramesCommand(model, groupType, infos, macro);
            }
        }

        for (const auto& path : batch.autoMasterCandidates)
        {
            const FrameId id(path);
            const bool oldIsMaster = model->isMasterFrame(id);
            const MasterSource oldSource = model->masterSource(id).value_or(MasterSource::Unknown);
            new SetMasterStatusCommand(model, id.canonicalPath(), oldIsMaster, oldSource, true,
                                       MasterSource::AutoDetected, macro);
        }

        if (macro->childCount() > 0)
        {
            undoStack->push(macro);
        }
        else
        {
            delete macro;
        }

        return;
    }

    model->beginBatchUpdate();
    for (auto& [groupType, infos] : batch.groupedInfos)
    {
        model->addFrames(groupType, infos);
    }

    for (const auto& path : batch.autoMasterCandidates)
    {
        model->setMasterStatus(path, true, MasterSource::AutoDetected);
    }
    model->endBatchUpdate();
}

} // namespace ImageIntegration::detail

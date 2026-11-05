// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The maps a lights stack writes beside its master - the rejection maps and the quality maps - where they are named,
/// and whether a map on disk belongs to the master beside it.
///
/// **A map belongs to its master by identity, not by time.** A map is not current because its file was modified no more
/// than two seconds before the master's: a map left from another run that happened to be touched, or a master restored
/// from a backup, would decide the answer. Every map the stack writes is recorded in the output directory's artifact
/// records under the master's own fingerprint, after the master's record; a map is current when both records exist,
/// name the same fingerprint, and both files are still the ones recorded - the artifact tracker's identity check, the
/// rule every other artifact of the workflow is reused by.

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"

#include <QFileInfo>
#include <QString>
#include <QVector>

#include <algorithm>
#include <vector>

namespace ImageIntegration
{
struct StackingAuxiliaryOutputSpec
{
    QString filename;
    QString title;
};

using RejectionMapSpec = StackingAuxiliaryOutputSpec;

enum class StackingAuxiliaryOutputKind
{
    RejectionMerged,
    RejectionLow,
    RejectionHigh,
    Coverage,
    Variance,
    SnrGain,
    WeightSum,
};

[[nodiscard]] inline StackingAuxiliaryOutputSpec stackingAuxiliaryOutputForOutput(const QFileInfo& outInfo,
                                                                                  StackingAuxiliaryOutputKind kind)
{
    const QString baseName = outInfo.completeBaseName();
    const QString suffix = outInfo.suffix();

    switch (kind)
    {
    case StackingAuxiliaryOutputKind::RejectionMerged:
        return {QStringLiteral("%1_rejection.%2").arg(baseName, suffix), QStringLiteral("Rejection Map")};
    case StackingAuxiliaryOutputKind::RejectionLow:
        return {QStringLiteral("%1_rejection_low.%2").arg(baseName, suffix), QStringLiteral("Rejection Map (Low)")};
    case StackingAuxiliaryOutputKind::RejectionHigh:
        return {QStringLiteral("%1_rejection_high.%2").arg(baseName, suffix), QStringLiteral("Rejection Map (High)")};
    case StackingAuxiliaryOutputKind::Coverage:
        return {QStringLiteral("%1_coverage.%2").arg(baseName, suffix), QStringLiteral("Coverage Map")};
    case StackingAuxiliaryOutputKind::Variance:
        return {QStringLiteral("%1_variance.%2").arg(baseName, suffix), QStringLiteral("Variance Map")};
    case StackingAuxiliaryOutputKind::SnrGain:
        return {QStringLiteral("%1_snrgain.%2").arg(baseName, suffix), QStringLiteral("SNR Gain Map")};
    case StackingAuxiliaryOutputKind::WeightSum:
        return {QStringLiteral("%1_weightsum.%2").arg(baseName, suffix), QStringLiteral("Weight Sum Map")};
    }

    return {};
}

[[nodiscard]] inline QVector<RejectionMapSpec> rejectionMapsForOutput(const QFileInfo& outInfo, bool mergeRejectionMaps)
{
    if (mergeRejectionMaps)
    {
        return {stackingAuxiliaryOutputForOutput(outInfo, StackingAuxiliaryOutputKind::RejectionMerged)};
    }

    return {
        stackingAuxiliaryOutputForOutput(outInfo, StackingAuxiliaryOutputKind::RejectionLow),
        stackingAuxiliaryOutputForOutput(outInfo, StackingAuxiliaryOutputKind::RejectionHigh),
    };
}

[[nodiscard]] inline QVector<StackingAuxiliaryOutputSpec> qualityMapsForOutput(const QFileInfo& outInfo,
                                                                               bool includeWeightSum)
{
    QVector<StackingAuxiliaryOutputSpec> specs{
        stackingAuxiliaryOutputForOutput(outInfo, StackingAuxiliaryOutputKind::Coverage),
        stackingAuxiliaryOutputForOutput(outInfo, StackingAuxiliaryOutputKind::Variance),
        stackingAuxiliaryOutputForOutput(outInfo, StackingAuxiliaryOutputKind::SnrGain),
    };

    if (includeWeightSum)
    {
        specs.push_back(stackingAuxiliaryOutputForOutput(outInfo, StackingAuxiliaryOutputKind::WeightSum));
    }

    return specs;
}

/// Whether a map beside a stacked master belongs to it: both are recorded in the output directory's artifact records
/// under one fingerprint, and both files are still the ones recorded.
///
/// @param tracker the artifact records of the master's output directory.
/// @param outputPath the master.
/// @param auxiliaryPath the map.
/// @return true when the map is the master's.
[[nodiscard]] inline bool isStackingAuxiliaryOutputCurrent(const ArtifactTracker& tracker, const QString& outputPath,
                                                           const QString& auxiliaryPath)
{
    const QString output = QFileInfo(outputPath).absoluteFilePath();
    const QString auxiliary = QFileInfo(auxiliaryPath).absoluteFilePath();
    if (!QFileInfo::exists(output) || !QFileInfo::exists(auxiliary))
    {
        return false;
    }
    const std::vector<ArtifactRecordSnapshot> records = tracker.artifactRecords();
    const auto recordOf = [&records](const QString& path) -> const ArtifactRecordSnapshot* {
        const auto found = std::find_if(records.begin(), records.end(), [&path](const ArtifactRecordSnapshot& record) {
            return QFileInfo(record.path).absoluteFilePath() == path;
        });
        return found == records.end() ? nullptr : &*found;
    };
    const ArtifactRecordSnapshot* master = recordOf(output);
    const ArtifactRecordSnapshot* map = recordOf(auxiliary);
    if (master == nullptr || map == nullptr || master->manifestHash.isEmpty() ||
        master->manifestHash != map->manifestHash)
    {
        return false;
    }
    return tracker.isArtifactValid(output, master->manifestHash) &&
           tracker.isArtifactValid(auxiliary, master->manifestHash);
}

} // namespace ImageIntegration

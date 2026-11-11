// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include "app/PlateSolveTypes.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

/**
 * @file MasterFacts.cpp
 * @brief The masters a run planned: what became of each, its file, what one read of it measured, what its stack
 * recorded, its maps and its pictures.
 */

namespace ImageIntegration::Reporting
{
[[nodiscard]] RejectionClusterMapKind rejectionMapKindForTitle(const QString& title)
{
    const QString normalized = title.trimmed().toLower();
    if (normalized.contains(QStringLiteral("low")))
    {
        return RejectionClusterMapKind::Low;
    }
    if (normalized.contains(QStringLiteral("high")))
    {
        return RejectionClusterMapKind::High;
    }
    return RejectionClusterMapKind::Merged;
}

[[nodiscard]] QString resolvePathAgainstBase(const QString& candidatePath, const QString& baseDir)
{
    const QString trimmed = candidatePath.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const QFileInfo info(trimmed);
    if (info.isAbsolute() || baseDir.trimmed().isEmpty())
    {
        return trimmed;
    }

    return QDir(baseDir).filePath(trimmed);
}

[[nodiscard]] std::optional<QPair<int, int>> parseResolutionDimensions(const QString& resolution)
{
    const QString trimmed = resolution.trimmed();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }

    static const QRegularExpression resRx(QStringLiteral("^(\\d+)\\s*[xX]\\s*(\\d+)$"));
    const auto match = resRx.match(trimmed);
    if (!match.hasMatch())
    {
        return std::nullopt;
    }

    bool okW = false;
    bool okH = false;
    const int width = match.captured(1).toInt(&okW);
    const int height = match.captured(2).toInt(&okH);
    if (!okW || !okH || width <= 0 || height <= 0)
    {
        return std::nullopt;
    }

    return QPair<int, int>(width, height);
}

std::optional<double> overlapRetainedPercent(const QString& captureResolution, const std::size_t width,
                                             const std::size_t height)
{
    const auto sourceDims = parseResolutionDimensions(captureResolution);
    if (!sourceDims.has_value())
    {
        return std::nullopt;
    }
    const double sourceArea = static_cast<double>(sourceDims->first) * static_cast<double>(sourceDims->second);
    const double outputArea = static_cast<double>(width) * static_cast<double>(height);
    if (!(sourceArea > 0.0) || !(outputArea > 0.0))
    {
        return std::nullopt;
    }
    return std::clamp(outputArea * 100.0 / sourceArea, 0.0, 100.0);
}

namespace
{
[[nodiscard]] ReportRetention retentionOf(const OutputRetentionStatus status)
{
    switch (status)
    {
    case OutputRetentionStatus::RetainedInHistory:
        return ReportRetention::Retained;
    case OutputRetentionStatus::NotRetainedByPolicy:
        return ReportRetention::NotRetainedByPolicy;
    case OutputRetentionStatus::NotRetainedByArchiveFailure:
        return ReportRetention::NotRetainedByArchiveFailure;
    case OutputRetentionStatus::Unknown:
        break;
    }
    return ReportRetention::Unknown;
}

[[nodiscard]] ReportAstrometryOutcome astrometryOf(const MasterSolutionOutcome outcome)
{
    switch (outcome)
    {
    case MasterSolutionOutcome::Propagated:
        return ReportAstrometryOutcome::Inherited;
    case MasterSolutionOutcome::Solved:
        return ReportAstrometryOutcome::Solved;
    case MasterSolutionOutcome::None:
        return ReportAstrometryOutcome::None;
    case MasterSolutionOutcome::NotRecorded:
        break;
    }
    return ReportAstrometryOutcome::NotRecorded;
}

[[nodiscard]] ReportClusterMap clusterMapOf(const RejectionClusterMapKind kind)
{
    switch (kind)
    {
    case RejectionClusterMapKind::Low:
        return ReportClusterMap::Low;
    case RejectionClusterMapKind::High:
        return ReportClusterMap::High;
    case RejectionClusterMapKind::Merged:
        break;
    }
    return ReportClusterMap::Merged;
}

/// The folder each group's pictures are archived in, by the group's key: the run's results first, then the groups
/// it reused, named as the archive names the folders it writes.
[[nodiscard]] QHash<QString, QString> previewFolders(const WorkflowExecutor::Result& result)
{
    std::vector<QString> keys;
    keys.reserve(result.integrationGroupResults.size() + result.reusedIntegrationGroups.size());
    for (const IntegrationGroupResult& group : result.integrationGroupResults)
    {
        keys.push_back(group.groupKey);
    }
    for (const ReusedIntegrationGroup& reused : result.reusedIntegrationGroups)
    {
        if (std::find(keys.begin(), keys.end(), reused.groupKey) == keys.end())
        {
            keys.push_back(reused.groupKey);
        }
    }
    const std::vector<QString> names = ImageIntegration::archiveGroupFolderNames(keys);
    QHash<QString, QString> folders;
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        // Two results of one key share the first one's folder, as the archive's records of them do.
        if (!folders.contains(keys[i]))
        {
            folders.insert(keys[i], names[i]);
        }
    }
    return folders;
}

/// A master's two pictures in the run's folder, each stated where the archive holds it.
void fillPictures(const ReportBuild& build, ReportMaster& master)
{
    const QString whole = QStringLiteral("report_previews/%1/master.jpg").arg(master.previewFolder);
    if (build.exists(whole))
    {
        master.preview = whole;
    }
    const QString detail = QStringLiteral("report_previews/%1/detail.jpg").arg(master.previewFolder);
    if (build.exists(detail))
    {
        master.detail = detail;
    }
}

/// The calibration masters a group's lights take, by file name, each kind's names in order.
[[nodiscard]] ReportMasterCalibration
calibrationOf(const WorkflowManifest::IntegrationGroup& group,
              const QHash<QString, const WorkflowManifest::LightsCalibrationParams*>& calibrationByLight)
{
    QSet<QString> bias;
    QSet<QString> dark;
    QSet<QString> flat;
    for (const FrameId& light : group.lightFiles)
    {
        const auto found = calibrationByLight.constFind(light.canonicalPath());
        if (found == calibrationByLight.constEnd())
        {
            continue;
        }
        const WorkflowManifest::LightsCalibrationParams& calibration = **found;
        if (calibration.biasPath.has_value())
        {
            bias.insert(QFileInfo(calibration.biasPath->canonicalPath()).fileName());
        }
        if (calibration.darkPath.has_value())
        {
            dark.insert(QFileInfo(calibration.darkPath->canonicalPath()).fileName());
        }
        if (calibration.flatPath.has_value())
        {
            flat.insert(QFileInfo(calibration.flatPath->canonicalPath()).fileName());
        }
    }
    const auto sorted = [](const QSet<QString>& names) {
        QStringList list = names.values();
        list.sort(Qt::CaseInsensitive);
        return list;
    };
    return ReportMasterCalibration{.bias = sorted(bias), .dark = sorted(dark), .flat = sorted(flat)};
}

[[nodiscard]] ReportStack stackOf(const StackQualityRecord& quality, const int lightFrameCount,
                                  const WorkflowManifest::IntegrationGroup* definition)
{
    ReportStack stack;
    stack.inputFrames = quality.inputFrameCount > 0 ? quality.inputFrameCount
                                                    : static_cast<std::uint64_t>((std::max)(lightFrameCount, 0));
    stack.effectiveFrames = quality.effectiveFrameCount;
    stack.measuredSnrGain = quality.actualSnrGain;
    stack.theoreticalSnrGain = quality.theoreticalSnrGain;
    stack.efficiency = quality.integrationEfficiency;
    stack.lowestCoverage = quality.minCoverage;
    stack.medianCoverage = quality.medianCoverage;
    stack.highestCoverage = quality.maxCoverage;
    stack.coverageDeviation = quality.coverageStddev;
    stack.normalization = quality.normalization;

    if (quality.hasRejectionStats)
    {
        stack.rejection = ReportStackRejection{.global = quality.globalRejectionRate,
                                               .lowestFrame = quality.minFrameRejectionRate,
                                               .meanFrame = quality.meanFrameRejectionRate,
                                               .medianFrame = quality.medianFrameRejectionRate,
                                               .highestFrame = quality.maxFrameRejectionRate};
    }
    if (quality.hasWeightingStats)
    {
        stack.weights = ReportStackWeights{.lowest = quality.weightMin,
                                           .highest = quality.weightMax,
                                           .deviation = quality.weightStddev,
                                           .efficiency = quality.weightEfficiency};
    }
    if (quality.hasLocalWeightingStats)
    {
        stack.localWeights = ReportLocalWeights{.windowSize = quality.localWeightWindowSize,
                                                .gridSpacing = quality.localWeightGridSpacing,
                                                .snrExponent = quality.localWeightSnrExponent,
                                                .grids = quality.localWeightGridCount,
                                                .lowest = quality.localWeightMin,
                                                .highest = quality.localWeightMax,
                                                .mean = quality.localWeightMean,
                                                .deviation = quality.localWeightStddev};
    }
    if (quality.hasSpatialRejectionClusters)
    {
        ReportStackClusters clusters;
        clusters.count = quality.spatialClusterCount;
        clusters.pixels = quality.spatialClusterPixels;
        clusters.fraction = quality.spatialClusterFraction;
        clusters.largestPixels = quality.spatialLargestClusterPixels;
        clusters.largestFraction = quality.spatialLargestClusterFraction;
        clusters.rejectedElements = quality.spatialRejectedElements;
        clusters.rejectedElementFraction = quality.spatialRejectedElementFraction;
        clusters.clusters.reserve(quality.spatialClusters.size());
        for (const RejectionClusterRecord& cluster : quality.spatialClusters)
        {
            clusters.clusters.push_back(ReportRejectionCluster{.map = clusterMapOf(cluster.mapKind),
                                                               .channel = cluster.channel,
                                                               .pixels = cluster.pixelCount,
                                                               .minX = cluster.minX,
                                                               .minY = cluster.minY,
                                                               .maxX = cluster.maxX,
                                                               .maxY = cluster.maxY,
                                                               .centroidX = cluster.centroidX,
                                                               .centroidY = cluster.centroidY});
        }
        stack.clusters = std::move(clusters);
    }
    if (quality.spatialRestackRequested)
    {
        ReportRestack restack;
        restack.applied = quality.spatialRestackApplied;
        restack.flaggedFrameThreshold = quality.spatialRestackFlaggedFrameThreshold;
        restack.initialClusters = quality.spatialRestackInitialClusterCount;
        restack.excluded.reserve(quality.spatialRestackExcludedFrameIndices.size());
        for (std::size_t i = 0; i < quality.spatialRestackExcludedFrameIndices.size(); ++i)
        {
            // The indices are places among the group's lights, the one list the stack's own order does not follow.
            ReportRestackExclusion excluded;
            excluded.index = quality.spatialRestackExcludedFrameIndices[i];
            if (definition != nullptr && excluded.index < definition->lightFiles.size())
            {
                excluded.fileName = QFileInfo(definition->lightFiles[excluded.index].canonicalPath()).fileName();
            }
            if (i < quality.spatialRestackExcludedFrameRates.size())
            {
                excluded.rejectedFraction = quality.spatialRestackExcludedFrameRates[i];
            }
            restack.excluded.push_back(std::move(excluded));
        }
        stack.restack = std::move(restack);
    }
    return stack;
}

/// The archived picture of a map when it is there, else the picture the result names.
[[nodiscard]] QString mapPreview(const ReportBuild& build, const QStringList& archived, const QString& recorded)
{
    for (const QString& candidate : archived)
    {
        if (build.exists(candidate))
        {
            return candidate;
        }
    }
    return recorded.trimmed();
}

void fillMaps(const ReportBuild& build, const IntegrationGroupResult& group,
              const WorkflowManifest::IntegrationGroup* definition, ReportMaster& master)
{
    // The maps are the ones the result names; the quality maps only while the group's settings still ask for them.
    const bool qualityMapsRequested =
        definition != nullptr && ImageIntegration::sanitizeStackingSettings(definition->stacking).createQualityMaps;
    const std::vector<QualityMapResult> qualityMaps =
        (definition == nullptr || qualityMapsRequested) ? group.qualityMaps : std::vector<QualityMapResult>{};
    const std::vector<RejectionMapResult>& rejectionMaps = group.rejectionMaps;

    if (!rejectionMaps.empty())
    {
        master.rejectionMapsExpected = definition != nullptr && definition->stacking.createRejectionMaps
                                           ? (definition->stacking.mergeRejectionMaps ? 1 : 2)
                                           : static_cast<int>(rejectionMaps.size());
    }

    int index = 1;
    for (const RejectionMapResult& map : rejectionMaps)
    {
        ReportMap stated;
        stated.kind = ReportMapKind::Rejection;
        stated.title = map.title.trimmed().isEmpty() ? reportText("Rejection Map") : map.title;
        stated.file = map.path;
        stated.bytes = build.fileSize(map.path);
        stated.preview = mapPreview(
            build, {QStringLiteral("report_previews/%1/rejection_map_%2.jpg").arg(master.previewFolder).arg(index)},
            map.previewPath);
        if (group.qualityReport.hasSpatialRejectionClusters)
        {
            const RejectionClusterMapKind kind = rejectionMapKindForTitle(stated.title);
            stated.clusters = static_cast<int>(
                std::count_if(group.qualityReport.spatialClusters.begin(), group.qualityReport.spatialClusters.end(),
                              [kind](const RejectionClusterRecord& cluster) { return cluster.mapKind == kind; }));
        }
        master.maps.push_back(std::move(stated));
        ++index;
    }

    index = 1;
    bool weightSumFound = false;
    for (const QualityMapResult& map : qualityMaps)
    {
        ReportMap stated;
        stated.kind = ReportMapKind::Quality;
        stated.title = map.title.trimmed().isEmpty() ? reportText("Quality Map") : map.title;
        stated.file = map.path;
        stated.bytes = build.fileSize(map.path);
        stated.preview = mapPreview(
            build, {QStringLiteral("report_previews/%1/quality_map_%2.jpg").arg(master.previewFolder).arg(index)},
            map.previewPath);
        // The sum of the weights is the picture of a stack weighted per pixel: the first such map with a picture.
        if (group.qualityReport.hasLocalWeightingStats && !weightSumFound &&
            map.title.contains(QStringLiteral("Weight Sum"), Qt::CaseInsensitive))
        {
            weightSumFound = true;
            stated.weightSum = !stated.preview.isEmpty();
        }
        master.maps.push_back(std::move(stated));
        ++index;
    }
}

/// What the solution in a master's header states of the frame: nothing for a header that cannot be read or holds no
/// solution that reads.
void fillSolution(ReportBuild& build, const QString& file, ReportMaster& master)
{
    if (master.astrometry == ReportAstrometryOutcome::None)
    {
        return;
    }
    const std::optional<ReportHeader> header = build.header(file);
    if (!header.has_value() || header->width == 0u || header->height == 0u)
    {
        return;
    }
    const an::Result<an::WcsSolution> solution =
        an::ui::readImageWcsSolution(header->metadata.codec().cards(), static_cast<std::uint32_t>(header->width),
                                     static_cast<std::uint32_t>(header->height));
    if (!solution.hasValue())
    {
        return;
    }
    const an::Result<an::SkyCoordinate> centre = solution->fieldCenter();
    if (!centre.hasValue())
    {
        return;
    }
    const an::WcsFieldOfView field = solution->fieldOfViewArcmin();
    master.solution = ReportAstrometrySolution{.centreRightAscension = centre->rightAscensionDegrees,
                                               .centreDeclination = centre->declinationDegrees,
                                               .pixelScale = solution->meanPixelScaleArcsecPerPixel(),
                                               .fieldWidth = field.widthArcmin,
                                               .fieldHeight = field.heightArcmin,
                                               .orientation = solution->orientationDegrees(),
                                               .mirrored = solution->mirrored()};
}

void fillMeasurement(ReportBuild& build, const QString& file, ReportMaster& master)
{
    std::optional<ReportImageMeasurement> measured =
        build.measure(file, reportText("the master %1").arg(master.name.isEmpty() ? master.fileName : master.name));
    if (!measured.has_value())
    {
        return;
    }
    master.distribution = std::move(measured->distribution);
    const std::optional<ReportImageStatistics> statistics = std::move(measured->statistics);
    master.statistics = statistics;

    if (const std::optional<double> retained =
            overlapRetainedPercent(master.captureResolution, statistics->width, statistics->height);
        retained.has_value())
    {
        master.overlapRetained = *retained / 100.0;
    }

    const auto mean =
        averageChannelStat(statistics->channelStats, [](const RawChannelStats& channel) { return channel.mean; });
    const auto noise = averageChannelStat(statistics->channelStats,
                                          [](const RawChannelStats& channel) { return channel.backgroundNoise; });
    const auto lowest =
        averageChannelStat(statistics->channelStats, [](const RawChannelStats& channel) { return channel.minimum; });
    const auto highest =
        averageChannelStat(statistics->channelStats, [](const RawChannelStats& channel) { return channel.maximum; });
    master.snapshot.mean = mean;
    master.snapshot.backgroundNoise = noise;
    if (mean.has_value() && noise.has_value() && *noise > 0.0)
    {
        master.snapshot.snr = *mean / *noise;
    }
    if (lowest.has_value() && highest.has_value())
    {
        master.snapshot.dynamicRange = *highest - *lowest;
    }
}

/// A master the run reused: what the run that made it stated of it, when the folder's history keeps that run's
/// report; else what one read of the file measures. The figures of another run are marked by `madeByRun`.
void fillReused(ReportBuild& build, ReportMaster& master)
{
    for (const StoredRun& run : build.earlierRuns())
    {
        const auto made =
            std::find_if(run.model.masters.begin(), run.model.masters.end(), [&master](const ReportMaster& candidate) {
                return candidate.key == master.key && candidate.state == ReportMasterState::Made;
            });
        if (made == run.model.masters.end())
        {
            continue;
        }
        master.madeByRun = run.runId;
        master.lightsCombined = made->lightsCombined;
        master.statistics = made->statistics;
        master.distribution = made->distribution;
        master.snapshot = made->snapshot;
        master.overlapRetained = made->overlapRetained;
        master.stack = made->stack;
        master.astrometry = made->astrometry;
        master.astrometryDetail = made->astrometryDetail;
        master.solution = made->solution;
        master.photometry = made->photometry;
        master.durationMs = made->durationMs;
        master.finalization = made->finalization;
        return;
    }
    const QString file = build.resolvePath(master.file);
    fillMeasurement(build, file, master);
    fillSolution(build, file, master);
    build.notStated(reportText("How the master %1 was stacked").arg(master.name),
                    reportText("This run reused the master, and the folder's history does not keep the report of "
                               "the run that made it."));
}

/// A master the run has a result for.
void fillFromResult(ReportBuild& build, const IntegrationGroupResult& group,
                    const WorkflowManifest::IntegrationGroup* definition, ReportMaster& master)
{
    master.state = group.success ? ReportMasterState::Made : ReportMasterState::Failed;
    if (!group.outputFilename.trimmed().isEmpty())
    {
        master.fileName = group.outputFilename;
    }
    const QString file = build.outputFile(group);
    if (!group.outputPath.trimmed().isEmpty())
    {
        master.file = group.outputPath;
    }
    else if (!file.isEmpty())
    {
        master.file = file;
    }
    if (group.outputBytes > 0)
    {
        master.bytes = group.outputBytes;
    }
    else if (group.success)
    {
        master.bytes = build.fileSize(file);
    }
    master.retention = retentionOf(group.outputRetentionStatus);
    master.retentionReason = group.outputRetentionReason.trimmed();
    master.lightsCombined = group.lightFrameCount;
    if (!group.registrationGroupKey.isEmpty())
    {
        master.registrationGroupKey = group.registrationGroupKey;
    }
    master.registrationInputPolicy = group.registrationInputPolicy.trimmed();
    master.astrometry = astrometryOf(group.masterSolution);
    master.astrometryDetail = group.masterSolutionDetail;
    master.durationMs = group.durationMs;
    if (group.finalizationTiming.captured)
    {
        master.finalization.reserve(group.finalizationTiming.phases.size());
        for (const StagePhaseTiming& phase : group.finalizationTiming.phases)
        {
            master.finalization.push_back(
                ReportPhase{.name = phase.stageName, .status = phase.status, .durationMs = phase.durationMs});
        }
    }
    if (!group.success)
    {
        // The phase a failed stack stopped in is the one record of why; the run's own words are among its messages.
        const auto failedPhase =
            std::find_if(master.finalization.begin(), master.finalization.end(), [](const ReportPhase& phase) {
                return phase.status.compare(QStringLiteral("failed"), Qt::CaseInsensitive) == 0;
            });
        master.failure = failedPhase != master.finalization.end()
                             ? reportText("The stack failed in its phase \"%1\".").arg(failedPhase->name)
                             : reportText("The stack failed.");
        return;
    }

    fillMeasurement(build, file, master);
    fillSolution(build, file, master);
    if (group.qualityReport.valid)
    {
        master.stack = stackOf(group.qualityReport, group.lightFrameCount, definition);
    }

    fillPictures(build, master);
    if (master.preview.isEmpty())
    {
        master.preview = group.previewPath.trimmed();
    }
    fillMaps(build, group, definition, master);
}
} // namespace

void fillMasters(ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    const WorkflowExecutor::Result& result = build.result();

    QHash<QString, const WorkflowManifest::LightsCalibrationParams*> calibrationByLight;
    calibrationByLight.reserve(static_cast<qsizetype>(manifest.lightsCalibration.size()));
    for (const auto& calibration : manifest.lightsCalibration)
    {
        calibrationByLight.insert(calibration.lightPath.canonicalPath(), &calibration);
    }

    const QHash<QString, QString> folders = previewFolders(result);
    QHash<QString, std::size_t> resultByKey;
    for (std::size_t i = 0; i < result.integrationGroupResults.size(); ++i)
    {
        resultByKey.insert(result.integrationGroupResults[i].groupKey, i);
    }
    // The groups the run left as they are, by the run's own record of them: nothing else makes a master reused.
    QHash<QString, const ReusedIntegrationGroup*> reusedByKey;
    for (const ReusedIntegrationGroup& reused : result.reusedIntegrationGroups)
    {
        reusedByKey.insert(reused.groupKey, &reused);
    }
    QSet<QString> combinedKeys;
    for (const auto& registrationGroup : manifest.registrationGroups)
    {
        combinedKeys.insert(ImageIntegration::combinedRgbGroupKey(registrationGroup.groupKey.toString()));
    }

    const bool stopped = result.cancelled || !result.success;
    QSet<QString> planned;
    model.masters.reserve(manifest.integrationGroups.size() + 1);
    for (const auto& group : manifest.integrationGroups)
    {
        const QString key = group.groupKey.toString();
        planned.insert(key);

        ReportMaster master;
        master.key = key;
        master.slug = build.slugs().slugFor(key);
        master.previewFolder = ImageIntegration::sanitizeFilename(key);
        master.name = friendlyGroupKey(key);
        master.target = resolveTargetName(group);
        master.filter = group.filter.trimmed();
        master.kind = ReportMasterKind::Stack;
        master.fileName = group.outputFilename;
        if (!group.outputFilename.trimmed().isEmpty() && !manifest.outputDirectory.trimmed().isEmpty())
        {
            master.file = QDir(manifest.outputDirectory).filePath(group.outputFilename);
        }
        master.exposure = reportExposure(buildExposureBreakdown(group));
        master.captureResolution = group.resolution.trimmed();
        master.calibration = calibrationOf(group, calibrationByLight);
        master.registrationGroupKey = group.registrationGroupKey.toString();
        master.stackingSummary = stage_utils::describeStackingSettings(group.stacking);
        if (group.drizzle.has_value() && group.drizzle->enabled)
        {
            master.drizzleSummary = stage_utils::describeDrizzleSettings(*group.drizzle);
        }

        if (const auto found = resultByKey.constFind(key); found != resultByKey.constEnd())
        {
            master.previewFolder = folders.value(key, master.previewFolder);
            fillFromResult(build, result.integrationGroupResults[*found], &group, master);
        }
        else if (const auto reused = reusedByKey.constFind(key); reused != reusedByKey.constEnd())
        {
            // A group whose output is still valid leaves no result: the run names it among the groups it reused,
            // however the run ended.
            master.state = ReportMasterState::Reused;
            master.previewFolder = folders.value(key, master.previewFolder);
            if (!(*reused)->outputPath.trimmed().isEmpty())
            {
                master.file = (*reused)->outputPath;
            }
            master.bytes = build.fileSize(master.file);
            fillReused(build, master);
            fillPictures(build, master);
        }
        else if (stopped)
        {
            master.state = result.cancelled ? ReportMasterState::Cancelled : ReportMasterState::NotReached;
            master.failure = result.cancelled ? reportText("The run was cancelled before it reached this master.")
                                              : reportText("The run stopped before it reached this master.");
        }
        else
        {
            // Neither made nor reused in a run that completed: the run recorded nothing of the master, and the
            // report infers nothing from the file being there.
            master.state = ReportMasterState::NotReached;
            master.failure = reportText("The run completed without a record of this master.");
            build.notStated(reportText("What became of the master %1").arg(master.name),
                            reportText("The run completed with neither a result for it nor a record of having reused "
                                       "it."));
        }
        model.masters.push_back(std::move(master));
    }

    // A result that is no group of the manifest is an output the run made of other masters: a combined colour image.
    for (std::size_t i = 0; i < result.integrationGroupResults.size(); ++i)
    {
        const IntegrationGroupResult& group = result.integrationGroupResults[i];
        if (planned.contains(group.groupKey))
        {
            continue;
        }
        ReportMaster master;
        master.key = group.groupKey;
        master.slug = build.slugs().slugFor(group.groupKey);
        master.previewFolder = folders.value(group.groupKey, ImageIntegration::sanitizeFilename(group.groupKey));
        master.name = friendlyGroupKey(group.groupKey);
        master.kind = combinedKeys.contains(group.groupKey) ? ReportMasterKind::CombinedRgb : ReportMasterKind::Stack;
        for (const auto& registrationGroup : manifest.registrationGroups)
        {
            if (registrationGroup.groupKey.toString() == group.registrationGroupKey)
            {
                master.target = registrationGroup.object.trimmed();
                break;
            }
        }
        fillFromResult(build, group, nullptr, master);
        model.masters.push_back(std::move(master));
    }
}
} // namespace ImageIntegration::Reporting

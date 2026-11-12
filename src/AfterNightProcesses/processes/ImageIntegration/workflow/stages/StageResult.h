// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/**
 * @file StageResult.h
 * @brief Result type for workflow stage execution.
 *
 * StageResult encapsulates the outcome of a workflow stage, including
 * success/failure status, error information, and any warnings generated
 * during execution.
 *
 * @see IWorkflowStage, WorkflowExecutor, IntegrationError
 */

#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

/**
 * @brief Represents a file that was replaced during execution and staged for archiving.
 */
struct StagedFile
{
    QString originalPath; ///< Original path (e.g. output/stacked.fits)
    QString stagedPath; ///< Path to the temporary staged copy (e.g. output/stacked.fits.tmp_archive_<uuid>)
};

/**
 * @brief Metadata for a rejection map produced with an integrated master.
 */
struct RejectionMapResult
{
    QString title; ///< User-facing name (e.g. "Rejection Map (Low)")
    QString path; ///< Full path to the rejection map file
    QString previewPath; ///< Optional preview path (relative or absolute)
};

/**
 * @brief Metadata for a quality/confidence map produced with an integrated master.
 */
struct QualityMapResult
{
    QString title; ///< User-facing name (e.g. "Coverage Map")
    QString path; ///< Full path to the quality map file
    QString previewPath; ///< Optional preview path (relative or absolute)
};

/**
 * @brief Whether a successful scientific output has a retained history payload.
 *
 * An archive that states no retention status for an output reads as Unknown rather
 * than claiming that its payload was discarded.
 */
enum class OutputRetentionStatus
{
    Unknown,
    RetainedInHistory,
    NotRetainedByPolicy,
    NotRetainedByArchiveFailure,
};

[[nodiscard]] inline QString outputRetentionStatusToString(const OutputRetentionStatus status)
{
    switch (status)
    {
    case OutputRetentionStatus::RetainedInHistory:
        return QStringLiteral("retained_in_history");
    case OutputRetentionStatus::NotRetainedByPolicy:
        return QStringLiteral("not_retained_by_retention_policy");
    case OutputRetentionStatus::NotRetainedByArchiveFailure:
        return QStringLiteral("not_retained_by_archive_failure");
    case OutputRetentionStatus::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

[[nodiscard]] inline OutputRetentionStatus outputRetentionStatusFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("retained_in_history"))
    {
        return OutputRetentionStatus::RetainedInHistory;
    }
    if (normalized == QStringLiteral("not_retained_by_retention_policy"))
    {
        return OutputRetentionStatus::NotRetainedByPolicy;
    }
    if (normalized == QStringLiteral("not_retained_by_archive_failure"))
    {
        return OutputRetentionStatus::NotRetainedByArchiveFailure;
    }
    return OutputRetentionStatus::Unknown;
}

/**
 * @brief Where a stacked master's world-coordinate solution came from.
 *
 * An archive that states none reads as NotRecorded.
 */
enum class MasterSolutionOutcome
{
    NotRecorded,
    Propagated, ///< Carried from the registration's output plane or the reference light's own cards.
    Solved, ///< Found by the plate solver for the master, which had no propagated solution.
    None, ///< Written without a solution.
};

[[nodiscard]] inline QString masterSolutionOutcomeToString(const MasterSolutionOutcome outcome)
{
    switch (outcome)
    {
    case MasterSolutionOutcome::Propagated:
        return QStringLiteral("propagated");
    case MasterSolutionOutcome::Solved:
        return QStringLiteral("solved");
    case MasterSolutionOutcome::None:
        return QStringLiteral("none");
    case MasterSolutionOutcome::NotRecorded:
    default:
        return QStringLiteral("not_recorded");
    }
}

[[nodiscard]] inline MasterSolutionOutcome masterSolutionOutcomeFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("propagated"))
    {
        return MasterSolutionOutcome::Propagated;
    }
    if (normalized == QStringLiteral("solved"))
    {
        return MasterSolutionOutcome::Solved;
    }
    if (normalized == QStringLiteral("none"))
    {
        return MasterSolutionOutcome::None;
    }
    return MasterSolutionOutcome::NotRecorded;
}

struct StagePhaseTiming
{
    QString stageName;
    QString status;
    int durationMs = 0;
    int groupsProcessed = 0;
};

/**
 * @brief Exclusive finalization phases nested under one integration-group duration.
 *
 * durationMs on IntegrationGroupResult is the inclusive parent covering group
 * preparation, stack/drizzle compute, and finalization. phases contains only
 * non-overlapping finalization children. The parent must not be added to these
 * children in report totals. A skipped optional phase is represented by a
 * present phase with status "skipped" and durationMs == 0.
 */
struct StackGroupFinalizationTiming
{
    bool captured = false;
    std::vector<StagePhaseTiming> phases;

    [[nodiscard]] int exclusivePhasesTotalMs() const noexcept
    {
        int total_ms = 0;
        for (const auto& phase : phases)
        {
            total_ms += (std::max)(0, phase.durationMs);
        }
        return total_ms;
    }
};

[[nodiscard]] inline QJsonObject stagePhaseTimingToJson(const StagePhaseTiming& timing)
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = timing.stageName;
    obj[QStringLiteral("status")] = timing.status;
    obj[QStringLiteral("durationMs")] = timing.durationMs;
    if (timing.groupsProcessed > 0)
    {
        obj[QStringLiteral("groupsProcessed")] = timing.groupsProcessed;
    }
    return obj;
}

[[nodiscard]] inline StagePhaseTiming stagePhaseTimingFromJson(const QJsonObject& obj)
{
    StagePhaseTiming timing;
    timing.stageName = obj.value(QStringLiteral("name")).toString();
    timing.status = obj.value(QStringLiteral("status")).toString();
    timing.durationMs = obj.value(QStringLiteral("durationMs")).toInt();
    timing.groupsProcessed = obj.value(QStringLiteral("groupsProcessed")).toInt();
    return timing;
}

[[nodiscard]] inline QJsonObject stackGroupFinalizationTimingToJson(const StackGroupFinalizationTiming& timing)
{
    QJsonObject obj;
    obj[QStringLiteral("captured")] = timing.captured;
    QJsonArray phases;
    for (const auto& phase : timing.phases)
    {
        phases.append(stagePhaseTimingToJson(phase));
    }
    obj[QStringLiteral("phases")] = phases;
    return obj;
}

[[nodiscard]] inline StackGroupFinalizationTiming stackGroupFinalizationTimingFromJson(const QJsonObject& obj)
{
    StackGroupFinalizationTiming timing;
    if (obj.isEmpty())
    {
        return timing;
    }

    timing.captured = obj.value(QStringLiteral("captured")).toBool(true);
    const QJsonArray phases = obj.value(QStringLiteral("phases")).toArray();
    timing.phases.reserve(static_cast<size_t>(phases.size()));
    for (const auto& value : phases)
    {
        if (value.isObject())
        {
            timing.phases.push_back(stagePhaseTimingFromJson(value.toObject()));
        }
    }
    return timing;
}

/// Which rejection map a spatial cluster was found in.
enum class RejectionClusterMapKind
{
    Merged,
    Low,
    High
};

/// One cluster of rejected samples a stack's spatial analysis found, as a report and an archive keep it.
struct RejectionClusterRecord
{
    RejectionClusterMapKind mapKind = RejectionClusterMapKind::Merged;
    std::uint64_t channel = 0;
    std::uint64_t pixelCount = 0;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    double centroidX = 0.0;
    double centroidY = 0.0;
    double meanValue = 0.0;
    double peakValue = 0.0;
    double integratedValue = 0.0;
};

/// What a stack's quality report states, as the workflow's reports and archives keep it. The stacking stage fills it
/// from the engine's own report; its JSON form is the one archived runs already hold.
/// The weight a stack combined one light with.
struct StackLightWeight
{
    /// The light, by its path.
    QString light;

    /// Its weight, at a mean of one over the lights the master combines; the mean over its channels where they differ.
    double weight = 1.0;
};

struct StackQualityRecord
{
    bool valid = false;
    std::uint64_t inputFrameCount = 0;
    double effectiveFrameCount = 0.0;
    double theoreticalSnrGain = 0.0;
    double actualSnrGain = 0.0;
    double integrationEfficiency = 0.0;
    double minCoverage = 0.0;
    double medianCoverage = 0.0;
    double maxCoverage = 0.0;
    double coverageStddev = 0.0;
    bool hasRejectionStats = false;
    double globalRejectionRate = 0.0;
    double minFrameRejectionRate = 0.0;
    double meanFrameRejectionRate = 0.0;
    double medianFrameRejectionRate = 0.0;
    double maxFrameRejectionRate = 0.0;
    std::vector<double> frameRejectionRates;
    bool hasWeightingStats = false;
    double weightMin = 0.0;
    double weightMax = 0.0;
    double weightStddev = 0.0;
    double weightEfficiency = 0.0;
    bool hasLocalWeightingStats = false;
    int localWeightWindowSize = 0;
    int localWeightGridSpacing = 0;
    double localWeightSnrExponent = 0.0;
    std::uint64_t localWeightGridCount = 0;
    double localWeightMean = 0.0;
    double localWeightMin = 0.0;
    double localWeightMax = 0.0;
    double localWeightStddev = 0.0;
    bool hasSpatialRejectionClusters = false;
    std::uint64_t spatialClusterCount = 0;
    std::uint64_t spatialClusterPixels = 0;
    std::uint64_t spatialLargestClusterPixels = 0;
    double spatialClusterFraction = 0.0;
    double spatialLargestClusterFraction = 0.0;
    /// The elements either rejection map reached at all, clustered or not, and their share of the map's elements: a
    /// figure of its own beside the clusters' pixels, which the merge setting does not change the meaning of.
    std::uint64_t spatialRejectedElements = 0;
    double spatialRejectedElementFraction = 0.0;
    std::vector<RejectionClusterRecord> spatialClusters;
    bool spatialRestackRequested = false;
    bool spatialRestackApplied = false;
    std::uint64_t spatialRestackInitialClusterCount = 0;
    double spatialRestackFlaggedFrameThreshold = 0.0;
    /// The lights the spatial rejection flagged - each by its place among the group's lights, which the report names
    /// it by - and each one's rejection rate in the first stack.
    std::vector<std::uint64_t> spatialRestackExcludedFrameIndices;
    std::vector<double> spatialRestackExcludedFrameRates;
    /// What brought the lights to the stack's reference, and which light that was: the normalization stage's
    /// coefficients or the stack's own measurement. Empty when the stack normalized nothing.
    QString normalization;
    /// The weight the stack combined each light the master holds with, in the order it read them - after a re-stack,
    /// the lights it kept. Empty where it recorded none.
    std::vector<StackLightWeight> lightWeights;
};

/// The input and output a stack's execution counted, for the run's diagnostics.
struct StackExecutionCounters
{
    std::uint64_t inputDecodeOperations = 0;
    std::uint64_t scientificOutputEncodeCompletions = 0;
    std::uint64_t inputPassesStarted = 0;
    std::uint64_t tilePassesStarted = 0;
    std::uint64_t stackLoaderRequests = 0;
    std::uint64_t roiSubsetReads = 0;
    std::uint64_t fullFrameFallbackReads = 0;
    std::uint64_t logicalDecodedPixelBytes = 0;
    std::uint64_t calibrationInvocations = 0;
    std::uint64_t calibratedSpillFilesCreated = 0;
    std::uint64_t calibratedSpillBytesWritten = 0;
    std::uint64_t calibratedSpillBytesRead = 0;
    std::uint64_t calibratedSpillRoiReads = 0;
    std::uint64_t calibratedSpillCleanupSuccesses = 0;
    std::uint64_t calibratedSpillCleanupFailures = 0;
    std::uint64_t dumpBytesRead = 0;
    std::uint64_t dumpBytesWritten = 0;
    std::uint64_t pixmapBytesRead = 0;
    std::uint64_t pixmapBytesWritten = 0;
    std::uint64_t descriptorProviderHits = 0;
    std::uint64_t denseProviderHits = 0;
    std::uint64_t sipLazyProviderHits = 0;
    std::uint64_t polynomialLazyProviderHits = 0;
    std::uint64_t sipCoordinatesEvaluated = 0;
    std::uint64_t sharedSipPowerTableBuilds = 0;
    std::uint64_t explicitSipInverseEvaluations = 0;
    std::uint64_t sipNewtonFallbackIterations = 0;
    std::uint64_t polynomialCoordinatesEvaluated = 0;
    std::uint64_t sharedPolynomialPowerTableBuilds = 0;
    std::uint64_t lazyBlocksGenerated = 0;
    std::uint64_t lazyCoordinatePairsGenerated = 0;
    std::uint64_t peakLazyCoordinateBytes = 0;
    std::uint64_t denseFallbackCount = 0;
    std::uint64_t lazyMapCancellations = 0;
    std::uint64_t lazyMapFailures = 0;
};

[[nodiscard]] inline QString rejectionClusterMapKindToString(const RejectionClusterMapKind kind)
{
    switch (kind)
    {
    case RejectionClusterMapKind::Merged:
        return QStringLiteral("merged");
    case RejectionClusterMapKind::Low:
        return QStringLiteral("low");
    case RejectionClusterMapKind::High:
        return QStringLiteral("high");
    }

    return QStringLiteral("merged");
}

[[nodiscard]] inline RejectionClusterMapKind rejectionClusterMapKindFromString(const QString& kind)
{
    const QString normalized = kind.trimmed().toLower();
    if (normalized == QStringLiteral("low"))
    {
        return RejectionClusterMapKind::Low;
    }
    if (normalized == QStringLiteral("high"))
    {
        return RejectionClusterMapKind::High;
    }
    return RejectionClusterMapKind::Merged;
}

/**
 * @brief Per-group alignment result metadata (populated by LightsAlignment stage).
 */
struct AlignmentGroupResult
{
    QString groupKey; ///< Registration group key
    int totalAligned = 0; ///< Total frames aligned in this group
    int sipCorrectedCount = 0; ///< Frames with SIP distortion correction applied
    int caCorrectedCount = 0; ///< Frames with chromatic aberration correction applied
    int caPassthroughCount = 0; ///< Frames where CA correction was skipped (mono or insufficient stars)
};

/// A light a registration found no transform for, and why, in the registration's own words. A run records it
/// where the registration fails, since the metrics database keeps only that it failed; a later run that finds the
/// registration still valid registers nothing again and records none.
struct RegistrationFailure
{
    /// The registration group's key.
    QString groupKey;

    /// The light, by its path.
    QString light;

    /// Why no transform was found: too few stars, too few pairs matched, a fit that failed.
    QString reason;
};

[[nodiscard]] inline QJsonArray registrationFailuresToJson(const std::vector<RegistrationFailure>& failures)
{
    QJsonArray array;
    for (const RegistrationFailure& failure : failures)
    {
        QJsonObject obj;
        obj[QStringLiteral("groupKey")] = failure.groupKey;
        obj[QStringLiteral("light")] = failure.light;
        obj[QStringLiteral("reason")] = failure.reason;
        array.append(obj);
    }
    return array;
}

/// Why a stack left a light of its group out on its own.
enum class DroppedLightReason
{
    NoRegisteredOutput, ///< The alignment wrote no registered image of the light, or the image is gone.
    NoRegistrationTransform, ///< The registration recorded no transform of the light.
    NoStarBaseProduct, ///< A stars-plus-comet stack has no star base product of the light.
    NoCometAlignedImage, ///< A comet stack has no comet-aligned image of the light.
};

/// A light a stack left out on its own, and why.
struct DroppedLight
{
    /// The light, by its path.
    QString light;

    DroppedLightReason reason = DroppedLightReason::NoRegisteredOutput;
};

/// The reason in the words the stacking stage's warning gives it.
[[nodiscard]] inline QString droppedLightReasonText(const DroppedLightReason reason)
{
    switch (reason)
    {
    case DroppedLightReason::NoRegistrationTransform:
        return QStringLiteral("no registration transform");
    case DroppedLightReason::NoStarBaseProduct:
        return QStringLiteral("no star base product");
    case DroppedLightReason::NoCometAlignedImage:
        return QStringLiteral("no comet-aligned image");
    case DroppedLightReason::NoRegisteredOutput:
    default:
        return QStringLiteral("no registered output");
    }
}

/// The word an archive stores for the reason.
[[nodiscard]] inline QString droppedLightReasonToString(const DroppedLightReason reason)
{
    switch (reason)
    {
    case DroppedLightReason::NoRegistrationTransform:
        return QStringLiteral("no_registration_transform");
    case DroppedLightReason::NoStarBaseProduct:
        return QStringLiteral("no_star_base_product");
    case DroppedLightReason::NoCometAlignedImage:
        return QStringLiteral("no_comet_aligned_image");
    case DroppedLightReason::NoRegisteredOutput:
    default:
        return QStringLiteral("no_registered_output");
    }
}

/// The reason a stored word names; nothing for a word that names none.
[[nodiscard]] inline std::optional<DroppedLightReason> droppedLightReasonFromString(const QString& value)
{
    for (const DroppedLightReason reason :
         {DroppedLightReason::NoRegisteredOutput, DroppedLightReason::NoRegistrationTransform,
          DroppedLightReason::NoStarBaseProduct, DroppedLightReason::NoCometAlignedImage})
    {
        if (value == droppedLightReasonToString(reason))
        {
            return reason;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline QJsonArray droppedLightsToJson(const std::vector<DroppedLight>& dropped)
{
    QJsonArray array;
    for (const DroppedLight& light : dropped)
    {
        QJsonObject obj;
        obj[QStringLiteral("light")] = light.light;
        obj[QStringLiteral("reason")] = droppedLightReasonToString(light.reason);
        array.append(obj);
    }
    return array;
}

/// The dropped lights an archive stores; an entry that names no light or no reason is not one.
[[nodiscard]] inline std::vector<DroppedLight> droppedLightsFromJson(const QJsonArray& array)
{
    std::vector<DroppedLight> dropped;
    dropped.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        const QJsonObject obj = value.toObject();
        const QString light = obj.value(QStringLiteral("light")).toString();
        const std::optional<DroppedLightReason> reason =
            droppedLightReasonFromString(obj.value(QStringLiteral("reason")).toString());
        if (!light.isEmpty() && reason.has_value())
        {
            dropped.push_back(DroppedLight{.light = light, .reason = *reason});
        }
    }
    return dropped;
}

/// An integration group a run did not stack because the master on disk is still the one it would make: the plan of
/// an incremental run left the group out, or the stacking stage found the output valid. Such a group has no
/// `IntegrationGroupResult`, and nothing that counts a run's successful groups counts it.
struct ReusedIntegrationGroup
{
    QString groupKey;

    /// The master the run left as it is.
    QString outputPath;
};

[[nodiscard]] inline QJsonArray reusedIntegrationGroupsToJson(const std::vector<ReusedIntegrationGroup>& groups)
{
    QJsonArray array;
    for (const ReusedIntegrationGroup& group : groups)
    {
        QJsonObject obj;
        obj[QStringLiteral("groupKey")] = group.groupKey;
        obj[QStringLiteral("outputPath")] = group.outputPath;
        array.append(obj);
    }
    return array;
}

/// The reused groups an archive stores; an entry that names no group is not one.
[[nodiscard]] inline std::vector<ReusedIntegrationGroup> reusedIntegrationGroupsFromJson(const QJsonArray& array)
{
    std::vector<ReusedIntegrationGroup> groups;
    groups.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        const QJsonObject obj = value.toObject();
        const QString key = obj.value(QStringLiteral("groupKey")).toString();
        if (!key.isEmpty())
        {
            groups.push_back(ReusedIntegrationGroup{.groupKey = key,
                                                    .outputPath = obj.value(QStringLiteral("outputPath")).toString()});
        }
    }
    return groups;
}

/// An integration group whose stack failed before a master of it was written, and why, in the words the stage gives
/// the person. A group result is made only of a group that reached its master's finalization, so a group that had
/// too few registered lights, or whose stack the engine refused, has none: the run names it here, and its report
/// says the master failed and why instead of finding no record of it.
struct FailedIntegrationGroup
{
    QString groupKey;

    /// Why the group's stack failed.
    QString error;
};

[[nodiscard]] inline QJsonArray failedIntegrationGroupsToJson(const std::vector<FailedIntegrationGroup>& groups)
{
    QJsonArray array;
    for (const FailedIntegrationGroup& group : groups)
    {
        QJsonObject obj;
        obj[QStringLiteral("groupKey")] = group.groupKey;
        obj[QStringLiteral("error")] = group.error;
        array.append(obj);
    }
    return array;
}

/**
 * @brief Result metadata for a single integration group.
 */
struct IntegrationGroupResult
{
    QString groupKey;
    QString outputPath;
    QString outputFilename;
    qint64 outputBytes = 0;
    std::optional<FinalCommittedArtifact> committedArtifact;
    QString previewPath;
    QByteArray reportPreviewJpeg; ///< Bounded JPEG prepared from the resident master for P1-03 archiving.
    QSize reportPreviewSize; ///< Pixel dimensions of reportPreviewJpeg; empty when no report asset was prepared.
    bool reportPreviewPreparedForCurrentRun = false; ///< Suppresses a master reload after a current-run attempt.
    QByteArray reportDetailJpeg; ///< JPEG of the master's centre at full scale, stretched as reportPreviewJpeg is.
    QSize reportDetailSize; ///< Pixel dimensions of reportDetailJpeg; empty when no detail was prepared.
    OutputRetentionStatus outputRetentionStatus = OutputRetentionStatus::Unknown;
    QString outputRetentionReason;
    int lightFrameCount = 0;
    QString registrationGroupKey;
    QString registrationInputPolicy; ///< Authoritative runtime stacking route and typed eligibility reason.
    MasterSolutionOutcome masterSolution = MasterSolutionOutcome::NotRecorded;
    QString masterSolutionDetail; ///< The source carried from, the solver's detail, or why the master has none.
    int durationMs = 0;
    bool success = false;
    StackGroupFinalizationTiming finalizationTiming;

    StackQualityRecord qualityReport;

    /// The lights of the group the stack left out on its own, each with its reason; the stack's warning names them
    /// too.
    std::vector<DroppedLight> droppedLights;

    // Rejection maps generated alongside this stacked output.
    std::vector<RejectionMapResult> rejectionMaps;

    // Quality/confidence maps generated alongside this stacked output.
    std::vector<QualityMapResult> qualityMaps;

    // Files that were displaced by this group's execution (staged for archiving)
    std::vector<StagedFile> stagedFiles;
};

[[nodiscard]] inline QJsonObject stackingQualityReportToJson(const StackQualityRecord& report)
{
    QJsonObject obj;
    obj[QStringLiteral("valid")] = report.valid;
    obj[QStringLiteral("inputFrameCount")] = static_cast<qint64>(report.inputFrameCount);
    obj[QStringLiteral("effectiveFrameCount")] = report.effectiveFrameCount;
    obj[QStringLiteral("theoreticalSnrGain")] = report.theoreticalSnrGain;
    obj[QStringLiteral("actualSnrGain")] = report.actualSnrGain;
    obj[QStringLiteral("integrationEfficiency")] = report.integrationEfficiency;
    obj[QStringLiteral("minCoverage")] = report.minCoverage;
    obj[QStringLiteral("medianCoverage")] = report.medianCoverage;
    obj[QStringLiteral("maxCoverage")] = report.maxCoverage;
    obj[QStringLiteral("coverageStddev")] = report.coverageStddev;
    obj[QStringLiteral("hasRejectionStats")] = report.hasRejectionStats;
    obj[QStringLiteral("globalRejectionRate")] = report.globalRejectionRate;
    obj[QStringLiteral("minFrameRejectionRate")] = report.minFrameRejectionRate;
    obj[QStringLiteral("meanFrameRejectionRate")] = report.meanFrameRejectionRate;
    obj[QStringLiteral("medianFrameRejectionRate")] = report.medianFrameRejectionRate;
    obj[QStringLiteral("maxFrameRejectionRate")] = report.maxFrameRejectionRate;

    QJsonArray frameRejectionRates;
    for (const double rate : report.frameRejectionRates)
    {
        frameRejectionRates.append(rate);
    }
    obj[QStringLiteral("frameRejectionRates")] = frameRejectionRates;

    obj[QStringLiteral("hasWeightingStats")] = report.hasWeightingStats;
    obj[QStringLiteral("weightMin")] = report.weightMin;
    obj[QStringLiteral("weightMax")] = report.weightMax;
    obj[QStringLiteral("weightStddev")] = report.weightStddev;
    obj[QStringLiteral("weightEfficiency")] = report.weightEfficiency;

    obj[QStringLiteral("hasLocalWeightingStats")] = report.hasLocalWeightingStats;
    obj[QStringLiteral("localWeightWindowSize")] = report.localWeightWindowSize;
    obj[QStringLiteral("localWeightGridSpacing")] = report.localWeightGridSpacing;
    obj[QStringLiteral("localWeightSnrExponent")] = report.localWeightSnrExponent;
    obj[QStringLiteral("localWeightGridCount")] = static_cast<qint64>(report.localWeightGridCount);
    obj[QStringLiteral("localWeightMean")] = report.localWeightMean;
    obj[QStringLiteral("localWeightMin")] = report.localWeightMin;
    obj[QStringLiteral("localWeightMax")] = report.localWeightMax;
    obj[QStringLiteral("localWeightStddev")] = report.localWeightStddev;

    obj[QStringLiteral("hasSpatialRejectionClusters")] = report.hasSpatialRejectionClusters;
    obj[QStringLiteral("spatialClusterCount")] = static_cast<qint64>(report.spatialClusterCount);
    obj[QStringLiteral("spatialClusterPixels")] = static_cast<qint64>(report.spatialClusterPixels);
    obj[QStringLiteral("spatialLargestClusterPixels")] = static_cast<qint64>(report.spatialLargestClusterPixels);
    obj[QStringLiteral("spatialClusterFraction")] = report.spatialClusterFraction;
    obj[QStringLiteral("spatialLargestClusterFraction")] = report.spatialLargestClusterFraction;
    obj[QStringLiteral("spatialRejectedElements")] = static_cast<qint64>(report.spatialRejectedElements);
    obj[QStringLiteral("spatialRejectedElementFraction")] = report.spatialRejectedElementFraction;

    QJsonArray spatialClusters;
    for (const auto& cluster : report.spatialClusters)
    {
        QJsonObject clusterObj;
        clusterObj[QStringLiteral("mapKind")] = rejectionClusterMapKindToString(cluster.mapKind);
        clusterObj[QStringLiteral("channel")] = static_cast<qint64>(cluster.channel);
        clusterObj[QStringLiteral("pixelCount")] = static_cast<qint64>(cluster.pixelCount);
        clusterObj[QStringLiteral("minX")] = cluster.minX;
        clusterObj[QStringLiteral("minY")] = cluster.minY;
        clusterObj[QStringLiteral("maxX")] = cluster.maxX;
        clusterObj[QStringLiteral("maxY")] = cluster.maxY;
        clusterObj[QStringLiteral("centroidX")] = cluster.centroidX;
        clusterObj[QStringLiteral("centroidY")] = cluster.centroidY;
        clusterObj[QStringLiteral("meanValue")] = cluster.meanValue;
        clusterObj[QStringLiteral("peakValue")] = cluster.peakValue;
        clusterObj[QStringLiteral("integratedValue")] = cluster.integratedValue;
        spatialClusters.append(clusterObj);
    }
    obj[QStringLiteral("spatialClusters")] = spatialClusters;

    obj[QStringLiteral("spatialRestackRequested")] = report.spatialRestackRequested;
    obj[QStringLiteral("spatialRestackApplied")] = report.spatialRestackApplied;
    obj[QStringLiteral("spatialRestackInitialClusterCount")] =
        static_cast<qint64>(report.spatialRestackInitialClusterCount);
    obj[QStringLiteral("spatialRestackFlaggedFrameThreshold")] = report.spatialRestackFlaggedFrameThreshold;

    QJsonArray excludedIndices;
    for (const auto index : report.spatialRestackExcludedFrameIndices)
    {
        excludedIndices.append(static_cast<qint64>(index));
    }
    obj[QStringLiteral("spatialRestackExcludedFrameIndices")] = excludedIndices;

    QJsonArray excludedRates;
    for (const auto rate : report.spatialRestackExcludedFrameRates)
    {
        excludedRates.append(rate);
    }
    obj[QStringLiteral("spatialRestackExcludedFrameRates")] = excludedRates;
    obj[QStringLiteral("normalization")] = report.normalization;

    QJsonArray lightWeights;
    for (const StackLightWeight& entry : report.lightWeights)
    {
        QJsonObject weighed;
        weighed[QStringLiteral("light")] = entry.light;
        weighed[QStringLiteral("weight")] = entry.weight;
        lightWeights.append(weighed);
    }
    obj[QStringLiteral("lightWeights")] = lightWeights;
    return obj;
}

[[nodiscard]] inline StackQualityRecord stackingQualityReportFromJson(const QJsonObject& obj)
{
    StackQualityRecord report;
    if (obj.isEmpty())
    {
        return report;
    }

    report.valid = obj.value(QStringLiteral("valid")).toBool();
    report.inputFrameCount = obj.value(QStringLiteral("inputFrameCount")).toVariant().toULongLong();
    report.effectiveFrameCount = obj.value(QStringLiteral("effectiveFrameCount")).toDouble();
    report.theoreticalSnrGain = obj.value(QStringLiteral("theoreticalSnrGain")).toDouble();
    report.actualSnrGain = obj.value(QStringLiteral("actualSnrGain")).toDouble();
    report.integrationEfficiency = obj.value(QStringLiteral("integrationEfficiency")).toDouble();
    report.minCoverage = obj.value(QStringLiteral("minCoverage")).toDouble();
    report.medianCoverage = obj.value(QStringLiteral("medianCoverage")).toDouble();
    report.maxCoverage = obj.value(QStringLiteral("maxCoverage")).toDouble();
    report.coverageStddev = obj.value(QStringLiteral("coverageStddev")).toDouble();
    report.hasRejectionStats = obj.value(QStringLiteral("hasRejectionStats")).toBool();
    report.globalRejectionRate = obj.value(QStringLiteral("globalRejectionRate")).toDouble();
    report.minFrameRejectionRate = obj.value(QStringLiteral("minFrameRejectionRate")).toDouble();
    report.meanFrameRejectionRate = obj.value(QStringLiteral("meanFrameRejectionRate")).toDouble();
    report.medianFrameRejectionRate = obj.value(QStringLiteral("medianFrameRejectionRate")).toDouble();
    report.maxFrameRejectionRate = obj.value(QStringLiteral("maxFrameRejectionRate")).toDouble();

    const QJsonArray frameRejectionRates = obj.value(QStringLiteral("frameRejectionRates")).toArray();
    report.frameRejectionRates.reserve(static_cast<std::size_t>(frameRejectionRates.size()));
    for (const auto& value : frameRejectionRates)
    {
        report.frameRejectionRates.push_back(value.toDouble());
    }

    report.hasWeightingStats = obj.value(QStringLiteral("hasWeightingStats")).toBool();
    report.weightMin = obj.value(QStringLiteral("weightMin")).toDouble();
    report.weightMax = obj.value(QStringLiteral("weightMax")).toDouble();
    report.weightStddev = obj.value(QStringLiteral("weightStddev")).toDouble();
    report.weightEfficiency = obj.value(QStringLiteral("weightEfficiency")).toDouble();

    report.hasLocalWeightingStats = obj.value(QStringLiteral("hasLocalWeightingStats")).toBool();
    report.localWeightWindowSize = obj.value(QStringLiteral("localWeightWindowSize")).toInt();
    report.localWeightGridSpacing = obj.value(QStringLiteral("localWeightGridSpacing")).toInt();
    report.localWeightSnrExponent = obj.value(QStringLiteral("localWeightSnrExponent")).toDouble();
    report.localWeightGridCount = obj.value(QStringLiteral("localWeightGridCount")).toVariant().toULongLong();
    report.localWeightMean = obj.value(QStringLiteral("localWeightMean")).toDouble();
    report.localWeightMin = obj.value(QStringLiteral("localWeightMin")).toDouble();
    report.localWeightMax = obj.value(QStringLiteral("localWeightMax")).toDouble();
    report.localWeightStddev = obj.value(QStringLiteral("localWeightStddev")).toDouble();

    report.hasSpatialRejectionClusters = obj.value(QStringLiteral("hasSpatialRejectionClusters")).toBool();
    report.spatialClusterCount = obj.value(QStringLiteral("spatialClusterCount")).toVariant().toULongLong();
    report.spatialClusterPixels = obj.value(QStringLiteral("spatialClusterPixels")).toVariant().toULongLong();
    report.spatialLargestClusterPixels =
        obj.value(QStringLiteral("spatialLargestClusterPixels")).toVariant().toULongLong();
    report.spatialClusterFraction = obj.value(QStringLiteral("spatialClusterFraction")).toDouble();
    report.spatialLargestClusterFraction = obj.value(QStringLiteral("spatialLargestClusterFraction")).toDouble();
    report.spatialRejectedElements = obj.value(QStringLiteral("spatialRejectedElements")).toVariant().toULongLong();
    report.spatialRejectedElementFraction = obj.value(QStringLiteral("spatialRejectedElementFraction")).toDouble();

    const QJsonArray spatialClusters = obj.value(QStringLiteral("spatialClusters")).toArray();
    report.spatialClusters.reserve(static_cast<std::size_t>(spatialClusters.size()));
    for (const auto& value : spatialClusters)
    {
        const QJsonObject clusterObj = value.toObject();
        RejectionClusterRecord cluster;
        cluster.mapKind = rejectionClusterMapKindFromString(clusterObj.value(QStringLiteral("mapKind")).toString());
        cluster.channel = clusterObj.value(QStringLiteral("channel")).toVariant().toULongLong();
        cluster.pixelCount = clusterObj.value(QStringLiteral("pixelCount")).toVariant().toULongLong();
        cluster.minX = clusterObj.value(QStringLiteral("minX")).toInt();
        cluster.minY = clusterObj.value(QStringLiteral("minY")).toInt();
        cluster.maxX = clusterObj.value(QStringLiteral("maxX")).toInt();
        cluster.maxY = clusterObj.value(QStringLiteral("maxY")).toInt();
        cluster.centroidX = clusterObj.value(QStringLiteral("centroidX")).toDouble();
        cluster.centroidY = clusterObj.value(QStringLiteral("centroidY")).toDouble();
        cluster.meanValue = clusterObj.value(QStringLiteral("meanValue")).toDouble();
        cluster.peakValue = clusterObj.value(QStringLiteral("peakValue")).toDouble();
        cluster.integratedValue = clusterObj.value(QStringLiteral("integratedValue")).toDouble();
        report.spatialClusters.push_back(std::move(cluster));
    }

    report.spatialRestackRequested = obj.value(QStringLiteral("spatialRestackRequested")).toBool();
    report.spatialRestackApplied = obj.value(QStringLiteral("spatialRestackApplied")).toBool();
    report.spatialRestackInitialClusterCount =
        obj.value(QStringLiteral("spatialRestackInitialClusterCount")).toVariant().toULongLong();
    report.spatialRestackFlaggedFrameThreshold =
        obj.value(QStringLiteral("spatialRestackFlaggedFrameThreshold")).toDouble();

    const QJsonArray excludedIndices = obj.value(QStringLiteral("spatialRestackExcludedFrameIndices")).toArray();
    report.spatialRestackExcludedFrameIndices.reserve(static_cast<std::size_t>(excludedIndices.size()));
    for (const auto& value : excludedIndices)
    {
        report.spatialRestackExcludedFrameIndices.push_back(value.toVariant().toULongLong());
    }

    const QJsonArray excludedRates = obj.value(QStringLiteral("spatialRestackExcludedFrameRates")).toArray();
    report.spatialRestackExcludedFrameRates.reserve(static_cast<std::size_t>(excludedRates.size()));
    for (const auto& value : excludedRates)
    {
        report.spatialRestackExcludedFrameRates.push_back(value.toDouble());
    }
    report.normalization = obj.value(QStringLiteral("normalization")).toString();

    const QJsonArray lightWeights = obj.value(QStringLiteral("lightWeights")).toArray();
    report.lightWeights.reserve(static_cast<std::size_t>(lightWeights.size()));
    for (const auto& value : lightWeights)
    {
        const QJsonObject weighed = value.toObject();
        report.lightWeights.push_back(
            StackLightWeight{.light = weighed.value(QStringLiteral("light")).toString(),
                             .weight = weighed.value(QStringLiteral("weight")).toDouble(1.0)});
    }
    return report;
}

/**
 * @brief Result of a workflow stage execution.
 *
 * Each stage returns a StageResult indicating whether it completed
 * successfully, failed, was cancelled, or was skipped. Failure results
 * include structured error information for diagnostics.
 */
struct StageResult
{
    /**
     * @brief Stage execution status.
     */
    enum class Status
    {
        Success, ///< Stage completed successfully
        PartialSuccess, ///< Stage completed but some frames failed (workflow can continue)
        Failure, ///< Stage failed with an error
        Cancelled, ///< Stage was cancelled by user
        Skipped ///< Stage was skipped (e.g., already completed)
    };

    Status status = Status::Success; ///< Execution status
    QString errorMessage; ///< Error message as text (prefer error.message())
    QStringList warnings; ///< Non-fatal warnings generated
    IntegrationError error; ///< Structured error information
    IntegrationErrorReport errorReport; ///< Aggregated per-frame errors (for PartialSuccess)

    std::vector<IntegrationGroupResult> groupResults; ///< Per-group results (if applicable)
    std::vector<AlignmentGroupResult> alignmentGroupResults; ///< Per-group alignment results
    std::vector<StagePhaseTiming> phaseTimings; ///< Timed sub-phases reported separately from the parent stage
    std::optional<StackExecutionCounters> engineExecutionCounters; ///< Optional stage-local diagnostics
    std::vector<ReusedIntegrationGroup> reusedGroups; ///< Groups whose output was still valid, which have no result
    /// The groups whose stack failed before a master was written, which have no result either, each with why.
    std::vector<FailedIntegrationGroup> failedGroups;
    /// The keys of the calibration groups whose master a calibration stage found still valid and left as it was.
    std::vector<QString> reusedCalibrationGroups;
    /// The lights the registration stage found no transform for in this run, each with its reason.
    std::vector<RegistrationFailure> registrationFailures;

    /** @brief Create a success result. */
    [[nodiscard]] static StageResult success()
    {
        return StageResult{Status::Success, {}, {}, IntegrationError::success(), {}, {}};
    }

    /**
     * @brief Create a failure result with a simple message.
     * @param message Error description.
     * @deprecated Use failure(IntegrationError) for structured errors.
     */
    [[nodiscard]] static StageResult failure(const QString& message)
    {
        return StageResult{
            Status::Failure, message, {}, IntegrationError(IntegrationErrorCode::InternalError, message), {}, {}};
    }

    /**
     * @brief Create a failure result with structured error.
     * @param err The structured error with code and context.
     */
    [[nodiscard]] static StageResult failure(const IntegrationError& err)
    {
        return StageResult{Status::Failure, err.message(), {}, err, {}, {}};
    }

    /** @brief Create a cancellation result. */
    [[nodiscard]] static StageResult cancelled()
    {
        return StageResult{Status::Cancelled, QStringLiteral("Cancelled"), {}, IntegrationError::cancelled(), {}, {}};
    }

    /**
     * @brief Create a partial success result (some frames failed, workflow continues).
     * @param report Aggregated error report with per-frame failures.
     */
    [[nodiscard]] static StageResult partialSuccess(const IntegrationErrorReport& report)
    {
        StageResult r;
        r.status = Status::PartialSuccess;
        r.errorReport = report;
        r.warnings = report.warnings;
        return r;
    }

    /** @brief Create a skipped result. */
    [[nodiscard]] static StageResult skipped()
    {
        return StageResult{Status::Skipped, {}, {}, IntegrationError::success(), {}, {}};
    }

    /** @brief Check if stage completed successfully (including partial success). */
    [[nodiscard]] bool isSuccess() const noexcept
    {
        return status == Status::Success || status == Status::PartialSuccess;
    }

    /** @brief Check if stage completed with some frame failures. */
    [[nodiscard]] bool isPartialSuccess() const noexcept
    {
        return status == Status::PartialSuccess;
    }

    /** @brief Check if stage was cancelled. */
    [[nodiscard]] bool isCancelled() const noexcept
    {
        return status == Status::Cancelled;
    }

    /** @brief Check if stage failed. */
    [[nodiscard]] bool isFailure() const noexcept
    {
        return status == Status::Failure;
    }

    /** @brief Check if the error is recoverable (workflow can continue). */
    [[nodiscard]] bool isRecoverable() const noexcept
    {
        return error.isRecoverable();
    }

    /** @brief Get the structured error code. */
    [[nodiscard]] IntegrationErrorCode errorCode() const noexcept
    {
        return error.code();
    }
};

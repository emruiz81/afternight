// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowExecutor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include "WorkflowRunStateStore.h"
#include "WorkflowRunArchive.h"
#include "WorkflowArchiver.h"
#include "ExecutionPreparationSnapshot.h"
#include "ManifestHashing.h"
#include "WorkflowImageCache.h"
#include "WorkflowArtifactPublicationLedger.h"
#include "WorkflowRawLoadPolicy.h"
#include "WorkflowStageServices.h"
#include <AfterNightPlatform/Logging.h>
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/WorkflowLogger.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"

#include "app/FilePolicies.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <utility>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(WorkflowExecutor)

namespace
{
/// The files one directory held that an interrupted run left mid-write: the engine's pending outputs and the workflow's
/// own pending documents. Nothing else is touched.
[[nodiscard]] std::size_t sweepAbandonedWritesIn(const QString& directory)
{
    std::size_t reclaimed = 0;
    const an::Result<std::uint64_t> outputs =
        an::sweepPendingOutputs(QDir::toNativeSeparators(directory).toStdString());
    if (outputs.hasValue())
    {
        reclaimed += static_cast<std::size_t>(outputs.value());
    }
    reclaimed += static_cast<std::size_t>(ImageIntegration::recoverDocumentDirectory(directory).removed.size());
    return reclaimed;
}

/**
 * @brief Remove pending-write payloads left below @p outputDirectory by an interrupted run.
 *
 * Sweeps the output root, the directories the workflow materializes intermediates into and the directories its
 * documents are published in. Only files still carrying a pending-write suffix are removed; a file reaches its final
 * name only once it is complete, so no committed artifact can match.
 */
std::size_t sweepAbandonedWrites(const QString& outputDirectory)
{
    if (outputDirectory.isEmpty())
    {
        return 0;
    }

    static const QStringList kIntermediateDirectories = {
        QStringLiteral("calibrated_lights"), QStringLiteral("ca_corrected_lights"), QStringLiteral("registered_lights"),
        QStringLiteral(".afternight/image_integration/artifacts/normalization"),
        QStringLiteral(".afternight/image_integration/artifacts/drizzle")};

    const QDir root(outputDirectory);
    std::size_t reclaimed = sweepAbandonedWritesIn(outputDirectory);
    for (const QString& name : kIntermediateDirectories)
    {
        const QString path = root.filePath(name);
        reclaimed += sweepAbandonedWritesIn(path);

        // Registered frames and each group's documents live one or two levels deeper.
        const QDir nested(path);
        const auto groups = nested.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& group : groups)
        {
            const QString groupPath = nested.filePath(group);
            reclaimed += sweepAbandonedWritesIn(groupPath);
            const QDir groupDir(groupPath);
            for (const QString& child : groupDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                reclaimed += sweepAbandonedWritesIn(groupDir.filePath(child));
            }
        }
    }

    return reclaimed;
}

/// What removing the calibrated copies of the lights a manifest no longer calibrates came to.
struct UncalibratedCopyRemoval
{
    std::size_t removed = 0;
    QStringList failed;
};

/**
 * @brief Remove the calibrated copy of every light @p manifest does not calibrate.
 *
 * A light's registration, alignment, drizzle and stack read its calibrated copy whenever one is on disk, and the run
 * writes one only for a light whose calibration entry calibrates it. A copy an earlier run wrote for a light since
 * marked pre-calibrated, or since left with no master and no pedestal, holds a calibration the project no longer asks
 * for, so it is removed before any stage reads; a later run that calibrates the light again writes it again.
 */
UncalibratedCopyRemoval removeCalibratedCopiesOfUncalibratedLights(const WorkflowManifest& manifest)
{
    UncalibratedCopyRemoval removal;
    if (manifest.outputDirectory.isEmpty())
    {
        return removal;
    }

    std::set<QString> calibrated;
    for (const auto& params : manifest.lightsCalibration)
    {
        if (params.calibrates())
        {
            calibrated.insert(params.lightPath.identityKey());
        }
    }

    std::set<QString> considered;
    const auto consider = [&](const FrameId& light) {
        const QString& key = light.identityKey();
        if (key.isEmpty() || calibrated.count(key) != 0 || !considered.insert(key).second)
        {
            return;
        }
        const QString copy = calibratedLightOutputPathFor(manifest.outputDirectory, light);
        if (!QFileInfo::exists(copy))
        {
            return;
        }
        if (QFile::remove(copy))
        {
            ++removal.removed;
        }
        else
        {
            removal.failed.append(copy);
        }
    };
    for (const auto& params : manifest.lightsCalibration)
    {
        consider(params.lightPath);
    }
    for (const auto& group : manifest.preCalibratedLightGroups)
    {
        for (const auto& light : group.lightFiles)
        {
            consider(light);
        }
    }
    for (const auto& group : manifest.registrationGroups)
    {
        for (const auto& light : group.lightFiles)
        {
            consider(light);
        }
    }
    return removal;
}

class BoundedWorkflowLog
{
public:
    void append(const QString& line)
    {
        if (m_truncated)
        {
            return;
        }
        static const QByteArray marker = QByteArrayLiteral("[AfterNight workflow log truncated at 8388608 bytes]");
        const QByteArray encoded = line.toUtf8();
        const qsizetype separator_bytes = m_bytes.isEmpty() ? 0 : 1;
        const qsizetype content_limit = ImageIntegration::schema::kWorkflowLogMaxBytes - marker.size() - 1;
        if (m_bytes.size() + separator_bytes + encoded.size() <= content_limit)
        {
            if (separator_bytes != 0)
            {
                m_bytes.append('\n');
            }
            m_bytes.append(encoded);
            return;
        }
        if (!m_bytes.isEmpty())
        {
            m_bytes.append('\n');
        }
        m_bytes.append(marker);
        m_truncated = true;
    }

    [[nodiscard]] QString text() const
    {
        return QString::fromUtf8(m_bytes);
    }

private:
    QByteArray m_bytes;
    bool m_truncated = false;
};

[[nodiscard]] qint64 elapsedMilliseconds(const WorkflowMonotonicClock& clock,
                                         const std::chrono::steady_clock::time_point start)
{
    return (
        std::max)(static_cast<qint64>(0),
                  static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(clock() - start).count()));
}

[[nodiscard]] int durationToInt(const qint64 durationMs)
{
    return static_cast<int>((std::min)(durationMs, static_cast<qint64>((std::numeric_limits<int>::max)())));
}

class ExclusivePhaseTimer
{
public:
    explicit ExclusivePhaseTimer(const WorkflowMonotonicClock& clock) : m_clock(clock)
    {
        m_lastBoundary = m_clock();
    }

    [[nodiscard]] qint64 capturePhase()
    {
        const auto current = m_clock();
        const qint64 duration_ms =
            (std::max)(static_cast<qint64>(0),
                       static_cast<qint64>(
                           std::chrono::duration_cast<std::chrono::milliseconds>(current - m_lastBoundary).count()));
        m_lastBoundary = current;
        return duration_ms;
    }

private:
    const WorkflowMonotonicClock& m_clock;
    std::chrono::steady_clock::time_point m_lastBoundary;
};

class ScopedStageExecutionBinding
{
public:
    ScopedStageExecutionBinding(std::vector<std::unique_ptr<IWorkflowStage>>& stages,
                                WorkflowExecutionCounterAccumulator* counters,
                                std::shared_ptr<const ExecutionPreparationSnapshot> snapshot,
                                const std::shared_ptr<WorkflowImageCache>& workflowImageCache,
                                const std::shared_ptr<WorkflowArtifactPublicationLedger>& publicationLedger,
                                const ImageIntegration::WorkflowStageServices* services)
        : m_stages(stages)
    {
        for (const auto& stage : m_stages)
        {
            stage->setExecutionCounterAccumulator(counters);
            stage->setExecutionPreparationSnapshot(snapshot);
            stage->setWorkflowImageCache(workflowImageCache);
            stage->setArtifactPublicationLedger(publicationLedger);
            stage->setStageServices(services);
        }
    }

    ~ScopedStageExecutionBinding()
    {
        for (const auto& stage : m_stages)
        {
            stage->setExecutionCounterAccumulator(nullptr);
            stage->setExecutionPreparationSnapshot(nullptr);
            stage->setWorkflowImageCache(nullptr);
            stage->setArtifactPublicationLedger(nullptr);
            stage->setStageServices(nullptr);
        }
    }

private:
    std::vector<std::unique_ptr<IWorkflowStage>>& m_stages;
};

[[nodiscard]] std::uint64_t workflowCacheCapacity(const WorkflowManifest& manifest)
{
    constexpr std::uint64_t kDefaultCapacityBytes = 32ull * 1024ull * 1024ull;
    constexpr std::uint64_t kBudgetDivisor = 16;
    std::uint64_t capacity = kDefaultCapacityBytes;
    for (const auto& group : manifest.integrationGroups)
    {
        if (!group.stacking.limitMemory || group.stacking.maxMemoryMb <= 0)
        {
            continue;
        }
        const std::uint64_t budget_bytes = static_cast<std::uint64_t>(group.stacking.maxMemoryMb) * 1024ull * 1024ull;
        capacity = (std::min)(capacity, budget_bytes / kBudgetDivisor);
    }
    return capacity;
}

[[nodiscard]] QString stageSequenceToken(const WorkflowStage stage)
{
    switch (stage)
    {
    case WorkflowStage::Bias:
        return QStringLiteral("Bias");
    case WorkflowStage::Dark:
        return QStringLiteral("Dark");
    case WorkflowStage::Flat:
        return QStringLiteral("Flat");
    case WorkflowStage::LightsCalibration:
        return QStringLiteral("LightsCalibration");
    case WorkflowStage::LightsRegistration:
        return QStringLiteral("LightsRegistration");
    case WorkflowStage::LightsAlignment:
        return QStringLiteral("LightsAlignment");
    case WorkflowStage::LightsNormalization:
        return QStringLiteral("LightsNormalization");
    case WorkflowStage::LightsDrizzle:
        return QStringLiteral("LightsDrizzle");
    case WorkflowStage::LightsStacking:
        return QStringLiteral("LightsStacking");
    case WorkflowStage::None:
    default:
        return {};
    }
}

[[nodiscard]] bool isDrizzleEnabled(const WorkflowManifest::IntegrationGroup& group)
{
    return group.drizzle.has_value() && group.drizzle->enabled;
}

[[nodiscard]] bool cometNormalizationRequiresRegisteredOutputs(const WorkflowManifest::IntegrationGroup& group)
{
    if (!ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode) ||
        group.stacking.normalizationMode == NormalizationMode::None)
    {
        return false;
    }

    const bool pure_comet_local_drizzle = isDrizzleEnabled(group) &&
                                          group.stacking.normalizationMode == NormalizationMode::Local &&
                                          group.stacking.localNormalization.enabled &&
                                          ImageIntegration::usesCometOnlyStackingMode(group.stacking.cometStackingMode);
    return !pure_comet_local_drizzle;
}

[[nodiscard]] bool isNonDrizzleCometStackingGroup(const WorkflowManifest::IntegrationGroup& group)
{
    return !isDrizzleEnabled(group) && ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode);
}

[[nodiscard]] bool hasNonDrizzleCometStackingGroup(const WorkflowManifest& manifest)
{
    return std::any_of(
        manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
        [](const WorkflowManifest::IntegrationGroup& group) { return isNonDrizzleCometStackingGroup(group); });
}

[[nodiscard]] bool hasCometNormalizationGroupRequiringRegisteredOutputs(const WorkflowManifest& manifest)
{
    return std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                       [](const WorkflowManifest::IntegrationGroup& group) {
                           return cometNormalizationRequiresRegisteredOutputs(group);
                       });
}

void ensureStageBefore(QStringList& sequence, const QString& stage, const QString& before)
{
    sequence.removeAll(stage);
    const int before_index = sequence.indexOf(before);
    if (before_index >= 0)
    {
        sequence.insert(before_index, stage);
        return;
    }

    sequence.append(stage);
}

void ensureCometStackingStageDependencies(WorkflowManifest& manifest)
{
    if (manifest.stageSequence.isEmpty())
    {
        return;
    }

    const bool normalization_consumes_comet_outputs =
        manifest.stageSequence.contains(QStringLiteral("LightsNormalization")) &&
        hasCometNormalizationGroupRequiringRegisteredOutputs(manifest);
    const bool stacking_consumes_comet_outputs =
        manifest.stageSequence.contains(QStringLiteral("LightsStacking")) && hasNonDrizzleCometStackingGroup(manifest);
    if (!normalization_consumes_comet_outputs && !stacking_consumes_comet_outputs)
    {
        return;
    }

    ensureStageBefore(manifest.stageSequence, QStringLiteral("LightsAlignment"),
                      normalization_consumes_comet_outputs ? QStringLiteral("LightsNormalization")
                                                           : QStringLiteral("LightsStacking"));
}

[[nodiscard]] QString resolvedCalibrationGroupPath(const WorkflowManifest& manifest,
                                                   const WorkflowManifest::CalibrationStage::Group& group)
{
    if (group.existingMaster.has_value())
    {
        return group.existingMaster->canonicalPath();
    }

    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return group.outputFilename;
    }

    return QFileInfo(QDir(manifest.outputDirectory).filePath(group.outputFilename)).absoluteFilePath();
}

[[nodiscard]] std::optional<WorkflowManifest::CalibrationStage>
filterCalibrationStage(const WorkflowManifest& manifest, const std::optional<WorkflowManifest::CalibrationStage>& stage,
                       const std::set<QString>& usedMasterPaths)
{
    if (!stage.has_value())
    {
        return std::nullopt;
    }

    WorkflowManifest::CalibrationStage filtered = *stage;
    std::erase_if(filtered.groups, [&](const WorkflowManifest::CalibrationStage::Group& group) {
        return usedMasterPaths.find(resolvedCalibrationGroupPath(manifest, group)) == usedMasterPaths.end();
    });

    if (filtered.groups.empty())
    {
        return std::nullopt;
    }

    return filtered;
}

[[nodiscard]] QStringList buildBaselineStageSequence(const WorkflowManifest& manifest)
{
    QStringList sequence;
    if (manifest.biasStage.has_value() && !manifest.biasStage->groups.empty())
    {
        sequence << QStringLiteral("Bias");
    }
    if (manifest.darkStage.has_value() && !manifest.darkStage->groups.empty())
    {
        sequence << QStringLiteral("Dark");
    }
    if (manifest.flatStage.has_value() && !manifest.flatStage->groups.empty())
    {
        sequence << QStringLiteral("Flat");
    }
    if (!manifest.lightsCalibration.empty() && !manifest.calibrateLightsInMemory)
    {
        sequence << QStringLiteral("LightsCalibration");
    }
    if (!manifest.registrationGroups.empty())
    {
        sequence << QStringLiteral("LightsRegistration");
    }

    const bool hasNonDrizzleGroups = std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                                                 [](const WorkflowManifest::IntegrationGroup& group) {
                                                     return !group.drizzle.has_value() || !group.drizzle->enabled;
                                                 });
    if (!manifest.integrationGroups.empty() && hasNonDrizzleGroups)
    {
        sequence << QStringLiteral("LightsAlignment");
    }

    const bool hasNormalization = std::any_of(
        manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
        [](const WorkflowManifest::IntegrationGroup& group) { return !group.normalizationReferenceFrames.empty(); });
    if (hasNormalization)
    {
        sequence << QStringLiteral("LightsNormalization");
    }

    const bool hasDrizzle = std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                                        [](const WorkflowManifest::IntegrationGroup& group) {
                                            return group.drizzle.has_value() && group.drizzle->enabled;
                                        });
    if (hasDrizzle)
    {
        sequence << QStringLiteral("LightsDrizzle");
    }

    if (!manifest.integrationGroups.empty())
    {
        sequence << QStringLiteral("LightsStacking");
    }

    return sequence;
}

void clearIncrementalSelections(WorkflowManifest& manifest)
{
    manifest.dirtyGroups.clear();
    manifest.incrementalBiasGroups.clear();
    manifest.incrementalDarkGroups.clear();
    manifest.incrementalFlatGroups.clear();
    manifest.incrementalRegistrationGroups.clear();
    manifest.incrementalIntegrationGroups.clear();
}

/// Adds a reused group to a run's list once, by its key.
void appendReusedGroup(std::vector<ReusedIntegrationGroup>& groups, const ReusedIntegrationGroup& group)
{
    const bool listed = std::any_of(groups.begin(), groups.end(), [&group](const ReusedIntegrationGroup& known) {
        return known.groupKey == group.groupKey;
    });
    if (!listed)
    {
        groups.push_back(group);
    }
}

/// The integration groups an incremental run's plan leaves out: the plan found each one's master still valid, so no
/// stage visits the group and the run makes nothing of it.
[[nodiscard]] std::vector<ReusedIntegrationGroup> groupsThePlanLeavesAsTheyAre(const WorkflowManifest& manifest)
{
    std::vector<ReusedIntegrationGroup> groups;
    // A run that only registers stacks nothing, so it reuses no master either.
    const bool registersOnly =
        manifest.executionScope.trimmed().startsWith(QStringLiteral("registration"), Qt::CaseInsensitive);
    if (manifest.executionMode != ExecutionMode::Incremental || registersOnly)
    {
        return groups;
    }
    for (const auto& group : manifest.integrationGroups)
    {
        if (std::find(manifest.incrementalIntegrationGroups.begin(), manifest.incrementalIntegrationGroups.end(),
                      group.groupKey) != manifest.incrementalIntegrationGroups.end())
        {
            continue;
        }
        appendReusedGroup(groups, ReusedIntegrationGroup{
                                      .groupKey = group.groupKey.toString(),
                                      .outputPath = QDir(manifest.outputDirectory).filePath(group.outputFilename)});
    }
    return groups;
}

[[nodiscard]] std::optional<WorkflowManifest> buildCommittedSubsetManifest(const WorkflowManifest& manifest,
                                                                           const WorkflowExecutor::Result& result)
{
    std::set<QString> successfulIntegrationGroupKeys;
    for (const auto& groupResult : result.integrationGroupResults)
    {
        if (groupResult.success)
        {
            successfulIntegrationGroupKeys.insert(groupResult.groupKey);
        }
    }

    if (successfulIntegrationGroupKeys.empty())
    {
        return std::nullopt;
    }

    WorkflowManifest subset = manifest;

    std::set<FrameId> retainedLights;
    std::set<GroupKey> retainedRegistrationGroups;
    std::erase_if(subset.integrationGroups, [&](const WorkflowManifest::IntegrationGroup& group) {
        const bool keep =
            successfulIntegrationGroupKeys.find(group.groupKey.toString()) != successfulIntegrationGroupKeys.end();
        if (keep)
        {
            retainedLights.insert(group.lightFiles.begin(), group.lightFiles.end());
            retainedRegistrationGroups.insert(group.registrationGroupKey);
        }
        return !keep;
    });

    std::erase_if(subset.registrationGroups, [&](const WorkflowManifest::RegistrationGroup& group) {
        return retainedRegistrationGroups.find(group.groupKey) == retainedRegistrationGroups.end();
    });

    std::set<QString> usedMasterPaths;
    std::erase_if(subset.lightsCalibration, [&](const WorkflowManifest::LightsCalibrationParams& params) {
        const bool keep = retainedLights.find(params.lightPath) != retainedLights.end();
        if (keep)
        {
            if (params.biasPath.has_value())
            {
                usedMasterPaths.insert(params.biasPath->canonicalPath());
            }
            if (params.darkPath.has_value())
            {
                usedMasterPaths.insert(params.darkPath->canonicalPath());
            }
            if (params.flatPath.has_value())
            {
                usedMasterPaths.insert(params.flatPath->canonicalPath());
            }
        }
        return !keep;
    });

    subset.biasStage = filterCalibrationStage(manifest, subset.biasStage, usedMasterPaths);
    subset.darkStage = filterCalibrationStage(manifest, subset.darkStage, usedMasterPaths);
    subset.flatStage = filterCalibrationStage(manifest, subset.flatStage, usedMasterPaths);

    clearIncrementalSelections(subset);
    subset.stageSequence = buildBaselineStageSequence(subset);
    return subset;
}

/// A run that commits nothing beyond its run state, for an executor given no archive.
class NoWorkflowRunArchive final : public IWorkflowRunArchive
{
public:
    CommitOutcome commit(const WorkflowManifest&, WorkflowExecutor::Result&, const CapturePhaseDuration&) override
    {
        CommitOutcome outcome;
        outcome.success = true;
        return outcome;
    }

    void revertStagedFiles(const WorkflowExecutor::Result&) override {}
    void finalizeTimingArtifacts(const WorkflowExecutor::Result&) override {}
    void finalizeLogArtifact(const WorkflowExecutor::Result&) override {}
};

/// The registry a run reads through when its composer names no factory: the product's, its camera RAW adapter built
/// with the run's treatment.
[[nodiscard]] std::shared_ptr<const an::CodecRegistry>
productRegistryFor(const ImageIntegration::WorkflowRawPolicy& policy)
{
    auto registry = an::ui::createProductCodecRegistry(ImageIntegration::workflowRawCodecOptions(policy));
    if (!registry.hasValue())
    {
        LOG_WARNING("WorkflowExecutor: the run's codec registry could not be built: {}", registry.status().message());
        return {};
    }
    return std::move(registry).value();
}

/// What the session's engine context's memory resource counts now, or nothing when there is no session, its context
/// is not open, or the context counts nothing.
[[nodiscard]] std::optional<an::MemoryCounters> engineMemoryCounters(const an::consumer::EngineSession* session)
{
    if (session == nullptr)
    {
        return std::nullopt;
    }
    const an::Result<an::consumer::SessionHold> hold = session->hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const an::MemoryResource* resource = hold->engine().memoryResource();
    if (resource == nullptr)
    {
        return std::nullopt;
    }
    return resource->counters();
}

/// The engine's memory over a run, from its counters when the run started and when it ended.
[[nodiscard]] WorkflowExecutor::Result::EngineMemory engineMemoryOverRun(const an::MemoryCounters& before,
                                                                         const an::MemoryCounters& after)
{
    WorkflowExecutor::Result::EngineMemory memory;
    memory.peakBytes = after.peakBytes;
    memory.peakBytesBeforeRun = before.peakBytes;
    memory.liveBytesAtEnd = after.liveBytes;
    memory.allocations = after.totalAllocations - before.totalAllocations;
    memory.bytesAllocated = after.totalBytes - before.totalBytes;
    memory.deniedAllocations = after.deniedAllocations - before.deniedAllocations;
    return memory;
}

/// One line for the run's log and the application's: the engine's memory over the run, in bytes.
[[nodiscard]] QString describeEngineMemory(const WorkflowExecutor::Result::EngineMemory& memory)
{
    return QStringLiteral("Engine memory: peak %1 bytes (%2 when the run started), %3 bytes still allocated, %4 "
                          "allocations of %5 bytes, %6 refused")
        .arg(memory.peakBytes)
        .arg(memory.peakBytesBeforeRun)
        .arg(memory.liveBytesAtEnd)
        .arg(memory.allocations)
        .arg(memory.bytesAllocated)
        .arg(memory.deniedAllocations);
}
} // namespace

class WorkflowExecutor::Impl
{
public:
    ProgressCallback progressCallback;
    LogCallback logCallback;
    std::vector<std::unique_ptr<IWorkflowStage>> stages;
    CancellationToken cancellationToken;
    std::chrono::milliseconds progressInterval{50};
    WorkflowMonotonicClock monotonicClock = []() { return std::chrono::steady_clock::now(); };
    WorkflowRunArchiveFactory archiveFactory;
    WorkflowCodecRegistryFactory registryFactory;
    /// The run in flight's stop, so a cancellation requested while it runs stops its engine work too.
    std::mutex stopMutex;
    std::stop_source runStop;
};

WorkflowExecutor::WorkflowExecutor(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {}

WorkflowExecutor::WorkflowExecutor(WorkflowMonotonicClock monotonicClock, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    if (monotonicClock)
    {
        m_impl->monotonicClock = std::move(monotonicClock);
    }
}

WorkflowExecutor::~WorkflowExecutor() = default;

void WorkflowExecutor::setProgressCallback(ProgressCallback callback)
{
    m_impl->progressCallback = std::move(callback);
}

void WorkflowExecutor::setLogCallback(LogCallback callback)
{
    m_impl->logCallback = std::move(callback);
}

void WorkflowExecutor::addStage(std::unique_ptr<IWorkflowStage> stage)
{
    if (stage)
    {
        m_impl->stages.push_back(std::move(stage));
    }
}

void WorkflowExecutor::clearStages()
{
    m_impl->stages.clear();
}

void WorkflowExecutor::setRunArchiveFactory(WorkflowRunArchiveFactory factory)
{
    m_impl->archiveFactory = std::move(factory);
}

void WorkflowExecutor::setCodecRegistryFactory(WorkflowCodecRegistryFactory factory)
{
    m_impl->registryFactory = std::move(factory);
}

void WorkflowExecutor::requestCancel()
{
    m_impl->cancellationToken.requestCancel();
    std::lock_guard lock(m_impl->stopMutex);
    m_impl->runStop.request_stop();
}

bool WorkflowExecutor::isCancelRequested() const
{
    return m_impl->cancellationToken.isCancellationRequested();
}

WorkflowExecutor::Result WorkflowExecutor::execute(const WorkflowManifest& incoming,
                                                   WorkflowPreparationTiming preparationTiming,
                                                   WorkflowExecutionOptions options)
{
    // Stamp a unique execution ID so all groups produced by this workflow run
    // can be identified together in run manifests and the history UI.
    // Use timestamp-based ID for better sorting and readability in the history folder. The history is ordered by
    // these names, so the id is never at or before the latest run already there, whatever the clock says: a clock
    // that went back an hour would otherwise put this run before the one it follows.
    WorkflowManifest manifest = incoming;
    if (manifest.executionId.isEmpty())
    {
        manifest.executionId =
            ImageIntegration::executionIdAfterHistory(manifest.outputDirectory, QDateTime::currentDateTime());
    }
    ensureCometStackingStageDependencies(manifest);
    // The parallel-cores setting narrows each stage operation the run admits; it caps nothing outside the run.
    const int requested_parallel_cores = manifest.maxParallelCores;
    const int resolved_parallel_cores = ImageIntegration::core::clampParallelCoreLimit(requested_parallel_cores);
    const std::uint32_t stage_width = ImageIntegration::core::parallelCoreWidth(requested_parallel_cores);
    std::stop_token run_stop;
    {
        std::lock_guard lock(m_impl->stopMutex);
        m_impl->runStop = std::stop_source();
        if (m_impl->cancellationToken.isCancellationRequested())
        {
            m_impl->runStop.request_stop();
        }
        run_stop = m_impl->runStop.get_token();
    }

    Result result;
    result.preparationTiming = preparationTiming;
    result.success = true;
    std::optional<WorkflowExecutionCounterAccumulator> counterAccumulator;
    if (options.collectCounters)
    {
        counterAccumulator.emplace();
    }
    auto workflow_image_cache = std::make_shared<WorkflowImageCache>(
        options.workflowCacheCapacityBytes.value_or(workflowCacheCapacity(manifest)),
        WorkflowImageCache::SameKeyWaitObserver{}, options.session != nullptr ? &options.session->memory() : nullptr);
    const auto captureCounters = [&]() {
        workflow_image_cache->clear();
        if (counterAccumulator.has_value())
        {
            result.executionCounters = counterAccumulator->snapshot();
            result.workflowCacheDiagnostics = workflow_image_cache->diagnostics();
        }
    };

    ExclusivePhaseTimer timer(m_impl->monotonicClock);
    result.executorTiming.captured = true;

    if (m_impl->cancellationToken.consumeCancellationRequest())
    {
        result.success = false;
        result.cancelled = true;
        result.errorMessage = QStringLiteral("Cancelled.");
        result.executorTiming.stageExecutionMs = timer.capturePhase();
        result.totalDurationMs = durationToInt(result.executorTiming.exclusivePhasesTotalMs());
        captureCounters();
        return result;
    }

    if (options.preparationSnapshot && !options.preparationSnapshot->matchesCurrentFacts(
                                           [this]() { return m_impl->cancellationToken.isCancellationRequested(); }))
    {
        result.success = false;
        if (m_impl->cancellationToken.consumeCancellationRequest())
        {
            result.cancelled = true;
            result.errorMessage = QStringLiteral("Cancelled.");
        }
        else
        {
            result.errorMessage = QStringLiteral(
                "Workflow inputs or persisted state changed after the execution plan was prepared. Review the plan "
                "and run again.");
            result.errors << result.errorMessage;
        }
        result.executorTiming.stageExecutionMs = timer.capturePhase();
        result.totalDurationMs = durationToInt(result.executorTiming.exclusivePhasesTotalMs());
        captureCounters();
        return result;
    }

    // The run's RAW treatment is a snapshot taken now and read through a registry of the run's own; the application's
    // settings and every other reader's registry are untouched by it.
    const ImageIntegration::WorkflowRawPolicy raw_policy = ImageIntegration::workflowRawPolicy();
    std::optional<ImageIntegration::WorkflowStageServices> stage_services;
    const std::optional<an::MemoryCounters> engine_memory_before = engineMemoryCounters(options.session);
    if (options.session != nullptr)
    {
        stage_services.emplace(
            *options.session, stage_width, raw_policy,
            m_impl->registryFactory ? m_impl->registryFactory(raw_policy) : productRegistryFor(raw_policy), run_stop);
    }

    const auto publication_ledger = std::make_shared<WorkflowArtifactPublicationLedger>();
    ScopedStageExecutionBinding stageExecutionBinding(
        m_impl->stages, counterAccumulator.has_value() ? &*counterAccumulator : nullptr,
        std::move(options.preparationSnapshot), workflow_image_cache, publication_ledger,
        stage_services.has_value() ? &*stage_services : nullptr);

    // Capture logs for archiving
    BoundedWorkflowLog logBuffer;

    // Setup logger for executor's own messages
    auto logCallback = [this, &logBuffer](LogCategory cat, const QString& msg) {
        WorkflowLogLevel level = WorkflowLogLevel::Info;
        QString levelStr = QStringLiteral("INFO");
        switch (cat)
        {
        case LogCategory::Header:
            level = WorkflowLogLevel::Header;
            levelStr = "HEADER";
            break;
        case LogCategory::Progress:
            level = WorkflowLogLevel::Progress;
            levelStr = "PROGRESS";
            break;
        case LogCategory::Detail:
            level = WorkflowLogLevel::Detail;
            levelStr = "DETAIL";
            break;
        case LogCategory::Success:
            level = WorkflowLogLevel::Success;
            levelStr = "SUCCESS";
            break;
        case LogCategory::Warning:
            level = WorkflowLogLevel::Warning;
            levelStr = "WARNING";
            break;
        case LogCategory::Error:
            level = WorkflowLogLevel::Error;
            levelStr = "ERROR";
            break;
        default:
            level = WorkflowLogLevel::Info;
            break;
        }

        // Format for log file: [TIMESTAMP] [LEVEL] Message
        QString logLine =
            QStringLiteral("[%1] [%2] %3").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), levelStr, msg);
        logBuffer.append(logLine);

        if (m_impl->logCallback)
        {
            m_impl->logCallback(level, msg);
        }
        emit logMessage(level, msg);
    };

    WorkflowLogger executorLogger(logCallback);

    const WorkflowValidationResult validation =
        manifest.validate(options.session != nullptr ? ImageIntegration::sessionFrameHeaderReader(*options.session)
                                                     : ImageIntegration::FrameHeaderReader{});
    if (validation.hasErrors())
    {
        result.success = false;
        if (!validation.issues.empty())
        {
            result.errorMessage = validation.issues.front().message;
        }
        else
        {
            result.errorMessage = QStringLiteral("Invalid manifest.");
        }
        executorLogger.error(result.errorMessage);
        result.executorTiming.stageExecutionMs = timer.capturePhase();
        result.totalDurationMs = durationToInt(result.executorTiming.exclusivePhasesTotalMs());
        captureCounters();
        return result;
    }

    if (m_impl->cancellationToken.consumeCancellationRequest())
    {
        result.success = false;
        result.cancelled = true;
        result.errorMessage = QStringLiteral("Cancelled.");
        result.executorTiming.stageExecutionMs = timer.capturePhase();
        result.totalDurationMs = durationToInt(result.executorTiming.exclusivePhasesTotalMs());
        captureCounters();
        return result;
    }

    ProgressReporter reporter;
    reporter.setMinimumInterval(m_impl->progressInterval);
    reporter.setCallback([this](const Progress& progress) {
        if (m_impl->progressCallback)
        {
            m_impl->progressCallback(progress);
        }
        emit progressUpdated(progress);
    });
    reporter.setLogCallback(logCallback);

    // Log workflow start with high-level context
    const int stageCount = static_cast<int>(m_impl->stages.size());
    executorLogger.workflowStarted(manifest.outputDirectory, stageCount);

    // Reclaim payloads an interrupted run left mid-write. Committed artifacts are never touched:
    // only files still carrying the pending-write suffix are removed, and a file reaches its final
    // name only once complete. See docs/dev/IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md.
    if (const std::size_t reclaimed = sweepAbandonedWrites(manifest.outputDirectory); reclaimed > 0)
    {
        executorLogger.settingsApplied(QStringLiteral("Reclaimed incomplete outputs"),
                                       QStringLiteral("%1 file(s) from an interrupted run").arg(reclaimed));
    }

    const UncalibratedCopyRemoval uncalibrated_copies = removeCalibratedCopiesOfUncalibratedLights(manifest);
    if (uncalibrated_copies.removed > 0)
    {
        executorLogger.settingsApplied(
            QStringLiteral("Removed calibrated copies"),
            QStringLiteral("%1 light(s) no longer calibrated; their lights are read as they are")
                .arg(uncalibrated_copies.removed));
    }
    if (!uncalibrated_copies.failed.isEmpty() && manifest.calibrateLightsInMemory)
    {
        // A run that calibrates in memory reads no calibrated copy, so a copy another program holds open is left for a
        // later run to remove, and the run goes on with the lights as they are.
        const QString warning =
            QStringLiteral(
                "%1 calibrated copy(ies) of lights the project no longer calibrates could not be removed and "
                "were left; this run calibrates in memory and does not read them: %2")
                .arg(uncalibrated_copies.failed.size())
                .arg(uncalibrated_copies.failed.front());
        executorLogger.warning(warning);
        result.warnings.append(warning);
    }
    else if (!uncalibrated_copies.failed.isEmpty())
    {
        result.success = false;
        result.errorMessage =
            QStringLiteral("A calibrated copy of a light the project no longer calibrates could not be removed: %1")
                .arg(uncalibrated_copies.failed.front());
        executorLogger.error(result.errorMessage);
        result.executorTiming.stageExecutionMs = timer.capturePhase();
        result.totalDurationMs = durationToInt(result.executorTiming.exclusivePhasesTotalMs());
        captureCounters();
        return result;
    }

    // Log key settings that affect workflow behavior
    executorLogger.settingsApplied(QStringLiteral("Reprocess existing"), manifest.reprocessExisting
                                                                             ? QStringLiteral("Yes")
                                                                             : QStringLiteral("No (incremental)"));
    executorLogger.settingsApplied(QStringLiteral("In-memory calibration"),
                                   manifest.calibrateLightsInMemory ? QStringLiteral("Yes") : QStringLiteral("No"));
    executorLogger.settingsApplied(QStringLiteral("Parallel threads"),
                                   requested_parallel_cores > 0
                                       ? QStringLiteral("%1 of %2")
                                             .arg(resolved_parallel_cores)
                                             .arg(ImageIntegration::core::detectedParallelCoreCount())
                                       : QStringLiteral("All available (%1)").arg(resolved_parallel_cores));
    executorLogger.settingsApplied(QStringLiteral("RAW white balance"),
                                   ImageIntegration::workflowRawLoadPolicyDescription());
    if (options.session == nullptr)
    {
        executorLogger.warning(QStringLiteral("No engine session was given; the stages run with no engine services."));
    }
    executorLogger.settingsApplied(QStringLiteral("Combine RGB images"),
                                   manifest.combineRgbImages ? QStringLiteral("Yes") : QStringLiteral("No"));
    executorLogger.settingsApplied(QStringLiteral("Execution ID"), manifest.executionId);

    // What the plan leaves as it is is known before any stage runs, and holds however the run ends.
    result.reusedIntegrationGroups = groupsThePlanLeavesAsTheyAre(manifest);

    for (const auto& stage : m_impl->stages)
    {
        if (m_impl->cancellationToken.isCancellationRequested())
        {
            result.success = false;
            result.cancelled = true;
            result.errorMessage = QStringLiteral("Cancelled.");
            executorLogger.workflowCancelled();
            break;
        }

        const QString selectedStageToken = stageSequenceToken(stage->stageId());
        if (!manifest.stageSequence.isEmpty() && !selectedStageToken.isEmpty() &&
            !manifest.stageSequence.contains(selectedStageToken))
        {
            executorLogger.stageSkipped(stage->name(), QStringLiteral("Not selected for this execution."));
            continue;
        }

        if (stage->shouldSkip(manifest))
        {
            executorLogger.stageSkipped(stage->name(), stage->skipReason(manifest));
            continue;
        }

        const auto stage_start = m_impl->monotonicClock();
        StageResult stage_result = stage->execute(manifest, m_impl->cancellationToken, reporter);
        if (counterAccumulator.has_value() && stage_result.engineExecutionCounters.has_value())
        {
            const StackExecutionCounters& counters = *stage_result.engineExecutionCounters;
            counterAccumulator->addInputDecodeOperations(counters.inputDecodeOperations);
            counterAccumulator->addScientificOutputEncodeCompletions(counters.scientificOutputEncodeCompletions);
            counterAccumulator->addInputPassesStarted(counters.inputPassesStarted);
            counterAccumulator->addTilePassesStarted(counters.tilePassesStarted);
            counterAccumulator->addStackLoaderRequests(counters.stackLoaderRequests);
            counterAccumulator->addRoiSubsetReads(counters.roiSubsetReads);
            counterAccumulator->addFullFrameFallbackReads(counters.fullFrameFallbackReads);
            counterAccumulator->addLogicalDecodedPixelBytes(counters.logicalDecodedPixelBytes);
            counterAccumulator->addCalibrationInvocations(counters.calibrationInvocations);
            counterAccumulator->addCalibratedSpillFilesCreated(counters.calibratedSpillFilesCreated);
            counterAccumulator->addCalibratedSpillBytesWritten(counters.calibratedSpillBytesWritten);
            counterAccumulator->addCalibratedSpillBytesRead(counters.calibratedSpillBytesRead);
            counterAccumulator->addCalibratedSpillRoiReads(counters.calibratedSpillRoiReads);
            counterAccumulator->addCalibratedSpillCleanupSuccesses(counters.calibratedSpillCleanupSuccesses);
            counterAccumulator->addCalibratedSpillCleanupFailures(counters.calibratedSpillCleanupFailures);
            counterAccumulator->addDumpBytesRead(counters.dumpBytesRead);
            counterAccumulator->addDumpBytesWritten(counters.dumpBytesWritten);
            counterAccumulator->addPixmapBytesRead(counters.pixmapBytesRead);
            counterAccumulator->addPixmapBytesWritten(counters.pixmapBytesWritten);
            counterAccumulator->addDescriptorProviderHits(counters.descriptorProviderHits);
            counterAccumulator->addDenseProviderHits(counters.denseProviderHits);
            counterAccumulator->addSipLazyProviderHits(counters.sipLazyProviderHits);
            counterAccumulator->addPolynomialLazyProviderHits(counters.polynomialLazyProviderHits);
            counterAccumulator->addSipCoordinatesEvaluated(counters.sipCoordinatesEvaluated);
            counterAccumulator->addSharedSipPowerTableBuilds(counters.sharedSipPowerTableBuilds);
            counterAccumulator->addExplicitSipInverseEvaluations(counters.explicitSipInverseEvaluations);
            counterAccumulator->addSipNewtonFallbackIterations(counters.sipNewtonFallbackIterations);
            counterAccumulator->addPolynomialCoordinatesEvaluated(counters.polynomialCoordinatesEvaluated);
            counterAccumulator->addSharedPolynomialPowerTableBuilds(counters.sharedPolynomialPowerTableBuilds);
            counterAccumulator->addLazyBlocksGenerated(counters.lazyBlocksGenerated);
            counterAccumulator->addLazyCoordinatePairsGenerated(counters.lazyCoordinatePairsGenerated);
            counterAccumulator->updatePeakLazyCoordinateBytes(counters.peakLazyCoordinateBytes);
            counterAccumulator->addDenseFallbackCount(counters.denseFallbackCount);
            counterAccumulator->addLazyMapCancellations(counters.lazyMapCancellations);
            counterAccumulator->addLazyMapFailures(counters.lazyMapFailures);
        }
        const int stage_ms = durationToInt(elapsedMilliseconds(m_impl->monotonicClock, stage_start));

        // A warning two stages give in the same words - each stage that calibrates the lights in memory says what their
        // calibration fell back to - is stated once.
        for (const QString& warning : stage_result.warnings)
        {
            if (!result.warnings.contains(warning))
            {
                result.warnings.append(warning);
            }
        }

        // Aggregate stage result
        Result::StageTiming timing;
        timing.stageName = stage->name();
        int nested_phase_ms = 0;
        for (const auto& phase : stage_result.phaseTimings)
        {
            nested_phase_ms += (std::max)(0, phase.durationMs);
        }
        timing.durationMs = (std::max)(0, stage_ms - nested_phase_ms);
        timing.groupsProcessed =
            static_cast<int>(stage_result.groupResults.size()); // Approx count based on results returned
        if (stage_result.status == StageResult::Status::Success)
            timing.status = "completed";
        else if (stage_result.status == StageResult::Status::PartialSuccess)
            timing.status = "partial";
        else if (stage_result.status == StageResult::Status::Failure)
            timing.status = "failed";
        else if (stage_result.status == StageResult::Status::Cancelled)
            timing.status = "cancelled";
        else
            timing.status = "unknown";

        result.stageResults.push_back(timing);
        for (const auto& phase : stage_result.phaseTimings)
        {
            Result::StageTiming phase_timing;
            phase_timing.stageName = phase.stageName;
            phase_timing.status = phase.status;
            phase_timing.durationMs = phase.durationMs;
            phase_timing.groupsProcessed = phase.groupsProcessed;
            result.stageResults.push_back(std::move(phase_timing));
        }

        const auto appendCalibrationGroups =
            [&](const QString& type, const std::optional<WorkflowManifest::CalibrationStage>& calibrationStage,
                const std::vector<GroupKey>& plannedGroups, QStringList& generatedMasters) {
                if (!calibrationStage.has_value())
                {
                    return;
                }

                for (const auto& group : calibrationStage->groups)
                {
                    // An incremental run's stage visits only the groups its plan names: the plan found every other
                    // group's master still valid, so the run made nothing of it.
                    const bool leftOutByThePlan =
                        manifest.executionMode == ExecutionMode::Incremental &&
                        std::find(plannedGroups.begin(), plannedGroups.end(), group.groupKey) == plannedGroups.end();
                    const QString masterPath = resolvedCalibrationGroupPath(manifest, group);
                    const bool exists = QFileInfo::exists(masterPath);

                    Result::CalibrationGroupResult calibrationResult;
                    calibrationResult.type = type;
                    calibrationResult.groupKey = group.groupKey.toString();
                    calibrationResult.masterPath = masterPath;
                    calibrationResult.inputCount = static_cast<int>(group.inputFiles.size());
                    calibrationResult.success = stage_result.isSuccess() && exists;
                    calibrationResult.reused =
                        leftOutByThePlan ||
                        std::find(stage_result.reusedCalibrationGroups.begin(),
                                  stage_result.reusedCalibrationGroups.end(),
                                  calibrationResult.groupKey) != stage_result.reusedCalibrationGroups.end();
                    result.calibrationGroupResults.push_back(std::move(calibrationResult));

                    if (exists)
                    {
                        generatedMasters.append(masterPath);
                    }
                }
            };

        if (stage_result.status != StageResult::Status::Skipped &&
            stage_result.status != StageResult::Status::Cancelled)
        {
            switch (stage->stageId())
            {
            case WorkflowStage::Bias:
                appendCalibrationGroups(QStringLiteral("Bias"), manifest.biasStage, manifest.incrementalBiasGroups,
                                        result.generatedBiasMasters);
                break;
            case WorkflowStage::Dark:
                appendCalibrationGroups(QStringLiteral("Dark"), manifest.darkStage, manifest.incrementalDarkGroups,
                                        result.generatedDarkMasters);
                break;
            case WorkflowStage::Flat:
                appendCalibrationGroups(QStringLiteral("Flat"), manifest.flatStage, manifest.incrementalFlatGroups,
                                        result.generatedFlatMasters);
                break;
            default:
                break;
            }
        }

        // Aggregate integration group results
        for (const auto& gr : stage_result.groupResults)
        {
            result.integrationGroupResults.push_back(gr);
            if (gr.success && stage->stageId() == WorkflowStage::LightsStacking)
            {
                result.generatedLightMasters.append(gr.outputPath);
                for (const auto& rejectionMap : gr.rejectionMaps)
                {
                    if (!rejectionMap.path.trimmed().isEmpty())
                    {
                        result.generatedRejectionMaps.append(rejectionMap.path);
                    }
                }
            }
        }

        // The groups the stage found up to date, which have no result.
        for (const auto& reused : stage_result.reusedGroups)
        {
            appendReusedGroup(result.reusedIntegrationGroups, reused);
        }

        // The groups whose stack failed before a master was written, which have no result either.
        for (const auto& failed : stage_result.failedGroups)
        {
            result.failedIntegrationGroups.push_back(failed);
        }

        // The lights the registration found no transform for, each with its reason.
        for (const auto& failure : stage_result.registrationFailures)
        {
            result.registrationFailures.push_back(failure);
        }

        // Aggregate alignment group results (SIP info)
        for (const auto& ar : stage_result.alignmentGroupResults)
        {
            result.alignmentGroupResults.push_back(ar);
        }

        if (stage_result.status == StageResult::Status::Cancelled ||
            m_impl->cancellationToken.isCancellationRequested())
        {
            result.success = false;
            result.cancelled = true;
            result.errorMessage = QStringLiteral("Cancelled.");
            break;
        }

        // Collect per-frame errors from partial successes
        if (stage_result.isPartialSuccess())
        {
            result.partialSuccess = true;
            for (const auto& err : stage_result.errorReport.errors)
            {
                const QString errMsg = QStringLiteral("[%1] %2").arg(stage->name(), err.toUserString());
                result.errors.append(errMsg);
                LOG_WARNING("Stage '{}' partial failure: {}", stage->name().toStdString(),
                            err.toLogString().toStdString());
            }
            executorLogger.warning(QStringLiteral("%1: %2 frame(s)/group(s) failed, continuing with remaining")
                                       .arg(stage->name())
                                       .arg(stage_result.errorReport.errors.size()));
        }

        if (stage_result.status == StageResult::Status::Failure)
        {
            result.success = false;
            // Use the user-friendly error for dialogs (includes details but not error codes)
            result.errorMessage = stage_result.error.toUserString();

            // Log to file for debugging (with full details including error code)
            LOG_ERROR("Stage '{}' failed: {}", stage->name().toStdString(),
                      stage_result.error.toLogString().toStdString());

            // Show in workflow panel - same user-friendly format
            executorLogger.stageFailed(stage->name(), result.errorMessage);
            break;
        }

        if (stage_result.status == StageResult::Status::Skipped)
        {
            executorLogger.stageSkipped(stage->name(), "Internal check.");
        }
    }

    result.warningCount = result.warnings.size();
    result.capturedLog = logBuffer.text();

    std::optional<WorkflowRunStateStore::RunMetadata> runMetadata;
    const auto refreshTotalDuration = [&]() {
        result.totalDurationMs = durationToInt(result.executorTiming.exclusivePhasesTotalMs());
    };
    const auto applyResultTimingToMetadata = [&]() {
        if (!runMetadata.has_value())
        {
            return;
        }
        runMetadata->totalDurationMs = result.totalDurationMs;
        runMetadata->stageExecutionMs = result.executorTiming.stageExecutionMs;
        runMetadata->stateCommitMs = result.executorTiming.stateCommitMs;
        runMetadata->archiveMs = result.executorTiming.archiveMs;
        runMetadata->reportMs = result.executorTiming.reportMs;
    };

    result.executorTiming.stageExecutionMs = timer.capturePhase();
    refreshTotalDuration();

    std::unique_ptr<IWorkflowRunArchive> archive =
        m_impl->archiveFactory
            ? m_impl->archiveFactory(manifest.outputDirectory,
                                     counterAccumulator.has_value() ? &*counterAccumulator : nullptr, options.session)
            : nullptr;
    if (!archive)
    {
        archive = std::make_unique<NoWorkflowRunArchive>();
    }
    IWorkflowRunArchive& archiver = *archive;
    const auto commitArchive = [&](const QString& failureMessage) {
        IWorkflowRunArchive::CommitOutcome archiveResult =
            archiver.commit(manifest, result, [&timer]() { return timer.capturePhase(); });
        result.executorTiming.archiveMs += archiveResult.archiveMs;
        result.executorTiming.reportMs += archiveResult.reportMs;

        if (!archiveResult.success)
        {
            executorLogger.warning(QStringLiteral("%1: %2").arg(failureMessage, archiveResult.error));
        }
        else if (!archiveResult.reportPath.isEmpty() && QFile::exists(archiveResult.reportPath))
        {
            result.htmlReportPath = archiveResult.reportPath;
        }

        result.executorTiming.archiveMs += timer.capturePhase();
        refreshTotalDuration();
        return archiveResult;
    };

    if (result.success && !result.cancelled)
    {
        // Last cancellation gate before we claim ownership of the staged files. The token is read
        // twice on purpose: once here, so we do not announce an archive we are about to abandon, and
        // once again after the log line, so a cancel that lands while we are logging is still
        // honoured. The short circuit keeps the second read out of the already-cancelled case,
        // exactly as the original nested form did.
        const bool cancelledBeforeArchive = m_impl->cancellationToken.isCancellationRequested();
        if (!cancelledBeforeArchive)
        {
            executorLogger.operationDetail(QStringLiteral("Archiving workflow"),
                                           QStringLiteral("Committing outputs and execution history"));
        }

        if (cancelledBeforeArchive || m_impl->cancellationToken.isCancellationRequested())
        {
            result.success = false;
            result.cancelled = true;
            result.errorMessage = QStringLiteral("Cancelled.");
            archiver.revertStagedFiles(result);
        }

        if (result.success && !result.cancelled)
        {
            const IWorkflowRunArchive::CommitOutcome archiveResult =
                commitArchive(QStringLiteral("Failed to archive workflow execution"));
            if (!archiveResult.success)
            {
                result.success = false;
                result.errorMessage =
                    QStringLiteral("Failed to archive workflow execution: %1").arg(archiveResult.error);
                result.errors.append(result.errorMessage);
            }
        }

        WorkflowRunStateStore store(manifest.outputDirectory);
        if (result.success && !result.cancelled && store.saveLastSuccessfulManifest(manifest))
        {
            runMetadata.emplace();
            runMetadata->timestamp = QDateTime::currentSecsSinceEpoch();
            runMetadata->appVersion = qApp ? qApp->applicationVersion() : QString();
            runMetadata->success = true;
            runMetadata->executionId = manifest.executionId;
            runMetadata->executionScope = manifest.executionScope;
            runMetadata->executionScopeKey = manifest.executionScopeKey;
            switch (manifest.executionMode)
            {
            case ExecutionMode::Incremental:
                runMetadata->executionMode = QStringLiteral("Incremental");
                break;
            case ExecutionMode::RegistrationOnly:
                runMetadata->executionMode = QStringLiteral("RegistrationOnly");
                break;
            case ExecutionMode::Full:
            default:
                runMetadata->executionMode = QStringLiteral("Full");
                break;
            }
            runMetadata->overallResult = QStringLiteral("success");
            runMetadata->warningCount = result.warningCount;
            runMetadata->errorCount = static_cast<int>(result.errors.size());
            runMetadata->integrationGroupCount = static_cast<int>(result.integrationGroupResults.size());
            int successfulGroups = 0;
            for (const auto& group : result.integrationGroupResults)
            {
                if (group.success)
                {
                    ++successfulGroups;
                }
            }
            runMetadata->successfulGroupCount = successfulGroups;
            runMetadata->cancelled = false;
            applyResultTimingToMetadata();
            (void)store.saveRunMetadata(*runMetadata);
            executorLogger.operationDetail(QStringLiteral("State saved"),
                                           QStringLiteral("Incremental execution enabled"));
        }
        result.executorTiming.stateCommitMs += timer.capturePhase();
        refreshTotalDuration();
    }
    else
    {
        // Workflow failed or was cancelled.
        //
        // Invariant: a staged previous output is either committed to history or restored as the current
        // output - it is never left behind as a `.tmp_archive` file, because while a group is staged
        // that file is the only copy of the previous output.
        //
        // Partial successes are committed and become the new baseline. LightsStackingStage reports
        // only groups that ran to completion (a group that failed or was cancelled has already
        // reverted its own staged files), so every group still marked successful here produced a
        // valid output that overwrote a previous one. Committing moves the previous output into
        // history and leaves the new one current, and the committed *subset* - not the full manifest
        // - becomes the incremental baseline, so a later run re-executes only what did not finish.
        //
        // With no successful group there is nothing to claim and nothing to commit: the stages that
        // staged those files have already put them back.
        const bool has_successful_integration_group =
            std::any_of(result.integrationGroupResults.begin(), result.integrationGroupResults.end(),
                        [](const IntegrationGroupResult& group) { return group.success; });
        if (has_successful_integration_group)
        {
            const IWorkflowRunArchive::CommitOutcome archiveResult =
                commitArchive(QStringLiteral("Failed to archive partial workflow execution"));
            if (archiveResult.success)
            {
                if (const auto committedManifest = buildCommittedSubsetManifest(manifest, result);
                    committedManifest.has_value())
                {
                    WorkflowRunStateStore store(manifest.outputDirectory);
                    if (store.saveLastSuccessfulManifest(*committedManifest))
                    {
                        executorLogger.operationDetail(QStringLiteral("State saved"),
                                                       QStringLiteral("Committed partial outputs baseline updated"));
                    }
                    else
                    {
                        executorLogger.warning(QStringLiteral("Failed to save committed partial workflow state."));
                    }
                    result.executorTiming.stateCommitMs += timer.capturePhase();
                    refreshTotalDuration();
                }
            }
            // A failed commit leaves staged files behind rather than deleting them (it only logs a
            // warning), which is the safe direction: the archive's orphaned-staged-file cleanup sweeps
            // the output directory and restores or drops them, and the process runs it whenever the
            // output directory is (re)opened.
        }
        // Groups that were still in progress need nothing here - LightsStackingStage reverts its own
        // staged files group by group.
    }

    refreshTotalDuration();
    captureCounters();
    if (const std::optional<an::MemoryCounters> engine_memory_after = engineMemoryCounters(options.session);
        engine_memory_before.has_value() && engine_memory_after.has_value())
    {
        result.engineMemory = engineMemoryOverRun(*engine_memory_before, *engine_memory_after);
        const QString engine_memory = describeEngineMemory(*result.engineMemory);
        executorLogger.operationDetail(engine_memory);
        LOG_INFO("WorkflowExecutor: {}", engine_memory.toStdString());
    }
    archiver.finalizeTimingArtifacts(result);
    if (runMetadata.has_value())
    {
        applyResultTimingToMetadata();
        WorkflowRunStateStore store(manifest.outputDirectory);
        if (!store.saveRunMetadata(*runMetadata))
        {
            executorLogger.warning(QStringLiteral("Failed to finalize workflow timing in run metadata."));
        }
    }

    if (result.success && !result.cancelled)
    {
        const int errorCount = result.errors.size();
        executorLogger.workflowCompleted(result.totalDurationMs, result.warningCount);
        if (errorCount > 0)
        {
            executorLogger.warning(QStringLiteral("%1 frame(s)/group(s) had errors during processing").arg(errorCount));
        }
    }
    result.capturedLog = logBuffer.text();
    archiver.finalizeLogArtifact(result);

    // A request arriving after the archive commit boundary belongs to this completed execution and must never poison
    // the next execute() call. Requests observed before that boundary already set result.cancelled above.
    (void)m_impl->cancellationToken.consumeCancellationRequest();
    return result;
}

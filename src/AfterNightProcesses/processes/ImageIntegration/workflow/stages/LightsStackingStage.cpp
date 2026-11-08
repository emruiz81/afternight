// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow's lights stacking stage: each group's lights read where its
/// registration put them, brought to the reference by the normalization stage's coefficients, weighted by what the
/// registration measured and combined by the engine's stack in one admitted operation; the master's solution chosen
/// and solved before it is written; and the master and its maps published once, whole and stamped.

#include "stdafx.hpp"
#include "LightsStackingStage.h"

#include "app/PlateSolveService.h"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/CometStackingProductResolver.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/core/StackWeightingInputs.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactPublicationLedger.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsReader.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationOutputPlane.h"
#include "processes/ImageIntegration/workflow/stages/StackArtifactFingerprints.h"
#include "processes/ImageIntegration/workflow/stages/StackCometProducts.h"
#include "processes/ImageIntegration/workflow/stages/StackDrizzlePixmapIndex.h"
#include "processes/ImageIntegration/workflow/stages/StackFrameSources.h"
#include "processes/ImageIntegration/workflow/stages/StackMasterWcs.h"
#include "processes/ImageIntegration/workflow/stages/StackNormalizationArtifacts.h"
#include "processes/ImageIntegration/workflow/stages/StackOutputFinalization.h"
#include "processes/ImageIntegration/workflow/stages/StackRgbCombination.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/ImageIntegration/workflow/stages/TransformOnReadRouting.h"
#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/core/RegistrationRun.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleStackSource.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <AfterNightPlatform/Logging.h>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using ImageIntegration::NormalizationMeasurementPlane;
using ImageIntegration::detail::appendUniqueWarnings;

constexpr float kPreparationPhaseEnd = 0.10f;
constexpr float kRuntimePhaseEnd = 0.90f;
constexpr float kWcsPhaseStart = 0.91f;
constexpr float kSavePhaseStart = 0.93f;
constexpr float kMapsPhaseStart = 0.95f;
constexpr float kPreviewPhaseStart = 0.97f;
constexpr float kArtifactPhaseStart = 0.98f;
constexpr float kPublicationPhaseStart = 0.995f;

/// The most rejection clusters a group's report keeps from each map.
constexpr std::size_t kMaximumReportedClusters = 256u;

std::vector<const WorkflowManifest::IntegrationGroup*> groupsToProcess(const WorkflowManifest& manifest)
{
    std::vector<const WorkflowManifest::IntegrationGroup*> groups;
    for (const auto& group : manifest.integrationGroups)
    {
        if (manifest.executionMode == ExecutionMode::Incremental &&
            std::find(manifest.incrementalIntegrationGroups.begin(), manifest.incrementalIntegrationGroups.end(),
                      group.groupKey) == manifest.incrementalIntegrationGroups.end())
        {
            continue;
        }
        groups.push_back(&group);
    }
    return groups;
}

[[nodiscard]] bool isCancellation(const an::Status& status)
{
    return status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded;
}

[[nodiscard]] StageResult engineFailure(const an::Status& status, const IntegrationErrorCode stageCode,
                                        const QString& message, const QString& path = {})
{
    if (isCancellation(status))
    {
        return StageResult::cancelled();
    }
    return StageResult::failure(ImageIntegration::integrationErrorFromEngine(status, stageCode, message, path));
}

[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, message.toUtf8().toStdString());
}

[[nodiscard]] const WorkflowManifest::RegistrationGroup* findRegistrationGroup(const WorkflowManifest& manifest,
                                                                               const GroupKey& key)
{
    const auto found =
        std::find_if(manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
                     [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey == key; });
    return found == manifest.registrationGroups.end() ? nullptr : &*found;
}

/// The stack's progress, in the stage's reporter as a share of the group's: each report the engine makes - a frame
/// read or the finalization, with the pass that made it - moves the group's fraction; nothing is read from text.
class GroupStackProgress final : public an::ProgressSink
{
public:
    GroupStackProgress(ProgressReporter& reporter, const WorkflowStage stage, const int groupIndex,
                       const int totalGroups, QString operation, QString file, float* floor)
        : m_reporter(reporter), m_stage(stage), m_groupIndex(groupIndex), m_totalGroups(totalGroups),
          m_operation(std::move(operation)), m_file(std::move(file)), m_floor(floor)
    {
    }

    /// Moves the share of the group the next stack's reports fill.
    void setRange(const float start, const float end) noexcept
    {
        m_start = start;
        m_end = end;
    }

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        if (report.total == 0u)
        {
            return;
        }
        try
        {
            const double done = static_cast<double>(report.completed) / static_cast<double>(report.total);
            float fraction = m_start + static_cast<float>((std::min)(1.0, done)) * (m_end - m_start);
            if (m_floor != nullptr)
            {
                fraction = (std::max)(fraction, *m_floor);
                *m_floor = fraction;
            }
            stage_utils::reportStageProgress(m_reporter, m_stage, m_groupIndex, m_totalGroups, fraction, m_operation,
                                             m_file);
        }
        catch (...)
        {
            // A reporter that fails loses a progress line; the stack is not stopped for it.
        }
    }

private:
    ProgressReporter& m_reporter;
    WorkflowStage m_stage;
    int m_groupIndex = 0;
    int m_totalGroups = 0;
    QString m_operation;
    QString m_file;
    float* m_floor = nullptr;
    float m_start = kPreparationPhaseEnd;
    float m_end = kRuntimePhaseEnd;
};

/// What the engine says of a group's work while it runs - a sum it could not normalize, whole-frame reads a tiling
/// multiplied, a frame's decode - logged as it is said and kept for the stage to say among its warnings. The engine's
/// strings are borrowed for the call alone, so each sentence is copied; a sentence said again is said once; and only
/// the first few distinct sentences are kept, the first many logged. Told from any of the operation's workers, and
/// from the operations the group is admitted again for, so it is guarded.
class GroupStackDiagnostics final : public an::DiagnosticSink
{
public:
    explicit GroupStackDiagnostics(const QString& group) : m_group(group.toStdString()) {}

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        try
        {
            if (event.severity < an::DiagnosticSeverity::Warning)
            {
                LOG_DEBUG("LightsStackingStage: {} ({}, group='{}')", event.message, event.operation, m_group);
                return;
            }
            const QString sentence = QString::fromUtf8(event.message).trimmed();
            if (sentence.isEmpty())
            {
                return;
            }
            const std::lock_guard<std::mutex> lock(m_mutex);
            if (m_said.contains(sentence) || m_said.size() >= kMaxLoggedSentences)
            {
                return;
            }
            m_said.insert(sentence);
            LOG_WARNING("LightsStackingStage: {} ({}, group='{}')", event.message, event.operation, m_group);
            if (m_kept < kMaxKeptSentences)
            {
                m_pending.push_back(sentence);
                ++m_kept;
            }
        }
        catch (...)
        {
            // A sentence that cannot be copied or logged is lost; the group's work is not stopped for it.
        }
    }

    /// The sentences kept since the last call, in the order they were said.
    [[nodiscard]] QStringList takeWarnings()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        QStringList taken = std::move(m_pending);
        m_pending.clear();
        return taken;
    }

private:
    static constexpr qsizetype kMaxLoggedSentences = 64;
    static constexpr qsizetype kMaxKeptSentences = 8;

    std::string m_group;
    std::mutex m_mutex;
    QSet<QString> m_said;
    QStringList m_pending;
    qsizetype m_kept = 0;
};

/// The calibration a light is read with: its assignment, when the run calibrates in memory and the file read is the
/// light itself.
[[nodiscard]] const WorkflowManifest::LightsCalibrationParams*
calibrationOf(const WorkflowManifest& manifest, const ExecutionPreparationSnapshot* snapshot, const FrameId& light,
              const QString& path)
{
    if (!manifest.calibrateLightsInMemory || QFileInfo(path) != QFileInfo(light.canonicalPath()))
    {
        return nullptr;
    }
    if (snapshot != nullptr)
    {
        if (const auto* params = snapshot->calibrationAssignment(light))
        {
            return params;
        }
    }
    for (const auto& candidate : manifest.lightsCalibration)
    {
        if (candidate.lightPath == light)
        {
            return &candidate;
        }
    }
    return nullptr;
}

/// The revision a registered light has, when it is still the one its registration recorded.
[[nodiscard]] std::optional<FileRevisionStamp>
currentRegisteredOutputRevision(const stage_utils::RegistrationOutputRecord& record)
{
    if (!record.succeeded || record.outputSizeBytes < 0 || record.outputModifiedAtMs < 0 ||
        record.outputIdentityToken.isEmpty())
    {
        return std::nullopt;
    }
    const auto revision = ArtifactTracker::captureFileRevision(record.outputPath);
    if (!revision.has_value() || revision->sizeBytes != record.outputSizeBytes ||
        revision->modifiedAtMs != record.outputModifiedAtMs || revision->identityToken != record.outputIdentityToken)
    {
        return std::nullopt;
    }
    return revision;
}

/// How a group's lights reach the stack.
enum class GroupRoute
{
    /// As they are read: no registration placed them.
    Frames,
    /// As the registered images the alignment wrote.
    RegisteredImages,
    /// Their registration inputs warped onto the registration's plane as they are read.
    TransformOnRead,
    /// Drizzled onto their canvas.
    Drizzle,
    /// As the comet's registration aligned them.
    Comet,
    /// Split at the comet's aperture, both layers stacked and blended.
    StarsPlusComet
};

/// Where and how a group's lights are read.
struct GroupPlan
{
    GroupRoute route = GroupRoute::Frames;
    stage_utils::StackFramePlan frames;

    /// The measurement plane the normalization stage measured the group on.
    NormalizationMeasurementPlane plane = NormalizationMeasurementPlane::Frames;

    /// The registration's settings version and the record of its plane, when the group is registered.
    std::optional<int> settingsVersion;
    std::optional<QString> outputPlaneWcsJson;
    QString registrationReference;
    std::optional<bool> expectedSipCorrection;
    std::optional<an::RegistrationCanvas> registrationCanvas;

    /// The canvas mode the registration's plane was planned by - the standard canvas's when it stands in for the
    /// group's intersection - which the plane's record must name; the group's own mode when the route planned none.
    std::optional<ImageIntegration::RegistrationCanvasMode> planeMode;

    /// The drizzle geometry, when the group drizzles.
    std::optional<ImageIntegration::DrizzleGroupGeometry> drizzle;

    /// The composite's products, when the group is a stars-plus-comet stack.
    std::optional<an::ui::StarsPlusCometExperimentalProductResolveResult> composite;

    /// The fingerprints the stack's identity is made of beside its files.
    QString transformsFingerprint;
    QString distortionFingerprint;
    QString cometProductsFingerprint;

    /// The policy a report and a log name the route by.
    QString policy;

    /// Lights left out and why.
    QStringList dropped;
};

struct PlanOutcome
{
    std::optional<GroupPlan> plan;
    std::optional<StageResult> result;
};

[[nodiscard]] PlanOutcome planFailure(StageResult result)
{
    PlanOutcome outcome;
    outcome.result = std::move(result);
    return outcome;
}

/// The weights a group's frames are stacked with: the engine's source and each frame's measurement of the figure it
/// reads, from the registration's records.
struct GroupWeights
{
    an::StackWeighting method = an::StackWeighting::Uniform;
    std::vector<an::StackFrameMeasurement> measurements;
    QString fallback;
};

/// The weights of a group's lights: an image count from how many frames each light's header says it combines, every
/// other figure from the registration's records, read as the Frame Quality dashboard and the report read them.
[[nodiscard]] GroupWeights weightsFor(const an::StackWeighting method, const std::vector<FrameId>& lights,
                                      const std::vector<std::uint32_t>& combinedFrames, const QString& metricsPath,
                                      const QString& registrationKey, const std::optional<int>& settingsVersion,
                                      WorkflowExecutionCounterAccumulator* counters)
{
    GroupWeights weights;
    weights.method = method;
    if (method == an::StackWeighting::Uniform)
    {
        return weights;
    }
    const auto fallBack = [&weights](QString reason) {
        weights.method = an::StackWeighting::Uniform;
        weights.measurements.clear();
        weights.fallback = std::move(reason);
        return weights;
    };
    if (method == an::StackWeighting::ImageCount)
    {
        if (std::all_of(combinedFrames.begin(), combinedFrames.end(),
                        [](const std::uint32_t frames) { return frames == 1u; }))
        {
            return fallBack(QStringLiteral("no light states more than one combined frame in STACKCNT or NCOMBINE"));
        }
        weights.measurements.resize(lights.size());
        for (std::size_t index = 0; index < lights.size() && index < combinedFrames.size(); ++index)
        {
            weights.measurements[index].integratedFrameCount = combinedFrames[index];
        }
        return weights;
    }
    if (registrationKey.trimmed().isEmpty() || !settingsVersion.has_value() || !QFileInfo::exists(metricsPath))
    {
        return fallBack(QStringLiteral("registration metrics are unavailable"));
    }
    an::ui::RegistrationMetricsStateLoader loader(metricsPath, QStringLiteral("StackingWeightMetrics"), counters);
    std::vector<an::ui::RegistrationMetricRecord> records;
    QString error;
    if (!loader.open(&error) || !loader.loadMetricRecords(registrationKey, *settingsVersion, records, &error))
    {
        return fallBack(QStringLiteral("registration metrics could not be loaded"));
    }
    std::unordered_map<QString, const an::ui::RegistrationMetricRecord*> byLight;
    for (const auto& record : records)
    {
        if (record.metrics.succeeded)
        {
            byLight[record.frameId.identityKey()] = &record;
        }
    }
    int missing = 0;
    weights.measurements.resize(lights.size());
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const auto found = byLight.find(lights[index].identityKey());
        if (found == byLight.end())
        {
            ++missing;
            continue;
        }
        weights.measurements[index] =
            ImageIntegration::stackFrameMeasurementFor(found->second->metrics, found->second->noise);
    }
    if (missing > 0)
    {
        return fallBack(QStringLiteral("%1 frame(s) are missing registration metrics").arg(missing));
    }
    const bool carried = std::all_of(weights.measurements.begin(), weights.measurements.end(),
                                     [method](const an::StackFrameMeasurement& measurement) {
                                         return ImageIntegration::carriesWeightingFigure(method, measurement);
                                     });
    if (!carried)
    {
        return fallBack(QStringLiteral("registration metrics do not carry %1")
                            .arg(ImageIntegration::weightingInputDescription(method)));
    }
    return weights;
}

/// What a group's stack is asked for beside its frames.
struct StackShape
{
    ImageIntegration::IntegrationStackChoice choice;
    an::StackWeighting weighting = an::StackWeighting::Uniform;
    an::StackNormalizationScope normalization = an::StackNormalizationScope::None;
    std::optional<an::NormalizationMethod> globalMethod;
    an::ScaleEstimator estimator = an::ScaleEstimator::IterativeKSigma;
    std::uint32_t referenceFrame = 0u;
    an::RejectionMapForm rejectionMaps = an::RejectionMapForm::None;
    ImageIntegration::StackingQualityMapProductPolicy qualityMaps;
};

/// The engine's stack request for a group.
[[nodiscard]] an::Result<an::StackRequest> stackRequestFor(const StackingSettings& settings, const StackShape& shape,
                                                           const an::ImageDescriptor& input,
                                                           const std::uint32_t frameCount, const bool comet)
{
    an::StackRequest request{.input = an::StackInputDescription{.frameCount = frameCount, .descriptor = input},
                             .combination = shape.choice.combination};
    request.rejection.method =
        frameCount < an::kMinimumRejectionFrames ? an::StackRejection::None : shape.choice.rejection;
    if (an::stackRejectionUsesSigmaThresholds(request.rejection.method))
    {
        request.rejection.sigma = an::SigmaClipThresholds{.low = settings.sigmaLow, .high = settings.sigmaHigh};
    }
    else if (an::stackRejectionUsesMedianFractions(request.rejection.method))
    {
        request.rejection.medianFractions =
            an::MedianClipFractions{.low = settings.sigmaLow, .high = settings.sigmaHigh};
    }
    else if (an::stackRejectionUsesGesdtParameters(request.rejection.method))
    {
        request.rejection.gesdt =
            an::GesdtParameters{.maximumOutlierFraction = settings.sigmaLow, .significance = settings.sigmaHigh};
    }
    const bool weighted = ImageIntegration::combinationTakesWeights(shape.choice.combination) && !comet;
    request.weighting = ImageIntegration::stackWeightingRequestFor(
        settings, weighted ? shape.weighting : an::StackWeighting::Uniform, shape.choice.starCountScaling);
    if (weighted && settings.localWeighting.enabled)
    {
        // At the nearest values the engine takes; the group's notes say which ones it changed.
        request.localWeighting = ImageIntegration::localWeightingRequestFor(settings.localWeighting);
    }
    request.normalization.scope = shape.normalization;
    request.normalization.globalMethod =
        an::stackNormalizationScopeUsesGlobalMethod(shape.normalization) ? shape.globalMethod : std::nullopt;
    request.normalization.scaleEstimator = shape.estimator;
    request.normalization.referenceFrame = shape.referenceFrame;
    request.output.sampleType = an::SampleType::Float32;
    if (request.rejection.method != an::StackRejection::None && !comet)
    {
        request.output.rejectionMaps = shape.rejectionMaps;
    }
    if (!comet)
    {
        request.output.coverageMap = shape.qualityMaps.coverage;
        request.output.varianceMap = shape.qualityMaps.variance;
        request.output.signalToNoiseGainMap = shape.qualityMaps.snrGain;
        request.output.weightSumMap =
            shape.qualityMaps.weightSum && request.weighting.method != an::StackWeighting::Uniform;
    }
    const an::StackModelError refused = an::StackModel::check(request);
    if (refused != an::StackModelError::None)
    {
        return an::Status::failure(an::stackModelErrorCode(refused),
                                   std::string("The stack cannot be made: ") + an::stackModelErrorName(refused));
    }
    return request;
}

/// The stacked master, its metadata the reference light's as the stack's transition moves it.
[[nodiscard]] an::Result<an::Image<float>> stackedMaster(an::StackResult& result, const an::StackModel& model,
                                                         const an::ImageMetadata& reference)
{
    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::fromBytes(std::move(result.image));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::fromStorage(std::move(*storage), result.descriptor, reference);
    if (!image.hasValue())
    {
        return image.status();
    }
    const an::Result<an::MetadataTransition> change = an::stackTransition(model, result.report, reference.semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = image->transition(*change); stated.failed())
    {
        return stated;
    }
    return std::move(*image);
}

/// A stacked image cut to a rectangle: the edge guard an intersection's drizzle keeps.
[[nodiscard]] an::Result<an::Image<float>> cropped(const an::consumer::OperationScope& scope,
                                                   const an::Image<float>& image, const std::uint32_t x,
                                                   const std::uint32_t y, const std::uint32_t width,
                                                   const std::uint32_t height)
{
    const std::optional<an::ImageDescriptor> described = image.descriptor();
    const auto view = image.view();
    if (!described.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the stack holds nothing"));
    }
    const an::Result<an::GeometryPlan> plan =
        an::planCrop(*described, an::CropRequest{.x = x, .y = y, .width = width, .height = height});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<float>> output =
        an::Image<float>::create(scope.engine(), plan->destinationDescriptor(), an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, image.metadata());
    if (!output.hasValue())
    {
        return output.status();
    }
    an::Result<an::ValidityMask> mask =
        an::ValidityMask::create(scope.engine(), plan->destinationDescriptor().extent());
    if (!mask.hasValue())
    {
        return mask.status();
    }
    std::optional<an::ImageWriteAccess<float>> access = output->write();
    std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
    if (!access.has_value() || !bits.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the cropped stack holds nothing"));
    }
    const an::Result<an::GeometryResult> done = an::transformImageGeometry(
        scope.execution(), an::SampleType::Float32, view->data(), view->layout(), access->view().data(),
        access->view().layout(), *plan, an::GeometrySelection{}, *bits);
    if (!done.hasValue())
    {
        return done.status();
    }
    return std::move(*output);
}

/// The total exposure of the lights a stack combined, from the exposures the group lists; nothing when none is listed.
[[nodiscard]] std::optional<double> totalIntegrationSeconds(const WorkflowManifest::IntegrationGroup& group,
                                                            const std::vector<FrameId>& lights)
{
    const std::size_t listed = (std::min)(group.lightFiles.size(), group.lightExposureSeconds.size());
    std::unordered_map<QString, double> exposureByLight;
    for (std::size_t index = 0; index < listed; ++index)
    {
        const double exposure = group.lightExposureSeconds[index];
        if (std::isfinite(exposure) && exposure > 0.0)
        {
            exposureByLight[group.lightFiles[index].identityKey()] = exposure;
        }
    }
    double total = 0.0;
    for (const FrameId& light : lights)
    {
        const auto found = exposureByLight.find(light.identityKey());
        if (found != exposureByLight.end())
        {
            total += found->second;
        }
    }
    return std::isfinite(total) && total > 0.0 ? std::optional<double>(total) : std::nullopt;
}

/// A file's metadata, read from its header without decoding its samples.
[[nodiscard]] an::Result<an::CodecImageInfo> probeFile(const an::consumer::OperationScope& scope,
                                                       const an::CodecRegistry* registry, const QString& path)
{
    // The request borrows its path for the call, so the UTF-8 text lives in a local.
    const std::string utf8 = path.toUtf8().toStdString();
    const an::CodecReadRequest request{.path = utf8, .validatePayload = false};
    return registry != nullptr ? an::probeImage(scope.engine(), scope.execution(), *registry, request)
                               : an::probeImage(scope.engine(), scope.execution(), request);
}

/// Which samples of a light a stack of it alone would hold: every one its rule admits, one bit-plane per channel.
[[nodiscard]] an::Result<an::ValidityMask> heldSamplesOf(const an::consumer::OperationScope& scope,
                                                         const an::Image<float>& light, const an::ValidityRule rule)
{
    const std::optional<an::ImageDescriptor> described = light.descriptor();
    const auto samples = light.view();
    if (!described.has_value() || !samples.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the light holds nothing"));
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(scope.engine(), described->extent());
    if (!mask.hasValue())
    {
        return mask.status();
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
    if (!bits.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the light's mask holds nothing"));
    }
    if (an::Status marked = an::markValidity(*bits, *samples, stage_utils::stackFrameValidity(rule)); marked.failed())
    {
        return marked;
    }
    return mask;
}

/// A stack's quality as the workflow's reports keep it, from the engine's report and the rejection maps' scans.
struct RejectionScans
{
    bool analyzed = false;
    std::vector<RejectionClusterRecord> clusters;
    std::uint64_t clusterCount = 0u;
    std::uint64_t clusterPixels = 0u;
    std::uint64_t largestClusterPixels = 0u;
    double clusterFraction = 0.0;
    double largestClusterFraction = 0.0;
    std::uint64_t rejectedElements = 0u;
    double rejectedElementFraction = 0.0;
};

[[nodiscard]] RejectionClusterMapKind mapKindOf(const an::RejectionClusterSource source)
{
    switch (source)
    {
    case an::RejectionClusterSource::Low:
        return RejectionClusterMapKind::Low;
    case an::RejectionClusterSource::High:
        return RejectionClusterMapKind::High;
    case an::RejectionClusterSource::Merged:
        break;
    }
    return RejectionClusterMapKind::Merged;
}

/// The clusters each rejection map holds and the elements either reached, as two figures: the clusters' pixels, and
/// every element a map reached at all.
[[nodiscard]] an::Result<RejectionScans> scanRejectionMaps(const an::ExecutionContext& execution,
                                                           const an::StackResult& result)
{
    RejectionScans scans;
    const an::ImageExtent& extent = result.descriptor.extent();
    const std::uint64_t elements = static_cast<std::uint64_t>(extent.width()) * extent.height() * extent.channelCount();
    const auto countsOf = [&](const an::StackOutputMap which) -> std::span<const std::uint32_t> {
        const an::ImageStorageBytes& bytes = result.maps[static_cast<std::size_t>(which)];
        if (!bytes.valid())
        {
            return {};
        }
        return std::span<const std::uint32_t>(static_cast<const std::uint32_t*>(bytes.data()),
                                              static_cast<std::size_t>(elements));
    };
    const std::span<const std::uint32_t> merged = countsOf(an::StackOutputMap::RejectionMerged);
    const std::span<const std::uint32_t> low = countsOf(an::StackOutputMap::RejectionLow);
    const std::span<const std::uint32_t> high = countsOf(an::StackOutputMap::RejectionHigh);
    std::vector<std::pair<std::span<const std::uint32_t>, an::RejectionClusterSource>> maps;
    if (!merged.empty())
    {
        maps.emplace_back(merged, an::RejectionClusterSource::Merged);
    }
    if (!low.empty())
    {
        maps.emplace_back(low, an::RejectionClusterSource::Low);
    }
    if (!high.empty())
    {
        maps.emplace_back(high, an::RejectionClusterSource::High);
    }
    if (maps.empty())
    {
        return scans;
    }
    std::vector<an::RejectionClusterDetection> detections(kMaximumReportedClusters);
    for (const auto& [map, source] : maps)
    {
        const an::Result<an::RejectionClusterReport> report = an::analyzeRejectionClusters(
            execution, map, extent, an::RejectionClusterAnalysisRequest{.source = source}, detections);
        if (!report.hasValue())
        {
            return report.status();
        }
        scans.clusterCount += report->clustersFound;
        scans.clusterPixels += report->reportedClusterPixels;
        scans.largestClusterPixels = (std::max)(scans.largestClusterPixels, report->largestClusterPixels);
        scans.largestClusterFraction = (std::max)(scans.largestClusterFraction, report->largestClusterFraction);
        for (std::uint64_t index = 0; index < report->clustersReported; ++index)
        {
            const an::RejectionClusterDetection& detection = detections[static_cast<std::size_t>(index)];
            scans.clusters.push_back(
                RejectionClusterRecord{.mapKind = mapKindOf(detection.source),
                                       .channel = detection.channel,
                                       .pixelCount = detection.pixelCount,
                                       .minX = static_cast<int>(detection.minX),
                                       .minY = static_cast<int>(detection.minY),
                                       .maxX = static_cast<int>(detection.maxX),
                                       .maxY = static_cast<int>(detection.maxY),
                                       .centroidX = detection.centroidX,
                                       .centroidY = detection.centroidY,
                                       .meanValue = detection.meanRejections,
                                       .peakValue = static_cast<double>(detection.peakRejections),
                                       .integratedValue = static_cast<double>(detection.integratedRejections)});
        }
    }
    const an::Result<an::RejectionMapCounts> counts =
        merged.empty() ? an::countRejectionMaps(execution, low.empty() ? high : low,
                                                low.empty() ? std::span<const std::uint32_t>{} : high, extent)
                       : an::countRejectionMaps(execution, merged, {}, extent);
    if (!counts.hasValue())
    {
        return counts.status();
    }
    scans.rejectedElements = counts->eitherElements;
    scans.rejectedElementFraction = counts->eitherFraction;
    scans.clusterFraction =
        elements > 0u ? static_cast<double>(scans.clusterPixels) / static_cast<double>(elements) : 0.0;
    std::sort(scans.clusters.begin(), scans.clusters.end(),
              [](const RejectionClusterRecord& left, const RejectionClusterRecord& right) {
                  if (left.pixelCount != right.pixelCount)
                  {
                      return left.pixelCount > right.pixelCount;
                  }
                  if (left.integratedValue != right.integratedValue)
                  {
                      return left.integratedValue > right.integratedValue;
                  }
                  return left.peakValue > right.peakValue;
              });
    scans.analyzed = true;
    return scans;
}

[[nodiscard]] StackQualityRecord qualityRecordOf(const an::StackExecutionReport& report, const std::uint32_t frames,
                                                 const std::vector<an::FrameRejectionRate>& rates,
                                                 const RejectionScans& scans)
{
    StackQualityRecord record;
    record.valid = true;
    record.inputFrameCount = frames;
    record.effectiveFrameCount = report.effectiveFrameCount;
    record.theoreticalSnrGain = report.theoreticalSnrGain;
    record.actualSnrGain = report.actualSnrGain;
    record.integrationEfficiency = report.integrationEfficiency;
    if (report.coverage.available)
    {
        record.minCoverage = report.coverage.minimum;
        record.medianCoverage = report.coverage.median;
        record.maxCoverage = report.coverage.maximum;
        record.coverageStddev = report.coverage.standardDeviation;
    }
    if (report.rejectionSummarized)
    {
        record.hasRejectionStats = true;
        record.globalRejectionRate = report.rejection.globalRate;
        record.minFrameRejectionRate = report.rejection.minimumRate;
        record.meanFrameRejectionRate = report.rejection.meanRate;
        record.medianFrameRejectionRate = report.rejection.medianRate;
        record.maxFrameRejectionRate = report.rejection.maximumRate;
        for (const an::FrameRejectionRate& rate : rates)
        {
            record.frameRejectionRates.push_back(rate.rated ? rate.rate : 0.0);
        }
    }
    if (report.weighting.disposition == an::StackFrameWeightingDisposition::Computed ||
        report.weighting.disposition == an::StackFrameWeightingDisposition::Supplied)
    {
        record.hasWeightingStats = true;
        record.weightMin = report.weighting.summary.minimum;
        record.weightMax = report.weighting.summary.maximum;
        record.weightStddev = report.weighting.summary.standardDeviation;
        record.weightEfficiency = report.weighting.summary.efficiency;
    }
    if (report.localWeightsMeasured)
    {
        record.hasLocalWeightingStats = true;
        record.localWeightWindowSize = static_cast<int>(report.localWeighting.windowSize);
        record.localWeightGridSpacing = static_cast<int>(report.localWeighting.gridSpacing);
        record.localWeightSnrExponent = report.localWeighting.snrExponent;
        record.localWeightGridCount = report.localWeighting.nodeCount;
        record.localWeightMean = report.localWeighting.meanWeight;
        record.localWeightMin = report.localWeighting.minimumWeight;
        record.localWeightMax = report.localWeighting.maximumWeight;
        record.localWeightStddev = report.localWeighting.weightStandardDeviation;
    }
    if (scans.analyzed)
    {
        record.spatialClusterCount = scans.clusterCount;
        record.spatialClusterPixels = scans.clusterPixels;
        record.spatialLargestClusterPixels = scans.largestClusterPixels;
        record.spatialClusterFraction = scans.clusterFraction;
        record.spatialLargestClusterFraction = scans.largestClusterFraction;
        record.spatialRejectedElements = scans.rejectedElements;
        record.spatialRejectedElementFraction = scans.rejectedElementFraction;
        record.spatialClusters = scans.clusters;
        record.hasSpatialRejectionClusters = !scans.clusters.empty();
    }
    return record;
}

/// A stack run over a frame source: the result, the frames' rejection rates, which frames stood out, and what brought
/// the frames to the reference.
struct StackRun
{
    std::optional<an::StackModel> model;
    std::optional<an::StackResult> result;
    std::vector<an::FrameRejectionRate> rates;
    std::vector<std::uint32_t> flagged;
    QString normalization;
    /// The weight the stack combined each frame it read with, at a mean of one, the mean over a frame's channels.
    std::vector<double> frameWeights;
};

/// The weight a stack combined each frame with, from the engine's weights for each frame and channel: a frame's is the
/// mean over its channels, and the frames' are restated at a mean of one.
[[nodiscard]] std::vector<double> perFrameWeights(const std::vector<double>& combined, const std::uint32_t frameCount)
{
    std::vector<double> weights;
    if (frameCount == 0u || combined.empty() || combined.size() % frameCount != 0u)
    {
        return weights;
    }
    const std::size_t channels = combined.size() / frameCount;
    weights.reserve(frameCount);
    double sum = 0.0;
    for (std::uint32_t frame = 0u; frame < frameCount; ++frame)
    {
        double frameSum = 0.0;
        for (std::size_t channel = 0u; channel < channels; ++channel)
        {
            frameSum += combined[static_cast<std::size_t>(frame) * channels + channel];
        }
        weights.push_back(frameSum / static_cast<double>(channels));
        sum += weights.back();
    }
    if (!(sum > 0.0) || !std::isfinite(sum))
    {
        return {};
    }
    const double mean = sum / static_cast<double>(frameCount);
    for (double& weight : weights)
    {
        weight /= mean;
    }
    return weights;
}

} // namespace

LightsStackingStage::LightsStackingStage(std::shared_ptr<an::ui::PlateSolveService> plateSolveService,
                                         StackGroupMonotonicClock monotonicClock,
                                         StackPreviewGenerator previewGenerator,
                                         StackPreStackGenerationHook preStackGenerationHook)
    : m_plateSolveService(std::move(plateSolveService)),
      m_monotonicClock(monotonicClock ? std::move(monotonicClock)
                                      : StackGroupMonotonicClock([]() { return std::chrono::steady_clock::now(); })),
      m_previewGenerator(previewGenerator
                             ? std::move(previewGenerator)
                             : StackPreviewGenerator(ImageIntegration::detail::generateResidentStackPreviewAssets)),
      m_preStackGenerationHook(std::move(preStackGenerationHook))
{
}

QString LightsStackingStage::name() const
{
    return QStringLiteral("LightsStacking");
}

WorkflowStage LightsStackingStage::stageId() const
{
    return WorkflowStage::LightsStacking;
}

bool LightsStackingStage::shouldSkip(const WorkflowManifest& manifest) const
{
    return groupsToProcess(manifest).empty();
}

QString LightsStackingStage::skipReason(const WorkflowManifest& manifest) const
{
    if (manifest.integrationGroups.empty())
    {
        return QStringLiteral("No integration groups.");
    }
    if (manifest.executionMode == ExecutionMode::Incremental && groupsToProcess(manifest).empty())
    {
        return QStringLiteral("No dirty integration groups in incremental plan.");
    }
    return QStringLiteral("No work required.");
}

StageResult LightsStackingStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                         ProgressReporter& reporter)
{
    using namespace ImageIntegration::detail;

    const auto groups = groupsToProcess(manifest);
    if (groups.empty())
    {
        return StageResult::skipped();
    }
    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return StageResult::failure(
            IntegrationError(IntegrationErrorCode::InvalidOutputPath, QStringLiteral("Output directory is required")));
    }
    const ImageIntegration::WorkflowStageServices* services = stageServices();
    if (services == nullptr)
    {
        return StageResult::failure(IntegrationError::internalError(
            QStringLiteral("The lights stacking stage runs only inside a workflow run, which lends it the session")));
    }
    const QDir outputRoot(manifest.outputDirectory);
    if (!outputRoot.exists() && !outputRoot.mkpath(QStringLiteral(".")))
    {
        return StageResult::failure(IntegrationError::directoryNotWritable(manifest.outputDirectory));
    }
    cleanupLeftoverWcsTempFiles(outputRoot);

    const QString metricsPath = outputRoot.filePath(QStringLiteral("registration_metrics.db"));
    const bool hasMetricsDb = QFileInfo::exists(metricsPath);
    const bool needsDrizzle = std::any_of(groups.begin(), groups.end(), [](const auto* group) {
        return group->drizzle.has_value() && group->drizzle->enabled;
    });
    if (needsDrizzle && !hasMetricsDb)
    {
        return StageResult::failure(IntegrationError::fileNotFound(metricsPath));
    }
    std::unique_ptr<stage_utils::RegistrationMetricsReader> metricsReader;
    if (hasMetricsDb)
    {
        metricsReader = std::make_unique<stage_utils::RegistrationMetricsReader>(
            metricsPath, QStringLiteral("StackingMetrics"), executionCounterAccumulator());
        QString openError;
        if (!metricsReader->open(&openError))
        {
            LOG_WARNING("LightsStackingStage: failed to open registration metrics DB: {}", openError.toStdString());
            metricsReader.reset();
        }
    }

    const ExecutionPreparationSnapshot* snapshot = executionPreparationSnapshot();
    ArtifactTracker tracker(manifest.outputDirectory, executionCounterAccumulator(),
                            sessionArtifactStampReader(services->session()));
    core::CalibrationMasterSet masters(services->codecRegistry());
    const an::CodecRegistry* registry = services->codecRegistry().get();
    std::shared_ptr<an::ui::PlateSolveService> plateSolveService = m_plateSolveService;
    FinalMasterWcsCache finalMasterWcsCache;
    StackExecutionCounters engineCounters;
    bool countersTouched = false;

    const int totalGroups = static_cast<int>(groups.size());
    reporter.logger().stageStarted(name(), totalGroups);
    QElapsedTimer stageTimer;
    stageTimer.start();
    int groupIndex = 0;
    IntegrationErrorReport errorReport;
    bool anyGroupSucceeded = false;
    std::vector<IntegrationGroupResult> completedGroupResults;
    completedGroupResults.reserve(groups.size());
    QSet<QString> processedGroupKeys;
    QStringList stageWarnings;

    for (const auto* groupPtr : groups)
    {
        const auto& group = *groupPtr;
        ++groupIndex;
        processedGroupKeys.insert(group.groupKey.toString());
        const QString groupLabel = group.groupKey.toCompactString();
        reporter.logger().groupStarted(groupIndex, totalGroups, groupLabel);
        StackGroupTimingRecorder groupTiming(m_monotonicClock);
        if (token.isCancellationRequested() || services->stopToken().stop_requested())
        {
            StageResult cancelled = StageResult::cancelled();
            cancelled.groupResults = std::move(completedGroupResults);
            return cancelled;
        }
        if (group.lightFiles.empty())
        {
            reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTiming.elapsedNowMs());
            continue;
        }

        const QString outputPath = outputRoot.filePath(group.outputFilename);
        float progressFloor = 0.0f;
        const auto report = [&](const float fraction, const QString& operation) {
            progressFloor = (std::max)(progressFloor, fraction);
            stage_utils::reportStageProgress(reporter, stageId(), groupIndex, totalGroups, progressFloor, operation,
                                             outputPath);
        };

        const auto processGroup = [&]() -> StageResult {
            report(0.0f, QStringLiteral("Preparing stacking"));
            QStringList groupWarnings;
            const auto warn = [&](const QString& warning) {
                appendUniqueWarnings(groupWarnings, QStringList{warning});
                reporter.logger().warning(warning);
                LOG_WARNING("LightsStackingStage: {} (group='{}')", warning.toStdString(), groupLabel.toStdString());
            };

            std::vector<FrameId> inputFrames = group.lightFiles;
            std::sort(inputFrames.begin(), inputFrames.end());
            inputFrames.erase(std::unique(inputFrames.begin(), inputFrames.end()), inputFrames.end());
            if (inputFrames.size() < 2u)
            {
                return StageResult::failure(
                    IntegrationError::insufficientFrames(2, static_cast<int>(inputFrames.size())));
            }
            if (group.stacking.hdr.enabled && group.drizzle.has_value() && group.drizzle->enabled)
            {
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                     QStringLiteral("HDR exposure merge is not supported together with drizzle"),
                                     outputPath, groupLabel));
            }

            // The settings the stack runs, read through the closed maps: an unknown text stops the group by name.
            const StackingSettings sanitized = ImageIntegration::sanitizeStackingSettings(group.stacking);
            const an::Result<ImageIntegration::IntegrationStackChoice> choice =
                ImageIntegration::integrationStackChoice(sanitized);
            if (!choice.hasValue())
            {
                return StageResult::failure(IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                                             QStringLiteral("The stacking settings name no method"),
                                                             outputPath, QString::fromUtf8(choice.status().message())));
            }
            for (const QString& note : ImageIntegration::stackingSettingsNotes(group.stacking))
            {
                warn(note);
            }

            const auto* registration = findRegistrationGroup(manifest, group.registrationGroupKey);
            const QString registrationKey = group.registrationGroupKey.toString();
            const ImageIntegration::RegistrationCanvasMode canvasMode =
                registration != nullptr ? stage_utils::parseAlignmentMode(registration->settings.alignmentMode)
                                        : ImageIntegration::RegistrationCanvasMode::Standard;
            const bool isMosaic = canvasMode == ImageIntegration::RegistrationCanvasMode::Mosaic;
            const bool cometOnly = ImageIntegration::usesCometOnlyStackingMode(group.stacking.cometStackingMode);
            const bool starsPlusComet =
                ImageIntegration::usesStarsPlusCometExperimentalStackingMode(group.stacking.cometStackingMode);
            const bool drizzled = group.drizzle.has_value() && group.drizzle->enabled;
            const bool wantsLocal =
                sanitized.normalizationMode == NormalizationMode::Local && sanitized.localNormalization.enabled;

            const bool userReferences = !group.normalizationReferenceFrames.empty();
            std::vector<FrameId> referenceFrames = group.normalizationReferenceFrames;
            if (referenceFrames.empty())
            {
                reporter.logger().warning(QStringLiteral(
                    "No stacking reference frames were provided; falling back to the first input frame."));
                referenceFrames = {inputFrames.front()};
            }

            std::optional<int> settingsVersion;
            if (metricsReader && registration != nullptr && !registrationKey.trimmed().isEmpty())
            {
                settingsVersion =
                    metricsReader->resolveSettingsVersionForGroup(registrationKey, registration->settings.version);
            }
            const auto readRevision = [](const QString& path) { return ArtifactTracker::captureFileRevision(path); };

            // Where and how the group's lights are read.
            // The light the registration's transforms carry the others onto: the one its record names, or else the
            // group's reference or first light. The record of the registration's plane names it too, and is checked
            // against it.
            const auto registrationReferenceOf = [&]() -> FrameId {
                if (metricsReader && settingsVersion.has_value())
                {
                    if (const auto persisted =
                            metricsReader->readReferenceFrameId(group.registrationGroupKey, *settingsVersion))
                    {
                        if (std::optional<FrameId> recorded = stage_utils::persistedRegistrationFrameId(*persisted))
                        {
                            return *recorded;
                        }
                    }
                }
                if (registration != nullptr && registration->referenceFrame.has_value())
                {
                    return *registration->referenceFrame;
                }
                return registration != nullptr && !registration->lightFiles.empty() ? registration->lightFiles.front()
                                                                                    : inputFrames.front();
            };

            const auto planGroup = [&]() -> PlanOutcome {
                GroupPlan plan;
                plan.settingsVersion = settingsVersion;
                if (registration != nullptr)
                {
                    plan.frames.demosaic = an::ui::processes::registrationDemosaicOf(registration->settings);
                    plan.frames.interpolation = stage_utils::parseInterpolation(registration->settings.interpolation)
                                                    .value_or(an::KernelInterpolation::Lanczos4);
                }

                if (cometOnly || starsPlusComet)
                {
                    if (registrationKey.trimmed().isEmpty() || !hasMetricsDb)
                    {
                        return planFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::MissingRequiredFrames,
                            QStringLiteral("Comet stacking requires registration metrics with comet_aligned products."),
                            outputPath, groupLabel)));
                    }
                    if (starsPlusComet && drizzled)
                    {
                        return planFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::InvalidConfiguration,
                            QStringLiteral("%1 stacking does not drizzle; turn drizzle off for this group.")
                                .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken()),
                            outputPath, groupLabel)));
                    }
                }

                if (drizzled)
                {
                    ImageIntegration::DrizzleGeometryResolution resolution =
                        ImageIntegration::resolveDrizzleGroupGeometry(*services, manifest, group,
                                                                      executionCounterAccumulator());
                    if (resolution.cancelled)
                    {
                        return planFailure(StageResult::cancelled());
                    }
                    if (!resolution.geometry.has_value())
                    {
                        return planFailure(StageResult::failure(resolution.error));
                    }
                    ImageIntegration::DrizzleGroupGeometry& geometry = *resolution.geometry;
                    for (const QString& missing : geometry.withoutTransform)
                    {
                        plan.dropped << QStringLiteral("%1 (no registration transform)").arg(missing);
                    }
                    if (geometry.intersectionReplaced)
                    {
                        warn(stage_utils::intersectionReplacedWarning(groupLabel));
                    }
                    plan.planeMode = geometry.canvasMode;
                    if (geometry.frames.size() < 2u)
                    {
                        return planFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::MissingRequiredFrames,
                            cometOnly ? QStringLiteral("Comet drizzle stacking requires at least two successful "
                                                       "comet_aligned transform products.")
                                      : QStringLiteral("Drizzle stacking requires at least two input frames with "
                                                       "successful registration transforms. Run registration again "
                                                       "before stacking."))));
                    }
                    plan.route = GroupRoute::Drizzle;
                    plan.plane = NormalizationMeasurementPlane::DrizzleAnalysisPlane;
                    plan.settingsVersion = geometry.settingsVersion;
                    plan.frames.keepMosaic = group.drizzle->cfaAware;
                    // A pattern the drizzle states reads every light as that array's samples; Auto reads each light's
                    // own, which the geometry has found each to state.
                    if (group.drizzle->cfaAware &&
                        !stage_utils::parseBayerPattern(group.drizzle->cfaPattern, plan.frames.mosaicPattern))
                    {
                        return planFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::InvalidConfiguration,
                            QStringLiteral("Drizzle CFA pattern is invalid"), {}, group.drizzle->cfaPattern)));
                    }
                    std::unordered_map<QString, an::RegistrationTransform> transforms;
                    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
                    std::unordered_map<QString, std::optional<an::SipDistortion>> resolved;
                    for (const auto& frame : geometry.frames)
                    {
                        stage_utils::StackFrameRead read;
                        read.logical = frame.frame;
                        read.path = frame.inputPath;
                        read.calibration = calibrationOf(manifest, snapshot, frame.frame, frame.inputPath);
                        read.revision = readRevision(frame.inputPath);
                        plan.frames.frames.push_back(std::move(read));
                        transforms.emplace(frame.frame.canonicalPath(), frame.transform);
                        provenance.emplace(frame.frame.canonicalPath(), frame.provenance);
                        resolved.emplace(frame.frame.canonicalPath(), frame.resolvedDistortion);
                    }
                    plan.transformsFingerprint = stage_utils::fingerprintTransforms(transforms);
                    plan.distortionFingerprint =
                        stage_utils::fingerprintSourceDistortions(transforms, provenance, resolved);
                    plan.cometProductsFingerprint = geometry.cometProductsFingerprint;
                    plan.policy =
                        QStringLiteral("Drizzle (%1)").arg(stage_utils::describeDrizzleSettings(*group.drizzle));
                    if (registration != nullptr && metricsReader && settingsVersion.has_value())
                    {
                        plan.outputPlaneWcsJson =
                            metricsReader->readOutputWcsJson(group.registrationGroupKey, *settingsVersion);
                        // The record describes the registration's plane, which the canvas is drizzled over unless the
                        // lights are placed on a comet's: only then is it checked against the registration's reference
                        // and moved onto the drizzled canvas.
                        if (geometry.cometRunId.isEmpty())
                        {
                            plan.registrationReference = registrationReferenceOf().canonicalPath();
                        }
                    }
                    plan.drizzle = std::move(geometry);
                    return PlanOutcome{.plan = std::move(plan), .result = std::nullopt};
                }

                if (cometOnly || starsPlusComet)
                {
                    QString error;
                    auto request = ImageIntegration::cometProductRequestFor(metricsPath, group, settingsVersion, false,
                                                                            executionCounterAccumulator(), error);
                    if (!request.has_value())
                    {
                        return planFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::MissingRequiredFrames,
                            error.isEmpty()
                                ? QStringLiteral("Comet stacking could not resolve the registration context.")
                                : error,
                            outputPath, groupLabel)));
                    }
                    if (starsPlusComet)
                    {
                        auto products = an::ui::resolveStarsPlusCometExperimentalProducts(*request);
                        if (!products.available)
                        {
                            const QString message =
                                products.errorMessage.trimmed().isEmpty()
                                    ? QStringLiteral("%1 stacking requires matching star base and pure comet products.")
                                          .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken())
                                    : products.errorMessage;
                            return planFailure(StageResult::failure(IntegrationError(
                                IntegrationErrorCode::MissingRequiredFrames, message, outputPath, groupLabel)));
                        }
                        plan.route = GroupRoute::StarsPlusComet;
                        plan.plane = NormalizationMeasurementPlane::RegisteredCanvas;
                        plan.cometProductsFingerprint = starsPlusCometProductsFingerprint(products);
                        for (const FrameId& light : inputFrames)
                        {
                            const auto star =
                                std::find_if(products.starBaseProducts.begin(), products.starBaseProducts.end(),
                                             [&light](const auto& product) {
                                                 return product.frameId.identityKey() == light.identityKey();
                                             });
                            if (star == products.starBaseProducts.end())
                            {
                                plan.dropped << QStringLiteral("%1 (no star base product)").arg(light.canonicalPath());
                                continue;
                            }
                            stage_utils::StackFrameRead read;
                            read.logical = light;
                            read.path = star->outputPath;
                            const auto revision = products.starBaseRevisions.find(light.identityKey());
                            if (revision != products.starBaseRevisions.end())
                            {
                                read.revision = revision->second;
                            }
                            plan.frames.frames.push_back(std::move(read));
                        }
                        plan.policy =
                            QStringLiteral("Stars + comet composite (run %1)").arg(products.cometProducts.cometRunId);
                        plan.composite = std::move(products);
                        return PlanOutcome{.plan = std::move(plan), .result = std::nullopt};
                    }
                    const auto products = an::ui::resolveCometStackingProducts(*request);
                    if (!products.available || products.kind != an::ui::CometStackingProductKind::RegisteredOutputs)
                    {
                        return planFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::MissingRequiredFrames,
                            products.errorMessage.trimmed().isEmpty()
                                ? QStringLiteral("Comet stacking requires comet_aligned registered outputs.")
                                : products.errorMessage,
                            outputPath, groupLabel)));
                    }
                    plan.route = GroupRoute::Comet;
                    plan.plane = NormalizationMeasurementPlane::RegisteredCanvas;
                    plan.frames.validity = an::ValidityRule::ZeroIsNoData;
                    plan.cometProductsFingerprint = ImageIntegration::fingerprintCometProducts(products);
                    for (const FrameId& light : inputFrames)
                    {
                        const auto output =
                            std::find_if(products.registeredOutputs.begin(), products.registeredOutputs.end(),
                                         [&light](const auto& candidate) {
                                             return candidate.frameId.identityKey() == light.identityKey();
                                         });
                        if (output == products.registeredOutputs.end() || !output->succeeded)
                        {
                            plan.dropped << QStringLiteral("%1 (no comet-aligned image)").arg(light.canonicalPath());
                            continue;
                        }
                        stage_utils::StackFrameRead read;
                        read.logical = light;
                        read.path = output->outputPath;
                        const auto revision = products.registeredOutputRevisions.find(light.identityKey());
                        if (revision != products.registeredOutputRevisions.end())
                        {
                            read.revision = revision->second;
                        }
                        plan.frames.frames.push_back(std::move(read));
                    }
                    plan.policy = QStringLiteral("Comet-aligned registered images (run %1)").arg(products.cometRunId);
                    return PlanOutcome{.plan = std::move(plan), .result = std::nullopt};
                }

                const bool registrationExpected = registration != nullptr && !registrationKey.trimmed().isEmpty() &&
                                                  metricsReader != nullptr && settingsVersion.has_value();
                if (!registrationExpected)
                {
                    if (wantsLocal)
                    {
                        reporter.logger().warning(QStringLiteral("Local normalization requires registered/aligned "
                                                                 "outputs. Registration data was not found for this "
                                                                 "group."));
                        return planFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::MissingRequiredFrames,
                            QStringLiteral("Local normalization requires registered/aligned outputs from registration"),
                            {},
                            QStringLiteral("group=%1, registrationGroup=%2, metricsDbPresent=%3")
                                .arg(groupLabel, group.registrationGroupKey.toCompactString(),
                                     hasMetricsDb ? QStringLiteral("true") : QStringLiteral("false")))));
                    }
                    if (!registrationKey.trimmed().isEmpty())
                    {
                        warn(QStringLiteral("No registration record was found for this group; its lights are stacked "
                                            "as they are read."));
                    }
                    plan.route = GroupRoute::Frames;
                    plan.plane = NormalizationMeasurementPlane::Frames;
                    for (const FrameId& light : inputFrames)
                    {
                        // The file as it is now: this run's calibration may have written it since the execution
                        // plan was made.
                        const QString path = registrationInputPathForFrame(
                            manifest.outputDirectory, manifest.calibrateLightsInMemory, registration, light);
                        stage_utils::StackFrameRead read;
                        read.logical = light;
                        read.path = path;
                        read.calibration = calibrationOf(manifest, snapshot, light, path);
                        read.revision = readRevision(path);
                        plan.frames.frames.push_back(std::move(read));
                    }
                    plan.policy = QStringLiteral("Lights as read (no registration)");
                    return PlanOutcome{.plan = std::move(plan), .result = std::nullopt};
                }

                plan.plane = NormalizationMeasurementPlane::RegisteredCanvas;
                plan.frames.validity = an::ValidityRule::ZeroIsNoData;
                plan.outputPlaneWcsJson =
                    metricsReader->readOutputWcsJson(group.registrationGroupKey, *settingsVersion);
                const FrameId registrationReference = registrationReferenceOf();
                plan.registrationReference = registrationReference.canonicalPath();

                // The route: the registered images, or the registration inputs warped as they are read.
                std::unordered_map<QString, an::RegistrationTransform> transforms;
                std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
                QString transformError;
                const bool transformsLoaded =
                    metricsReader->loadTransforms(registrationKey, *settingsVersion, transforms, transformError,
                                                  &provenance) &&
                    stage_utils::rekeyRegistrationFrameLookupByIdentity(transforms, &transformError) &&
                    stage_utils::rekeyRegistrationFrameLookupByIdentity(provenance, &transformError);
                ImageIntegration::TransformOnReadRouting routing =
                    ImageIntegration::decideLightsTransformOnRead(manifest, group);
                if (routing.usesTransformOnRead())
                {
                    std::vector<ImageIntegration::RegisteredLightRecord> lights;
                    for (const FrameId& light : inputFrames)
                    {
                        ImageIntegration::RegisteredLightRecord record;
                        record.frame = light;
                        record.inputPath = registrationInputPathForFrame(
                            manifest.outputDirectory, manifest.calibrateLightsInMemory, registration, light);
                        const auto transform = transforms.find(light.identityKey());
                        if (transformsLoaded && transform != transforms.end())
                        {
                            const auto recorded = provenance.find(light.identityKey());
                            record.transformJson = stage_utils::serializeTransformJson(
                                transform->second, recorded != provenance.end()
                                                       ? recorded->second
                                                       : stage_utils::SourceDistortionProvenance::inheritMetadata());
                        }
                        lights.push_back(std::move(record));
                    }
                    routing = ImageIntegration::confirmLightsTransformOnReadFor(
                        manifest, group, lights,
                        registrationInputPathForFrame(manifest.outputDirectory, manifest.calibrateLightsInMemory,
                                                      registration, registrationReference));
                }
                const QString routeDescription = routing.detail.isEmpty()
                                                     ? routing.reason
                                                     : QStringLiteral("%1: %2").arg(routing.reason, routing.detail);

                if (routing.usesTransformOnRead())
                {
                    an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
                    if (!scope.hasValue())
                    {
                        return planFailure(engineFailure(scope.status(), IntegrationErrorCode::StackingFailed,
                                                         QStringLiteral("Stacking could not start")));
                    }
                    std::vector<stage_utils::RegistrationOutputPlaneFrame> planeFrames;
                    std::size_t referenceIndex = 0u;
                    std::unordered_map<QString, an::RegistrationTransform> usedTransforms;
                    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> usedProvenance;
                    std::unordered_map<QString, std::optional<an::SipDistortion>> usedResolved;
                    bool anyDistortion = false;
                    for (const FrameId& light : inputFrames)
                    {
                        const auto transform = transforms.find(light.identityKey());
                        const QString path = registrationInputPathForFrame(
                            manifest.outputDirectory, manifest.calibrateLightsInMemory, registration, light);
                        const an::Result<stage_utils::RegistrationFrameFacts> facts =
                            stage_utils::probeRegistrationFrame(*scope, registry, path);
                        if (!facts.hasValue())
                        {
                            return planFailure(engineFailure(facts.status(), IntegrationErrorCode::FileNotReadable,
                                                             QStringLiteral("A light's header could not be read"),
                                                             path));
                        }
                        const auto recorded = provenance.find(light.identityKey());
                        const stage_utils::SourceDistortionProvenance lightProvenance =
                            recorded != provenance.end() ? recorded->second
                                                         : stage_utils::SourceDistortionProvenance::inheritMetadata();
                        std::optional<an::SipDistortion> distortion;
                        if (lightProvenance.policy == stage_utils::SourceDistortionPolicy::ExplicitModel)
                        {
                            distortion = lightProvenance.model;
                        }
                        else if (lightProvenance.policy == stage_utils::SourceDistortionPolicy::InheritMetadata)
                        {
                            distortion = facts->distortion;
                        }
                        anyDistortion = anyDistortion || distortion.has_value();
                        if (light.identityKey() == registrationReference.identityKey())
                        {
                            referenceIndex = planeFrames.size();
                        }
                        planeFrames.push_back({transform->second, facts->width, facts->height, distortion});
                        usedTransforms.emplace(light.canonicalPath(), transform->second);
                        usedProvenance.emplace(light.canonicalPath(), lightProvenance);
                        usedResolved.emplace(light.canonicalPath(), distortion);
                        stage_utils::StackFrameRead read;
                        read.logical = light;
                        read.path = path;
                        read.calibration = calibrationOf(manifest, snapshot, light, path);
                        read.warp = true;
                        read.transform = transform->second;
                        read.distortion = distortion;
                        read.revision = readRevision(path);
                        plan.frames.frames.push_back(std::move(read));
                    }
                    const an::Result<stage_utils::PlannedRegistrationOutputPlane> canvas =
                        stage_utils::planRegistrationOutputPlane(planeFrames, referenceIndex, canvasMode);
                    if (!canvas.hasValue())
                    {
                        return planFailure(StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                            canvas.status(), IntegrationErrorCode::RegistrationFailed,
                            QStringLiteral("The registration's plane could not be planned"))));
                    }
                    if (canvas->intersectionReplaced)
                    {
                        warn(stage_utils::intersectionReplacedWarning(groupLabel));
                    }
                    plan.frames.canvas = canvas->canvas;
                    plan.registrationCanvas = canvas->canvas;
                    plan.planeMode = canvas->usedMode;
                    plan.expectedSipCorrection = anyDistortion;
                    plan.route = GroupRoute::TransformOnRead;
                    plan.transformsFingerprint = stage_utils::fingerprintTransforms(usedTransforms);
                    plan.distortionFingerprint =
                        stage_utils::fingerprintSourceDistortions(usedTransforms, usedProvenance, usedResolved);
                    plan.policy = QStringLiteral("Transform-on-read (%1)").arg(routeDescription);
                    return PlanOutcome{.plan = std::move(plan), .result = std::nullopt};
                }

                // The registered images the alignment wrote, each held to the revision its record states.
                std::unordered_map<QString, stage_utils::RegistrationOutputRecord> outputs;
                if (!metricsReader->loadOutputRecords(registrationKey, *settingsVersion, outputs) ||
                    !stage_utils::rekeyRegistrationFrameLookupByIdentity(outputs))
                {
                    outputs.clear();
                }
                QStringList stale;
                for (const FrameId& light : inputFrames)
                {
                    const auto record = outputs.find(light.identityKey());
                    if (record == outputs.end() || !record->second.succeeded || record->second.outputPath.isEmpty() ||
                        !QFileInfo::exists(record->second.outputPath))
                    {
                        plan.dropped << QStringLiteral("%1 (no registered output)").arg(light.canonicalPath());
                        continue;
                    }
                    const std::optional<FileRevisionStamp> revision = currentRegisteredOutputRevision(record->second);
                    if (!revision.has_value())
                    {
                        stale << record->second.outputPath;
                        continue;
                    }
                    stage_utils::StackFrameRead read;
                    read.logical = light;
                    read.path = record->second.outputPath;
                    read.revision = revision;
                    plan.frames.frames.push_back(std::move(read));
                }
                if (!stale.isEmpty())
                {
                    stale.sort(Qt::CaseInsensitive);
                    return planFailure(StageResult::failure(IntegrationError(
                        IntegrationErrorCode::MissingRequiredFrames,
                        QStringLiteral("A registered light output changed after it was persisted. Run alignment again "
                                       "before stacking."),
                        stale.front(),
                        QStringLiteral("Stale registered outputs: %1").arg(stale.join(QLatin1Char(';'))))));
                }
                if (plan.frames.frames.empty())
                {
                    reporter.logger().warning(QStringLiteral(
                        "No registered light outputs were found; stacking cannot continue with drizzle disabled."));
                    return planFailure(StageResult::failure(IntegrationError(
                        IntegrationErrorCode::MissingRequiredFrames,
                        QStringLiteral("No registered light frames were found. Drizzle is disabled, so integration "
                                       "requires registered outputs on disk. Please run registration/alignment and try "
                                       "again."))));
                }
                // The alignment wrote the registered images onto the standard canvas in place of an intersection no
                // area is common to, and recorded the plane under the mode it was planned by: the record of an
                // intersection group names either mode, and its extent - which the master's must equal - says which
                // plane it is.
                if (canvasMode == ImageIntegration::RegistrationCanvasMode::Intersection &&
                    plan.outputPlaneWcsJson.has_value())
                {
                    stage_utils::OutputPlaneWcsDescriptorInfo recorded;
                    QString recordError;
                    if (stage_utils::parseOutputPlaneWcsJson(*plan.outputPlaneWcsJson, nullptr, &recorded,
                                                             recordError) &&
                        stage_utils::parseAlignmentMode(recorded.alignmentMode) ==
                            ImageIntegration::RegistrationCanvasMode::Standard)
                    {
                        plan.planeMode = ImageIntegration::RegistrationCanvasMode::Standard;
                    }
                }
                plan.route = GroupRoute::RegisteredImages;
                plan.policy = QStringLiteral("Materialized registered images (%1)").arg(routeDescription);
                return PlanOutcome{.plan = std::move(plan), .result = std::nullopt};
            };

            PlanOutcome planned = planGroup();
            if (planned.result.has_value())
            {
                return *planned.result;
            }
            GroupPlan& plan = *planned.plan;
            if (!plan.dropped.isEmpty())
            {
                warn(QStringLiteral("Skipping %1 light frame(s): %2")
                         .arg(plan.dropped.size())
                         .arg(plan.dropped.join(QStringLiteral("; "))));
            }
            if (plan.frames.frames.size() < 2u)
            {
                return StageResult::failure(IntegrationError(
                    IntegrationErrorCode::MissingRequiredFrames,
                    QStringLiteral("Not enough registered frames to integrate after skipping missing outputs.")));
            }
            std::vector<FrameId> stackedLights;
            std::vector<FrameId> stackInputs;
            for (const auto& frame : plan.frames.frames)
            {
                stackedLights.push_back(frame.logical);
                stackInputs.emplace_back(frame.path);
            }
            if (plan.route == GroupRoute::StarsPlusComet && plan.composite.has_value())
            {
                for (const auto& output : plan.composite->cometProducts.registeredOutputs)
                {
                    stackInputs.emplace_back(output.outputPath);
                }
            }

            // The reference, among the lights the stack reads.
            const FrameId referenceLight = referenceFrames.front();
            std::size_t referenceIndex = 0u;
            const auto referenceFound =
                std::find_if(stackedLights.begin(), stackedLights.end(),
                             [&](const FrameId& light) { return light.identityKey() == referenceLight.identityKey(); });
            if (referenceFound != stackedLights.end())
            {
                referenceIndex = static_cast<std::size_t>(std::distance(stackedLights.begin(), referenceFound));
            }
            else if (userReferences)
            {
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                     QStringLiteral("Normalization reference frame not found in group inputs"),
                                     referenceLight.canonicalPath(), groupLabel));
            }
            else
            {
                warn(QStringLiteral("Reference frame was not available; using the first available frame."));
            }

            // The stack's identity, and whether the master on disk already is it.
            const InputsFingerprint inputsFingerprint = fingerprintInputs(stackInputs);
            const QString artifactHash = computeStackedLightArtifactHash(
                manifest, group, inputsFingerprint.fingerprint, plan.transformsFingerprint, plan.distortionFingerprint,
                plan.cometProductsFingerprint, nullptr);
            const bool canSkip = group.skipIfExists && !manifest.reprocessExisting;
            if (canSkip && tracker.isArtifactValid(outputPath, artifactHash))
            {
                reporter.logger().operationDetail(QStringLiteral("Skipping"), QStringLiteral("Output up-to-date"));
                report(1.0f, QStringLiteral("Lights stacking (skipped)"));
                reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTiming.elapsedNowMs());
                return StageResult::success();
            }
            if (canSkip)
            {
                QString reason = QStringLiteral("Settings or inputs changed.");
                const QFileInfo outInfo(outputPath);
                if (!outInfo.exists())
                {
                    reason = QStringLiteral("Output missing.");
                }
                else if (inputsFingerprint.missingFiles > 0)
                {
                    reason = QStringLiteral("Some input files are missing.");
                }
                else if (inputsFingerprint.maxMtimeMs > outInfo.lastModified().toMSecsSinceEpoch())
                {
                    reason = QStringLiteral("Inputs are newer than output.");
                }
                reporter.logger().operationDetail(QStringLiteral("Reprocessing"), reason);
            }
            else if (manifest.reprocessExisting)
            {
                reporter.logger().operationDetail(QStringLiteral("Reprocessing"),
                                                  QStringLiteral("Reprocess existing enabled."));
            }
            else
            {
                reporter.logger().operationDetail(QStringLiteral("Reprocessing"),
                                                  QStringLiteral("Group is configured to always run."));
            }

            const QString methodRan = ImageIntegration::describeStackChoice(*choice);
            reporter.logger().operationDetail(QStringLiteral("Inputs"),
                                              QStringLiteral("Lights: %1 | Stacked: %2")
                                                  .arg(static_cast<int>(inputFrames.size()))
                                                  .arg(static_cast<int>(stackedLights.size())));
            reporter.logger().operationDetail(
                QStringLiteral("Settings"),
                stage_utils::describeStackingSettings(group.stacking, plan.route == GroupRoute::Drizzle));
            reporter.logger().operationDetail(QStringLiteral("Stack method"), methodRan);
            reporter.logger().operationDetail(QStringLiteral("STACKING reference frame"),
                                              stage_utils::describeReferenceSelection(group.stacking, referenceFrames));
            reporter.logger().operationDetail(QStringLiteral("Registration input policy"), plan.policy);
            if (manifest.calibrateLightsInMemory)
            {
                reporter.logger().operationDetail(
                    QStringLiteral("Calibration"),
                    plan.route == GroupRoute::RegisteredImages || plan.route == GroupRoute::Comet ||
                            plan.route == GroupRoute::StarsPlusComet
                        ? QStringLiteral("Already applied during in-memory alignment; stacking registered outputs.")
                        : QStringLiteral("Applying in-memory calibration during stacking."));
            }
            report(kPreparationPhaseEnd, QStringLiteral("Preparing stacking"));

            // What the stack is asked for beside its frames. The weights are read once the group's operation is
            // admitted, because an image count is read from each light's header.
            StackShape shape;
            shape.choice = *choice;
            const bool comet = plan.route == GroupRoute::Comet || plan.route == GroupRoute::StarsPlusComet;
            if (choice->rejection != an::StackRejection::None && stackedLights.size() < an::kMinimumRejectionFrames)
            {
                warn(QStringLiteral("Group '%1' has %2 frame(s): %3 needs at least %4, so the master is combined "
                                    "without it.")
                         .arg(groupLabel)
                         .arg(static_cast<int>(stackedLights.size()))
                         .arg(sanitized.rejectionMethod)
                         .arg(an::kMinimumRejectionFrames));
            }
            if (choice->rejection != an::StackRejection::None &&
                (sanitized.createRejectionMaps || sanitized.spatialRejectionClustering))
            {
                shape.rejectionMaps = sanitized.mergeRejectionMaps || !sanitized.createRejectionMaps
                                          ? an::RejectionMapForm::Merged
                                          : an::RejectionMapForm::LowAndHigh;
            }
            shape.qualityMaps = ImageIntegration::stackingQualityMapProductPolicy(sanitized);
            shape.referenceFrame = static_cast<std::uint32_t>(referenceIndex);

            // What the engine says of the group's work while it runs, said among the group's warnings after each
            // stack; declared before the operations it is lent to, so it outlives them.
            GroupStackDiagnostics diagnostics(groupLabel);
            const auto sayEngineWarnings = [&]() {
                for (const QString& warning : diagnostics.takeWarnings())
                {
                    appendUniqueWarnings(groupWarnings, QStringList{warning});
                    reporter.logger().warning(warning);
                }
            };

            // The operation the group stacks in.
            GroupStackProgress progress(reporter, stageId(), groupIndex, totalGroups, QStringLiteral("Lights stacking"),
                                        outputPath, &progressFloor);
            an::Result<an::consumer::OperationScope> admitted = services->admitStageOperation(&progress, &diagnostics);
            if (!admitted.hasValue())
            {
                return engineFailure(admitted.status(), IntegrationErrorCode::StackingFailed,
                                     QStringLiteral("Stacking could not start"), outputPath);
            }
            an::consumer::OperationScope& scope = *admitted;
            stage_utils::StackFrameReader reader(scope, registry, plan.frames, masters);
            const auto nameOf = [&plan](const std::uint32_t index) {
                return index < plan.frames.frames.size() ? plan.frames.frames[index].path : QString();
            };

            // Each light's header, read once: how many frames it combines - what a group weighted by the number of
            // images weighs it by, and what the master states it combines - and its cards, which the master takes the
            // reference's and a mosaic every light's pointing from.
            std::vector<std::uint32_t> combinedFrames;
            std::vector<std::optional<an::CodecImageInfo>> lightHeaders;
            combinedFrames.reserve(stackedLights.size());
            lightHeaders.reserve(stackedLights.size());
            for (const FrameId& light : stackedLights)
            {
                an::Result<an::CodecImageInfo> probed = probeFile(scope, registry, light.canonicalPath());
                combinedFrames.push_back(
                    probed.hasValue() ? ImageIntegration::combinedFrameCount(probed->metadata.codec().cards()) : 1u);
                lightHeaders.push_back(probed.hasValue() ? std::optional<an::CodecImageInfo>(std::move(*probed))
                                                         : std::nullopt);
            }

            // The weights: an image count from those, every other figure from the registration's records.
            GroupWeights weights;
            if (ImageIntegration::combinationTakesWeights(choice->combination) && !comet &&
                choice->weighting != an::StackWeighting::Uniform)
            {
                weights = weightsFor(choice->weighting, stackedLights, combinedFrames, metricsPath, registrationKey,
                                     plan.settingsVersion, executionCounterAccumulator());
                if (!weights.fallback.isEmpty())
                {
                    warn(QStringLiteral("%1 weighting is unavailable for %2 (%3); stacking this group unweighted.")
                             .arg(sanitized.weighting.trimmed(), groupLabel, weights.fallback));
                }
            }
            shape.weighting = weights.method;

            // The normalization: the normalization stage's coefficients for the frames the stack reads, measured under
            // the settings the group stacks with, or the stack's own measurement when none describe them.
            std::optional<ImageIntegration::NormalizationCoefficientSet> coefficients;
            if (sanitized.normalizationMode != NormalizationMode::None && !sanitized.hdr.enabled)
            {
                // A drizzled Local group's stack fits its surfaces itself and applies no document, so none is read: one
                // an earlier run measured, under settings the group may no longer state, would only be said refused.
                if (!ImageIntegration::localNormalizationFittedByDrizzledStack(group))
                {
                    const QString path = stage_utils::normalizationArtifactPath(manifest, group);
                    ImageIntegration::NormalizationDocumentRead read =
                        ImageIntegration::readStackNormalizationCoefficients(path, group, referenceFrames);
                    if (read.coefficients.has_value())
                    {
                        coefficients = std::move(read.coefficients);
                    }
                    else if (read.failure == ImageIntegration::NormalizationDocumentFailure::OtherSettings)
                    {
                        warn(QStringLiteral("The normalization coefficients were measured under settings other than "
                                            "the group's; the stack measures its own."));
                    }
                    else if (QFileInfo::exists(path))
                    {
                        warn(QStringLiteral("The normalization coefficients could not be read (%1); the stack measures "
                                            "its own.")
                                 .arg(read.message));
                    }
                }
                shape.estimator = ImageIntegration::scaleEstimatorFor(sanitized.scaleEstimator);
                if (wantsLocal)
                {
                    shape.normalization = an::StackNormalizationScope::LocalThenGlobal;
                    shape.globalMethod = an::NormalizationMethod::AdditiveScaling;
                }
                else
                {
                    shape.normalization = an::StackNormalizationScope::Global;
                    shape.globalMethod = ImageIntegration::normalizationMethodFor(sanitized.globalNormalizationMethod);
                }
            }

            if (m_preStackGenerationHook)
            {
                m_preStackGenerationHook(stackInputs);
            }

            // Runs one stack over the frames a source reads, the normalization the group's coefficients state where
            // they describe those frames.
            const auto runStack = [&](an::StackFrameSource& source, const an::ImageDescriptor& input,
                                      const std::vector<std::uint32_t>& frames, const bool cometLayer,
                                      const std::optional<std::vector<std::uint32_t>>& normalizationFrames,
                                      const NormalizationMeasurementPlane measuredPlane,
                                      const std::optional<ImageIntegration::DrizzleAnalysisPlaneRecord>& drizzlePlane,
                                      const std::optional<std::pair<std::uint32_t, std::uint32_t>>& measuredExtent)
                -> an::Result<StackRun> {
                StackRun run;
                StackShape runShape = shape;
                const auto frameCount = static_cast<std::uint32_t>(frames.size());
                std::optional<ImageIntegration::StackNormalizationInputs> prepared;
                an::StackExecutionRequest execution;
                if (sanitized.limitMemory && sanitized.maxMemoryMb > 0)
                {
                    execution.workingSetLimitBytes = static_cast<std::uint64_t>(sanitized.maxMemoryMb) * 1024u * 1024u;
                }
                // A source that reads bands reads several frames' bands of a tile at once, one on each worker, into a
                // tile kept to the cache (`stackBandTileRows`) - or holding the rows one compressed piece of a light
                // spans, which a narrower tile would decode again for every tile that reaches it.
                std::uint32_t granularityRows = 1u;
                if (source.access() == an::StackSourceAccess::ConcurrentRegion)
                {
                    if (const auto* files = dynamic_cast<const stage_utils::WorkflowStackFrameSource*>(&source))
                    {
                        granularityRows = files->decodeGranularityRows();
                    }
                    const std::uint32_t workers = scope.execution().admittedConcurrency();
                    execution.concurrentFrameReads = (std::max)(1u, (std::min)(frameCount, workers));
                    execution.maximumTileRows = stage_utils::stackBandTileRows(
                        frameCount, input.extent().width(), input.extent().channelCount(), workers, granularityRows);
                    execution.sourceGranularityRows = granularityRows;
                }
                if (cometLayer)
                {
                    runShape.normalization = an::StackNormalizationScope::None;
                    runShape.globalMethod.reset();
                }
                // The group's reference among the frames this stack reads. The coefficients are handed over in the
                // order of those frames, so a position among them is one among `frames`.
                const auto referencePosition =
                    std::find(frames.begin(), frames.end(), static_cast<std::uint32_t>(referenceIndex));
                const std::uint32_t ownReference =
                    referencePosition != frames.end()
                        ? static_cast<std::uint32_t>(std::distance(frames.begin(), referencePosition))
                        : 0u;
                if (runShape.normalization != an::StackNormalizationScope::None && coefficients.has_value() &&
                    normalizationFrames.has_value())
                {
                    ImageIntegration::StackNormalizationExpectation expectation;
                    for (const std::uint32_t index : *normalizationFrames)
                    {
                        expectation.logicalFrames.push_back(stackedLights.at(index).canonicalPath());
                    }
                    expectation.referenceFrame = ownReference;
                    expectation.plane = measuredPlane;
                    expectation.channelCount = input.extent().channelCount();
                    expectation.width = measuredExtent.has_value() ? measuredExtent->first : input.extent().width();
                    expectation.height = measuredExtent.has_value() ? measuredExtent->second : input.extent().height();
                    expectation.drizzlePlane = drizzlePlane;
                    QString reason;
                    prepared = ImageIntegration::stackNormalizationInputsFor(*coefficients, expectation, reason);
                    if (!prepared.has_value())
                    {
                        warn(QStringLiteral("The normalization coefficients do not describe the frames the stack reads "
                                            "(%1); the stack measures its own.")
                                 .arg(reason));
                    }
                    else if (prepared->local.has_value() &&
                             (drizzlePlane.has_value() ||
                              runShape.normalization == an::StackNormalizationScope::Global))
                    {
                        prepared.reset();
                    }
                }
                if (prepared.has_value() && !prepared->local.has_value() &&
                    runShape.normalization == an::StackNormalizationScope::LocalThenGlobal)
                {
                    prepared.reset();
                }
                if (runShape.normalization == an::StackNormalizationScope::LocalThenGlobal && !prepared.has_value())
                {
                    execution.localNormalization =
                        ImageIntegration::localNormalizationRequestFor(sanitized.localNormalization);
                    execution.fitLocalScaleGrid = sanitized.localNormalization.localScaleEnabled;
                }
                std::vector<an::StackFrameMeasurement> measurements;
                if (runShape.weighting != an::StackWeighting::Uniform && !weights.measurements.empty())
                {
                    for (const std::uint32_t index : frames)
                    {
                        measurements.push_back(weights.measurements.at(index));
                    }
                    execution.measurements = measurements;
                }
                // A local document is applied over the light it names, which the engine reads from the stack's one
                // reference - the light the noise and width weights are relative to as well.
                runShape.referenceFrame = prepared.has_value() ? prepared->referenceFrame : ownReference;
                // The frames are weighed against the group's reference, the light the Frame Quality dashboard and the
                // report weigh them against: where a local document normalizes them to another light, the weights
                // are derived against the group's, by the same rule (`deriveStackWeights`), and handed over as they
                // are, since the engine reads both from its one reference.
                std::vector<double> derivedWeights;
                if (!measurements.empty() && runShape.referenceFrame != ownReference && !cometLayer &&
                    ImageIntegration::combinationTakesWeights(runShape.choice.combination))
                {
                    const an::Result<ImageIntegration::DerivedStackWeights> derived =
                        ImageIntegration::deriveStackWeights(
                            scope.execution(),
                            ImageIntegration::stackWeightingRequestFor(sanitized, runShape.weighting,
                                                                       runShape.choice.starCountScaling),
                            measurements, ownReference);
                    if (!derived.hasValue())
                    {
                        return derived.status();
                    }
                    derivedWeights = derived->weights;
                    runShape.weighting = an::StackWeighting::Explicit;
                    execution.measurements = {};
                    execution.explicitWeights = an::StackFrameWeights{.perFrame = derivedWeights};
                }
                an::Result<an::StackRequest> request =
                    stackRequestFor(sanitized, runShape, input, frameCount, cometLayer);
                if (!request.hasValue())
                {
                    return request.status();
                }
                run.model = an::StackModel::create(*request);
                if (!run.model.has_value())
                {
                    return refusal(an::ErrorCode::InvalidArgument,
                                   QStringLiteral("The stack model refused the request"));
                }
                an::StackSourceContract contract{.state = an::StackFrameState::Calibrated};
                if (prepared.has_value())
                {
                    contract.normalization = prepared->global;
                    if (prepared->local.has_value())
                    {
                        contract.localNormalization = &*prepared->local;
                    }
                }
                run.rates.assign(frameCount, an::FrameRejectionRate{});
                run.flagged.assign(frameCount, 0u);
                std::vector<double> combinedWeights(static_cast<std::size_t>(frameCount) *
                                                    input.extent().channelCount());
                an::StackExecutionOutputs outputs;
                outputs.frameWeights = combinedWeights;
                if (request->rejection.method != an::StackRejection::None)
                {
                    outputs.frameRejectionRates = run.rates;
                    outputs.flaggedFrames = run.flagged;
                }
                an::Result<an::StackResult> result = an::executeStack(scope.engine(), scope.execution(), *run.model,
                                                                      source, contract, execution, outputs);
                if (!result.hasValue() && prepared.has_value() &&
                    (result.status().code() == an::ErrorCode::IncompatibleArguments ||
                     result.status().code() == an::ErrorCode::InvalidArgument))
                {
                    // Coefficients the engine refuses for these frames are let go of, and the stack measures its own.
                    warn(QStringLiteral("The normalization coefficients were refused by the stack (%1); the stack "
                                        "measures its own.")
                             .arg(QString::fromUtf8(result.status().message())));
                    if (runShape.normalization == an::StackNormalizationScope::LocalThenGlobal)
                    {
                        execution.localNormalization =
                            ImageIntegration::localNormalizationRequestFor(sanitized.localNormalization);
                        execution.fitLocalScaleGrid = sanitized.localNormalization.localScaleEnabled;
                    }
                    prepared.reset();
                    if (runShape.referenceFrame != ownReference)
                    {
                        // The stack's own measurement is made against the group's reference.
                        runShape.referenceFrame = ownReference;
                        request = stackRequestFor(sanitized, runShape, input, frameCount, cometLayer);
                        if (!request.hasValue())
                        {
                            return request.status();
                        }
                        run.model = an::StackModel::create(*request);
                        if (!run.model.has_value())
                        {
                            return refusal(an::ErrorCode::InvalidArgument,
                                           QStringLiteral("The stack model refused the request"));
                        }
                    }
                    result = an::executeStack(scope.engine(), scope.execution(), *run.model, source,
                                              an::StackSourceContract{.state = an::StackFrameState::Calibrated},
                                              execution, outputs);
                }
                sayEngineWarnings();
                if (!result.hasValue())
                {
                    return result.status();
                }
                if (source.access() == an::StackSourceAccess::ConcurrentRegion)
                {
                    const an::StackTilePlan& tile = result->report.plan.tile;
                    reporter.logger().operationDetail(
                        QStringLiteral("Stack tiles"),
                        tile.tileCount == 1u
                            ? QStringLiteral("One tile of %1 rows").arg(tile.rows)
                            : QStringLiteral("%1 tiles of %2 rows").arg(tile.tileCount).arg(tile.rows));
                    if (tile.tileCount > 1u && tile.rows < granularityRows)
                    {
                        warn(QStringLiteral("The memory limit keeps the stack's tiles at %1 rows, fewer than the %2 "
                                            "rows one compressed piece of a light spans, so each piece is decoded "
                                            "about %3 times.")
                                 .arg(tile.rows)
                                 .arg(granularityRows)
                                 .arg(1u + (granularityRows + tile.rows - 1u) / tile.rows));
                    }
                }
                run.frameWeights = perFrameWeights(combinedWeights, frameCount);
                if (result->report.frameOutliersDetected)
                {
                    run.flagged.resize(result->report.frameOutliers.flaggedFrameCount);
                }
                else
                {
                    run.flagged.clear();
                }
                if (runShape.normalization != an::StackNormalizationScope::None)
                {
                    const QString reference =
                        QFileInfo(stackedLights.at(frames.at(runShape.referenceFrame)).canonicalPath()).fileName();
                    if (prepared.has_value())
                    {
                        run.normalization =
                            QStringLiteral("The normalization stage's coefficients, reference %1").arg(reference);
                    }
                    else if (drizzlePlane.has_value() &&
                             runShape.normalization == an::StackNormalizationScope::LocalThenGlobal)
                    {
                        // A drizzled Local group has no document: the stack fits the surfaces on the drizzled lights,
                        // reading none of the analysis the normalization stage makes.
                        run.normalization =
                            QStringLiteral("Local normalization fitted by the drizzled stack, reference %1; the "
                                           "reference strategy, the evaluation, the PSF flux scale and the hot-pixel "
                                           "and noise filters do not apply")
                                .arg(reference);
                    }
                    else
                    {
                        run.normalization = QStringLiteral("Measured by the stack, reference %1").arg(reference);
                    }
                }
                run.result = std::move(*result);
                return run;
            };

            // How the stack read its frames - a band at a time from their files, or whole and why - said once for the
            // group, after the first stack has decided it.
            bool frameReadsSaid = false;
            const auto sayFrameReads = [&](const stage_utils::WorkflowStackFrameSource& source) {
                if (frameReadsSaid)
                {
                    return;
                }
                frameReadsSaid = true;
                const QString pieces =
                    source.decodeGranularityRows() > 1u
                        ? QStringLiteral("; the lights are compressed in pieces of %1 rows, which the stack's tiles "
                                         "hold where the memory allows")
                              .arg(source.decodeGranularityRows())
                        : QString();
                reporter.logger().operationDetail(
                    QStringLiteral("Frame reads"),
                    source.readsBands()
                        ? QStringLiteral("Bands of rows, read from each light's file as the stack combines them") +
                              pieces
                        : QStringLiteral("Whole frames: %1").arg(source.wholeFrameReason()));
            };

            const auto stackFrames = [&](const std::vector<std::uint32_t>& frames) -> an::Result<StackRun> {
                stage_utils::WorkflowStackFrameSource source(
                    [&reader, frames](const std::uint32_t index) { return reader.read(frames.at(index)); },
                    static_cast<std::uint32_t>(frames.size()), plan.frames.validity,
                    [&nameOf, frames](const std::uint32_t index) { return nameOf(frames.at(index)); },
                    scope.engine().limits(), stage_utils::stackFrameBandPlan(reader, frames));
                const an::Result<an::ImageDescriptor> described = source.describe();
                if (!described.hasValue())
                {
                    return described.status();
                }
                sayFrameReads(source);
                return runStack(source, *described, frames, false, frames, plan.plane, std::nullopt, std::nullopt);
            };

            std::vector<std::uint32_t> allFrames(stackedLights.size());
            for (std::uint32_t index = 0; index < allFrames.size(); ++index)
            {
                allFrames[index] = index;
            }

            an::Result<StackRun> stacked =
                refusal(an::ErrorCode::Internal, QStringLiteral("the group was not stacked"));
            std::optional<an::Image<float>> exposureMerged;
            std::optional<StarsPlusCometCompositeOutput> composite;
            std::optional<an::ImageMetadata> referenceMetadata;
            StackQualityRecord quality;
            std::optional<DrizzleMasterGeometry> drizzleMaster;
            std::uint32_t masterGuard = 0u;
            std::uint32_t masterWidth = 0u;
            std::uint32_t masterHeight = 0u;

            if (plan.route == GroupRoute::Drizzle)
            {
                const ImageIntegration::DrizzleGroupGeometry& geometry = *plan.drizzle;
                stage_utils::WorkflowDrizzleFrameSource drizzleSource(
                    [&reader](const std::uint32_t index) { return reader.read(index); },
                    static_cast<std::uint32_t>(plan.frames.frames.size()), nameOf);
                const an::Result<an::ImageDescriptor> lightDescriptor = drizzleSource.describe();
                if (!lightDescriptor.hasValue())
                {
                    return engineFailure(lightDescriptor.status(), IntegrationErrorCode::DrizzleFailed,
                                         QStringLiteral("Drizzle stacking failed"), outputPath);
                }
                if (drizzleSource.firstMetadata() != nullptr)
                {
                    referenceMetadata = *drizzleSource.firstMetadata();
                }
                const auto& drizzle = *group.drizzle;
                if (drizzle.scale < 1.0f || drizzle.scale > 4.0f)
                {
                    return StageResult::failure(
                        IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                         QStringLiteral("Drizzle scale must be between 1.0 and 4.0")));
                }
                if (drizzle.pixelFraction <= 0.0f || drizzle.pixelFraction > 1.0f)
                {
                    return StageResult::failure(
                        IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                         QStringLiteral("Drizzle pixel fraction must be within (0.0, 1.0]")));
                }
                an::DrizzleKernel kernel = an::DrizzleKernel::Square;
                if (!stage_utils::parseKernel(drizzle.kernel, kernel))
                {
                    return StageResult::failure(IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                                                 QStringLiteral("Drizzle kernel is invalid"), {},
                                                                 drizzle.kernel));
                }
                if (drizzle.cfaAware && lightDescriptor->extent().channelCount() != 1u)
                {
                    return StageResult::failure(
                        IntegrationError(IntegrationErrorCode::IncompatibleChannels,
                                         QStringLiteral("CFA-aware drizzle requires single-channel input"),
                                         plan.frames.frames.front().path));
                }
                an::DrizzleRequest drizzleRequest{.input = *lightDescriptor,
                                                  .frameCount = static_cast<std::uint32_t>(plan.frames.frames.size())};
                drizzleRequest.parameters.kernel = kernel;
                drizzleRequest.parameters.scale = static_cast<double>(drizzle.scale);
                drizzleRequest.parameters.pixelFraction = static_cast<double>(drizzle.pixelFraction);
                drizzleRequest.parameters.nonFinite = an::DrizzleNonFinitePolicy::Exclude;
                // A filter array's samples are drizzled into one plane per colour; any other light into its own planes.
                const std::uint32_t drizzledPlanes =
                    lightDescriptor->isMosaiced() ? 3u : lightDescriptor->extent().channelCount();
                drizzleRequest.output.extent = an::ImageExtent::create(geometry.canvas.width, geometry.canvas.height,
                                                                       drizzledPlanes, scope.engine().limits());
                an::Result<an::DrizzleModel> drizzleModel = an::DrizzleModel::create(drizzleRequest);
                if (!drizzleModel.hasValue())
                {
                    return engineFailure(drizzleModel.status(), IntegrationErrorCode::DrizzleFailed,
                                         QStringLiteral("The drizzle cannot be made"), outputPath);
                }
                std::vector<an::DrizzleFrameParameters> drizzleFrames;
                for (const auto& frame : geometry.frames)
                {
                    an::Result<an::DrizzleMapDescriptor> descriptor = an::DrizzleMapDescriptor::create(
                        *drizzleModel, an::DrizzleMappingRequest{.transform = frame.transform,
                                                                 .postTranslation = geometry.canvas.origin,
                                                                 .sourceDistortion = frame.resolvedDistortion});
                    if (!descriptor.hasValue())
                    {
                        return engineFailure(descriptor.status(), IntegrationErrorCode::DrizzleFailed,
                                             QStringLiteral("A light cannot be drizzled onto the canvas"),
                                             frame.inputPath);
                    }
                    drizzleFrames.push_back(an::DrizzleFrameParameters{.geometry = std::move(*descriptor)});
                }
                an::Result<an::DrizzledStackFrameSource> drizzledSource =
                    an::DrizzledStackFrameSource::create(scope.engine(), *drizzleModel, drizzleSource, drizzleFrames);
                if (!drizzledSource.hasValue())
                {
                    return engineFailure(drizzledSource.status(), IntegrationErrorCode::DrizzleFailed,
                                         QStringLiteral("The drizzled frames cannot be stacked"), outputPath);
                }
                const ImageIntegration::DrizzleAnalysisPlaneRecord drizzlePlane{
                    geometry.canvas.origin.x, geometry.canvas.origin.y, geometry.canvas.analysisWidth,
                    geometry.canvas.analysisHeight, static_cast<double>(drizzle.scale)};
                // The engine renders a drizzled frame at its lights' level divided by the scale squared. Restated at
                // the lights' level, the master keeps that level at every scale, and the coefficients the analysis
                // plane measured - at the lights' own level - apply at the level they were measured at.
                const double drizzleScale = drizzleModel->request().parameters.scale;
                stage_utils::ScaledStackFrameSource atLightsLevel(*drizzledSource, drizzleScale * drizzleScale);
                stacked = runStack(atLightsLevel, drizzleModel->outputDescriptor(), allFrames, false, allFrames,
                                   NormalizationMeasurementPlane::DrizzleAnalysisPlane, drizzlePlane,
                                   std::pair{geometry.canvas.analysisWidth, geometry.canvas.analysisHeight});
                masterGuard = geometry.canvas.edgeGuard;
                masterWidth = geometry.canvas.outputWidth;
                masterHeight = geometry.canvas.outputHeight;
                // The registration's own plane, planned from the same transforms by the mode the drizzle canvas was,
                // gives the drizzle canvas's offset from the plane the registration's solution describes.
                std::vector<stage_utils::RegistrationOutputPlaneFrame> planeFrames;
                for (const auto& frame : geometry.frames)
                {
                    planeFrames.push_back({frame.transform, frame.width, frame.height, frame.resolvedDistortion});
                }
                const an::Result<stage_utils::PlannedRegistrationOutputPlane> registrationPlane =
                    stage_utils::planRegistrationOutputPlane(planeFrames, geometry.referenceIndex, geometry.canvasMode);
                if (registrationPlane.hasValue())
                {
                    drizzleMaster =
                        DrizzleMasterGeometry{.shiftX = geometry.canvas.origin.x - registrationPlane->canvas.origin.x,
                                              .shiftY = geometry.canvas.origin.y - registrationPlane->canvas.origin.y,
                                              .analysisWidth = geometry.canvas.analysisWidth,
                                              .analysisHeight = geometry.canvas.analysisHeight,
                                              .scale = static_cast<double>(drizzle.scale),
                                              .edgeGuard = geometry.canvas.edgeGuard};
                    plan.registrationCanvas = registrationPlane->canvas;
                }
            }
            else if (plan.route == GroupRoute::StarsPlusComet)
            {
                std::optional<StackRun> starRun;
                const CompositeLayerStacker stacker = [&](stage_utils::WorkflowStackFrameSource& source,
                                                          const bool cometLayer) -> an::Result<an::Image<float>> {
                    const an::Result<an::ImageDescriptor> described = source.describe();
                    if (!described.hasValue())
                    {
                        return described.status();
                    }
                    sayFrameReads(source);
                    if (!cometLayer && source.firstMetadata() != nullptr)
                    {
                        referenceMetadata = *source.firstMetadata();
                    }
                    an::Result<StackRun> run = runStack(
                        source, *described, allFrames, cometLayer, cometLayer ? std::nullopt : std::optional(allFrames),
                        NormalizationMeasurementPlane::RegisteredCanvas, std::nullopt, std::nullopt);
                    if (!run.hasValue())
                    {
                        return run.status();
                    }
                    an::Result<an::Image<float>> image = stackedMaster(
                        *run->result, *run->model,
                        source.firstMetadata() != nullptr ? *source.firstMetadata() : an::ImageMetadata{});
                    if (!cometLayer && image.hasValue())
                    {
                        starRun = std::move(*run);
                    }
                    return image;
                };
                StarsPlusCometCompositeRequest request;
                request.metricsPath = metricsPath;
                request.products = &*plan.composite;
                request.frames = stackedLights;
                request.referenceIndex = referenceIndex;
                request.canvasMode = canvasMode;
                request.counters = executionCounterAccumulator();
                an::Result<StarsPlusCometCompositeOutput> built =
                    buildStarsPlusCometComposite(scope, registry, request, stacker);
                if (!built.hasValue())
                {
                    if (isCancellation(built.status()))
                    {
                        return StageResult::cancelled();
                    }
                    return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                        built.status(), IntegrationErrorCode::StackingFailed,
                        QStringLiteral("Stars + comet composite failed"), outputPath));
                }
                reporter.logger().operationDetail(QStringLiteral("Stars + comet composite"), built->diagnostic);
                composite = std::move(*built);
                if (starRun.has_value())
                {
                    stacked = std::move(*starRun);
                }
            }
            else if (sanitized.hdr.enabled)
            {
                std::vector<double> exposures;
                const std::size_t listed = (std::min)(group.lightFiles.size(), group.lightExposureSeconds.size());
                for (const FrameId& light : stackedLights)
                {
                    double exposure = 0.0;
                    for (std::size_t index = 0; index < listed; ++index)
                    {
                        if (group.lightFiles[index].identityKey() == light.identityKey())
                        {
                            exposure = group.lightExposureSeconds[index];
                            break;
                        }
                    }
                    exposures.push_back(exposure);
                }
                const an::Result<std::vector<ImageIntegration::ExposureBucket>> buckets =
                    ImageIntegration::exposureMergeBuckets(exposures);
                if (!buckets.hasValue())
                {
                    return StageResult::failure(IntegrationError(
                        IntegrationErrorCode::InvalidConfiguration, QStringLiteral("The exposure merge cannot be made"),
                        outputPath, QString::fromUtf8(buckets.status().message())));
                }
                std::vector<an::Image<float>> bucketStacks;
                // Which samples each bucket holds: its stack's mask, or the samples its one light's rule admits.
                std::vector<an::ValidityMask> bucketSamples;
                for (const ImageIntegration::ExposureBucket& bucket : *buckets)
                {
                    if (bucket.frames.size() == 1u)
                    {
                        an::Result<an::Image<float>> single = reader.read(bucket.frames.front());
                        if (!single.hasValue())
                        {
                            return engineFailure(single.status(), IntegrationErrorCode::StackingFailed,
                                                 QStringLiteral("The exposure merge could not read a frame"),
                                                 outputPath);
                        }
                        // As a stack of the one light reads it: a sample that is not a number - a calibration's,
                        // where its flat is zero - is no sample, which the stack writes as zero.
                        an::Result<an::ValidityMask> held = heldSamplesOf(scope, *single, plan.frames.validity);
                        if (!held.hasValue())
                        {
                            return engineFailure(held.status(), IntegrationErrorCode::StackingFailed,
                                                 QStringLiteral("The exposure merge could not read a frame"),
                                                 outputPath);
                        }
                        if (const an::Result<std::uint64_t> restated = stage_utils::zeroNonFiniteSamples(*single);
                            !restated.hasValue())
                        {
                            return engineFailure(restated.status(), IntegrationErrorCode::StackingFailed,
                                                 QStringLiteral("The exposure merge could not read a frame"),
                                                 outputPath);
                        }
                        if (!referenceMetadata.has_value())
                        {
                            referenceMetadata = single->metadata();
                        }
                        bucketStacks.push_back(std::move(*single));
                        bucketSamples.push_back(std::move(*held));
                        continue;
                    }
                    an::Result<StackRun> run = stackFrames(bucket.frames);
                    if (!run.hasValue())
                    {
                        return engineFailure(run.status(), IntegrationErrorCode::StackingFailed,
                                             QStringLiteral("An exposure bucket could not be stacked"), outputPath);
                    }
                    an::Result<an::Image<float>> image = stackedMaster(*run->result, *run->model, an::ImageMetadata{});
                    if (!image.hasValue())
                    {
                        return engineFailure(image.status(), IntegrationErrorCode::StackingFailed,
                                             QStringLiteral("An exposure bucket could not be stacked"), outputPath);
                    }
                    bucketStacks.push_back(std::move(*image));
                    bucketSamples.push_back(std::move(run->result->mask));
                }
                // Blended longest first: each shorter exposure, scaled to the longest, takes over where the last one
                // approaches saturation, and wherever the merge so far holds no sample; a sample a shorter exposure
                // lacks leaves the merge as it is.
                const double longest = buckets->front().exposureSeconds;
                an::Image<float>& merged = bucketStacks.front();
                std::optional<an::ImageWriteAccess<float>> access = merged.write();
                const std::optional<an::ImageDescriptor> mergedDescriptor = merged.descriptor();
                if (!access.has_value() || !mergedDescriptor.has_value())
                {
                    return StageResult::failure(
                        IntegrationError::stackingFailed(QStringLiteral("The exposure merge holds nothing")));
                }
                // A mask that holds nothing says every sample is held.
                const std::optional<an::ValidityMaskView<std::uint8_t>> mergedHolds = bucketSamples.front().view();
                const auto holds = [](const auto& mask, const std::uint32_t plane, const std::uint32_t x,
                                      const std::uint32_t y) {
                    return !mask.has_value() || mask->test(plane, x, y).value_or(true);
                };
                std::vector<std::vector<float>> references;
                for (std::uint32_t plane = 0; plane < access->view().planeCount(); ++plane)
                {
                    for (std::uint32_t y = 0; y < access->view().height(); ++y)
                    {
                        const auto row = access->view().row(plane, y);
                        references.emplace_back(row->begin(), row->end());
                    }
                }
                for (std::size_t bucket = 1u; bucket < bucketStacks.size(); ++bucket)
                {
                    const auto shorter = std::as_const(bucketStacks[bucket]).view();
                    const std::optional<an::ValidityMaskView<const std::uint8_t>> shorterHolds =
                        std::as_const(bucketSamples[bucket]).view();
                    const double scale = longest / (*buckets)[bucket].exposureSeconds;
                    std::size_t rowIndex = 0u;
                    for (std::uint32_t plane = 0; plane < access->view().planeCount(); ++plane)
                    {
                        for (std::uint32_t y = 0; y < access->view().height(); ++y, ++rowIndex)
                        {
                            const auto out = access->view().row(plane, y);
                            const auto in = shorter->row(plane, y);
                            if (!out.has_value() || !in.has_value())
                            {
                                continue;
                            }
                            std::vector<float>& reference = references[rowIndex];
                            for (std::size_t x = 0; x < out->size(); ++x)
                            {
                                const auto column = static_cast<std::uint32_t>(x);
                                if (!holds(shorterHolds, plane, column, y))
                                {
                                    continue;
                                }
                                const bool mergedHeld = holds(mergedHolds, plane, column, y);
                                const double alpha =
                                    mergedHeld ? ImageIntegration::exposureMergeBlendWeight(reference[x]) : 1.0;
                                const double value = static_cast<double>((*in)[x]);
                                (*out)[x] = static_cast<float>(static_cast<double>((*out)[x]) * (1.0 - alpha) +
                                                               value * scale * alpha);
                                reference[x] = static_cast<float>(value);
                                if (!mergedHeld)
                                {
                                    (void)mergedHolds->setValid(plane, column, y, true);
                                }
                            }
                        }
                    }
                }
                access.reset();
                exposureMerged = std::move(merged);
                reporter.logger().operationDetail(QStringLiteral("Exposure merge"),
                                                  QStringLiteral("%1 exposure(s) merged").arg(buckets->size()));
            }
            else
            {
                stage_utils::WorkflowStackFrameSource source(
                    [&reader](const std::uint32_t index) { return reader.read(index); },
                    static_cast<std::uint32_t>(allFrames.size()), plan.frames.validity, nameOf, scope.engine().limits(),
                    stage_utils::stackFrameBandPlan(reader, allFrames));
                const an::Result<an::ImageDescriptor> described = source.describe();
                if (!described.hasValue())
                {
                    return engineFailure(described.status(), IntegrationErrorCode::StackingFailed,
                                         QStringLiteral("Stacking failed"), outputPath);
                }
                sayFrameReads(source);
                if (source.firstMetadata() != nullptr)
                {
                    referenceMetadata = *source.firstMetadata();
                }
                stacked =
                    runStack(source, *described, allFrames, false, allFrames, plan.plane, std::nullopt, std::nullopt);
            }

            if (!exposureMerged.has_value() && !stacked.hasValue())
            {
                return engineFailure(stacked.status(),
                                     plan.route == GroupRoute::Drizzle ? IntegrationErrorCode::DrizzleFailed
                                                                       : IntegrationErrorCode::StackingFailed,
                                     plan.route == GroupRoute::Drizzle ? QStringLiteral("Drizzle stacking failed")
                                                                       : QStringLiteral("Stacking failed"),
                                     outputPath);
            }

            // The rejection maps' clusters and the spatial restack without the frames that stood out.
            RejectionScans scans;
            if (stacked.hasValue() && stacked->result.has_value() && sanitized.spatialRejectionClustering)
            {
                an::Result<RejectionScans> scanned = scanRejectionMaps(scope.execution(), *stacked->result);
                if (!scanned.hasValue())
                {
                    return engineFailure(scanned.status(), IntegrationErrorCode::StackingFailed,
                                         QStringLiteral("The rejection maps could not be analyzed"), outputPath);
                }
                scans = std::move(*scanned);
            }
            bool restackApplied = false;
            std::uint64_t initialClusters = scans.clusterCount;
            std::vector<std::uint64_t> excludedFrames;
            std::vector<double> excludedRates;
            double flaggedThreshold = 0.0;
            if (stacked.hasValue() && stacked->result.has_value() && sanitized.restackExcludingFlaggedFrames &&
                plan.route != GroupRoute::Drizzle && !composite.has_value() && !scans.clusters.empty() &&
                !stacked->flagged.empty())
            {
                flaggedThreshold = stacked->result->report.frameOutliers.threshold;
                std::vector<std::uint32_t> kept;
                for (std::uint32_t index = 0; index < allFrames.size(); ++index)
                {
                    if (std::find(stacked->flagged.begin(), stacked->flagged.end(), index) == stacked->flagged.end())
                    {
                        kept.push_back(index);
                    }
                }
                for (const std::uint32_t flagged : stacked->flagged)
                {
                    // Named by its place among the group's lights, where the report looks it up.
                    const FrameId& light = stackedLights.at(flagged);
                    const auto listed = std::find_if(
                        group.lightFiles.begin(), group.lightFiles.end(),
                        [&light](const FrameId& candidate) { return candidate.identityKey() == light.identityKey(); });
                    excludedFrames.push_back(listed != group.lightFiles.end()
                                                 ? static_cast<std::uint64_t>(listed - group.lightFiles.begin())
                                                 : flagged);
                    excludedRates.push_back(flagged < stacked->rates.size() ? stacked->rates[flagged].rate : 0.0);
                }
                // Only a rejection flags frames, and the re-stack keeps it, so it needs as many frames as the
                // rejection does.
                if (kept.size() >= an::kMinimumRejectionFrames)
                {
                    reporter.logger().operationDetail(QStringLiteral("Spatial rejection"),
                                                      QStringLiteral("Flagged %1 frame(s); re-stacking without them.")
                                                          .arg(static_cast<int>(stacked->flagged.size())));
                    progress.setRange(progressFloor, kRuntimePhaseEnd);
                    an::Result<StackRun> restacked = stackFrames(kept);
                    if (restacked.hasValue())
                    {
                        an::Result<RejectionScans> rescanned = scanRejectionMaps(scope.execution(), *restacked->result);
                        if (rescanned.hasValue())
                        {
                            scans = std::move(*rescanned);
                        }
                        stacked = std::move(restacked);
                        // The lights the master combines, and the group's reference among them - or the re-stack's
                        // first light when it left the reference out, as the stack's own measurement takes it.
                        std::vector<FrameId> keptLights;
                        std::vector<std::uint32_t> keptFrames;
                        std::vector<std::optional<an::CodecImageInfo>> keptHeaders;
                        for (const std::uint32_t index : kept)
                        {
                            keptLights.push_back(stackedLights[index]);
                            keptFrames.push_back(combinedFrames[index]);
                            keptHeaders.push_back(lightHeaders[index]);
                        }
                        const auto keptReference =
                            std::find(kept.begin(), kept.end(), static_cast<std::uint32_t>(referenceIndex));
                        referenceIndex = keptReference != kept.end()
                                             ? static_cast<std::size_t>(std::distance(kept.begin(), keptReference))
                                             : 0u;
                        stackedLights = std::move(keptLights);
                        combinedFrames = std::move(keptFrames);
                        lightHeaders = std::move(keptHeaders);
                        restackApplied = true;
                    }
                    else if (isCancellation(restacked.status()))
                    {
                        return StageResult::cancelled();
                    }
                    else
                    {
                        warn(QStringLiteral("Spatial rejection re-stack failed; keeping the original result (%1).")
                                 .arg(QString::fromUtf8(restacked.status().message())));
                    }
                }
                else
                {
                    warn(QStringLiteral("Group '%1' would keep %2 frame(s) without the %3 its spatial rejection "
                                        "flagged: %4 needs at least %5, so the master is not stacked again without "
                                        "them.")
                             .arg(groupLabel)
                             .arg(static_cast<int>(kept.size()))
                             .arg(static_cast<int>(stacked->flagged.size()))
                             .arg(sanitized.rejectionMethod)
                             .arg(an::kMinimumRejectionFrames));
                }
            }

            if (stacked.hasValue() && !stacked->normalization.isEmpty())
            {
                reporter.logger().operationDetail(QStringLiteral("Normalization"), stacked->normalization);
            }

            // The master, its weighting's disposition said.
            an::Result<an::Image<float>> master =
                refusal(an::ErrorCode::Internal, QStringLiteral("no master was made"));
            std::vector<StackMapImage> maps;
            if (composite.has_value())
            {
                master = an::Result<an::Image<float>>(std::move(composite->composite));
            }
            else if (exposureMerged.has_value())
            {
                master = an::Result<an::Image<float>>(std::move(*exposureMerged));
            }
            else
            {
                const an::StackExecutionReport& engineReport = stacked->result->report;
                if (shape.weighting != an::StackWeighting::Uniform &&
                    engineReport.weighting.disposition != an::StackFrameWeightingDisposition::Computed)
                {
                    warn(QStringLiteral("%1 weighting gave every frame the same weight (%2).")
                             .arg(sanitized.weighting.trimmed(),
                                  QString::fromLatin1(
                                      an::stackFrameWeightingDispositionName(engineReport.weighting.disposition))));
                }
                quality = qualityRecordOf(engineReport, static_cast<std::uint32_t>(stackedLights.size()),
                                          stacked->rates, scans);
                quality.normalization = stacked->normalization;
                if (stacked->frameWeights.size() == stackedLights.size())
                {
                    quality.lightWeights.reserve(stackedLights.size());
                    for (std::size_t index = 0u; index < stackedLights.size(); ++index)
                    {
                        quality.lightWeights.push_back(StackLightWeight{.light = stackedLights[index].canonicalPath(),
                                                                        .weight = stacked->frameWeights[index]});
                    }
                }
                countersTouched = true;
                engineCounters.inputDecodeOperations += engineReport.frameReads;
                engineCounters.inputPassesStarted += engineReport.passesRun;
                engineCounters.tilePassesStarted += engineReport.tilesCombined;
                engineCounters.logicalDecodedPixelBytes += engineReport.inputBytesRead;
                an::Result<std::vector<StackMapImage>> madeMaps =
                    stackMapImages(scope.engine(), *stacked->result, static_cast<std::uint32_t>(stackedLights.size()),
                                   an::ImageMetadata{});
                if (!madeMaps.hasValue())
                {
                    return engineFailure(madeMaps.status(), IntegrationErrorCode::StackingFailed,
                                         QStringLiteral("The stack's maps could not be made"), outputPath);
                }
                maps = std::move(*madeMaps);
                master = stackedMaster(*stacked->result, *stacked->model,
                                       referenceMetadata.has_value() ? *referenceMetadata : an::ImageMetadata{});
            }
            if (!master.hasValue())
            {
                return engineFailure(master.status(), IntegrationErrorCode::StackingFailed,
                                     QStringLiteral("The stacked master could not be made"), outputPath);
            }
            quality.spatialRestackRequested =
                sanitized.restackExcludingFlaggedFrames && plan.route != GroupRoute::Drizzle;
            quality.spatialRestackApplied = restackApplied;
            quality.spatialRestackInitialClusterCount = initialClusters;
            quality.spatialRestackFlaggedFrameThreshold = flaggedThreshold;
            quality.spatialRestackExcludedFrameIndices = excludedFrames;
            quality.spatialRestackExcludedFrameRates = excludedRates;

            if (masterGuard > 0u && masterWidth > 0u && masterHeight > 0u)
            {
                master = cropped(scope, *master, masterGuard, masterGuard, masterWidth, masterHeight);
                if (!master.hasValue())
                {
                    return engineFailure(master.status(), IntegrationErrorCode::DrizzleFailed,
                                         QStringLiteral("The drizzled master's edge could not be cropped"), outputPath);
                }
                for (StackMapImage& map : maps)
                {
                    an::Result<an::Image<float>> croppedMap =
                        cropped(scope, map.image, masterGuard, masterGuard, masterWidth, masterHeight);
                    if (!croppedMap.hasValue())
                    {
                        return engineFailure(croppedMap.status(), IntegrationErrorCode::DrizzleFailed,
                                             QStringLiteral("A drizzled map's edge could not be cropped"), outputPath);
                    }
                    map.image = std::move(*croppedMap);
                }
            }
            const std::optional<an::ImageDescriptor> masterDescriptor = master->descriptor();
            if (!masterDescriptor.has_value())
            {
                return StageResult::failure(
                    IntegrationError::stackingFailed(QStringLiteral("The master holds nothing")));
            }
            report(kRuntimePhaseEnd, QStringLiteral("Finalizing stacked output"));

            // The reference light's cards, and every light's for a mosaic's pointing, as the headers read before the
            // stack stated them.
            const std::size_t referenceHeaderIndex = (std::min)(referenceIndex, stackedLights.size() - 1u);
            const std::optional<an::CodecImageInfo> referenceProbe =
                referenceHeaderIndex < lightHeaders.size() ? lightHeaders[referenceHeaderIndex] : std::nullopt;
            const an::CodecMetadata referenceCards =
                referenceProbe.has_value() ? referenceProbe->metadata.codec() : an::CodecMetadata{};
            std::vector<std::vector<an::CodecCard>> lightCards;
            if (isMosaic)
            {
                for (const std::optional<an::CodecImageInfo>& header : lightHeaders)
                {
                    if (header.has_value())
                    {
                        const auto cards = header->metadata.codec().cards();
                        lightCards.emplace_back(cards.begin(), cards.end());
                    }
                }
            }

            // The master's solution, decided - and solved, when the group asks - before it is written.
            IntegrationGroupResult groupResult;
            groupResult.groupKey = group.groupKey.toString();
            groupResult.outputPath = outputPath;
            groupResult.outputFilename = group.outputFilename;
            groupResult.registrationGroupKey = group.registrationGroupKey.toString();
            groupResult.registrationInputPolicy = plan.policy;
            groupResult.lightFrameCount = static_cast<int>(stackedLights.size());
            groupTiming.beginFinalization();

            const bool embedsWcs = stage_utils::supportsEmbeddedWcsOutput(outputPath);
            MasterWcsRequest wcsRequest;
            wcsRequest.registered = plan.route != GroupRoute::Frames && !registrationKey.trimmed().isEmpty() &&
                                    plan.route != GroupRoute::Comet && plan.route != GroupRoute::StarsPlusComet;
            wcsRequest.outputPlaneWcsJson = plan.outputPlaneWcsJson;
            wcsRequest.registrationReference = plan.registrationReference;
            wcsRequest.alignmentMode = stage_utils::alignmentModeKey(plan.planeMode.value_or(canvasMode));
            if (plan.registrationCanvas.has_value())
            {
                wcsRequest.planeWidth = plan.registrationCanvas->width;
                wcsRequest.planeHeight = plan.registrationCanvas->height;
            }
            else
            {
                wcsRequest.planeWidth = masterDescriptor->extent().width();
                wcsRequest.planeHeight = masterDescriptor->extent().height();
            }
            wcsRequest.expectedSipCorrection =
                plan.route == GroupRoute::TransformOnRead ? plan.expectedSipCorrection : std::nullopt;
            if (referenceProbe.has_value())
            {
                const auto cards = referenceCards.cards();
                wcsRequest.referenceCards.assign(cards.begin(), cards.end());
                const std::optional<an::ImageDescriptor> referenceDescriptor =
                    referenceProbe->descriptor.toImageDescriptor();
                if (referenceDescriptor.has_value())
                {
                    wcsRequest.referenceWidth = referenceDescriptor->extent().width();
                    wcsRequest.referenceHeight = referenceDescriptor->extent().height();
                }
            }
            if (plan.route == GroupRoute::Drizzle)
            {
                wcsRequest.drizzle = drizzleMaster;
                if (!drizzleMaster.has_value())
                {
                    wcsRequest.outputPlaneWcsJson.reset();
                }
            }
            wcsRequest.masterWidth = masterDescriptor->extent().width();
            wcsRequest.masterHeight = masterDescriptor->extent().height();
            if (plan.route == GroupRoute::Frames)
            {
                // A stack no registration placed is on its reference light's own plane only when every light is.
                wcsRequest.registered = false;
            }
            MasterWcsCandidate wcs = buildMasterWcsCandidate(wcsRequest);
            const bool cometDrizzle =
                plan.route == GroupRoute::Drizzle && plan.drizzle.has_value() && !plan.drizzle->cometRunId.isEmpty();
            if (plan.route == GroupRoute::Comet || plan.route == GroupRoute::StarsPlusComet || cometDrizzle)
            {
                wcs.solution.reset();
                wcs.diagnostic = QStringLiteral("A comet-aligned master carries no star solution.");
            }

            MasterCardsRequest cardsRequest;
            cardsRequest.referenceCards = &referenceCards;
            cardsRequest.integrationSeconds = totalIntegrationSeconds(group, stackedLights);
            cardsRequest.combinedFrames =
                std::accumulate(combinedFrames.begin(), combinedFrames.end(), std::uint64_t{0});
            if (plan.route == GroupRoute::Drizzle)
            {
                cardsRequest.drizzle = group.drizzle;
                cardsRequest.drizzledLights = stackedLights.size();
            }
            const auto masterWithCards =
                [&](const std::optional<an::WcsSolution>& solution) -> an::Result<an::Image<float>> {
                cardsRequest.solution = embedsWcs ? solution : std::nullopt;
                cardsRequest.pointing.clear();
                if (isMosaic && embedsWcs)
                {
                    cardsRequest.pointing =
                        mosaicPointingCards(referenceCards.cards(), lightCards, cardsRequest.solution);
                }
                const an::Result<an::CodecMetadata> cards = masterCodecMetadata(cardsRequest);
                if (!cards.hasValue())
                {
                    return cards.status();
                }
                an::AstrometricSolution astrometry;
                if (cardsRequest.solution.has_value())
                {
                    an::Result<an::AstrometricSolution> summary = cardsRequest.solution->toAstrometricSolution();
                    if (summary.hasValue())
                    {
                        astrometry = *summary;
                    }
                }
                const an::ImageMetadata& current = master->metadata();
                const an::ImageMetadata metadata = an::ImageMetadata::create(
                    current.acquisition(), current.semantics(), astrometry, current.provenance(), *cards, std::nullopt);
                const auto view = master->view();
                if (!view.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the master holds nothing"));
                }
                an::Result<an::Image<float>> copy =
                    an::Image<float>::create(scope.engine(), *masterDescriptor, an::StorageArrangement::Planar,
                                             an::StorageInitialization::Uninitialized, metadata);
                if (!copy.hasValue())
                {
                    return copy.status();
                }
                std::optional<an::ImageWriteAccess<float>> access = copy->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the master holds nothing"));
                }
                if (an::Status copied =
                        an::repackImageSamples(scope.execution(), an::SampleType::Float32, view->data(), view->layout(),
                                               access->view().data(), access->view().layout());
                    copied.failed())
                {
                    return copied;
                }
                access.reset();
                return copy;
            };

            report(kWcsPhaseStart, QStringLiteral("Finalizing master WCS"));
            QString wcsPhaseStatus = QStringLiteral("skipped");
            std::optional<an::WcsSolution> solution = wcs.solution;
            // Where the master's solution came from, as the group's result keeps it for the report.
            const auto recordSolution = [&groupResult](const MasterSolutionOutcome outcome, const QString& detail) {
                groupResult.masterSolution = outcome;
                groupResult.masterSolutionDetail = detail.trimmed();
            };
            if (embedsWcs && solution.has_value())
            {
                recordSolution(MasterSolutionOutcome::Propagated,
                               QStringLiteral("Carried from the %1.").arg(wcs.sourceDescription.trimmed()));
            }
            if (embedsWcs && !solution.has_value())
            {
                wcsPhaseStatus = QStringLiteral("completed");
                const QString currentIssue = !wcs.diagnostic.trimmed().isEmpty()
                                                 ? wcs.diagnostic.trimmed()
                                                 : QStringLiteral("the propagated WCS was missing or invalid");
                if (sanitized.solveFinalMasterIfNeeded)
                {
                    reporter.logger().operationDetail(
                        QStringLiteral("Final master WCS"),
                        QStringLiteral("Resolving fallback WCS for %1.").arg(group.outputFilename));
                    an::Result<an::Image<float>> unsolved = masterWithCards(std::nullopt);
                    if (!unsolved.hasValue())
                    {
                        return engineFailure(unsolved.status(), IntegrationErrorCode::StackingFailed,
                                             QStringLiteral("The master's cards could not be written"), outputPath);
                    }
                    if (!plateSolveService)
                    {
                        plateSolveService = std::make_shared<an::ui::PlateSolveService>();
                    }
                    scope.end();
                    const FinalMasterWcsOutcome solved =
                        solveFinalMasterWcs(*plateSolveService, *services, manifest, group, *unsolved,
                                            outputRoot.path(), currentIssue, token, &finalMasterWcsCache);
                    if (solved.cancelled)
                    {
                        groupTiming.capturePhase(StackFinalizationPhase::FinalMasterWcs, QStringLiteral("cancelled"));
                        return StageResult::cancelled();
                    }
                    if (!solved.detail.trimmed().isEmpty())
                    {
                        reporter.logger().operationDetail(QStringLiteral("Final master WCS"), solved.detail);
                    }
                    if (!solved.warning.trimmed().isEmpty())
                    {
                        warn(solved.warning);
                    }
                    if (solved.solution.has_value())
                    {
                        solution = solved.solution;
                        recordSolution(MasterSolutionOutcome::Solved,
                                       !solved.detail.trimmed().isEmpty()
                                           ? solved.detail
                                           : QStringLiteral("Solved by the plate solver, the propagated WCS being "
                                                            "missing or invalid (%1).")
                                                 .arg(currentIssue));
                    }
                    else
                    {
                        wcsPhaseStatus = QStringLiteral("failed");
                        QString unsolvedBecause = solved.warning.trimmed();
                        if (unsolvedBecause.isEmpty())
                        {
                            unsolvedBecause = !solved.detail.trimmed().isEmpty() ? solved.detail : currentIssue;
                        }
                        recordSolution(MasterSolutionOutcome::None, unsolvedBecause);
                    }
                    an::Result<an::consumer::OperationScope> readmitted =
                        services->admitStageOperation(&progress, &diagnostics);
                    if (!readmitted.hasValue())
                    {
                        return engineFailure(readmitted.status(), IntegrationErrorCode::StackingFailed,
                                             QStringLiteral("Stacking could not finish"), outputPath);
                    }
                    *admitted = std::move(*readmitted);
                }
                else if (wcs.missingOutputPlaneRecord)
                {
                    const QString saved =
                        QStringLiteral("Saved %1 without WCS because no registration output-plane WCS was available "
                                       "and fallback plate solving is disabled.")
                            .arg(group.outputFilename);
                    reporter.logger().operationDetail(QStringLiteral("Final master WCS"), saved);
                    recordSolution(MasterSolutionOutcome::None, saved);
                }
                else if (!wcs.diagnostic.isEmpty())
                {
                    const QString saved =
                        QStringLiteral("Final master '%1' was saved without a valid WCS (%2). Fallback plate solving "
                                       "is disabled.")
                            .arg(group.outputFilename, currentIssue);
                    warn(saved);
                    recordSolution(MasterSolutionOutcome::None, saved);
                }
                else
                {
                    recordSolution(MasterSolutionOutcome::None,
                                   QStringLiteral("Saved %1 without WCS because no solution was found to carry forward "
                                                  "and fallback plate solving is disabled.")
                                       .arg(group.outputFilename));
                }
            }
            else if (!embedsWcs)
            {
                const QString skipped =
                    QStringLiteral("Skipping final-master WCS export for '%1' because %2 outputs do not support full "
                                   "astrometric headers.")
                        .arg(group.outputFilename, QFileInfo(outputPath).suffix().toUpper());
                if (wcs.solution.has_value() || sanitized.solveFinalMasterIfNeeded)
                {
                    warn(skipped);
                }
                recordSolution(MasterSolutionOutcome::None, skipped);
            }
            groupTiming.capturePhase(StackFinalizationPhase::FinalMasterWcs, wcsPhaseStatus);

            an::Result<an::Image<float>> finalMaster = masterWithCards(solution);
            if (!finalMaster.hasValue())
            {
                return engineFailure(finalMaster.status(), IntegrationErrorCode::StackingFailed,
                                     QStringLiteral("The master's cards could not be written"), outputPath);
            }

            const auto finalizationFailure = [&](StageResult failure) {
                groupResult.success = false;
                groupResult.durationMs = groupTiming.capturedGroupDurationMs();
                groupResult.finalizationTiming = groupTiming.finalizationTiming();
                groupResult.outputPath.clear();
                groupResult.committedArtifact.reset();
                clearPreparedStackPreviewAssets(groupResult);
                groupResult.rejectionMaps.clear();
                groupResult.qualityMaps.clear();
                groupResult.stagedFiles.clear();
                failure.groupResults.push_back(std::move(groupResult));
                return failure;
            };

            report(kSavePhaseStart, QStringLiteral("Finalizing stacked output - Saving final master"));
            if (token.isCancellationRequested())
            {
                groupTiming.capturePhase(StackFinalizationPhase::FinalMasterSave, QStringLiteral("cancelled"));
                groupResult.durationMs = groupTiming.capturedGroupDurationMs();
                groupResult.finalizationTiming = groupTiming.finalizationTiming();
                groupResult.outputPath.clear();
                StageResult cancelled = StageResult::cancelled();
                cancelled.groupResults.push_back(std::move(groupResult));
                return cancelled;
            }
            an::Result<LocalOutputTransaction> transaction =
                stageOutputsForOverwrite(outputPath, executionCounterAccumulator());
            if (!transaction.hasValue())
            {
                groupTiming.capturePhase(StackFinalizationPhase::FinalMasterSave, QStringLiteral("failed"));
                return finalizationFailure(StageResult::failure(IntegrationError(
                    IntegrationErrorCode::FileNotWritable, QStringLiteral("Failed to save stacked image"), outputPath,
                    QString::fromUtf8(transaction.status().message()))));
            }
            if (composite.has_value())
            {
                for (const QString& kind : {QStringLiteral("comet_layer"), QStringLiteral("comet_mask")})
                {
                    if (!stageLocalFileForOverwrite(compositeLayerPath(outputPath, kind), *transaction))
                    {
                        restoreLocalStagedFiles(*transaction);
                        groupTiming.capturePhase(StackFinalizationPhase::FinalMasterSave, QStringLiteral("failed"));
                        return finalizationFailure(StageResult::failure(IntegrationError(
                            IntegrationErrorCode::FileNotWritable,
                            QStringLiteral("Failed to stage existing Stars + comet artifacts"), outputPath)));
                    }
                }
            }
            for (const auto& staged : transaction->staged)
            {
                groupResult.stagedFiles.push_back({staged.originalPath, staged.stagedPath});
            }
            const auto restoreAndFail = [&](const StackFinalizationPhase phase, const an::Status& status,
                                            const QString& message) {
                groupTiming.capturePhase(phase, isCancellation(status) ? QStringLiteral("cancelled")
                                                                       : QStringLiteral("failed"));
                restoreLocalStagedFiles(*transaction);
                if (isCancellation(status))
                {
                    return finalizationFailure(StageResult::cancelled());
                }
                return finalizationFailure(StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                    status, IntegrationErrorCode::FileNotWritable, message, outputPath)));
            };

            const an::Result<an::ArtifactIdentity> identity =
                ImageIntegration::workflowArtifactIdentity(artifactHash, manifest.executionId);
            if (!identity.hasValue())
            {
                return restoreAndFail(StackFinalizationPhase::FinalMasterSave, identity.status(),
                                      QStringLiteral("The master's artifact identity could not be stated"));
            }
            progress.setRange(kSavePhaseStart, kMapsPhaseStart);
            if (an::Status published =
                    ImageIntegration::publishWorkflowImage(scope, *finalMaster, outputPath, *identity);
                published.failed())
            {
                return restoreAndFail(StackFinalizationPhase::FinalMasterSave, published,
                                      QStringLiteral("Failed to save stacked image"));
            }
            ++engineCounters.scientificOutputEncodeCompletions;
            groupTiming.capturePhase(StackFinalizationPhase::FinalMasterSave, QStringLiteral("completed"));

            // The maps, each stamped with the master's identity and recorded under its fingerprint.
            QStringList mapPaths;
            if (!maps.empty() || composite.has_value())
            {
                report(kMapsPhaseStart, QStringLiteral("Saving requested maps"));
                const QFileInfo outInfo(outputPath);
                for (StackMapImage& map : maps)
                {
                    const bool rejection = map.kind == ImageIntegration::StackingAuxiliaryOutputKind::RejectionMerged ||
                                           map.kind == ImageIntegration::StackingAuxiliaryOutputKind::RejectionLow ||
                                           map.kind == ImageIntegration::StackingAuxiliaryOutputKind::RejectionHigh;
                    if (rejection && !sanitized.createRejectionMaps)
                    {
                        continue;
                    }
                    const auto spec = ImageIntegration::stackingAuxiliaryOutputForOutput(outInfo, map.kind);
                    const QString path = outInfo.absoluteDir().filePath(spec.filename);
                    if (an::Status published =
                            ImageIntegration::publishWorkflowImage(scope, map.image, path, *identity);
                        published.failed())
                    {
                        return restoreAndFail(StackFinalizationPhase::RequestedMaps, published,
                                              QStringLiteral("Failed to save a map beside the stacked image"));
                    }
                    mapPaths << path;
                    if (rejection)
                    {
                        groupResult.rejectionMaps.push_back(RejectionMapResult{spec.title, path, {}});
                    }
                    else
                    {
                        groupResult.qualityMaps.push_back(QualityMapResult{spec.title, path, {}});
                    }
                }
                if (composite.has_value())
                {
                    for (const auto& [kind, image] : {std::pair<QString, const an::Image<float>*>{
                                                          QStringLiteral("comet_layer"), &composite->cometLayer},
                                                      std::pair<QString, const an::Image<float>*>{
                                                          QStringLiteral("comet_mask"), &composite->cometMask}})
                    {
                        const QString path = compositeLayerPath(outputPath, kind);
                        if (an::Status published =
                                ImageIntegration::publishWorkflowImage(scope, *image, path, *identity);
                            published.failed())
                        {
                            return restoreAndFail(StackFinalizationPhase::RequestedMaps, published,
                                                  QStringLiteral("Failed to save Stars + comet composite artifacts"));
                        }
                        mapPaths << path;
                    }
                }
                groupTiming.capturePhase(StackFinalizationPhase::RequestedMaps, QStringLiteral("completed"));
            }

            report(kPreviewPhaseStart, QStringLiteral("Generating stacked preview"));
            if (token.isCancellationRequested())
            {
                return restoreAndFail(StackFinalizationPhase::PreviewGeneration,
                                      an::Status::failure(an::ErrorCode::Cancelled, "cancelled"), QString());
            }
            prepareResidentStackPreviewAssets(m_previewGenerator, scope, *finalMaster, outputPath, groupResult);
            groupTiming.capturePhase(StackFinalizationPhase::PreviewGeneration, QStringLiteral("completed"));

            // The record follows the committed writes: the master's first, then each map's under the master's
            // fingerprint, which is what makes a map current (RejectionMapUtils.h). Reading a file's stamp is an
            // operation of its own, which the session admits only once this one has let go of its admission.
            scope.end();
            report(kArtifactPhaseStart, QStringLiteral("Recording stacked artifact"));
            const auto cancelRequested = [&token]() { return token.isCancellationRequested(); };
            const auto committed =
                FinalCommittedArtifact::fromFile(outputPath, executionCounterAccumulator(), cancelRequested);
            if (!committed.has_value() ||
                !tracker.recordCurrentRunCommittedArtifact(*committed, artifactHash, cancelRequested))
            {
                return restoreAndFail(StackFinalizationPhase::DigestArtifactRecording,
                                      token.isCancellationRequested()
                                          ? an::Status::failure(an::ErrorCode::Cancelled, "cancelled")
                                          : an::Status::failure(an::ErrorCode::ExternalFailure,
                                                                "the stacked artifact could not be recorded"),
                                      QStringLiteral("Failed to record stacked artifact"));
            }
            for (const QString& path : mapPaths)
            {
                if (!tracker.recordArtifact(path, artifactHash, *identity, cancelRequested))
                {
                    LOG_WARNING("LightsStackingStage: failed to record the map '{}'", path.toStdString());
                }
            }
            if (artifactPublicationLedger())
            {
                (void)artifactPublicationLedger()->recordPublishedArtifact(*committed);
            }
            groupTiming.capturePhase(StackFinalizationPhase::DigestArtifactRecording, QStringLiteral("completed"));

            if (composite.has_value())
            {
                const auto recordOf = [&](const QString& kind, const QString& path) {
                    an::ui::StackingCometArtifactRecord record;
                    record.cometRunId = plan.composite->cometProducts.cometRunId;
                    record.integrationGroupKey = group.groupKey.toString();
                    record.stackingHash = artifactHash;
                    record.starBaseOutputDescriptorId = composite->starBaseDescriptorId;
                    record.cometOutputPlaneId = plan.composite->cometProducts.outputPlane->cometOutputPlaneId;
                    record.artifactKind = kind;
                    record.outputPath = path;
                    record.artifactHash = artifactHash;
                    record.maskSettingsFingerprint = composite->maskSettingsFingerprint;
                    record.succeeded = true;
                    record.updatedAt = QDateTime::currentSecsSinceEpoch();
                    return record;
                };
                std::vector<an::ui::StackingCometArtifactRecord> records{
                    recordOf(QStringLiteral("FinalComposite"), outputPath),
                    recordOf(QStringLiteral("CometLayer"),
                             compositeLayerPath(outputPath, QStringLiteral("comet_layer"))),
                    recordOf(QStringLiteral("CometMask"),
                             compositeLayerPath(outputPath, QStringLiteral("comet_mask")))};
                an::ui::RegistrationMetricsWriter writer(metricsPath,
                                                         QStringLiteral("StarsPlusCometCompositeArtifactWriter"),
                                                         executionCounterAccumulator());
                QString writeError;
                if (!writer.initialize(&writeError) || !writer.replaceStackingCometArtifacts(records, &writeError))
                {
                    warn(QStringLiteral("Failed to record composite artifacts: %1").arg(writeError));
                }
            }

            report(kPublicationPhaseStart, QStringLiteral("Publishing stacking result"));
            groupResult.outputBytes = committed->sizeBytes();
            groupResult.committedArtifact = *committed;
            groupResult.success = true;
            groupResult.qualityReport = quality;
            reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTiming.elapsedNowMs());
            groupTiming.capturePhase(StackFinalizationPhase::CompletionPublication, QStringLiteral("completed"));
            groupResult.durationMs = groupTiming.capturedGroupDurationMs();
            groupResult.finalizationTiming = groupTiming.finalizationTiming();

            sayEngineWarnings();
            StageResult result = StageResult::success();
            result.warnings = groupWarnings;
            result.groupResults.push_back(std::move(groupResult));
            report(1.0f, plan.route == GroupRoute::Drizzle ? QStringLiteral("Drizzle stacking")
                                                           : QStringLiteral("Lights stacking"));
            return result;
        };

        StageResult groupResult = processGroup();
        for (auto& result : groupResult.groupResults)
        {
            completedGroupResults.push_back(std::move(result));
        }
        appendUniqueWarnings(stageWarnings, groupResult.warnings);
        if (groupResult.isSuccess())
        {
            anyGroupSucceeded = true;
        }
        if (groupResult.isCancelled())
        {
            StageResult cancelled = StageResult::cancelled();
            cancelled.groupResults = std::move(completedGroupResults);
            return cancelled;
        }
        if (groupResult.status == StageResult::Status::Failure)
        {
            errorReport.addError(groupResult.error);
            reporter.logger().warning(QStringLiteral("Group '%1' failed: %2 - continuing with remaining groups")
                                          .arg(groupLabel, groupResult.error.toUserString()));
            reporter.logger().groupCompleted(groupIndex, totalGroups, QStringLiteral("%1 (failed)").arg(groupLabel),
                                             groupTiming.elapsedNowMs());
        }
        else if (groupResult.isPartialSuccess())
        {
            errorReport.merge(groupResult.errorReport);
        }
    }

    if (combineEligibleRgbMasters(manifest, *services, token, reporter, completedGroupResults, processedGroupKeys,
                                  errorReport, stageWarnings, anyGroupSucceeded, m_monotonicClock,
                                  executionCounterAccumulator(), tracker, m_previewGenerator))
    {
        StageResult cancelled = StageResult::cancelled();
        cancelled.groupResults = std::move(completedGroupResults);
        return cancelled;
    }

    // What the calibrations of the lights read in memory fell back to, once per calibration.
    stage_utils::sayStageWarnings(reporter, masters.fallbackWarnings(), stageWarnings);
    reporter.logger().stageCompleted(name(), totalGroups, static_cast<int>(stageTimer.elapsed()));
    StageResult outcome = StageResult::success();
    if (!errorReport.errors.empty())
    {
        LOG_WARNING("LightsStackingStage: {} group(s) had stacking errors", errorReport.errors.size());
        if (!anyGroupSucceeded)
        {
            outcome = StageResult::failure(errorReport.errors.front());
            outcome.warnings = stageWarnings;
        }
        else
        {
            outcome = StageResult::partialSuccess(errorReport);
            appendUniqueWarnings(outcome.warnings, stageWarnings);
        }
    }
    else
    {
        outcome.warnings = stageWarnings;
    }
    outcome.groupResults = std::move(completedGroupResults);
    if (executionCounterAccumulator() != nullptr && countersTouched)
    {
        outcome.engineExecutionCounters = engineCounters;
    }
    return outcome;
}

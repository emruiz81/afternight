// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow's normalization stage: each group's lights measured where its stack reads
/// them, the global matches, the overlap solution or the local surfaces fitted by the engine's normalization steps in
/// one admitted operation, and the coefficients published as the group's document and sidecar.

#include "stdafx.hpp"
#include "LightsNormalizationStage.h"

#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/CometStackingProductResolver.h"
#include "processes/ImageIntegration/core/NormalizationAnalysis.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsReader.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationOutputPlane.h"
#include "processes/ImageIntegration/workflow/stages/StackDrizzlePixmapIndex.h"
#include "processes/ImageIntegration/workflow/stages/StackFrameSources.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/core/RegistrationRun.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Normalization/DrizzleLocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using ImageIntegration::NormalizationCoefficientMode;
using ImageIntegration::NormalizationCoefficientSet;
using ImageIntegration::NormalizationMeasurementPlane;

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

[[nodiscard]] bool cancelledStatus(const an::Status& status)
{
    return status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded;
}

[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, message.toStdString());
}

[[nodiscard]] const WorkflowManifest::RegistrationGroup* findRegistrationGroup(const WorkflowManifest& manifest,
                                                                               const GroupKey& key)
{
    const auto found =
        std::find_if(manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
                     [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey == key; });
    return found == manifest.registrationGroups.end() ? nullptr : &*found;
}

/// One light as the stage reads it.
struct MeasuredFrame
{
    /// The light the group lists.
    FrameId logical;

    /// The file its samples are read from.
    QString path;

    /// Whether it is warped onto the plane in memory after it is read, and through what.
    bool warp = false;
    an::RegistrationTransform transform;
    std::optional<an::SipDistortion> distortion;

    /// Its calibration, when the run calibrates in memory and the file read is the light itself.
    const WorkflowManifest::LightsCalibrationParams* calibration = nullptr;

    /// Whether the file read is one the workflow wrote during this run, whose revision the preparation did not see.
    bool workflowOwned = false;
};

/// Where and how a group's lights are measured.
struct MeasurementPlan
{
    NormalizationMeasurementPlane plane = NormalizationMeasurementPlane::Frames;
    std::vector<MeasuredFrame> frames;

    /// The plane a warped light lands on, and how it is sampled.
    an::RegistrationCanvas canvas;
    an::KernelInterpolation interpolation = an::KernelInterpolation::Lanczos4;
    an::ui::processes::RegistrationDemosaic demosaic;

    std::optional<ImageIntegration::DrizzleAnalysisPlaneRecord> drizzlePlane;

    /// The geometry a warped plane was planned from; empty when nothing is warped.
    QString geometryFingerprint;

    QString cometProductsFingerprint;
    std::optional<int> registrationSettingsVersion;

    /// Lights that are not measured, and why, for the log.
    QStringList skipped;

    /// The registered lights were read from disk, every one current: the standard-global coefficients' identity is
    /// recorded for the stack's transform-on-read route.
    bool registeredOutputsOnDisk = false;
};

/// The outcome of planning where a group is measured.
struct PlanOutcome
{
    std::optional<MeasurementPlan> plan;
    std::optional<StageResult> result;
};

/// Which samples of a light measured on a plane mean something, as the stack reads them there: every finite one for a
/// light as it was read, which a calibration leaves not a number where its flat is zero; everything but an exact zero
/// on a warped plane, where a zero is where the light does not reach and the reader restates a sample that is not a
/// number as one.
[[nodiscard]] an::ValidityRule validityRuleFor(const NormalizationMeasurementPlane plane) noexcept
{
    return plane == NormalizationMeasurementPlane::Frames ? an::ValidityRule::FiniteOnly
                                                          : an::ValidityRule::ZeroIsNoData;
}

[[nodiscard]] an::Result<an::NormalizationFrameView> frameViewOf(const an::Image<float>& image,
                                                                 const an::ValidityRule rule)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    const auto view = image.view();
    const std::optional<an::ValidityDescriptor> validity = an::ValidityDescriptor::create({.rule = rule});
    if (!descriptor.has_value() || !view.has_value() || !validity.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    an::KernelValidityView kernelValidity;
    kernelValidity.descriptor = *validity;
    return an::NormalizationFrameView::create(view->data(), view->layout().totalBytes(), view->layout(), *descriptor,
                                              kernelValidity);
}

/// Where the samples an exact zero does not mark as uncovered lie: the smallest rectangle holding them.
[[nodiscard]] std::optional<an::StatisticsRegion> coverageOf(const an::Image<float>& image)
{
    const auto view = image.view();
    if (!view.has_value())
    {
        return std::nullopt;
    }
    const std::uint32_t perPixel = (std::max)(1u, view->samplesPerPixel());
    std::uint32_t minX = view->width();
    std::uint32_t minY = view->height();
    std::uint32_t maxX = 0u;
    std::uint32_t maxY = 0u;
    bool any = false;
    for (std::uint32_t y = 0; y < view->height(); ++y)
    {
        const auto row = view->row(0u, y);
        if (!row.has_value())
        {
            continue;
        }
        for (std::size_t index = 0; index < row->size(); ++index)
        {
            if ((*row)[index] != 0.0f)
            {
                const auto x = static_cast<std::uint32_t>(index / perPixel);
                minX = (std::min)(minX, x);
                maxX = (std::max)(maxX, x);
                minY = (std::min)(minY, y);
                maxY = (std::max)(maxY, y);
                any = true;
            }
        }
    }
    if (!any)
    {
        return std::nullopt;
    }
    return an::StatisticsRegion{minX, minY, maxX - minX + 1u, maxY - minY + 1u};
}

/// Reads the plan's lights, each into the operation's engine, warped onto the plane when the plan warps it.
class FrameReader
{
public:
    FrameReader(const ImageIntegration::WorkflowStageServices& services, const MeasurementPlan& plan,
                core::CalibrationMasterSet& masters)
        : m_services(services), m_plan(plan), m_masters(masters)
    {
    }

    [[nodiscard]] an::Result<an::Image<float>> read(const an::consumer::OperationScope& scope, const std::size_t index)
    {
        const MeasuredFrame& frame = m_plan.frames.at(index);
        std::shared_ptr<const core::LightCalibration> calibration;
        if (frame.calibration != nullptr)
        {
            an::Result<std::shared_ptr<const core::LightCalibration>> prepared =
                m_masters.prepare(scope, *frame.calibration);
            if (!prepared.hasValue())
            {
                return prepared.status();
            }
            calibration = std::move(*prepared);
        }
        an::Result<an::Image<float>> image = stage_utils::readRegistrationImage(
            scope, m_services.codecRegistry().get(), frame.path, calibration.get(), m_plan.demosaic);
        if (image.hasValue() && frame.warp)
        {
            image = stage_utils::warpRegistrationFrame(scope, *image, frame.transform, frame.distortion, m_plan.canvas,
                                                       m_plan.interpolation, std::nullopt);
        }
        if (!image.hasValue() || validityRuleFor(m_plan.plane) != an::ValidityRule::ZeroIsNoData)
        {
            return image;
        }
        // On the plane an exact zero means nothing, so a sample a calibration left not a number is read as one, as the
        // stack reads it.
        if (const an::Result<std::uint64_t> restated = stage_utils::zeroNonFiniteSamples(*image); !restated.hasValue())
        {
            return restated.status();
        }
        return image;
    }

    /// Reads a light and holds it to the extent and channels of the first light read.
    [[nodiscard]] an::Result<an::Image<float>> readChecked(const an::consumer::OperationScope& scope,
                                                           const std::size_t index)
    {
        an::Result<an::Image<float>> image = read(scope, index);
        if (!image.hasValue())
        {
            return image;
        }
        const auto descriptor = image->descriptor();
        if (!descriptor.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the frame holds nothing");
        }
        const auto& extent = descriptor->extent();
        if (m_channels == 0u)
        {
            m_width = extent.width();
            m_height = extent.height();
            m_channels = extent.channelCount();
        }
        else if (extent.width() != m_width || extent.height() != m_height || extent.channelCount() != m_channels)
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           QStringLiteral("%1 is %2x%3 of %4 channel(s) where the first light is %5x%6 of %7")
                               .arg(QFileInfo(m_plan.frames.at(index).path).fileName())
                               .arg(extent.width())
                               .arg(extent.height())
                               .arg(extent.channelCount())
                               .arg(m_width)
                               .arg(m_height)
                               .arg(m_channels));
        }
        return image;
    }

    [[nodiscard]] std::uint32_t width() const noexcept
    {
        return m_width;
    }
    [[nodiscard]] std::uint32_t height() const noexcept
    {
        return m_height;
    }
    [[nodiscard]] std::uint32_t channels() const noexcept
    {
        return m_channels;
    }

private:
    const ImageIntegration::WorkflowStageServices& m_services;
    const MeasurementPlan& m_plan;
    core::CalibrationMasterSet& m_masters;
    std::uint32_t m_width = 0u;
    std::uint32_t m_height = 0u;
    std::uint32_t m_channels = 0u;
};

using FrameProgress = std::function<void(int done, int total, const QString& operation, const QString& file)>;

/// What measuring a group needs that its settings say.
struct MeasurementSettings
{
    an::NormalizationMethod method = an::NormalizationMethod::AdditiveScaling;
    an::ScaleEstimator estimator = an::ScaleEstimator::MedianAbsoluteDeviation;
    std::size_t primaryReference = 0u;
    std::vector<std::size_t> references;
};

/// One global match per light against the references combined, the primary reference at the identity.
[[nodiscard]] an::Result<std::vector<an::FrameNormalization>>
measureGlobal(const an::consumer::OperationScope& scope, FrameReader& reader, const MeasurementPlan& plan,
              const MeasurementSettings& settings, const FrameProgress& progress)
{
    const an::ValidityRule rule = validityRuleFor(plan.plane);
    std::vector<an::NormalizationFrameStatistics> statistics;
    statistics.reserve(plan.frames.size());
    for (std::size_t index = 0; index < plan.frames.size(); ++index)
    {
        an::Result<an::Image<float>> image = reader.readChecked(scope, index);
        if (!image.hasValue())
        {
            return image.status();
        }
        const an::Result<an::NormalizationFrameView> view = frameViewOf(*image, rule);
        if (!view.hasValue())
        {
            return view.status();
        }
        an::Result<an::NormalizationFrameStatistics> measured =
            an::measureGlobalNormalization(scope.execution(), *view, settings.estimator);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        statistics.push_back(*measured);
        progress(static_cast<int>(index) + 1, static_cast<int>(plan.frames.size()),
                 QStringLiteral("Measuring global normalization"), plan.frames[index].path);
    }
    std::vector<an::NormalizationFrameStatistics> members;
    for (const std::size_t reference : settings.references)
    {
        members.push_back(statistics[reference]);
    }
    const an::Result<an::NormalizationReference> reference = an::combineNormalizationReferences(members);
    if (!reference.hasValue())
    {
        return reference.status();
    }
    std::vector<an::FrameNormalization> matches;
    matches.reserve(statistics.size());
    for (std::size_t index = 0; index < statistics.size(); ++index)
    {
        an::Result<an::FrameNormalization> match =
            index == settings.primaryReference
                ? an::identityNormalization(settings.method, reader.channels(), settings.estimator)
                : an::estimateGlobalNormalization(*reference, statistics[index], settings.method);
        if (!match.hasValue())
        {
            return match.status();
        }
        matches.push_back(*match);
    }
    return matches;
}

/// The matches that best reconcile every overlapping pair of lights, or a refusal the caller falls back from.
[[nodiscard]] an::Result<std::vector<an::FrameNormalization>>
measureOverlap(const an::consumer::OperationScope& scope, FrameReader& reader, const MeasurementPlan& plan,
               const MeasurementSettings& settings, const OverlapNormalizationSettings& overlap,
               const FrameProgress& progress)
{
    const std::size_t count = plan.frames.size();
    std::vector<std::optional<an::StatisticsRegion>> coverage(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        an::Result<an::Image<float>> image = reader.readChecked(scope, index);
        if (!image.hasValue())
        {
            return image.status();
        }
        coverage[index] = coverageOf(*image);
        progress(static_cast<int>(index) + 1, static_cast<int>(count), QStringLiteral("Finding overlaps"),
                 plan.frames[index].path);
    }

    std::vector<an::OverlapCandidate> candidates;
    std::vector<an::StatisticsRegion> regions;
    const auto minimumPixels = static_cast<std::uint64_t>((std::max)(0, overlap.minOverlapPixels));
    const double minimumFraction = (std::max)(0.0, overlap.minOverlapFraction);
    for (std::size_t a = 0; a < count; ++a)
    {
        for (std::size_t b = a + 1; b < count; ++b)
        {
            if (!coverage[a].has_value() || !coverage[b].has_value())
            {
                continue;
            }
            const auto& ra = *coverage[a];
            const auto& rb = *coverage[b];
            const std::uint32_t x0 = (std::max)(ra.originX, rb.originX);
            const std::uint32_t y0 = (std::max)(ra.originY, rb.originY);
            const std::uint32_t x1 = (std::min)(ra.originX + ra.width, rb.originX + rb.width);
            const std::uint32_t y1 = (std::min)(ra.originY + ra.height, rb.originY + rb.height);
            if (x1 <= x0 || y1 <= y0)
            {
                continue;
            }
            const std::uint64_t area = static_cast<std::uint64_t>(x1 - x0) * (y1 - y0);
            const std::uint64_t smaller = (std::min)(static_cast<std::uint64_t>(ra.width) * ra.height,
                                                     static_cast<std::uint64_t>(rb.width) * rb.height);
            if (area < minimumPixels || static_cast<double>(area) < minimumFraction * static_cast<double>(smaller))
            {
                continue;
            }
            candidates.push_back({static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), area});
            regions.push_back({x0, y0, x1 - x0, y1 - y0});
        }
    }
    if (candidates.empty())
    {
        return an::Status::failure(an::ErrorCode::EmptySelection, "no two lights overlap enough");
    }

    const auto measurePairs = [&](const an::OverlapGraphPolicy policy,
                                  std::vector<an::OverlapMeasurement>& pairs) -> an::Status {
        std::vector<std::uint8_t> selected(candidates.size(), 0u);
        const an::Result<an::OverlapPairSelection> selection =
            an::selectOverlapPairs(scope.execution(), candidates, static_cast<std::uint32_t>(count),
                                   static_cast<std::uint32_t>(settings.primaryReference), policy, selected);
        if (!selection.hasValue())
        {
            return selection.status();
        }
        if (!selection->connected)
        {
            return an::Status::failure(an::ErrorCode::InvalidData, "the overlapping lights do not connect");
        }
        std::map<std::size_t, an::Image<float>> resident;
        const auto residentFrame = [&](const std::size_t index) -> an::Result<const an::Image<float>*> {
            if (const auto found = resident.find(index); found != resident.end())
            {
                return &found->second;
            }
            if (resident.size() >= 2u)
            {
                resident.erase(resident.begin());
            }
            an::Result<an::Image<float>> image = reader.readChecked(scope, index);
            if (!image.hasValue())
            {
                return image.status();
            }
            return &resident.emplace(index, std::move(*image)).first->second;
        };
        int measured = 0;
        for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate)
        {
            if (selected[candidate] == 0u)
            {
                continue;
            }
            const auto& pair = candidates[candidate];
            an::Result<const an::Image<float>*> a = residentFrame(pair.frameA);
            if (!a.hasValue())
            {
                return a.status();
            }
            const an::Image<float>* imageA = *a;
            an::Result<const an::Image<float>*> b = residentFrame(pair.frameB);
            if (!b.hasValue())
            {
                return b.status();
            }
            if (resident.find(pair.frameA) == resident.end())
            {
                an::Result<const an::Image<float>*> again = residentFrame(pair.frameA);
                if (!again.hasValue())
                {
                    return again.status();
                }
                imageA = *again;
            }
            const an::Result<an::NormalizationFrameView> viewA = frameViewOf(*imageA, validityRuleFor(plan.plane));
            const an::Result<an::NormalizationFrameView> viewB = frameViewOf(**b, validityRuleFor(plan.plane));
            if (!viewA.hasValue() || !viewB.hasValue())
            {
                return viewA.hasValue() ? viewB.status() : viewA.status();
            }
            an::Result<an::OverlapMeasurement> measurement =
                an::measureOverlapStatistics(scope.execution(), pair.frameA, *viewA, regions[candidate], pair.frameB,
                                             *viewB, regions[candidate], settings.estimator);
            if (measurement.hasValue())
            {
                pairs.push_back(*measurement);
            }
            else if (cancelledStatus(measurement.status()) ||
                     measurement.status().code() != an::ErrorCode::EmptySelection)
            {
                return measurement.status();
            }
            progress(++measured, static_cast<int>(selection->selectedCount), QStringLiteral("Measuring overlaps"),
                     plan.frames[pair.frameB].path);
        }
        if (pairs.empty())
        {
            return an::Status::failure(an::ErrorCode::EmptySelection, "no overlapping pair shares a sample");
        }
        return an::Status{};
    };

    const auto solve =
        [&](const an::OverlapGraphPolicy policy,
            const an::OverlapDegeneracyPolicy degeneracy) -> an::Result<std::vector<an::FrameNormalization>> {
        std::vector<an::OverlapMeasurement> pairs;
        if (const an::Status measured = measurePairs(policy, pairs); measured.failed())
        {
            return measured;
        }
        std::vector<an::FrameNormalization> matches(count);
        const an::Result<an::OverlapSolveReport> solved = an::solveOverlapNormalization(
            scope.execution(), pairs, static_cast<std::uint32_t>(settings.primaryReference), settings.method,
            degeneracy, matches);
        if (!solved.hasValue())
        {
            return solved.status();
        }
        return matches;
    };

    // The sparse graph is tried first and refuses a degenerate system, so it is retried over every pair, which
    // substitutes what it cannot solve.
    an::Result<std::vector<an::FrameNormalization>> sparse =
        solve(ImageIntegration::kWorkflowOverlapGraphPolicy, an::OverlapDegeneracyPolicy::Refuse);
    if (sparse.hasValue() || cancelledStatus(sparse.status()))
    {
        return sparse;
    }
    return solve(an::OverlapGraphPolicy::AllPairs, an::OverlapDegeneracyPolicy::SubstituteIdentity);
}

/// What a group's local normalization measured: the engine's document and the light its scales were measured against.
struct LocalMeasurement
{
    an::LocalNormalizationDocument document;
    std::size_t analysisReference = 0u;
};

/// One background surface per light, fitted on its analysis copy; the reference surface, of the named reference
/// frames or of the lights the analysis ranks best; each light's scale against the best of them - its stars' fitted
/// fluxes where enough pair up, the residual ratio otherwise - and, over the locally normalized light, its global
/// match. A reference of one frame is that frame, brought to itself; a reference of several is their surfaces
/// integrated, and every light is brought to it.
[[nodiscard]] an::Result<LocalMeasurement>
measureLocal(const an::consumer::OperationScope& scope, FrameReader& reader, const MeasurementPlan& plan,
             const MeasurementSettings& settings, const an::LocalNormalizationRequest& request,
             const ImageIntegration::LocalNormalizationAnalysis& analysis, const bool namedReferences,
             const bool scaleGrid, const FrameProgress& progress)
{
    const an::ValidityRule rule = validityRuleFor(plan.plane);
    const an::KernelValidityView validityView = [&] {
        an::KernelValidityView view;
        if (const auto descriptor = an::ValidityDescriptor::create({.rule = rule}))
        {
            view.descriptor = *descriptor;
        }
        return view;
    }();
    const std::size_t count = plan.frames.size();
    const int steps = static_cast<int>(namedReferences ? count : 2u * count);
    int done = 0;

    std::optional<an::LocalNormalizationPlan> localPlan;
    const auto ensurePlan = [&]() -> an::Status {
        if (localPlan.has_value())
        {
            return an::Status{};
        }
        an::Result<an::LocalNormalizationPlan> planned =
            an::planLocalNormalization(request, reader.width(), reader.height(), reader.channels());
        if (!planned.hasValue())
        {
            return planned.status();
        }
        if (plan.drizzlePlane.has_value())
        {
            // A drizzled group's grid is the one the engine states over its canvas's analysis plane.
            an::DrizzleCanvas canvas;
            canvas.analysisWidth = plan.drizzlePlane->width;
            canvas.analysisHeight = plan.drizzlePlane->height;
            const an::Result<an::LocalNormalizationGridShape> shape =
                an::drizzleLocalNormalizationShape(canvas, request.gridSize, reader.channels());
            if (!shape.hasValue())
            {
                return shape.status();
            }
            if (!(*shape == planned->shape()))
            {
                return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                           "the lights are not the drizzle canvas's analysis plane");
            }
        }
        localPlan = std::move(*planned);
        return an::Status{};
    };
    const auto analysisViewOf =
        [&](const ImageIntegration::LocalNormalizationAnalysisFrame& frame) -> an::Result<an::NormalizationFrameView> {
        return an::NormalizationFrameView::create(frame.samples(), frame.layout().totalBytes(), frame.layout(),
                                                  frame.descriptor(), validityView);
    };
    std::vector<std::vector<double>> backgrounds(count);
    const auto fitBackground = [&](const ImageIntegration::LocalNormalizationAnalysisFrame& frame,
                                   const std::size_t index) -> an::Status {
        const an::Result<an::NormalizationFrameView> view = analysisViewOf(frame);
        if (!view.hasValue())
        {
            return view.status();
        }
        backgrounds[index].assign(static_cast<std::size_t>(localPlan->shape().valueCount()), 0.0);
        const auto fitted = an::fitLocalBackgroundGrid(scope.execution(), *view, *localPlan, backgrounds[index]);
        return fitted.hasValue() ? an::Status{} : fitted.status();
    };
    // Reads a light, plans the grid from the first one read, and prepares its analysis copy.
    const auto readAnalysed =
        [&](const std::size_t index,
            std::optional<an::Image<float>>& light) -> an::Result<ImageIntegration::LocalNormalizationAnalysisFrame> {
        an::Result<an::Image<float>> image = reader.readChecked(scope, index);
        if (!image.hasValue())
        {
            return image.status();
        }
        light.emplace(std::move(*image));
        if (const an::Status planned = ensurePlan(); planned.failed())
        {
            return planned;
        }
        return ImageIntegration::LocalNormalizationAnalysisFrame::prepare(scope.execution(), analysis, *light, rule);
    };

    // The reference frames: those named, or the lights the analysis ranks best.
    std::vector<std::size_t> references;
    if (namedReferences)
    {
        references = settings.references;
    }
    else
    {
        std::vector<std::pair<std::size_t, double>> ranked;
        for (std::size_t index = 0; index < count; ++index)
        {
            std::optional<an::Image<float>> light;
            an::Result<ImageIntegration::LocalNormalizationAnalysisFrame> frame = readAnalysed(index, light);
            if (!frame.hasValue())
            {
                return frame.status();
            }
            const an::Result<std::optional<double>> figure =
                ImageIntegration::localNormalizationRankingFigure(scope.execution(), analysis, *frame);
            if (!figure.hasValue())
            {
                return figure.status();
            }
            if (figure->has_value())
            {
                ranked.emplace_back(index, **figure);
            }
            if (const an::Status fitted = fitBackground(*frame, index); fitted.failed())
            {
                return fitted;
            }
            progress(++done, steps, QStringLiteral("Ranking reference frames"), plan.frames[index].path);
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return left.second != right.second ? left.second > right.second : left.first < right.first;
        });
        if (ranked.empty())
        {
            references = {settings.primaryReference};
        }
        else if (analysis.strategy == ImageIntegration::LocalNormReferenceStrategy::SingleBestFrame)
        {
            references = {ranked.front().first};
        }
        else
        {
            const std::size_t kept = (std::min)(ranked.size(), static_cast<std::size_t>(analysis.maxIntegratedFrames));
            for (std::size_t position = 0; position < kept; ++position)
            {
                references.push_back(ranked[position].first);
            }
        }
    }
    const std::size_t analysisReference = references.front();
    const bool integrated = references.size() > 1u;

    // The analysis reference stays resident: every light's scale is measured against it.
    std::optional<an::Image<float>> referenceLight;
    an::Result<ImageIntegration::LocalNormalizationAnalysisFrame> referenceFrame =
        readAnalysed(analysisReference, referenceLight);
    if (!referenceFrame.hasValue())
    {
        return referenceFrame.status();
    }
    if (backgrounds[analysisReference].empty())
    {
        if (const an::Status fitted = fitBackground(*referenceFrame, analysisReference); fitted.failed())
        {
            return fitted;
        }
    }
    for (const std::size_t reference : references)
    {
        if (!backgrounds[reference].empty())
        {
            continue;
        }
        std::optional<an::Image<float>> light;
        an::Result<ImageIntegration::LocalNormalizationAnalysisFrame> frame = readAnalysed(reference, light);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        if (const an::Status fitted = fitBackground(*frame, reference); fitted.failed())
        {
            return fitted;
        }
    }
    const an::LocalNormalizationGridShape shape = localPlan->shape();
    std::vector<double> referenceBackground = backgrounds[analysisReference];
    if (integrated)
    {
        std::vector<std::span<const double>> members;
        for (const std::size_t reference : references)
        {
            members.emplace_back(backgrounds[reference]);
        }
        if (const an::Status combined = an::combineLocalGrids(shape, members, referenceBackground); combined.failed())
        {
            return combined;
        }
    }
    const an::Result<an::NormalizationFrameView> referenceView = analysisViewOf(*referenceFrame);
    if (!referenceView.hasValue())
    {
        return referenceView.status();
    }

    // The channels a flux scale is measured in: each one, or the detection channel standing for all of them.
    const bool perChannel = request.scaleMode == an::LocalScaleMode::PerChannel;
    std::vector<std::uint32_t> fluxChannels;
    if (analysis.psfFluxScale)
    {
        if (perChannel)
        {
            for (std::uint32_t channel = 0; channel < shape.channelCount; ++channel)
            {
                fluxChannels.push_back(channel);
            }
        }
        else
        {
            fluxChannels.push_back(ImageIntegration::localNormalizationDetectionChannel(*referenceFrame));
        }
    }
    std::vector<ImageIntegration::LocalNormalizationStars> referenceStars;
    for (const std::uint32_t channel : fluxChannels)
    {
        an::Result<ImageIntegration::LocalNormalizationStars> stars =
            ImageIntegration::detectLocalNormalizationStars(scope.execution(), analysis, *referenceFrame, channel);
        if (!stars.hasValue())
        {
            return stars.status();
        }
        referenceStars.push_back(std::move(*stars));
    }

    LocalMeasurement measurement;
    measurement.analysisReference = analysisReference;
    an::LocalNormalizationDocument& document = measurement.document;
    document.request = request;
    document.shape = shape;
    document.validity = rule;
    if (!integrated)
    {
        document.referenceIndex = static_cast<std::uint32_t>(analysisReference);
    }
    document.referenceBackground = referenceBackground;
    document.frames.resize(count);

    // Brings a light to the reference surface in place and measures its global statistics there.
    std::vector<an::NormalizationFrameStatistics> statistics(count);
    const auto normalizeAndMeasure = [&](an::Image<float>& light, const std::size_t index) -> an::Status {
        const an::LocalNormalizationFrameCoefficients& record = document.frames[index];
        an::LocalNormalizationApplyRequest apply;
        apply.shape = shape;
        apply.interpolation = request.interpolation;
        apply.targetBackground = record.background;
        apply.referenceBackground = referenceBackground;
        apply.scale = std::span<const double>(record.scale.data(), shape.channelCount);
        apply.scaleGrid = record.scaleGrid;
        auto access = light.write();
        if (!access.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the frame cannot be written");
        }
        const auto& writable = access->view();
        const an::Result<an::NormalizationFrameView> source = frameViewOf(light, rule);
        if (!source.hasValue())
        {
            return source.status();
        }
        if (const auto applied = an::applyLocalNormalization(scope.execution(), apply, *source, writable.data(),
                                                             writable.layout().totalBytes(), writable.layout());
            !applied.hasValue())
        {
            return applied.status();
        }
        const an::Result<an::NormalizationFrameView> normalized = frameViewOf(light, rule);
        if (!normalized.hasValue())
        {
            return normalized.status();
        }
        const an::Result<an::NormalizationFrameStatistics> measured =
            an::measureGlobalNormalization(scope.execution(), *normalized, settings.estimator);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        statistics[index] = *measured;
        return an::Status{};
    };

    for (std::size_t index = 0; index < count; ++index)
    {
        an::LocalNormalizationFrameCoefficients& record = document.frames[index];
        record.frameIndex = static_cast<std::uint32_t>(index);
        if (index == analysisReference)
        {
            // Measured last, once every other light's scale has been read against its samples.
            continue;
        }
        std::optional<an::Image<float>> light;
        an::Result<ImageIntegration::LocalNormalizationAnalysisFrame> frame = readAnalysed(index, light);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        if (backgrounds[index].empty())
        {
            if (const an::Status fitted = fitBackground(*frame, index); fitted.failed())
            {
                return fitted;
            }
        }
        record.background = backgrounds[index];
        const an::Result<an::NormalizationFrameView> view = analysisViewOf(*frame);
        if (!view.hasValue())
        {
            return view.status();
        }
        const an::Result<an::LocalScaleEstimate> scale = an::estimateLocalScale(
            scope.execution(), *view, record.background, *referenceView, backgrounds[analysisReference], *localPlan);
        if (!scale.hasValue())
        {
            return scale.status();
        }
        for (std::uint32_t channel = 0; channel < shape.channelCount; ++channel)
        {
            record.scale[channel] = scale->scale[channel];
        }
        for (std::size_t position = 0; position < fluxChannels.size(); ++position)
        {
            const an::Result<ImageIntegration::LocalNormalizationStars> stars =
                ImageIntegration::detectLocalNormalizationStars(scope.execution(), analysis, *frame,
                                                                fluxChannels[position]);
            if (!stars.hasValue())
            {
                return stars.status();
            }
            const std::optional<double> flux =
                ImageIntegration::localNormalizationFluxScale(referenceStars[position], *stars);
            if (!flux.has_value())
            {
                continue;
            }
            if (perChannel)
            {
                record.scale[fluxChannels[position]] = *flux;
            }
            else
            {
                std::fill_n(record.scale.begin(), shape.channelCount, *flux);
            }
        }
        if (scaleGrid)
        {
            record.scaleGrid.resize(static_cast<std::size_t>(shape.valueCount()));
            const an::Result<an::LocalScaleGridResult> fitted = an::fitLocalScaleGrid(
                scope.execution(), *view, record.background, *referenceView, backgrounds[analysisReference], *localPlan,
                std::span<const double>(record.scale.data(), shape.channelCount), record.scaleGrid);
            if (!fitted.hasValue())
            {
                return fitted.status();
            }
        }
        if (const an::Status measured = normalizeAndMeasure(*light, index); measured.failed())
        {
            return measured;
        }
        progress(++done, steps, QStringLiteral("Fitting local normalization"), plan.frames[index].path);
    }

    // The analysis reference: brought to itself when it is the reference, to the integrated surface otherwise, its
    // scale one either way.
    {
        an::LocalNormalizationFrameCoefficients& record = document.frames[analysisReference];
        record.background = integrated ? backgrounds[analysisReference] : referenceBackground;
        if (integrated)
        {
            if (const an::Status measured = normalizeAndMeasure(*referenceLight, analysisReference); measured.failed())
            {
                return measured;
            }
        }
        else
        {
            const an::Result<an::NormalizationFrameView> view = frameViewOf(*referenceLight, rule);
            if (!view.hasValue())
            {
                return view.status();
            }
            const an::Result<an::NormalizationFrameStatistics> measured =
                an::measureGlobalNormalization(scope.execution(), *view, settings.estimator);
            if (!measured.hasValue())
            {
                return measured.status();
            }
            statistics[analysisReference] = *measured;
        }
        progress(++done, steps, QStringLiteral("Fitting local normalization"), plan.frames[analysisReference].path);
    }

    std::vector<an::NormalizationFrameStatistics> members;
    for (const std::size_t reference : references)
    {
        members.push_back(statistics[reference]);
    }
    const an::Result<an::NormalizationReference> reference = an::combineNormalizationReferences(members);
    if (!reference.hasValue())
    {
        return reference.status();
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        an::Result<an::FrameNormalization> match =
            !integrated && index == analysisReference
                ? an::identityNormalization(settings.method, shape.channelCount, settings.estimator)
                : an::estimateGlobalNormalization(*reference, statistics[index], settings.method);
        if (!match.hasValue())
        {
            return match.status();
        }
        document.frames[index].global = *match;
    }
    return measurement;
}

} // namespace

QString LightsNormalizationStage::name() const
{
    return QStringLiteral("LightsNormalization");
}

WorkflowStage LightsNormalizationStage::stageId() const
{
    return WorkflowStage::LightsNormalization;
}

bool LightsNormalizationStage::shouldSkip(const WorkflowManifest& manifest) const
{
    const auto groups = groupsToProcess(manifest);
    return std::none_of(groups.begin(), groups.end(), [](const WorkflowManifest::IntegrationGroup* group) {
        return group != nullptr && group->stacking.normalizationMode != NormalizationMode::None;
    });
}

QString LightsNormalizationStage::skipReason(const WorkflowManifest& manifest) const
{
    if (manifest.integrationGroups.empty())
    {
        return QStringLiteral("No integration groups.");
    }
    const auto groups = groupsToProcess(manifest);
    if (manifest.executionMode == ExecutionMode::Incremental && groups.empty())
    {
        return QStringLiteral("No dirty integration groups in incremental plan.");
    }
    return QStringLiteral("Normalization disabled for all groups.");
}

StageResult LightsNormalizationStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                              ProgressReporter& reporter)
{
    const auto groups = groupsToProcess(manifest);
    if (groups.empty() || shouldSkip(manifest))
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
        return StageResult::failure(IntegrationError::internalError(QStringLiteral(
            "The lights normalization stage runs only inside a workflow run, which lends it the session")));
    }
    const QDir outputRoot(manifest.outputDirectory);
    if (!outputRoot.exists() && !outputRoot.mkpath(QStringLiteral(".")))
    {
        return StageResult::failure(IntegrationError::directoryNotWritable(manifest.outputDirectory));
    }
    const QString metricsPath = outputRoot.filePath(QStringLiteral("registration_metrics.db"));
    std::unique_ptr<stage_utils::RegistrationMetricsReader> metricsReader;
    if (QFileInfo::exists(metricsPath))
    {
        metricsReader = std::make_unique<stage_utils::RegistrationMetricsReader>(
            metricsPath, QStringLiteral("NormalizationMetrics"), executionCounterAccumulator());
        QString openError;
        if (!metricsReader->open(&openError))
        {
            LOG_WARNING("LightsNormalizationStage: failed to open registration metrics DB: {}",
                        openError.toStdString());
            metricsReader.reset();
        }
    }
    const ExecutionPreparationSnapshot* snapshot = executionPreparationSnapshot();
    core::CalibrationMasterSet masters(services->codecRegistry());

    const int totalGroups = static_cast<int>(groups.size());
    reporter.logger().stageStarted(name(), totalGroups);
    stage_utils::reportStageProgress(reporter, stageId(), 1, (std::max)(1, totalGroups), 0.0f,
                                     QStringLiteral("Preparing normalization"), QString());
    QElapsedTimer stageTimer;
    stageTimer.start();
    int groupIndex = 0;
    IntegrationErrorReport errorReport;

    for (const auto* groupPtr : groups)
    {
        const auto& group = *groupPtr;
        ++groupIndex;
        const QString groupLabel = group.groupKey.toCompactString();
        reporter.logger().groupStarted(groupIndex, totalGroups, groupLabel);
        QElapsedTimer groupTimer;
        groupTimer.start();
        bool abortStage = false;

        const auto report = [&](const float fraction, const QString& operation, const QString& file) {
            stage_utils::reportStageProgress(reporter, stageId(), groupIndex, totalGroups, fraction, operation, file);
        };

        // Where and how the group's lights are measured.
        const auto planGroup = [&](const std::vector<FrameId>& inputFrames) -> PlanOutcome {
            PlanOutcome outcome;
            MeasurementPlan plan;
            const auto* registration = findRegistrationGroup(manifest, group.registrationGroupKey);
            if (registration != nullptr)
            {
                plan.demosaic = an::ui::processes::registrationDemosaicOf(registration->settings);
                plan.interpolation = stage_utils::parseInterpolation(registration->settings.interpolation)
                                         .value_or(an::KernelInterpolation::Lanczos4);
            }
            const bool drizzled = group.drizzle.has_value() && group.drizzle->enabled;
            const bool comet = ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode);
            const auto calibrationOf = [&](const FrameId& light,
                                           const QString& path) -> const WorkflowManifest::LightsCalibrationParams* {
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
            };

            if (comet && drizzled)
            {
                reporter.logger().warning(QStringLiteral(
                    "A drizzled comet stack normalizes its comet-aligned lights as it reads them; no coefficients are "
                    "measured beforehand."));
                outcome.result = StageResult::success();
                return outcome;
            }

            if (comet)
            {
                abortStage = true;
                QString error;
                const std::optional<int> preferred =
                    registration != nullptr ? std::optional<int>(registration->settings.version) : std::nullopt;
                const std::optional<int> version = metricsReader ? metricsReader->resolveSettingsVersionForGroup(
                                                                       group.registrationGroupKey.toString(), preferred)
                                                                 : std::nullopt;
                auto request = metricsReader
                                   ? ImageIntegration::cometProductRequestFor(metricsPath, group, version, false,
                                                                              executionCounterAccumulator(), error)
                                   : std::nullopt;
                if (!request.has_value())
                {
                    outcome.result = StageResult::failure(IntegrationError(
                        IntegrationErrorCode::MissingRequiredFrames,
                        error.isEmpty()
                            ? QStringLiteral("Comet normalization requires registration metrics with comet_aligned "
                                             "products.")
                            : error));
                    return outcome;
                }
                std::vector<std::pair<FrameId, QString>> products;
                if (ImageIntegration::usesStarsPlusCometExperimentalStackingMode(group.stacking.cometStackingMode))
                {
                    const auto dual = an::ui::resolveStarsPlusCometExperimentalProducts(*request);
                    if (!dual.available)
                    {
                        outcome.result = StageResult::failure(IntegrationError(
                            IntegrationErrorCode::MissingRequiredFrames,
                            dual.errorMessage.trimmed().isEmpty()
                                ? QStringLiteral("%1 normalization requires matching star base and pure comet "
                                                 "products.")
                                      .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken())
                                : dual.errorMessage));
                        return outcome;
                    }
                    for (const auto& product : dual.starBaseProducts)
                    {
                        products.emplace_back(product.frameId, product.outputPath);
                    }
                    plan.cometProductsFingerprint = ImageIntegration::fingerprintCometProducts(dual.cometProducts);
                }
                else
                {
                    const auto resolved = an::ui::resolveCometStackingProducts(*request);
                    if (!resolved.available || resolved.kind != an::ui::CometStackingProductKind::RegisteredOutputs)
                    {
                        outcome.result = StageResult::failure(IntegrationError(
                            IntegrationErrorCode::MissingRequiredFrames,
                            resolved.errorMessage.trimmed().isEmpty()
                                ? QStringLiteral("Comet normalization requires comet_aligned registered outputs.")
                                : resolved.errorMessage));
                        return outcome;
                    }
                    for (const auto& output : resolved.registeredOutputs)
                    {
                        products.emplace_back(output.frameId, output.outputPath);
                    }
                    plan.cometProductsFingerprint = ImageIntegration::fingerprintCometProducts(resolved);
                }
                plan.plane = NormalizationMeasurementPlane::RegisteredCanvas;
                plan.registrationSettingsVersion = version;
                for (const FrameId& light : inputFrames)
                {
                    const auto product = std::find_if(products.begin(), products.end(), [&](const auto& candidate) {
                        return candidate.first.identityKey() == light.identityKey();
                    });
                    if (product == products.end())
                    {
                        plan.skipped << QStringLiteral("%1 (no comet product)").arg(light.canonicalPath());
                        continue;
                    }
                    plan.frames.push_back({.logical = light, .path = product->second, .workflowOwned = true});
                }
                outcome.plan = std::move(plan);
                return outcome;
            }

            if (drizzled)
            {
                ImageIntegration::DrizzleGeometryResolution resolution = ImageIntegration::resolveDrizzleGroupGeometry(
                    *services, manifest, group, executionCounterAccumulator());
                if (resolution.cancelled)
                {
                    outcome.result = StageResult::cancelled();
                    return outcome;
                }
                if (!resolution.geometry.has_value())
                {
                    outcome.result = StageResult::failure(resolution.error);
                    return outcome;
                }
                const ImageIntegration::DrizzleGroupGeometry& geometry = *resolution.geometry;
                plan.plane = NormalizationMeasurementPlane::DrizzleAnalysisPlane;
                plan.registrationSettingsVersion = geometry.settingsVersion;
                plan.canvas = an::RegistrationCanvas{.boundingBox = geometry.canvas.boundingBox,
                                                     .origin = geometry.canvas.origin,
                                                     .width = geometry.canvas.analysisWidth,
                                                     .height = geometry.canvas.analysisHeight};
                plan.drizzlePlane = ImageIntegration::DrizzleAnalysisPlaneRecord{
                    geometry.canvas.origin.x, geometry.canvas.origin.y, geometry.canvas.analysisWidth,
                    geometry.canvas.analysisHeight, static_cast<double>(group.drizzle->scale)};
                QJsonObject transformsByLight;
                for (const auto& frame : geometry.frames)
                {
                    transformsByLight.insert(frame.frame.canonicalPath(),
                                             stage_utils::serializeTransformJson(frame.transform, frame.provenance));
                }
                plan.geometryFingerprint = ImageIntegration::warpedPlaneGeometryFingerprint(
                    NormalizationMeasurementPlane::DrizzleAnalysisPlane, transformsByLight, geometry.canvas.origin.x,
                    geometry.canvas.origin.y, geometry.canvas.analysisWidth, geometry.canvas.analysisHeight,
                    plan.interpolation);
                for (const QString& missing : geometry.withoutTransform)
                {
                    plan.skipped << QStringLiteral("%1 (no registration transform)").arg(missing);
                }
                for (const FrameId& light : inputFrames)
                {
                    const auto frame =
                        std::find_if(geometry.frames.begin(), geometry.frames.end(), [&](const auto& candidate) {
                            return candidate.frame.identityKey() == light.identityKey();
                        });
                    if (frame == geometry.frames.end())
                    {
                        continue;
                    }
                    plan.frames.push_back(
                        {.logical = light,
                         .path = frame->inputPath,
                         .warp = true,
                         .transform = frame->transform,
                         .distortion = frame->resolvedDistortion,
                         .calibration = calibrationOf(light, frame->inputPath),
                         .workflowOwned = stage_utils::isWorkflowOwnedOutputPath(manifest, FrameId(frame->inputPath))});
                }
                outcome.plan = std::move(plan);
                return outcome;
            }

            const QString registrationKey = group.registrationGroupKey.toString();
            if (registration == nullptr || registrationKey.trimmed().isEmpty() || !metricsReader)
            {
                if (!registrationKey.trimmed().isEmpty())
                {
                    reporter.logger().warning(QStringLiteral(
                        "No registration record was found for this group; its lights are measured as they are read."));
                }
                plan.plane = NormalizationMeasurementPlane::Frames;
                for (const FrameId& light : inputFrames)
                {
                    // The file as it is now: this run's calibration may have written it since the execution plan
                    // was made.
                    const QString path = registrationInputPathForFrame(
                        manifest.outputDirectory, manifest.calibrateLightsInMemory, registration, light);
                    plan.frames.push_back(
                        {.logical = light,
                         .path = path,
                         .calibration = calibrationOf(light, path),
                         .workflowOwned = stage_utils::isWorkflowOwnedOutputPath(manifest, FrameId(path))});
                }
                outcome.plan = std::move(plan);
                return outcome;
            }

            const std::optional<int> version =
                metricsReader->resolveSettingsVersionForGroup(registrationKey, registration->settings.version);
            if (!version.has_value())
            {
                outcome.result = StageResult::failure(IntegrationError(
                    IntegrationErrorCode::MissingRequiredFrames,
                    QStringLiteral("No registration was recorded for the group's lights"), {}, registrationKey));
                return outcome;
            }
            plan.registrationSettingsVersion = version;
            plan.plane = NormalizationMeasurementPlane::RegisteredCanvas;

            // The lights the registration accepted are the ones with a transform; the stack reads only those.
            std::unordered_map<QString, an::RegistrationTransform> transforms;
            std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
            QString error;
            if (!metricsReader->loadTransforms(registrationKey, *version, transforms, error, &provenance) ||
                !stage_utils::rekeyRegistrationFrameLookupByIdentity(transforms, &error) ||
                !stage_utils::rekeyRegistrationFrameLookupByIdentity(provenance, &error))
            {
                outcome.result = StageResult::failure(IntegrationError::registrationFailed(
                    error.isEmpty() ? QStringLiteral("Failed to load registration transforms") : error));
                return outcome;
            }

            // Every registered light current: read them as the alignment wrote them, on the plane it planned over the
            // whole registration - the one the stack reads. A light the registration refused has no output, and is
            // left out here as the stack leaves it out.
            std::unordered_map<QString, stage_utils::RegistrationOutputRecord> outputs;
            if (metricsReader->loadOutputRecords(registrationKey, *version, outputs) &&
                stage_utils::rekeyRegistrationFrameLookupByIdentity(outputs))
            {
                std::vector<MeasuredFrame> onDisk;
                QStringList refused;
                bool current = true;
                for (const FrameId& light : inputFrames)
                {
                    if (!transforms.contains(light.identityKey()))
                    {
                        refused << QStringLiteral("%1 (no registration transform)").arg(light.canonicalPath());
                        continue;
                    }
                    const auto record = outputs.find(light.identityKey());
                    if (record == outputs.end() || !record->second.succeeded ||
                        record->second.outputPath.trimmed().isEmpty())
                    {
                        current = false;
                        break;
                    }
                    const auto revision = ArtifactTracker::captureFileRevision(record->second.outputPath);
                    if (!revision.has_value() || revision->sizeBytes != record->second.outputSizeBytes ||
                        revision->modifiedAtMs != record->second.outputModifiedAtMs ||
                        revision->identityToken != record->second.outputIdentityToken)
                    {
                        current = false;
                        break;
                    }
                    onDisk.push_back({.logical = light, .path = record->second.outputPath, .workflowOwned = true});
                }
                if (current && !onDisk.empty())
                {
                    plan.skipped << refused;
                    plan.frames = std::move(onDisk);
                    plan.registeredOutputsOnDisk = true;
                    outcome.plan = std::move(plan);
                    return outcome;
                }
            }

            // Otherwise the registration inputs are warped onto the registration's plane as they are read.
            std::optional<FrameId> reference;
            if (const auto persisted = metricsReader->readReferenceFrameId(registration->groupKey, *version))
            {
                reference = stage_utils::persistedRegistrationFrameId(*persisted);
            }
            if (!reference.has_value())
            {
                reference = registration->referenceFrame;
            }
            an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
            if (!scope.hasValue())
            {
                outcome.result = cancelledStatus(scope.status())
                                     ? StageResult::cancelled()
                                     : StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                                           scope.status(), IntegrationErrorCode::NormalizationFailed,
                                           QStringLiteral("Normalization could not start")));
                return outcome;
            }
            std::vector<stage_utils::RegistrationOutputPlaneFrame> planeFrames;
            std::size_t referenceIndex = 0u;
            QJsonObject transformsByLight;
            for (const FrameId& light : inputFrames)
            {
                const auto transform = transforms.find(light.identityKey());
                if (transform == transforms.end())
                {
                    plan.skipped << QStringLiteral("%1 (no registration transform)").arg(light.canonicalPath());
                    continue;
                }
                const QString path = registrationInputPathForFrame(
                    manifest.outputDirectory, manifest.calibrateLightsInMemory, registration, light);
                const an::Result<stage_utils::RegistrationFrameFacts> facts =
                    stage_utils::probeRegistrationFrame(*scope, services->codecRegistry().get(), path);
                if (!facts.hasValue())
                {
                    outcome.result = cancelledStatus(facts.status())
                                         ? StageResult::cancelled()
                                         : StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                                               facts.status(), IntegrationErrorCode::FileNotReadable,
                                               QStringLiteral("A light's header could not be read"), path));
                    return outcome;
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
                if (reference.has_value() && reference->identityKey() == light.identityKey())
                {
                    referenceIndex = planeFrames.size();
                }
                planeFrames.push_back({transform->second, facts->width, facts->height, distortion});
                transformsByLight.insert(light.canonicalPath(),
                                         stage_utils::serializeTransformJson(transform->second, lightProvenance));
                plan.frames.push_back(
                    {.logical = light,
                     .path = path,
                     .warp = true,
                     .transform = transform->second,
                     .distortion = distortion,
                     .calibration = calibrationOf(light, path),
                     .workflowOwned = stage_utils::isWorkflowOwnedOutputPath(manifest, FrameId(path))});
            }
            if (planeFrames.empty())
            {
                outcome.result = StageResult::failure(IntegrationError::registrationFailed(
                    QStringLiteral("No light of the group has a registration transform")));
                return outcome;
            }
            // The plane the stack warps onto, planned as the stack plans it; the stack says when it is the standard
            // canvas in place of an intersection.
            const ImageIntegration::RegistrationCanvasMode mode =
                stage_utils::parseAlignmentMode(registration->settings.alignmentMode);
            const an::Result<stage_utils::PlannedRegistrationOutputPlane> plane =
                stage_utils::planRegistrationOutputPlane(planeFrames, referenceIndex, mode);
            if (!plane.hasValue())
            {
                outcome.result = StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                    plane.status(), IntegrationErrorCode::RegistrationFailed,
                    QStringLiteral("The registration's plane could not be planned")));
                return outcome;
            }
            plan.canvas = plane->canvas;
            plan.geometryFingerprint = ImageIntegration::warpedPlaneGeometryFingerprint(
                NormalizationMeasurementPlane::RegisteredCanvas, transformsByLight, plan.canvas.origin.x,
                plan.canvas.origin.y, plan.canvas.width, plan.canvas.height, plan.interpolation);
            reporter.logger().operationDetail(
                QStringLiteral("Registered inputs"),
                QStringLiteral("The registered lights are not all current; the registration inputs are warped onto "
                               "the registration's plane as they are measured."));
            outcome.plan = std::move(plan);
            return outcome;
        };

        const auto processGroup = [&]() -> StageResult {
            if (token.isCancellationRequested() || services->stopToken().stop_requested())
            {
                return StageResult::cancelled();
            }
            if (group.lightFiles.empty())
            {
                reporter.logger().operationDetail(QStringLiteral("Skipping"),
                                                  QStringLiteral("No light frames in group"));
                return StageResult::success();
            }
            if (group.stacking.normalizationMode == NormalizationMode::None)
            {
                report(1.0f, QStringLiteral("Normalization (disabled)"), groupLabel);
                return StageResult::success();
            }
            reporter.logger().operationDetail(QStringLiteral("Normalization settings"),
                                              stage_utils::describeNormalizationSettings(group.stacking));

            const bool useLocal = group.stacking.normalizationMode == NormalizationMode::Local &&
                                  group.stacking.localNormalization.enabled;
            const bool useOverlap = group.stacking.normalizationMode == NormalizationMode::Global &&
                                    group.stacking.overlapNormalization.enabled;

            std::vector<FrameId> inputFrames = group.lightFiles;
            std::sort(inputFrames.begin(), inputFrames.end());
            inputFrames.erase(std::unique(inputFrames.begin(), inputFrames.end()), inputFrames.end());

            const bool userReferences = !group.normalizationReferenceFrames.empty();
            std::vector<FrameId> referenceFrames =
                userReferences ? group.normalizationReferenceFrames : std::vector<FrameId>{inputFrames.front()};
            if (!userReferences)
            {
                reporter.logger().warning(QStringLiteral(
                    "No normalization reference frames were provided; falling back to the first light frame."));
            }
            reporter.logger().operationDetail(QStringLiteral("Reference frames"),
                                              stage_utils::describeReferenceSelection(group.stacking, referenceFrames));

            // A standard global group whose stack warps its lights as it reads them may reuse coefficients measured
            // for that route without reading a registered light.
            const bool drizzled = group.drizzle.has_value() && group.drizzle->enabled;
            if (!manifest.reprocessExisting && snapshot != nullptr && !useLocal && !useOverlap && !drizzled &&
                !ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode) &&
                ImageIntegration::transformOnReadGlobalCoefficientsReusable(manifest, group, *snapshot))
            {
                reporter.logger().operationDetail(
                    QStringLiteral("Reusing normalization coefficients"),
                    QStringLiteral("The coefficients measured for the lights warped as they are read are current."));
                report(1.0f, QStringLiteral("Reusing cached standard-global normalization coefficients"), groupLabel);
                return StageResult::success();
            }

            PlanOutcome planned = planGroup(inputFrames);
            if (planned.result.has_value())
            {
                return *planned.result;
            }
            MeasurementPlan& plan = *planned.plan;
            for (const QString& skipped : plan.skipped)
            {
                reporter.logger().warning(QStringLiteral("Not measured: %1").arg(skipped));
            }
            if (plan.frames.size() < 2u)
            {
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::MissingRequiredFrames,
                                     QStringLiteral("Not enough lights can be measured to normalize the group")));
            }

            // The references, among the lights measured.
            MeasurementSettings settings;
            settings.method = useLocal
                                  ? an::NormalizationMethod::AdditiveScaling
                                  : ImageIntegration::normalizationMethodFor(group.stacking.globalNormalizationMethod);
            settings.estimator = ImageIntegration::scaleEstimatorFor(group.stacking.scaleEstimator);
            for (const FrameId& reference : referenceFrames)
            {
                const auto found =
                    std::find_if(plan.frames.begin(), plan.frames.end(), [&](const MeasuredFrame& frame) {
                        return frame.logical.identityKey() == reference.identityKey();
                    });
                if (found == plan.frames.end())
                {
                    if (userReferences &&
                        group.stacking.normalizationReferenceMode == NormalizationReferenceMode::Manual)
                    {
                        return StageResult::failure(
                            IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                             QStringLiteral("Normalization reference frame not found in group inputs"),
                                             reference.canonicalPath(), groupLabel));
                    }
                    reporter.logger().warning(QStringLiteral("Reference frame %1 is not measured and is left out.")
                                                  .arg(QFileInfo(reference.canonicalPath()).fileName()));
                    continue;
                }
                const auto index = static_cast<std::size_t>(std::distance(plan.frames.begin(), found));
                if (std::find(settings.references.begin(), settings.references.end(), index) ==
                    settings.references.end())
                {
                    settings.references.push_back(index);
                }
            }
            if (settings.references.empty())
            {
                settings.references.push_back(0u);
            }
            settings.primaryReference = settings.references.front();

            // The fingerprints: the files read, as each is now, and the settings and references.
            std::vector<ExecutionPreparationSnapshot::FileStamp> stamps;
            stamps.reserve(plan.frames.size());
            for (const MeasuredFrame& frame : plan.frames)
            {
                auto captured = captureFrameFileStamps({FrameId(frame.path)}, frame.workflowOwned ? nullptr : snapshot);
                stamps.push_back(std::move(captured.front()));
            }
            QString inputsFingerprint = computeFrameFileStampFingerprint(stamps);
            if (!plan.geometryFingerprint.isEmpty())
            {
                inputsFingerprint =
                    ImageIntegration::warpedMeasurementInputsFingerprint(inputsFingerprint, plan.geometryFingerprint);
            }
            const QString settingsFingerprint =
                computeNormalizationSettingsFingerprint(group, referenceFrames, plan.cometProductsFingerprint);
            const QString artifactPath = stage_utils::normalizationArtifactPath(manifest, group);
            const QString identity =
                ImageIntegration::normalizationArtifactIdentity(inputsFingerprint, settingsFingerprint);

            (void)ImageIntegration::recoverNormalizationArtifact(artifactPath);
            const ImageIntegration::NormalizationArtifactReuse reuse = ImageIntegration::checkNormalizationArtifact(
                artifactPath, group.groupKey, inputsFingerprint, settingsFingerprint);
            if (reuse.newerSchema && !manifest.reprocessExisting)
            {
                return StageResult::failure(IntegrationError(
                    IntegrationErrorCode::InvalidConfiguration,
                    QStringLiteral("The normalization coefficients were written by a newer version and are not "
                                   "replaced"),
                    artifactPath, QStringLiteral("%1 Reprocess the group to replace them.").arg(reuse.reason)));
            }
            if (reuse.reuse && !manifest.reprocessExisting)
            {
                reporter.logger().operationDetail(QStringLiteral("Reusing normalization coefficients"), reuse.reason);
                report(1.0f, QStringLiteral("Reusing normalization coefficients"), artifactPath);
                return StageResult::success();
            }
            if (QFileInfo::exists(artifactPath))
            {
                reporter.logger().operationDetail(
                    QStringLiteral("Recomputing normalization coefficients"),
                    manifest.reprocessExisting ? QStringLiteral("Reprocess existing enabled.") : reuse.reason);
            }

            // One admitted operation measures the group.
            an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
            if (!scope.hasValue())
            {
                return cancelledStatus(scope.status())
                           ? StageResult::cancelled()
                           : StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                                 scope.status(), IntegrationErrorCode::NormalizationFailed,
                                 QStringLiteral("Normalization could not start"), groupLabel));
            }
            FrameReader reader(*services, plan, masters);
            const FrameProgress progress = [&](const int done, const int total, const QString& operation,
                                               const QString& file) {
                report(total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 1.0f, operation, file);
            };

            NormalizationCoefficientSet set;
            set.plane = plan.plane;
            set.drizzlePlane = plan.drizzlePlane;
            set.referenceIndex = static_cast<std::uint32_t>(settings.primaryReference);
            for (const MeasuredFrame& frame : plan.frames)
            {
                set.logicalFrames.push_back(frame.logical.canonicalPath());
                set.sourceFrames.push_back(frame.path);
            }

            an::Status measured;
            if (useLocal)
            {
                set.mode = NormalizationCoefficientMode::LocalThenGlobal;
                const ImageIntegration::LocalNormalizationAnalysis analysis =
                    ImageIntegration::localNormalizationAnalysisFor(group.stacking.localNormalization);
                // Frames a person named are the reference; otherwise the analysis ranks the lights for one.
                const bool namedReferences =
                    analysis.strategy == ImageIntegration::LocalNormReferenceStrategy::ExplicitReference ||
                    (group.stacking.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
                     !group.stacking.manualNormalizationReference.trimmed().isEmpty());
                an::Result<LocalMeasurement> local = measureLocal(
                    *scope, reader, plan, settings,
                    ImageIntegration::localNormalizationRequestFor(group.stacking.localNormalization), analysis,
                    namedReferences, group.stacking.localNormalization.localScaleEnabled, progress);
                if (local.hasValue())
                {
                    set.referenceIndex = static_cast<std::uint32_t>(local->analysisReference);
                    set.local = std::move(local->document);
                }
                measured = local.status();
            }
            else
            {
                set.mode = NormalizationCoefficientMode::Global;
                std::optional<an::Result<std::vector<an::FrameNormalization>>> matches;
                if (useOverlap && plan.plane != NormalizationMeasurementPlane::Frames)
                {
                    matches =
                        measureOverlap(*scope, reader, plan, settings, group.stacking.overlapNormalization, progress);
                    if (!matches->hasValue() && !cancelledStatus(matches->status()))
                    {
                        reporter.logger().warning(
                            QStringLiteral("Overlap normalization fell back to standard global normalization: %1")
                                .arg(ImageIntegration::engineFailureText(matches->status())));
                        matches.reset();
                    }
                }
                else if (useOverlap)
                {
                    reporter.logger().warning(QStringLiteral(
                        "Overlap normalization needs registered lights; standard global normalization is used."));
                }
                if (!matches.has_value())
                {
                    matches = measureGlobal(*scope, reader, plan, settings, progress);
                }
                if (matches->hasValue())
                {
                    set.global = std::move(**matches);
                }
                measured = matches->status();
            }
            if (measured.failed())
            {
                if (cancelledStatus(measured) || token.isCancellationRequested())
                {
                    return StageResult::cancelled();
                }
                return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                    measured, IntegrationErrorCode::NormalizationFailed,
                    QStringLiteral("Failed to compute normalization coefficients"), groupLabel));
            }
            set.width = reader.width();
            set.height = reader.height();
            set.channelCount = reader.channels();
            if (token.isCancellationRequested())
            {
                return StageResult::cancelled();
            }
            if (!frameFileStampsMatchCurrent(stamps))
            {
                return StageResult::failure(IntegrationError(
                    IntegrationErrorCode::InvalidConfiguration,
                    QStringLiteral("A normalization input changed while coefficients were computed"), {}, groupLabel));
            }

            QJsonObject sidecar = ImageIntegration::normalizationSidecar(group.groupKey, inputsFingerprint,
                                                                         settingsFingerprint, QString(), set);
            if (!plan.cometProductsFingerprint.isEmpty())
            {
                sidecar.insert(QStringLiteral("cometProductsFingerprint"), plan.cometProductsFingerprint);
            }
            if (plan.registrationSettingsVersion.has_value())
            {
                sidecar.insert(QStringLiteral("registrationSettingsVersion"), *plan.registrationSettingsVersion);
            }
            if (!plan.geometryFingerprint.isEmpty())
            {
                sidecar.insert(QStringLiteral("planeGeometryFingerprint"), plan.geometryFingerprint);
            }
            if (plan.registeredOutputsOnDisk && set.mode == NormalizationCoefficientMode::Global && !useOverlap &&
                plan.cometProductsFingerprint.isEmpty())
            {
                // What the stack's transform-on-read route checks before it reuses these matches for lights it warps
                // as it reads them.
                QJsonArray logical;
                QJsonArray registrationInputs;
                const auto* registration = findRegistrationGroup(manifest, group.registrationGroupKey);
                for (const MeasuredFrame& frame : plan.frames)
                {
                    logical.append(frame.logical.canonicalPath());
                    registrationInputs.append(registrationInputPathForFrame(
                        manifest.outputDirectory, manifest.calibrateLightsInMemory, registration, frame.logical));
                }
                QJsonArray references;
                for (const FrameId& reference : referenceFrames)
                {
                    references.append(reference.canonicalPath());
                }
                sidecar.insert(QStringLiteral("coefficientProvenance"), QStringLiteral("standard_global"));
                sidecar.insert(QStringLiteral("coefficientFramePaths"), logical);
                sidecar.insert(QStringLiteral("coefficientRegistrationInputPaths"), registrationInputs);
                sidecar.insert(QStringLiteral("normalizationReferenceFramePaths"), references);
                if (registration != nullptr)
                {
                    sidecar.insert(QStringLiteral("registrationSettingsFingerprint"),
                                   computeRegistrationSettingsHash(registration->settings));
                }
            }

            const ImageIntegration::DocumentPublicationResult published =
                ImageIntegration::publishNormalizationArtifact(artifactPath, set, identity, sidecar,
                                                               services->stopToken());
            if (!published.ok())
            {
                if (published.failure == ImageIntegration::DocumentPublicationFailure::Cancelled)
                {
                    return StageResult::cancelled();
                }
                return StageResult::failure(IntegrationError(
                    published.failure == ImageIntegration::DocumentPublicationFailure::DiskFull
                        ? IntegrationErrorCode::DiskFull
                        : IntegrationErrorCode::FileNotWritable,
                    QStringLiteral("Failed to publish normalization coefficients"), artifactPath, published.message));
            }
            report(1.0f, QStringLiteral("Normalization coefficients written"), artifactPath);
            return StageResult::success();
        };

        const StageResult groupResult = processGroup();
        if (groupResult.isCancelled())
        {
            return StageResult::cancelled();
        }
        if (groupResult.isFailure())
        {
            if (abortStage)
            {
                reporter.logger().warning(
                    QStringLiteral("Group '%1' failed: %2").arg(groupLabel, groupResult.error.toUserString()));
                reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTimer.elapsed());
                return groupResult;
            }
            errorReport.addError(groupResult.error);
            reporter.logger().warning(QStringLiteral("Group '%1' failed: %2 - continuing with remaining groups.")
                                          .arg(groupLabel, groupResult.error.toUserString()));
        }
        reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTimer.elapsed());
    }

    // What the calibrations of the lights read in memory fell back to, once per calibration.
    QStringList calibrationWarnings;
    stage_utils::sayStageWarnings(reporter, masters.fallbackWarnings(), calibrationWarnings);
    reporter.logger().stageCompleted(name(), totalGroups, stageTimer.elapsed());
    StageResult outcome =
        errorReport.errors.empty() ? StageResult::success() : StageResult::partialSuccess(errorReport);
    outcome.warnings.append(calibrationWarnings);
    return outcome;
}

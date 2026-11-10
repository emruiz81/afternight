// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/core/StackWeightingInputs.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>

#include <AfterNightPlatform/Logging.h>

#include <QFileInfo>

#include <algorithm>
#include <utility>

/**
 * @file FrameFacts.cpp
 * @brief Each master's lights: what the registration recorded of them and how the stack weighed them.
 */

namespace ImageIntegration::Reporting
{
/// Whether the report shows a weighting's weights against the group's floor: the star-count and width weights, which
/// the stack raises to the floor on the scale the report shows them, relative to the heaviest frame. A noise or image
/// count weight takes no floor, and the photometric model's floor is on its raw scale, not the one it is shown on.
[[nodiscard]] bool showsWeightFloor(const QString& weighting)
{
    const ImageIntegration::PersistedChoiceValue value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::Weighting, weighting);
    const std::optional<an::StackWeighting> source =
        value.known ? ImageIntegration::stackWeightingForKey(value.key) : std::nullopt;
    return source.has_value() && an::stackWeightingUsesMinimumWeight(*source);
}

[[nodiscard]] std::optional<double> medianOf(QList<double> values)
{
    if (values.isEmpty())
    {
        return std::nullopt;
    }

    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    if ((values.size() % 2) == 0)
    {
        return (values[middle - 1] + values[middle]) * 0.5;
    }
    return values[middle];
}

[[nodiscard]] double resolveLightExposureSeconds(const WorkflowManifest::IntegrationGroup& group, int index)
{
    if (index >= 0 && index < static_cast<int>(group.lightExposureSeconds.size()))
    {
        const double embeddedExposure = group.lightExposureSeconds[static_cast<size_t>(index)];
        if (embeddedExposure > 0.0 && std::isfinite(embeddedExposure))
        {
            return embeddedExposure;
        }
    }

    if (index >= 0 && index < static_cast<int>(group.lightFiles.size()))
    {
        const auto fromName = extractExposureFromFilename(group.lightFiles[static_cast<size_t>(index)].canonicalPath());
        if (fromName.has_value() && *fromName > 0.0)
        {
            return *fromName;
        }
    }

    const auto groupExposure = parseGroupExposure(group);
    if (groupExposure.has_value() && *groupExposure > 0.0)
    {
        return *groupExposure;
    }

    return 0.0;
}

/// The weight the stack gave each sample: the engine's own weighting over the measurements the stacking stage builds
/// from the registration's records (`deriveStackWeights`), under the group's settings as its stack reads them and
/// against the group's reference, derived in the session the builder is given. The photometric weights are shown at the
/// engine's mean of one, every other weighting's relative to the heaviest frame - the scale the group's floor is stated
/// on. Every frame weighs one where the stack weighs them equally - no weighting, a figure some frame lacks - and
/// without a session.
///
/// @param session the session the weights are derived and the headers read in, or null.
/// @param samples the group's measured frames.
/// @param group the group.
/// @return one weight per sample.
[[nodiscard]] QList<double> computeWeightsForSamples(const an::consumer::EngineSession* session,
                                                     const QList<QualityWeightSample>& samples,
                                                     const WorkflowManifest::IntegrationGroup& group)
{
    QList<double> weights(samples.size(), 1.0);
    const StackingSettings sanitized = ImageIntegration::sanitizeStackingSettings(group.stacking);
    const an::Result<ImageIntegration::IntegrationStackChoice> choice =
        ImageIntegration::integrationStackChoice(sanitized);
    if (session == nullptr || samples.isEmpty() || !choice.hasValue() ||
        choice->weighting == an::StackWeighting::Uniform)
    {
        return weights;
    }

    const an::StackWeighting source = choice->weighting;
    // One operation reads every header an image count needs and derives the weights.
    an::Result<an::consumer::OperationScope> scope = session->admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        return weights;
    }
    std::vector<FrameId> frames;
    std::vector<an::StackFrameMeasurement> measurements;
    frames.reserve(static_cast<std::size_t>(samples.size()));
    measurements.reserve(static_cast<std::size_t>(samples.size()));
    for (const auto& sample : samples)
    {
        frames.emplace_back(sample.frameId);
        an::StackFrameMeasurement measurement =
            ImageIntegration::stackFrameMeasurementFor(sample.metrics, sample.noise);
        if (source == an::StackWeighting::ImageCount)
        {
            // How many frames the light's header says it combines, as the stack reads it; one where it cannot be read.
            const std::string utf8Path = sample.frameId.toUtf8().toStdString();
            const an::Result<an::CodecImageInfo> probed = an::probeImage(
                scope->engine(), scope->execution(), an::CodecReadRequest{.path = utf8Path, .validatePayload = false});
            measurement.integratedFrameCount =
                probed.hasValue() ? ImageIntegration::combinedFrameCount(probed->metadata.codec().cards()) : 1u;
        }
        else if (!ImageIntegration::carriesWeightingFigure(source, measurement))
        {
            // The stack weighs a set by a figure only when every frame carries it.
            return weights;
        }
        measurements.push_back(std::move(measurement));
    }
    const std::optional<FrameId> reference = group.normalizationReferenceFrames.empty()
                                                 ? std::nullopt
                                                 : std::optional<FrameId>(group.normalizationReferenceFrames.front());
    const an::Result<ImageIntegration::DerivedStackWeights> derived = ImageIntegration::deriveStackWeights(
        scope->execution(), ImageIntegration::stackWeightingRequestFor(sanitized, source, choice->starCountScaling),
        measurements, ImageIntegration::stackWeightingReferenceIndex(frames, reference));
    if (!derived.hasValue())
    {
        LOG_WARNING("WorkflowReportBuilder: frame weights not derived: {}", derived.status().message());
        return weights;
    }
    const double heaviest = *std::max_element(derived->weights.begin(), derived->weights.end());
    const bool relative = source != an::StackWeighting::Photometric && heaviest > 0.0;
    for (qsizetype i = 0; i < weights.size(); ++i)
    {
        const double weight = derived->weights[static_cast<std::size_t>(i)];
        weights[i] = relative ? weight / heaviest : weight;
    }
    return weights;
}

/// The weight the stack recorded combining each sample with, on the scale `computeWeightsForSamples` shows: relative
/// to the heaviest light, the photometric model's at its mean of one. A light the master does not combine - one a
/// spatial re-stack left out - weighs nothing, and the others are as the stack weighed them among the lights it kept.
///
/// @param samples the group's measured frames.
/// @param quality the record of the group's stack, which holds its lights' weights.
/// @param group the group.
/// @return one weight per sample.
[[nodiscard]] QList<double> recordedWeightsForSamples(const QList<QualityWeightSample>& samples,
                                                      const StackQualityRecord& quality,
                                                      const WorkflowManifest::IntegrationGroup& group)
{
    QHash<QString, double> byLight;
    byLight.reserve(static_cast<qsizetype>(quality.lightWeights.size()));
    double heaviest = 0.0;
    for (const StackLightWeight& entry : quality.lightWeights)
    {
        byLight.insert(FrameId(entry.light).identityKey(), entry.weight);
        heaviest = (std::max)(heaviest, entry.weight);
    }
    const an::Result<ImageIntegration::IntegrationStackChoice> choice =
        ImageIntegration::integrationStackChoice(ImageIntegration::sanitizeStackingSettings(group.stacking));
    const bool relative =
        !(choice.hasValue() && choice->weighting == an::StackWeighting::Photometric) && heaviest > 0.0;
    QList<double> weights;
    weights.reserve(samples.size());
    for (const QualityWeightSample& sample : samples)
    {
        const double weight = byLight.value(FrameId(sample.frameId).identityKey(), 0.0);
        weights.append(relative ? weight / heaviest : weight);
    }
    return weights;
}

namespace
{
/// A figure the registration stores as zero when it has none.
[[nodiscard]] std::optional<double> positive(const double value)
{
    return std::isfinite(value) && value > 0.0 ? std::optional<double>(value) : std::nullopt;
}

/// One light of a group, before its registration record is read: its file and its exposure.
[[nodiscard]] ReportFrame frameOf(const WorkflowManifest::IntegrationGroup& group, const int index)
{
    ReportFrame frame;
    frame.path = group.lightFiles[static_cast<std::size_t>(index)].canonicalPath();
    frame.fileName = QFileInfo(frame.path).fileName();
    if (frame.fileName.isEmpty())
    {
        frame.fileName = frame.path;
    }
    frame.exposureSeconds = positive(resolveLightExposureSeconds(group, index));
    return frame;
}

void stateMetrics(const RegistrationMetricRow& row, ReportFrame& frame)
{
    frame.registered = row.succeeded;
    frame.stars = row.stars;
    frame.fwhm = positive(row.fwhm);
    frame.snr = positive(row.snr);
    frame.eccentricity = std::isfinite(row.eccentricity) ? std::optional<double>(row.eccentricity) : std::nullopt;
    if (std::isfinite(row.photometricZeroPoint) && std::abs(row.photometricZeroPoint) > 1.0e-12)
    {
        frame.zeroPoint = row.photometricZeroPoint;
        frame.zeroPointScatter = positive(row.zeroPointScatter);
    }
    frame.astrometricRmsPixels = positive(row.astrometricRmsPixels);
    frame.astrometricRmsArcsec = positive(row.astrometricRmsArcsec);
    frame.limitingMagnitude = positive(row.limitingMagnitude);
    if (row.catalogMatchedStars > 0)
    {
        frame.catalogMatches = row.catalogMatchedStars;
    }
    frame.transformKind = row.transformKind;
}

/// The lights of one group, with what its registration recorded of each and, for the ones it registered, the weight
/// the stack gave them, their quality score and their share of the stack.
void fillGroup(ReportBuild& build, const WorkflowManifest::IntegrationGroup& group,
               const IntegrationGroupResult* groupResult, ReportMaster& master)
{
    const int lights = static_cast<int>(group.lightFiles.size());
    master.frames.reserve(static_cast<std::size_t>(lights));
    for (int i = 0; i < lights; ++i)
    {
        master.frames.push_back(frameOf(group, i));
    }

    const QString registrationKey = group.registrationGroupKey.toString().trimmed();
    if (registrationKey.isEmpty() || !build.hasMetrics())
    {
        return;
    }
    const std::optional<int> settingsVersion = build.settingsVersion(registrationKey);
    const QHash<QString, RegistrationMetricRow>& metricsByFrame = build.metrics(registrationKey);
    if (!settingsVersion.has_value() || metricsByFrame.isEmpty())
    {
        return;
    }

    // The weighting the group's stack applied: none beside a combination that takes no weights or a merge.
    const StackingSettings applied = ImageIntegration::sanitizeStackingSettings(group.stacking);
    ReportMasterWeighting weighting;
    weighting.mode = applied.weighting.trimmed().isEmpty() ? QStringLiteral("None") : applied.weighting.trimmed();
    weighting.minimumWeight = std::clamp(group.stacking.minWeightFloor, 0.0, 1.0);
    weighting.floorApplies = showsWeightFloor(weighting.mode);

    const an::Result<ImageIntegration::IntegrationStackChoice> choice =
        ImageIntegration::integrationStackChoice(applied);
    const QHash<QString, FrameNoiseStatistics> noiseByFrame =
        choice.hasValue() && choice->weighting == an::StackWeighting::Noise
            ? build.sources().metrics->noiseByFrame(registrationKey, *settingsVersion)
            : QHash<QString, FrameNoiseStatistics>{};

    QList<QualityWeightSample> samples;
    QList<int> sampleFrames;
    samples.reserve(lights);
    sampleFrames.reserve(lights);
    for (int i = 0; i < lights; ++i)
    {
        ReportFrame& frame = master.frames[static_cast<std::size_t>(i)];
        const auto metric = metricsByFrame.constFind(frame.path);
        if (metric == metricsByFrame.cend())
        {
            continue;
        }
        ++weighting.framesWithMetrics;
        stateMetrics(*metric, frame);
        if (noiseByFrame.contains(frame.path))
        {
            frame.backgroundNoise = positive(noiseByFrame.value(frame.path).background_noise);
        }
        if (!metric->succeeded)
        {
            continue;
        }

        ++weighting.framesRegistered;
        QualityWeightSample sample;
        sample.frameId = frame.path;
        sample.exposureSeconds = resolveLightExposureSeconds(group, i);
        sample.metrics.stars = metric->stars;
        sample.metrics.fwhm = metric->fwhm;
        sample.metrics.snr = metric->snr;
        sample.metrics.eccentricity = metric->eccentricity;
        sample.metrics.photometric_zero_point = metric->photometricZeroPoint;
        sample.metrics.zero_point_scatter = metric->zeroPointScatter;
        sample.metrics.astrometric_rms_pixels = metric->astrometricRmsPixels;
        sample.metrics.astrometric_rms_arcsec = metric->astrometricRmsArcsec;
        sample.metrics.limiting_magnitude = metric->limitingMagnitude;
        sample.metrics.catalog_matched_stars = metric->catalogMatchedStars;
        sample.metrics.succeeded = true;
        sample.noise = noiseByFrame.value(frame.path);
        samples.append(std::move(sample));
        sampleFrames.append(i);
    }

    if (!samples.isEmpty())
    {
        // What the stack recorded of its own weights, where the run left a record; else what its settings imply.
        const QList<double> weights =
            groupResult != nullptr && groupResult->success && !groupResult->qualityReport.lightWeights.empty()
                ? recordedWeightsForSamples(samples, groupResult->qualityReport, group)
                : computeWeightsForSamples(build.sources().session, samples, group);
        weighting.medianWeight = medianOf(weights);

        std::vector<FrameQualityMetrics> metricsForStatistics;
        metricsForStatistics.reserve(static_cast<std::size_t>(samples.size()));
        for (const QualityWeightSample& sample : samples)
        {
            metricsForStatistics.push_back(sample.metrics);
        }
        const QString filter = group.filter.trimmed().isEmpty() ? QStringLiteral("Unfiltered") : group.filter.trimmed();
        const std::optional<FilterStatistics> filterStatistics =
            FilterStatisticsEngine::computeStatistics(filter, metricsForStatistics);

        QList<double> qualityScores;
        QList<double> zeroPoints;
        QList<double> astrometricRms;
        QList<double> catalogMatches;
        QList<double> weightedSeconds;
        weightedSeconds.reserve(samples.size());
        for (int i = 0; i < samples.size() && i < weights.size(); ++i)
        {
            const QualityWeightSample& sample = samples[i];
            ReportFrame& frame = master.frames[static_cast<std::size_t>(sampleFrames[i])];
            const double weight = weights[i];
            const double seconds = sample.exposureSeconds > 0.0 && std::isfinite(sample.exposureSeconds)
                                       ? sample.exposureSeconds * weight
                                       : 0.0;
            weightedSeconds.append(seconds);
            weighting.weightedIntegrationMs += std::llround(seconds * 1000.0);
            if (weighting.floorApplies && weight <= weighting.minimumWeight + 1e-6)
            {
                ++weighting.framesAtFloor;
            }
            frame.weight = weight;

            const double score = FilterStatisticsEngine::computeQualityScore(sample.metrics, filterStatistics);
            if (std::isfinite(score))
            {
                frame.qualityScore = score;
                qualityScores.append(score);
            }
            if (frame.zeroPoint.has_value())
            {
                zeroPoints.append(*frame.zeroPoint);
            }
            if (frame.astrometricRmsArcsec.has_value())
            {
                astrometricRms.append(*frame.astrometricRmsArcsec);
            }
            if (frame.catalogMatches.has_value())
            {
                catalogMatches.append(static_cast<double>(*frame.catalogMatches));
            }
        }

        // The shares are of the weighted exposure itself, not of its sum rounded light by light to milliseconds, so
        // they add up to the whole.
        double totalWeightedSeconds = 0.0;
        for (const double seconds : weightedSeconds)
        {
            totalWeightedSeconds += seconds;
        }
        for (int i = 0; i < weightedSeconds.size(); ++i)
        {
            if (totalWeightedSeconds > 0.0 && weightedSeconds[i] > 0.0)
            {
                master.frames[static_cast<std::size_t>(sampleFrames[i])].stackShare =
                    weightedSeconds[i] / totalWeightedSeconds;
            }
        }
        weighting.medianQualityScore = medianOf(qualityScores);
        weighting.medianZeroPoint = medianOf(zeroPoints);
        weighting.medianAstrometricRmsArcsec = medianOf(astrometricRms);
        weighting.medianCatalogMatches = medianOf(catalogMatches);
    }
    master.weighting = std::move(weighting);
}
} // namespace

void fillFrames(ReportBuild& build, ReportModel& model)
{
    QHash<QString, ReportMaster*> masterByKey;
    for (ReportMaster& master : model.masters)
    {
        masterByKey.insert(master.key, &master);
    }
    QHash<QString, const IntegrationGroupResult*> resultByKey;
    for (const IntegrationGroupResult& groupResult : build.result().integrationGroupResults)
    {
        resultByKey.insert(groupResult.groupKey, &groupResult);
    }
    for (const auto& group : build.manifest().integrationGroups)
    {
        const QString key = group.groupKey.toString();
        if (ReportMaster* master = masterByKey.value(key, nullptr); master != nullptr)
        {
            fillGroup(build, group, resultByKey.value(key, nullptr), *master);
        }
    }
}
} // namespace ImageIntegration::Reporting

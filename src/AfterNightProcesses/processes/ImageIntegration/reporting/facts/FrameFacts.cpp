// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/core/StackWeightingInputs.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRequests.h>

#include <AfterNightPlatform/Logging.h>

#include <QFileInfo>
#include <QSet>
#include <QTimeZone>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
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

/// The weight the stack recorded combining each sample with, relative to the heaviest light, the photometric
/// model's at its mean of one. A light the master does not combine - one a
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

/// A turn from which a transform's translation says nothing of where the light sat without the frame's size.
constexpr double kTurnedDegrees = 1.0;

/// States where a light sat against its reference and how far it is turned, from its transform's matrix.
///
/// The shift is how far the transform moves the frame's centre. The matrix's own translation is where the frame's
/// corner goes: a light taken after a meridian flip is turned half a turn about its centre, so its corner lands a
/// frame's width and height away though the light sits on the reference to within its dither.
///
/// @param matrix the transform's matrix, row by row.
/// @param frameSize the light's width and height; nothing when the group states no size that reads.
/// @param frame the light.
void statePlacement(const std::array<double, 9>& matrix, const std::optional<QPair<int, int>>& frameSize,
                    ReportFrame& frame)
{
    const double rotation = qRadiansToDegrees(std::atan2(matrix[3], matrix[0]));
    if (std::isfinite(rotation))
    {
        frame.rotationDegrees = rotation;
    }
    if (!frameSize.has_value() && !(std::abs(rotation) < kTurnedDegrees))
    {
        // Without the frame's size a turned light's shift cannot be told from its turn.
        return;
    }
    const double centreX = frameSize.has_value() ? 0.5 * static_cast<double>(frameSize->first) : 0.0;
    const double centreY = frameSize.has_value() ? 0.5 * static_cast<double>(frameSize->second) : 0.0;
    double shiftX = 0.0;
    double shiftY = 0.0;
    if (matrix[6] == 0.0 && matrix[7] == 0.0 && matrix[8] == 1.0)
    {
        // Written so that a transform that turns and scales nothing states its translation exactly.
        shiftX = (matrix[0] - 1.0) * centreX + matrix[1] * centreY + matrix[2];
        shiftY = matrix[3] * centreX + (matrix[4] - 1.0) * centreY + matrix[5];
    }
    else
    {
        const double scale = matrix[6] * centreX + matrix[7] * centreY + matrix[8];
        if (!(std::abs(scale) > 1.0e-12))
        {
            return;
        }
        shiftX = (matrix[0] * centreX + matrix[1] * centreY + matrix[2]) / scale - centreX;
        shiftY = (matrix[3] * centreX + matrix[4] * centreY + matrix[5]) / scale - centreY;
    }
    if (std::isfinite(shiftX) && std::isfinite(shiftY))
    {
        frame.shiftX = shiftX;
        frame.shiftY = shiftY;
    }
}

void stateMetrics(const RegistrationMetricRow& row, const std::optional<QPair<int, int>>& frameSize, ReportFrame& frame)
{
    frame.registered = row.succeeded;
    frame.stars = row.stars;
    frame.fwhm = positive(row.fwhm);
    frame.snr = positive(row.snr);
    frame.eccentricity = std::isfinite(row.eccentricity) ? std::optional<double>(row.eccentricity) : std::nullopt;
    frame.backgroundNoise = positive(row.backgroundNoise);
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
    if (row.succeeded && row.transformMatrix.has_value())
    {
        statePlacement(*row.transformMatrix, frameSize, frame);
    }
}

/// When each light of the run's registration groups was taken, by the light's canonical path, as the manifest states
/// it; a light with no time that reads is left out.
[[nodiscard]] QHash<QString, QDateTime> captureTimes(const WorkflowManifest& manifest)
{
    QHash<QString, QDateTime> times;
    for (const auto& group : manifest.registrationGroups)
    {
        const std::size_t stated = (std::min)(group.lightFiles.size(), group.lightCaptureTimestampsUtc.size());
        for (std::size_t i = 0; i < stated; ++i)
        {
            const QString text = group.lightCaptureTimestampsUtc[i].trimmed();
            if (text.isEmpty())
            {
                continue;
            }
            QDateTime captured = QDateTime::fromString(text, Qt::ISODateWithMs);
            if (!captured.isValid())
            {
                captured = QDateTime::fromString(text, Qt::ISODate);
            }
            if (!captured.isValid())
            {
                continue;
            }
            if (captured.timeSpec() == Qt::LocalTime)
            {
                // The manifest's times are UTC; one written without a zone is read as such.
                captured = QDateTime(captured.date(), captured.time(), QTimeZone::UTC);
            }
            times.insert(group.lightFiles[i].canonicalPath(), captured);
        }
    }
    return times;
}

/// What a light's header states - its gain, its offset, its sensor's temperature - and, for a light the manifest
/// states no time for, when it was taken. The temperature falls back to the one the file's name states.
void stateHeader(ReportBuild& build, ReportFrame& frame)
{
    if (const std::optional<ReportHeader> header = build.header(frame.path); header.has_value())
    {
        const an::AcquisitionProjection projected = an::projectAcquisition(header->metadata.codec());
        frame.gain = formatIsoGainLabel(header->metadata);
        frame.offset = formatOffsetLabel(header->metadata);
        frame.temperature = projected.acquisition.sensorTemperatureCelsius;
        if (!frame.capturedAt.has_value() && projected.acquisition.exposureStart.has_value())
        {
            frame.capturedAt = QDateTime::fromMSecsSinceEpoch(
                projected.acquisition.exposureStart->nanosecondsSinceEpoch / 1'000'000, QTimeZone::UTC);
        }
    }
    if (!frame.temperature.has_value())
    {
        frame.temperature = extractSensorTemperature(frame.path);
    }
}

/// States a light's capture time in the archive's zone and the night it belongs to: the local date twelve hours
/// before the capture, so the hours after midnight belong to the evening they follow.
void stateNight(const int utcOffsetSeconds, ReportFrame& frame)
{
    if (!frame.capturedAt.has_value())
    {
        return;
    }
    const QDateTime local = frame.capturedAt->toTimeZone(QTimeZone::fromSecondsAheadOfUtc(utcOffsetSeconds));
    frame.capturedAt = local;
    frame.night = local.addSecs(-12 * 3600).date();
}

/// The share of a light's samples a stack rejected, as a reader meets it.
[[nodiscard]] QString percentText(const double fraction)
{
    return QStringLiteral("%1 %").arg(formatFigure(fraction * 100.0));
}

/// Why a stack left a light out on its own, as a sentence of the light.
[[nodiscard]] QString droppedLightDetail(const DroppedLightReason reason)
{
    switch (reason)
    {
    case DroppedLightReason::NoRegistrationTransform:
        return reportText("The stack left this light out: its registration recorded no transform.");
    case DroppedLightReason::NoStarBaseProduct:
        return reportText("The stack left this light out: it has no star base image to stack.");
    case DroppedLightReason::NoCometAlignedImage:
        return reportText("The stack left this light out: it has no comet-aligned image to stack.");
    case DroppedLightReason::NoRegisteredOutput:
        break;
    }
    return reportText("The stack left this light out: it has no registered image to stack.");
}

/// What became of each light of a master the run stacked, by the precedence a light's states have: its registration
/// failed; the stack left it out on its own, which the group's result names with the reason; a second stack was made
/// without it; the stack combined it. A light is combined when the stack's record of its weights names it, and left
/// out when the result's dropped lights name it, and by nothing else: a light none of the run's records names has no
/// state, and the model says so.
void stateFatesOfStack(ReportBuild& build, const IntegrationGroupResult& groupResult, ReportMaster& master)
{
    QHash<QString, DroppedLightReason> dropped;
    for (const DroppedLight& light : groupResult.droppedLights)
    {
        dropped.insert(FrameId(light.light).identityKey(), light.reason);
    }

    const StackQualityRecord& quality = groupResult.qualityReport;
    if (!quality.valid || quality.lightWeights.empty())
    {
        for (ReportFrame& frame : master.frames)
        {
            if (frame.registered.has_value() && !*frame.registered)
            {
                frame.state = ReportFrameState::RegistrationFailed;
                frame.stateDetail =
                    reportText("Registration failed; %1 stars were detected.").arg(frame.stars.value_or(0));
            }
            else if (const auto reason = dropped.constFind(FrameId(frame.path).identityKey()); reason != dropped.cend())
            {
                frame.state = ReportFrameState::LeftOutByStack;
                frame.stateDetail = droppedLightDetail(*reason);
            }
        }
        if (!master.frames.empty())
        {
            build.notStated(reportText("What became of each light of %1").arg(master.name),
                            reportText("The stack recorded no weights of its lights, which is what names the lights "
                                       "it combined."));
        }
        return;
    }

    QSet<QString> combined;
    QHash<QString, std::size_t> placeInStack;
    for (std::size_t i = 0; i < quality.lightWeights.size(); ++i)
    {
        const QString identity = FrameId(quality.lightWeights[i].light).identityKey();
        combined.insert(identity);
        placeInStack.insert(identity, i);
    }
    // The places of a restack's exclusions are among the group's lights, which the frames follow.
    QHash<std::size_t, std::size_t> excludedOnRestack;
    if (quality.spatialRestackApplied)
    {
        for (std::size_t i = 0; i < quality.spatialRestackExcludedFrameIndices.size(); ++i)
        {
            excludedOnRestack.insert(static_cast<std::size_t>(quality.spatialRestackExcludedFrameIndices[i]), i);
        }
    }

    // A rate belongs to the light the stack's weights name at the same place, never to a place among the group's
    // lights: the stack reads fewer lights than the group lists, in its own order.
    const bool ratesBelong = quality.frameRejectionRates.size() == quality.lightWeights.size();
    if (!quality.frameRejectionRates.empty() && !ratesBelong)
    {
        build.notStated(reportText("The share of each light's samples the stack of %1 rejected").arg(master.name),
                        reportText("The stack recorded %1 rates and the weights of %2 lights, so no rate can be given "
                                   "to a light.")
                            .arg(quality.frameRejectionRates.size())
                            .arg(quality.lightWeights.size()));
    }

    int used = 0;
    int unnamed = 0;
    for (std::size_t i = 0; i < master.frames.size(); ++i)
    {
        ReportFrame& frame = master.frames[i];
        const QString identity = FrameId(frame.path).identityKey();
        if (frame.registered.has_value() && !*frame.registered)
        {
            frame.state = ReportFrameState::RegistrationFailed;
            frame.stateDetail = reportText("Registration failed; %1 stars were detected.").arg(frame.stars.value_or(0));
        }
        else if (const auto reason = dropped.constFind(identity); reason != dropped.cend())
        {
            frame.state = ReportFrameState::LeftOutByStack;
            frame.stateDetail = droppedLightDetail(*reason);
        }
        else if (combined.contains(identity))
        {
            frame.state = ReportFrameState::Used;
            if (ratesBelong)
            {
                frame.rejectedFraction = quality.frameRejectionRates[placeInStack.value(identity)];
            }
        }
        else if (const auto excluded = excludedOnRestack.constFind(i); excluded != excludedOnRestack.cend())
        {
            frame.state = ReportFrameState::ExcludedOnRestack;
            if (*excluded < quality.spatialRestackExcludedFrameRates.size())
            {
                frame.rejectedFraction = quality.spatialRestackExcludedFrameRates[*excluded];
                frame.stateDetail = reportText("The first stack rejected %1 of the light's samples, so the master "
                                               "was stacked again without it.")
                                        .arg(percentText(*frame.rejectedFraction));
            }
            else
            {
                frame.stateDetail = reportText("The master was stacked again without this light.");
            }
        }
        else
        {
            // None of the run's records names the light: nothing is inferred of it.
            ++unnamed;
        }

        if (frame.state == ReportFrameState::Used)
        {
            ++used;
        }
        else
        {
            // The stack recorded no weight for a light the master does not combine.
            frame.weight.reset();
            frame.stackShare.reset();
        }
    }
    if (unnamed > 0)
    {
        build.notStated(reportText("What became of %1 of the lights of %2").arg(unnamed).arg(master.name),
                        reportText("The stack's weights do not name them, and the run recorded no reason for leaving "
                                   "them out."));
    }

    if (used != groupResult.lightFrameCount)
    {
        build.notStated(reportText("Which lights the master %1 combines").arg(master.name),
                        reportText("The run counted %1 lights in the stack and its records name %2.")
                            .arg(groupResult.lightFrameCount)
                            .arg(used));
    }
}

/// What became of each light of a master the run did not stack: a light whose registration failed says so; the
/// others were not stacked, for the reason the master states.
void stateFatesWithoutStack(ReportMaster& master)
{
    for (ReportFrame& frame : master.frames)
    {
        if (frame.registered.has_value() && !*frame.registered)
        {
            frame.state = ReportFrameState::RegistrationFailed;
            frame.stateDetail = reportText("Registration failed; %1 stars were detected.").arg(frame.stars.value_or(0));
        }
        else if (master.state != ReportMasterState::Reused)
        {
            frame.state = ReportFrameState::NotStacked;
            frame.stateDetail = master.failure;
        }
        if (master.state != ReportMasterState::Reused)
        {
            // No stack weighed the lights of a master that was not made.
            frame.weight.reset();
            frame.stackShare.reset();
        }
    }
}

/// What the run that made a reused master stated of its lights' part in the stack, light by light: read from the
/// stored report the master's own figures were taken from, which is that run's or the one of a later run that
/// carried them.
void stateFatesOfReused(ReportBuild& build, ReportMaster& master)
{
    if (master.madeByRun.isEmpty())
    {
        return;
    }
    const ReusedMasterStatement statement = statementOfReusedMaster(build, master);
    const ReportMaster* made = statement.master;
    if (made == nullptr)
    {
        return;
    }
    QHash<QString, const ReportFrame*> byPath;
    for (const ReportFrame& frame : made->frames)
    {
        byPath.insert(frame.path, &frame);
    }
    for (ReportFrame& frame : master.frames)
    {
        const ReportFrame* stated = byPath.value(frame.path, nullptr);
        if (stated == nullptr)
        {
            continue;
        }
        frame.state = stated->state;
        frame.stateDetail = stated->stateDetail;
        frame.weight = stated->weight;
        frame.stackShare = stated->stackShare;
        frame.rejectedFraction = stated->rejectedFraction;
        frame.normalizationScale = stated->normalizationScale;
        frame.normalizationOffset = stated->normalizationOffset;
    }
    master.weighting = made->weighting;
}

/// The scale and the offset the normalization gave each light per channel, where the group's coefficients are one
/// match per light. A local normalization gives each light a surface, which is not a figure of the light.
void stateNormalization(ReportBuild& build, const WorkflowManifest::IntegrationGroup& group, ReportMaster& master)
{
    const QString artifact = group.normalizationCoefficientsArtifactPath.value_or(QString()).trimmed();
    if (artifact.isEmpty() || master.state != ReportMasterState::Made)
    {
        return;
    }
    const QString what = reportText("The normalization's scale and offset of the lights of %1").arg(master.name);
    if (!build.exists(artifact))
    {
        return;
    }
    const ImageIntegration::NormalizationDocumentRead read =
        ImageIntegration::readNormalizationCoefficients(artifact, std::nullopt);
    if (!read.coefficients.has_value())
    {
        build.notStated(what, reportText("The coefficients could not be read: %1").arg(read.message));
        return;
    }
    const ImageIntegration::NormalizationCoefficientSet& set = *read.coefficients;
    if (set.global.size() != set.logicalFrames.size())
    {
        return;
    }
    QHash<QString, std::size_t> placeByLight;
    for (std::size_t i = 0; i < set.logicalFrames.size(); ++i)
    {
        // A frame the artifact names by nothing is no light's coefficients.
        if (!set.logicalFrames[i].trimmed().isEmpty())
        {
            placeByLight.insert(FrameId(set.logicalFrames[i]).identityKey(), i);
        }
    }
    for (ReportFrame& frame : master.frames)
    {
        const auto place = placeByLight.constFind(FrameId(frame.path).identityKey());
        if (place == placeByLight.cend())
        {
            continue;
        }
        const an::FrameNormalization& match = set.global[*place];
        const std::uint32_t channels = (std::min)(match.channelCount, an::kMaxNormalizationChannels);
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            frame.normalizationScale.push_back(match.channels[channel].scale);
            frame.normalizationOffset.push_back(match.channels[channel].offset);
        }
    }
}

/// The photometry the registration recorded of the lights a master combines; of its registered lights, for a master
/// whose lights' fates are not stated.
void statePhotometry(ReportMaster& master)
{
    ReportMasterPhotometry photometry;
    QList<double> zeroPoints;
    QList<double> scatters;
    QList<double> limits;
    QList<double> matches;
    for (const ReportFrame& frame : master.frames)
    {
        const bool counts =
            frame.state.has_value() ? *frame.state == ReportFrameState::Used : frame.registered.value_or(false);
        if (!counts)
        {
            continue;
        }
        if (frame.zeroPoint.has_value())
        {
            ++photometry.frames;
            zeroPoints.append(*frame.zeroPoint);
        }
        if (frame.zeroPointScatter.has_value())
        {
            scatters.append(*frame.zeroPointScatter);
        }
        if (frame.limitingMagnitude.has_value())
        {
            limits.append(*frame.limitingMagnitude);
        }
        if (frame.catalogMatches.has_value())
        {
            matches.append(static_cast<double>(*frame.catalogMatches));
        }
    }
    if (photometry.frames == 0 && limits.isEmpty() && matches.isEmpty())
    {
        return;
    }
    photometry.medianZeroPoint = medianOf(zeroPoints);
    photometry.medianZeroPointScatter = medianOf(scatters);
    photometry.medianLimitingMagnitude = medianOf(limits);
    photometry.medianCatalogMatches = medianOf(matches);
    master.photometry = std::move(photometry);
}

/// Why each light whose registration failed has no transform, in the registration's own words: what this run's
/// registration recorded of it, else what the latest earlier run of the folder whose report is kept states of the
/// same light. A run registers a light once: a later run that finds the registration still valid records nothing
/// of it again, and the metrics database keeps only that it failed.
void stateRegistrationReasons(ReportBuild& build, ReportMaster& master)
{
    QHash<QString, QString> recorded;
    for (const RegistrationFailure& failure : build.result().registrationFailures)
    {
        recorded.insert(failure.light, failure.reason.trimmed());
    }
    int unexplained = 0;
    for (ReportFrame& frame : master.frames)
    {
        if (frame.state != ReportFrameState::RegistrationFailed)
        {
            continue;
        }
        QString reason = recorded.value(frame.path);
        if (reason.isEmpty())
        {
            for (const StoredRun& run : build.earlierRuns())
            {
                for (const ReportMaster& earlier : run.model.masters)
                {
                    const auto same = std::find_if(
                        earlier.frames.begin(), earlier.frames.end(), [&frame](const ReportFrame& candidate) {
                            return candidate.path == frame.path && !candidate.registrationFailure.isEmpty();
                        });
                    if (same != earlier.frames.end())
                    {
                        reason = same->registrationFailure;
                        break;
                    }
                }
                if (!reason.isEmpty())
                {
                    break;
                }
            }
        }
        if (reason.isEmpty())
        {
            ++unexplained;
            continue;
        }
        frame.registrationFailure = reason;
        frame.stateDetail =
            reportText("Registration failed; %1 stars were detected. %2").arg(frame.stars.value_or(0)).arg(reason);
    }
    if (unexplained > 0)
    {
        build.notStated(reportText("Why %1 of the lights of %2 failed registration").arg(unexplained).arg(master.name),
                        reportText("An earlier run registered them, and no report kept in the folder states its "
                                   "reasons."));
    }
}

/// What the registration recorded of each light of a group and, for the ones it registered, the weight the stack
/// gave them, their quality score and their share of the stack.
void fillMetrics(ReportBuild& build, const WorkflowManifest::IntegrationGroup& group,
                 const IntegrationGroupResult* groupResult, ReportMaster& master)
{
    const int lights = static_cast<int>(group.lightFiles.size());
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

    // The size of the group's lights, which says where the centre of one is.
    const std::optional<QPair<int, int>> frameSize = parseResolutionDimensions(group.resolution);

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
        stateMetrics(*metric, frameSize, frame);
        if (!frame.backgroundNoise.has_value() && noiseByFrame.contains(frame.path))
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
        // The weights are the ones the stack recorded combining each light with; a stack that recorded none states
        // none.
        const bool recorded =
            groupResult != nullptr && groupResult->success && !groupResult->qualityReport.lightWeights.empty();
        const QList<double> weights =
            recorded ? recordedWeightsForSamples(samples, groupResult->qualityReport, group) : QList<double>{};
        // The lights the stack names. A measured light it does not name - one a second stack left out, one the
        // stack dropped - weighs nothing in the master's exposure and is no part of the weights' own figures: the
        // median and the count at the floor are of the lights the master combines.
        QSet<QString> combined;
        if (recorded)
        {
            for (const StackLightWeight& entry : groupResult->qualityReport.lightWeights)
            {
                combined.insert(FrameId(entry.light).identityKey());
            }
            QList<double> ofCombined;
            for (int i = 0; i < samples.size(); ++i)
            {
                if (combined.contains(FrameId(samples[i].frameId).identityKey()))
                {
                    ofCombined.append(weights[i]);
                }
            }
            weighting.medianWeight = medianOf(ofCombined);
        }

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
        for (int i = 0; i < samples.size(); ++i)
        {
            const QualityWeightSample& sample = samples[i];
            ReportFrame& frame = master.frames[static_cast<std::size_t>(sampleFrames[i])];
            const double weight = recorded ? weights[i] : 0.0;
            const bool named = recorded && combined.contains(FrameId(sample.frameId).identityKey());
            const double seconds = sample.exposureSeconds > 0.0 && std::isfinite(sample.exposureSeconds)
                                       ? sample.exposureSeconds * weight
                                       : 0.0;
            weightedSeconds.append(seconds);
            weighting.weightedIntegrationMs += std::llround(seconds * 1000.0);
            if (named && weighting.floorApplies && weight <= weighting.minimumWeight + 1e-6)
            {
                ++weighting.framesAtFloor;
            }
            if (named)
            {
                frame.weight = weight;
            }

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

/// The lights of one group: when each was taken and what its header states, what its registration recorded, how the
/// stack weighed it and what became of it.
void fillGroup(ReportBuild& build, const WorkflowManifest::IntegrationGroup& group,
               const IntegrationGroupResult* groupResult, const QHash<QString, QDateTime>& capturedByLight,
               ReportMaster& master)
{
    const int lights = static_cast<int>(group.lightFiles.size());
    const int utcOffsetSeconds = build.utcOffsetSeconds();
    master.frames.reserve(static_cast<std::size_t>(lights));
    int withoutTime = 0;
    for (int i = 0; i < lights; ++i)
    {
        ReportFrame frame = frameOf(group, i);
        if (const auto captured = capturedByLight.constFind(frame.path); captured != capturedByLight.cend())
        {
            frame.capturedAt = *captured;
        }
        stateHeader(build, frame);
        stateNight(utcOffsetSeconds, frame);
        withoutTime += frame.capturedAt.has_value() ? 0 : 1;
        master.frames.push_back(std::move(frame));
    }
    if (withoutTime > 0)
    {
        build.notStated(reportText("When %1 of the %2 lights of %3 were taken, and on which night")
                            .arg(withoutTime)
                            .arg(lights)
                            .arg(master.name),
                        reportText("Neither the run's manifest nor the lights' headers state a capture time for "
                                   "them."));
    }

    fillMetrics(build, group, groupResult, master);

    if (groupResult != nullptr && groupResult->success)
    {
        stateFatesOfStack(build, *groupResult, master);
    }
    else
    {
        stateFatesWithoutStack(master);
        stateFatesOfReused(build, master);
    }
    stateRegistrationReasons(build, master);
    stateNormalization(build, group, master);
    statePhotometry(master);
}
} // namespace

/// The lights the user left out, which the manifest lists apart since no group names them: each takes the state its
/// exclusion gives it, under the master of the group it would have joined - after that group's own lights, whose
/// places the stack's records count by - or, where the run planned no such master, under the group's key among the
/// groups not stacked. What the run knows of such a light is stated: when it was taken, what its header states and,
/// for a light left out of the stack alone, what its registration measured.
void fillLeftOut(ReportBuild& build, const QHash<QString, ReportMaster*>& masterByKey,
                 const QHash<QString, QDateTime>& capturedByLight, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    if (manifest.leftOutLights.empty())
    {
        return;
    }
    // A light left out of a stack is still in its registration group, which states its exposure.
    QHash<QString, double> exposureByLight;
    for (const auto& group : manifest.registrationGroups)
    {
        const std::size_t stated = (std::min)(group.lightFiles.size(), group.lightExposureSeconds.size());
        for (std::size_t i = 0; i < stated; ++i)
        {
            exposureByLight.insert(group.lightFiles[i].canonicalPath(), group.lightExposureSeconds[i]);
        }
    }
    QHash<QString, QString> registrationKeyOfGroup;
    QHash<QString, QString> resolutionOfGroup;
    for (const auto& group : manifest.integrationGroups)
    {
        registrationKeyOfGroup.insert(group.groupKey.toString(), group.registrationGroupKey.toString().trimmed());
        resolutionOfGroup.insert(group.groupKey.toString(), group.resolution);
    }

    const int utcOffsetSeconds = build.utcOffsetSeconds();
    for (const WorkflowManifest::LeftOutLight& leftOut : manifest.leftOutLights)
    {
        const QString key = leftOut.groupKey.toString();
        const bool fromStack = leftOut.kind == WorkflowManifest::LeftOutLight::Kind::StackExcluded;

        ReportFrame frame;
        frame.path = leftOut.light.canonicalPath();
        frame.fileName = QFileInfo(frame.path).fileName();
        if (frame.fileName.isEmpty())
        {
            frame.fileName = frame.path;
        }
        frame.state = fromStack ? ReportFrameState::StackExcludedByUser : ReportFrameState::ExcludedByUser;
        frame.stateDetail = fromStack ? reportText("Excluded by you from this master's stack.")
                                      : reportText("Excluded by you in the Lights tab.");
        if (const auto exposure = exposureByLight.constFind(frame.path); exposure != exposureByLight.cend())
        {
            frame.exposureSeconds = positive(*exposure);
        }
        if (const auto captured = capturedByLight.constFind(frame.path); captured != capturedByLight.cend())
        {
            frame.capturedAt = *captured;
        }
        stateHeader(build, frame);
        if (!frame.exposureSeconds.has_value())
        {
            // No group states the exposure of a light excluded in the lights' list: its header does.
            if (const std::optional<ReportHeader> header = build.header(frame.path); header.has_value())
            {
                const an::AcquisitionProjection projected = an::projectAcquisition(header->metadata.codec());
                if (projected.acquisition.exposureSeconds.has_value())
                {
                    frame.exposureSeconds = positive(*projected.acquisition.exposureSeconds);
                }
            }
        }
        stateNight(utcOffsetSeconds, frame);

        const QString registrationKey = registrationKeyOfGroup.value(key);
        if (fromStack && !registrationKey.isEmpty() && build.hasMetrics() &&
            build.settingsVersion(registrationKey).has_value())
        {
            const QHash<QString, RegistrationMetricRow>& metrics = build.metrics(registrationKey);
            if (const auto metric = metrics.constFind(frame.path); metric != metrics.cend())
            {
                stateMetrics(*metric, parseResolutionDimensions(resolutionOfGroup.value(key)), frame);
            }
        }

        if (ReportMaster* master = masterByKey.value(key, nullptr); master != nullptr)
        {
            master->frames.push_back(std::move(frame));
            continue;
        }
        auto group = std::find_if(model.groupsNotStacked.begin(), model.groupsNotStacked.end(),
                                  [&key](const ReportGroupNotStacked& candidate) { return candidate.key == key; });
        if (group == model.groupsNotStacked.end())
        {
            model.groupsNotStacked.push_back(
                ReportGroupNotStacked{.key = key, .name = friendlyGroupKey(key), .frames = {}});
            group = std::prev(model.groupsNotStacked.end());
        }
        group->frames.push_back(std::move(frame));
    }
}

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
    const QHash<QString, QDateTime> capturedByLight = captureTimes(build.manifest());
    for (const auto& group : build.manifest().integrationGroups)
    {
        const QString key = group.groupKey.toString();
        if (ReportMaster* master = masterByKey.value(key, nullptr); master != nullptr)
        {
            fillGroup(build, group, resultByKey.value(key, nullptr), capturedByLight, *master);
        }
    }
    fillLeftOut(build, masterByKey, capturedByLight, model);
    if (!build.hasMetrics() && !build.manifest().registrationGroups.empty())
    {
        build.notStated(reportText("What the registration measured of each light, and which lights it failed on"),
                        reportText("The run has no registration metrics database to read."));
    }
}
} // namespace ImageIntegration::Reporting

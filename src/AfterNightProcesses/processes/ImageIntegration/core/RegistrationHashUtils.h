// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "app/PlateSolveTypes.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace ImageIntegration::RegistrationHashUtils
{
[[nodiscard]] inline QString sha1Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

[[nodiscard]] inline QString hashFrameIdList(const std::vector<FrameId>& ids)
{
    QStringList parts;
    parts.reserve(static_cast<int>(ids.size()));
    for (const auto& id : ids)
    {
        parts.append(id.canonicalPath());
    }
    parts.sort(Qt::CaseSensitive);
    return sha1Hex(parts.join(QStringLiteral("\n")).toUtf8());
}

/// The pixel convention every stored manual control point and comet observation is written in: engine pixel
/// coordinates, the first pixel's centre at (0.5, 0.5).
inline constexpr char kImagePointPixelConvention[] = "engine-pixel-centres-at-half";

/// The key a stored set of image points states its convention under.
inline constexpr char kImagePointPixelConventionKey[] = "pixelConvention";

/// Whether a stored set of image points states the convention its positions are in. A set that states another, or
/// none, is not read: its positions would stand half a pixel from where they were placed were it another convention's.
[[nodiscard]] inline bool imagePointsStateTheirConvention(const QJsonObject& holder)
{
    return holder.value(QLatin1String(kImagePointPixelConventionKey)).toString() ==
           QLatin1String(kImagePointPixelConvention);
}

[[nodiscard]] inline QJsonArray
manualControlPointsToJson(const RegistrationSettings::ManualControlPointMap& control_points)
{
    QJsonArray out;
    for (auto it = control_points.cbegin(); it != control_points.cend(); ++it)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("frame"), it.key());

        QJsonArray points;
        for (const auto& point : it.value())
        {
            QJsonObject point_obj;

            QJsonArray source;
            source.append(point.sourcePoint.x());
            source.append(point.sourcePoint.y());
            point_obj.insert(QStringLiteral("source"), source);

            QJsonArray reference;
            reference.append(point.referencePoint.x());
            reference.append(point.referencePoint.y());
            point_obj.insert(QStringLiteral("reference"), reference);

            points.append(point_obj);
        }

        entry.insert(QStringLiteral("points"), points);
        entry.insert(QLatin1String(kImagePointPixelConventionKey), QLatin1String(kImagePointPixelConvention));
        out.append(entry);
    }
    return out;
}

[[nodiscard]] inline QString canonicalCometFrameId(QString frame_id)
{
    frame_id = frame_id.trimmed();
    if (frame_id.isEmpty())
    {
        return QString();
    }
    return FrameId(frame_id).canonicalPath();
}

[[nodiscard]] inline double canonicalCometCoordinate(const double value)
{
    constexpr double kScale = 1000000.0;
    return std::round(value * kScale) / kScale;
}

[[nodiscard]] inline QJsonArray cometPointToJson(const QPointF& point)
{
    QJsonArray coords;
    coords.append(canonicalCometCoordinate(point.x()));
    coords.append(canonicalCometCoordinate(point.y()));
    return coords;
}

[[nodiscard]] inline QString cometObservationOriginToString(const RegistrationSettings::CometObservationOrigin origin)
{
    switch (origin)
    {
    case RegistrationSettings::CometObservationOrigin::AcceptedAutomatic:
        return QStringLiteral("AcceptedAutomatic");
    case RegistrationSettings::CometObservationOrigin::Imported:
        return QStringLiteral("Imported");
    case RegistrationSettings::CometObservationOrigin::Manual:
    default:
        return QStringLiteral("Manual");
    }
}

[[nodiscard]] inline RegistrationSettings::CometObservationOrigin cometObservationOriginFromString(QString origin)
{
    origin = origin.trimmed();
    if (origin.compare(QStringLiteral("AcceptedAutomatic"), Qt::CaseInsensitive) == 0 ||
        origin.compare(QStringLiteral("accepted_automatic"), Qt::CaseInsensitive) == 0)
    {
        return RegistrationSettings::CometObservationOrigin::AcceptedAutomatic;
    }
    if (origin.compare(QStringLiteral("Imported"), Qt::CaseInsensitive) == 0)
    {
        return RegistrationSettings::CometObservationOrigin::Imported;
    }
    return RegistrationSettings::CometObservationOrigin::Manual;
}

[[nodiscard]] inline bool
cometObservationIsSerializable(const RegistrationSettings::CometTrackingObservation& observation)
{
    return !canonicalCometFrameId(observation.frameId).isEmpty() && std::isfinite(observation.sourcePosition.x()) &&
           std::isfinite(observation.sourcePosition.y());
}

[[nodiscard]] inline QJsonObject
cometObservationToJson(const RegistrationSettings::CometTrackingObservation& observation)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("frameId"), canonicalCometFrameId(observation.frameId));
    obj.insert(QStringLiteral("sourcePosition"), cometPointToJson(observation.sourcePosition));
    obj.insert(QStringLiteral("origin"), cometObservationOriginToString(observation.origin));
    obj.insert(QStringLiteral("enabled"), observation.enabled);
    if (observation.apertureRadiusPixels > 0.0 && std::isfinite(observation.apertureRadiusPixels))
    {
        obj.insert(QStringLiteral("apertureRadiusPixels"), canonicalCometCoordinate(observation.apertureRadiusPixels));
    }
    if (!observation.note.trimmed().isEmpty())
    {
        obj.insert(QStringLiteral("note"), observation.note.trimmed());
    }
    return obj;
}

[[nodiscard]] inline QJsonObject cometTrackingOptionsToJson(const RegistrationSettings::CometTrackingOptions& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("searchRadiusPixels"), canonicalCometCoordinate(settings.searchRadiusPixels));
    obj.insert(QStringLiteral("minimumSnr"), canonicalCometCoordinate(settings.minimumSnr));
    obj.insert(QStringLiteral("maxBootstrapFrames"), settings.maxBootstrapFrames);
    return obj;
}

[[nodiscard]] inline QJsonObject cometFitOptionsToJson(const RegistrationSettings::CometFitOptions& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("model"), settings.model.trimmed().toLower());
    obj.insert(QStringLiteral("robustRejection"), settings.robustRejection);
    obj.insert(QStringLiteral("outlierSigma"), canonicalCometCoordinate(settings.outlierSigma));
    return obj;
}

[[nodiscard]] inline QJsonObject cometTrackingToJson(const RegistrationSettings::CometTrackingSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), settings.schemaVersion);
    obj.insert(QLatin1String(kImagePointPixelConventionKey), QLatin1String(kImagePointPixelConvention));

    QJsonArray observations;
    for (const auto& observation : settings.observations)
    {
        if (cometObservationIsSerializable(observation))
        {
            observations.append(cometObservationToJson(observation));
        }
    }
    obj.insert(QStringLiteral("observations"), observations);
    obj.insert(QStringLiteral("trackingOptions"), cometTrackingOptionsToJson(settings.trackingOptions));
    obj.insert(QStringLiteral("fitOptions"), cometFitOptionsToJson(settings.fitOptions));
    obj.insert(QStringLiteral("outputMode"), ImageIntegration::normalizeCometStackingMode(settings.outputMode));
    return obj;
}

[[nodiscard]] inline QJsonObject cometTrackingHashToJson(const RegistrationSettings::CometTrackingSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), settings.schemaVersion);
    obj.insert(QStringLiteral("trackingOptions"), cometTrackingOptionsToJson(settings.trackingOptions));
    obj.insert(QStringLiteral("fitOptions"), cometFitOptionsToJson(settings.fitOptions));

    std::vector<QJsonObject> enabled_observations;
    enabled_observations.reserve(static_cast<size_t>(settings.observations.size()));
    QSet<QString> seen_frames;
    for (const auto& observation : settings.observations)
    {
        if (!observation.enabled || !cometObservationIsSerializable(observation))
        {
            continue;
        }

        QJsonObject observation_obj = cometObservationToJson(observation);
        const QString frame_id = observation_obj.value(QStringLiteral("frameId")).toString();
        if (seen_frames.contains(frame_id))
        {
            continue;
        }
        seen_frames.insert(frame_id);
        enabled_observations.push_back(observation_obj);
    }

    std::sort(enabled_observations.begin(), enabled_observations.end(),
              [](const QJsonObject& lhs, const QJsonObject& rhs) {
                  const QString lhs_frame = lhs.value(QStringLiteral("frameId")).toString();
                  const QString rhs_frame = rhs.value(QStringLiteral("frameId")).toString();
                  if (lhs_frame != rhs_frame)
                  {
                      return lhs_frame < rhs_frame;
                  }
                  const QString lhs_origin = lhs.value(QStringLiteral("origin")).toString();
                  const QString rhs_origin = rhs.value(QStringLiteral("origin")).toString();
                  if (lhs_origin != rhs_origin)
                  {
                      return lhs_origin < rhs_origin;
                  }
                  const QJsonArray lhs_pos = lhs.value(QStringLiteral("sourcePosition")).toArray();
                  const QJsonArray rhs_pos = rhs.value(QStringLiteral("sourcePosition")).toArray();
                  if (lhs_pos.at(0).toDouble() != rhs_pos.at(0).toDouble())
                  {
                      return lhs_pos.at(0).toDouble() < rhs_pos.at(0).toDouble();
                  }
                  return lhs_pos.at(1).toDouble() < rhs_pos.at(1).toDouble();
              });

    QJsonArray observations;
    for (const auto& observation : enabled_observations)
    {
        observations.append(observation);
    }
    obj.insert(QStringLiteral("enabledObservations"), observations);
    return obj;
}

[[nodiscard]] inline QJsonObject
chromaticAberrationToJson(const RegistrationSettings::ChromaticAberrationSettings& settings)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), settings.enabled);
    obj.insert(QStringLiteral("edgeFraction"), settings.edgeFraction);
    obj.insert(QStringLiteral("transformType"), settings.transformType);
    obj.insert(QStringLiteral("minMatchedStars"), settings.minMatchedStars);
    obj.insert(QStringLiteral("interpolation"), settings.interpolation);
    return obj;
}

[[nodiscard]] inline bool jsonToPointF(const QJsonValue& value, QPointF& out)
{
    const QJsonArray coordinates = value.toArray();
    if (coordinates.size() != 2)
    {
        return false;
    }

    out.setX(coordinates.at(0).toDouble(std::numeric_limits<double>::quiet_NaN()));
    out.setY(coordinates.at(1).toDouble(std::numeric_limits<double>::quiet_NaN()));
    return std::isfinite(out.x()) && std::isfinite(out.y());
}

[[nodiscard]] inline RegistrationSettings::ManualControlPointList
manualControlPointListFromJson(const QJsonArray& points)
{
    RegistrationSettings::ManualControlPointList result;
    result.reserve(points.size());
    for (const auto& value : points)
    {
        const QJsonObject object = value.toObject();
        RegistrationSettings::ManualControlPoint point;
        if (!jsonToPointF(object.value(QStringLiteral("source")), point.sourcePoint) ||
            !jsonToPointF(object.value(QStringLiteral("reference")), point.referencePoint))
        {
            continue;
        }
        result.push_back(point);
    }
    return result;
}

/// The stored manual control points, in engine pixel coordinates; a set that does not state that convention is not
/// read.
[[nodiscard]] inline RegistrationSettings::ManualControlPointMap manualControlPointsFromJson(const QJsonValue& value)
{
    RegistrationSettings::ManualControlPointMap result;
    const QJsonArray entries = value.toArray();
    for (const auto& entry_value : entries)
    {
        const QJsonObject entry = entry_value.toObject();
        const QString frame = entry.value(QStringLiteral("frame")).toString().trimmed();
        if (frame.isEmpty() || !imagePointsStateTheirConvention(entry))
        {
            continue;
        }

        const auto points = manualControlPointListFromJson(entry.value(QStringLiteral("points")).toArray());
        if (!points.isEmpty())
        {
            result.insert(frame, points);
        }
    }
    return result;
}

[[nodiscard]] inline std::optional<RegistrationSettings::CometTrackingSettings>
cometTrackingFromJson(const QJsonValue& value)
{
    const QJsonObject object = value.toObject();
    if (object.isEmpty())
    {
        return std::nullopt;
    }

    RegistrationSettings::CometTrackingSettings settings;
    settings.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(settings.schemaVersion);
    // Observations that do not state the engine's pixel convention are not read.
    const QJsonArray observations =
        imagePointsStateTheirConvention(object) ? object.value(QStringLiteral("observations")).toArray() : QJsonArray{};
    settings.observations.reserve(observations.size());
    for (const auto& observation_value : observations)
    {
        const QJsonObject observation_object = observation_value.toObject();
        RegistrationSettings::CometTrackingObservation observation;
        observation.frameId = canonicalCometFrameId(observation_object.value(QStringLiteral("frameId")).toString());
        observation.origin =
            cometObservationOriginFromString(observation_object.value(QStringLiteral("origin")).toString());
        observation.enabled = observation_object.value(QStringLiteral("enabled")).toBool(observation.enabled);
        observation.apertureRadiusPixels =
            observation_object.value(QStringLiteral("apertureRadiusPixels")).toDouble(observation.apertureRadiusPixels);
        observation.note = observation_object.value(QStringLiteral("note")).toString().trimmed();
        if (!jsonToPointF(observation_object.value(QStringLiteral("sourcePosition")), observation.sourcePosition) ||
            !cometObservationIsSerializable(observation))
        {
            continue;
        }
        settings.observations.push_back(observation);
    }

    const QJsonObject tracking_options = object.value(QStringLiteral("trackingOptions")).toObject();
    if (!tracking_options.isEmpty())
    {
        settings.trackingOptions.searchRadiusPixels = tracking_options.value(QStringLiteral("searchRadiusPixels"))
                                                          .toDouble(settings.trackingOptions.searchRadiusPixels);
        settings.trackingOptions.minimumSnr =
            tracking_options.value(QStringLiteral("minimumSnr")).toDouble(settings.trackingOptions.minimumSnr);
        settings.trackingOptions.maxBootstrapFrames = tracking_options.value(QStringLiteral("maxBootstrapFrames"))
                                                          .toInt(settings.trackingOptions.maxBootstrapFrames);
    }

    const QJsonObject fit_options = object.value(QStringLiteral("fitOptions")).toObject();
    if (!fit_options.isEmpty())
    {
        settings.fitOptions.model = fit_options.value(QStringLiteral("model")).toString(settings.fitOptions.model);
        settings.fitOptions.robustRejection =
            fit_options.value(QStringLiteral("robustRejection")).toBool(settings.fitOptions.robustRejection);
        settings.fitOptions.outlierSigma =
            fit_options.value(QStringLiteral("outlierSigma")).toDouble(settings.fitOptions.outlierSigma);
    }
    settings.outputMode = ImageIntegration::normalizeCometStackingMode(
        object.value(QStringLiteral("outputMode")).toString(settings.outputMode));

    return settings;
}

[[nodiscard]] inline RegistrationSettings::ChromaticAberrationSettings
chromaticAberrationFromJson(const QJsonValue& value)
{
    RegistrationSettings::ChromaticAberrationSettings settings;
    const QJsonObject object = value.toObject();
    if (object.isEmpty())
    {
        return settings;
    }

    settings.enabled = object.value(QStringLiteral("enabled")).toBool(settings.enabled);
    settings.edgeFraction = object.value(QStringLiteral("edgeFraction")).toDouble(settings.edgeFraction);
    settings.transformType = object.value(QStringLiteral("transformType")).toString(settings.transformType);
    settings.minMatchedStars = object.value(QStringLiteral("minMatchedStars")).toInt(settings.minMatchedStars);
    settings.interpolation = object.value(QStringLiteral("interpolation")).toString(settings.interpolation);
    return settings;
}

[[nodiscard]] inline QJsonObject registrationSettingsToJson(const RegistrationSettings& settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), settings.version);
    object.insert(QStringLiteral("minStarPairs"), settings.minStarPairs);
    object.insert(QStringLiteral("maxStarPairs"), settings.maxStarPairs);
    object.insert(QStringLiteral("autoReferenceDuringRegistration"), settings.autoReferenceDuringRegistration);
    object.insert(QStringLiteral("autoSolveUnsolvedFrames"), settings.autoSolveUnsolvedFrames);
    object.insert(QStringLiteral("enrichWithGaiaCatalog"), settings.enrichWithGaiaCatalog);
    object.insert(QStringLiteral("referenceImage"), settings.referenceImage);
    object.insert(QStringLiteral("registrationMethod"), settings.registrationMethod);
    object.insert(QStringLiteral("transformation"), settings.transformation);
    object.insert(QStringLiteral("transformMode"), settings.transformMode);
    object.insert(QStringLiteral("transformOrder"), settings.transformOrder);
    object.insert(QStringLiteral("twoPassRefinementEnabled"), settings.twoPassRefinementEnabled);
    object.insert(QStringLiteral("alignmentMode"), settings.alignmentMode);
    object.insert(QStringLiteral("interpolation"), settings.interpolation);
    object.insert(QStringLiteral("manualControlPoints"),
                  manualControlPointsToJson(settings.manualControlPointsByFrame));
    if (settings.cometTracking.has_value())
    {
        object.insert(QStringLiteral("cometTracking"), cometTrackingToJson(*settings.cometTracking));
    }
    object.insert(QStringLiteral("chromaticAberration"), chromaticAberrationToJson(settings.chromaticAberration));

    object.insert(QStringLiteral("radius"), settings.radius);
    object.insert(QStringLiteral("threshold"), settings.threshold);
    object.insert(QStringLiteral("amplitudeRangeEnabled"), settings.amplitudeRangeEnabled);
    object.insert(QStringLiteral("thresholdMinimum"), settings.thresholdMinimum);
    object.insert(QStringLiteral("thresholdMaximum"), settings.thresholdMaximum);
    object.insert(QStringLiteral("roundnessThreshold"), settings.roundnessThreshold);
    object.insert(QStringLiteral("roundnessRangeEnabled"), settings.roundnessRangeEnabled);
    object.insert(QStringLiteral("roundnessThresholdMaximum"), settings.roundnessThresholdMaximum);
    object.insert(QStringLiteral("convergence"), settings.convergence);
    object.insert(QStringLiteral("relaxPsfChecks"), settings.relaxPsfChecks);
    object.insert(QStringLiteral("profileType"), settings.profileType);
    object.insert(QStringLiteral("minimumBeta"), settings.minimumBeta);
    object.insert(QStringLiteral("cfaPattern"), settings.cfaPattern);
    object.insert(QStringLiteral("debayerMethod"), settings.debayerMethod);
    return object;
}

/// A stored transform contract made one reading, the star matcher's: the mode decides, so an order is kept only beside
/// a polynomial mode and two-pass refinement is on exactly when the mode refines - polynomial or automatic. Every
/// reader of stored settings calls it, so the dialog, the run and a manual fit cannot read one contract three ways.
inline void reconcileTransformContract(RegistrationSettings& settings)
{
    const QString mode = settings.transformMode.trimmed().toLower();
    const bool polynomial = mode == QStringLiteral("polynomial");
    if (!polynomial)
    {
        settings.transformOrder = 0;
    }
    settings.twoPassRefinementEnabled = polynomial || mode == QStringLiteral("auto");
}

[[nodiscard]] inline RegistrationSettings registrationSettingsFromJson(const QJsonObject& object)
{
    RegistrationSettings settings;
    settings.version = object.value(QStringLiteral("version")).toInt(settings.version);
    settings.minStarPairs = object.value(QStringLiteral("minStarPairs")).toInt(settings.minStarPairs);
    settings.maxStarPairs = object.value(QStringLiteral("maxStarPairs")).toInt(settings.maxStarPairs);
    settings.autoReferenceDuringRegistration = object.value(QStringLiteral("autoReferenceDuringRegistration"))
                                                   .toBool(settings.autoReferenceDuringRegistration);
    settings.autoSolveUnsolvedFrames =
        object.value(QStringLiteral("autoSolveUnsolvedFrames")).toBool(settings.autoSolveUnsolvedFrames);
    settings.enrichWithGaiaCatalog =
        object.value(QStringLiteral("enrichWithGaiaCatalog")).toBool(settings.enrichWithGaiaCatalog);
    settings.referenceImage = object.value(QStringLiteral("referenceImage")).toString(settings.referenceImage);
    settings.registrationMethod =
        object.value(QStringLiteral("registrationMethod")).toString(settings.registrationMethod);
    settings.transformation = object.value(QStringLiteral("transformation")).toString(settings.transformation);
    settings.transformMode = object.value(QStringLiteral("transformMode")).toString(settings.transformMode);
    settings.transformOrder = object.value(QStringLiteral("transformOrder")).toInt(settings.transformOrder);
    settings.twoPassRefinementEnabled =
        object.value(QStringLiteral("twoPassRefinementEnabled")).toBool(settings.twoPassRefinementEnabled);
    settings.alignmentMode = object.value(QStringLiteral("alignmentMode")).toString(settings.alignmentMode);
    settings.interpolation = object.value(QStringLiteral("interpolation")).toString(settings.interpolation);
    settings.manualControlPointsByFrame =
        manualControlPointsFromJson(object.value(QStringLiteral("manualControlPoints")));
    settings.cometTracking = cometTrackingFromJson(object.value(QStringLiteral("cometTracking")));
    settings.chromaticAberration = chromaticAberrationFromJson(object.value(QStringLiteral("chromaticAberration")));

    settings.radius = object.value(QStringLiteral("radius")).toInt(settings.radius);
    settings.threshold = object.value(QStringLiteral("threshold")).toDouble(settings.threshold);
    settings.amplitudeRangeEnabled =
        object.value(QStringLiteral("amplitudeRangeEnabled")).toBool(settings.amplitudeRangeEnabled);
    settings.thresholdMinimum = object.value(QStringLiteral("thresholdMinimum")).toDouble(settings.thresholdMinimum);
    settings.thresholdMaximum = object.value(QStringLiteral("thresholdMaximum")).toDouble(settings.thresholdMaximum);
    settings.roundnessThreshold =
        object.value(QStringLiteral("roundnessThreshold")).toDouble(settings.roundnessThreshold);
    settings.roundnessRangeEnabled =
        object.value(QStringLiteral("roundnessRangeEnabled")).toBool(settings.roundnessRangeEnabled);
    settings.roundnessThresholdMaximum =
        object.value(QStringLiteral("roundnessThresholdMaximum")).toDouble(settings.roundnessThresholdMaximum);
    settings.convergence = object.value(QStringLiteral("convergence")).toInt(settings.convergence);
    settings.relaxPsfChecks = object.value(QStringLiteral("relaxPsfChecks")).toBool(settings.relaxPsfChecks);
    settings.profileType = object.value(QStringLiteral("profileType")).toString(settings.profileType);
    settings.minimumBeta = object.value(QStringLiteral("minimumBeta")).toDouble(settings.minimumBeta);
    settings.cfaPattern = object.value(QStringLiteral("cfaPattern")).toString(settings.cfaPattern);
    settings.debayerMethod = object.value(QStringLiteral("debayerMethod")).toString(settings.debayerMethod);
    reconcileTransformContract(settings);
    return settings;
}

/// A stored choice's stable key, so a fingerprint does not move with the text a dialog or a reader spells it by; text
/// no table knows is fingerprinted as it is.
[[nodiscard]] inline QString fingerprintChoice(const PersistedChoice choice, const QString& text)
{
    const PersistedChoiceValue value = persistedChoiceFromText(choice, text);
    return value.known ? value.key : text;
}

/// The chromatic aberration correction's settings as a fingerprint reads them: each choice by its key.
[[nodiscard]] inline QJsonObject
chromaticAberrationFingerprintJson(const RegistrationSettings::ChromaticAberrationSettings& settings)
{
    QJsonObject obj = chromaticAberrationToJson(settings);
    obj.insert(QStringLiteral("transformType"),
               fingerprintChoice(PersistedChoice::TransformModel, settings.transformType));
    obj.insert(QStringLiteral("interpolation"),
               fingerprintChoice(PersistedChoice::Interpolation, settings.interpolation));
    return obj;
}

[[nodiscard]] inline QString registrationDetectionHash(const RegistrationSettings& s)
{
    QJsonObject obj;
    // The star detections a registration caches are a derived family: another engine generation's are not reused.
    schema::insertArtifactFamilyIdentity(obj, schema::ArtifactFamily::StarDetections);
    const QString method = RegistrationSettingsUtils::normalizeRegistrationMethod(s.registrationMethod);
    obj.insert(QStringLiteral("registrationMethod"), method);
    obj.insert(QStringLiteral("cfaPattern"), fingerprintChoice(PersistedChoice::CfaPattern, s.cfaPattern));
    obj.insert(QStringLiteral("debayerMethod"), fingerprintChoice(PersistedChoice::DemosaicMethod, s.debayerMethod));
    if (RegistrationSettingsUtils::usesStarDetection(method))
    {
        obj.insert(QStringLiteral("radius"), s.radius);
        obj.insert(QStringLiteral("threshold"), s.threshold);
        obj.insert(QStringLiteral("amplitudeRangeEnabled"), s.amplitudeRangeEnabled);
        obj.insert(QStringLiteral("thresholdMinimum"), s.thresholdMinimum);
        obj.insert(QStringLiteral("thresholdMaximum"), s.thresholdMaximum);
        obj.insert(QStringLiteral("roundnessThreshold"), s.roundnessThreshold);
        obj.insert(QStringLiteral("roundnessRangeEnabled"), s.roundnessRangeEnabled);
        obj.insert(QStringLiteral("roundnessThresholdMaximum"), s.roundnessThresholdMaximum);
        obj.insert(QStringLiteral("convergence"), s.convergence);
        obj.insert(QStringLiteral("relaxPsfChecks"), s.relaxPsfChecks);
        obj.insert(QStringLiteral("profileType"), s.profileType);
        obj.insert(QStringLiteral("minimumBeta"), s.minimumBeta);
        // The most stars a frame is detected with bounds the detection: stars cached under another cap are not reused.
        obj.insert(QStringLiteral("maxStarPairs"), s.maxStarPairs);
    }
    return sha1Hex(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline QString registrationTransformHash(const RegistrationSettings& s,
                                                       const QString& resolvedReferenceFrameId)
{
    QJsonObject obj;
    // The transforms a registration records are a derived family: another engine generation's are not reused.
    schema::insertArtifactFamilyIdentity(obj, schema::ArtifactFamily::Registration);
    const QString method = RegistrationSettingsUtils::normalizeRegistrationMethod(s.registrationMethod);
    obj.insert(QStringLiteral("registrationMethod"), method);
    obj.insert(QStringLiteral("autoReferenceDuringRegistration"), s.autoReferenceDuringRegistration);
    obj.insert(QStringLiteral("autoSolveUnsolvedFrames"), s.autoSolveUnsolvedFrames);
    obj.insert(QStringLiteral("enrichWithGaiaCatalog"), s.enrichWithGaiaCatalog);
    obj.insert(QStringLiteral("referenceImage"), s.referenceImage);
    // The canvas and the interpolation shape the alignment, not the transforms. A mosaic alone changes what is
    // registered, because it lets a frame the stars do not place be placed by its solution.
    const PersistedChoiceValue canvas = persistedChoiceFromText(PersistedChoice::AlignmentMode, s.alignmentMode);
    obj.insert(QStringLiteral("mosaicCanvas"),
               canvas.known && canvasModeForKey(canvas.key) == RegistrationCanvasMode::Mosaic);
    if (RegistrationSettingsUtils::usesStarDetection(method))
    {
        obj.insert(QStringLiteral("minStarPairs"), s.minStarPairs);
        obj.insert(QStringLiteral("maxStarPairs"), s.maxStarPairs);
    }
    if (RegistrationSettingsUtils::usesCometTracking(method) && s.cometTracking.has_value())
    {
        obj.insert(QStringLiteral("cometTracking"), cometTrackingHashToJson(*s.cometTracking));
        const QString resolved_reference = canonicalCometFrameId(resolvedReferenceFrameId);
        if (!resolved_reference.isEmpty())
        {
            obj.insert(QStringLiteral("resolvedReferenceFrameId"), resolved_reference);
        }
    }
    obj.insert(QStringLiteral("chromaticAberrationEnabled"), s.chromaticAberration.enabled);
    if (s.chromaticAberration.enabled)
    {
        obj.insert(QStringLiteral("chromaticAberration"), chromaticAberrationFingerprintJson(s.chromaticAberration));
    }
    if (RegistrationSettingsUtils::usesTransformContract(method))
    {
        obj.insert(QStringLiteral("transformation"),
                   fingerprintChoice(PersistedChoice::TransformModel, s.transformation));
        obj.insert(QStringLiteral("transformMode"), s.transformMode);
        // Two-pass refinement follows the mode and is read by no run, so it is not part of what is registered.
        obj.insert(QStringLiteral("transformOrder"), s.transformOrder);
        if (RegistrationSettingsUtils::usesManualControlPoints(method))
        {
            obj.insert(QStringLiteral("manualControlPoints"), manualControlPointsToJson(s.manualControlPointsByFrame));
        }
    }
    return sha1Hex(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline QString registrationTransformHash(const RegistrationSettings& s)
{
    return registrationTransformHash(s, QString());
}

[[nodiscard]] inline QString plateSolveSettingsHash(const an::ui::PlateSolveSettings& settings,
                                                    const QString& astrometry_api_key_hash)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("defaultSolver"), settings.defaultSolver);
    obj.insert(QStringLiteral("astapPath"), settings.astapPath);
    obj.insert(QStringLiteral("astrometryLocalPath"), settings.astrometryLocalPath);
    obj.insert(QStringLiteral("astrometryApiKeyHash"), astrometry_api_key_hash.trimmed());
    obj.insert(QStringLiteral("indexPath"), settings.indexPath);
    obj.insert(QStringLiteral("gaiaCatalogDirectory"), settings.gaiaCatalogDirectory);
    obj.insert(QStringLiteral("gaiaCatalogPack"), settings.gaiaCatalogPack);
    if (settings.focalLengthMm.has_value())
    {
        obj.insert(QStringLiteral("focalLengthMm"), *settings.focalLengthMm);
    }
    if (settings.pixelSizeUm.has_value())
    {
        obj.insert(QStringLiteral("pixelSizeUm"), *settings.pixelSizeUm);
    }
    obj.insert(QStringLiteral("searchRadius"), settings.searchRadius);
    obj.insert(QStringLiteral("downsample"), settings.downsample);
    obj.insert(QStringLiteral("timeoutSeconds"), settings.timeoutSeconds);
    obj.insert(QStringLiteral("useHints"), settings.useHints);
    return sha1Hex(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline QString effectiveRegistrationHash(const RegistrationSettings& settings,
                                                       const an::ui::PlateSolveSettings& plate_solve,
                                                       const QString& astrometry_api_key_hash)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("registration"), registrationTransformHash(settings));
    obj.insert(QStringLiteral("plateSolve"), plateSolveSettingsHash(plate_solve, astrometry_api_key_hash));
    return sha1Hex(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline bool plateSolveSettingsAreDefault(const an::ui::PlateSolveSettings& settings,
                                                       const QString& astrometry_api_key_hash)
{
    const an::ui::PlateSolveSettings defaults;
    return settings.defaultSolver == defaults.defaultSolver && settings.astapPath == defaults.astapPath &&
           settings.astrometryLocalPath == defaults.astrometryLocalPath &&
           settings.astrometryApiKey.trimmed().isEmpty() && astrometry_api_key_hash.trimmed().isEmpty() &&
           settings.indexPath == defaults.indexPath && settings.gaiaCatalogDirectory == defaults.gaiaCatalogDirectory &&
           settings.gaiaCatalogPack == defaults.gaiaCatalogPack && settings.focalLengthMm == defaults.focalLengthMm &&
           settings.pixelSizeUm == defaults.pixelSizeUm &&
           qFuzzyCompare(settings.searchRadius + 1.0, defaults.searchRadius + 1.0) &&
           settings.downsample == defaults.downsample && settings.timeoutSeconds == defaults.timeoutSeconds &&
           settings.useHints == defaults.useHints;
}
} // namespace ImageIntegration::RegistrationHashUtils

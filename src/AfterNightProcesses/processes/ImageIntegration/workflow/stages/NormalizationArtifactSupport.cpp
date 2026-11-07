// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A group's normalization coefficients as the workflow keeps them: the document and its sidecar.

#include "stdafx.hpp"
#include "NormalizationArtifactSupport.h"

#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <string>

namespace ImageIntegration
{

namespace
{

constexpr char kStageId[] = "LightsNormalization";

[[nodiscard]] std::optional<NormalizationCoefficientMode> modeFromKey(const QString& key)
{
    for (const auto mode : {NormalizationCoefficientMode::Global, NormalizationCoefficientMode::Local,
                            NormalizationCoefficientMode::LocalThenGlobal})
    {
        if (key == QLatin1String(normalizationCoefficientModeKey(mode)))
        {
            return mode;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<NormalizationMeasurementPlane> planeFromKey(const QString& key)
{
    for (const auto plane : {NormalizationMeasurementPlane::Frames, NormalizationMeasurementPlane::RegisteredCanvas,
                             NormalizationMeasurementPlane::DrizzleAnalysisPlane})
    {
        if (key == QLatin1String(normalizationMeasurementPlaneKey(plane)))
        {
            return plane;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<an::NormalizationMethod> methodFromName(const QString& name)
{
    for (const auto method : {an::NormalizationMethod::Additive, an::NormalizationMethod::Multiplicative,
                              an::NormalizationMethod::AdditiveScaling, an::NormalizationMethod::MultiplicativeScaling})
    {
        if (name == QLatin1String(an::normalizationMethodName(method)))
        {
            return method;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<an::ScaleEstimator> estimatorFromName(const QString& name)
{
    for (const auto estimator : {an::ScaleEstimator::MedianAbsoluteDeviation, an::ScaleEstimator::IterativeKSigma})
    {
        if (name == QLatin1String(an::scaleEstimatorName(estimator)))
        {
            return estimator;
        }
    }
    return std::nullopt;
}

[[nodiscard]] QString checkMatch(const an::FrameNormalization& match, const std::uint32_t channelCount)
{
    if (!an::isNormalizationMethod(match.method) || !an::isScaleEstimator(match.estimator))
    {
        return QStringLiteral("a global match names an unknown method or estimator");
    }
    if (match.channelCount != channelCount)
    {
        return QStringLiteral("a global match holds %1 channel(s) where the frames hold %2")
            .arg(match.channelCount)
            .arg(channelCount);
    }
    for (std::uint32_t channel = 0; channel < match.channelCount; ++channel)
    {
        const auto& coefficients = match.channels[channel];
        if (!std::isfinite(coefficients.scale) || coefficients.scale == 0.0 || !std::isfinite(coefficients.offset))
        {
            return QStringLiteral("a global match holds a zero or non-finite coefficient");
        }
    }
    return {};
}

[[nodiscard]] QJsonObject matchToJson(const an::FrameNormalization& match)
{
    QJsonObject object;
    object.insert(QStringLiteral("method"), QString::fromLatin1(an::normalizationMethodName(match.method)));
    object.insert(QStringLiteral("estimator"), QString::fromLatin1(an::scaleEstimatorName(match.estimator)));
    QJsonArray channels;
    for (std::uint32_t channel = 0; channel < match.channelCount; ++channel)
    {
        QJsonObject coefficients;
        coefficients.insert(QStringLiteral("scale"), match.channels[channel].scale);
        coefficients.insert(QStringLiteral("offset"), match.channels[channel].offset);
        channels.append(coefficients);
    }
    object.insert(QStringLiteral("channels"), channels);
    object.insert(QStringLiteral("dispersionFallbacks"), static_cast<qint64>(match.dispersionFallbacks));
    object.insert(QStringLiteral("locationFallbacks"), static_cast<qint64>(match.locationFallbacks));
    return object;
}

[[nodiscard]] std::optional<an::FrameNormalization> matchFromJson(const QJsonValue& value)
{
    const QJsonObject object = value.toObject();
    const auto method = methodFromName(object.value(QStringLiteral("method")).toString());
    const auto estimator = estimatorFromName(object.value(QStringLiteral("estimator")).toString());
    const QJsonArray channels = object.value(QStringLiteral("channels")).toArray();
    if (!method.has_value() || !estimator.has_value() || channels.isEmpty() ||
        channels.size() > static_cast<qsizetype>(an::kMaxNormalizationChannels))
    {
        return std::nullopt;
    }
    an::FrameNormalization match;
    match.method = *method;
    match.estimator = *estimator;
    match.channelCount = static_cast<std::uint32_t>(channels.size());
    for (qsizetype channel = 0; channel < channels.size(); ++channel)
    {
        const QJsonObject coefficients = channels.at(channel).toObject();
        const QJsonValue scale = coefficients.value(QStringLiteral("scale"));
        const QJsonValue offset = coefficients.value(QStringLiteral("offset"));
        if (!scale.isDouble() || !offset.isDouble())
        {
            return std::nullopt;
        }
        match.channels[static_cast<std::size_t>(channel)] = {scale.toDouble(), offset.toDouble()};
    }
    match.dispersionFallbacks =
        static_cast<std::uint32_t>(object.value(QStringLiteral("dispersionFallbacks")).toInteger());
    match.locationFallbacks = static_cast<std::uint32_t>(object.value(QStringLiteral("locationFallbacks")).toInteger());
    return match;
}

[[nodiscard]] bool positiveCount(const QJsonValue& value, std::uint32_t& out)
{
    if (!value.isDouble())
    {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 0.0 ||
        number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }
    out = static_cast<std::uint32_t>(number);
    return true;
}

[[nodiscard]] QString sha256OfFile(const QString& path)
{
    return ArtifactTracker::contentDigestForFile(path);
}

} // namespace

const char* normalizationCoefficientModeKey(const NormalizationCoefficientMode mode) noexcept
{
    switch (mode)
    {
    case NormalizationCoefficientMode::Global:
        return "global";
    case NormalizationCoefficientMode::Local:
        return "local";
    case NormalizationCoefficientMode::LocalThenGlobal:
        return "local-then-global";
    }
    return "unknown";
}

const char* normalizationMeasurementPlaneKey(const NormalizationMeasurementPlane plane) noexcept
{
    switch (plane)
    {
    case NormalizationMeasurementPlane::Frames:
        return "frames";
    case NormalizationMeasurementPlane::RegisteredCanvas:
        return "registered-canvas";
    case NormalizationMeasurementPlane::DrizzleAnalysisPlane:
        return "drizzle-analysis-plane";
    }
    return "unknown";
}

QString checkNormalizationCoefficientSet(const NormalizationCoefficientSet& set)
{
    const std::size_t frames = set.logicalFrames.size();
    if (frames == 0u || set.sourceFrames.size() != frames)
    {
        return QStringLiteral("the set names no frame, or a source for some frames and not others");
    }
    if (set.referenceIndex >= frames)
    {
        return QStringLiteral("the reference is not one of the frames");
    }
    if (set.channelCount == 0u || set.channelCount > an::kMaxNormalizationChannels || set.width == 0u ||
        set.height == 0u)
    {
        return QStringLiteral("the frames' extent or channels are invalid");
    }
    if ((set.plane == NormalizationMeasurementPlane::DrizzleAnalysisPlane) != set.drizzlePlane.has_value())
    {
        return QStringLiteral("a drizzle analysis plane is stated for another plane, or missing for its own");
    }
    if (set.drizzlePlane.has_value() &&
        (set.drizzlePlane->width != set.width || set.drizzlePlane->height != set.height ||
         !std::isfinite(set.drizzlePlane->originX) || !std::isfinite(set.drizzlePlane->originY) ||
         !std::isfinite(set.drizzlePlane->scale) || set.drizzlePlane->scale < 1.0))
    {
        return QStringLiteral("the drizzle analysis plane is not the frames' extent or its numbers are invalid");
    }

    if (set.mode == NormalizationCoefficientMode::Global)
    {
        if (set.local.has_value() || set.global.size() != frames)
        {
            return QStringLiteral("a global set holds a local document, or not one match per frame");
        }
        for (const auto& match : set.global)
        {
            if (const QString reason = checkMatch(match, set.channelCount); !reason.isEmpty())
            {
                return reason;
            }
        }
        return {};
    }

    if (!set.global.empty() || !set.local.has_value())
    {
        return QStringLiteral("a local set holds a global table, or no local document");
    }
    const an::LocalNormalizationDocument& local = *set.local;
    if (local.shape.channelCount != set.channelCount || local.shape.width != set.width ||
        local.shape.height != set.height)
    {
        return QStringLiteral("the local document's shape is not the frames'");
    }
    if (local.frames.size() != frames)
    {
        return QStringLiteral("the local document does not hold one record per frame");
    }
    if (local.referenceIndex.has_value() && *local.referenceIndex != set.referenceIndex)
    {
        return QStringLiteral("the local document's reference is not the set's");
    }
    for (std::size_t index = 0; index < frames; ++index)
    {
        const auto& record = local.frames[index];
        if (record.frameIndex != index)
        {
            return QStringLiteral("the local document's frames are not in coefficient order");
        }
        const bool wantsGlobal = set.mode == NormalizationCoefficientMode::LocalThenGlobal;
        if (record.global.has_value() != wantsGlobal)
        {
            return QStringLiteral("a local record's global match does not follow the set's mode");
        }
        if (record.global.has_value())
        {
            if (const QString reason = checkMatch(*record.global, set.channelCount); !reason.isEmpty())
            {
                return reason;
            }
        }
    }
    return {};
}

QByteArray formatNormalizationCoefficients(const NormalizationCoefficientSet& set)
{
    QJsonObject object;
    object.insert(QStringLiteral("mode"), QString::fromLatin1(normalizationCoefficientModeKey(set.mode)));
    object.insert(QStringLiteral("plane"), QString::fromLatin1(normalizationMeasurementPlaneKey(set.plane)));
    object.insert(QStringLiteral("channelCount"), static_cast<qint64>(set.channelCount));
    object.insert(QStringLiteral("width"), static_cast<qint64>(set.width));
    object.insert(QStringLiteral("height"), static_cast<qint64>(set.height));
    object.insert(QStringLiteral("referenceIndex"), static_cast<qint64>(set.referenceIndex));
    QJsonArray frames;
    for (std::size_t index = 0; index < set.logicalFrames.size(); ++index)
    {
        QJsonObject frame;
        frame.insert(QStringLiteral("logical"), set.logicalFrames[index]);
        frame.insert(QStringLiteral("source"), index < set.sourceFrames.size() ? set.sourceFrames[index] : QString());
        frames.append(frame);
    }
    object.insert(QStringLiteral("frames"), frames);
    if (!set.global.empty())
    {
        QJsonArray global;
        for (const auto& match : set.global)
        {
            global.append(matchToJson(match));
        }
        object.insert(QStringLiteral("global"), global);
    }
    if (set.local.has_value())
    {
        const an::Result<std::string> text = an::formatLocalNormalizationDocument(*set.local);
        if (!text.hasValue())
        {
            return {};
        }
        object.insert(QStringLiteral("localDocument"), QString::fromStdString(*text));
    }
    if (set.drizzlePlane.has_value())
    {
        QJsonObject plane;
        plane.insert(QStringLiteral("originX"), set.drizzlePlane->originX);
        plane.insert(QStringLiteral("originY"), set.drizzlePlane->originY);
        plane.insert(QStringLiteral("width"), static_cast<qint64>(set.drizzlePlane->width));
        plane.insert(QStringLiteral("height"), static_cast<qint64>(set.drizzlePlane->height));
        plane.insert(QStringLiteral("scale"), set.drizzlePlane->scale);
        object.insert(QStringLiteral("drizzlePlane"), plane);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<NormalizationCoefficientSet> parseNormalizationCoefficients(const QByteArray& payload, QString& error)
{
    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        error = QStringLiteral("The coefficient document is not readable.");
        return std::nullopt;
    }
    const QJsonObject object = json.object();
    NormalizationCoefficientSet set;
    const auto mode = modeFromKey(object.value(QStringLiteral("mode")).toString());
    const auto plane = planeFromKey(object.value(QStringLiteral("plane")).toString());
    if (!mode.has_value() || !plane.has_value())
    {
        error = QStringLiteral("The coefficient document names a mode or plane this version does not read.");
        return std::nullopt;
    }
    set.mode = *mode;
    set.plane = *plane;
    if (!positiveCount(object.value(QStringLiteral("channelCount")), set.channelCount) ||
        !positiveCount(object.value(QStringLiteral("width")), set.width) ||
        !positiveCount(object.value(QStringLiteral("height")), set.height) ||
        !positiveCount(object.value(QStringLiteral("referenceIndex")), set.referenceIndex))
    {
        error = QStringLiteral("The coefficient document's counts are invalid.");
        return std::nullopt;
    }
    for (const auto& value : object.value(QStringLiteral("frames")).toArray())
    {
        const QJsonObject frame = value.toObject();
        set.logicalFrames.push_back(frame.value(QStringLiteral("logical")).toString());
        set.sourceFrames.push_back(frame.value(QStringLiteral("source")).toString());
    }
    for (const auto& value : object.value(QStringLiteral("global")).toArray())
    {
        const auto match = matchFromJson(value);
        if (!match.has_value())
        {
            error = QStringLiteral("The coefficient document holds a global match this version does not read.");
            return std::nullopt;
        }
        set.global.push_back(*match);
    }
    if (object.contains(QStringLiteral("localDocument")))
    {
        const an::Result<an::LocalNormalizationDocument> local =
            an::parseLocalNormalizationDocument(object.value(QStringLiteral("localDocument")).toString().toStdString());
        if (!local.hasValue())
        {
            error = QStringLiteral("The coefficient document's local document is refused: %1")
                        .arg(QString::fromUtf8(local.status().message()));
            return std::nullopt;
        }
        set.local = std::move(*local);
    }
    if (object.contains(QStringLiteral("drizzlePlane")))
    {
        const QJsonObject drizzle = object.value(QStringLiteral("drizzlePlane")).toObject();
        DrizzleAnalysisPlaneRecord record;
        record.originX = drizzle.value(QStringLiteral("originX")).toDouble(std::numeric_limits<double>::quiet_NaN());
        record.originY = drizzle.value(QStringLiteral("originY")).toDouble(std::numeric_limits<double>::quiet_NaN());
        record.scale = drizzle.value(QStringLiteral("scale")).toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!positiveCount(drizzle.value(QStringLiteral("width")), record.width) ||
            !positiveCount(drizzle.value(QStringLiteral("height")), record.height))
        {
            error = QStringLiteral("The coefficient document's drizzle plane is invalid.");
            return std::nullopt;
        }
        set.drizzlePlane = record;
    }
    if (const QString reason = checkNormalizationCoefficientSet(set); !reason.isEmpty())
    {
        error = QStringLiteral("The coefficient document is refused: %1.").arg(reason);
        return std::nullopt;
    }
    return set;
}

QString normalizationArtifactIdentity(const QString& inputsFingerprint, const QString& settingsFingerprint)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(inputsFingerprint.toUtf8());
    hash.addData(QByteArrayView("\n"));
    hash.addData(settingsFingerprint.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

QString warpedPlaneGeometryFingerprint(const NormalizationMeasurementPlane plane, const QJsonObject& transforms,
                                       const double originX, const double originY, const std::uint32_t width,
                                       const std::uint32_t height, const an::KernelInterpolation interpolation)
{
    const bool analysisPlane = plane == NormalizationMeasurementPlane::DrizzleAnalysisPlane;
    QJsonObject identity = transforms;
    identity.insert(QStringLiteral("canvasOriginX"), originX);
    identity.insert(QStringLiteral("canvasOriginY"), originY);
    identity.insert(analysisPlane ? QStringLiteral("analysisWidth") : QStringLiteral("canvasWidth"),
                    static_cast<qint64>(width));
    identity.insert(analysisPlane ? QStringLiteral("analysisHeight") : QStringLiteral("canvasHeight"),
                    static_cast<qint64>(height));
    identity.insert(QStringLiteral("interpolation"), static_cast<int>(interpolation));
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
}

QString warpedMeasurementInputsFingerprint(const QString& filesFingerprint, const QString& geometryFingerprint)
{
    return QString::fromLatin1(
        QCryptographicHash::hash((filesFingerprint + QLatin1Char('\n') + geometryFingerprint).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}

std::optional<DocumentEnvelope> normalizationCoefficientsEnvelope(const NormalizationCoefficientSet& set,
                                                                  const QString& identity)
{
    if (!checkNormalizationCoefficientSet(set).isEmpty())
    {
        return std::nullopt;
    }
    DocumentEnvelope envelope;
    envelope.documentType = QString::fromLatin1(kNormalizationCoefficientsDocumentType);
    envelope.schemaVersion = kNormalizationCoefficientsSchemaVersion;
    envelope.identity = identity;
    envelope.payload = formatNormalizationCoefficients(set);
    if (envelope.payload.isEmpty())
    {
        return std::nullopt;
    }
    return envelope;
}

NormalizationDocumentRead readNormalizationCoefficients(const QString& path,
                                                        const std::optional<QString>& expectedIdentity)
{
    NormalizationDocumentRead result;
    DocumentReadRequest request;
    request.documentType = QString::fromLatin1(kNormalizationCoefficientsDocumentType);
    request.maximumSchemaVersion = kNormalizationCoefficientsSchemaVersion;
    request.expectedIdentity = expectedIdentity;
    request.maxPayloadBytes = an::kMaxLocalNormalizationDocumentBytes + 1024ull * 1024ull;
    const DocumentReadResult read = readDocument(path, request);
    if (!read.ok())
    {
        result.failure = read.failure == DocumentReadFailure::UnknownSchema ||
                                 read.failure == DocumentReadFailure::UnknownEnvelopeVersion
                             ? NormalizationDocumentFailure::NewerSchema
                             : NormalizationDocumentFailure::Unreadable;
        result.message = read.message;
        return result;
    }
    QString error;
    result.coefficients = parseNormalizationCoefficients(read.envelope.payload, error);
    if (!result.coefficients.has_value())
    {
        result.failure = NormalizationDocumentFailure::Invalid;
        result.message = error;
    }
    return result;
}

QString normalizationSidecarPath(const QString& artifactPath)
{
    return artifactPath + QStringLiteral(".meta.json");
}

QJsonObject normalizationSidecar(const IntegrationGroupKey& groupKey, const QString& inputsFingerprint,
                                 const QString& settingsFingerprint, const QString& payloadSha256,
                                 const NormalizationCoefficientSet& set)
{
    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("schemaVersion"), schema::kNormalizationArtifactSchemaVersion);
    schema::insertArtifactFamilyIdentity(sidecar, schema::ArtifactFamily::NormalizationCoefficients);
    sidecar.insert(QStringLiteral("stageId"), QString::fromLatin1(kStageId));
    sidecar.insert(QStringLiteral("integrationGroupKey"), groupKey.toString());
    sidecar.insert(QStringLiteral("documentType"), QString::fromLatin1(kNormalizationCoefficientsDocumentType));
    sidecar.insert(QStringLiteral("documentSchemaVersion"), kNormalizationCoefficientsSchemaVersion);
    sidecar.insert(QStringLiteral("inputsFingerprint"), inputsFingerprint);
    sidecar.insert(QStringLiteral("settingsFingerprint"), settingsFingerprint);
    if (!payloadSha256.isEmpty())
    {
        sidecar.insert(QStringLiteral("coefficientPayloadSha256"), payloadSha256);
    }
    sidecar.insert(QStringLiteral("mode"), QString::fromLatin1(normalizationCoefficientModeKey(set.mode)));
    sidecar.insert(QStringLiteral("plane"), QString::fromLatin1(normalizationMeasurementPlaneKey(set.plane)));
    sidecar.insert(QStringLiteral("frameCount"), static_cast<qint64>(set.logicalFrames.size()));
    sidecar.insert(QStringLiteral("channelCount"), static_cast<qint64>(set.channelCount));
    sidecar.insert(QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return sidecar;
}

NormalizationArtifactReuse checkNormalizationArtifact(const QString& artifactPath, const IntegrationGroupKey& groupKey,
                                                      const QString& inputsFingerprint,
                                                      const QString& settingsFingerprint)
{
    NormalizationArtifactReuse result;
    const QString sidecarPath = normalizationSidecarPath(artifactPath);
    if (!QFileInfo::exists(sidecarPath))
    {
        result.reason = QStringLiteral("Artifact metadata missing.");
        return result;
    }
    QFile file(sidecarPath);
    QByteArray bytes;
    QString error;
    if (!schema::readMetadataSidecarBounded(file, bytes, &error))
    {
        result.reason = QStringLiteral("Failed to read artifact metadata: %1").arg(error);
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        result.reason = QStringLiteral("Artifact metadata is not readable.");
        return result;
    }
    result.sidecar = json.object();
    const int schemaVersion = result.sidecar.value(QStringLiteral("schemaVersion")).toInt();
    if (schemaVersion > schema::kNormalizationArtifactSchemaVersion)
    {
        result.newerSchema = true;
        result.reason =
            QStringLiteral("Artifact metadata was written by a newer version (schema %1).").arg(schemaVersion);
        return result;
    }
    if (schemaVersion != schema::kNormalizationArtifactSchemaVersion)
    {
        result.reason = QStringLiteral("Artifact metadata schema version mismatch.");
        return result;
    }
    if (!schema::statesCurrentArtifactFamily(result.sidecar, schema::ArtifactFamily::NormalizationCoefficients))
    {
        result.reason = QStringLiteral("Artifact was computed by another engine or another revision of its family.");
        return result;
    }
    if (result.sidecar.value(QStringLiteral("stageId")).toString() != QLatin1String(kStageId) ||
        result.sidecar.value(QStringLiteral("integrationGroupKey")).toString() != groupKey.toString())
    {
        result.reason = QStringLiteral("Artifact belongs to another stage or group.");
        return result;
    }
    if (result.sidecar.value(QStringLiteral("inputsFingerprint")).toString() != inputsFingerprint)
    {
        result.reason = QStringLiteral("Inputs changed since artifact was created.");
        return result;
    }
    if (result.sidecar.value(QStringLiteral("settingsFingerprint")).toString() != settingsFingerprint)
    {
        result.reason = QStringLiteral("Settings changed since artifact was created.");
        return result;
    }
    if (!QFileInfo::exists(artifactPath))
    {
        result.reason = QStringLiteral("Artifact missing.");
        return result;
    }
    const QString expected = result.sidecar.value(QStringLiteral("coefficientPayloadSha256")).toString();
    if (expected.size() != 64 || sha256OfFile(artifactPath) != expected)
    {
        result.reason = QStringLiteral("Normalization coefficient payload digest mismatch.");
        return result;
    }
    NormalizationDocumentRead read = readNormalizationCoefficients(
        artifactPath, normalizationArtifactIdentity(inputsFingerprint, settingsFingerprint));
    if (read.failure == NormalizationDocumentFailure::NewerSchema)
    {
        result.newerSchema = true;
        result.reason = QStringLiteral("The coefficient document was written by a newer version: %1").arg(read.message);
        return result;
    }
    if (!read.coefficients.has_value())
    {
        result.reason = QStringLiteral("The coefficient document is refused: %1").arg(read.message);
        return result;
    }
    result.reuse = true;
    result.reason = QStringLiteral("Inputs and settings match.");
    result.coefficients = std::move(read.coefficients);
    return result;
}

DocumentPublicationResult publishNormalizationArtifact(const QString& artifactPath,
                                                       const NormalizationCoefficientSet& set, const QString& identity,
                                                       QJsonObject sidecar, const std::stop_token stop,
                                                       const DocumentFileOperations& operations)
{
    const auto envelope = normalizationCoefficientsEnvelope(set, identity);
    if (!envelope.has_value())
    {
        DocumentPublicationResult refused;
        refused.failure = DocumentPublicationFailure::InvalidRequest;
        refused.message = QStringLiteral("The coefficients are not a set this version writes: %1")
                              .arg(checkNormalizationCoefficientSet(set));
        return refused;
    }
    const QByteArray payload = encodeDocumentEnvelope(*envelope);
    if (payload.isEmpty())
    {
        DocumentPublicationResult refused;
        refused.failure = DocumentPublicationFailure::InvalidRequest;
        refused.message = QStringLiteral("The coefficient document's envelope could not be written.");
        return refused;
    }
    const QByteArray sidecarBytes = [&]() {
        sidecar.insert(QStringLiteral("coefficientPayloadSha256"),
                       QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()));
        return QJsonDocument(sidecar).toJson(QJsonDocument::Compact);
    }();
    if (sidecarBytes.size() > schema::kMetadataSidecarMaxBytes)
    {
        DocumentPublicationResult refused;
        refused.failure = DocumentPublicationFailure::InvalidRequest;
        refused.message = QStringLiteral("Normalization metadata exceeds the %1-byte sidecar limit.")
                              .arg(schema::kMetadataSidecarMaxBytes);
        return refused;
    }
    if (!QDir().mkpath(QFileInfo(artifactPath).absolutePath()))
    {
        DocumentPublicationResult refused;
        refused.failure = DocumentPublicationFailure::CreateFailed;
        refused.message = QStringLiteral("The coefficients' directory could not be made.");
        return refused;
    }
    const DocumentPublicationResult published = publishDocumentBytes(artifactPath, payload, stop, operations);
    if (!published.ok())
    {
        return published;
    }
    return publishDocumentBytes(normalizationSidecarPath(artifactPath), sidecarBytes, stop, operations);
}

DocumentRecoveryReport recoverNormalizationArtifact(const QString& artifactPath)
{
    const QFileInfo info(artifactPath);
    const QString name = info.fileName();
    const QStringList leftoverPrefixes{name + QStringLiteral(".pending."), name + QStringLiteral(".rollback."),
                                       name + QStringLiteral(".meta.json.pending."),
                                       name + QStringLiteral(".meta.json.rollback.")};
    const QString leftoverLock = name + QStringLiteral(".publish.lock");
    return recoverDocumentDirectory(info.absolutePath(), [&](const QString& candidate) {
        const QString file = QFileInfo(candidate).fileName();
        if (file == leftoverLock)
        {
            return true;
        }
        for (const QString& prefix : leftoverPrefixes)
        {
            if (file.startsWith(prefix))
            {
                return true;
            }
        }
        return false;
    });
}

} // namespace ImageIntegration

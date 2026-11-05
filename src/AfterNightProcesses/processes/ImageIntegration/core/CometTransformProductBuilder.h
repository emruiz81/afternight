// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The comet-aligned transforms a comet registration records: every frame moved so that its comet lies where the
/// reference frame's does, on a non-sidereal plane the size of the reference frame.
///
/// A pure comet alignment has no star registration under it: each frame's transform is the translation from its comet's
/// position on the comet's reference plane - its own frame with its distortion undone - to the reference frame's, and
/// the frame's distortion travels beside it as the source distortion the warp undoes first.

#include "processes/ImageIntegration/core/CometGuidedAutoTracker.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsPersistenceTypes.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace an::ui
{
struct CometTransformProductRequest
{
    QString cometRunId;
    QString alignmentMode;
    std::vector<CometGuidedAutoTrackingFrame> frames;
    std::vector<CometGuidedAutoTrackingDiagnostic> diagnostics;
    std::optional<FrameId> referenceFrameId;
    std::optional<an::PixelCoordinate> referenceCometPosition;
    int canvasWidth = 0;
    int canvasHeight = 0;
    bool allowPredictedOnlyTransforms = true;
    bool requiresMaterializedOutputs = false;
    qint64 updatedAt = 0;
};

struct CometTransformProductResult
{
    bool succeeded = false;
    CometOutputPlaneRecord outputPlane;
    CometRoleReadinessRecord readiness;
    std::vector<CometTransformRoleRecord> transforms;
    QString message;
};

[[nodiscard]] inline QString cometTransformProductRole()
{
    return QStringLiteral("comet_aligned");
}

[[nodiscard]] inline QString cometTransformProductAlignmentModeFromRole(const CometTransformProductRequest&)
{
    return QStringLiteral("PureComet");
}

[[nodiscard]] inline bool cometTransformProductPointIsFinite(const an::PixelCoordinate& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] inline const CometGuidedAutoTrackingDiagnostic*
findCometTransformProductDiagnostic(const std::vector<CometGuidedAutoTrackingDiagnostic>& diagnostics,
                                    const FrameId& frame_id)
{
    const auto it =
        std::find_if(diagnostics.begin(), diagnostics.end(), [&](const CometGuidedAutoTrackingDiagnostic& diagnostic) {
            return diagnostic.originalFrameId == frame_id;
        });
    return it == diagnostics.end() ? nullptr : &(*it);
}

[[nodiscard]] inline std::optional<an::PixelCoordinate>
cometTransformProductReferencePlanePosition(const CometGuidedAutoTrackingDiagnostic& diagnostic,
                                            bool allow_predicted_only)
{
    if (diagnostic.status == CometGuidedAutoTrackingStatus::AcceptedObservation ||
        diagnostic.status == CometGuidedAutoTrackingStatus::Candidate)
    {
        if (cometTransformProductPointIsFinite(diagnostic.refinedReferencePlanePosition))
        {
            return diagnostic.refinedReferencePlanePosition;
        }
        return std::nullopt;
    }

    if (diagnostic.status == CometGuidedAutoTrackingStatus::PredictedOnly && allow_predicted_only)
    {
        if (cometTransformProductPointIsFinite(diagnostic.refinedReferencePlanePosition))
        {
            return diagnostic.refinedReferencePlanePosition;
        }
        if (cometTransformProductPointIsFinite(diagnostic.predictedReferencePlanePosition))
        {
            return diagnostic.predictedReferencePlanePosition;
        }
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<an::PixelCoordinate>
resolveCometTransformProductReferencePosition(const CometTransformProductRequest& request)
{
    if (request.referenceCometPosition.has_value() &&
        cometTransformProductPointIsFinite(request.referenceCometPosition.value()))
    {
        return request.referenceCometPosition.value();
    }

    if (request.referenceFrameId.has_value())
    {
        const CometGuidedAutoTrackingDiagnostic* diagnostic =
            findCometTransformProductDiagnostic(request.diagnostics, request.referenceFrameId.value());
        if (diagnostic != nullptr)
        {
            const std::optional<an::PixelCoordinate> reference_position =
                cometTransformProductReferencePlanePosition(*diagnostic, request.allowPredictedOnlyTransforms);
            if (reference_position.has_value())
            {
                return reference_position;
            }
        }
    }

    for (const CometGuidedAutoTrackingDiagnostic& diagnostic : request.diagnostics)
    {
        const std::optional<an::PixelCoordinate> reference_position =
            cometTransformProductReferencePlanePosition(diagnostic, request.allowPredictedOnlyTransforms);
        if (reference_position.has_value())
        {
            return reference_position;
        }
    }

    return std::nullopt;
}

[[nodiscard]] inline QString cometTransformProductOutputPlaneId(const CometTransformProductRequest& request)
{
    return QStringLiteral("%1:%2").arg(cometTransformProductRole(), request.cometRunId.trimmed());
}

[[nodiscard]] inline QString cometTransformProductSourceFingerprint(const CometTransformProductRequest& request,
                                                                    const QString& alignment_mode)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(request.cometRunId.toUtf8());
    hash.addData(alignment_mode.toUtf8());
    if (request.referenceFrameId.has_value())
    {
        hash.addData(request.referenceFrameId->canonicalPath().toUtf8());
    }
    if (request.referenceCometPosition.has_value())
    {
        hash.addData(QByteArray::number(request.referenceCometPosition->x, 'g', 17));
        hash.addData(QByteArray::number(request.referenceCometPosition->y, 'g', 17));
    }

    std::vector<QString> frame_ids;
    frame_ids.reserve(request.frames.size());
    for (const CometGuidedAutoTrackingFrame& frame : request.frames)
    {
        frame_ids.push_back(frame.context.originalFrameId.canonicalPath());
    }
    std::sort(frame_ids.begin(), frame_ids.end());
    for (const QString& frame_id : frame_ids)
    {
        hash.addData(frame_id.toUtf8());
    }
    return QString::fromLatin1(hash.result().toHex());
}

[[nodiscard]] inline QString cometTransformProductDescriptorJson(const CometTransformProductRequest& request,
                                                                 const QString& alignment_mode,
                                                                 const an::PixelCoordinate& reference_position)
{
    QJsonObject obj;
    // Schema 2: the comet position in engine pixel coordinates.
    obj.insert(QStringLiteral("schemaVersion"), 2);
    obj.insert(QStringLiteral("role"), cometTransformProductRole());
    obj.insert(QStringLiteral("alignmentMode"), alignment_mode);
    obj.insert(QStringLiteral("coordinateFrameKind"), QStringLiteral("CometReference"));
    obj.insert(QStringLiteral("nonSidereal"), true);
    obj.insert(QStringLiteral("referenceCometPosition"), stage_utils::pointToJsonArray(reference_position));
    if (request.referenceFrameId.has_value())
    {
        obj.insert(QStringLiteral("referenceFrameId"), request.referenceFrameId->canonicalPath());
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline CometOutputPlaneRecord
makeCometTransformProductOutputPlane(const CometTransformProductRequest& request, const QString& alignment_mode,
                                     const an::PixelCoordinate& reference_position)
{
    CometOutputPlaneRecord plane;
    plane.cometOutputPlaneId = cometTransformProductOutputPlaneId(request);
    plane.cometRunId = request.cometRunId;
    plane.role = cometTransformProductRole();
    plane.alignmentMode = alignment_mode;
    plane.coordinateFrameKind = QStringLiteral("CometReference");
    plane.canvasWidth = request.canvasWidth;
    plane.canvasHeight = request.canvasHeight;
    plane.skyWcsValid = false;
    plane.skyWcsInvalidReason = QStringLiteral("NonSiderealCometPlane: comet_aligned is a non-sidereal moving-object "
                                               "plane.");
    plane.descriptorJson = cometTransformProductDescriptorJson(request, alignment_mode, reference_position);
    plane.sourceFingerprint = cometTransformProductSourceFingerprint(request, alignment_mode);
    plane.updatedAt = request.updatedAt;
    return plane;
}

/// The translation that carries a frame's comet onto the reference frame's.
[[nodiscard]] inline an::Result<an::RegistrationTransform>
cometTransformProductTranslation(const an::PixelCoordinate& shift)
{
    return an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Similarity,
                                         .sourceToReference = {1.0, 0.0, shift.x, 0.0, 1.0, shift.y, 0.0, 0.0, 1.0}});
}

[[nodiscard]] inline QString
cometTransformProductSourceDistortionJson(const std::optional<an::SipDistortion>& source_distortion)
{
    if (!source_distortion.has_value())
    {
        return {};
    }
    return QString::fromUtf8(
        QJsonDocument(stage_utils::sipDistortionToJson(source_distortion.value())).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline CometTransformRoleRecord
makeFailedCometTransformProductRecord(const CometTransformProductRequest& request, const CometOutputPlaneRecord& plane,
                                      const CometGuidedAutoTrackingFrame& frame, const QString& reason)
{
    CometTransformRoleRecord record;
    record.cometRunId = request.cometRunId;
    record.frameId = frame.context.originalFrameId;
    record.role = cometTransformProductRole();
    record.cometOutputPlaneId = plane.cometOutputPlaneId;
    record.succeeded = false;
    record.failureReason = reason;
    record.updatedAt = request.updatedAt;
    return record;
}

[[nodiscard]] inline CometTransformRoleRecord makeSuccessfulCometTransformProductRecord(
    const CometTransformProductRequest& request, const CometOutputPlaneRecord& plane,
    const CometGuidedAutoTrackingFrame& frame, const an::RegistrationTransform& transform)
{
    CometTransformRoleRecord record;
    record.cometRunId = request.cometRunId;
    record.frameId = frame.context.originalFrameId;
    record.role = cometTransformProductRole();
    record.cometOutputPlaneId = plane.cometOutputPlaneId;
    record.transformJson = stage_utils::serializeTransformJson(
        transform, frame.mapping.sourceDistortion.has_value()
                       ? stage_utils::SourceDistortionProvenance::explicitModel(*frame.mapping.sourceDistortion)
                       : stage_utils::SourceDistortionProvenance::inheritMetadata());
    record.sourceDistortionJson = cometTransformProductSourceDistortionJson(frame.mapping.sourceDistortion);
    record.succeeded = true;
    record.updatedAt = request.updatedAt;
    return record;
}

[[nodiscard]] inline CometTransformRoleRecord
buildCometTransformProductRecord(const CometTransformProductRequest& request, const CometOutputPlaneRecord& plane,
                                 const CometGuidedAutoTrackingFrame& frame,
                                 const an::PixelCoordinate& reference_position)
{
    if (frame.context.status != CometTrackingFrameContextStatus::Ready)
    {
        const QString reason = frame.context.message.isEmpty() ? QStringLiteral("Comet frame context is not ready.")
                                                               : frame.context.message;
        return makeFailedCometTransformProductRecord(request, plane, frame, reason);
    }

    const CometGuidedAutoTrackingDiagnostic* diagnostic =
        findCometTransformProductDiagnostic(request.diagnostics, frame.context.originalFrameId);
    if (diagnostic == nullptr)
    {
        return makeFailedCometTransformProductRecord(request, plane, frame,
                                                     QStringLiteral("No comet diagnostic is available for frame."));
    }

    const std::optional<an::PixelCoordinate> frame_reference_position =
        cometTransformProductReferencePlanePosition(*diagnostic, request.allowPredictedOnlyTransforms);
    if (!frame_reference_position.has_value())
    {
        const QString reason = diagnostic->status == CometGuidedAutoTrackingStatus::PredictedOnly
                                   ? QStringLiteral("Predicted-only comet transform was rejected by policy.")
                                   : QStringLiteral("Comet diagnostic cannot produce a transform for frame.");
        return makeFailedCometTransformProductRecord(request, plane, frame, reason);
    }

    const an::Result<an::RegistrationTransform> translation = cometTransformProductTranslation(an::PixelCoordinate{
        reference_position.x - frame_reference_position->x, reference_position.y - frame_reference_position->y});
    if (!translation.hasValue())
    {
        return makeFailedCometTransformProductRecord(request, plane, frame,
                                                     QString::fromUtf8(translation.status().message()));
    }
    return makeSuccessfulCometTransformProductRecord(request, plane, frame, *translation);
}

[[nodiscard]] inline CometRoleReadinessRecord
makeCometTransformProductReadiness(const CometTransformProductRequest& request, const CometOutputPlaneRecord& plane,
                                   const std::vector<CometTransformRoleRecord>& transforms)
{
    CometRoleReadinessRecord readiness;
    readiness.cometRunId = request.cometRunId;
    readiness.role = cometTransformProductRole();
    readiness.selectedFrameCount = static_cast<int>(request.frames.size());
    readiness.outputPlaneStatus = !plane.cometOutputPlaneId.isEmpty() && plane.canvasWidth > 0 && plane.canvasHeight > 0
                                      ? QStringLiteral("Ready")
                                      : QStringLiteral("Invalid");
    readiness.requiresMaterializedOutputs = request.requiresMaterializedOutputs;
    readiness.updatedAt = request.updatedAt;

    for (const CometTransformRoleRecord& transform : transforms)
    {
        if (transform.succeeded)
        {
            ++readiness.successfulTransformCount;
        }
        else
        {
            ++readiness.failedTransformCount;
            if (readiness.failureReason.isEmpty())
            {
                readiness.failureReason = transform.failureReason;
            }
        }
    }

    readiness.missingTransformCount = (std::max)(0, readiness.selectedFrameCount - static_cast<int>(transforms.size()));
    if (request.requiresMaterializedOutputs)
    {
        readiness.missingOutputCount = readiness.successfulTransformCount;
    }

    if (readiness.outputPlaneStatus != QStringLiteral("Ready"))
    {
        readiness.readinessStatus = QStringLiteral("Failed");
        if (readiness.failureReason.isEmpty())
        {
            readiness.failureReason = QStringLiteral("Comet output plane is invalid.");
        }
        return readiness;
    }
    if (readiness.failedTransformCount > 0 || readiness.missingTransformCount > 0)
    {
        readiness.readinessStatus = QStringLiteral("Failed");
        return readiness;
    }
    if (request.requiresMaterializedOutputs)
    {
        readiness.readinessStatus = QStringLiteral("OutputMissing");
        readiness.failureReason = QStringLiteral("comet_aligned registered outputs have not been materialized.");
        return readiness;
    }

    readiness.readinessStatus = QStringLiteral("TransformReady");
    return readiness;
}

[[nodiscard]] inline CometTransformProductResult
buildCometTransformProducts(const CometTransformProductRequest& request)
{
    CometTransformProductResult result;
    if (request.cometRunId.trimmed().isEmpty())
    {
        result.message = QStringLiteral("Comet run id is required for transform products.");
        return result;
    }
    if (request.frames.empty())
    {
        result.message = QStringLiteral("No comet tracking frames are available for transform products.");
        return result;
    }

    const std::optional<an::PixelCoordinate> reference_position =
        resolveCometTransformProductReferencePosition(request);
    if (!reference_position.has_value())
    {
        result.message = QStringLiteral("No comet reference position is available for transform products.");
        return result;
    }

    const QString alignment_mode = cometTransformProductAlignmentModeFromRole(request);
    result.outputPlane = makeCometTransformProductOutputPlane(request, alignment_mode, reference_position.value());
    result.transforms.reserve(request.frames.size());
    for (const CometGuidedAutoTrackingFrame& frame : request.frames)
    {
        result.transforms.push_back(
            buildCometTransformProductRecord(request, result.outputPlane, frame, reference_position.value()));
    }

    result.readiness = makeCometTransformProductReadiness(request, result.outputPlane, result.transforms);
    result.succeeded = result.readiness.readinessStatus == QStringLiteral("TransformReady") ||
                       result.readiness.readinessStatus == QStringLiteral("Ready");
    result.message = result.succeeded ? QStringLiteral("Comet transform products are transform-ready.")
                                      : result.readiness.failureReason;
    return result;
}
} // namespace an::ui

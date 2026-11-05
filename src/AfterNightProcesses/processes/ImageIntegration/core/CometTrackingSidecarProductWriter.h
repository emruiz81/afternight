// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/core/CometTransformProductBuilder.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <QString>

#include <vector>

namespace an::ui
{
struct CometTrackingSidecarProductWriteRequest
{
    CometTrackingRunRecord run;
    CometTrackingRunSummaryRecord summary;
    std::vector<CometTrackingPointRecord> points;
    CometTransformProductResult transformProducts;
    std::vector<CometRegisteredOutputRecord> registeredOutputs;
};

struct CometTrackingSidecarProductWriteResult
{
    bool succeeded = false;
    QString message;
    QString cometRunId;
    QString persistedStatus;
};

inline void assignCometSidecarProductError(QString* error, const QString& value)
{
    if (error != nullptr)
    {
        *error = value;
    }
}

[[nodiscard]] inline QString cometTrackingSidecarProductRunStatus(const CometTransformProductResult& products)
{
    if (products.readiness.readinessStatus == QStringLiteral("TransformReady") ||
        products.readiness.readinessStatus == QStringLiteral("Ready"))
    {
        return QStringLiteral("RegistrationReady");
    }
    return QStringLiteral("Candidate");
}

[[nodiscard]] inline bool cometTrackingSidecarProductRecordRunIdMatches(const QString& expectedRunId,
                                                                        const QString& actualRunId,
                                                                        const QString& recordLabel, QString* error)
{
    if (!actualRunId.trimmed().isEmpty() && actualRunId != expectedRunId)
    {
        assignCometSidecarProductError(error, QStringLiteral("%1 belongs to comet run '%2', expected '%3'.")
                                                  .arg(recordLabel, actualRunId, expectedRunId));
        return false;
    }
    return true;
}

[[nodiscard]] inline bool
validateCometTrackingSidecarProductWriteRequest(const CometTrackingSidecarProductWriteRequest& request, QString* error)
{
    assignCometSidecarProductError(error, QString());

    const QString comet_run_id = request.run.cometRunId.trimmed();
    if (comet_run_id.isEmpty())
    {
        assignCometSidecarProductError(error, QStringLiteral("Comet run id is required."));
        return false;
    }
    if (!request.registeredOutputs.empty())
    {
        assignCometSidecarProductError(
            error, QStringLiteral(
                       "comet_registered_outputs registered output rows are owned by alignment/materialization, not "
                       "registration."));
        return false;
    }
    if (request.transformProducts.outputPlane.cometOutputPlaneId.trimmed().isEmpty())
    {
        assignCometSidecarProductError(error, QStringLiteral("Comet output plane is required."));
        return false;
    }
    if (request.transformProducts.transforms.empty())
    {
        assignCometSidecarProductError(error, QStringLiteral("At least one comet transform role is required."));
        return false;
    }
    if (!cometTrackingSidecarProductRecordRunIdMatches(comet_run_id, request.transformProducts.outputPlane.cometRunId,
                                                       QStringLiteral("Comet output plane"), error))
    {
        return false;
    }
    if (!cometTrackingSidecarProductRecordRunIdMatches(comet_run_id, request.transformProducts.readiness.cometRunId,
                                                       QStringLiteral("Comet readiness row"), error))
    {
        return false;
    }
    if (!cometTrackingSidecarProductRecordRunIdMatches(comet_run_id, request.summary.cometRunId,
                                                       QStringLiteral("Comet run summary"), error))
    {
        return false;
    }

    for (const CometTrackingPointRecord& point : request.points)
    {
        if (!cometTrackingSidecarProductRecordRunIdMatches(comet_run_id, point.cometRunId,
                                                           QStringLiteral("Comet tracking point"), error))
        {
            return false;
        }
    }
    for (const CometTransformRoleRecord& transform : request.transformProducts.transforms)
    {
        if (!cometTrackingSidecarProductRecordRunIdMatches(comet_run_id, transform.cometRunId,
                                                           QStringLiteral("Comet transform role"), error))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline CometTrackingSidecarProductWriteResult
writeCometTrackingSidecarProducts(RegistrationMetricsWriter& writer,
                                  const CometTrackingSidecarProductWriteRequest& request, QString* error = nullptr)
{
    CometTrackingSidecarProductWriteResult result;
    result.cometRunId = request.run.cometRunId;
    result.persistedStatus = cometTrackingSidecarProductRunStatus(request.transformProducts);

    QString validation_error;
    if (!validateCometTrackingSidecarProductWriteRequest(request, &validation_error))
    {
        result.message = validation_error;
        assignCometSidecarProductError(error, validation_error);
        return result;
    }

    CometTrackingRunRecord run = request.run;
    run.cometRunId = result.cometRunId;
    run.status = result.persistedStatus;
    if (!request.transformProducts.outputPlane.alignmentMode.trimmed().isEmpty())
    {
        run.mode = request.transformProducts.outputPlane.alignmentMode;
    }

    CometTrackingRunSummaryRecord summary = request.summary;
    summary.cometRunId = result.cometRunId;

    CometOutputPlaneRecord output_plane = request.transformProducts.outputPlane;
    output_plane.cometRunId = result.cometRunId;

    std::vector<CometTrackingPointRecord> points = request.points;
    for (CometTrackingPointRecord& point : points)
    {
        point.cometRunId = result.cometRunId;
    }

    std::vector<CometTransformRoleRecord> transforms = request.transformProducts.transforms;
    for (CometTransformRoleRecord& transform : transforms)
    {
        transform.cometRunId = result.cometRunId;
    }

    CometRoleReadinessRecord readiness = request.transformProducts.readiness;
    readiness.cometRunId = result.cometRunId;

    QString write_error;
    const auto fail = [&](const QString& message) {
        result.message = message;
        assignCometSidecarProductError(error, message);
        return result;
    };

    if (!writer.upsertCometTrackingRun(run, &write_error))
    {
        return fail(write_error);
    }
    if (!writer.upsertCometTrackingRunSummary(summary, &write_error))
    {
        return fail(write_error);
    }
    for (const CometTrackingPointRecord& point : points)
    {
        if (!writer.upsertCometTrackingPoint(point, &write_error))
        {
            return fail(write_error);
        }
    }
    if (!writer.upsertCometOutputPlane(output_plane, &write_error))
    {
        return fail(write_error);
    }
    for (const CometTransformRoleRecord& transform : transforms)
    {
        if (!writer.upsertCometTransformRole(transform, &write_error))
        {
            return fail(write_error);
        }
    }
    if (!writer.upsertCometRoleReadiness(readiness, &write_error))
    {
        return fail(write_error);
    }

    result.succeeded = true;
    result.message = QStringLiteral("Comet sidecar transform products were persisted.");
    assignCometSidecarProductError(error, QString());
    return result;
}
} // namespace an::ui

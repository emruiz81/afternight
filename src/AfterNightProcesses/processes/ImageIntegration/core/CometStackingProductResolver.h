// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace an::ui
{
enum class CometStackingProductKind
{
    None,
    RegisteredOutputs,
    TransformRoles,
};

struct CometStackingProductResolveRequest
{
    QString databasePath;
    QString groupKey;
    QString registrationHash;
    QString lightFilesHash;
    QString calibrationHash;
    QString resolvedReferenceFrameId;
    QString stackingMode = QStringLiteral("Stars");
    QString role = QStringLiteral("comet_aligned");
    bool drizzle = false;
    std::vector<FrameId> frameIds;
    WorkflowExecutionCounterAccumulator* counters = nullptr;
};

struct CometStackingProductResolveResult
{
    bool available = false;
    QString cometRunId;
    int settingsVersion = 0;
    QString role = QStringLiteral("comet_aligned");
    CometStackingProductKind kind = CometStackingProductKind::None;
    std::vector<CometRegisteredOutputRecord> registeredOutputs;
    std::unordered_map<QString, FileRevisionStamp> registeredOutputRevisions;
    std::vector<CometTransformRoleRecord> transforms;
    std::optional<CometOutputPlaneRecord> outputPlane;
    std::optional<CometRoleReadinessRecord> readiness;
    QString errorMessage;
};

struct StarsPlusCometExperimentalProductResolveResult
{
    bool available = false;
    std::vector<RegistrationMetricRecord> starBaseProducts;
    std::unordered_map<QString, FileRevisionStamp> starBaseRevisions;
    CometStackingProductResolveResult cometProducts;
    QString errorMessage;
};

namespace detail
{
[[nodiscard]] inline CometStackingProductResolveResult cometStackingUnavailable(QString role, QString message)
{
    CometStackingProductResolveResult result;
    result.role = std::move(role);
    result.errorMessage = std::move(message);
    return result;
}

[[nodiscard]] inline QString cometStackingRole(const CometStackingProductResolveRequest& request)
{
    const QString role = request.role.trimmed();
    return role.isEmpty() ? QStringLiteral("comet_aligned") : role;
}

[[nodiscard]] inline bool cometStackingReadinessHasTransformProducts(const QString& readinessStatus)
{
    return readinessStatus == QStringLiteral("TransformReady") || readinessStatus == QStringLiteral("OutputReady") ||
           readinessStatus == QStringLiteral("Ready");
}

[[nodiscard]] inline bool cometStackingReadinessHasMaterializedOutputs(const QString& readinessStatus)
{
    return readinessStatus == QStringLiteral("OutputReady") || readinessStatus == QStringLiteral("Ready");
}

[[nodiscard]] inline QString cometStackingFrameList(const QStringList& frameIds)
{
    QStringList sorted = frameIds;
    sorted.sort(Qt::CaseSensitive);
    return sorted.join(QStringLiteral(", "));
}

[[nodiscard]] inline std::optional<FileRevisionStamp>
captureRegisteredOutputRevision(const QString& outputPath, const qint64 expectedSize, const qint64 expectedModifiedAtMs,
                                const QString& expectedIdentityToken)
{
    const auto revision = ArtifactTracker::captureFileRevision(outputPath);
    if (!revision.has_value() || expectedSize < 0 || expectedModifiedAtMs < 0 || expectedIdentityToken.isEmpty() ||
        revision->sizeBytes != expectedSize || revision->modifiedAtMs != expectedModifiedAtMs ||
        revision->identityToken != expectedIdentityToken)
    {
        return std::nullopt;
    }
    return revision;
}

[[nodiscard]] inline StarsPlusCometExperimentalProductResolveResult
starsPlusCometUnavailable(QString message, CometStackingProductResolveResult cometProducts = {})
{
    StarsPlusCometExperimentalProductResolveResult result;
    result.cometProducts = std::move(cometProducts);
    result.errorMessage = std::move(message);
    return result;
}

} // namespace detail

[[nodiscard]] inline CometStackingProductResolveResult
resolveCometStackingProducts(const CometStackingProductResolveRequest& request)
{
    const QString role = detail::cometStackingRole(request);

    if (ImageIntegration::isStarsPlusCometExperimentalStackingMode(request.stackingMode))
    {
        return detail::cometStackingUnavailable(
            role, QStringLiteral("%1 stacking requires dual star-base and comet product validation.")
                      .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken()));
    }

    if (!ImageIntegration::usesCometStackingMode(request.stackingMode))
    {
        CometStackingProductResolveResult result;
        result.available = true;
        result.role = role;
        return result;
    }

    if (request.frameIds.empty())
    {
        return detail::cometStackingUnavailable(
            role, QStringLiteral("Comet stacking requires an explicit list of consumed frames."));
    }

    RegistrationMetricsStateLoader loader(request.databasePath, QStringLiteral("CometStackingProductResolver"),
                                          request.counters);
    QString error;
    if (!loader.open(&error))
    {
        return detail::cometStackingUnavailable(
            role, QStringLiteral("Failed to open registration metrics for comet stacking: %1").arg(error));
    }

    const auto run =
        loader.loadActiveCometRunForContext(request.groupKey, request.registrationHash, request.lightFilesHash,
                                            request.calibrationHash, request.resolvedReferenceFrameId, &error);
    if (!run.has_value())
    {
        return detail::cometStackingUnavailable(
            role,
            QStringLiteral("Comet stacking requires an active comet tracking run for this exact registration context. "
                           "Run comet tracking and registration before stacking."));
    }

    const auto readiness = loader.loadCometRoleReadiness(run->cometRunId, role, &error);
    if (!readiness.has_value())
    {
        return detail::cometStackingUnavailable(
            role, QStringLiteral("Comet stacking requires %1 role readiness for active run '%2': %3")
                      .arg(role, run->cometRunId, error));
    }

    if (readiness->outputPlaneStatus != QStringLiteral("Ready"))
    {
        return detail::cometStackingUnavailable(
            role, QStringLiteral("Comet stacking requires a ready %1 output plane for active run '%2' (status: %3).")
                      .arg(role, run->cometRunId, readiness->outputPlaneStatus));
    }

    const auto outputPlane = loader.loadCometOutputPlaneForRole(run->cometRunId, role, &error);
    if (!outputPlane.has_value())
    {
        return detail::cometStackingUnavailable(
            role, QStringLiteral("Comet stacking requires a %1 output plane for active run '%2': %3")
                      .arg(role, run->cometRunId, error));
    }

    CometStackingProductResolveResult result;
    result.available = true;
    result.cometRunId = run->cometRunId;
    result.settingsVersion = run->settingsVersion;
    result.role = role;
    result.outputPlane = outputPlane.value();
    result.readiness = readiness.value();
    if (request.drizzle)
    {
        if (!detail::cometStackingReadinessHasTransformProducts(readiness->readinessStatus))
        {
            return detail::cometStackingUnavailable(
                role, QStringLiteral("Comet drizzle stacking requires %1 transforms for active run '%2' (readiness: "
                                     "%3).")
                          .arg(role, run->cometRunId, readiness->readinessStatus));
        }

        std::vector<CometTransformRoleRecord> transforms;
        if (!loader.loadCometTransformRoles(run->cometRunId, role, transforms, &error))
        {
            return detail::cometStackingUnavailable(
                role, QStringLiteral("Comet drizzle stacking failed to load %1 transforms for active run '%2': %3")
                          .arg(role, run->cometRunId, error));
        }

        std::unordered_map<QString, CometTransformRoleRecord> transformsByFrame;
        transformsByFrame.reserve(transforms.size());
        for (const auto& transform : transforms)
        {
            transformsByFrame[transform.frameId.canonicalPath()] = transform;
        }

        QStringList missingFrames;
        QStringList failedFrames;
        for (const auto& frameId : request.frameIds)
        {
            const QString frameKey = frameId.canonicalPath();
            const auto it = transformsByFrame.find(frameKey);
            if (it == transformsByFrame.end())
            {
                missingFrames.push_back(frameKey);
                continue;
            }

            if (!it->second.succeeded)
            {
                failedFrames.push_back(it->second.failureReason.trimmed().isEmpty()
                                           ? frameKey
                                           : QStringLiteral("%1 (%2)").arg(frameKey, it->second.failureReason));
                continue;
            }

            if (it->second.cometOutputPlaneId != outputPlane->cometOutputPlaneId)
            {
                failedFrames.push_back(
                    QStringLiteral("%1 (unexpected output plane '%2')").arg(frameKey, it->second.cometOutputPlaneId));
                continue;
            }

            result.transforms.push_back(it->second);
        }

        if (!missingFrames.empty() || !failedFrames.empty())
        {
            QStringList details;
            if (!missingFrames.empty())
            {
                details.push_back(QStringLiteral("missing: %1").arg(detail::cometStackingFrameList(missingFrames)));
            }
            if (!failedFrames.empty())
            {
                details.push_back(QStringLiteral("failed: %1").arg(detail::cometStackingFrameList(failedFrames)));
            }
            return detail::cometStackingUnavailable(
                role,
                QStringLiteral("Comet drizzle stacking requires successful %1 transforms for every consumed frame; "
                               "%2. Exclude these frames or rerun comet tracking.")
                    .arg(role, details.join(QStringLiteral("; "))));
        }

        result.kind = CometStackingProductKind::TransformRoles;
        return result;
    }

    if (!detail::cometStackingReadinessHasMaterializedOutputs(readiness->readinessStatus))
    {
        return detail::cometStackingUnavailable(
            role,
            QStringLiteral("Comet non-drizzle stacking requires materialized %1 registered outputs for active run "
                           "'%2' (readiness: %3). Run comet alignment/materialization before stacking.")
                .arg(role, run->cometRunId, readiness->readinessStatus));
    }

    QStringList missingFrames;
    QStringList failedFrames;
    for (const auto& frameId : request.frameIds)
    {
        const auto output = loader.loadCometRegisteredOutput(run->cometRunId, frameId, role, &error);
        if (!output.has_value())
        {
            missingFrames.push_back(frameId.canonicalPath());
            continue;
        }

        if (!output->succeeded)
        {
            failedFrames.push_back(output->failureReason.trimmed().isEmpty()
                                       ? frameId.canonicalPath()
                                       : QStringLiteral("%1 (%2)").arg(frameId.canonicalPath(), output->failureReason));
            continue;
        }

        if (output->cometOutputPlaneId != outputPlane->cometOutputPlaneId)
        {
            failedFrames.push_back(QStringLiteral("%1 (unexpected output plane '%2')")
                                       .arg(frameId.canonicalPath(), output->cometOutputPlaneId));
            continue;
        }

        const auto revision = detail::captureRegisteredOutputRevision(
            output->outputPath, output->outputSizeBytes, output->outputModifiedAtMs, output->outputIdentityToken);
        if (output->outputPath.trimmed().isEmpty() || !revision.has_value())
        {
            failedFrames.push_back(QStringLiteral("%1 (missing %2 registered output file '%3')")
                                       .arg(frameId.canonicalPath(), role, output->outputPath));
            continue;
        }

        result.registeredOutputs.push_back(output.value());
        result.registeredOutputRevisions[frameId.identityKey()] = *revision;
    }

    if (!missingFrames.empty() || !failedFrames.empty())
    {
        QStringList details;
        if (!missingFrames.empty())
        {
            details.push_back(QStringLiteral("missing rows: %1").arg(detail::cometStackingFrameList(missingFrames)));
        }
        if (!failedFrames.empty())
        {
            details.push_back(QStringLiteral("failed files: %1").arg(detail::cometStackingFrameList(failedFrames)));
        }
        return detail::cometStackingUnavailable(
            role,
            QStringLiteral("Comet non-drizzle stacking requires successful %1 registered outputs on disk for every "
                           "consumed frame; %2. Run comet alignment/materialization before stacking.")
                .arg(role, details.join(QStringLiteral("; "))));
    }

    result.kind = CometStackingProductKind::RegisteredOutputs;
    return result;
}

[[nodiscard]] inline StarsPlusCometExperimentalProductResolveResult
resolveStarsPlusCometExperimentalProducts(const CometStackingProductResolveRequest& request)
{
    if (!ImageIntegration::isStarsPlusCometExperimentalStackingMode(request.stackingMode))
    {
        StarsPlusCometExperimentalProductResolveResult result;
        result.available = true;
        return result;
    }

    if (request.frameIds.empty())
    {
        return detail::starsPlusCometUnavailable(
            QStringLiteral("%1 stacking requires an explicit list of consumed frames.")
                .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken()));
    }

    CometStackingProductResolveRequest cometRequest = request;
    cometRequest.stackingMode = QStringLiteral("Comet");
    cometRequest.role = detail::cometStackingRole(request);
    const auto cometProducts = resolveCometStackingProducts(cometRequest);
    const CometStackingProductKind requiredKind =
        request.drizzle ? CometStackingProductKind::TransformRoles : CometStackingProductKind::RegisteredOutputs;
    if (!cometProducts.available || cometProducts.kind != requiredKind)
    {
        const QString fallback = request.drizzle ? QStringLiteral("requires comet_aligned transform products")
                                                 : QStringLiteral("requires comet_aligned registered outputs");
        const QString detail = cometProducts.errorMessage.trimmed().isEmpty() ? fallback : cometProducts.errorMessage;
        return detail::starsPlusCometUnavailable(
            QStringLiteral("%1 stacking requires pure comet products for the same light group: %2")
                .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken(), detail),
            cometProducts);
    }

    RegistrationMetricsStateLoader loader(
        request.databasePath, QStringLiteral("StarsPlusCometExperimentalProductResolver"), request.counters);
    QString error;
    if (!loader.open(&error))
    {
        return detail::starsPlusCometUnavailable(
            QStringLiteral("Failed to open registration metrics for star base products: %1").arg(error), cometProducts);
    }

    const auto groupState = loader.loadGroupState(request.groupKey, cometProducts.settingsVersion, &error);
    if (!groupState.has_value())
    {
        return detail::starsPlusCometUnavailable(
            QStringLiteral("%1 stacking requires star base products for settings version %2 in the same light group: "
                           "%3")
                .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken())
                .arg(cometProducts.settingsVersion)
                .arg(error),
            cometProducts);
    }

    QStringList staleReasons;
    if (groupState->registrationHash != request.registrationHash)
    {
        staleReasons.push_back(QStringLiteral("registration hash mismatch"));
    }
    if (groupState->lightFilesHash != request.lightFilesHash)
    {
        staleReasons.push_back(QStringLiteral("light files hash mismatch"));
    }
    if (groupState->calibrationHash != request.calibrationHash)
    {
        staleReasons.push_back(QStringLiteral("calibration hash mismatch"));
    }
    if (groupState->referenceFrameId != request.resolvedReferenceFrameId)
    {
        staleReasons.push_back(QStringLiteral("reference frame mismatch"));
    }
    if (!staleReasons.empty())
    {
        return detail::starsPlusCometUnavailable(
            QStringLiteral("%1 stacking requires fresh star base products for the same light group; stale context: %2.")
                .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken(),
                     staleReasons.join(QStringLiteral(", "))),
            cometProducts);
    }

    std::vector<RegistrationMetricRecord> metrics;
    if (!loader.loadMetricRecords(groupState->groupKey, groupState->settingsVersion, metrics, &error))
    {
        return detail::starsPlusCometUnavailable(
            QStringLiteral("Failed to load star base products for %1 stacking: %2")
                .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken(), error),
            cometProducts);
    }

    std::unordered_map<QString, RegistrationMetricRecord> metricsByFrame;
    metricsByFrame.reserve(metrics.size());
    for (const auto& metric : metrics)
    {
        metricsByFrame[metric.frameId.identityKey()] = metric;
    }

    StarsPlusCometExperimentalProductResolveResult result;
    result.cometProducts = cometProducts;
    QStringList missingFrames;
    QStringList failedFrames;
    result.starBaseProducts.reserve(request.frameIds.size());
    for (const auto& frameId : request.frameIds)
    {
        const QString frameKey = frameId.canonicalPath();
        const auto it = metricsByFrame.find(frameId.identityKey());
        if (it == metricsByFrame.end())
        {
            missingFrames.push_back(frameKey);
            continue;
        }

        const auto& product = it->second;
        if (!product.metrics.succeeded)
        {
            failedFrames.push_back(QStringLiteral("%1 (registration failed)").arg(frameKey));
            continue;
        }

        if (request.drizzle)
        {
            if (product.transformJson.trimmed().isEmpty())
            {
                failedFrames.push_back(QStringLiteral("%1 (missing star base transform)").arg(frameKey));
                continue;
            }
        }
        else if (const auto revision =
                     detail::captureRegisteredOutputRevision(product.outputPath, product.outputSizeBytes,
                                                             product.outputModifiedAtMs, product.outputIdentityToken);
                 product.outputPath.trimmed().isEmpty() || !revision.has_value())
        {
            failedFrames.push_back(
                QStringLiteral("%1 (missing star base registered output file '%2')").arg(frameKey, product.outputPath));
            continue;
        }
        else
        {
            result.starBaseRevisions[frameId.identityKey()] = *revision;
        }

        result.starBaseProducts.push_back(product);
    }

    if (!missingFrames.empty() || !failedFrames.empty())
    {
        QStringList details;
        if (!missingFrames.empty())
        {
            details.push_back(QStringLiteral("missing: %1").arg(detail::cometStackingFrameList(missingFrames)));
        }
        if (!failedFrames.empty())
        {
            details.push_back(QStringLiteral("failed or stale: %1").arg(detail::cometStackingFrameList(failedFrames)));
        }
        return detail::starsPlusCometUnavailable(
            QStringLiteral("%1 stacking requires complete star base products for every consumed frame; %2.")
                .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken(),
                     details.join(QStringLiteral("; "))),
            cometProducts);
    }

    result.available = true;
    return result;
}
} // namespace an::ui

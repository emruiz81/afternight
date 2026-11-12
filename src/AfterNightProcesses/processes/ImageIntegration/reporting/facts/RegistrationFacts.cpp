// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"

#include <QFileInfo>
#include <QMap>

#include <algorithm>
#include <utility>

/**
 * @file RegistrationFacts.cpp
 * @brief The registration groups of a run: the reference, the transform, the corrections, the comet fits and the
 * catalog sources kept for the colour calibration.
 */

namespace ImageIntegration::Reporting
{
[[nodiscard]] bool isAutoToken(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized.isEmpty() || normalized == QStringLiteral("auto");
}

[[nodiscard]] QString registrationBaseTransformDisplayName(const RegistrationSettings& settings)
{
    const QString trimmed = settings.transformation.trimmed();
    return trimmed.isEmpty() ? reportText("Homography") : trimmed;
}

[[nodiscard]] QString transformKindDisplayName(const QString& kind)
{
    const QString normalized = kind.trimmed().toLower();
    if (normalized == QStringLiteral("similarity"))
    {
        return reportText("Similarity");
    }
    if (normalized == QStringLiteral("affine"))
    {
        return reportText("Affine");
    }
    if (normalized == QStringLiteral("homography"))
    {
        return reportText("Homography");
    }
    if (normalized == QStringLiteral("polynomial2"))
    {
        return reportText("Polynomial 2");
    }
    if (normalized == QStringLiteral("polynomial3"))
    {
        return reportText("Polynomial 3");
    }
    if (normalized == QStringLiteral("polynomial4"))
    {
        return reportText("Polynomial 4");
    }
    return kind.trimmed();
}

[[nodiscard]] bool isPolynomialTransformKind(const QString& kind)
{
    const QString normalized = kind.trimmed().toLower();
    return normalized == QStringLiteral("polynomial2") || normalized == QStringLiteral("polynomial3") ||
           normalized == QStringLiteral("polynomial4");
}

[[nodiscard]] AutoTransformResolution resolveAutoTransformDetail(const RegistrationSettings& settings,
                                                                 const QSet<QString>& metricKinds)
{
    const QString base = registrationBaseTransformDisplayName(settings);
    QStringList polynomialKinds;
    QStringList linearKinds;

    for (const QString& kind : metricKinds)
    {
        const QString display = transformKindDisplayName(kind);
        if (display.isEmpty())
        {
            continue;
        }
        if (isPolynomialTransformKind(kind))
        {
            polynomialKinds << display;
        }
        else
        {
            linearKinds << display;
        }
    }

    polynomialKinds.removeDuplicates();
    polynomialKinds.sort(Qt::CaseInsensitive);
    linearKinds.removeDuplicates();
    linearKinds.sort(Qt::CaseInsensitive);

    if (!polynomialKinds.isEmpty())
    {
        return {reportText("%1 + %2").arg(base, polynomialKinds.join(QStringLiteral(" / "))),
                reportText("Auto requested; metrics show %1 was used").arg(polynomialKinds.join(QStringLiteral(", ")))};
    }
    if (!linearKinds.isEmpty())
    {
        return {linearKinds.join(QStringLiteral(" / ")),
                reportText("Auto requested; metrics show %1 was used").arg(linearKinds.join(QStringLiteral(", ")))};
    }

    return {reportText("%1 + adaptive Polynomial 2-4").arg(base),
            reportText("Auto starts with %1 and tries Polynomial 2-4 only when the fit improves").arg(base)};
}

[[nodiscard]] QString registrationTransformDisplayName(const RegistrationSettings& settings,
                                                       const QString& resolvedAutoDetail)
{
    const QString method =
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(settings.registrationMethod);
    if (method == QStringLiteral("phase_correlation"))
    {
        return reportText("Phase Correlation");
    }
    if (method == QStringLiteral("manual_points"))
    {
        return reportText("Manual Points");
    }
    if (method == QStringLiteral("comet_tracking"))
    {
        return reportText("Comet Tracking");
    }

    const QString transform_mode = settings.transformMode.trimmed().toLower();
    if (transform_mode == QStringLiteral("auto"))
    {
        const QString detail =
            resolvedAutoDetail.trimmed().isEmpty()
                ? reportText("%1 + adaptive Polynomial 2-4").arg(registrationBaseTransformDisplayName(settings))
                : resolvedAutoDetail.trimmed();
        return reportText("Auto (%1)").arg(detail);
    }

    // The mode decides, as the star matcher reads it.
    const QString selection = ImageIntegration::RegistrationSettingsUtils::transformSelectionKey(
        settings.transformMode, settings.transformOrder, settings.transformation);
    if (selection.startsWith(QStringLiteral("polynomial")))
    {
        return reportText("Polynomial %1").arg(settings.transformOrder);
    }

    return registrationBaseTransformDisplayName(settings);
}

[[nodiscard]] QSet<QString> successfulTransformKinds(const QHash<QString, RegistrationMetricRow>& metricsByFrame)
{
    QSet<QString> kinds;
    for (auto it = metricsByFrame.cbegin(); it != metricsByFrame.cend(); ++it)
    {
        const RegistrationMetricRow& row = it.value();
        if (row.succeeded && !row.transformKind.trimmed().isEmpty())
        {
            kinds.insert(row.transformKind.trimmed().toLower());
        }
    }
    return kinds;
}

[[nodiscard]] AutoTransformResolution
resolveAutoTransformDetail(const WorkflowManifest::RegistrationGroup& registrationGroup, const QString& metricsDbPath,
                           WorkflowExecutionCounterAccumulator* counters)
{
    return resolveAutoTransformDetail(registrationGroup.settings,
                                      successfulTransformKinds(loadRegistrationMetricsForGroup(
                                          metricsDbPath, registrationGroup.groupKey.toString(), counters)));
}

namespace
{
[[nodiscard]] ReportCometRun cometRunOf(const CometReportRun& stored)
{
    ReportCometRun run;
    run.status = stored.run.status.trimmed();
    run.mode = stored.run.mode.trimmed();
    if (stored.summary.has_value())
    {
        run.fitModel = stored.summary->fitModel;
        run.rmsPixels = stored.summary->rmsPixels;
        run.largestResidualPixels = stored.summary->maxResidualPixels;
        run.warnings = stored.summary->warningFlags.trimmed();
        run.inputsAccepted = stored.summary->acceptedInputCount;
        run.inputsRejected = stored.summary->rejectedInputCount;
        run.inputsPredictedOnly = stored.summary->predictedOnlyFrameCount;
    }
    for (const auto& point : stored.points)
    {
        if (point.origin == QStringLiteral("Manual"))
        {
            ++run.pointsManual;
        }
        else if (point.origin == QStringLiteral("AcceptedAutomatic"))
        {
            ++run.pointsAutomatic;
        }
        if (point.fitState == QStringLiteral("PredictedOnly"))
        {
            ++run.pointsPredictedOnly;
        }
        else if (point.fitState == QStringLiteral("Outlier"))
        {
            ++run.pointsOutliers;
        }
    }
    if (stored.cometAlignedProduct.has_value())
    {
        const an::ui::CometRoleReadinessRecord& readiness = stored.cometAlignedProduct->readiness;
        run.readiness = readiness.readinessStatus.trimmed();
        run.readinessFailure = readiness.failureReason.trimmed();
        if (readiness.selectedFrameCount > 0 || readiness.successfulTransformCount > 0 ||
            readiness.failedTransformCount > 0 || readiness.missingTransformCount > 0)
        {
            run.transformsSucceeded = readiness.successfulTransformCount;
            run.transformsFailed = readiness.failedTransformCount;
            run.transformsMissing = readiness.missingTransformCount;
        }
        else
        {
            // No readiness figures: the transforms themselves say how many succeeded.
            int succeeded = 0;
            int failed = 0;
            for (const auto& transform : stored.cometAlignedProduct->transforms)
            {
                ++(transform.succeeded ? succeeded : failed);
            }
            run.transformsSucceeded = succeeded;
            run.transformsFailed = failed;
        }

        const an::ui::CometOutputPlaneRecord& plane = stored.cometAlignedProduct->outputPlane;
        QStringList parts;
        if (!plane.coordinateFrameKind.trimmed().isEmpty())
        {
            parts << plane.coordinateFrameKind.trimmed();
        }
        if (!plane.alignmentMode.trimmed().isEmpty())
        {
            parts << plane.alignmentMode.trimmed();
        }
        if (plane.canvasWidth > 0 && plane.canvasHeight > 0)
        {
            parts << QStringLiteral("%1 × %2").arg(plane.canvasWidth).arg(plane.canvasHeight);
        }
        if (plane.skyWcsValid)
        {
            parts << reportText("sidereal WCS");
        }
        else if (!plane.skyWcsInvalidReason.trimmed().isEmpty())
        {
            parts << plane.skyWcsInvalidReason.trimmed();
        }
        else
        {
            parts << reportText("non-sidereal plane");
        }
        run.outputPlane = parts.join(QStringLiteral("; "));
    }
    return run;
}

[[nodiscard]] ReportCatalogHandoff handoffOf(const SpccHandoffGroupSummary& summary)
{
    ReportCatalogHandoff handoff;
    handoff.settingsVersion = summary.settingsVersion;
    handoff.frames = summary.frameCount;
    handoff.references = summary.referenceCount;
    handoff.sources = summary.sourceCount;
    handoff.satisfied = summary.satisfiedCount;
    handoff.missingSource = summary.missingSourceCount;
    handoff.missingChunkOrCache = summary.missingChunkOrCacheCount;
    handoff.manifestMismatch = summary.manifestMismatchCount;
    handoff.staleMatch = summary.staleMatchCount;
    handoff.rejectedQualityFields = summary.rejectedQualityFieldsCount;
    handoff.untested = summary.untestedCount;
    handoff.astrometricCatalog = summary.astrometricManifestId.trimmed();
    handoff.photometricCatalog = summary.spccManifestId.trimmed();
    handoff.medianResidualPixels = medianOf(summary.residualsPx);
    handoff.medianSeparationArcsec = medianOf(summary.matchSeparationsArcsec);
    return handoff;
}
} // namespace

void fillRegistration(ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    IReportMetrics* metrics = build.sources().metrics;
    const QList<CometReportRun> cometRuns = build.hasMetrics() ? metrics->cometRuns(manifest) : QList<CometReportRun>{};
    const QList<SpccHandoffGroupSummary> handoffs =
        build.hasMetrics() ? metrics->catalogHandoff(manifest) : QList<SpccHandoffGroupSummary>{};

    model.registrationGroups.reserve(manifest.registrationGroups.size());
    for (const auto& group : manifest.registrationGroups)
    {
        const QString key = group.groupKey.toString();
        ReportRegistrationGroup stated;
        stated.key = key;
        // A registration group's key may be spelled as a group of lights is: the prefix keeps their anchors apart.
        stated.slug = build.slugs().slugFor(QStringLiteral("registration|%1").arg(key));
        stated.name = friendlyGroupKey(key);
        stated.target = group.object.trimmed();
        stated.lights = static_cast<int>(group.lightFiles.size());
        if (group.referenceFrame.has_value())
        {
            stated.reference = QFileInfo(group.referenceFrame->canonicalPath()).fileName();
        }
        else if (const QString reference = build.referenceFrame(key); !reference.isEmpty())
        {
            // A reference the application chose is in the registration's own records, not in the manifest.
            stated.reference = QFileInfo(reference).fileName();
        }
        stated.referenceChosenAutomatically =
            group.settings.autoReferenceDuringRegistration || group.settings.referenceImage.trimmed().isEmpty();
        stated.alignmentMode = group.settings.alignmentMode;

        const QHash<QString, RegistrationMetricRow>& metricsByFrame = build.metrics(key);
        if (!metricsByFrame.isEmpty())
        {
            int registered = 0;
            int failed = 0;
            QMap<QString, int> framesByKind;
            QList<double> astrometricRms;
            QList<double> catalogMatches;
            for (const FrameId& light : group.lightFiles)
            {
                const auto row = metricsByFrame.constFind(light.canonicalPath());
                if (row == metricsByFrame.cend())
                {
                    continue;
                }
                if (!row->succeeded)
                {
                    ++failed;
                    continue;
                }
                ++registered;
                const QString kind = row->transformKind.trimmed().toLower();
                if (!kind.isEmpty())
                {
                    ++framesByKind[kind];
                }
                if (std::isfinite(row->astrometricRmsArcsec) && row->astrometricRmsArcsec > 0.0)
                {
                    astrometricRms.append(row->astrometricRmsArcsec);
                }
                if (row->catalogMatchedStars > 0)
                {
                    catalogMatches.append(static_cast<double>(row->catalogMatchedStars));
                }
            }
            stated.registered = registered;
            stated.failed = failed;
            stated.medianAstrometricRmsArcsec = medianOf(astrometricRms);
            stated.medianCatalogMatches = medianOf(catalogMatches);
            for (auto it = framesByKind.cbegin(); it != framesByKind.cend(); ++it)
            {
                stated.transformsUsed.push_back(ReportTransformUse{.kind = it.key(), .frames = it.value()});
            }
        }
        stated.transform = registrationTransformDisplayName(
            group.settings,
            resolveAutoTransformDetail(group.settings, successfulTransformKinds(metricsByFrame)).detail);

        for (const AlignmentGroupResult& alignment : build.result().alignmentGroupResults)
        {
            if (alignment.groupKey != key)
            {
                continue;
            }
            stated.aligned = alignment.totalAligned;
            stated.distortionCorrected = alignment.sipCorrectedCount;
            stated.chromaticAberrationCorrected = alignment.caCorrectedCount;
            stated.chromaticAberrationPassedThrough = alignment.caPassthroughCount;
            break;
        }

        for (const CometReportRun& run : cometRuns)
        {
            if (run.run.groupKey.trimmed() == key.trimmed())
            {
                stated.cometRuns.push_back(cometRunOf(run));
            }
        }
        for (const SpccHandoffGroupSummary& handoff : handoffs)
        {
            if (handoff.groupKey == key.trimmed())
            {
                stated.catalogHandoff = handoffOf(handoff);
                break;
            }
        }

        for (const auto& integrationGroup : manifest.integrationGroups)
        {
            if (integrationGroup.registrationGroupKey.toString() == key)
            {
                stated.masters.append(integrationGroup.groupKey.toString());
            }
        }
        // The group's masters share its output plane, so each states the share of the capture the plane keeps.
        for (const ReportMaster& master : model.masters)
        {
            if (master.overlapRetained.has_value() && stated.masters.contains(master.key))
            {
                stated.overlap = (std::min)(stated.overlap.value_or(*master.overlapRetained), *master.overlapRetained);
            }
        }
        model.registrationGroups.push_back(std::move(stated));
    }
}
} // namespace ImageIntegration::Reporting

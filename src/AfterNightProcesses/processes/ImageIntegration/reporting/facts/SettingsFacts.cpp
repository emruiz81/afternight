// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"

#include <QFileInfo>

#include <utility>

/**
 * @file SettingsFacts.cpp
 * @brief The settings of every group of a run, the ones stated once for the run, and what was left to the
 * application.
 *
 * Settings belong to groups, as the manifest holds them: each calibration master, each registration group and each
 * master has its own. A setting that only applies in a mode is stated only in that mode.
 */

namespace ImageIntegration::Reporting
{
[[nodiscard]] QString interpolationDisplayText(const QString& interpolation)
{
    if (isAutoToken(interpolation))
    {
        return reportText("Auto (Lanczos4)");
    }
    return interpolation.trimmed().isEmpty() ? reportText("Lanczos4") : interpolation.trimmed();
}

[[nodiscard]] QString cfaPatternDisplayText(const QString& cfaPattern)
{
    if (isAutoToken(cfaPattern))
    {
        return reportText("Auto (from metadata)");
    }
    return cfaPattern.trimmed().isEmpty() ? reportText("Auto (from metadata)") : cfaPattern.trimmed();
}

[[nodiscard]] QString debayerMethodDisplayText(const QString& debayerMethod)
{
    if (isAutoToken(debayerMethod))
    {
        return reportText("Auto (engine-selected)");
    }
    return debayerMethod.trimmed().isEmpty() ? reportText("Auto (engine-selected)") : debayerMethod.trimmed();
}

[[nodiscard]] QString normalizationModeText(NormalizationMode mode)
{
    switch (mode)
    {
    case NormalizationMode::None:
        return reportText("None");
    case NormalizationMode::Global:
        return reportText("Global");
    case NormalizationMode::Local:
        return reportText("Local");
    }

    return reportText("Unknown");
}

[[nodiscard]] QString globalNormalizationMethodText(GlobalNormalizationMethod method)
{
    switch (method)
    {
    case GlobalNormalizationMethod::Additive:
        return reportText("Additive");
    case GlobalNormalizationMethod::AdditiveScaling:
        return reportText("Additive + Scaling");
    case GlobalNormalizationMethod::Multiplicative:
        return reportText("Multiplicative");
    case GlobalNormalizationMethod::MultiplicativeScaling:
        return reportText("Multiplicative + Scaling");
    }

    return reportText("Unknown");
}

[[nodiscard]] QString scaleEstimatorText(ScaleEstimatorMethod method)
{
    switch (method)
    {
    case ScaleEstimatorMethod::MAD:
        return reportText("MAD");
    case ScaleEstimatorMethod::IKSS:
        return reportText("IKSS");
    }

    return reportText("Unknown");
}

[[nodiscard]] QString rejectionMethodText(const StackingSettings& settings)
{
    // A method the settings leave empty is none. The request is compared with the method as the settings spell it,
    // and the report prints it in its own words.
    const QString stored = settings.rejectionMethod.trimmed();
    const QString method = stored.isEmpty() ? QStringLiteral("None") : stored;
    const QString effective = stored.isEmpty() ? reportText("None") : stored;
    const QString requested = settings.requestedRejectionMethod.trimmed();
    if (!requested.isEmpty() && requested.compare(method, Qt::CaseInsensitive) != 0)
    {
        if (requested.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0)
        {
            return reportText("Auto (%1)").arg(effective);
        }
        return reportText("%1 -> %2").arg(requested, effective);
    }
    return effective;
}

[[nodiscard]] QString frameLabel(const FrameId& id)
{
    const QString canonical = id.canonicalPath().trimmed();
    if (canonical.isEmpty())
    {
        return QStringLiteral("-");
    }

    return QFileInfo(canonical).fileName();
}

namespace
{
/// The rows of one block, added a setting at a time.
class Rows
{
public:
    explicit Rows(const char* title)
    {
        m_block.title = reportText(title);
    }

    void add(const char* name, const QString& value)
    {
        m_block.rows.push_back(
            ReportSettingsRow{.name = reportText(name), .value = value, .requested = {}, .reason = {}});
    }

    void add(const char* name, const double value)
    {
        add(name, formatFigure(value));
    }

    void add(const char* name, const int value)
    {
        add(name, QString::number(value));
    }

    void add(const char* name, const bool value)
    {
        add(name, value ? reportText("Yes") : reportText("No"));
    }

    /// A setting the application decided: what it resolved to, what was asked for and why.
    void addDecided(const char* name, const QString& value, const QString& requested, const QString& reason)
    {
        m_block.rows.push_back(
            ReportSettingsRow{.name = reportText(name), .value = value, .requested = requested, .reason = reason});
    }

    [[nodiscard]] ReportSettingsBlock take()
    {
        return std::move(m_block);
    }

    [[nodiscard]] bool isEmpty() const
    {
        return m_block.rows.empty();
    }

private:
    ReportSettingsBlock m_block;
};

/// The names of a group's normalization reference lights, in order and once each.
[[nodiscard]] QStringList normalizationReferences(const WorkflowManifest::IntegrationGroup& group)
{
    QStringList references;
    references.reserve(static_cast<qsizetype>(group.normalizationReferenceFrames.size()));
    for (const FrameId& frame : group.normalizationReferenceFrames)
    {
        const QString label = frameLabel(frame);
        if (!label.trimmed().isEmpty() && label != QStringLiteral("-"))
        {
            references << label;
        }
    }
    references.removeDuplicates();
    references.sort(Qt::CaseInsensitive);
    return references;
}

[[nodiscard]] bool normalizationReferenceChosenByUser(const StackingSettings& stacking)
{
    const bool manual = stacking.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
                        !stacking.manualNormalizationReference.trimmed().isEmpty();
    return manual ||
           ImageIntegration::isExplicitLocalNormReferenceStrategy(stacking.localNormalization.referenceStrategy);
}

/// What a rejection method left on Auto resolved to, for a group of so many frames.
struct RejectionDecision
{
    QString resolved;
    QString reason;
};

[[nodiscard]] std::optional<RejectionDecision> rejectionDecision(const StackingSettings& stacking, const int frames)
{
    if (stacking.requestedRejectionMethod.trimmed().compare(QStringLiteral("Auto"), Qt::CaseInsensitive) != 0)
    {
        return std::nullopt;
    }
    const QString effective =
        stacking.rejectionMethod.trimmed().isEmpty() ? reportText("None") : stacking.rejectionMethod.trimmed();
    return RejectionDecision{
        .resolved = effective,
        .reason =
            reportText("Auto resolved to %1 from the stacking policy for %2 frame(s)").arg(effective).arg(frames)};
}

/// How a group's frames are combined. `exposure` is the group's lights by exposure, for a merge of exposures; a
/// calibration master has none, and its stack reads none of the settings a stack of lights alone has. `drizzled` is
/// whether the group's lights are drizzled.
[[nodiscard]] ReportSettingsBlock stackingBlock(const StackingSettings& stacking, const int frames,
                                                const ReportExposure* exposure, const bool drizzled)
{
    // The settings as the stack reads them: what its combination, a merge of exposures or a comet stack does not
    // take is off in them.
    const StackingSettings applied = ImageIntegration::sanitizeStackingSettings(stacking);
    const bool lights = exposure != nullptr;
    const bool merge = stacking.hdr.enabled;

    Rows rows(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Stacking"));
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Stack Method"),
             stacking.hdr.enabled ? reportText("HDR Merge (%1)").arg(stacking.stackMethod) : stacking.stackMethod);
    const QString cometMode = ImageIntegration::normalizeCometStackingMode(stacking.cometStackingMode);
    if (ImageIntegration::usesCometStackingMode(cometMode))
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Comet Stacking Mode"), cometMode);
    }
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "HDR Exposure Merge"), stacking.hdr.enabled);
    if (stacking.hdr.enabled && exposure != nullptr && exposure->framesWithExposure > 0)
    {
        QStringList buckets;
        for (const ReportExposureBin& bin : exposure->bins)
        {
            buckets << QStringLiteral("%1 × %2 s").arg(bin.frames).arg(formatFigure(bin.seconds));
        }
        if (exposure->frames > exposure->framesWithExposure)
        {
            buckets << reportText("%1 with no exposure stated").arg(exposure->frames - exposure->framesWithExposure);
        }
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "HDR Exposure Buckets"),
                 buckets.join(QStringLiteral(", ")));
    }
    if (const std::optional<RejectionDecision> decided = rejectionDecision(stacking, frames); decided.has_value())
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Rejection Method"), decided->resolved,
                        reportText("Auto"), decided->reason);
    }
    else
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Rejection Method"), rejectionMethodText(stacking));
    }
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Sigma Low"), stacking.sigmaLow);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Sigma High"), stacking.sigmaHigh);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Weighting"), stacking.weighting);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Star Count Scaling"), stacking.starCountScaling);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Exponential Alpha"), stacking.exponentialAlpha);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Minimum Weight Floor"), stacking.minWeightFloor);
    // A light's weight varies over its frame only beside a frame weighting the stack applies, and the settings of
    // that variation are read only while it is on.
    const QString weighting = applied.weighting.trimmed();
    const bool weighs = !weighting.isEmpty() && weighting.compare(QStringLiteral("None"), Qt::CaseInsensitive) != 0;
    if (lights && weighs)
    {
        const LocalWeightingSettings& perPixel = stacking.localWeighting;
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Per-Pixel Weighting"), perPixel.enabled);
        if (perPixel.enabled)
        {
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Per-Pixel Weighting Window Size"),
                     perPixel.windowSize);
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Per-Pixel Weighting Grid Spacing"),
                     perPixel.gridSpacing);
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Per-Pixel Weighting SNR Exponent"),
                     perPixel.snrWeightExponent);
        }
    }
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Create Rejection Maps"), stacking.createRejectionMaps);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Merge Rejection Maps"), stacking.mergeRejectionMaps);
    if (lights && !merge)
    {
        // The analysis reads what the stack rejected, so a stack that rejects nothing has none. A text the stack does
        // not know stops the group by name, and no row is left out on its account.
        const an::Result<ImageIntegration::IntegrationStackChoice> choice =
            ImageIntegration::integrationStackChoice(applied);
        if (!choice.hasValue() || choice->rejection != an::StackRejection::None)
        {
            // Stacking again without the flagged lights turns the analysis on, and a drizzled stack is not stacked
            // again.
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Analyze Spatial Rejection Clusters"),
                     applied.spatialRejectionClustering);
            if (!drizzled)
            {
                rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Re-Stack Excluding Flagged Frames"),
                         applied.restackExcludingFlaggedFrames);
            }
        }
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Create Quality Maps"), stacking.createQualityMaps);
    }
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Normalization Mode"),
             normalizationModeText(stacking.normalizationMode));
    if (stacking.normalizationMode != NormalizationMode::None)
    {
        const bool local =
            stacking.normalizationMode == NormalizationMode::Local && stacking.localNormalization.enabled;
        // Local normalization matches each light globally in Additive With Scaling, whatever method the group
        // stores for Global.
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Global Normalization Method"),
                 local ? reportText("%1 (set by local normalization)")
                             .arg(globalNormalizationMethodText(GlobalNormalizationMethod::AdditiveScaling))
                       : globalNormalizationMethodText(stacking.globalNormalizationMethod));
        if (stacking.normalizationMode == NormalizationMode::Local ||
            ImageIntegration::normalizationUsesScaleEstimator(stacking.globalNormalizationMethod))
        {
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Scale Estimator"),
                     scaleEstimatorText(stacking.scaleEstimator));
        }
        // The lights are matched over the areas they share only in a global normalization, and the floors of that
        // match are read only while it is on.
        if (lights && !merge && stacking.normalizationMode == NormalizationMode::Global)
        {
            const OverlapNormalizationSettings& overlap = stacking.overlapNormalization;
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Overlap Normalization"), overlap.enabled);
            if (overlap.enabled)
            {
                rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Min Overlap Pixels"), overlap.minOverlapPixels);
                rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Min Overlap Fraction"),
                         overlap.minOverlapFraction);
            }
        }
    }
    if (lights)
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Plate-Solve Final Master If Needed"),
                 stacking.solveFinalMasterIfNeeded);
    }
    return rows.take();
}

/// Which lights a group was normalized to, and who chose them. Nothing for a group that is not normalized.
[[nodiscard]] std::optional<ReportSettingsBlock> normalizationBlock(const WorkflowManifest::IntegrationGroup& group)
{
    const StackingSettings& stacking = group.stacking;
    if (stacking.normalizationMode == NormalizationMode::None)
    {
        return std::nullopt;
    }
    Rows rows(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Normalization Reference"));
    const QStringList references = normalizationReferences(group);
    const bool byUser = normalizationReferenceChosenByUser(stacking);
    if (byUser)
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reference Frames"),
                 references.isEmpty() ? reportText("Not recorded") : references.join(QStringLiteral(", ")));
    }
    else
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reference Frames"),
                        references.isEmpty() ? reportText("Not recorded") : references.join(QStringLiteral(", ")),
                        reportText("Automatic"),
                        reportText("Auto-selected during normalization planning from frame quality and coverage"));
    }
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reference Source"),
             byUser ? reportText("Selected in the settings") : reportText("Chosen by the application"));
    if (stacking.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
        !stacking.manualNormalizationReference.trimmed().isEmpty())
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Manual Reference Requested"),
                 QFileInfo(stacking.manualNormalizationReference).fileName());
    }
    if (group.normalizationCoefficientsArtifactPath.has_value() &&
        !group.normalizationCoefficientsArtifactPath->trimmed().isEmpty())
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Coefficients Artifact"),
                 *group.normalizationCoefficientsArtifactPath);
    }
    return rows.take();
}

/// The local normalization's own settings. Nothing unless the group is normalized locally.
[[nodiscard]] std::optional<ReportSettingsBlock> localNormalizationBlock(const StackingSettings& stacking)
{
    if (stacking.normalizationMode != NormalizationMode::Local || !stacking.localNormalization.enabled)
    {
        return std::nullopt;
    }
    const auto& local = stacking.localNormalization;
    Rows rows(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Local Normalization"));
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reference Strategy"), local.referenceStrategy);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Max Integrated Frames"), local.maxIntegratedFrames);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Grid Size"), local.gridSize);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Grid Interpolation"), local.gridInterpolation);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Local Scale Enabled"), local.localScaleEnabled);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Local Scale Smoothing Radius"),
             local.localScaleSmoothingRadius);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Evaluation Criteria"), local.evaluationCriteria);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Background Estimator"), local.backgroundEstimator);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Background Percentile"), local.backgroundPercentile);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Sigma Clip K"), local.sigmaClipK);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Sigma Clip Iterations"), local.sigmaClipIterations);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Background Smoothing Radius"),
             local.backgroundSmoothingRadius);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Scale Evaluation"), local.scaleEvaluation);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Scale Mode"), local.scaleMode);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "PSF Type"), local.psfType);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Moffat Beta"), local.moffatBeta);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Max Stars"), local.maxStars);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Min SNR (flux scale)"), local.minDetectionSnr);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Allow Clustered Sources"), local.allowClusteredSources);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Low Clip"), local.lowClip);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "High Clip"), local.highClip);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Hot Pixel Median Radius"), local.hotPixelMedianRadius);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Noise Reduction Radius"), local.noiseReductionRadius);
    return rows.take();
}

[[nodiscard]] ReportSettingsBlock drizzleBlock(const WorkflowManifest::IntegrationGroup::DrizzleSettings& drizzle)
{
    Rows rows(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Drizzle"));
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Enabled"), drizzle.enabled);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Scale"), drizzle.scale);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Pixel Fraction"), drizzle.pixelFraction);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Kernel"), drizzle.kernel);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CFA Aware"), drizzle.cfaAware);
    if (drizzle.cfaAware && isAutoToken(drizzle.cfaPattern))
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CFA Pattern"), reportText("from metadata"),
                        reportText("Auto"),
                        reportText("Auto reads the CFA pattern from image metadata before drizzle mapping"));
    }
    else
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CFA Pattern"), cfaPatternDisplayText(drizzle.cfaPattern));
    }
    return rows.take();
}

/// A registration group's settings. `transform` is what a transform left on Auto resolved to, and `recordedReference`
/// the light the registration's own records name as the reference, as `frameLabel` labels it; empty when they name
/// none.
[[nodiscard]] ReportSettingsBlock registrationBlock(const WorkflowManifest::RegistrationGroup& group,
                                                    const AutoTransformResolution& transform,
                                                    const QString& recordedReference)
{
    namespace utils = ImageIntegration::RegistrationSettingsUtils;
    const RegistrationSettings& settings = group.settings;
    const bool automaticReference =
        settings.autoReferenceDuringRegistration || settings.referenceImage.trimmed().isEmpty();
    // The manifest names a reference the user chose; one the application chose is in the registration's own records.
    // With neither, the row can only say that the choice was left to the application.
    QString chosenReference = reportText("Automatic");
    if (group.referenceFrame.has_value())
    {
        chosenReference = frameLabel(*group.referenceFrame);
    }
    else if (!recordedReference.isEmpty())
    {
        chosenReference = recordedReference;
    }

    Rows rows(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Registration"));
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Settings Version"), settings.version);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Registration Method"),
             utils::registrationMethodDisplayName(settings.registrationMethod));
    if (automaticReference)
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reference Frame"), chosenReference,
                        reportText("Automatic"),
                        reportText("Auto-reference selects the best available light from registration quality "
                                   "metrics when no manual reference is supplied"));
    }
    else
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reference Frame"), chosenReference);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reference Image Requested"),
                 QFileInfo(settings.referenceImage).fileName());
    }
    // Both are stated as the group stores them, whatever its method: a mosaic canvas has the lights the method could
    // not register solved whatever the first says, and neither does anything without a solver or a catalog to call.
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Auto-Solve Unsolved Frames During Registration"),
             settings.autoSolveUnsolvedFrames);
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Enrich With Gaia Catalog"), settings.enrichWithGaiaCatalog);
    if (utils::usesStarDetection(settings.registrationMethod))
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Min Star Pairs"), settings.minStarPairs);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Max Stars per Frame"), settings.maxStarPairs);
    }
    const QString transformName = registrationTransformDisplayName(settings, transform.detail);
    if (settings.transformMode.trimmed().compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0)
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Transform Model"), transformName,
                        reportText("Auto"), transform.reason);
    }
    else
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Transform Model"), transformName);
    }
    if (utils::usesCometTracking(settings.registrationMethod) && settings.cometTracking.has_value())
    {
        int enabled = 0;
        for (const auto& observation : settings.cometTracking->observations)
        {
            enabled += observation.enabled ? 1 : 0;
        }
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Comet Settings Schema"),
                 settings.cometTracking->schemaVersion);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Comet Observations"),
                 reportText("%1 total, %2 enabled").arg(settings.cometTracking->observations.size()).arg(enabled));
    }
    if (utils::usesTransformContract(settings.registrationMethod))
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Base Transform"), settings.transformation);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Transform Mode"), settings.transformMode);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Transform Order"), settings.transformOrder);
        if (utils::usesManualControlPoints(settings.registrationMethod))
        {
            int pairs = 0;
            for (auto it = settings.manualControlPointsByFrame.cbegin();
                 it != settings.manualControlPointsByFrame.cend(); ++it)
            {
                pairs += static_cast<int>(it.value().size());
            }
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Manual Control-Point Frames"),
                     static_cast<int>(settings.manualControlPointsByFrame.size()));
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Manual Control-Point Pairs"), pairs);
        }
    }
    // The correction of the lights' colour fringes is asked of any method, and its own settings are read only while
    // it is on.
    const RegistrationSettings::ChromaticAberrationSettings& chromatic = settings.chromaticAberration;
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Correct Chromatic Aberration Before Registration"),
             chromatic.enabled);
    if (chromatic.enabled)
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CA Edge Fraction"), chromatic.edgeFraction);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CA Fit Type"), chromatic.transformType);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CA Min Matched Stars"), chromatic.minMatchedStars);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CA Interpolation"), chromatic.interpolation);
    }
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Alignment Mode"), settings.alignmentMode);
    if (isAutoToken(settings.interpolation))
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Interpolation"), reportText("Lanczos4"),
                        reportText("Auto"),
                        reportText("Auto resolves to Lanczos4 for high-quality sub-pixel resampling"));
    }
    else
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Interpolation"),
                 interpolationDisplayText(settings.interpolation));
    }
    if (utils::usesStarDetection(settings.registrationMethod))
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Detection Radius"), settings.radius);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Detection Threshold"), settings.threshold);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Amplitude Range Enabled"),
                 settings.amplitudeRangeEnabled);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Threshold Minimum"), settings.thresholdMinimum);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Threshold Maximum"), settings.thresholdMaximum);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Roundness Threshold"), settings.roundnessThreshold);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Roundness Range Enabled"),
                 settings.roundnessRangeEnabled);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Roundness Threshold Maximum"),
                 settings.roundnessThresholdMaximum);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Convergence"), settings.convergence);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Relax PSF Checks"), settings.relaxPsfChecks);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Profile Type"), settings.profileType);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Minimum Beta"), settings.minimumBeta);
    }
    if (isAutoToken(settings.cfaPattern))
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CFA Pattern"), reportText("from metadata"),
                        reportText("Auto"),
                        reportText("Auto reads the CFA pattern from image metadata at alignment time"));
    }
    else
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "CFA Pattern"),
                 cfaPatternDisplayText(settings.cfaPattern));
    }
    if (isAutoToken(settings.debayerMethod))
    {
        rows.addDecided(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Debayer Method"), reportText("engine-selected"),
                        reportText("Auto"),
                        reportText("Auto lets the demosaic engine choose the default method for the detected CFA "
                                   "data"));
    }
    else
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Debayer Method"),
                 debayerMethodDisplayText(settings.debayerMethod));
    }
    return rows.take();
}

/// What is stated once for the run: how it executed, how masters are plate-solved, whether colours are combined.
void fillRunSettings(const WorkflowManifest& manifest, ReportRun& run)
{
    const int resolvedCores = ImageIntegration::core::clampParallelCoreLimit(manifest.maxParallelCores);
    Rows execution(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Execution"));
    execution.add(
        QT_TRANSLATE_NOOP("ImageIntegrationReport", "Parallel CPU Threads"),
        manifest.maxParallelCores > 0
            ? reportText("%1 of %2").arg(resolvedCores).arg(ImageIntegration::core::detectedParallelCoreCount())
            : reportText("All available (%1)").arg(resolvedCores));
    execution.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibrate Lights in Memory"),
                  manifest.calibrateLightsInMemory);
    execution.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Reprocess Existing Outputs"),
                  manifest.reprocessExisting);
    run.settings.push_back(execution.take());

    Rows plateSolve(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Plate Solve"));
    if (!manifest.plateSolve.defaultSolver.trimmed().isEmpty())
    {
        plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Solver"),
                       manifest.plateSolve.defaultSolver.trimmed());
    }
    plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Catalog"), manifest.plateSolve.gaiaCatalogPack);
    plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Search Radius (degrees)"),
                   manifest.plateSolve.searchRadius);
    plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Downsample"), manifest.plateSolve.downsample);
    plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Timeout (seconds)"),
                   manifest.plateSolve.timeoutSeconds);
    plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Use Position Hints"), manifest.plateSolve.useHints);
    if (manifest.plateSolve.focalLengthMm.has_value())
    {
        plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Focal Length (mm)"),
                       *manifest.plateSolve.focalLengthMm);
    }
    if (manifest.plateSolve.pixelSizeUm.has_value())
    {
        plateSolve.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Pixel Size (micrometres)"),
                       *manifest.plateSolve.pixelSizeUm);
    }
    run.settings.push_back(plateSolve.take());

    Rows colour(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Colour Combination"));
    colour.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Combine Red, Green and Blue Masters"),
               manifest.combineRgbImages);
    run.settings.push_back(colour.take());
}

} // namespace

ReportSettingsBlock lightsCalibrationBlock(const WorkflowManifest::CalibrationSettings& settings, const bool takesDark,
                                           const bool takesFlat)
{
    Rows rows(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibration"));
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cosmetic Correction"), settings.cosmeticCorrection);
    if (settings.cosmeticCorrection)
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Hot Pixel Sigma"), settings.sigmaHot);
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Cold Pixel Sigma"), settings.sigmaCold);
    }
    if (takesDark)
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Dark Optimization"), settings.darkOptimization);
        if (!settings.darkOptimization)
        {
            rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Dark Multiplication Factor"),
                     settings.darkMultiplicationFactor);
        }
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Subtract Bias From Dark"), settings.subtractBiasFromDark);
    }
    if (takesFlat)
    {
        rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Flat Auto Level"), settings.flatAutoLevel);
    }
    rows.add(QT_TRANSLATE_NOOP("ImageIntegrationReport", "Pedestal"), settings.pedestal);
    return rows.take();
}

namespace
{
/// One row of the run's decisions for each setting of a block that was left to the application.
void collectDecisions(const ReportSettingsBlock& block, const QString& area, const QString& groupKey,
                      const QString& group, ReportModel& model)
{
    for (const ReportSettingsRow& row : block.rows)
    {
        if (row.requested.isEmpty())
        {
            continue;
        }
        model.decisions.push_back(ReportDecision{.area = QStringLiteral("%1: %2").arg(area, row.name),
                                                 .groupKey = groupKey,
                                                 .group = group,
                                                 .requested = row.requested,
                                                 .resolved = row.value,
                                                 .reason = row.reason});
    }
}
} // namespace

void fillSettings(ReportBuild& build, ReportModel& model)
{
    const WorkflowManifest& manifest = build.manifest();
    fillRunSettings(manifest, model.run);

    for (ReportRegistrationGroup& stated : model.registrationGroups)
    {
        for (const auto& group : manifest.registrationGroups)
        {
            if (group.groupKey.toString() != stated.key)
            {
                continue;
            }
            const AutoTransformResolution transform =
                resolveAutoTransformDetail(group.settings, successfulTransformKinds(build.metrics(stated.key)));
            // A reference the application chose is in the registration's own records, which are asked only where the
            // manifest names none.
            const QString recorded =
                group.referenceFrame.has_value() ? QString() : build.referenceFrame(stated.key).trimmed();
            stated.settings.push_back(
                registrationBlock(group, transform, recorded.isEmpty() ? QString() : frameLabel(FrameId(recorded))));
            collectDecisions(stated.settings.back(), stated.settings.back().title, stated.key, stated.name, model);
            break;
        }
    }

    const auto fillCalibrationStage = [&](const std::optional<WorkflowManifest::CalibrationStage>& stage,
                                          const ReportCalibrationKind kind, const QString& kindName) {
        if (!stage.has_value())
        {
            return;
        }
        for (const auto& group : stage->groups)
        {
            const QString key = group.groupKey.toString();
            for (ReportCalibrationMaster& master : model.calibrationMasters)
            {
                if (master.kind != kind || master.key != key)
                {
                    continue;
                }
                master.settings.push_back(
                    stackingBlock(group.stacking, static_cast<int>(group.inputFiles.size()), nullptr, false));
                collectDecisions(master.settings.back(),
                                 QStringLiteral("%1 %2").arg(kindName, master.settings.back().title), master.key,
                                 master.name, model);
                break;
            }
        }
    };
    fillCalibrationStage(manifest.biasStage, ReportCalibrationKind::Bias, reportText("Bias"));
    fillCalibrationStage(manifest.darkStage, ReportCalibrationKind::Dark, reportText("Dark"));
    fillCalibrationStage(manifest.flatStage, ReportCalibrationKind::Flat, reportText("Flat"));

    for (const auto& group : manifest.integrationGroups)
    {
        const QString key = group.groupKey.toString();
        for (ReportMaster& master : model.masters)
        {
            if (master.key != key)
            {
                continue;
            }
            // A group is drizzled when the run plans its drizzle or its stacking settings ask for one.
            const bool drizzled =
                group.stacking.drizzle.enabled || (group.drizzle.has_value() && group.drizzle->enabled);
            master.settings.push_back(
                stackingBlock(group.stacking, static_cast<int>(group.lightFiles.size()), &master.exposure, drizzled));
            if (std::optional<ReportSettingsBlock> block = normalizationBlock(group); block.has_value())
            {
                master.settings.push_back(std::move(*block));
            }
            if (std::optional<ReportSettingsBlock> block = localNormalizationBlock(group.stacking); block.has_value())
            {
                master.settings.push_back(std::move(*block));
            }
            if (group.drizzle.has_value())
            {
                master.settings.push_back(drizzleBlock(*group.drizzle));
            }
            for (const ReportSettingsBlock& block : master.settings)
            {
                collectDecisions(block, block.title, master.key, master.name, model);
            }
            break;
        }
    }
}
} // namespace ImageIntegration::Reporting

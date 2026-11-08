// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationPanelUtils.h"

#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/core/FileImport.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/IntegrationGroupEngine.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsReader.h"
#include "app/FileDialogUtils.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightPlatform/Logging.h>

#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QDir>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMetaObject>
#include <QMutex>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QThreadPool>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

namespace
{
QString yesNo(bool value)
{
    return value ? QCoreApplication::translate("CalibrationPanelUtils", "Yes")
                 : QCoreApplication::translate("CalibrationPanelUtils", "No");
}

QString optionalIntText(const std::optional<int>& value)
{
    return value.has_value() ? QString::number(*value)
                             : QCoreApplication::translate("CalibrationPanelUtils", "Unknown");
}

QString optionalDoubleText(const std::optional<double>& value, int decimals = 2)
{
    return value.has_value() ? QString::number(*value, 'f', decimals)
                             : QCoreApplication::translate("CalibrationPanelUtils", "Unknown");
}

ImageIntegration::PhotometricCoverageSummary loadPhotometricCoverage(const IntegrationDataModel* model,
                                                                     const std::vector<FrameInfo>& frames)
{
    std::vector<FrameQualityMetrics> metricsList;
    metricsList.reserve(frames.size());
    for (const auto& frame : frames)
    {
        if (!frame.excluded)
        {
            metricsList.push_back(FrameQualityMetrics{});
        }
    }

    if (!model || metricsList.empty())
    {
        return ImageIntegration::summarizePhotometricCoverage(metricsList);
    }

    const QString outputDir = model->outputDirectory().trimmed();
    if (outputDir.isEmpty())
    {
        return ImageIntegration::summarizePhotometricCoverage(metricsList);
    }

    const QString dbPath = QDir(outputDir).filePath(QStringLiteral("registration_metrics.db"));
    if (!QFileInfo::exists(dbPath))
    {
        return ImageIntegration::summarizePhotometricCoverage(metricsList);
    }

    stage_utils::RegistrationMetricsReader reader(dbPath, QStringLiteral("StackingSettingsDialogPhotometric"));
    QString openError;
    if (!reader.open(&openError))
    {
        LOG_WARNING(
            "CalibrationPanelUtils: failed to open registration metrics DB for photometric weighting availability: {}",
            openError.toStdString());
        return ImageIntegration::summarizePhotometricCoverage(metricsList);
    }

    std::vector<FrameId> allowedFrames;
    allowedFrames.reserve(frames.size());
    for (const auto& frame : frames)
    {
        if (!frame.excluded)
        {
            allowedFrames.push_back(frame.id);
        }
    }

    QString error;
    std::unordered_map<FrameId, stage_utils::LatestRegistrationFrameRecord> recordsByFrame;
    if (!reader.loadLatestRecordsForFrames(allowedFrames, recordsByFrame, error))
    {
        LOG_WARNING(
            "CalibrationPanelUtils: failed to load registration metrics for photometric weighting availability: {}",
            error.toStdString());
        return ImageIntegration::summarizePhotometricCoverage(metricsList);
    }

    metricsList.clear();
    for (const auto& frame : frames)
    {
        if (frame.excluded)
        {
            continue;
        }
        if (auto it = recordsByFrame.find(frame.id); it != recordsByFrame.end())
        {
            metricsList.push_back(it->second.metrics);
        }
        else
        {
            metricsList.push_back(FrameQualityMetrics{});
        }
    }

    return ImageIntegration::summarizePhotometricCoverage(metricsList);
}

[[nodiscard]] QString compactGroupKeyForDisplay(const QString& rawKey)
{
    const QString trimmed = rawKey.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const auto parsed = GroupKey::fromString(trimmed);
    if (!parsed.has_value())
    {
        return trimmed;
    }

    const QString compact = parsed->toCompactString().trimmed();
    return compact.isEmpty() ? trimmed : compact;
}

QStringList normalizationReferenceCandidates(IntegrationDataModel* model, const GroupKey& groupKey)
{
    if (!model || groupKey.toString().trimmed().isEmpty())
    {
        return {};
    }

    IntegrationGroupEngine engine;
    const auto groups = engine.computeGroups(model->lightFrames().frames(), model->lightIntegrationGroupingOptions());
    const auto it = groups.find(IntegrationGroupKey{groupKey});
    if (it == groups.end())
    {
        return {};
    }

    QStringList ids;
    ids.reserve(static_cast<int>(it->second.size()));
    for (const auto& id : it->second)
    {
        ids.push_back(id.canonicalPath());
    }
    ids.removeDuplicates();
    ids.sort();
    return ids;
}

QString cometOutputModeFromRegistrationSettings(const RegistrationSettings& settings)
{
    if (!settings.cometTracking.has_value())
    {
        return QStringLiteral("Comet");
    }

    const QString output_mode = ImageIntegration::normalizeCometStackingMode(settings.cometTracking->outputMode);
    return output_mode.trimmed().isEmpty() ? QStringLiteral("Comet") : output_mode;
}

QString cometStackingModeForLightGroupRegistration(const IntegrationDataModel* model,
                                                   const std::vector<FrameInfo>* frames)
{
    if (!model || !frames || frames->empty())
    {
        return QStringLiteral("Stars");
    }

    const auto frame_it =
        std::find_if(frames->begin(), frames->end(), [](const FrameInfo& frame) { return !frame.excluded; });
    if (frame_it == frames->end())
    {
        return QStringLiteral("Stars");
    }

    const GroupKey registration_key =
        RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(*frame_it));
    const auto& registration_settings = model->registrationSettings();
    const auto settings_it = registration_settings.find(registration_key);
    if (settings_it == registration_settings.end() ||
        !ImageIntegration::RegistrationSettingsUtils::usesCometTracking(settings_it->second.registrationMethod))
    {
        return QStringLiteral("Stars");
    }

    return cometOutputModeFromRegistrationSettings(settings_it->second);
}

} // namespace

std::optional<FrameId> bestNormalizationReferenceCandidate(IntegrationDataModel* model, const GroupKey& groupKey)
{
    if (!model || groupKey.toString().trimmed().isEmpty())
    {
        return std::nullopt;
    }

    IntegrationGroupEngine engine;
    const auto groups = engine.computeGroups(model->lightFrames().frames(), model->lightIntegrationGroupingOptions());
    const auto group_it = groups.find(IntegrationGroupKey{groupKey});
    if (group_it == groups.end())
    {
        return std::nullopt;
    }

    std::unordered_map<FrameId, QString> filtersById;
    filtersById.reserve(model->lightFrames().frames().size());
    for (const auto& frame : model->lightFrames().frames())
    {
        filtersById.emplace(frame.id, frame.metadata.filter());
    }

    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    candidates.reserve(group_it->second.size());
    for (const auto& id : group_it->second)
    {
        if (const auto metrics = model->frameQualityMetrics(id); metrics.has_value())
        {
            candidates.emplace_back(id, *metrics);
        }
    }

    const auto filterLookup = [&](const FrameId& id) -> QString {
        const auto it = filtersById.find(id);
        return it != filtersById.end() ? it->second : QString();
    };

    return selectBestReferenceFrameAutoSmart(candidates, filterLookup).best;
}

/**
 * @brief Analyze sub-pixel dithering quality from registration transforms.
 *
 * Loads the latest transform data for each light frame from the registration
 * metrics database.  For each supported transform model, including polynomial
 * registration, the fractional-pixel component of the linear translation seed
 * is extracted.  The uniformity of these fractional offsets across the [0,1)
 * range is measured as "sub-pixel coverage" — a value near 1.0 means excellent,
 * uniformly-distributed dithering that will benefit from drizzle integration.
 */
DrizzleAnalysis computeDrizzleAnalysis(const QString& outputDir, const std::vector<FrameInfo>& frames)
{
    DrizzleAnalysis result;

    if (frames.empty())
    {
        return result;
    }

    std::vector<const FrameInfo*> includedFrames;
    includedFrames.reserve(frames.size());
    for (const auto& frame : frames)
    {
        if (!frame.excluded)
        {
            includedFrames.push_back(&frame);
        }
    }
    result.totalFrames = static_cast<int>(includedFrames.size());

    if (includedFrames.empty())
    {
        return result;
    }

    // Resolve the metrics DB path from the output directory
    if (outputDir.isEmpty())
    {
        return result;
    }

    const QDir outputRoot(outputDir);
    const QString metricsDbPath = outputRoot.filePath(QStringLiteral("registration_metrics.db"));
    if (!QFileInfo::exists(metricsDbPath))
    {
        return result;
    }

    // Open the registration metrics DB and load transforms
    stage_utils::RegistrationMetricsReader reader(metricsDbPath, QStringLiteral("drizzle_analysis"));
    if (!reader.open())
    {
        return result;
    }

    std::vector<FrameId> requestedFrames;
    requestedFrames.reserve(includedFrames.size());
    for (const auto* frame : includedFrames)
    {
        requestedFrames.push_back(frame->id);
    }

    std::unordered_map<FrameId, stage_utils::LatestRegistrationFrameRecord> recordsByFrame;
    QString loadError;
    if (!reader.loadLatestRecordsForFrames(requestedFrames, recordsByFrame, loadError))
    {
        return result;
    }

    std::vector<const FrameInfo*> analysisFrames;
    analysisFrames.reserve(includedFrames.size());
    for (const auto* frame : includedFrames)
    {
        const auto recordIt = recordsByFrame.find(frame->id);
        if (recordIt != recordsByFrame.end())
        {
            if (!recordIt->second.metrics.succeeded)
            {
                continue;
            }
            ++result.framesWithMetrics;
        }
        analysisFrames.push_back(frame);
    }

    result.totalFrames = static_cast<int>(analysisFrames.size());
    if (analysisFrames.empty())
    {
        return result;
    }

    // Match stacking group frames to their transforms
    std::vector<std::pair<double, double>> fractionalOffsets;
    fractionalOffsets.reserve(analysisFrames.size());
    double sumFrac = 0.0;

    for (const auto* frame : analysisFrames)
    {
        auto it = recordsByFrame.find(frame->id);
        if (it == recordsByFrame.end() || !it->second.transform.has_value())
        {
            continue;
        }

        // Where the frame's origin lands on the reference: its fractional part is the sub-pixel dither.
        const an::PixelCoordinate translation = it->second.transform->translation();
        const double tx = translation.x;
        const double ty = translation.y;
        if (!std::isfinite(tx) || !std::isfinite(ty))
        {
            continue;
        }

        const double fracX = tx - std::floor(tx);
        const double fracY = ty - std::floor(ty);
        fractionalOffsets.emplace_back(fracX, fracY);

        const double nearestDx = std::abs(tx - std::round(tx));
        const double nearestDy = std::abs(ty - std::round(ty));
        sumFrac += std::sqrt(nearestDx * nearestDx + nearestDy * nearestDy);
    }

    result.framesWithTransforms = static_cast<int>(fractionalOffsets.size());
    result.available = result.framesWithTransforms >= 2;

    if (!result.available)
    {
        return result;
    }

    // Image resolution from the first frame's metadata
    const auto& dims = analysisFrames.front()->metadata.dimensions();
    result.imageMegapixels =
        static_cast<int>((static_cast<long long>(dims.width()) * dims.height() + 500000LL) / 1000000LL);

    // Compute mean fractional offset (how far from integer shifts on average)
    result.meanFractionalOffset = sumFrac / static_cast<double>(fractionalOffsets.size());

    // Compute sub-pixel coverage using a simple grid histogram approach.
    // Divide the [0,1) × [0,1) fractional space into a 4×4 grid (16 bins).
    // Count how many bins are occupied.  Uniform dithering fills all bins;
    // integer-only shifts fill only bin (0,0).
    constexpr int gridSize = 4;
    constexpr int totalBins = gridSize * gridSize;
    bool occupied[totalBins] = {};
    for (const auto& [fx, fy] : fractionalOffsets)
    {
        const int bx = std::clamp(static_cast<int>(fx * gridSize), 0, gridSize - 1);
        const int by = std::clamp(static_cast<int>(fy * gridSize), 0, gridSize - 1);
        occupied[by * gridSize + bx] = true;
    }
    int filledBins = 0;
    for (bool b : occupied)
    {
        if (b)
            ++filledBins;
    }
    result.subPixelCoverage = static_cast<double>(filledBins) / static_cast<double>(totalBins);

    // Load per-frame FWHM values to derive median seeing for pixfrac estimation
    std::vector<double> fwhmValues;
    fwhmValues.reserve(recordsByFrame.size());
    for (const auto& entry : recordsByFrame)
    {
        if (entry.second.metrics.fwhm > 0.0 && std::isfinite(entry.second.metrics.fwhm))
        {
            fwhmValues.push_back(entry.second.metrics.fwhm);
        }
    }
    if (!fwhmValues.empty())
    {
        std::sort(fwhmValues.begin(), fwhmValues.end());
        const size_t mid = fwhmValues.size() / 2;
        result.medianFwhmPx =
            (fwhmValues.size() % 2 == 0) ? (fwhmValues[mid - 1] + fwhmValues[mid]) * 0.5 : fwhmValues[mid];
    }

    return result;
}

namespace
{
class DrizzleAnalysisDispatcher
{
public:
    static DrizzleAnalysisDispatcher& instance()
    {
        static DrizzleAnalysisDispatcher dispatcher;
        return dispatcher;
    }

    void request(const QString& groupKey, const QString& outputDir, std::vector<FrameInfo> frames,
                 std::function<void(const DrizzleAnalysis&)> callback)
    {
        if (groupKey.trimmed().isEmpty())
        {
            callback(computeDrizzleAnalysis(outputDir, frames));
            return;
        }

        bool shouldStartTask = false;
        {
            QMutexLocker lock(&m_mutex);
            m_callbacks[groupKey].push_back(std::move(callback));
            if (!m_inFlight.contains(groupKey))
            {
                m_inFlight.insert(groupKey);
                shouldStartTask = true;
            }
        }

        if (!shouldStartTask)
        {
            return;
        }

        class Task final : public QRunnable
        {
        public:
            Task(QString key, QString outDir, std::vector<FrameInfo> inFrames)
                : m_key(std::move(key)), m_outputDir(std::move(outDir)), m_frames(std::move(inFrames))
            {
                setAutoDelete(true);
            }

            void run() override
            {
                const DrizzleAnalysis analysis = computeDrizzleAnalysis(m_outputDir, m_frames);
                if (QCoreApplication* app = QCoreApplication::instance())
                {
                    QMetaObject::invokeMethod(
                        app,
                        [key = m_key, analysis]() { DrizzleAnalysisDispatcher::instance().complete(key, analysis); },
                        Qt::QueuedConnection);
                }
                else
                {
                    DrizzleAnalysisDispatcher::instance().complete(m_key, analysis);
                }
            }

        private:
            QString m_key;
            QString m_outputDir;
            std::vector<FrameInfo> m_frames;
        };

        m_pool.start(new Task(groupKey, outputDir, std::move(frames)));
    }

    void complete(const QString& groupKey, const DrizzleAnalysis& analysis)
    {
        std::vector<std::function<void(const DrizzleAnalysis&)>> callbacks;
        {
            QMutexLocker lock(&m_mutex);
            callbacks = m_callbacks.take(groupKey);
            m_inFlight.remove(groupKey);
        }

        for (const auto& callback : callbacks)
        {
            callback(analysis);
        }
    }

private:
    DrizzleAnalysisDispatcher()
    {
        m_pool.setMaxThreadCount(2);
    }

    QThreadPool m_pool;
    QMutex m_mutex;
    QSet<QString> m_inFlight;
    QHash<QString, std::vector<std::function<void(const DrizzleAnalysis&)>>> m_callbacks;
};

[[nodiscard]] QString matchDisplayName(const CalibrationMatch& match)
{
    QString name = QFileInfo(match.masterPath).fileName();
    if (name.isEmpty())
    {
        name = compactGroupKeyForDisplay(match.matchedGroupKey);
    }
    return name.trimmed().isEmpty() ? QStringLiteral("[Unknown]") : name;
}

[[nodiscard]] QString confidenceSuffix(const CalibrationMatch& match)
{
    if (match.type == CalibrationMatch::MatchType::None)
    {
        return QString();
    }

    if (match.confidence <= 0.0)
    {
        return QString();
    }

    const int pct = std::clamp(static_cast<int>(match.confidence * 100.0 + 0.5), 0, 100);
    return QStringLiteral(" (%1%)").arg(pct);
}

[[nodiscard]] QString fallbackSuffix(const CalibrationMatch& match)
{
    switch (match.type)
    {
    case CalibrationMatch::MatchType::ClosestDate:
        return QCoreApplication::translate("CalibrationPanelUtils", " (+/-%1d)")
            .arg(std::abs(match.dateDifferencesDays));
    case CalibrationMatch::MatchType::ClosestExposure:
        return QCoreApplication::translate("CalibrationPanelUtils", " (+/-%1s)")
            .arg(match.exposureDifference, 0, 'f', 2);
    default:
        break;
    }
    return QString();
}

[[nodiscard]] QIcon tintedSvgIcon(const QString& resourcePath, const QColor& color)
{
    static QHash<QString, QIcon> cache;
    const QString cacheKey = resourcePath + QLatin1Char('|') + color.name(QColor::HexArgb);
    if (auto it = cache.find(cacheKey); it != cache.end())
    {
        return *it;
    }

    QIcon icon;
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        icon = QIcon(iconProvider->loadSvgFileAsPixmap(resourcePath, QSize(16, 16), color));
    }

    // Fallback if the app-context icon provider is not available (e.g. in unit tests)
    // or if the pixmap creation failed.
    if (icon.isNull())
    {
        icon = QIcon(resourcePath);
    }

    cache.insert(cacheKey, icon);
    return icon;
}

QString globalNormalizationMethodToText(GlobalNormalizationMethod method)
{
    switch (method)
    {
    case GlobalNormalizationMethod::Additive:
        return QCoreApplication::translate("CalibrationPanelUtils", "Additive");
    case GlobalNormalizationMethod::Multiplicative:
        return QCoreApplication::translate("CalibrationPanelUtils", "Multiplicative");
    case GlobalNormalizationMethod::AdditiveScaling:
        return QCoreApplication::translate("CalibrationPanelUtils", "Additive With Scaling");
    case GlobalNormalizationMethod::MultiplicativeScaling:
        return QCoreApplication::translate("CalibrationPanelUtils", "Multiplicative With Scaling");
    }
    return QCoreApplication::translate("CalibrationPanelUtils", "Additive With Scaling");
}

QString scaleEstimatorMethodToText(ScaleEstimatorMethod method)
{
    switch (method)
    {
    case ScaleEstimatorMethod::MAD:
        return QCoreApplication::translate("CalibrationPanelUtils", "MAD");
    case ScaleEstimatorMethod::IKSS:
        return QCoreApplication::translate("CalibrationPanelUtils", "IKSS");
    }
    return QCoreApplication::translate("CalibrationPanelUtils", "MAD");
}

QString rejectionMethodToText(const StackingSettings& settings)
{
    const QString effective =
        settings.rejectionMethod.trimmed().isEmpty() ? QStringLiteral("None") : settings.rejectionMethod.trimmed();
    const QString requested = settings.requestedRejectionMethod.trimmed();
    if (!requested.isEmpty() && requested.compare(effective, Qt::CaseInsensitive) != 0)
    {
        return QStringLiteral("%1 -> %2").arg(requested, effective);
    }
    return effective;
}
} // namespace

namespace
{
std::optional<GlobalNormalizationMethod> globalNormalizationMethodFromText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("additive"))
    {
        return GlobalNormalizationMethod::Additive;
    }
    if (normalized == QStringLiteral("multiplicative"))
    {
        return GlobalNormalizationMethod::Multiplicative;
    }

    if (normalized.contains(QStringLiteral("additive")) &&
        (normalized.contains(QStringLiteral("with scaling")) || normalized.contains(QStringLiteral("scaling"))))
    {
        return GlobalNormalizationMethod::AdditiveScaling;
    }
    if (normalized.contains(QStringLiteral("multiplicative")) &&
        (normalized.contains(QStringLiteral("with scaling")) || normalized.contains(QStringLiteral("scaling"))))
    {
        return GlobalNormalizationMethod::MultiplicativeScaling;
    }

    return std::nullopt;
}
} // namespace

QString normalizationModeToText(NormalizationMode mode)
{
    switch (mode)
    {
    case NormalizationMode::Local:
        return QStringLiteral("Local");
    case NormalizationMode::Global:
        return QStringLiteral("Global");
    case NormalizationMode::None:
        return QStringLiteral("None");
    }
    return QStringLiteral("None");
}

QString normalizationReferenceModeToText(NormalizationReferenceMode mode)
{
    switch (mode)
    {
    case NormalizationReferenceMode::Manual:
        return QCoreApplication::translate("CalibrationPanelUtils", "Manual");
    case NormalizationReferenceMode::Automatic:
    default:
        return QCoreApplication::translate("CalibrationPanelUtils", "Automatic (best quality)");
    }
}

void applyStackingSettingsToDialog(const StackingSettings& settings, StackingSettingsDialog& dialog)
{
    applyStackingSettingsToDialog(settings, dialog, settings.cometStackingMode);
}

void applyStackingSettingsToDialog(const StackingSettings& settings, StackingSettingsDialog& dialog,
                                   const QString& effectiveCometStackingMode)
{
    dialog.setStackMethod(settings.stackMethod);
    dialog.setNormalizationMode(settings.normalizationMode == NormalizationMode::None
                                    ? QStringLiteral("None")
                                    : globalNormalizationMethodToText(settings.globalNormalizationMethod));
    dialog.setRejectionMethod(settings.rejectionMethod);
    dialog.setSigmaLow(settings.sigmaLow);
    dialog.setSigmaHigh(settings.sigmaHigh);
    dialog.setWeighting(settings.weighting);
    dialog.setCreateRejectionMaps(settings.createRejectionMaps);
    dialog.setMergeRejectionMaps(settings.mergeRejectionMaps);
    dialog.setSpatialRejectionClustering(settings.spatialRejectionClustering);
    dialog.setRestackExcludingFlaggedFrames(settings.restackExcludingFlaggedFrames);
    dialog.setCreateQualityMaps(settings.createQualityMaps);
    dialog.setRegisteredOutputPolicy(settings.registeredOutputPolicy);
    dialog.setStarCountScaling(settings.starCountScaling);
    dialog.setExponentialAlpha(settings.exponentialAlpha);
    dialog.setMinWeightFloor(settings.minWeightFloor);
    dialog.setLocalWeightingEnabled(settings.localWeighting.enabled);
    dialog.setLocalWeightingWindowSize(settings.localWeighting.windowSize);
    dialog.setLocalWeightingGridSpacing(settings.localWeighting.gridSpacing);
    dialog.setLocalWeightingSnrWeightExponent(settings.localWeighting.snrWeightExponent);
    dialog.setDrizzleEnabled(settings.drizzle.enabled);
    dialog.setDrizzleScale(settings.drizzle.scale);
    dialog.setDrizzleKernel(settings.drizzle.kernel);
    dialog.setDrizzlePixfrac(settings.drizzle.pixelFraction);
    dialog.setSolveFinalMasterIfNeeded(settings.solveFinalMasterIfNeeded);
    dialog.setHdrExposureMergeEnabled(settings.hdr.enabled);
    dialog.setCometStackingMode(effectiveCometStackingMode);

    dialog.setNormalizationReferenceMode(settings.normalizationReferenceMode == NormalizationReferenceMode::Manual
                                             ? QStringLiteral("Manual")
                                             : QStringLiteral("Automatic (best quality)"));
    dialog.setManualNormalizationReference(settings.manualNormalizationReference);
    dialog.setScaleEstimator(settings.scaleEstimator);
    dialog.setOverlapNormalizationEnabled(settings.overlapNormalization.enabled);
    dialog.setOverlapMinimumPixels(settings.overlapNormalization.minOverlapPixels);
    dialog.setOverlapMinimumFraction(settings.overlapNormalization.minOverlapFraction);

    dialog.setLocalNormalizationEnabled(settings.localNormalization.enabled);
    dialog.setLocalNormalizationReferenceStrategy(settings.localNormalization.referenceStrategy);
    dialog.setLocalNormalizationMaxIntegratedFrames(settings.localNormalization.maxIntegratedFrames);
    dialog.setLocalNormalizationEvaluationCriteria(settings.localNormalization.evaluationCriteria);
    dialog.setLocalNormalizationGridSize(settings.localNormalization.gridSize);
    dialog.setLocalNormalizationGridInterpolation(settings.localNormalization.gridInterpolation);
    dialog.setLocalNormalizationBackgroundEstimator(settings.localNormalization.backgroundEstimator);
    dialog.setLocalNormalizationBackgroundPercentile(settings.localNormalization.backgroundPercentile);
    dialog.setLocalNormalizationSigmaClipK(settings.localNormalization.sigmaClipK);
    dialog.setLocalNormalizationSigmaClipIterations(settings.localNormalization.sigmaClipIterations);
    dialog.setLocalNormalizationBackgroundSmoothingRadius(settings.localNormalization.backgroundSmoothingRadius);
    dialog.setLocalNormalizationScaleEvaluation(settings.localNormalization.scaleEvaluation);
    dialog.setLocalNormalizationScaleMode(settings.localNormalization.scaleMode);
    dialog.setLocalNormalizationPsfType(settings.localNormalization.psfType);
    dialog.setLocalNormalizationMoffatBeta(settings.localNormalization.moffatBeta);
    dialog.setLocalNormalizationMaxStars(settings.localNormalization.maxStars);
    dialog.setLocalNormalizationMinDetectionSnr(settings.localNormalization.minDetectionSnr);
    dialog.setLocalNormalizationAllowClusteredSources(settings.localNormalization.allowClusteredSources);
    dialog.setLocalNormalizationLowClip(settings.localNormalization.lowClip);
    dialog.setLocalNormalizationHighClip(settings.localNormalization.highClip);
    dialog.setLocalNormalizationHotPixelMedianRadius(settings.localNormalization.hotPixelMedianRadius);
    dialog.setLocalNormalizationNoiseReductionRadius(settings.localNormalization.noiseReductionRadius);
    dialog.setLocalNormalizationLocalScaleEnabled(settings.localNormalization.localScaleEnabled);
    dialog.setLocalNormalizationLocalScaleSmoothingRadius(settings.localNormalization.localScaleSmoothingRadius);
}

StackingSettings stackingSettingsFromDialog(const StackingSettingsDialog& dialog, const StackingSettings& base)
{
    StackingSettings settings = base;
    settings.stackMethod = dialog.getStackMethod();
    settings.rejectionMethod = dialog.getRejectionMethod();
    // The dialog offers no automatic rejection, so the one it gives back is chosen, not one Lite resolved.
    settings.requestedRejectionMethod.clear();
    settings.sigmaLow = dialog.getSigmaLow();
    settings.sigmaHigh = dialog.getSigmaHigh();
    settings.weighting = dialog.getWeighting();
    settings.createRejectionMaps = dialog.getCreateRejectionMaps();
    settings.mergeRejectionMaps = dialog.getMergeRejectionMaps();
    settings.spatialRejectionClustering = dialog.getSpatialRejectionClustering();
    settings.restackExcludingFlaggedFrames = dialog.getRestackExcludingFlaggedFrames();
    settings.createQualityMaps = dialog.getCreateQualityMaps();
    settings.registeredOutputPolicy = dialog.getRegisteredOutputPolicy();
    settings.starCountScaling = dialog.getStarCountScaling();
    settings.exponentialAlpha = dialog.getExponentialAlpha();
    settings.minWeightFloor = dialog.getMinWeightFloor();
    settings.localWeighting.enabled = dialog.getLocalWeightingEnabled();
    settings.localWeighting.windowSize = dialog.getLocalWeightingWindowSize();
    settings.localWeighting.gridSpacing = dialog.getLocalWeightingGridSpacing();
    settings.localWeighting.snrWeightExponent = dialog.getLocalWeightingSnrWeightExponent();

    settings.drizzle.enabled = dialog.getDrizzleEnabled();
    settings.drizzle.scale = dialog.getDrizzleScale();
    settings.drizzle.kernel = dialog.getDrizzleKernel();
    settings.drizzle.pixelFraction = dialog.getDrizzlePixfrac();
    settings.solveFinalMasterIfNeeded = dialog.getSolveFinalMasterIfNeeded();
    settings.hdr.enabled = dialog.getHdrExposureMergeEnabled();
    settings.cometStackingMode = dialog.getCurrentSettings().cometStackingMode;

    const QString refMode = dialog.getNormalizationReferenceMode();
    settings.normalizationReferenceMode = (refMode.compare(QStringLiteral("Manual"), Qt::CaseInsensitive) == 0)
                                              ? NormalizationReferenceMode::Manual
                                              : NormalizationReferenceMode::Automatic;
    settings.manualNormalizationReference = settings.normalizationReferenceMode == NormalizationReferenceMode::Manual
                                                ? dialog.getManualNormalizationReference()
                                                : QString();
    settings.scaleEstimator = dialog.getScaleEstimator();
    settings.overlapNormalization.enabled = dialog.getOverlapNormalizationEnabled();
    settings.overlapNormalization.minOverlapPixels = dialog.getOverlapMinimumPixels();
    settings.overlapNormalization.minOverlapFraction = dialog.getOverlapMinimumFraction();

    settings.localNormalization.enabled = dialog.getLocalNormalizationEnabled();
    settings.localNormalization.referenceStrategy = dialog.getLocalNormalizationReferenceStrategy();
    settings.localNormalization.maxIntegratedFrames = dialog.getLocalNormalizationMaxIntegratedFrames();
    settings.localNormalization.evaluationCriteria = dialog.getLocalNormalizationEvaluationCriteria();
    settings.localNormalization.gridSize = dialog.getLocalNormalizationGridSize();
    settings.localNormalization.gridInterpolation = dialog.getLocalNormalizationGridInterpolation();
    settings.localNormalization.backgroundEstimator = dialog.getLocalNormalizationBackgroundEstimator();
    settings.localNormalization.backgroundPercentile = dialog.getLocalNormalizationBackgroundPercentile();
    settings.localNormalization.sigmaClipK = dialog.getLocalNormalizationSigmaClipK();
    settings.localNormalization.sigmaClipIterations = dialog.getLocalNormalizationSigmaClipIterations();
    settings.localNormalization.backgroundSmoothingRadius = dialog.getLocalNormalizationBackgroundSmoothingRadius();
    settings.localNormalization.scaleEvaluation = dialog.getLocalNormalizationScaleEvaluation();
    settings.localNormalization.scaleMode = dialog.getLocalNormalizationScaleMode();
    settings.localNormalization.psfType = dialog.getLocalNormalizationPsfType();
    settings.localNormalization.moffatBeta = dialog.getLocalNormalizationMoffatBeta();
    settings.localNormalization.maxStars = dialog.getLocalNormalizationMaxStars();
    settings.localNormalization.minDetectionSnr = dialog.getLocalNormalizationMinDetectionSnr();
    settings.localNormalization.allowClusteredSources = dialog.getLocalNormalizationAllowClusteredSources();
    settings.localNormalization.lowClip = dialog.getLocalNormalizationLowClip();
    settings.localNormalization.highClip = dialog.getLocalNormalizationHighClip();
    settings.localNormalization.hotPixelMedianRadius = dialog.getLocalNormalizationHotPixelMedianRadius();
    settings.localNormalization.noiseReductionRadius = dialog.getLocalNormalizationNoiseReductionRadius();
    settings.localNormalization.localScaleEnabled = dialog.getLocalNormalizationLocalScaleEnabled();
    settings.localNormalization.localScaleSmoothingRadius = dialog.getLocalNormalizationLocalScaleSmoothingRadius();

    // The method the group stores, which local normalization keeps for when it is turned off.
    const QString modeText = dialog.getNormalizationMode();
    if (const auto parsed = globalNormalizationMethodFromText(modeText))
    {
        settings.globalNormalizationMethod = *parsed;
    }
    if (settings.localNormalization.enabled)
    {
        settings.normalizationMode = NormalizationMode::Local;
    }
    else if (modeText.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0)
    {
        settings.normalizationMode = NormalizationMode::None;
    }
    else
    {
        settings.normalizationMode = NormalizationMode::Global;
    }

    return ImageIntegration::sanitizeStackingSettings(std::move(settings));
}

std::vector<FrameInfo> lightIntegrationGroupFrames(const IntegrationDataModel& model, const GroupKey& groupKey)
{
    // The collection hands its frames out by value: the copy is held here for as long as the pointers into it are read.
    const std::vector<FrameInfo> lights = model.lightFrames().frames();
    IntegrationGroupEngine engine;
    const auto groups = engine.computeGroups(lights, model.lightIntegrationGroupingOptions());
    const auto group = groups.find(IntegrationGroupKey{groupKey});
    std::vector<FrameInfo> frames;
    if (group == groups.end())
    {
        return frames;
    }
    std::map<FrameId, const FrameInfo*> byId;
    for (const FrameInfo& frame : lights)
    {
        byId[frame.id] = &frame;
    }
    frames.reserve(group->second.size());
    for (const FrameId& id : group->second)
    {
        if (const auto found = byId.find(id); found != byId.end())
        {
            frames.push_back(*found->second);
        }
    }
    return frames;
}

void openStackingSettingsDialog(IntegrationDataModel* model, FrameType type, const GroupKey& groupKey,
                                const QString& groupLabel, QWidget* parent, QUndoStack* undoStack,
                                std::function<void(bool)> finished)
{
    if (!model)
    {
        if (finished)
        {
            finished(false);
        }
        return;
    }

    StackingSettings base = defaultCalibrationStackingSettings(type);
    const auto& map = model->stackingSettings(type);
    if (auto it = map.find(groupKey); it != map.end())
    {
        base = normalizeCalibrationStackingSettings(type, it->second);
    }

    StackingSettingsDialog::FrameType dialogType = StackingSettingsDialog::FrameType::LIGHT;
    switch (type)
    {
    case FrameType::Bias:
        dialogType = StackingSettingsDialog::FrameType::BIAS;
        break;
    case FrameType::Dark:
        dialogType = StackingSettingsDialog::FrameType::DARK;
        break;
    case FrameType::Flat:
        dialogType = StackingSettingsDialog::FrameType::FLAT;
        break;
    case FrameType::Light:
        dialogType = StackingSettingsDialog::FrameType::LIGHT;
        break;
    }

    // Shown without a nested event loop: the process runs inside a host whose event loop must keep answering, so the
    // settings are saved when the dialog finishes rather than when a modal call returns.
    auto* dialog = new StackingSettingsDialog(dialogType, groupLabel, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // Look up frame count for this group to enable intelligent recommendations.
    // The actual setFrameCount call is deferred until after applyStackingSettingsToDialog
    // so the recommendation banner reflects the user's saved settings, not defaults.
    int frameCount = 0;
    std::vector<FrameInfo> tempFrames;
    const std::vector<FrameInfo>* groupFramesPtr = nullptr;

    if (type == FrameType::Light)
    {
        // For LIGHT frames, we are dealing with Integration Groups (Stacking),
        // so we must use the IntegrationGroupEngine to match the key using integration options.
        tempFrames = lightIntegrationGroupFrames(*model, groupKey);
        frameCount = static_cast<int>(tempFrames.size());
        if (frameCount > 0)
        {
            groupFramesPtr = &tempFrames;
        }
    }
    else
    {
        // For calibration frames (Bias/Dark/Flat), we use the cached calibration groups from the model.
        const GroupedFrameView* groupView = nullptr;
        switch (type)
        {
        case FrameType::Bias:
            groupView = &model->biasGroups();
            break;
        case FrameType::Dark:
            groupView = &model->darkGroups();
            break;
        case FrameType::Flat:
            groupView = &model->flatGroups();
            break;
        default:
            break;
        }

        if (groupView)
        {
            for (const auto& group : *groupView)
            {
                if (group.key == groupKey)
                {
                    frameCount = static_cast<int>(group.frames.size());
                    groupFramesPtr = &group.frames;
                    break;
                }
            }
        }
    }

    if (type == FrameType::Light)
    {
        dialog->setNormalizationReferenceCandidates(normalizationReferenceCandidates(model, groupKey));

        const auto photometricCoverage = groupFramesPtr ? loadPhotometricCoverage(model, *groupFramesPtr)
                                                        : ImageIntegration::PhotometricCoverageSummary{};
        dialog->setPhotometricWeightingAvailable(photometricCoverage.photometricWeightingAvailable());
        if (ImageIntegration::isPhotometricWeightingMode(base.weighting) &&
            !photometricCoverage.photometricWeightingAvailable())
        {
            LOG_WARNING(
                "CalibrationPanelUtils: Photometric weighting is unavailable for group '{}'; falling back to None",
                groupKey.toString().toStdString());
            base.weighting = QStringLiteral("None");
        }

        // Compute drizzle analysis asynchronously (LIGHT frames only) so dialog open
        // stays responsive even when reading/parsing registration transforms is slow.
        if (groupFramesPtr && !groupFramesPtr->empty())
        {
            const QString outputDir = model->outputDirectory();
            std::vector<FrameInfo> framesForAnalysis = *groupFramesPtr;
            QPointer<StackingSettingsDialog> dialogGuard(dialog);
            const QString requestKey = groupKey.toString();

            DrizzleAnalysisDispatcher::instance().request(requestKey, outputDir, std::move(framesForAnalysis),
                                                          [dialogGuard](const DrizzleAnalysis& analysis) {
                                                              if (dialogGuard)
                                                              {
                                                                  dialogGuard->setDrizzleAnalysis(analysis);
                                                              }
                                                          });
        }
    }
    QString effectiveCometStackingMode = base.cometStackingMode;
    if (type == FrameType::Light && groupFramesPtr)
    {
        effectiveCometStackingMode = cometStackingModeForLightGroupRegistration(model, groupFramesPtr);
    }
    applyStackingSettingsToDialog(base, *dialog, effectiveCometStackingMode);

    if (type == FrameType::Light)
    {
        const auto updateAutomaticReferenceDisplay = [dialog, model, groupKey]() {
            if (const auto best = bestNormalizationReferenceCandidate(model, groupKey); best.has_value())
            {
                dialog->setManualNormalizationReference(best->canonicalPath());
            }
        };

        if (base.normalizationReferenceMode == NormalizationReferenceMode::Automatic ||
            base.manualNormalizationReference.trimmed().isEmpty())
        {
            updateAutomaticReferenceDisplay();
        }

        QObject::connect(dialog, &StackingSettingsDialog::automaticNormalizationReferenceDisplayRequested, dialog,
                         updateAutomaticReferenceDisplay);
        QObject::connect(dialog, &StackingSettingsDialog::bestNormalizationReferenceRequested, dialog,
                         [dialog, model, groupKey]() {
                             const auto best = bestNormalizationReferenceCandidate(model, groupKey);
                             if (!best.has_value())
                             {
                                 ImageIntegration::detail::openNonBlockingMessageBox(
                                     dialog, QMessageBox::Information,
                                     QCoreApplication::translate("CalibrationPanelUtils", "Best Reference"),
                                     QCoreApplication::translate(
                                         "CalibrationPanelUtils",
                                         "No frame-quality metrics are available for this stacking group yet."));
                                 return;
                             }

                             dialog->setNormalizationReferenceMode(QStringLiteral("Manual"));
                             dialog->setManualNormalizationReference(best->canonicalPath());
                         });
    }

    // Now set frame count so the banner compares against the user's actual saved settings
    dialog->setFrameCount(frameCount);

    const QPointer<IntegrationDataModel> modelGuard(model);
    const QPointer<QUndoStack> undoGuard(undoStack);
    const auto appliedToAll = std::make_shared<bool>(false);
    QObject::connect(
        dialog, &StackingSettingsDialog::applyToAllRequested, dialog,
        [dialog, base, type, groupKey, modelGuard, undoGuard, appliedToAll]() {
            IntegrationDataModel* model = modelGuard.data();
            if (!model)
            {
                return;
            }
            QUndoStack* undoStack = undoGuard.data();

            *appliedToAll = true;

            std::vector<GroupKey> keys;
            keys.reserve(256);

            switch (type)
            {
            case FrameType::Bias:
                for (const auto& group : model->biasGroups())
                {
                    if (!group.frames.empty())
                    {
                        keys.push_back(group.key);
                    }
                }
                break;
            case FrameType::Dark:
                for (const auto& group : model->darkGroups())
                {
                    if (!group.frames.empty())
                    {
                        keys.push_back(group.key);
                    }
                }
                break;
            case FrameType::Flat:
                for (const auto& group : model->flatGroups())
                {
                    if (!group.frames.empty())
                    {
                        keys.push_back(group.key);
                    }
                }
                break;
            case FrameType::Light:
                // Apply to all integration groups (stacking tab groups).
                keys = model->integrationGroups();
                break;
            }

            if (std::find(keys.begin(), keys.end(), groupKey) == keys.end())
            {
                keys.push_back(groupKey);
            }

            const auto currentMap = model->stackingSettings(type);
            const auto baseForKey = [&](const GroupKey& key) {
                if (key == groupKey)
                {
                    return base;
                }

                StackingSettings settings = defaultCalibrationStackingSettings(type);
                if (const auto it = currentMap.find(key); it != currentMap.end())
                {
                    settings = normalizeCalibrationStackingSettings(type, it->second);
                }
                return settings;
            };

            const auto updatedForKey = [&](const GroupKey& key) {
                StackingSettings updated = stackingSettingsFromDialog(*dialog, baseForKey(key));
                return normalizeCalibrationStackingSettings(type, updated);
            };

            if (undoStack)
            {
                model->beginBatchUpdate();
                undoStack->beginMacro(QCoreApplication::translate("CalibrationPanelUtils", "Update stacking settings"));
                for (const auto& key : keys)
                {
                    StackingSettings oldSettings = baseForKey(key);
                    undoStack->push(new SetStackingSettingsCommand(model, type, key, oldSettings, updatedForKey(key)));
                }
                undoStack->endMacro();
                model->endBatchUpdate();
            }
            else
            {
                for (const auto& key : keys)
                {
                    model->setStackingSettings(type, key, updatedForKey(key));
                }
            }

            dialog->accept();
        });

    QObject::connect(dialog, &QDialog::finished, dialog,
                     [dialog, base, type, groupKey, modelGuard, undoGuard, appliedToAll,
                      finished = std::move(finished)](const int result) {
                         IntegrationDataModel* model = modelGuard.data();
                         if (result != QDialog::Accepted || !model)
                         {
                             if (finished)
                             {
                                 finished(false);
                             }
                             return;
                         }

                         if (!*appliedToAll)
                         {
                             StackingSettings updated = stackingSettingsFromDialog(*dialog, base);
                             updated = normalizeCalibrationStackingSettings(type, updated);
                             if (QUndoStack* undoStack = undoGuard.data())
                             {
                                 undoStack->push(new SetStackingSettingsCommand(model, type, groupKey, base, updated));
                             }
                             else
                             {
                                 model->setStackingSettings(type, groupKey, updated);
                             }
                         }

                         if (finished)
                         {
                             finished(true);
                         }
                     });
    dialog->open();
}

void openCalibrationMasterFileDialog(QWidget* parent, const QString& title, const QString& filter,
                                     std::function<void(const QString&)> chosen)
{
    static const QString contextKey = QStringLiteral("__CALIBRATION_MASTER_BROWSE__");
    QString initialDir = process_utils::selectPath(contextKey);
    if (!initialDir.isEmpty() && !QDir(initialDir).exists())
    {
        initialDir.clear();
    }

    // The application's open-file dialog, shown without a nested event loop: the process runs inside a host whose
    // event loop must keep answering.
    auto* dialog = new QFileDialog(parent, title, initialDir, filter);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAcceptMode(QFileDialog::AcceptOpen);
    dialog->setFileMode(QFileDialog::ExistingFile);
    an::ui::dialogs::applyCompactOpenFilterPresentation(*dialog);
    dialog->setOptions(
        an::ui::dialogs::resolveDialogOptions(process_utils::currentGlobalSettings(), QFileDialog::Options()));
    an::ui::dialogs::detail::applyNonNativeDialogChrome(*dialog);
    QObject::connect(dialog, &QFileDialog::fileSelected, dialog, [chosen = std::move(chosen)](const QString& path) {
        if (path.trimmed().isEmpty())
        {
            return;
        }
        process_utils::rememberPath(contextKey, QFileInfo(path).absolutePath());
        if (chosen)
        {
            chosen(path);
        }
    });
    dialog->open();
}

QString calibrationMatchDisplayText(const CalibrationMatch& match, const QString& missingText)
{
    if (match.type == CalibrationMatch::MatchType::None)
    {
        return missingText;
    }

    if (match.type == CalibrationMatch::MatchType::ManuallyUnassigned)
    {
        return QCoreApplication::translate("CalibrationPanelUtils", "(Disabled)");
    }

    return matchDisplayName(match);
}

QString calibrationMatchLabel(const CalibrationMatch& match, const QString& okPrefix, const QString& warnPrefix,
                              const QString& overridePrefix, const QString& missingText, const QString& checkPrefix)
{
    Q_UNUSED(okPrefix);
    Q_UNUSED(warnPrefix);
    Q_UNUSED(overridePrefix);
    Q_UNUSED(checkPrefix);
    return calibrationMatchDisplayText(match, missingText);
}

QString calibrationMatchTooltip(const CalibrationMatch& match, const QString& missingMessage, bool warnOnFallback)
{
    QStringList tooltip;

    if (match.type == CalibrationMatch::MatchType::None)
    {
        if (!missingMessage.trimmed().isEmpty())
        {
            tooltip << missingMessage;
        }
        return tooltip.join(QStringLiteral("\n"));
    }

    if (match.type == CalibrationMatch::MatchType::ManuallyUnassigned)
    {
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Calibration manually disabled.");
        tooltip << QString();
        tooltip << QCoreApplication::translate(
            "CalibrationPanelUtils", "You have explicitly chosen not to use any calibration master for this type. "
                                     "Right-click to re-enable auto-detection or select a specific master.");
        return tooltip.join(QStringLiteral("\n"));
    }

    if (match.needsUserAttention)
    {
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Ambiguous match: please review.");
    }

    if (!match.warningMessage.trimmed().isEmpty())
    {
        tooltip << match.warningMessage;
    }

    switch (match.type)
    {
    case CalibrationMatch::MatchType::Exact:
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Match type: exact");
        break;
    case CalibrationMatch::MatchType::ManualOverride:
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Match type: manual override");
        break;
    case CalibrationMatch::MatchType::ClosestDate:
        tooltip << (warnOnFallback
                        ? QCoreApplication::translate("CalibrationPanelUtils", "Match type: fallback (closest session)")
                        : QCoreApplication::translate("CalibrationPanelUtils", "Match type: closest session"));
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Session difference: +/- %1 days")
                       .arg(std::abs(match.dateDifferencesDays));
        break;
    case CalibrationMatch::MatchType::ClosestExposure:
        tooltip << (warnOnFallback
                        ? QCoreApplication::translate("CalibrationPanelUtils",
                                                      "Match type: fallback (closest exposure)")
                        : QCoreApplication::translate("CalibrationPanelUtils", "Match type: closest exposure"));
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Exposure difference: +/- %1 s")
                       .arg(match.exposureDifference, 0, 'f', 2);
        break;
    case CalibrationMatch::MatchType::ManuallyUnassigned:
    case CalibrationMatch::MatchType::None:
        break;
    }

    tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Master: %1").arg(matchDisplayName(match));

    if (!match.masterPath.trimmed().isEmpty())
    {
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Path: %1").arg(match.masterPath);
    }

    if (!match.matchedGroupKey.trimmed().isEmpty())
    {
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Matched group: %1")
                       .arg(compactGroupKeyForDisplay(match.matchedGroupKey));
    }

    if (match.confidence > 0.0)
    {
        const int pct = std::clamp(static_cast<int>(match.confidence * 100.0 + 0.5), 0, 100);
        tooltip << QCoreApplication::translate("CalibrationPanelUtils", "Confidence: %1%").arg(pct);
    }

    return tooltip.join(QStringLiteral("\n"));
}

QColor calibrationMatchColor(const CalibrationMatch& match, bool warnOnFallback)
{
    const auto& colors = an::ui::tokens().colors;

    if (match.needsUserAttention)
    {
        return colors.statusError;
    }

    switch (match.type)
    {
    case CalibrationMatch::MatchType::Exact:
    case CalibrationMatch::MatchType::ManualOverride:
        return colors.statusSuccess;
    case CalibrationMatch::MatchType::ClosestDate:
    case CalibrationMatch::MatchType::ClosestExposure:
        return warnOnFallback ? colors.statusWarning : colors.statusSuccess;
    case CalibrationMatch::MatchType::ManuallyUnassigned:
        return colors.statusWarning;
    case CalibrationMatch::MatchType::None:
        return colors.statusError;
    }

    return colors.statusError;
}

QIcon calibrationMatchIcon(const CalibrationMatch& match, bool warnOnFallback)
{
    const auto& colors = an::ui::tokens().colors;
    const QColor okColor = colors.statusSuccess;
    const QColor warningColor = colors.statusWarning;
    const QColor errorColor = colors.statusError;
    const QColor manualColor = colors.statusInfo;

    if (match.needsUserAttention)
    {
        return tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/exclamation.svg"), errorColor);
    }

    switch (match.type)
    {
    case CalibrationMatch::MatchType::Exact:
        return tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/check.svg"), okColor);
    case CalibrationMatch::MatchType::ManualOverride:
        return tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/edit.svg"), manualColor);
    case CalibrationMatch::MatchType::ClosestDate:
    case CalibrationMatch::MatchType::ClosestExposure:
        return warnOnFallback ? tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/warning.svg"), warningColor)
                              : tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/check.svg"), okColor);
    case CalibrationMatch::MatchType::ManuallyUnassigned:
        return tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/warning.svg"), warningColor);
    case CalibrationMatch::MatchType::None:
        return tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/exclamation.svg"), errorColor);
    }

    return tintedSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/exclamation.svg"), errorColor);
}

QString masterSourceLabel(MasterSource source)
{
    switch (source)
    {
    case MasterSource::UserDefined:
        return QStringLiteral("User Defined");
    case MasterSource::AutoDetected:
        return QStringLiteral("Auto Detected");
    case MasterSource::AutoGenerated:
        return QStringLiteral("Auto Generated");
    case MasterSource::Unknown:
        break;
    }

    return QStringLiteral("Unknown");
}

QString calibrationMasterOutputPath(IntegrationDataModel* model, CalibrationOverrideType type, const GroupKey& groupKey,
                                    const GroupingOptions& options, FrameType frameType)
{
    const QString filename = calibrationMasterOutputFilename(type, groupKey, options, frameType);
    if (!model)
    {
        return filename;
    }

    QString outputDir = model->outputDirectory().trimmed();
    if (outputDir.size() >= 2 && ((outputDir.startsWith(QLatin1Char('"')) && outputDir.endsWith(QLatin1Char('"'))) ||
                                  (outputDir.startsWith(QLatin1Char('\'')) && outputDir.endsWith(QLatin1Char('\'')))))
    {
        outputDir = outputDir.mid(1, outputDir.size() - 2).trimmed();
    }

    if (outputDir.isEmpty())
    {
        return filename;
    }
    return QDir(outputDir).absoluteFilePath(filename);
}

QString canonicalMasterPathForGroup(IntegrationDataModel* model, CalibrationOverrideType type,
                                    const GroupedFrameSet& group)
{
    if (!model)
    {
        return QString();
    }

    QString bestExisting;
    for (const auto& frame : group.frames)
    {
        if (!model->isMasterFrame(frame.id))
        {
            continue;
        }
        const QString candidate = frame.id.canonicalPath();
        if (bestExisting.isEmpty() || candidate < bestExisting)
        {
            bestExisting = candidate;
        }
    }
    if (!bestExisting.isEmpty())
    {
        return bestExisting;
    }

    FrameType frameType = FrameType::Bias;
    switch (type)
    {
    case CalibrationOverrideType::Bias:
        frameType = FrameType::Bias;
        break;
    case CalibrationOverrideType::Dark:
        frameType = FrameType::Dark;
        break;
    case CalibrationOverrideType::Flat:
        frameType = FrameType::Flat;
        break;
    }

    const auto& options = model->groupingOptions(frameType);
    return calibrationMasterOutputPath(model, type, group.key, options, frameType);
}

QString formatGroupLabel(const GroupKey& key, const QString& fallback, const GroupingOptions& options,
                         FrameType frameType)
{
    const QString value = key.toDisplayString(options, frameType);
    return value.trimmed().isEmpty() ? fallback : value;
}

QString groupTooltip(FrameType frameType, const GroupKey& groupKey, const GroupingOptions& options,
                     const std::vector<FrameInfo>& frames)
{
    QStringList lines;

    const QString group_label = groupKey.toDisplayString(options, frameType);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Group: %1")
                 .arg(group_label.trimmed().isEmpty()
                          ? QCoreApplication::translate("CalibrationPanelUtils", "[Ungrouped]")
                          : group_label);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Key: %1").arg(groupKey.toCompactString());

    if (!frames.empty())
    {
        const FrameInfo& frame = frames.front();
        lines << QString();
        lines << QCoreApplication::translate("CalibrationPanelUtils", "Representative frame:");
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Path: %1").arg(frame.id.canonicalPath());
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Resolution: %1")
                     .arg(frame.metadata.resolutionString());
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  ISO/Gain: %1")
                     .arg(optionalIntText(frame.metadata.isoGain()));
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Offset: %1")
                     .arg(optionalIntText(frame.metadata.offset()));

        if (frameType == FrameType::Light || frameType == FrameType::Dark)
        {
            lines << QCoreApplication::translate("CalibrationPanelUtils", "  Exposure: %1 s")
                         .arg(frame.metadata.exposureSeconds(), 0, 'f', 2);
        }
        if (frameType == FrameType::Light || frameType == FrameType::Dark)
        {
            lines << QCoreApplication::translate("CalibrationPanelUtils", "  Temperature: %1 °C")
                         .arg(optionalDoubleText(frame.metadata.temperatureC(), 2));
        }
        if (frameType == FrameType::Light || frameType == FrameType::Flat)
        {
            lines << QCoreApplication::translate("CalibrationPanelUtils", "  Filter: %1").arg(frame.metadata.filter());
            lines << QCoreApplication::translate("CalibrationPanelUtils", "  Object: %1")
                         .arg(frame.effectiveObjectName());
            lines
                << QCoreApplication::translate("CalibrationPanelUtils", "  Session: %1").arg(frame.effectiveSession());
        }
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Custom Tag: %1")
                     .arg(frame.customTag.trimmed().isEmpty() ? QStringLiteral("-") : frame.customTag);
    }

    lines << QString();
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Grouping criteria:");
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Resolution: %1").arg(yesNo(options.byResolution));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  ISO/Gain: %1").arg(yesNo(options.byIsoGain));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Offset: %1").arg(yesNo(options.byOffset));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Exposure: %1 (±%2 s)")
                 .arg(yesNo(options.byExposure))
                 .arg(options.exposureToleranceSeconds, 0, 'f', 1);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Temperature: %1 (±%2 °C)")
                 .arg(yesNo(options.byTemperature))
                 .arg(options.temperatureToleranceC, 0, 'f', 1);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Filter: %1").arg(yesNo(options.byFilter));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Object: %1").arg(yesNo(options.byObject));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Session: %1").arg(yesNo(options.bySession));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Custom Tag: %1").arg(yesNo(options.byCustomTag));

    return lines.join(QStringLiteral("\n"));
}

QString framesCountTooltip(IntegrationDataModel* model, FrameType frameType, const std::vector<FrameInfo>& frames)
{
    const int total = static_cast<int>(frames.size());
    int excluded = 0;
    int included = 0;
    int masters = 0;
    int included_non_master = 0;

    for (const auto& frame : frames)
    {
        if (frame.excluded)
        {
            ++excluded;
            continue;
        }

        ++included;
        const bool is_master = model && model->isMasterFrame(frame.id);
        if (is_master)
        {
            ++masters;
        }
        else
        {
            ++included_non_master;
        }
    }

    QStringList lines;
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Frames in this group:");
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Total: %1").arg(total);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Included: %1").arg(included);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Excluded: %1").arg(excluded);

    if (frameType != FrameType::Light)
    {
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Master files: %1").arg(masters);
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Stack inputs (included, non-master): %1")
                     .arg(included_non_master);
    }

    return lines.join(QStringLiteral("\n"));
}

QString masterSourceTooltip(MasterSource source)
{
    switch (source)
    {
    case MasterSource::UserDefined:
        return QCoreApplication::translate("CalibrationPanelUtils",
                                           "User-defined master: provided by the user and used as-is.\n"
                                           "This master file is not calibrated by AfterNight.");
    case MasterSource::AutoDetected:
        return QCoreApplication::translate("CalibrationPanelUtils",
                                           "Auto-detected master: an existing master file found on disk.\n"
                                           "This master file is not calibrated by AfterNight.");
    case MasterSource::AutoGenerated:
        return QCoreApplication::translate("CalibrationPanelUtils",
                                           "Auto-generated master: produced by stacking the frames in this group.\n"
                                           "Used as-is in subsequent workflow steps.");
    case MasterSource::Unknown:
        break;
    }
    return QCoreApplication::translate(
        "CalibrationPanelUtils",
        "Will generate: this master will be produced by stacking the frames in this group.\n"
        "During stacking, the input frames will be calibrated as required before integration.");
}

QString stackingSettingsReport(const StackingSettings& settings)
{
    QStringList lines;
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Stacking:");
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Stack Method: %1").arg(settings.stackMethod);
    lines
        << QCoreApplication::translate("CalibrationPanelUtils", "  Rejection: %1").arg(rejectionMethodToText(settings));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Sigma Low / High: %1 / %2")
                 .arg(settings.sigmaLow, 0, 'f', 2)
                 .arg(settings.sigmaHigh, 0, 'f', 2);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Weighting: %1").arg(settings.weighting);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Rejection Maps: create=%1, merge=%2")
                 .arg(yesNo(settings.createRejectionMaps))
                 .arg(yesNo(settings.mergeRejectionMaps));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Spatial Clustering: %1 | Re-stack flagged: %2")
                 .arg(yesNo(settings.spatialRejectionClustering))
                 .arg(yesNo(settings.restackExcludingFlaggedFrames));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Quality Maps: %1")
                 .arg(yesNo(settings.createQualityMaps));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  HDR Exposure Merge: %1")
                 .arg(yesNo(settings.hdr.enabled));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Star Count Scaling: %1")
                 .arg(settings.starCountScaling);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Exponential Alpha: %1")
                 .arg(settings.exponentialAlpha, 0, 'f', 2);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Min Weight Floor: %1")
                 .arg(settings.minWeightFloor, 0, 'f', 3);

    lines << QString();
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Normalization:");
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Mode: %1")
                 .arg(normalizationModeToText(settings.normalizationMode));
    // Local normalization matches each light globally in Additive With Scaling, whatever method the group stores.
    const bool localNormalization =
        settings.normalizationMode == NormalizationMode::Local && settings.localNormalization.enabled;
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Global Method: %1")
                 .arg(localNormalization
                          ? QCoreApplication::translate("CalibrationPanelUtils", "%1 (set by local normalization)")
                                .arg(globalNormalizationMethodToText(GlobalNormalizationMethod::AdditiveScaling))
                          : globalNormalizationMethodToText(settings.globalNormalizationMethod));
    if (settings.normalizationMode == NormalizationMode::Local ||
        (settings.normalizationMode == NormalizationMode::Global &&
         ImageIntegration::normalizationUsesScaleEstimator(settings.globalNormalizationMethod)))
    {
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Scale Estimator: %1")
                     .arg(scaleEstimatorMethodToText(settings.scaleEstimator));
    }
    if (settings.normalizationMode == NormalizationMode::Global)
    {
        lines << (settings.overlapNormalization.enabled
                      ? QCoreApplication::translate("CalibrationPanelUtils",
                                                    "  Overlap Normalization: Yes (at least %1 px and %2 of the "
                                                    "smaller light)")
                            .arg(settings.overlapNormalization.minOverlapPixels)
                            .arg(settings.overlapNormalization.minOverlapFraction, 0, 'g', 6)
                      : QCoreApplication::translate("CalibrationPanelUtils", "  Overlap Normalization: No"));
    }
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Reference Mode: %1")
                 .arg(normalizationReferenceModeToText(settings.normalizationReferenceMode));
    if (settings.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
        !settings.manualNormalizationReference.trimmed().isEmpty())
    {
        const QString refName = QFileInfo(settings.manualNormalizationReference).fileName();
        const QString label = refName.isEmpty() ? settings.manualNormalizationReference : refName;
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Reference Frame: %1").arg(label);
    }

    lines << QString();
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Local Normalization:");
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Enabled: %1")
                 .arg(yesNo(settings.localNormalization.enabled));
    if (localNormalization && settings.drizzle.enabled)
    {
        // A drizzled group has no normalization stage: its stack fits the surfaces on the drizzled lights.
        lines << QCoreApplication::translate("CalibrationPanelUtils",
                                             "  Fitted by the drizzled stack: the reference strategy, the evaluation, "
                                             "the PSF flux scale and the hot-pixel and noise filters do not apply");
    }
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Reference Strategy: %1")
                 .arg(settings.localNormalization.referenceStrategy);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Max Integrated Frames: %1")
                 .arg(settings.localNormalization.maxIntegratedFrames);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Evaluation Criteria: %1")
                 .arg(settings.localNormalization.evaluationCriteria);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Grid Size: %1")
                 .arg(settings.localNormalization.gridSize);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Grid Interpolation: %1")
                 .arg(settings.localNormalization.gridInterpolation);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Background Estimator: %1")
                 .arg(settings.localNormalization.backgroundEstimator);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Background Percentile: %1")
                 .arg(settings.localNormalization.backgroundPercentile, 0, 'f', 3);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Sigma Clip K / Iterations: %1 / %2")
                 .arg(settings.localNormalization.sigmaClipK, 0, 'f', 2)
                 .arg(settings.localNormalization.sigmaClipIterations);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  BG Smoothing Radius: %1")
                 .arg(settings.localNormalization.backgroundSmoothingRadius);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Scale Evaluation: %1")
                 .arg(settings.localNormalization.scaleEvaluation);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Scale Mode: %1")
                 .arg(settings.localNormalization.scaleMode);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  PSF Type: %1")
                 .arg(settings.localNormalization.psfType);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Moffat Beta: %1")
                 .arg(settings.localNormalization.moffatBeta, 0, 'f', 1);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Max Stars: %1")
                 .arg(settings.localNormalization.maxStars);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Min SNR (flux scale): %1")
                 .arg(settings.localNormalization.minDetectionSnr, 0, 'f', 2);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Allow Clustered Sources: %1")
                 .arg(yesNo(settings.localNormalization.allowClusteredSources));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Low / High Clip: %1 / %2")
                 .arg(settings.localNormalization.lowClip, 0, 'g', 6)
                 .arg(settings.localNormalization.highClip, 0, 'g', 6);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Hot Pixel Median Radius: %1")
                 .arg(settings.localNormalization.hotPixelMedianRadius);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Noise Reduction Radius: %1")
                 .arg(settings.localNormalization.noiseReductionRadius);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Local Scale Enabled: %1")
                 .arg(yesNo(settings.localNormalization.localScaleEnabled));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Local Scale Smoothing Radius: %1")
                 .arg(settings.localNormalization.localScaleSmoothingRadius);

    lines << QString();
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Drizzle:");
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Enabled: %1").arg(yesNo(settings.drizzle.enabled));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Scale: %1").arg(settings.drizzle.scale, 0, 'f', 2);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Pixel Fraction: %1")
                 .arg(settings.drizzle.pixelFraction, 0, 'f', 2);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  Kernel: %1").arg(settings.drizzle.kernel);
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  CFA-aware: %1")
                 .arg(yesNo(settings.drizzle.cfaAware));
    lines << QCoreApplication::translate("CalibrationPanelUtils", "  CFA Pattern: %1").arg(settings.drizzle.cfaPattern);

    return lines.join(QStringLiteral("\n"));
}

QString stackingSettingsTooltip(IntegrationDataModel* model, FrameType frameType, const GroupKey& groupKey,
                                const StackingSettingsMap& settings_map)
{
    const auto it = settings_map.find(groupKey);
    const bool is_custom = it != settings_map.end();
    const StackingSettings effective = is_custom ? normalizeCalibrationStackingSettings(frameType, it->second)
                                                 : defaultCalibrationStackingSettings(frameType);

    QStringList lines;
    lines << (is_custom ? QCoreApplication::translate("CalibrationPanelUtils", "Custom settings (this group)")
                        : QCoreApplication::translate("CalibrationPanelUtils", "Default settings (this group)"));
    lines << QString();
    lines << stackingSettingsReport(effective);
    lines << QString();
    lines << QCoreApplication::translate(
        "CalibrationPanelUtils",
        "Tip: Use the Stacking Settings button (or double-click the row) to edit stacking settings for this group.");
    Q_UNUSED(model);
    return lines.join(QStringLiteral("\n"));
}

QString stackingMethodColumnText(const StackingSettings& settings)
{
    if (settings.hdr.enabled)
    {
        return QCoreApplication::translate("CalibrationPanelUtils", "HDR Merge (%1)").arg(settings.stackMethod);
    }

    if (settings.stackMethod != QStringLiteral("Average") || settings.rejectionMethod == QStringLiteral("None"))
    {
        return settings.stackMethod;
    }

    QString text = settings.rejectionMethod;

    const QString method = settings.rejectionMethod;
    const bool needsParams = (method == QStringLiteral("Linear Fit")) ||
                             method.contains(QStringLiteral("sigma"), Qt::CaseInsensitive) ||
                             method.contains(QStringLiteral("mad"), Qt::CaseInsensitive) ||
                             method.contains(QStringLiteral("winsorized"), Qt::CaseInsensitive);

    if (needsParams)
    {
        text += QStringLiteral(" (%1|%2)").arg(settings.sigmaLow, 0, 'f', 1).arg(settings.sigmaHigh, 0, 'f', 1);
    }

    return text;
}

QString normalizationColumnText(const StackingSettings& settings)
{
    if (settings.localNormalization.enabled || settings.normalizationMode == NormalizationMode::Local)
    {
        return QCoreApplication::translate("CalibrationPanelUtils", "Local Normalization");
    }

    if (settings.normalizationMode == NormalizationMode::None)
    {
        return QCoreApplication::translate("CalibrationPanelUtils", "None");
    }

    return globalNormalizationMethodToText(settings.globalNormalizationMethod);
}

QString lightCalibrationSettingsReport(const LightCalibrationSettings& settings)
{
    QStringList lines;
    lines << QCoreApplication::translate("CalibrationPanelUtils", "Light calibration settings:");
    if (settings.preCalibrated)
    {
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  ⚠ Pre-Calibrated: Yes (calibration skipped)");
    }
    else
    {
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Cosmetic Correction: %1")
                     .arg(yesNo(settings.cosmeticCorrection));
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Sigma Cold / Hot: %1 / %2")
                     .arg(settings.sigmaCold, 0, 'f', 2)
                     .arg(settings.sigmaHot, 0, 'f', 2);
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Dark Optimization: %1")
                     .arg(yesNo(settings.darkOptimization));
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Dark Multiplication Factor: %1")
                     .arg(settings.darkMultiplicationFactor, 0, 'f', 3);
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Flat Auto Level: %1")
                     .arg(yesNo(settings.flatAutoLevel));
        lines << QCoreApplication::translate("CalibrationPanelUtils", "  Pedestal: %1 ADU")
                     .arg(settings.pedestal, 0, 'f', 1);
    }
    return lines.join(QStringLiteral("\n"));
}

CalibrationMatcher buildConfiguredCalibrationMatcher(IntegrationDataModel* model)
{
    CalibrationMatcher matcher(GroupingOptions{}, GroupingOptions{}, GroupingOptions{}, CalibrationMatchingPolicy{});
    if (!model)
    {
        return matcher;
    }

    matcher = CalibrationMatcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                                 model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());
    matcher.setOverrides(model->calibrationOverrides());
    matcher.setBiasGroups(collectBiasGroups(model));
    matcher.setDarkGroups(collectDarkGroups(model));
    matcher.setFlatGroups(collectFlatGroups(model));
    return matcher;
}

std::vector<BiasCalibrationGroup> collectBiasGroups(IntegrationDataModel* model)
{
    std::vector<BiasCalibrationGroup> groups;
    if (!model)
        return groups;

    for (const auto& group : model->biasGroups())
    {
        if (group.frames.empty())
        {
            continue;
        }
        BiasCalibrationGroup entry;
        entry.key = BiasGroupKey::fromFrame(group.frames.front());
        entry.groupKey = group.key;
        entry.masterPath = canonicalMasterPathForGroup(model, CalibrationOverrideType::Bias, group);
        groups.push_back(std::move(entry));
    }
    return groups;
}

std::vector<DarkCalibrationGroup> collectDarkGroups(IntegrationDataModel* model)
{
    std::vector<DarkCalibrationGroup> groups;
    if (!model)
        return groups;

    for (const auto& group : model->darkGroups())
    {
        if (group.frames.empty())
        {
            continue;
        }
        DarkCalibrationGroup entry;
        entry.key = DarkGroupKey::fromFrame(group.frames.front());
        entry.groupKey = group.key;
        entry.masterPath = canonicalMasterPathForGroup(model, CalibrationOverrideType::Dark, group);
        groups.push_back(std::move(entry));
    }
    return groups;
}

std::vector<FlatCalibrationGroup> collectFlatGroups(IntegrationDataModel* model)
{
    std::vector<FlatCalibrationGroup> groups;
    if (!model)
        return groups;

    for (const auto& group : model->flatGroups())
    {
        if (group.frames.empty())
        {
            continue;
        }
        FlatCalibrationGroup entry;
        entry.key = FlatGroupKey::fromFrame(group.frames.front());
        entry.groupKey = group.key;
        entry.masterPath = canonicalMasterPathForGroup(model, CalibrationOverrideType::Flat, group);
        groups.push_back(std::move(entry));
    }
    return groups;
}

std::optional<LightGroupKey> lightKeyForGroup(IntegrationDataModel* model, const GroupKey& groupKey)
{
    if (!model)
        return std::nullopt;
    for (const auto& group : model->lightGroups())
    {
        if (group.key == groupKey && !group.frames.empty())
        {
            return LightGroupKey::fromFrame(group.frames.front());
        }
    }
    return std::nullopt;
}

std::optional<LightGroupKey> lightKeyForFlatGroup(IntegrationDataModel* model, const GroupKey& groupKey)
{
    if (!model)
        return std::nullopt;
    for (const auto& group : model->flatGroups())
    {
        if (group.key == groupKey && !group.frames.empty())
        {
            return LightGroupKey::fromFrame(group.frames.front());
        }
    }
    return std::nullopt;
}

QIcon getAppIcon(const QString& key)
{
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        return iconProvider->getIcon(key);
    }

    return QIcon();
}

std::optional<FrameInfo> readExternalMaster(const QString& path, const FrameType type)
{
    const an::consumer::EngineSession* engineSession = ImageIntegration::detail::applicationEngineSession();
    if (engineSession == nullptr)
    {
        return std::nullopt;
    }
    const ImageIntegration::BuildDetectedFrameInfoResult read = ImageIntegration::buildDetectedFrameInfoWithIssue(
        *engineSession, path, type, ImageIntegration::ImportAdmission::NoWait);
    if (!read.detected.has_value())
    {
        return std::nullopt;
    }
    return read.detected->info;
}

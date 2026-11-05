// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow's registration stage: lights read through the run's registry, their
/// chromatic aberration corrected and published, their stars detected, each frame registered onto the group's
/// reference, through a light registered near it when the reference holds too few of its stars, and recovered from its
/// solution where the method could not, and every record the registration database keeps.

#include "stdafx.hpp"
#include "LightsRegistrationStage.h"

#include "app/AppContextHelpers.h"
#include "app/GaiaCatalogSettings.h"
#include "app/PlateSolveConfiguration.h"
#include "app/PlateSolveService.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/CometRegistrationProductBridge.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/StageFramePipeline.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationOutputPlane.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/SpectrophotometricColorCalibration/SpccHandoffSubsetSpectrophotometricCatalog.h"
#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/core/RegistrationRun.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationFit.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
using an::ui::RegisteredFrame;
using an::ui::RegisteredFrameSet;
using an::ui::RegisteredStar;
using an::ui::RegistrationGroupState;
using an::ui::RegistrationMatchedSourceReplacement;
using an::ui::RegistrationMetricRecord;
using an::ui::RegistrationMetricsStateLoader;
using an::ui::RegistrationMetricStatus;
using an::ui::RegistrationMetricsWriter;
using an::ui::RegistrationStarCacheEntry;
using an::ui::processes::RegisteredCatalogMatch;
using an::ui::processes::RegistrationDemosaic;
using an::ui::processes::RegistrationModel;
using ImageIntegration::RegistrationHashUtils::chromaticAberrationToJson;
using ImageIntegration::RegistrationHashUtils::effectiveRegistrationHash;
using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
using ImageIntegration::RegistrationHashUtils::plateSolveSettingsAreDefault;
using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
using ImageIntegration::RegistrationHashUtils::registrationTransformHash;
using stage_utils::RegistrationFrameFacts;

/// How many samples along each axis the astrometric registration maps from one solution to the other.
constexpr std::uint32_t kAstrometricGridSamples = 9u;

/// The fewest of those samples both solutions must place for the registration to be fitted.
constexpr std::size_t kAstrometricMinimumSamples = 16u;

/// The share of the stage's progress a group's star detection takes when it has any.
constexpr float kDetectionPhaseEnd = 0.8f;

/// Target number of progress lines emitted per logged phase.
constexpr int kPhaseProgressLogLines = 20;

/// The astrometric catalog family a registration's matches are made against.
constexpr char kGaiaAstrometricFamily[] = "gaia_dr3";

[[nodiscard]] QString pathKey(const QString& path)
{
    return path.trimmed().isEmpty() ? QString{} : FrameId(path).identityKey();
}

[[nodiscard]] bool cancelledStatus(const an::Status& status)
{
    return status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded;
}

std::vector<const WorkflowManifest::RegistrationGroup*> groupsToProcess(const WorkflowManifest& manifest)
{
    std::vector<const WorkflowManifest::RegistrationGroup*> groups;
    if (manifest.executionMode != ExecutionMode::Incremental)
    {
        groups.reserve(manifest.registrationGroups.size());
        for (const auto& group : manifest.registrationGroups)
        {
            groups.push_back(&group);
        }
        return groups;
    }
    for (const auto& group : manifest.registrationGroups)
    {
        if (std::find(manifest.incrementalRegistrationGroups.begin(), manifest.incrementalRegistrationGroups.end(),
                      group.groupKey) != manifest.incrementalRegistrationGroups.end())
        {
            groups.push_back(&group);
        }
    }
    return groups;
}

[[nodiscard]] bool
registrationGroupHasStarsPlusCometExperimentalConsumer(const WorkflowManifest& manifest,
                                                       const WorkflowManifest::RegistrationGroup& registration_group)
{
    if (ImageIntegration::registrationRequestsStarsPlusCometExperimentalOutput(registration_group.settings))
    {
        return true;
    }
    const QString registration_key = registration_group.groupKey.toString();
    return std::any_of(manifest.integrationGroups.cbegin(), manifest.integrationGroups.cend(),
                       [&](const WorkflowManifest::IntegrationGroup& group) {
                           return group.registrationGroupKey.toString() == registration_key &&
                                  ImageIntegration::isStarsPlusCometExperimentalStackingMode(
                                      group.stacking.cometStackingMode);
                       });
}

[[nodiscard]] QString jsonHashObject(const QJsonObject& obj)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(obj).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex());
}

void insertArtifactFileStamp(QJsonObject& obj, const QString& path)
{
    const QString clean_path = QDir::cleanPath(path);
    obj.insert(QStringLiteral("path"), clean_path);
    const QFileInfo info(clean_path);
    obj.insert(QStringLiteral("exists"), info.exists());
    if (info.exists())
    {
        obj.insert(QStringLiteral("size"), static_cast<qint64>(info.size()));
        obj.insert(QStringLiteral("mtime"), info.lastModified().toMSecsSinceEpoch());
    }
}

[[nodiscard]] QString manifestAstrometryApiKeyHash(const WorkflowManifest& manifest)
{
    if (!manifest.astrometryApiKeyHash.trimmed().isEmpty())
    {
        return manifest.astrometryApiKeyHash.trimmed();
    }
    return ImageIntegration::astrometryApiKeyHash(manifest.plateSolve.astrometryApiKey);
}

[[nodiscard]] WorkflowManifest::PlateSolveSettings runtimePlateSolveSettings(const WorkflowManifest& manifest)
{
    WorkflowManifest::PlateSolveSettings settings = manifest.plateSolve;
    settings.astrometryApiKey = process_utils::selectGlobalSetting(SettingsKeys::TOOLS_ASTROMETRY_API_KEY,
                                                                   SettingsDefaults::TOOLS_ASTROMETRY_API_KEY);
    return settings;
}

/// The identity a chromatic aberration output satisfies: its family, its frame, its source file, the correction's
/// settings and, for a light calibrated as it was read, the calibration it was read with.
[[nodiscard]] QString computeChromaticAberrationArtifactHash(
    const FrameId& frame, const QString& source_path, const RegistrationSettings::ChromaticAberrationSettings& settings,
    const WorkflowManifest::LightsCalibrationParams* assignment, const bool used_in_memory_calibration)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 2);
    ImageIntegration::schema::insertArtifactFamilyIdentity(
        obj, ImageIntegration::schema::ArtifactFamily::ChromaticCorrection);
    obj.insert(QStringLiteral("frame"), frame.canonicalPath());
    obj.insert(QStringLiteral("usedInMemoryCalibration"), used_in_memory_calibration);
    obj.insert(QStringLiteral("settings"), chromaticAberrationToJson(settings));

    QJsonObject source;
    insertArtifactFileStamp(source, source_path);
    obj.insert(QStringLiteral("source"), source);

    if (used_in_memory_calibration && assignment != nullptr)
    {
        obj.insert(QStringLiteral("calibrationArtifactHash"), computeCalibratedLightArtifactHash(*assignment));
    }
    return jsonHashObject(obj);
}

/// Whether every light of a group has one channel and no filter-array mosaic, as its header states: a group the
/// chromatic aberration correction, which moves a colour light's red and blue onto its green, has nothing to correct
/// in. A light whose header cannot be read counts as colour, so the correction reads it and says why it cannot.
///
/// @param scope the stage's admitted operation.
/// @param registry the run's registry.
/// @param lights the group's lights.
/// @return whether each has one channel; or the stop that ended the probe.
[[nodiscard]] an::Result<bool> everyLightHasOneChannel(const an::consumer::OperationScope& scope,
                                                       const an::CodecRegistry* registry,
                                                       const std::vector<FrameId>& lights)
{
    for (const FrameId& light : lights)
    {
        const an::Result<RegistrationFrameFacts> facts =
            stage_utils::probeRegistrationFrame(scope, registry, light.canonicalPath());
        if (!facts.hasValue())
        {
            if (cancelledStatus(facts.status()))
            {
                return facts.status();
            }
            return false;
        }
        if (facts->channels != 1u || facts->mosaic)
        {
            return false;
        }
    }
    return !lights.empty();
}

/// Whether a registration's method and transform settings are ones the stage runs; the reason when they are not.
[[nodiscard]] bool validateTransformContract(const RegistrationSettings& settings, QString& error)
{
    const QString method =
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(settings.registrationMethod);
    if (method != QStringLiteral("star_matching") && method != QStringLiteral("phase_correlation") &&
        method != QStringLiteral("manual_points") && method != QStringLiteral("comet_tracking"))
    {
        error = QStringLiteral("Registration method '%1' is invalid.").arg(settings.registrationMethod);
        return true;
    }
    if (!ImageIntegration::RegistrationSettingsUtils::usesTransformContract(method))
    {
        return false;
    }

    const QString transform_mode = settings.transformMode.trimmed().toLower();
    if (!transform_mode.isEmpty() && transform_mode != QStringLiteral("linear") &&
        transform_mode != QStringLiteral("polynomial") && transform_mode != QStringLiteral("auto"))
    {
        error = QStringLiteral("Registration transform mode '%1' is invalid.").arg(settings.transformMode);
        return true;
    }
    if (settings.transformOrder < 0 || settings.transformOrder == 1 || settings.transformOrder > 4)
    {
        error =
            QStringLiteral("Registration transform order '%1' is invalid. Use 0 for linear-only or 2-4 for polynomial.")
                .arg(settings.transformOrder);
        return true;
    }
    if (transform_mode == QStringLiteral("polynomial") && (settings.transformOrder < 2 || settings.transformOrder > 4))
    {
        error = QStringLiteral("Polynomial registration requires transform orders 2-4.");
        return true;
    }
    if (method == QStringLiteral("manual_points"))
    {
        if (settings.referenceImage.trimmed().isEmpty())
        {
            error = QStringLiteral("Manual point registration requires an explicit reference frame.");
            return true;
        }
        if (settings.manualControlPointsByFrame.isEmpty())
        {
            error = QStringLiteral("Manual point registration requires at least one target frame with control points.");
            return true;
        }
    }
    return false;
}

/// The registration one world-coordinate solution states onto another: each of a grid of the frame's positions placed
/// on the sky through its own solution and back into the reference through the reference's, and a projective model -
/// with the polynomial the residual asks for - fitted through them. Both solutions' distortions are inside the mapping,
/// so the transform needs no separate source distortion.
struct AstrometricRegistration
{
    an::RegistrationTransform transform;
    double residualPixels = 0.0;
};

[[nodiscard]] an::Result<AstrometricRegistration>
astrometricRegistration(const an::ExecutionContext& execution, const an::WcsSolution& frame,
                        const an::WcsSolution& reference, const std::uint32_t width, const std::uint32_t height)
{
    std::vector<an::PixelCoordinate> source;
    std::vector<an::PixelCoordinate> target;
    source.reserve(kAstrometricGridSamples * kAstrometricGridSamples);
    target.reserve(kAstrometricGridSamples * kAstrometricGridSamples);
    for (std::uint32_t row = 0u; row < kAstrometricGridSamples; ++row)
    {
        for (std::uint32_t column = 0u; column < kAstrometricGridSamples; ++column)
        {
            const an::PixelCoordinate pixel{
                .x = static_cast<double>(width) * (static_cast<double>(column) + 0.5) / kAstrometricGridSamples,
                .y = static_cast<double>(height) * (static_cast<double>(row) + 0.5) / kAstrometricGridSamples};
            const an::Result<an::SkyCoordinate> sky = frame.pixelToWorld(pixel);
            if (!sky.hasValue())
            {
                continue;
            }
            const an::Result<an::PixelCoordinate> placed = reference.worldToPixel(*sky);
            if (!placed.hasValue())
            {
                continue;
            }
            source.push_back(pixel);
            target.push_back(*placed);
        }
    }
    if (source.size() < kAstrometricMinimumSamples)
    {
        return an::Status::failure(an::ErrorCode::InvalidData,
                                   "the two solutions place too few common positions to register the frame");
    }
    an::RegistrationFitRequest request;
    request.linearKind = an::RegistrationTransformKind::Projective;
    request.polynomialPolicy = an::RegistrationPolynomialPolicy::AutomaticOrder;
    const double half_width = static_cast<double>((std::max)(width, 1u)) * 0.5;
    const double half_height = static_cast<double>((std::max)(height, 1u)) * 0.5;
    request.normalizationOrigin = an::PixelCoordinate{.x = half_width, .y = half_height};
    request.normalizationScale =
        an::PixelCoordinate{.x = (std::max)(half_width, 1.0), .y = (std::max)(half_height, 1.0)};
    const an::Result<an::RegistrationFitResult> fitted =
        an::fitRegistrationTransform(execution, source, target, request);
    if (!fitted.hasValue())
    {
        return fitted.status();
    }
    return AstrometricRegistration{.transform = fitted->transform, .residualPixels = fitted->rootMeanSquareResidual};
}

/// The Gaia source identifier an entry's identifier names: its digits, with or without the release's prefix.
[[nodiscard]] std::optional<std::uint64_t> gaiaSourceIdentifier(const std::string& identifier)
{
    QString text = QString::fromStdString(identifier).trimmed();
    static const QString kPrefix = QStringLiteral("Gaia DR3 ");
    if (text.startsWith(kPrefix))
    {
        text = text.mid(kPrefix.size());
    }
    if (text.isEmpty() || !std::all_of(text.cbegin(), text.cend(), [](const QChar ch) { return ch.isDigit(); }))
    {
        return std::nullopt;
    }
    bool ok = false;
    const qulonglong value = text.toULongLong(&ok);
    if (!ok || value == 0u)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

/// The matched sources a spectrophotometric calibration of the stack reuses, one per Gaia source a frame's stars
/// matched, stating the solution, epoch and proper-motion model they were matched under.
[[nodiscard]] std::vector<an::MatchedSourceReference>
matchedSourceReferences(const std::vector<RegisteredCatalogMatch>& matches, const an::WcsSolution& solution,
                        const std::optional<double>& epochYear, const QString& catalogDirectory,
                        const double residualPixels)
{
    std::vector<an::MatchedSourceReference> references;
    std::unordered_set<std::uint64_t> seen;
    const std::string solution_identity = an::ui::processes::spccSolutionIdentity(solution).toStdString();
    const std::string epoch = an::ui::processes::spccObservationEpochText(epochYear).toStdString();
    const std::string manifest_identity = QFileInfo(catalogDirectory).fileName().toStdString();
    for (const RegisteredCatalogMatch& match : matches)
    {
        const std::optional<std::uint64_t> source = gaiaSourceIdentifier(match.identifier);
        if (!source.has_value() || !seen.insert(*source).second)
        {
            continue;
        }
        an::MatchedSourceReference reference;
        reference.sourceIdentifier = *source;
        reference.astrometricCatalogFamilyId = kGaiaAstrometricFamily;
        reference.astrometricCatalogManifestId = manifest_identity;
        reference.spectrumCatalogFamilyId = an::kXpCatalogFamilyId;
        reference.wcsSolutionId = solution_identity;
        reference.observationEpoch = epoch;
        reference.properMotionModelVersion = an::ui::processes::kSpccProperMotionModelVersion;
        reference.separationArcseconds = match.separationArcseconds;
        if (residualPixels > 0.0 && std::isfinite(residualPixels))
        {
            reference.residualPixels = residualPixels;
        }
        references.push_back(std::move(reference));
    }
    return references;
}

void populateStarCacheInputStamp(RegistrationStarCacheEntry& entry, const FileRevisionStamp& revision)
{
    entry.inputMtime = revision.modifiedAtMs / 1000;
    entry.inputSizeBytes = revision.sizeBytes;
    entry.inputModifiedAtMs = revision.modifiedAtMs;
    entry.inputIdentityToken = revision.identityToken;
}

[[nodiscard]] bool starCacheInputMatches(const RegistrationStarCacheEntry& entry, const QString& input_path)
{
    if (entry.inputSizeBytes < 0 || entry.inputModifiedAtMs < 0 || entry.inputIdentityToken.isEmpty())
    {
        return false;
    }
    const auto current_revision = ArtifactTracker::captureFileRevision(input_path);
    return current_revision.has_value() && current_revision->sizeBytes == entry.inputSizeBytes &&
           current_revision->modifiedAtMs == entry.inputModifiedAtMs &&
           current_revision->identityToken == entry.inputIdentityToken;
}

struct RegistrationInput
{
    FrameId originalId;
    FrameId registrationId;
};

using RegistrationInputRevisions = std::unordered_map<QString, FileRevisionStamp>;

[[nodiscard]] std::optional<FileRevisionStamp> captureInputRevision(const RegistrationInput& input)
{
    return ArtifactTracker::captureFileRevision(input.registrationId.canonicalPath());
}

[[nodiscard]] bool inputUnchangedSince(const RegistrationInput& input, const FileRevisionStamp& captured)
{
    const auto current = captureInputRevision(input);
    return current.has_value() && captured.sameRevision(*current);
}

/// The record a frame's star detection leaves for a later run.
[[nodiscard]] RegistrationStarCacheEntry starCacheEntryOf(const RegistrationInput& input, const QString& group_key,
                                                          const QString& detection_hash, const RegisteredFrame& frame,
                                                          const FileRevisionStamp& revision)
{
    RegistrationStarCacheEntry entry;
    entry.frameId = input.originalId;
    entry.groupKey = group_key;
    entry.detectionHash = detection_hash;
    entry.inputPath = input.registrationId.canonicalPath();
    populateStarCacheInputStamp(entry, revision);
    entry.starsCount = frame.metrics.stars;
    entry.fwhm = frame.metrics.fwhm;
    entry.snr = frame.metrics.snr;
    entry.roundness = 1.0 - std::clamp(frame.metrics.eccentricity, 0.0, 1.0);
    entry.noise = frame.noise;
    entry.starsBlob = an::ui::serializeRegistrationStarCacheBlob(frame.stars);
    return entry;
}

/// A frame's record rebuilt from the star detection an earlier run left: its stars, figures and noise.
[[nodiscard]] RegisteredFrame registeredFrameFromCache(const RegistrationStarCacheEntry& entry, QString& error)
{
    RegisteredFrame frame;
    frame.sourcePath = entry.inputPath;
    if (std::optional<std::vector<RegisteredStar>> stars =
            an::ui::deserializeRegistrationStarCacheBlob(entry.starsBlob, error);
        stars.has_value())
    {
        frame.stars = std::move(*stars);
    }
    frame.metrics.stars = entry.starsCount;
    frame.metrics.fwhm = entry.fwhm;
    frame.metrics.snr = entry.snr;
    frame.metrics.eccentricity = an::ui::registrationEccentricityFromRoundness(entry.roundness);
    frame.noise = entry.noise;
    return frame;
}

[[nodiscard]] QString formatPhaseEta(const qint64 elapsed_ms, const int processed, const int total)
{
    if (elapsed_ms <= 0 || processed <= 0)
    {
        return {};
    }
    const QString elapsed = WorkflowLogger::formatDuration(elapsed_ms);
    if (processed >= total)
    {
        return QStringLiteral("%1 elapsed").arg(elapsed);
    }
    const double per_item_ms = static_cast<double>(elapsed_ms) / static_cast<double>(processed);
    const auto remaining_ms = static_cast<qint64>(per_item_ms * static_cast<double>(total - processed));
    return QStringLiteral("%1 elapsed, ~%2 remaining").arg(elapsed, WorkflowLogger::formatDuration(remaining_ms));
}

/// What a completed star detection pass found, reported when the phase closes.
struct StarDetectionSummary
{
    int frames = 0;
    int framesWithoutStars = 0;
    int framesBelowMinimum = 0;
    qint64 totalStars = 0;
    int minStars = 0;
    int maxStars = 0;
    QString minStarsFrame;
    QString maxStarsFrame;
    double medianFwhm = 0.0;
    double minFwhm = 0.0;
    double maxFwhm = 0.0;
    double medianEccentricity = 0.0;
};

[[nodiscard]] double medianOfSorted(std::vector<double>& values)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return values.size() % 2 == 1 ? values[middle] : 0.5 * (values[middle - 1] + values[middle]);
}

/// Announces a group's long-running sub-steps - star detection, transform computation - as numbered phases with their
/// own start line, progress and closing summary.
class RegistrationPhaseLog
{
public:
    RegistrationPhaseLog(WorkflowLogger& logger, const int total_phases) : m_logger(logger), m_totalPhases(total_phases)
    {
    }

    void begin(const QString& name, const QString& detail = QString())
    {
        m_name = name;
        ++m_index;
        m_timer.start();
        m_logger.phaseStarted(m_index, m_totalPhases, name, detail);
    }

    void detail(const QString& text)
    {
        m_logger.phaseDetail(text);
    }

    [[nodiscard]] qint64 elapsedMs() const
    {
        return m_timer.isValid() ? m_timer.elapsed() : 0;
    }

    void end(const QString& summary = QString())
    {
        m_logger.phaseCompleted(m_index, m_totalPhases, m_name, elapsedMs(), summary);
    }

private:
    WorkflowLogger& m_logger;
    int m_totalPhases = 1;
    int m_index = 0;
    QString m_name;
    QElapsedTimer m_timer;
};

void logStarDetectionSummary(RegistrationPhaseLog& phase_log, const StarDetectionSummary& summary,
                             const size_t minimum_stars)
{
    const QLocale locale = QLocale::c();
    if (summary.frames == 0)
    {
        phase_log.detail(QStringLiteral("Stars: total 0 (no frame produced a detection result)"));
        return;
    }
    const qint64 average = summary.totalStars / summary.frames;
    phase_log.detail(QStringLiteral("Stars: total %1 | avg %2 | min %3 (%4) | max %5 (%6)")
                         .arg(locale.toString(summary.totalStars), locale.toString(average),
                              locale.toString(summary.minStars), summary.minStarsFrame,
                              locale.toString(summary.maxStars), summary.maxStarsFrame));
    phase_log.detail(QStringLiteral("FWHM: median %1 px (%2-%3) | eccentricity median %4")
                         .arg(summary.medianFwhm, 0, 'f', 2)
                         .arg(summary.minFwhm, 0, 'f', 2)
                         .arg(summary.maxFwhm, 0, 'f', 2)
                         .arg(summary.medianEccentricity, 0, 'f', 2));
    phase_log.detail(QStringLiteral("Frames with no stars: %1 | below minimum (%2 stars): %3")
                         .arg(summary.framesWithoutStars)
                         .arg(static_cast<qulonglong>(minimum_stars))
                         .arg(summary.framesBelowMinimum));
}

/// Mirrors a phase's frame count into the workflow log: the first and last frames, and one line per twentieth of the
/// phase no sooner than two seconds after the previous one.
class PhaseProgressLog
{
public:
    PhaseProgressLog(WorkflowLogger& logger, QString verb, const int total)
        : m_logger(logger), m_verb(std::move(verb)), m_total((std::max)(0, total)),
          m_step((std::max)(1, m_total / kPhaseProgressLogLines))
    {
        m_timer.start();
    }

    void frameDone(const int processed)
    {
        if (m_total <= 0 || processed <= 0 || processed == m_lastLogged)
        {
            return;
        }
        const qint64 elapsed_ms = m_timer.elapsed();
        const bool is_first = m_lastLogged < 0;
        const bool is_last = processed >= m_total;
        const bool step_reached = processed - (std::max)(0, m_lastLogged) >= m_step;
        const bool interval_elapsed = elapsed_ms - m_lastLoggedMs >= 2000;
        if (!is_first && !is_last && !(step_reached && interval_elapsed))
        {
            return;
        }
        m_lastLogged = processed;
        m_lastLoggedMs = elapsed_ms;
        m_logger.phaseProgress(m_verb, processed, m_total, formatPhaseEta(elapsed_ms, processed, m_total));
    }

private:
    WorkflowLogger& m_logger;
    QString m_verb;
    int m_total = 0;
    int m_step = 1;
    int m_lastLogged = -1;
    qint64 m_lastLoggedMs = 0;
    QElapsedTimer m_timer;
};

/// Where the stage's time went, reported once at its end.
struct RegistrationStageProfile
{
    std::uint64_t framesDetected = 0;
    std::uint64_t starsDetected = 0;
    std::uint64_t readNanoseconds = 0;
    std::uint64_t detectNanoseconds = 0;
    std::uint64_t detectionLoopNanoseconds = 0;
    std::uint64_t matchingNanoseconds = 0;
};

[[nodiscard]] std::uint64_t elapsedNanoseconds(const std::chrono::steady_clock::time_point started)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
}

[[nodiscard]] QString formatRegistrationProfile(const RegistrationStageProfile& profile)
{
    if (profile.framesDetected == 0 || profile.detectionLoopNanoseconds == 0)
    {
        return {};
    }
    const auto share = [&](const std::uint64_t nanoseconds) {
        return 100.0 * static_cast<double>(nanoseconds) / static_cast<double>(profile.detectionLoopNanoseconds);
    };
    const std::uint64_t accounted = profile.readNanoseconds + profile.detectNanoseconds;
    const double per_frame_ms =
        static_cast<double>(profile.detectionLoopNanoseconds) / 1.0e6 / static_cast<double>(profile.framesDetected);
    return QStringLiteral("detection %1 ms/frame | read %2%, detect %3%, other %4% | %5 stars/frame, matching %6 s, "
                          "%7 hardware threads")
        .arg(per_frame_ms, 0, 'f', 0)
        .arg(share(profile.readNanoseconds), 0, 'f', 1)
        .arg(share(profile.detectNanoseconds), 0, 'f', 1)
        .arg(profile.detectionLoopNanoseconds > accounted ? share(profile.detectionLoopNanoseconds - accounted) : 0.0,
             0, 'f', 1)
        .arg(static_cast<double>(profile.starsDetected) / static_cast<double>(profile.framesDetected), 0, 'f', 0)
        .arg(static_cast<double>(profile.matchingNanoseconds) / 1.0e9, 0, 'f', 1)
        .arg(QThread::idealThreadCount());
}

std::vector<RegistrationInput> buildRegistrationInputs(const WorkflowManifest::RegistrationGroup& group,
                                                       const QString& output_dir, const bool prefer_calibrated,
                                                       QString& error)
{
    std::vector<RegistrationInput> inputs;
    inputs.reserve(group.lightFiles.size());
    for (const auto& frame : group.lightFiles)
    {
        if (!frame.isValid())
        {
            error = QStringLiteral("Invalid light frame path.");
            return {};
        }
        RegistrationInput input;
        input.originalId = frame;
        input.registrationId = frame;
        if (group.settings.chromaticAberration.enabled)
        {
            const QString ca_output = chromaticAberrationLightOutputPathFor(output_dir, frame);
            if (QFileInfo::exists(ca_output))
            {
                input.registrationId = FrameId(ca_output);
                inputs.push_back(std::move(input));
                continue;
            }
        }
        if (prefer_calibrated)
        {
            const QString calibrated = calibratedLightOutputPathFor(output_dir, frame);
            if (QFileInfo::exists(calibrated))
            {
                input.registrationId = FrameId(calibrated);
            }
        }
        inputs.push_back(std::move(input));
    }
    return inputs;
}

[[nodiscard]] QString registrationFailureReason(const RegisteredFrame* frame, const bool method_uses_star_detection,
                                                const std::uint32_t minimum_stars, const QString& engine_reason)
{
    if (frame == nullptr)
    {
        return QStringLiteral("No registration transform was produced for this file.");
    }
    if (method_uses_star_detection)
    {
        const int star_count = static_cast<int>(frame->stars.size());
        if (star_count <= 0)
        {
            return QStringLiteral(
                "No stars were detected. Check the frame quality or lower the star detection thresholds.");
        }
        if (static_cast<std::uint32_t>(star_count) < minimum_stars)
        {
            return QStringLiteral("Only %1 star(s) were detected; this run requires at least %2.")
                .arg(star_count)
                .arg(minimum_stars);
        }
        return QStringLiteral("Too few matching star pairs were found against the reference frame. The file may be "
                              "from a different field, or star detection/registration settings may need tuning.");
    }
    return engine_reason.isEmpty() ? QStringLiteral("Registration transform fitting failed with the selected method.")
                                   : engine_reason;
}

struct RegistrationFailureDiagnostic
{
    QString framePath;
    QString reason;
};

[[nodiscard]] std::optional<size_t> manifestLightIndex(const WorkflowManifest::RegistrationGroup& group,
                                                       const FrameId& frame_id)
{
    const auto it = std::find(group.lightFiles.cbegin(), group.lightFiles.cend(), frame_id);
    if (it == group.lightFiles.cend())
    {
        return std::nullopt;
    }
    return static_cast<size_t>(std::distance(group.lightFiles.cbegin(), it));
}

[[nodiscard]] QString cometManifestDateObs(const WorkflowManifest::RegistrationGroup& group, const FrameId& frame_id)
{
    const std::optional<size_t> index = manifestLightIndex(group, frame_id);
    if (!index.has_value() || *index >= group.lightCaptureTimestampsUtc.size())
    {
        return {};
    }
    return group.lightCaptureTimestampsUtc[*index].trimmed();
}

[[nodiscard]] double cometManifestExposureSeconds(const WorkflowManifest::RegistrationGroup& group,
                                                  const FrameId& frame_id)
{
    const std::optional<size_t> index = manifestLightIndex(group, frame_id);
    if (!index.has_value() || *index >= group.lightExposureSeconds.size())
    {
        return 0.0;
    }
    const double exposure_seconds = group.lightExposureSeconds[*index];
    return std::isfinite(exposure_seconds) ? (std::max)(0.0, exposure_seconds) : 0.0;
}

} // namespace

LightsRegistrationStage::LightsRegistrationStage(std::shared_ptr<an::ui::PlateSolveService> plateSolveService)
    : m_plateSolveService(std::move(plateSolveService))
{
}

QString LightsRegistrationStage::name() const
{
    return QStringLiteral("LightsRegistration");
}

WorkflowStage LightsRegistrationStage::stageId() const
{
    return WorkflowStage::LightsRegistration;
}

bool LightsRegistrationStage::shouldSkip(const WorkflowManifest& manifest) const
{
    return groupsToProcess(manifest).empty();
}

QString LightsRegistrationStage::skipReason(const WorkflowManifest& manifest) const
{
    if (manifest.registrationGroups.empty())
    {
        return QStringLiteral("No registration groups.");
    }
    if (manifest.executionMode == ExecutionMode::Incremental && groupsToProcess(manifest).empty())
    {
        return QStringLiteral("No dirty registration groups in incremental plan.");
    }
    return QStringLiteral("No work required.");
}

StageResult LightsRegistrationStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                             ProgressReporter& reporter)
{
    const auto groups_to_process = groupsToProcess(manifest);
    if (groups_to_process.empty())
    {
        return StageResult::skipped();
    }
    const ImageIntegration::WorkflowStageServices* services = stageServices();
    if (services == nullptr)
    {
        return StageResult::failure(IntegrationError::internalError(QStringLiteral(
            "The lights registration stage runs only inside a workflow run, which lends it the session")));
    }
    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return StageResult::failure(
            IntegrationError(IntegrationErrorCode::InvalidOutputPath, QStringLiteral("Output directory is required")));
    }
    const QDir output_root(manifest.outputDirectory);
    if (!output_root.exists() && !output_root.mkpath(QStringLiteral(".")))
    {
        return StageResult::failure(IntegrationError::directoryNotWritable(manifest.outputDirectory));
    }

    const QString metrics_path = output_root.filePath(QStringLiteral("registration_metrics.db"));
    RegistrationMetricsWriter metrics_writer(metrics_path, QStringLiteral("LightsRegistrationWriter"),
                                             executionCounterAccumulator());
    QString metrics_error;
    if (!metrics_writer.initialize(&metrics_error))
    {
        return StageResult::failure(IntegrationError(
            IntegrationErrorCode::FileNotWritable,
            metrics_error.isEmpty() ? QStringLiteral("Failed to initialize registration metrics database")
                                    : metrics_error,
            metrics_path));
    }
    RegistrationMetricsStateLoader metrics_loader(metrics_path, QStringLiteral("LightsRegistrationStateLoader"),
                                                  executionCounterAccumulator());
    if (!metrics_loader.open(&metrics_error))
    {
        return StageResult::failure(IntegrationError(
            IntegrationErrorCode::FileNotReadable,
            metrics_error.isEmpty() ? QStringLiteral("Failed to open registration metrics database") : metrics_error,
            metrics_path));
    }

    const an::CodecRegistry* registry = services->codecRegistry().get();
    ArtifactTracker calibration_tracker(manifest.outputDirectory, executionCounterAccumulator(),
                                        sessionArtifactStampReader(services->session()));
    ArtifactTracker chromatic_aberration_tracker(manifest.outputDirectory, executionCounterAccumulator(),
                                                 sessionArtifactStampReader(services->session()));
    core::CalibrationMasterSet masters(services->codecRegistry());
    std::vector<AlignmentGroupResult> ca_group_results;

    const auto* preparation_snapshot = executionPreparationSnapshot();
    std::unordered_map<QString, const WorkflowManifest::LightsCalibrationParams*> fallback_calibration_params_by_light;
    if (preparation_snapshot == nullptr)
    {
        fallback_calibration_params_by_light.reserve(manifest.lightsCalibration.size());
        for (const auto& params : manifest.lightsCalibration)
        {
            fallback_calibration_params_by_light[pathKey(params.lightPath.canonicalPath())] = &params;
        }
    }
    const auto calibration_assignment_for =
        [&](const FrameId& frame) -> const WorkflowManifest::LightsCalibrationParams* {
        if (preparation_snapshot != nullptr)
        {
            return preparation_snapshot->calibrationAssignment(frame);
        }
        const auto it = fallback_calibration_params_by_light.find(pathKey(frame.canonicalPath()));
        return it != fallback_calibration_params_by_light.end() ? it->second : nullptr;
    };

    const int total_groups = static_cast<int>(groups_to_process.size());
    int group_index = 0;
    IntegrationErrorReport error_report;
    RegistrationStageProfile stage_profile;

    reporter.logger().stageStarted(name(), total_groups);
    stage_utils::reportStageProgress(reporter, stageId(), 1, (std::max)(1, total_groups), 0.0f,
                                     QStringLiteral("Preparing registration"), QString());
    QElapsedTimer stage_timer;
    stage_timer.start();
    const WorkflowManifest::PlateSolveSettings runtime_plate_solve = runtimePlateSolveSettings(manifest);
    const QString persisted_api_key_hash = manifestAstrometryApiKeyHash(manifest);
    const QString astrometry_api_key_hash =
        persisted_api_key_hash.isEmpty() ? ImageIntegration::astrometryApiKeyHash(runtime_plate_solve.astrometryApiKey)
                                         : persisted_api_key_hash;
    const QString catalog_directory = an::ui::resolveGaiaCatalogDirectory(runtime_plate_solve).trimmed();
    const bool catalog_installed = !catalog_directory.isEmpty() && QFileInfo(catalog_directory).isDir();
    // Without a solver nothing is sent to one, whatever a group asks; the solve service chooses among the same ones.
    const bool plate_solver_configured = an::ui::hasAnyConfiguredPlateSolveSolver(runtime_plate_solve);

    const auto cancelled = [&token]() { return token.isCancellationRequested(); };

    for (const auto* group_ptr : groups_to_process)
    {
        const auto& group = *group_ptr;
        ++group_index;
        if (cancelled())
        {
            return StageResult::cancelled();
        }
        if (group.lightFiles.empty())
        {
            continue;
        }

        std::unordered_map<QString, const WorkflowManifest::LightsCalibrationParams*> calibration_params_by_light;
        calibration_params_by_light.reserve(group.lightFiles.size());
        for (const auto& frame : group.lightFiles)
        {
            if (const auto* params = calibration_assignment_for(frame); params != nullptr)
            {
                calibration_params_by_light[pathKey(frame.canonicalPath())] = params;
            }
        }
        std::vector<const WorkflowManifest::LightsCalibrationParams*> group_calibration_assignments;
        group_calibration_assignments.reserve(calibration_params_by_light.size());
        for (const auto& [frame_key, params] : calibration_params_by_light)
        {
            (void)frame_key;
            group_calibration_assignments.push_back(params);
        }

        const QString group_label = group.groupKey.toCompactString();
        reporter.logger().groupStarted(group_index, total_groups, group_label);
        QElapsedTimer group_timer;
        group_timer.start();
        const auto finish_group = [&]() {
            reporter.logger().groupCompleted(group_index, total_groups, group_label, group_timer.elapsed());
        };

        QString error;
        const QString group_key = group.groupKey.toString();
        const int settings_version = group.settings.version;
        const QString configured_registration_method =
            ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(group.settings.registrationMethod);
        const bool stars_plus_comet_star_base_required =
            configured_registration_method == QStringLiteral("comet_tracking") &&
            registrationGroupHasStarsPlusCometExperimentalConsumer(manifest, group);
        const QString registration_method =
            stars_plus_comet_star_base_required ? QStringLiteral("star_matching") : configured_registration_method;
        RegistrationSettings effective_registration_settings = group.settings;
        if (stars_plus_comet_star_base_required)
        {
            effective_registration_settings.registrationMethod = QStringLiteral("star_matching");
        }
        const bool method_uses_star_detection =
            ImageIntegration::RegistrationSettingsUtils::usesStarDetection(registration_method);
        const bool auto_reference_runtime = group.settings.autoReferenceDuringRegistration &&
                                            method_uses_star_detection && !stars_plus_comet_star_base_required;
        const bool allow_astrometric_fallback = registration_method != QStringLiteral("comet_tracking");
        const QString detection_hash = registrationDetectionHash(group.settings);
        const QString plain_registration_hash = registrationTransformHash(group.settings);
        const QString registration_hash =
            effectiveRegistrationHash(group.settings, runtime_plate_solve, astrometry_api_key_hash);
        const QString ref_frame_id = (!auto_reference_runtime && group.referenceFrame.has_value())
                                         ? group.referenceFrame.value().canonicalPath()
                                         : QString();
        const QString light_files_hash = hashFrameIdList(group.lightFiles);
        const ImageIntegration::RegistrationCanvasMode canvas_mode =
            stage_utils::parseAlignmentMode(group.settings.alignmentMode);
        // The rule the dialog's Auto-solve shows: on and fixed under Mosaic, off without a solver.
        const bool allow_external_plate_solve =
            ImageIntegration::RegistrationSettingsUtils::allowsPlateSolveDuringRegistration(
                group.settings.autoSolveUnsolvedFrames, canvas_mode == ImageIntegration::RegistrationCanvasMode::Mosaic,
                plate_solver_configured);
        const QString calibration_hash =
            computeLightsCalibrationHashForAssignments(group_calibration_assignments, nullptr);
        const RegistrationDemosaic demosaic = an::ui::processes::registrationDemosaicOf(group.settings);
        const an::StarDetectionRequest detection_request = an::ui::processes::registrationStarDetection(group.settings);
        // Stars are searched for a star match, for a comet group's quality figures and for the chromatic correction.
        if (method_uses_star_detection || registration_method == QStringLiteral("comet_tracking") ||
            group.settings.chromaticAberration.enabled)
        {
            if (const QString adjusted = an::ui::processes::registrationStarDetectionAdjustment(group.settings);
                !adjusted.isEmpty())
            {
                reporter.logger().warning(adjusted);
            }
        }

        reporter.logger().operationDetail(QStringLiteral("Settings version"), QString::number(settings_version));
        reporter.logger().operationDetail(QStringLiteral("Detection hash"), detection_hash);
        reporter.logger().operationDetail(QStringLiteral("Registration hash"), registration_hash);
        reporter.logger().operationDetail(
            QStringLiteral("Plate solve"),
            QStringLiteral("solver=%1 groupAuto=%2 effective=%3")
                .arg(runtime_plate_solve.defaultSolver,
                     group.settings.autoSolveUnsolvedFrames ? QStringLiteral("on") : QStringLiteral("off"),
                     allow_external_plate_solve ? QStringLiteral("on") : QStringLiteral("off")));
        reporter.logger().operationDetail(QStringLiteral("Calibration hash"), calibration_hash);

        QString prev_state_error;
        const auto prev_state = metrics_loader.loadLatestGroupState(group_key, &prev_state_error);
        if (!prev_state_error.isEmpty())
        {
            return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotReadable,
                                                         QStringLiteral("Failed to read previous registration state"),
                                                         metrics_path, prev_state_error));
        }

        QString output_wcs_json = prev_state.has_value() ? prev_state->outputWcsJson : QString();
        const bool allow_plain_registration_hash =
            plateSolveSettingsAreDefault(runtime_plate_solve, astrometry_api_key_hash);
        const bool registration_hash_matches_previous =
            prev_state.has_value() &&
            (prev_state->registrationHash == registration_hash ||
             (allow_plain_registration_hash && prev_state->registrationHash == plain_registration_hash));
        const bool prev_state_matches_settings =
            prev_state.has_value() && prev_state->detectionHash == detection_hash &&
            registration_hash_matches_previous &&
            (auto_reference_runtime || prev_state->referenceFrameId == ref_frame_id) &&
            prev_state->calibrationHash == calibration_hash;

        bool force_star_detection_all = false;
        bool force_registration_all = false;
        bool calibration_changed = false;
        bool frame_list_changed = false;
        QStringList reasons;
        if (manifest.reprocessExisting)
        {
            force_star_detection_all = true;
            force_registration_all = true;
            reasons << QStringLiteral("Reprocess existing enabled");
        }
        else if (!prev_state.has_value())
        {
            force_star_detection_all = true;
            force_registration_all = true;
            reasons << QStringLiteral("No previous registration state");
        }
        else
        {
            if (prev_state->detectionHash != detection_hash)
            {
                force_star_detection_all = true;
                force_registration_all = true;
                reasons << QStringLiteral("Star detection settings changed");
            }
            if (!registration_hash_matches_previous)
            {
                force_registration_all = true;
                reasons << QStringLiteral("Registration settings changed");
            }
            if (!auto_reference_runtime && prev_state->referenceFrameId != ref_frame_id)
            {
                force_registration_all = true;
                reasons << QStringLiteral("Reference frame changed");
            }
            if (prev_state->lightFilesHash != light_files_hash)
            {
                frame_list_changed = true;
                if (auto_reference_runtime)
                {
                    force_registration_all = true;
                    reasons << QStringLiteral("Group frame list changed; auto-reference may change");
                }
                else
                {
                    reasons << QStringLiteral("Group frame list changed; checking per-frame cache reuse");
                }
            }
            if (prev_state->calibrationHash != calibration_hash)
            {
                calibration_changed = true;
                reasons << QStringLiteral("Calibration assignments changed; checking affected frames");
            }
        }

        // A settings version that moved without any registration setting changing carries the previous metrics
        // forward rather than detecting and registering every frame again.
        if (configured_registration_method != QStringLiteral("comet_tracking") && !manifest.reprocessExisting &&
            prev_state_matches_settings && prev_state->settingsVersion != settings_version)
        {
            if (metrics_writer.copyMetricsToSettingsVersion(group_key, prev_state->settingsVersion, settings_version))
            {
                reporter.logger().operationDetail(QStringLiteral("Metrics migration"),
                                                  QStringLiteral("Copied metrics from settings_version %1 to %2")
                                                      .arg(prev_state->settingsVersion)
                                                      .arg(settings_version));
                RegistrationGroupState copied_group_state = *prev_state;
                copied_group_state.settingsVersion = settings_version;
                copied_group_state.detectionHash = detection_hash;
                copied_group_state.registrationHash = registration_hash;
                copied_group_state.lightFilesHash = light_files_hash;
                copied_group_state.calibrationHash = calibration_hash;
                if (!metrics_writer.upsertGroupState(copied_group_state))
                {
                    reporter.logger().warning(
                        QStringLiteral("Failed to persist settings-version migration state for group '%1'")
                            .arg(group_key));
                }
            }
        }

        if (!reasons.isEmpty())
        {
            reporter.logger().operationDetail(QStringLiteral("Change detection"), reasons.join(QStringLiteral("; ")));
        }

        // Each light's calibration is prepared once for the run and shared by every light naming the same masters.
        std::unordered_map<QString, std::shared_ptr<const core::LightCalibration>> calibration_by_light;
        const auto calibration_for = [&](const an::consumer::OperationScope& scope,
                                         const FrameId& frame) -> an::Result<const core::LightCalibration*> {
            const QString key = pathKey(frame.canonicalPath());
            const auto params_it = calibration_params_by_light.find(key);
            if (params_it == calibration_params_by_light.end())
            {
                return static_cast<const core::LightCalibration*>(nullptr);
            }
            if (const auto known = calibration_by_light.find(key); known != calibration_by_light.end())
            {
                return known->second.get();
            }
            an::Result<std::shared_ptr<const core::LightCalibration>> prepared =
                masters.prepare(scope, *params_it->second);
            if (!prepared.hasValue())
            {
                return prepared.status();
            }
            const core::LightCalibration* calibration = prepared->get();
            calibration_by_light.emplace(key, std::move(*prepared));
            return calibration;
        };

        const bool group_has_calibration_assignments = !calibration_params_by_light.empty();
        bool use_in_memory_calibration = manifest.calibrateLightsInMemory && group_has_calibration_assignments;

        // The calibrated lights this group registers from were written by this run's calibration stage when
        // reprocessing was requested, so probing them again cannot change the answer.
        if (!use_in_memory_calibration && !manifest.reprocessExisting && group_has_calibration_assignments)
        {
            const bool any_stale =
                std::any_of(group.lightFiles.cbegin(), group.lightFiles.cend(), [&](const FrameId& frame) {
                    const auto cal_it = calibration_params_by_light.find(pathKey(frame.canonicalPath()));
                    // A light with nothing to calibrate it has no calibrated copy, by design.
                    if (cal_it == calibration_params_by_light.end() || !cal_it->second->calibrates())
                    {
                        return false;
                    }
                    const QString calibrated_output =
                        calibratedLightOutputPathFor(manifest.outputDirectory, cal_it->second->lightPath);
                    return !QFileInfo::exists(calibrated_output) ||
                           !calibration_tracker.isArtifactValid(calibrated_output,
                                                                computeCalibratedLightArtifactHash(*cal_it->second));
                });
            if (any_stale)
            {
                use_in_memory_calibration = true;
                reporter.logger().warning(QStringLiteral(
                    "Calibrated files are missing or outdated; using in-memory calibration for registration."));
            }
        }

        const bool chromatic_aberration_used_in_memory_calibration = use_in_memory_calibration;
        // A run that calibrates in memory writes no calibrated copy and reads none: a copy on disk is one an earlier
        // run left, which the run may not have been able to remove, so a light without calibration is read as it is.
        const bool reads_calibrated_copies = !manifest.calibrateLightsInMemory;
        // A group whose every light has one channel has nothing to correct: its lights are registered as they are and
        // no copy of one is written. Every later reader takes a copy an earlier run left over its light, though, and
        // the run's execution plan names the copies it found, so a group that has one keeps them all current.
        bool chromatic_aberration_runs = group.settings.chromaticAberration.enabled;
        bool chromatic_aberration_one_channel = false;
        if (chromatic_aberration_runs)
        {
            an::Result<an::consumer::OperationScope> probe_scope = services->admitStageOperation();
            if (!probe_scope.hasValue() && cancelledStatus(probe_scope.status()))
            {
                return StageResult::cancelled();
            }
            if (probe_scope.hasValue())
            {
                const an::Result<bool> one_channel = everyLightHasOneChannel(*probe_scope, registry, group.lightFiles);
                if (!one_channel.hasValue())
                {
                    return StageResult::cancelled();
                }
                chromatic_aberration_one_channel = *one_channel;
            }
        }
        if (chromatic_aberration_one_channel)
        {
            reporter.logger().warning(QStringLiteral(
                "Chromatic aberration correction moves a colour light's red and blue channels onto its green one, and "
                "every light of this group has one channel: its lights are registered as they are."));
            chromatic_aberration_runs =
                std::any_of(group.lightFiles.cbegin(), group.lightFiles.cend(), [&](const FrameId& frame) {
                    return QFileInfo::exists(chromaticAberrationLightOutputPathFor(manifest.outputDirectory, frame));
                });
        }
        if (chromatic_aberration_runs)
        {
            if (!output_root.mkpath(QStringLiteral("ca_corrected_lights")))
            {
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::DirectoryNotWritable,
                                     QStringLiteral("Failed to prepare chromatic aberration output directory"),
                                     output_root.filePath(QStringLiteral("ca_corrected_lights"))));
            }
            int ca_corrected_frames = 0;
            int ca_passthrough_frames = 0;
            int ca_reused_frames = 0;
            int ca_failed_frames = 0;
            reporter.logger().operationDetail(QStringLiteral("Chromatic aberration"),
                                              QStringLiteral("Preparing %1 frame(s)").arg(group.lightFiles.size()));

            const int ca_total = static_cast<int>(group.lightFiles.size());
            for (int ca_index = 0; ca_index < ca_total; ++ca_index)
            {
                if (cancelled())
                {
                    return StageResult::cancelled();
                }
                const FrameId& frame = group.lightFiles[static_cast<size_t>(ca_index)];
                const QString original_path = frame.canonicalPath();
                const auto cal_it = calibration_params_by_light.find(pathKey(original_path));
                const WorkflowManifest::LightsCalibrationParams* assignment =
                    cal_it != calibration_params_by_light.end() ? cal_it->second : nullptr;

                QString source_path = original_path;
                if (!use_in_memory_calibration && reads_calibrated_copies)
                {
                    const QString calibrated_path = calibratedLightOutputPathFor(manifest.outputDirectory, frame);
                    if (QFileInfo::exists(calibrated_path))
                    {
                        source_path = calibrated_path;
                    }
                }

                const QString output_path = chromaticAberrationLightOutputPathFor(manifest.outputDirectory, frame);
                const QString artifact_hash = computeChromaticAberrationArtifactHash(
                    frame, source_path, group.settings.chromaticAberration, assignment, use_in_memory_calibration);
                if (!manifest.reprocessExisting &&
                    chromatic_aberration_tracker.isArtifactValid(output_path, artifact_hash))
                {
                    ++ca_reused_frames;
                    continue;
                }

                stage_utils::reportStageProgress(
                    reporter, stageId(), group_index, total_groups,
                    0.05f * static_cast<float>(ca_index + 1) / static_cast<float>((std::max)(1, ca_total)),
                    QStringLiteral("Correcting chromatic aberration (%1 of %2)").arg(ca_index + 1).arg(ca_total),
                    frame.toString());

                bool corrected_this_frame = false;
                bool read_failed = false;
                const an::Result<an::ArtifactIdentity> identity =
                    ImageIntegration::workflowArtifactIdentity(artifact_hash, manifest.executionId);
                const an::Status outcome = [&]() -> an::Status {
                    if (!identity.hasValue())
                    {
                        return identity.status();
                    }
                    an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
                    if (!scope.hasValue())
                    {
                        return scope.status();
                    }
                    const core::LightCalibration* calibration = nullptr;
                    if (use_in_memory_calibration)
                    {
                        an::Result<const core::LightCalibration*> prepared = calibration_for(*scope, frame);
                        if (!prepared.hasValue())
                        {
                            read_failed = true;
                            return prepared.status();
                        }
                        calibration = *prepared;
                    }
                    an::Result<an::Image<float>> image = stage_utils::readRegistrationImage(
                        *scope, registry, use_in_memory_calibration ? original_path : source_path, calibration,
                        demosaic);
                    if (!image.hasValue())
                    {
                        read_failed = true;
                        return image.status();
                    }
                    an::Result<an::ui::processes::ChromaticCorrection> correction =
                        an::ui::processes::correctChromaticAberration(scope->engine(), scope->execution(), *image,
                                                                      group.settings.chromaticAberration,
                                                                      detection_request);
                    if (!correction.hasValue())
                    {
                        if (cancelledStatus(correction.status()))
                        {
                            return correction.status();
                        }
                        reporter.logger().warning(
                            QStringLiteral("Chromatic aberration correction failed for %1; saving pass-through image "
                                           "(%2)")
                                .arg(original_path, ImageIntegration::engineFailureText(correction.status())));
                    }
                    else
                    {
                        corrected_this_frame = correction->corrected();
                    }
                    return ImageIntegration::publishWorkflowImage(*scope, *image, output_path, *identity);
                }();
                if (outcome.failed())
                {
                    if (cancelledStatus(outcome))
                    {
                        return StageResult::cancelled();
                    }
                    if (read_failed)
                    {
                        reporter.logger().warning(
                            QStringLiteral("Failed to load CA input for %1 (%2); skipping frame")
                                .arg(original_path, ImageIntegration::engineFailureText(outcome)));
                        ++ca_failed_frames;
                        continue;
                    }
                    return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                        outcome, IntegrationErrorCode::FileNotWritable,
                        QStringLiteral("Failed to write chromatic aberration output"), output_path));
                }
                // The record follows the committed write, so a stop between them leaves a whole file the next run
                // corrects again, never a record of a file not written.
                if (!chromatic_aberration_tracker.recordArtifact(output_path, artifact_hash, *identity, cancelled))
                {
                    if (cancelled())
                    {
                        return StageResult::cancelled();
                    }
                    reporter.logger().warning(
                        QStringLiteral("Failed to record the chromatic aberration output %1").arg(output_path));
                }
                corrected_this_frame ? ++ca_corrected_frames : ++ca_passthrough_frames;
            }

            if (ca_failed_frames > 0)
            {
                reporter.logger().warning(
                    QStringLiteral("Chromatic aberration: %1 frame(s) failed to load and were skipped")
                        .arg(ca_failed_frames));
            }
            // The file log says why for each channel; the group says that nothing moved.
            if (!chromatic_aberration_one_channel && ca_corrected_frames == 0 && ca_reused_frames == 0 &&
                ca_passthrough_frames > 0)
            {
                reporter.logger().warning(QStringLiteral(
                    "Chromatic aberration correction moved no channel of any light in this group: in none did enough "
                    "red or blue edge stars match the green ones, so its lights are registered as they were read."));
            }
            reporter.logger().operationDetail(QStringLiteral("Chromatic aberration"),
                                              QStringLiteral("reused=%1 corrected=%2 passthrough=%3 failed=%4")
                                                  .arg(ca_reused_frames)
                                                  .arg(ca_corrected_frames)
                                                  .arg(ca_passthrough_frames)
                                                  .arg(ca_failed_frames));
            AlignmentGroupResult ca_result;
            ca_result.groupKey = group_key;
            ca_result.caCorrectedCount = ca_corrected_frames + ca_reused_frames;
            ca_result.caPassthroughCount = ca_passthrough_frames;
            ca_group_results.push_back(std::move(ca_result));

            // The corrected lights are the registration's inputs now, already calibrated.
            use_in_memory_calibration = false;
        }

        auto inputs = buildRegistrationInputs(group, manifest.outputDirectory,
                                              !use_in_memory_calibration && reads_calibrated_copies, error);
        if (inputs.empty())
        {
            const QString message = error.isEmpty() ? QStringLiteral("No input files provided") : error;
            reporter.logger().error(QStringLiteral("No input files provided"), error);
            error_report.addError(IntegrationError(IntegrationErrorCode::EmptyFrameList, message, group_key));
            finish_group();
            continue;
        }
        reporter.logger().operationDetail(QStringLiteral("Group frames"), QString::number(inputs.size()));

        if (!auto_reference_runtime && group.referenceFrame.has_value())
        {
            auto ref_it = std::find_if(inputs.begin(), inputs.end(), [&](const RegistrationInput& input) {
                return input.originalId == group.referenceFrame.value();
            });
            if (ref_it != inputs.end() && ref_it != inputs.begin())
            {
                std::iter_swap(inputs.begin(), ref_it);
            }
            reporter.logger().operationDetail(QStringLiteral("Reference frame"),
                                              group.referenceFrame.value().toString());
        }
        else if (auto_reference_runtime)
        {
            reporter.logger().operationDetail(QStringLiteral("Reference frame"),
                                              QStringLiteral("Auto-select during registration"));
        }
        else
        {
            reporter.logger().operationDetail(
                QStringLiteral("Reference frame"),
                QStringLiteral("%1 (first in group)").arg(inputs.front().originalId.toString()));
        }

        // Every light's header, read once: its extent, its filter, its times and its own solution.
        std::unordered_map<QString, RegistrationFrameFacts> facts_by_input;
        {
            an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
            if (!scope.hasValue())
            {
                if (cancelledStatus(scope.status()))
                {
                    return StageResult::cancelled();
                }
                error_report.addError(ImageIntegration::integrationErrorFromEngine(
                    scope.status(), IntegrationErrorCode::RegistrationFailed,
                    QStringLiteral("Registration could not start"), group_key));
                finish_group();
                continue;
            }
            for (const RegistrationInput& input : inputs)
            {
                an::Result<RegistrationFrameFacts> facts =
                    stage_utils::probeRegistrationFrame(*scope, registry, input.registrationId.canonicalPath());
                if (facts.hasValue())
                {
                    facts_by_input.emplace(pathKey(input.registrationId.canonicalPath()), std::move(*facts));
                }
                else if (cancelledStatus(facts.status()))
                {
                    return StageResult::cancelled();
                }
                else
                {
                    reporter.logger().warning(QStringLiteral("Failed to read the header of %1: %2")
                                                  .arg(input.registrationId.canonicalPath(),
                                                       ImageIntegration::engineFailureText(facts.status())));
                }
            }
        }
        const auto facts_for = [&](const RegistrationInput& input) -> const RegistrationFrameFacts* {
            const auto it = facts_by_input.find(pathKey(input.registrationId.canonicalPath()));
            return it == facts_by_input.end() ? nullptr : &it->second;
        };

        // A frame's detection: its record, its stars' full fits and the input revision it was read at.
        struct DetectedFrame
        {
            RegisteredFrame frame;
            std::vector<an::DetectedStar> stars;
            FileRevisionStamp revision;
        };
        std::unordered_map<QString, DetectedFrame> detected_by_input;
        std::unordered_set<QString> unstable_detection_inputs;

        /// Reads one input as the registration detects it - calibrated by its masters when asked - in an operation the
        /// caller holds. Only one thread reads at a time: the master set and the calibrations it prepared are its own.
        const auto read_input = [&](const an::consumer::OperationScope& scope, const RegistrationInput& input,
                                    const bool calibrate) -> an::Result<an::Image<float>> {
            const core::LightCalibration* calibration = nullptr;
            if (calibrate)
            {
                an::Result<const core::LightCalibration*> prepared = calibration_for(scope, input.originalId);
                if (!prepared.hasValue())
                {
                    return prepared.status();
                }
                calibration = *prepared;
            }
            const auto read_started = std::chrono::steady_clock::now();
            an::Result<an::Image<float>> image = stage_utils::readRegistrationImage(
                scope, registry, calibrate ? input.originalId.canonicalPath() : input.registrationId.canonicalPath(),
                calibration, demosaic);
            stage_profile.readNanoseconds += elapsedNanoseconds(read_started);
            return image;
        };

        /// Detects the stars of one input read as above and keeps them under the input's path, with the search
        /// channel's statistics measured beforehand when they were.
        const auto detect_input = [&](const an::consumer::OperationScope& scope, const RegistrationInput& input,
                                      const an::Image<float>& image, const std::optional<FileRevisionStamp>& revision,
                                      const an::StarChannelStatistics* measured) -> an::Status {
            const auto detect_started = std::chrono::steady_clock::now();
            an::Result<an::ui::processes::FrameDetection> detection =
                measured != nullptr
                    ? an::ui::processes::detectRegistrationStars(scope.execution(), image, detection_request, *measured)
                    : an::ui::processes::detectRegistrationStars(scope.execution(), image, detection_request);
            stage_profile.detectNanoseconds += elapsedNanoseconds(detect_started);
            if (!detection.hasValue())
            {
                return detection.status();
            }
            const QString input_path = input.registrationId.canonicalPath();
            DetectedFrame detected;
            detected.frame = std::move(detection->frame);
            detected.frame.sourcePath = input_path;
            detected.stars = std::move(detection->stars);
            if (revision.has_value())
            {
                detected.revision = *revision;
            }
            stage_profile.starsDetected += detected.frame.stars.size();
            detected_by_input.insert_or_assign(pathKey(input_path), std::move(detected));
            return an::Status::success();
        };

        /// Detects the stars of each input in its own admitted operation, calibrating as it reads when asked. When the
        /// run is wide enough the next input is read on a thread of its own, in an operation of its own, while this
        /// one's stars are detected (`stage_utils::pipelineWidths`); from twelve threads its search channel is also
        /// measured on a thread of its own - the statistics the record keeps, which are most of a detection's cost -
        /// and handed to the search, which then gives the detection it would have measured for itself.
        const auto detect_frames = [&](const std::vector<RegistrationInput>& targets, const bool calibrate,
                                       const float progress_start, const float progress_end, PhaseProgressLog* log,
                                       QStringList& failures) -> an::Status {
            const int total = static_cast<int>(targets.size());
            int done = 0;
            // What follows one input's detection: its failure or changed file recorded, and the progress reported.
            const auto settle = [&](const RegistrationInput& input, const an::Status& outcome,
                                    const std::optional<FileRevisionStamp>& revision) {
                const QString input_path = input.registrationId.canonicalPath();
                ++stage_profile.framesDetected;
                if (outcome.failed())
                {
                    failures << QStringLiteral("%1 (%2)").arg(input_path, ImageIntegration::engineFailureText(outcome));
                    detected_by_input.erase(pathKey(input_path));
                }
                else if (!revision.has_value() || !inputUnchangedSince(input, *revision))
                {
                    unstable_detection_inputs.insert(pathKey(input_path));
                    detected_by_input.erase(pathKey(input_path));
                    const QString reason =
                        QStringLiteral("Input file changed during star detection: %1").arg(input_path);
                    reporter.logger().warning(reason);
                    error_report.addError(IntegrationError::fileNotReadable(input_path, reason));
                }
                ++done;
                if (log != nullptr)
                {
                    log->frameDone(done);
                }
                stage_utils::reportStageProgress(
                    reporter, stageId(), group_index, total_groups,
                    progress_start + (progress_end - progress_start) * static_cast<float>(done) /
                                         static_cast<float>((std::max)(1, total)),
                    QStringLiteral("Detecting stars in image %1 of %2").arg(done).arg(total),
                    input.originalId.toString());
            };

            const stage_utils::PipelineWidths widths = stage_utils::pipelineWidths(services->runWidth(), false);
            if (!widths.overlapped)
            {
                for (const RegistrationInput& input : targets)
                {
                    if (cancelled())
                    {
                        return an::Status::failure(an::ErrorCode::Cancelled, "the registration was stopped");
                    }
                    const auto revision = captureInputRevision(input);
                    const auto loop_started = std::chrono::steady_clock::now();
                    const an::Status outcome = [&]() -> an::Status {
                        an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
                        if (!scope.hasValue())
                        {
                            return scope.status();
                        }
                        an::Result<an::Image<float>> image = read_input(*scope, input, calibrate);
                        if (!image.hasValue())
                        {
                            return image.status();
                        }
                        return detect_input(*scope, input, *image, revision, nullptr);
                    }();
                    stage_profile.detectionLoopNanoseconds += elapsedNanoseconds(loop_started);
                    if (cancelledStatus(outcome))
                    {
                        return outcome;
                    }
                    settle(input, outcome, revision);
                }
                return an::Status::success();
            }

            // An input read ahead: the revision it was read at, and the light or why it could not be read.
            struct ReadInput
            {
                std::optional<FileRevisionStamp> revision;
                an::Status status;
                std::optional<an::Image<float>> image;
            };
            // An input read and measured ahead: what the reader gave, and its search channel's statistics.
            struct MeasuredInput
            {
                ReadInput read;
                std::optional<an::StarChannelStatistics> measured;
            };
            // Two measurements at once, because one does not keep the threads it is given busy: most of it is short
            // passes over the plane with a decision between each.
            const bool measure_ahead = services->runWidth() >= 12u;
            const std::uint32_t measurers = 2u;
            const std::uint32_t measuring_width =
                measure_ahead ? (std::max)(1u, widths.compute * 3u / (7u * measurers)) : 0u;
            const std::uint32_t detecting_width = widths.compute - measuring_width * (measure_ahead ? measurers : 0u);
            stage_utils::ReadAhead<ReadInput> reader(targets.size(), 1u, [&](const std::size_t index, std::stop_token) {
                ReadInput read;
                read.revision = captureInputRevision(targets[index]);
                an::Result<an::consumer::OperationScope> scope = services->admitStageOperationAtWidth(widths.reader);
                if (!scope.hasValue())
                {
                    read.status = scope.status();
                    return read;
                }
                an::Result<an::Image<float>> image = read_input(*scope, targets[index], calibrate);
                if (image.hasValue())
                {
                    read.image.emplace(std::move(*image));
                }
                else
                {
                    read.status = image.status();
                }
                return read;
            });
            // Declared after the reader, whose items it takes, so that it stops first.
            stage_utils::ReadAhead<MeasuredInput> measurer(
                measure_ahead ? targets.size() : 0u, 1u, measurers, [&](const std::size_t index, std::stop_token) {
                    MeasuredInput measured;
                    measured.read = reader.take(index);
                    if (measured.read.status.failed())
                    {
                        return measured;
                    }
                    an::Result<an::consumer::OperationScope> scope =
                        services->admitStageOperationAtWidth(measuring_width);
                    if (!scope.hasValue())
                    {
                        measured.read.status = scope.status();
                        return measured;
                    }
                    an::Result<an::StarChannelStatistics> statistics = an::ui::processes::measureRegistrationChannel(
                        scope->execution(), *measured.read.image, detection_request);
                    if (statistics.hasValue())
                    {
                        measured.measured = *statistics;
                    }
                    else
                    {
                        measured.read.status = statistics.status();
                    }
                    return measured;
                });
            for (const RegistrationInput& input : targets)
            {
                if (cancelled())
                {
                    return an::Status::failure(an::ErrorCode::Cancelled, "the registration was stopped");
                }
                const auto loop_started = std::chrono::steady_clock::now();
                MeasuredInput item = measure_ahead ? measurer.take() : MeasuredInput{reader.take(), std::nullopt};
                ReadInput& read = item.read;
                const an::Status outcome = [&]() -> an::Status {
                    if (read.status.failed())
                    {
                        return read.status;
                    }
                    an::Result<an::consumer::OperationScope> scope =
                        services->admitStageOperationAtWidth(detecting_width);
                    if (!scope.hasValue())
                    {
                        return scope.status();
                    }
                    return detect_input(*scope, input, *read.image, read.revision,
                                        item.measured.has_value() ? &*item.measured : nullptr);
                }();
                read.image.reset();
                stage_profile.detectionLoopNanoseconds += elapsedNanoseconds(loop_started);
                if (cancelledStatus(outcome))
                {
                    return outcome;
                }
                settle(input, outcome, read.revision);
            }
            return an::Status::success();
        };

        const auto persist_star_cache = [&](const std::vector<RegistrationInput>& targets) {
            std::vector<RegistrationStarCacheEntry> entries;
            entries.reserve(targets.size());
            for (const RegistrationInput& input : targets)
            {
                const auto it = detected_by_input.find(pathKey(input.registrationId.canonicalPath()));
                if (it == detected_by_input.end() || it->second.revision.identityToken.isEmpty())
                {
                    continue;
                }
                entries.push_back(
                    starCacheEntryOf(input, group_key, detection_hash, it->second.frame, it->second.revision));
            }
            QString cache_error;
            if (!metrics_writer.upsertStarCacheForGroup(group_key, detection_hash, entries, &cache_error))
            {
                reporter.logger().warning(QStringLiteral("Failed to persist star detection cache batch for %1: %2")
                                              .arg(group_label, cache_error));
            }
        };

        if (configured_registration_method == QStringLiteral("comet_tracking"))
        {
            const auto finish_comet_group_with_error = [&](const QString& message) {
                reporter.logger().error(QStringLiteral("Comet tracking failed"), message);
                error_report.addError(
                    IntegrationError::registrationFailed(QStringLiteral("Group '%1': %2").arg(group_label, message)));
                finish_group();
            };
            if (!group.settings.cometTracking.has_value())
            {
                finish_comet_group_with_error(QStringLiteral("Comet tracking settings are required."));
                continue;
            }
            if (!group.referenceFrame.has_value() || !group.referenceFrame->isValid())
            {
                finish_comet_group_with_error(QStringLiteral("Comet tracking requires an explicit reference frame."));
                continue;
            }
            const FrameId resolved_reference_frame = group.referenceFrame.value();
            if (std::none_of(inputs.begin(), inputs.end(), [&](const RegistrationInput& input) {
                    return input.originalId == resolved_reference_frame;
                }))
            {
                finish_comet_group_with_error(
                    QStringLiteral("Comet tracking reference frame is not part of the registration group."));
                continue;
            }
            const auto missing_header = std::find_if(inputs.begin(), inputs.end(), [&](const RegistrationInput& input) {
                return facts_for(input) == nullptr;
            });
            if (missing_header != inputs.end())
            {
                finish_comet_group_with_error(QStringLiteral("Failed to read comet registration metadata: %1")
                                                  .arg(missing_header->registrationId.canonicalPath()));
                continue;
            }

            // The frames' quality figures and star detections, which the dashboard and a later stars-plus-comet run
            // read, are measured even though the comet's transforms do not use the stars.
            QStringList quality_failures;
            const an::Status quality =
                detect_frames(inputs, use_in_memory_calibration, 0.0f, 0.4f, nullptr, quality_failures);
            if (cancelledStatus(quality))
            {
                return StageResult::cancelled();
            }
            std::vector<RegistrationMetricRecord> comet_quality_metrics;
            comet_quality_metrics.reserve(inputs.size());
            int missing_quality_metrics = 0;
            for (const RegistrationInput& input : inputs)
            {
                const auto it = detected_by_input.find(pathKey(input.registrationId.canonicalPath()));
                if (it == detected_by_input.end())
                {
                    ++missing_quality_metrics;
                    continue;
                }
                RegistrationMetricRecord metric;
                metric.frameId = input.originalId;
                metric.groupKey = group_key;
                metric.settingsVersion = settings_version;
                metric.metrics = it->second.frame.metrics;
                metric.noise = it->second.frame.noise;
                comet_quality_metrics.push_back(std::move(metric));
            }
            QString comet_metrics_error;
            int persisted_quality_metrics = 0;
            if (metrics_writer.upsertMetricsForGroup(group_key, settings_version, comet_quality_metrics,
                                                     &comet_metrics_error))
            {
                persisted_quality_metrics = static_cast<int>(comet_quality_metrics.size());
            }
            else
            {
                reporter.logger().warning(QStringLiteral("Failed to persist comet frame-quality batch for %1: %2")
                                              .arg(group_label, comet_metrics_error));
            }
            persist_star_cache(inputs);
            reporter.logger().operationDetail(
                QStringLiteral("Frame quality"),
                QStringLiteral("persisted=%1 missing=%2").arg(persisted_quality_metrics).arg(missing_quality_metrics));

            an::ui::CometRegistrationProductRequest comet_request;
            comet_request.groupKey = group_key;
            comet_request.settingsVersion = settings_version;
            comet_request.detectionHash = detection_hash;
            comet_request.registrationHash = registration_hash;
            comet_request.lightFilesHash = light_files_hash;
            comet_request.calibrationHash = calibration_hash;
            comet_request.cometSettings = group.settings.cometTracking.value();
            comet_request.resolvedReferenceFrame = resolved_reference_frame;
            comet_request.allowPredictedOnlyTransforms = true;
            comet_request.requiresMaterializedOutputs = false;
            comet_request.updatedAt = QDateTime::currentMSecsSinceEpoch();
            for (const RegistrationInput& input : inputs)
            {
                const RegistrationFrameFacts& facts = *facts_for(input);
                an::ui::CometRegistrationFrameInput frame;
                frame.originalFrameId = input.originalId;
                frame.refinementSourcePath = input.registrationId.canonicalPath();
                frame.dateObs = facts.dateObs.isEmpty() ? cometManifestDateObs(group, input.originalId) : facts.dateObs;
                frame.exposureSeconds = facts.exposureSeconds.value_or(0.0);
                if (!(frame.exposureSeconds > 0.0) || !std::isfinite(frame.exposureSeconds))
                {
                    frame.exposureSeconds = cometManifestExposureSeconds(group, input.originalId);
                }
                frame.width = static_cast<int>(facts.width);
                frame.height = static_cast<int>(facts.height);
                frame.hasSipDistortion = facts.distortion.has_value();
                frame.sourceDistortion = facts.distortion;
                if (const auto cal_it = calibration_params_by_light.find(pathKey(input.originalId.canonicalPath()));
                    cal_it != calibration_params_by_light.end())
                {
                    frame.calibrationFingerprint = computeCalibratedLightArtifactHash(*cal_it->second);
                }
                if (group.settings.chromaticAberration.enabled &&
                    pathKey(input.registrationId.canonicalPath()) ==
                        pathKey(chromaticAberrationLightOutputPathFor(manifest.outputDirectory, input.originalId)))
                {
                    QString source_path = input.originalId.canonicalPath();
                    if (!chromatic_aberration_used_in_memory_calibration && !manifest.calibrateLightsInMemory)
                    {
                        const QString calibrated_path =
                            calibratedLightOutputPathFor(manifest.outputDirectory, input.originalId);
                        if (QFileInfo::exists(calibrated_path))
                        {
                            source_path = calibrated_path;
                        }
                    }
                    const auto cal_it = calibration_params_by_light.find(pathKey(input.originalId.canonicalPath()));
                    frame.chromaticCorrectionFingerprint = computeChromaticAberrationArtifactHash(
                        input.originalId, source_path, group.settings.chromaticAberration,
                        cal_it != calibration_params_by_light.end() ? cal_it->second : nullptr,
                        chromatic_aberration_used_in_memory_calibration);
                }
                comet_request.frames.push_back(std::move(frame));
            }

            an::Result<an::consumer::OperationScope> comet_scope = services->admitStageOperation();
            if (!comet_scope.hasValue())
            {
                if (cancelledStatus(comet_scope.status()))
                {
                    return StageResult::cancelled();
                }
                finish_comet_group_with_error(ImageIntegration::engineFailureText(comet_scope.status()));
                continue;
            }

            // The tracker measures a frame's nucleus in its refinement file, read once while the tracker asks about
            // it.
            struct RefinementFrame
            {
                QString path;
                std::optional<an::Image<float>> image;
                std::optional<an::ImageLayout> layout;
                /// The samples the nucleus is measured over: every finite one when the measured channel holds one that
                /// is not, as a light calibrated where its flat is zero does.
                an::KernelValidityView validity;
                std::uint32_t channel = 0u;
            };
            RefinementFrame refinement_frame;
            comet_request.refiner =
                [&](const an::ui::CometTrackingFrameContext& context, const an::PixelCoordinate& seed,
                    const an::CometCenterRefinementOptions& options) -> an::Result<an::ui::CometRefinementMeasurement> {
                if (refinement_frame.path != context.refinementSourcePath || !refinement_frame.image.has_value())
                {
                    refinement_frame = RefinementFrame{};
                    an::Result<an::Image<float>> image = stage_utils::readRegistrationImage(
                        *comet_scope, registry, context.refinementSourcePath, nullptr, demosaic);
                    if (!image.hasValue())
                    {
                        return image.status();
                    }
                    const auto view = std::as_const(*image).view();
                    if (!view.has_value())
                    {
                        return an::Status::failure(an::ErrorCode::NotInitialized, "the comet frame holds nothing");
                    }
                    refinement_frame.layout = view->layout();
                    const std::optional<an::ImageDescriptor> described = image->descriptor();
                    refinement_frame.channel =
                        described.has_value() && described->extent().channelCount() >= 3u ? 1u : 0u;
                    refinement_frame.validity =
                        an::ui::processes::registrationChannelValidity(*image, refinement_frame.channel);
                    refinement_frame.image = std::move(*image);
                    refinement_frame.path = context.refinementSourcePath;
                }
                const an::Image<float>& image = *refinement_frame.image;
                const auto view = image.view();
                const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
                if (!view.has_value() || !descriptor.has_value())
                {
                    return an::Status::failure(an::ErrorCode::NotInitialized, "the comet frame holds nothing");
                }
                const an::Result<an::StarFrameView> frame_view =
                    an::StarFrameView::create(view->data(), refinement_frame.layout->totalBytes(),
                                              *refinement_frame.layout, *descriptor, refinement_frame.validity);
                if (!frame_view.hasValue())
                {
                    return frame_view.status();
                }
                an::CometCenterRefinementOptions measured = options;
                measured.channelIndex = refinement_frame.channel;
                an::Result<an::CometCenterRefinementResult> refined =
                    an::refineCometCenter(comet_scope->execution(), *frame_view, seed, measured);
                if (!refined.hasValue())
                {
                    return refined.status();
                }
                return an::ui::CometRefinementMeasurement{.refined = *refined,
                                                          .width = descriptor->extent().width(),
                                                          .height = descriptor->extent().height()};
            };
            comet_request.isCancellationRequested = cancelled;

            const an::ui::CometRegistrationProductResult comet_products =
                an::ui::buildCometRegistrationProducts(comet_scope->execution(), comet_request);
            refinement_frame = RefinementFrame{};
            if (comet_products.cancelled || cancelled())
            {
                return StageResult::cancelled();
            }
            if (comet_products.sidecarRequest.run.cometRunId.trimmed().isEmpty() ||
                comet_products.sidecarRequest.transformProducts.transforms.empty())
            {
                finish_comet_group_with_error(
                    comet_products.message.isEmpty()
                        ? QStringLiteral("Comet registration did not produce sidecar products.")
                        : comet_products.message);
                continue;
            }

            QString sidecar_error;
            const an::ui::CometTrackingSidecarProductWriteResult write_result =
                an::ui::writeCometTrackingSidecarProducts(metrics_writer, comet_products.sidecarRequest,
                                                          &sidecar_error);
            if (!write_result.succeeded)
            {
                finish_comet_group_with_error(sidecar_error.isEmpty()
                                                  ? QStringLiteral("Failed to persist comet sidecar products.")
                                                  : sidecar_error);
                continue;
            }

            RegistrationGroupState comet_group_state;
            comet_group_state.groupKey = group_key;
            comet_group_state.settingsVersion = settings_version;
            comet_group_state.detectionHash = detection_hash;
            comet_group_state.registrationHash = registration_hash;
            comet_group_state.referenceFrameId = resolved_reference_frame.canonicalPath();
            comet_group_state.lightFilesHash = light_files_hash;
            comet_group_state.calibrationHash = calibration_hash;
            QString group_state_error;
            if (!metrics_writer.upsertGroupState(comet_group_state, &group_state_error))
            {
                finish_comet_group_with_error(group_state_error.isEmpty()
                                                  ? QStringLiteral("Failed to persist comet registration group state.")
                                                  : group_state_error);
                continue;
            }
            if (!comet_products.succeeded)
            {
                finish_comet_group_with_error(comet_products.message.isEmpty()
                                                  ? QStringLiteral("Comet transform products are not ready.")
                                                  : comet_products.message);
                continue;
            }
            QString activation_error;
            if (!metrics_writer.activateCometTrackingRun(write_result.cometRunId, {QStringLiteral("comet_aligned")},
                                                         false, &activation_error))
            {
                finish_comet_group_with_error(activation_error.isEmpty()
                                                  ? QStringLiteral("Failed to activate comet tracking run.")
                                                  : activation_error);
                continue;
            }
            reporter.logger().operationDetail(
                QStringLiteral("Comet products"),
                QStringLiteral("Activated %1 with %2 transform role(s)")
                    .arg(write_result.cometRunId)
                    .arg(comet_products.sidecarRequest.transformProducts.transforms.size()));
            if (!stars_plus_comet_star_base_required)
            {
                finish_group();
                continue;
            }
            reporter.logger().operationDetail(
                QStringLiteral("Star base products"),
                QStringLiteral("%1 requires sidereal star-base transforms; running star registration.")
                    .arg(ImageIntegration::starsPlusCometExperimentalStackingModeToken()));
        }

        std::unordered_map<QString, RegistrationMetricStatus> metrics_by_frame;
        QString metrics_status_error;
        if (!metrics_loader.loadMetricStatuses(group_key, settings_version, metrics_by_frame, &metrics_status_error))
        {
            reporter.logger().warning(
                QStringLiteral("Failed to load cached registration metrics: %1").arg(metrics_status_error));
        }
        std::unordered_map<QString, RegistrationStarCacheEntry> star_cache_by_frame;
        QString star_cache_error;
        if (!metrics_loader.loadStarCacheForGroup(group_key, detection_hash, star_cache_by_frame, &star_cache_error))
        {
            reporter.logger().warning(
                QStringLiteral("Failed to load cached star detections: %1").arg(star_cache_error));
        }
        std::unordered_map<QString, RegistrationMetricStatus> metrics_by_key;
        metrics_by_key.reserve(metrics_by_frame.size());
        for (const auto& [frame_id, status] : metrics_by_frame)
        {
            metrics_by_key.emplace(pathKey(frame_id), status);
        }
        std::unordered_map<QString, RegistrationStarCacheEntry> star_cache_by_key;
        star_cache_by_key.reserve(star_cache_by_frame.size());
        for (const auto& [frame_id, entry] : star_cache_by_frame)
        {
            star_cache_by_key.emplace(pathKey(frame_id), entry);
        }

        const auto has_current_calibration_assignment = [&](const RegistrationInput& input) {
            return calibration_params_by_light.find(pathKey(input.originalId.canonicalPath())) !=
                   calibration_params_by_light.end();
        };

        int detect_forced_by_calibration = 0;
        int detect_missing_cache = 0;
        int detect_input_path_changed = 0;
        int detect_input_changed = 0;
        int register_forced_by_calibration = 0;
        int register_missing_metrics = 0;
        int register_missing_transform = 0;
        int register_failed_metrics = 0;
        int reused_star_cache = 0;
        int reused_registration_metrics = 0;

        const auto cached_star_detection = [&](const RegistrationInput& input) -> const RegistrationStarCacheEntry* {
            const auto it = star_cache_by_key.find(pathKey(input.originalId.canonicalPath()));
            if (it == star_cache_by_key.end() ||
                unstable_detection_inputs.contains(pathKey(input.registrationId.canonicalPath())) ||
                pathKey(it->second.inputPath) != pathKey(input.registrationId.canonicalPath()) ||
                !starCacheInputMatches(it->second, input.registrationId.canonicalPath()))
            {
                return nullptr;
            }
            return &it->second;
        };

        const auto detect_needed_for = [&](const RegistrationInput& input) -> bool {
            if (!method_uses_star_detection)
            {
                return false;
            }
            if (detected_by_input.contains(pathKey(input.registrationId.canonicalPath())))
            {
                return false;
            }
            if (force_star_detection_all)
            {
                return true;
            }
            if (calibration_changed && use_in_memory_calibration && has_current_calibration_assignment(input))
            {
                ++detect_forced_by_calibration;
                return true;
            }
            const auto it = star_cache_by_key.find(pathKey(input.originalId.canonicalPath()));
            if (it == star_cache_by_key.end())
            {
                ++detect_missing_cache;
                return true;
            }
            if (pathKey(it->second.inputPath) != pathKey(input.registrationId.canonicalPath()))
            {
                ++detect_input_path_changed;
                return true;
            }
            if (!starCacheInputMatches(it->second, input.registrationId.canonicalPath()))
            {
                ++detect_input_changed;
                return true;
            }
            ++reused_star_cache;
            return false;
        };

        const auto register_needed_for = [&](const RegistrationInput& input, const bool detect_needed) -> bool {
            if (force_registration_all)
            {
                return true;
            }
            if (calibration_changed && has_current_calibration_assignment(input))
            {
                ++register_forced_by_calibration;
                return true;
            }
            if (detect_needed)
            {
                return true;
            }
            const auto it = metrics_by_key.find(pathKey(input.originalId.canonicalPath()));
            if (it == metrics_by_key.end())
            {
                ++register_missing_metrics;
                return true;
            }
            if (!it->second.succeeded)
            {
                ++register_failed_metrics;
                return true;
            }
            if (it->second.transformJson.trimmed().isEmpty())
            {
                ++register_missing_transform;
                return true;
            }
            ++reused_registration_metrics;
            return false;
        };

        std::vector<RegistrationInput> registration_inputs;
        registration_inputs.reserve(inputs.size());
        std::unordered_set<QString> update_metric_frames;
        std::vector<RegistrationInput> detect_inputs;
        for (const auto& input : inputs)
        {
            const bool detect_needed = detect_needed_for(input);
            if (register_needed_for(input, detect_needed))
            {
                registration_inputs.push_back(input);
                update_metric_frames.insert(input.originalId.canonicalPath());
                if (detect_needed)
                {
                    detect_inputs.push_back(input);
                }
            }
        }

        // A subset is registered against the reference, which is included as context even when nothing of it
        // changed.
        if (!registration_inputs.empty() &&
            std::none_of(registration_inputs.begin(), registration_inputs.end(),
                         [&](const RegistrationInput& in) { return in.originalId == inputs.front().originalId; }))
        {
            registration_inputs.insert(registration_inputs.begin(), inputs.front());
            if (method_uses_star_detection &&
                !detected_by_input.contains(pathKey(inputs.front().registrationId.canonicalPath())) &&
                cached_star_detection(inputs.front()) == nullptr)
            {
                detect_inputs.insert(detect_inputs.begin(), inputs.front());
            }
        }

        if (registration_inputs.empty())
        {
            reporter.logger().operationDetail(QStringLiteral("Incremental"),
                                              QStringLiteral("No registration updates required; skipping group."));
            LOG_INFO("LightsRegistrationStage: skipping group '{}' (no changes; settings_version={})",
                     group_key.toStdString(), settings_version);
            if (!prev_state.has_value() || !reasons.isEmpty())
            {
                RegistrationGroupState skipped_group_state;
                skipped_group_state.groupKey = group_key;
                skipped_group_state.settingsVersion = settings_version;
                skipped_group_state.detectionHash = detection_hash;
                skipped_group_state.registrationHash = registration_hash;
                skipped_group_state.referenceFrameId =
                    prev_state.has_value() && auto_reference_runtime ? prev_state->referenceFrameId : ref_frame_id;
                skipped_group_state.lightFilesHash = light_files_hash;
                skipped_group_state.calibrationHash = calibration_hash;
                skipped_group_state.outputWcsJson = output_wcs_json;
                if (!metrics_writer.upsertGroupState(skipped_group_state))
                {
                    reporter.logger().warning(
                        QStringLiteral("Failed to persist skipped group state for group '%1'").arg(group_key));
                }
            }
            finish_group();
            continue;
        }

        if (frame_list_changed || calibration_changed || detect_missing_cache > 0 || detect_input_path_changed > 0 ||
            detect_input_changed > 0 || register_missing_metrics > 0 || register_missing_transform > 0 ||
            register_failed_metrics > 0)
        {
            QStringList reuse_details;
            reuse_details << QStringLiteral("metrics reused=%1").arg(reused_registration_metrics);
            if (method_uses_star_detection)
            {
                reuse_details << QStringLiteral("star cache reused=%1").arg(reused_star_cache);
            }
            const auto add_detail = [&](const int count, const QString& label) {
                if (count > 0)
                {
                    reuse_details << QStringLiteral("%1=%2").arg(label).arg(count);
                }
            };
            add_detail(detect_forced_by_calibration, QStringLiteral("calibrated detections invalidated"));
            add_detail(register_forced_by_calibration, QStringLiteral("calibrated registrations invalidated"));
            add_detail(detect_missing_cache, QStringLiteral("missing star cache"));
            add_detail(detect_input_path_changed, QStringLiteral("star input path changed"));
            add_detail(detect_input_changed, QStringLiteral("star input changed on disk"));
            add_detail(register_missing_metrics, QStringLiteral("missing metrics"));
            add_detail(register_missing_transform, QStringLiteral("missing transforms"));
            add_detail(register_failed_metrics, QStringLiteral("failed metrics"));
            reporter.logger().operationDetail(QStringLiteral("Per-frame cache analysis"),
                                              reuse_details.join(QStringLiteral("; ")));
        }
        const int context_frames =
            static_cast<int>(registration_inputs.size()) - static_cast<int>(update_metric_frames.size());
        reporter.logger().operationDetail(
            QStringLiteral("Incremental plan"),
            QStringLiteral("Detect stars: %1 | Register/update: %2 | Reuse metrics: %3%4")
                .arg(detect_inputs.size())
                .arg(update_metric_frames.size())
                .arg(reused_registration_metrics)
                .arg(context_frames > 0 ? QStringLiteral(" | Context frames: %1").arg(context_frames) : QString()));

        QString unsupported_transform_error;
        if (validateTransformContract(effective_registration_settings, unsupported_transform_error))
        {
            reporter.logger().error(QStringLiteral("Registration settings are not supported"),
                                    unsupported_transform_error);
            return StageResult::failure(IntegrationError::registrationFailed(
                QStringLiteral("Group '%1': %2").arg(group_label, unsupported_transform_error)));
        }

        const bool has_detection_work = !detect_inputs.empty();
        const float registration_phase_start = has_detection_work ? kDetectionPhaseEnd : 0.0f;
        RegistrationPhaseLog phase_log(reporter.logger(), has_detection_work ? 2 : 1);
        const std::uint32_t minimum_pairs = static_cast<std::uint32_t>((std::max)(1, group.settings.minStarPairs));

        if (has_detection_work)
        {
            phase_log.begin(QStringLiteral("Star Detection"), QStringLiteral("%1 frame(s)").arg(detect_inputs.size()));
            phase_log.detail(
                QStringLiteral("Detector: the engine's star detection | profile %1 | radius %2 | "
                               "threshold %3 | roundness %4 | max stars %5 | min pairs %6")
                    .arg(group.settings.profileType.trimmed().isEmpty() ? QStringLiteral("Gaussian")
                                                                        : group.settings.profileType.trimmed())
                    .arg(detection_request.searchRadius)
                    .arg(detection_request.noiseSigma, 0, 'f', 2)
                    .arg(detection_request.minimumRoundness, 0, 'f', 2)
                    .arg(detection_request.maximumStars.has_value() ? QString::number(*detection_request.maximumStars)
                                                                    : QStringLiteral("all"))
                    .arg(minimum_pairs));
            phase_log.detail(QStringLiteral("Source frames: %1")
                                 .arg(use_in_memory_calibration
                                          ? QStringLiteral("calibrated in memory during detection")
                                          : QStringLiteral("as supplied (no in-memory calibration)")));
            PhaseProgressLog progress_log(reporter.logger(), QStringLiteral("Detected"),
                                          static_cast<int>(detect_inputs.size()));
            QStringList detection_failures;
            an::Status detected = detect_frames(detect_inputs, use_in_memory_calibration, 0.0f, kDetectionPhaseEnd,
                                                &progress_log, detection_failures);
            if (cancelledStatus(detected))
            {
                return StageResult::cancelled();
            }

            // Masters that leave the reference starless are worked around by detecting on the lights as they are.
            if (use_in_memory_calibration)
            {
                const auto ref_it =
                    detected_by_input.find(pathKey(detect_inputs.front().registrationId.canonicalPath()));
                const std::size_t ref_stars =
                    ref_it != detected_by_input.end() ? ref_it->second.frame.stars.size() : 0u;
                if (ref_stars < minimum_pairs)
                {
                    reporter.logger().warning(QStringLiteral("In-memory calibration produced insufficient stars; "
                                                             "retrying star detection without calibration."));
                    for (const RegistrationInput& input : detect_inputs)
                    {
                        detected_by_input.erase(pathKey(input.registrationId.canonicalPath()));
                    }
                    detection_failures.clear();
                    PhaseProgressLog retry_log(reporter.logger(), QStringLiteral("Detected (retry)"),
                                               static_cast<int>(detect_inputs.size()));
                    detected =
                        detect_frames(detect_inputs, false, 0.0f, kDetectionPhaseEnd, &retry_log, detection_failures);
                    if (cancelledStatus(detected))
                    {
                        return StageResult::cancelled();
                    }
                }
            }
            for (const QString& failure : detection_failures)
            {
                reporter.logger().warning(QStringLiteral("Star detection failed for %1").arg(failure));
            }
            persist_star_cache(detect_inputs);

            StarDetectionSummary summary;
            std::vector<double> fwhm_values;
            std::vector<double> eccentricity_values;
            for (const RegistrationInput& input : detect_inputs)
            {
                const auto it = detected_by_input.find(pathKey(input.registrationId.canonicalPath()));
                if (it == detected_by_input.end())
                {
                    continue;
                }
                const RegisteredFrame& frame = it->second.frame;
                const int stars = static_cast<int>(frame.stars.size());
                const QString frame_name = QFileInfo(input.originalId.toString()).fileName();
                if (summary.frames == 0 || stars < summary.minStars)
                {
                    summary.minStars = stars;
                    summary.minStarsFrame = frame_name;
                }
                if (summary.frames == 0 || stars > summary.maxStars)
                {
                    summary.maxStars = stars;
                    summary.maxStarsFrame = frame_name;
                }
                ++summary.frames;
                summary.totalStars += stars;
                summary.framesWithoutStars += stars == 0 ? 1 : 0;
                summary.framesBelowMinimum += static_cast<std::uint32_t>(stars) < minimum_pairs ? 1 : 0;
                if (std::isfinite(frame.metrics.fwhm) && frame.metrics.fwhm > 0.0)
                {
                    fwhm_values.push_back(frame.metrics.fwhm);
                }
                if (stars > 0 && std::isfinite(frame.metrics.eccentricity))
                {
                    eccentricity_values.push_back(frame.metrics.eccentricity);
                }
            }
            summary.medianFwhm = medianOfSorted(fwhm_values);
            summary.medianEccentricity = medianOfSorted(eccentricity_values);
            if (!fwhm_values.empty())
            {
                summary.minFwhm = fwhm_values.front();
                summary.maxFwhm = fwhm_values.back();
            }
            const qint64 detection_elapsed_ms = phase_log.elapsedMs();
            phase_log.end(
                summary.frames > 0 && detection_elapsed_ms > 0
                    ? QStringLiteral("%1 frame(s), %2 s/frame")
                          .arg(summary.frames)
                          .arg(static_cast<double>(detection_elapsed_ms) / 1000.0 / static_cast<double>(summary.frames),
                               0, 'f', 2)
                    : QString());
            logStarDetectionSummary(phase_log, summary, minimum_pairs);
        }

        // The Gaia catalog's figures for every frame detected now whose own header is solved.
        std::unordered_map<QString, std::vector<an::MatchedSourceReference>> matched_sources_by_frame;
        const bool enrich_with_catalog = group.settings.enrichWithGaiaCatalog;
        const auto enrich_frame = [&](const an::consumer::OperationScope& scope, const RegistrationInput& input,
                                      RegisteredFrame& frame, const std::vector<an::DetectedStar>& stars,
                                      const an::WcsSolution& solution,
                                      const std::optional<double>& epoch) -> an::Status {
            if (!enrich_with_catalog || !catalog_installed || stars.empty())
            {
                return an::Status::success();
            }
            std::vector<RegisteredCatalogMatch> matches;
            an::Result<bool> enriched = an::ui::processes::enrichRegisteredFrame(
                scope.execution(), frame, stars, solution, catalog_directory, epoch, &matches);
            if (!enriched.hasValue())
            {
                return enriched.status();
            }
            std::vector<an::MatchedSourceReference> references = matchedSourceReferences(
                matches, solution, epoch, catalog_directory, frame.metrics.astrometric_rms_pixels);
            if (references.empty())
            {
                matched_sources_by_frame.erase(input.originalId.canonicalPath());
            }
            else
            {
                matched_sources_by_frame[input.originalId.canonicalPath()] = std::move(references);
            }
            return an::Status::success();
        };

        if (auto_reference_runtime)
        {
            std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
            candidates.reserve(inputs.size());
            for (const auto& input : inputs)
            {
                FrameQualityMetrics quality;
                bool has_quality = false;
                if (const auto it = detected_by_input.find(pathKey(input.registrationId.canonicalPath()));
                    it != detected_by_input.end())
                {
                    quality = it->second.frame.metrics;
                    has_quality = true;
                }
                else if (const RegistrationStarCacheEntry* cached = cached_star_detection(input); cached != nullptr)
                {
                    quality.stars = cached->starsCount;
                    quality.fwhm = cached->fwhm;
                    quality.snr = cached->snr;
                    quality.eccentricity = an::ui::registrationEccentricityFromRoundness(cached->roundness);
                    has_quality = true;
                }
                if (!has_quality)
                {
                    continue;
                }
                const auto status_it = metrics_by_key.find(pathKey(input.originalId.canonicalPath()));
                quality.succeeded = status_it == metrics_by_key.end() || status_it->second.succeeded;
                candidates.emplace_back(input.originalId, quality);
            }
            const auto get_filter = [&](const FrameId& id) -> QString {
                const auto input = std::find_if(inputs.begin(), inputs.end(),
                                                [&](const RegistrationInput& in) { return in.originalId == id; });
                const RegistrationFrameFacts* facts = input != inputs.end() ? facts_for(*input) : nullptr;
                return facts == nullptr || facts->filterName.isEmpty() ? QStringLiteral("(unknown)")
                                                                       : facts->filterName;
            };
            const AutoReferenceDecision decision = selectBestReferenceFrameAutoSmart(candidates, get_filter);
            if (decision.best.has_value())
            {
                const FrameId best_ref = decision.best.value();
                const auto move_ref_to_front = [&](std::vector<RegistrationInput>& target) {
                    auto ref_it = std::find_if(target.begin(), target.end(), [&](const RegistrationInput& input) {
                        return input.originalId == best_ref;
                    });
                    if (ref_it == target.end())
                    {
                        const auto src_it =
                            std::find_if(inputs.begin(), inputs.end(),
                                         [&](const RegistrationInput& in) { return in.originalId == best_ref; });
                        if (src_it != inputs.end())
                        {
                            target.insert(target.begin(), *src_it);
                        }
                        return;
                    }
                    if (ref_it != target.begin())
                    {
                        std::iter_swap(target.begin(), ref_it);
                    }
                };
                move_ref_to_front(inputs);
                move_ref_to_front(registration_inputs);
                reporter.logger().operationDetail(QStringLiteral("Reference frame (auto)"), best_ref.toString());
                LOG_INFO("LightsRegistrationStage: auto-selected reference frame (group='{}', frame='{}', mode={})",
                         group_key.toStdString(), best_ref.canonicalPath().toStdString(),
                         static_cast<int>(decision.modeUsed));
            }
            else
            {
                reporter.logger().warning(QStringLiteral("Auto-reference could not pick a suitable frame from detected "
                                                         "star metrics; using existing order."));
            }
        }

        // The registration's frames, from this run's detections and the cache an earlier run left.
        RegisteredFrameSet frame_set;
        for (const RegistrationInput& input : registration_inputs)
        {
            const QString input_path = input.registrationId.canonicalPath();
            RegisteredFrame frame;
            if (const auto it = detected_by_input.find(pathKey(input_path)); it != detected_by_input.end())
            {
                frame = it->second.frame;
            }
            else if (const RegistrationStarCacheEntry* cached = cached_star_detection(input); cached != nullptr)
            {
                QString parse_error;
                frame = registeredFrameFromCache(*cached, parse_error);
                if (!parse_error.isEmpty())
                {
                    LOG_WARNING("LightsRegistrationStage: failed to parse star cache for frame '{}' (group='{}'): {}",
                                input.originalId.canonicalPath().toStdString(), group_key.toStdString(),
                                parse_error.toStdString());
                }
            }
            frame.sourcePath = input_path;
            frame.transform.reset();
            frame.failed = false;
            frame.distortion = stage_utils::SourceDistortionProvenance::explicitNone();
            frame_set.frames.insert_or_assign(input_path, std::move(frame));
        }
        const RegistrationInput reference_input = registration_inputs.front();
        const QString reference_path = reference_input.registrationId.canonicalPath();
        frame_set.referencePath = reference_path;
        const RegistrationFrameFacts* reference_facts = facts_for(reference_input);
        const std::uint32_t reference_width = reference_facts != nullptr ? reference_facts->width : 0u;
        const std::uint32_t reference_height = reference_facts != nullptr ? reference_facts->height : 0u;
        std::unordered_map<QString, QString> engine_failure_by_input;
        std::unordered_set<QString> decided_inputs;

        phase_log.begin(QStringLiteral("Transform Computation"),
                        QStringLiteral("%1 frame(s)").arg(registration_inputs.size()));
        PhaseProgressLog registration_log(reporter.logger(), QStringLiteral("Registered"),
                                          static_cast<int>(registration_inputs.size()));
        const auto report_registration = [&](const std::size_t done, const std::size_t total, const QString& file) {
            registration_log.frameDone(static_cast<int>(done));
            stage_utils::reportStageProgress(reporter, stageId(), group_index, total_groups,
                                             registration_phase_start +
                                                 (1.0f - registration_phase_start) * static_cast<float>(done) /
                                                     static_cast<float>((std::max<std::size_t>)(1u, total)),
                                             QStringLiteral("Registering image %1 of %2").arg(done).arg(total), file);
        };

        bool registration_exception = false;
        QString registration_exception_text;
        const auto match_started = std::chrono::steady_clock::now();
        const an::Status registered = [&]() -> an::Status {
            an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
            if (!scope.hasValue())
            {
                return scope.status();
            }
            const an::ExecutionContext& execution = scope->execution();
            if (registration_method == QStringLiteral("manual_points"))
            {
                RegisteredFrame& reference = frame_set.frames.at(reference_path);
                reference.transform = an::RegistrationTransform{};
                reference.metrics.succeeded = true;
                decided_inputs.insert(reference_path);
                std::size_t done = 1u;
                report_registration(done, registration_inputs.size(), reference_input.originalId.toString());
                for (std::size_t index = 1; index < registration_inputs.size(); ++index)
                {
                    if (const an::Status checked = execution.checkCancellation(); checked.failed())
                    {
                        return checked;
                    }
                    const RegistrationInput& input = registration_inputs[index];
                    const QString input_path = input.registrationId.canonicalPath();
                    RegisteredFrame& frame = frame_set.frames.at(input_path);
                    decided_inputs.insert(input_path);
                    const auto points_it =
                        group.settings.manualControlPointsByFrame.find(input.originalId.canonicalPath());
                    if (points_it == group.settings.manualControlPointsByFrame.end() || points_it->isEmpty())
                    {
                        frame.failed = true;
                        engine_failure_by_input[input_path] = QStringLiteral("No manual control points are defined");
                        reporter.logger().warning(
                            QStringLiteral("No manual control points defined for %1").arg(input.originalId.toString()));
                    }
                    else
                    {
                        std::vector<an::PixelCoordinate> source;
                        std::vector<an::PixelCoordinate> target;
                        for (const auto& point : points_it.value())
                        {
                            source.push_back(
                                an::PixelCoordinate{.x = point.sourcePoint.x(), .y = point.sourcePoint.y()});
                            target.push_back(
                                an::PixelCoordinate{.x = point.referencePoint.x(), .y = point.referencePoint.y()});
                        }
                        const RegistrationFrameFacts* facts = facts_for(input);
                        const an::Result<an::RegistrationFitResult> fitted = an::fitRegistrationTransform(
                            execution, source, target,
                            an::ui::processes::manualRegistrationFit(
                                group.settings, facts != nullptr ? facts->width : reference_width,
                                facts != nullptr ? facts->height : reference_height));
                        if (!fitted.hasValue())
                        {
                            if (cancelledStatus(fitted.status()))
                            {
                                return fitted.status();
                            }
                            frame.failed = true;
                            engine_failure_by_input[input_path] = ImageIntegration::engineFailureText(fitted.status());
                            reporter.logger().warning(
                                QStringLiteral("Manual registration failed for %1: %2")
                                    .arg(input.originalId.toString(), engine_failure_by_input[input_path]));
                        }
                        else
                        {
                            frame.transform = fitted->transform;
                            frame.metrics.stars = static_cast<int>(fitted->correspondenceCount);
                            frame.metrics.astrometric_rms_pixels = fitted->rootMeanSquareResidual;
                            frame.metrics.succeeded = true;
                            reporter.logger().operationDetail(
                                QStringLiteral("Manual fit"),
                                QStringLiteral("%1: %2 points, rms=%3 px%4")
                                    .arg(QFileInfo(input.originalId.canonicalPath()).fileName())
                                    .arg(fitted->correspondenceCount)
                                    .arg(fitted->rootMeanSquareResidual, 0, 'f', 3)
                                    .arg(fitted->polynomialOrder > 0u
                                             ? QStringLiteral(", polynomial order %1 over %2 points")
                                                   .arg(fitted->polynomialOrder)
                                                   .arg(fitted->polynomialInliers)
                                             : QString()));
                        }
                    }
                    report_registration(++done, registration_inputs.size(), input.originalId.toString());
                }
                return an::Status::success();
            }

            an::Result<RegistrationModel> model =
                an::ui::processes::registrationModelOf(effective_registration_settings);
            if (!model.hasValue())
            {
                return model.status();
            }
            if (model->phaseCorrelation)
            {
                // A light is read as the registration reads it - the corrected copy a chromatic aberration pass wrote,
                // already calibrated - and calibrated in memory only when the file read is the light the run calibrates
                // as it reads it, as the alignment reads it.
                const auto read_input = [&](const RegistrationInput& input) -> an::Result<an::Image<float>> {
                    const core::LightCalibration* calibration = nullptr;
                    if (use_in_memory_calibration && input.registrationId == input.originalId)
                    {
                        an::Result<const core::LightCalibration*> prepared = calibration_for(*scope, input.originalId);
                        if (!prepared.hasValue())
                        {
                            return prepared.status();
                        }
                        calibration = *prepared;
                    }
                    return stage_utils::readRegistrationImage(*scope, registry, input.registrationId.canonicalPath(),
                                                              calibration, demosaic);
                };
                an::Result<an::Image<float>> reference_image = read_input(reference_input);
                if (!reference_image.hasValue())
                {
                    return reference_image.status();
                }
                std::size_t done = 0u;
                for (const RegistrationInput& input : registration_inputs)
                {
                    if (const an::Status checked = execution.checkCancellation(); checked.failed())
                    {
                        return checked;
                    }
                    const QString input_path = input.registrationId.canonicalPath();
                    RegisteredFrame& frame = frame_set.frames.at(input_path);
                    decided_inputs.insert(input_path);
                    if (input_path == reference_path)
                    {
                        frame.transform = an::RegistrationTransform{};
                        frame.metrics.succeeded = true;
                    }
                    else
                    {
                        const an::Status measured = [&]() -> an::Status {
                            an::Result<an::Image<float>> image = read_input(input);
                            if (!image.hasValue())
                            {
                                return image.status();
                            }
                            an::Result<an::RegistrationTransform> transform =
                                an::ui::processes::phaseCorrelationTransform(execution, *reference_image, *image);
                            if (!transform.hasValue())
                            {
                                return transform.status();
                            }
                            frame.transform = *transform;
                            frame.metrics.succeeded = true;
                            return an::Status::success();
                        }();
                        if (measured.failed())
                        {
                            if (cancelledStatus(measured))
                            {
                                return measured;
                            }
                            frame.failed = true;
                            engine_failure_by_input[input_path] = ImageIntegration::engineFailureText(measured);
                        }
                    }
                    report_registration(++done, registration_inputs.size(), input.originalId.toString());
                }
                return an::Status::success();
            }

            // Matching by stars: the threshold follows a reference with fewer stars than the minimum, as far down as
            // the fewest pairs a star match is accepted on.
            RegistrationModel star_model = *model;
            const std::size_t reference_stars = frame_set.frames.at(reference_path).stars.size();
            if (reference_stars > 0u && reference_stars < star_model.minimumMatches)
            {
                const std::uint32_t configured = star_model.minimumMatches;
                star_model.minimumMatches = (std::max)(static_cast<std::uint32_t>(reference_stars),
                                                       an::ui::processes::kReliableMinimumStarPairs);
                if (reference_stars < an::ui::processes::kReliableMinimumStarPairs)
                {
                    reporter.logger().warning(
                        QStringLiteral("The reference frame has %1 stars, and a star match needs at least %2 pairs; "
                                       "its lights cannot be matched by their stars.")
                            .arg(reference_stars)
                            .arg(an::ui::processes::kReliableMinimumStarPairs));
                }
                else
                {
                    reporter.logger().warning(QStringLiteral("The reference frame has %1 stars, fewer than the minimum "
                                                             "of %2 star pairs; lowering the minimum to %1 for this "
                                                             "run.")
                                                  .arg(reference_stars)
                                                  .arg(configured));
                }
            }
            std::vector<QString> order;
            order.reserve(frame_set.frames.size());
            for (const auto& [path, frame] : frame_set.frames)
            {
                (void)frame;
                order.push_back(path);
            }
            const an::Status matched = an::ui::processes::matchRegistrationStars(
                execution, frame_set, reference_path, star_model, reference_width, reference_height,
                [&](const std::size_t done, const std::size_t total) {
                    const QString& path = order[(std::min)(done, order.size()) - 1u];
                    decided_inputs.insert(path);
                    report_registration(done, total, path);
                });
            return matched;
        }();
        stage_profile.matchingNanoseconds += elapsedNanoseconds(match_started);

        // What registering a light did differently from its settings - the linear transform a stated polynomial order
        // its pairs could not carry left it - said once for the light, by its own name.
        for (const RegistrationInput& input : registration_inputs)
        {
            const auto frame = frame_set.frames.find(input.registrationId.canonicalPath());
            if (frame != frame_set.frames.end() && !frame->second.failed && !frame->second.registrationNote.isEmpty())
            {
                reporter.logger().warning(QStringLiteral("%1 %2.").arg(
                    QFileInfo(input.originalId.canonicalPath()).fileName(), frame->second.registrationNote));
            }
        }

        if (registered.failed())
        {
            if (cancelledStatus(registered))
            {
                // What finished before the stop is kept below; the rest is left to the next run.
            }
            else
            {
                registration_exception = true;
                registration_exception_text = ImageIntegration::engineFailureText(registered);
                for (auto& [path, frame] : frame_set.frames)
                {
                    if (!frame.transform.has_value())
                    {
                        frame.failed = true;
                        decided_inputs.insert(path);
                    }
                }
            }
        }
        bool cancelled_after_registration = cancelledStatus(registered) || cancelled();

        // A frame the method could not register is registered from its solution, when the reference and it are
        // solved: by their own headers, or by the plate solve when the group allows one.
        std::unordered_map<QString, an::WcsSolution> solved_by_input;
        std::shared_ptr<an::ui::PlateSolveService> plate_solve_service = m_plateSolveService;
        const auto solution_for = [&](const RegistrationInput& input, QString& why) -> std::optional<an::WcsSolution> {
            const QString key = pathKey(input.registrationId.canonicalPath());
            if (const auto known = solved_by_input.find(key); known != solved_by_input.end())
            {
                return known->second;
            }
            const RegistrationFrameFacts* facts = facts_for(input);
            if (facts != nullptr && facts->solution.has_value())
            {
                solved_by_input.emplace(key, *facts->solution);
                return facts->solution;
            }
            if (!allow_external_plate_solve)
            {
                why = (plate_solver_configured
                           ? QStringLiteral("%1 has no world-coordinate solution and solving it is not allowed")
                           : QStringLiteral("%1 has no world-coordinate solution and no plate solver is configured"))
                          .arg(input.originalId.toString());
                return std::nullopt;
            }
            if (!plate_solve_service)
            {
                plate_solve_service = std::make_shared<an::ui::PlateSolveService>();
            }
            an::ui::PlateSolveRequest request;
            request.imagePath = input.registrationId.canonicalPath();
            request.workDirectoryRoot = manifest.outputDirectory;
            request.requestTag = group_key;
            request.allowExternalSolve = true;
            request.focalLengthMm = runtime_plate_solve.focalLengthMm;
            request.pixelSizeUm = runtime_plate_solve.pixelSizeUm;
            if (facts != nullptr)
            {
                request.imageWidth = static_cast<int>(facts->width);
                request.imageHeight = static_cast<int>(facts->height);
            }
            const an::ui::PlateSolveResult result = plate_solve_service->solve(request, runtime_plate_solve, token);
            if (!result.succeeded || !result.solution.has_value())
            {
                why = result.errorMessage.isEmpty()
                          ? QStringLiteral("%1 could not be plate solved").arg(input.originalId.toString())
                          : result.errorMessage;
                return std::nullopt;
            }
            solved_by_input.emplace(key, *result.solution);
            return result.solution;
        };

        const auto apply_astrometric_fallback = [&](const std::vector<RegistrationInput>& targets,
                                                    const QString& reason, QString& failure_detail) -> bool {
            if (targets.empty())
            {
                return false;
            }
            QString why;
            const std::optional<an::WcsSolution> reference_solution = solution_for(reference_input, why);
            if (!reference_solution.has_value())
            {
                failure_detail = why;
                return false;
            }
            an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
            if (!scope.hasValue())
            {
                failure_detail = ImageIntegration::engineFailureText(scope.status());
                return false;
            }
            int recovered = 0;
            QStringList failures;
            for (const RegistrationInput& input : targets)
            {
                if (cancelled())
                {
                    break;
                }
                const QString input_path = input.registrationId.canonicalPath();
                auto frame_it = frame_set.frames.find(input_path);
                if (frame_it == frame_set.frames.end())
                {
                    continue;
                }
                RegisteredFrame& frame = frame_it->second;
                if (input_path == reference_path)
                {
                    frame.transform = an::RegistrationTransform{};
                    frame.failed = false;
                    frame.metrics.succeeded = true;
                    decided_inputs.insert(input_path);
                    ++recovered;
                    continue;
                }
                const std::optional<an::WcsSolution> solution = solution_for(input, why);
                if (!solution.has_value())
                {
                    failures << why;
                    continue;
                }
                const RegistrationFrameFacts* facts = facts_for(input);
                const std::uint32_t width = facts != nullptr ? facts->width : solution->imageWidth();
                const std::uint32_t height = facts != nullptr ? facts->height : solution->imageHeight();
                const an::Result<AstrometricRegistration> mapped =
                    astrometricRegistration(scope->execution(), *solution, *reference_solution, width, height);
                if (!mapped.hasValue())
                {
                    failures << QStringLiteral("%1 (%2)").arg(input.originalId.toString(),
                                                              ImageIntegration::engineFailureText(mapped.status()));
                    continue;
                }
                frame.transform = mapped->transform;
                frame.failed = false;
                frame.metrics.succeeded = true;
                frame.metrics.astrometric_rms_pixels = mapped->residualPixels;
                frame.distortion = stage_utils::SourceDistortionProvenance::explicitNone();
                decided_inputs.insert(input_path);
                update_metric_frames.insert(input.originalId.canonicalPath());
                ++recovered;
            }
            if (recovered > 0)
            {
                reporter.logger().warning(QStringLiteral("Recovered %1 frame(s) via astrometric registration (%2).")
                                              .arg(recovered)
                                              .arg(reason));
                LOG_INFO("LightsRegistrationStage: astrometric fallback recovered {} frame(s) for group '{}' ({})",
                         recovered, group_key.toStdString(), reason.toStdString());
            }
            failure_detail = failures.join(QStringLiteral(" | "));
            return recovered > 0;
        };

        if (registration_exception)
        {
            const QString failure_reason = registration_method == QStringLiteral("manual_points")
                                               ? QStringLiteral("manual registration failure")
                                               : QStringLiteral("star registration failure");
            QString fallback_error;
            if (!allow_astrometric_fallback ||
                !apply_astrometric_fallback(registration_inputs, failure_reason, fallback_error))
            {
                reporter.logger().error(registration_method == QStringLiteral("manual_points")
                                            ? QStringLiteral("Manual registration failed")
                                            : QStringLiteral("Star registration failed"),
                                        registration_exception_text);
                if (allow_astrometric_fallback && !fallback_error.isEmpty())
                {
                    reporter.logger().warning(QStringLiteral("Astrometric fallback failed: %1").arg(fallback_error));
                }
                error_report.addError(IntegrationError::registrationFailed(
                    QStringLiteral("Group '%1': %2").arg(group_label, registration_exception_text)));
                finish_group();
                continue;
            }
        }
        else if (!cancelled_after_registration && allow_astrometric_fallback)
        {
            std::vector<RegistrationInput> failed_inputs;
            for (const RegistrationInput& input : registration_inputs)
            {
                const auto it = frame_set.frames.find(input.registrationId.canonicalPath());
                const bool missing_stars =
                    it != frame_set.frames.end() && method_uses_star_detection && it->second.stars.empty();
                if (it == frame_set.frames.end() || it->second.failed || !it->second.transform.has_value() ||
                    missing_stars)
                {
                    failed_inputs.push_back(input);
                }
            }
            if (!failed_inputs.empty())
            {
                QString fallback_error;
                (void)apply_astrometric_fallback(
                    failed_inputs,
                    method_uses_star_detection
                        ? QStringLiteral("per-frame registration failures or missing star detections")
                        : QStringLiteral("per-frame registration failures"),
                    fallback_error);
                if (!fallback_error.isEmpty())
                {
                    reporter.logger().warning(
                        QStringLiteral("Astrometric fallback incomplete: %1").arg(fallback_error));
                }
            }
        }

        // The catalog's figures for the frames detected now, through their own or their solved solution.
        const auto enrichment_solution = [&](const RegistrationInput& input) -> std::optional<an::WcsSolution> {
            const QString key = pathKey(input.registrationId.canonicalPath());
            if (!detected_by_input.contains(key) || !frame_set.frames.contains(input.registrationId.canonicalPath()))
            {
                return std::nullopt;
            }
            if (const auto solved = solved_by_input.find(key); solved != solved_by_input.end())
            {
                return solved->second;
            }
            const RegistrationFrameFacts* facts = facts_for(input);
            return facts != nullptr ? facts->solution : std::nullopt;
        };
        if (!cancelled_after_registration && enrich_with_catalog && catalog_installed)
        {
            an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
            if (scope.hasValue())
            {
                for (const RegistrationInput& input : registration_inputs)
                {
                    const std::optional<an::WcsSolution> solution = enrichment_solution(input);
                    if (!solution.has_value())
                    {
                        continue;
                    }
                    const auto detected = detected_by_input.find(pathKey(input.registrationId.canonicalPath()));
                    auto frame_it = frame_set.frames.find(input.registrationId.canonicalPath());
                    const RegistrationFrameFacts* facts = facts_for(input);
                    const an::Status enriched =
                        enrich_frame(*scope, input, frame_it->second, detected->second.stars, *solution,
                                     facts != nullptr ? facts->observationEpochYear : std::nullopt);
                    if (cancelledStatus(enriched))
                    {
                        cancelled_after_registration = true;
                        break;
                    }
                    if (enriched.failed())
                    {
                        reporter.logger().warning(
                            QStringLiteral("Gaia enrichment failed for %1: %2")
                                .arg(input.originalId.toString(), ImageIntegration::engineFailureText(enriched)));
                    }
                }
            }
        }
        else if (!cancelled_after_registration && enrich_with_catalog)
        {
            // Without the catalog nothing is enriched: a group with a light the catalog would have enriched says so.
            const auto unenriched =
                std::count_if(registration_inputs.begin(), registration_inputs.end(),
                              [&](const RegistrationInput& input) { return enrichment_solution(input).has_value(); });
            if (unenriched > 0)
            {
                reporter.logger().warning(
                    QStringLiteral("Gaia enrichment is on, but %1: %2 solved light(s) were registered without the "
                                   "catalog's photometric figures. Install a Gaia catalog in Settings > Plate Solving, "
                                   "or turn Enrich with Gaia catalog off in Registration Settings.")
                        .arg(catalog_directory.isEmpty()
                                 ? QStringLiteral("no Gaia catalog is configured")
                                 : QStringLiteral("no Gaia catalog is installed at '%1'").arg(catalog_directory),
                             QString::number(unenriched)));
            }
        }
        // A residual in arcseconds for every frame whose solution is known and was not enriched.
        for (const RegistrationInput& input : registration_inputs)
        {
            auto frame_it = frame_set.frames.find(input.registrationId.canonicalPath());
            if (frame_it == frame_set.frames.end() || frame_it->second.metrics.astrometric_rms_arcsec > 0.0 ||
                !(frame_it->second.metrics.astrometric_rms_pixels > 0.0))
            {
                continue;
            }
            const auto solved = solved_by_input.find(pathKey(input.registrationId.canonicalPath()));
            const RegistrationFrameFacts* facts = facts_for(input);
            const an::WcsSolution* solution = solved != solved_by_input.end()                   ? &solved->second
                                              : facts != nullptr && facts->solution.has_value() ? &*facts->solution
                                                                                                : nullptr;
            if (solution != nullptr)
            {
                frame_it->second.metrics.astrometric_rms_arcsec =
                    frame_it->second.metrics.astrometric_rms_pixels * solution->meanPixelScaleArcsecPerPixel();
            }
        }

        cancelled_after_registration = cancelled_after_registration || cancelled();
        if (cancelled_after_registration)
        {
            reporter.logger().operationDetail(QStringLiteral("Cancellation"),
                                              QStringLiteral("Preserving completed registration metrics"));
        }

        int failed_frames = 0;
        QStringList failed_samples;
        std::vector<RegistrationFailureDiagnostic> failed_diagnostics;
        std::vector<RegistrationMetricRecord> metric_updates;
        metric_updates.reserve(registration_inputs.size());
        std::vector<RegistrationMatchedSourceReplacement> matched_source_replacements;
        matched_source_replacements.reserve(registration_inputs.size());
        const std::uint32_t effective_minimum_stars = minimum_pairs;

        for (const RegistrationInput& input : registration_inputs)
        {
            const QString input_path = input.registrationId.canonicalPath();
            const auto frame_it = frame_set.frames.find(input_path);
            const bool has_frame = frame_it != frame_set.frames.end();
            const bool succeeded = has_frame && !frame_it->second.failed && frame_it->second.transform.has_value();
            const bool metric_update_requested = update_metric_frames.contains(input.originalId.canonicalPath());
            if (!metric_update_requested && succeeded)
            {
                continue;
            }
            if (cancelled_after_registration && input_path != reference_path && !decided_inputs.contains(input_path))
            {
                continue;
            }

            RegistrationMetricRecord metric;
            metric.frameId = input.originalId;
            metric.groupKey = group_key;
            metric.settingsVersion = settings_version;
            if (has_frame)
            {
                const RegisteredFrame& frame = frame_it->second;
                metric.metrics = frame.metrics;
                metric.metrics.stars = static_cast<int>(frame.stars.empty() ? frame.metrics.stars : frame.stars.size());
                metric.noise = frame.noise;
                if (succeeded)
                {
                    metric.transformJson = stage_utils::serializeTransformJson(*frame.transform, frame.distortion);
                }
            }
            metric.metrics.succeeded = succeeded;
            if (!succeeded)
            {
                ++failed_frames;
                const auto engine_reason = engine_failure_by_input.find(input_path);
                const QString failure_reason = registrationFailureReason(
                    has_frame ? &frame_it->second : nullptr, method_uses_star_detection, effective_minimum_stars,
                    engine_reason != engine_failure_by_input.end() ? engine_reason->second : QString());
                failed_diagnostics.push_back({input.originalId.canonicalPath(), failure_reason});
                if (failed_samples.size() < 5)
                {
                    failed_samples << QStringLiteral("%1 (%2)").arg(input.originalId.canonicalPath(), failure_reason);
                }
            }
            metric_updates.push_back(std::move(metric));
            if (has_frame)
            {
                const auto matched_it = matched_sources_by_frame.find(input.originalId.canonicalPath());
                matched_source_replacements.push_back({input.originalId,
                                                       matched_it != matched_sources_by_frame.end()
                                                           ? matched_it->second
                                                           : std::vector<an::MatchedSourceReference>{},
                                                       0});
            }
        }
        const int total_frames = static_cast<int>(registration_inputs.size());
        phase_log.end(QStringLiteral("%1 registered, %2 failed").arg(total_frames - failed_frames).arg(failed_frames));

        QString metric_batch_error;
        const bool metric_batch_persisted =
            metrics_writer.upsertMetricsForGroup(group_key, settings_version, metric_updates, &metric_batch_error);
        if (!metric_batch_persisted)
        {
            reporter.logger().error(QStringLiteral("Failed to persist registration metrics"), metric_batch_error);
            error_report.addError(IntegrationError(
                IntegrationErrorCode::FileNotWritable, QStringLiteral("Failed to persist registration metrics"),
                metrics_path, QStringLiteral("Group: %1; %2").arg(group_key, metric_batch_error)));
        }
        QString matched_sources_batch_error;
        const bool matched_sources_batch_persisted = metrics_writer.replaceMatchedSourcesForGroup(
            group_key, settings_version, matched_source_replacements, &matched_sources_batch_error);
        if (!matched_sources_batch_persisted)
        {
            reporter.logger().warning(QStringLiteral("Failed to persist matched Gaia source-reference batch for %1: %2")
                                          .arg(group_label, matched_sources_batch_error));
        }

        // The reference the transforms are onto, which the alignment stage aligns onto whether it was configured or
        // chosen during the run.
        const QString final_reference_frame_id = reference_input.originalId.canonicalPath();
        if (cancelled_after_registration)
        {
            if (metric_batch_persisted && matched_sources_batch_persisted)
            {
                RegistrationGroupState resumable_group_state;
                resumable_group_state.groupKey = group_key;
                resumable_group_state.settingsVersion = settings_version;
                resumable_group_state.detectionHash = detection_hash;
                resumable_group_state.registrationHash = registration_hash;
                resumable_group_state.referenceFrameId =
                    auto_reference_runtime ? final_reference_frame_id : ref_frame_id;
                resumable_group_state.lightFilesHash = light_files_hash;
                resumable_group_state.calibrationHash = calibration_hash;
                resumable_group_state.outputWcsJson = output_wcs_json;
                if (!metrics_writer.upsertGroupState(resumable_group_state))
                {
                    reporter.logger().warning(
                        QStringLiteral("Failed to persist resumable registration state for group '%1'").arg(group_key));
                }
            }
            reporter.logger().operationDetail(
                QStringLiteral("Registration metrics"),
                QStringLiteral("Persisted %1 completed frame(s) before cancellation").arg(metric_updates.size()));
            finish_group();
            return StageResult::cancelled();
        }

        // The plane the group is aligned onto, planned from every frame's transform - the ones registered now and the
        // ones an earlier run left - and the reference's solution moved onto it.
        output_wcs_json.clear();
        {
            std::unordered_map<QString, std::pair<an::RegistrationTransform, stage_utils::SourceDistortionProvenance>>
                stored_transforms;
            for (const auto& [frame_id, status] : metrics_by_frame)
            {
                if (!status.succeeded || status.transformJson.trimmed().isEmpty())
                {
                    continue;
                }
                an::RegistrationTransform transform;
                stage_utils::SourceDistortionProvenance provenance;
                QString parse_error;
                if (stage_utils::parseRegistrationTransformJson(status.transformJson, transform, &provenance,
                                                                parse_error))
                {
                    stored_transforms.insert_or_assign(pathKey(frame_id), std::pair{transform, provenance});
                }
            }
            std::vector<stage_utils::RegistrationOutputPlaneFrame> plane_frames;
            std::size_t plane_reference_index = 0u;
            bool reference_present = false;
            for (const RegistrationInput& input : inputs)
            {
                const RegistrationFrameFacts* facts = facts_for(input);
                if (facts == nullptr)
                {
                    continue;
                }
                std::optional<std::pair<an::RegistrationTransform, stage_utils::SourceDistortionProvenance>> entry;
                if (const auto frame_it = frame_set.frames.find(input.registrationId.canonicalPath());
                    frame_it != frame_set.frames.end())
                {
                    if (!frame_it->second.failed && frame_it->second.transform.has_value())
                    {
                        entry = std::pair{*frame_it->second.transform, frame_it->second.distortion};
                    }
                }
                else if (const auto stored = stored_transforms.find(pathKey(input.originalId.canonicalPath()));
                         stored != stored_transforms.end())
                {
                    entry = stored->second;
                }
                if (!entry.has_value())
                {
                    continue;
                }
                stage_utils::RegistrationOutputPlaneFrame plane_frame;
                plane_frame.transform = entry->first;
                plane_frame.width = facts->width;
                plane_frame.height = facts->height;
                if (entry->second.policy == stage_utils::SourceDistortionPolicy::ExplicitModel)
                {
                    plane_frame.distortion = entry->second.model;
                }
                else if (entry->second.policy == stage_utils::SourceDistortionPolicy::InheritMetadata)
                {
                    plane_frame.distortion = facts->distortion;
                }
                if (input.originalId == reference_input.originalId)
                {
                    plane_reference_index = plane_frames.size();
                    reference_present = true;
                }
                plane_frames.push_back(std::move(plane_frame));
            }
            QString why;
            const std::optional<an::WcsSolution> reference_solution =
                reference_facts != nullptr && reference_facts->solution.has_value()
                    ? reference_facts->solution
                    : (solved_by_input.contains(pathKey(reference_path))
                           ? std::optional<an::WcsSolution>(solved_by_input.at(pathKey(reference_path)))
                           : std::nullopt);
            if (!reference_present || !reference_solution.has_value())
            {
                reporter.logger().operationDetail(QStringLiteral("Output-plane WCS"),
                                                  QStringLiteral("Unavailable: the reference frame is not solved"));
            }
            else
            {
                // The plane every route plans from these transforms - the standard canvas in place of an intersection
                // no area is common to - recorded under the mode it was planned by.
                const an::Result<stage_utils::PlannedRegistrationOutputPlane> canvas =
                    stage_utils::planRegistrationOutputPlane(plane_frames, plane_reference_index, canvas_mode);
                const an::Result<an::WcsSolution> plane =
                    canvas.hasValue() ? stage_utils::registrationOutputPlaneSolution(
                                            *reference_solution, canvas->canvas,
                                            plane_frames[plane_reference_index].distortion.has_value())
                                      : an::Result<an::WcsSolution>(canvas.status());
                const an::Result<std::vector<an::CodecCard>> cards =
                    plane.hasValue() ? an::buildWcsCards(*plane)
                                     : an::Result<std::vector<an::CodecCard>>(plane.status());
                if (!cards.hasValue())
                {
                    reporter.logger().operationDetail(
                        QStringLiteral("Output-plane WCS"),
                        QStringLiteral("Unavailable: %1").arg(ImageIntegration::engineFailureText(cards.status())));
                }
                else
                {
                    stage_utils::OutputPlaneWcsDescriptorInfo info;
                    info.sourceKind = QStringLiteral("reference_frame");
                    info.sourceFrameId = final_reference_frame_id;
                    info.alignmentMode = stage_utils::alignmentModeKey(canvas->usedMode);
                    info.width = static_cast<int>(canvas->canvas.width);
                    info.height = static_cast<int>(canvas->canvas.height);
                    info.sipCorrectionApplied = std::any_of(plane_frames.begin(), plane_frames.end(),
                                                            [](const stage_utils::RegistrationOutputPlaneFrame& frame) {
                                                                return frame.distortion.has_value();
                                                            });
                    output_wcs_json = stage_utils::serializeOutputPlaneWcsJson(*cards, info);
                }
            }
        }

        RegistrationGroupState final_group_state;
        final_group_state.groupKey = group_key;
        final_group_state.settingsVersion = settings_version;
        final_group_state.detectionHash = detection_hash;
        final_group_state.registrationHash = registration_hash;
        final_group_state.referenceFrameId = auto_reference_runtime ? final_reference_frame_id : ref_frame_id;
        final_group_state.lightFilesHash = light_files_hash;
        final_group_state.calibrationHash = calibration_hash;
        final_group_state.outputWcsJson = output_wcs_json;
        if (!metric_batch_persisted || !matched_sources_batch_persisted)
        {
            reporter.logger().warning(
                QStringLiteral("Registration group state was not advanced for group '%1' because its frame batch "
                               "was not fully persisted")
                    .arg(group_key));
        }
        else if (!metrics_writer.upsertGroupState(final_group_state))
        {
            reporter.logger().warning(
                QStringLiteral("Failed to persist final registration group state for group '%1'").arg(group_key));
        }

        if (failed_frames > 0)
        {
            reporter.logger().error(QStringLiteral("%1 frame(s) failed registration").arg(failed_frames),
                                    QStringLiteral("See the per-frame registration errors below."));
            for (const auto& diagnostic : failed_diagnostics)
            {
                reporter.logger().error(QStringLiteral("Registration failed for %1").arg(diagnostic.framePath),
                                        diagnostic.reason);
            }
            LOG_WARNING("LightsRegistrationStage: {} frame(s) failed registration in group '{}' (samples: {})",
                        failed_frames, group_key.toStdString(),
                        failed_samples.join(QStringLiteral("; ")).toStdString());
            for (const auto& sample : failed_samples)
            {
                error_report.addError(
                    IntegrationError::registrationFailed(QStringLiteral("Frame registration failed: %1").arg(sample)));
            }
            if (failed_frames > failed_samples.size())
            {
                error_report.addWarning(QStringLiteral("... and %1 more frame(s) failed registration in group '%2'")
                                            .arg(failed_frames - failed_samples.size())
                                            .arg(group_label));
            }
        }
        finish_group();
    }

    if (const QString profile_line = formatRegistrationProfile(stage_profile); !profile_line.isEmpty())
    {
        reporter.logger().operationDetail(QStringLiteral("Registration profile"), profile_line);
        LOG_INFO("LightsRegistrationStage: {}", profile_line.toStdString());
    }
    // What the calibrations of the lights read in memory fell back to, once per calibration.
    QStringList calibration_warnings;
    stage_utils::sayStageWarnings(reporter, masters.fallbackWarnings(), calibration_warnings);
    reporter.logger().stageCompleted(name(), total_groups, stage_timer.elapsed());

    if (!error_report.errors.empty())
    {
        LOG_WARNING("LightsRegistrationStage: {} group(s)/frame(s) had registration errors",
                    error_report.errors.size());
        auto result = StageResult::partialSuccess(error_report);
        result.warnings.append(calibration_warnings);
        result.alignmentGroupResults = std::move(ca_group_results);
        return result;
    }
    StageResult result = StageResult::success();
    result.warnings = calibration_warnings;
    result.alignmentGroupResults = std::move(ca_group_results);
    return result;
}

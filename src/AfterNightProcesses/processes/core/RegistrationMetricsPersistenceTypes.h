// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file RegistrationMetricsPersistenceTypes.h
 * @brief Shared persistence types for registration metrics SQLite storage.
 *
 * This header centralizes the data contracts used by both the registration
 * metrics writer and the state loader. Keeping these types shared makes it
 * easier for standalone processes and ImageIntegration to read and write the
 * same `registration_metrics.db` schema without duplicating assumptions.
 *
 * Every position a record holds is an engine pixel coordinate - the first pixel's centre at (0.5, 0.5) - and every
 * transform an `an::RegistrationTransform` in that convention, stored as the workflow's schema 5 transform record.
 */
#pragma once

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QPointF>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace an::ui
{
/**
 * @brief Lightweight status view of a persisted registration metric row.
 */
struct RegistrationMetricStatus
{
    bool succeeded = false;
    QString outputPath;
    qint64 outputSizeBytes = -1;
    qint64 outputModifiedAtMs = -1;
    QString outputIdentityToken;
    QString transformJson;
    qint64 updatedAt = 0;
};

/**
 * @brief Persisted registration group state row.
 */
struct RegistrationGroupState
{
    QString groupKey;
    int settingsVersion = 0;
    QString detectionHash;
    QString registrationHash;
    QString referenceFrameId;
    QString lightFilesHash;
    QString calibrationHash;
    QString outputWcsJson;
    qint64 updatedAt = 0;
};

/**
 * @brief Persisted star-detection cache row.
 */
struct RegistrationStarCacheEntry
{
    FrameId frameId;
    QString groupKey;
    QString detectionHash;
    QString inputPath;
    qint64 inputMtime = 0;
    qint64 inputSizeBytes = -1;
    qint64 inputModifiedAtMs = -1;
    QString inputIdentityToken;
    int starsCount = 0;
    double fwhm = 0.0;
    double snr = 0.0;
    double roundness = 0.0;
    /// Frame-level noise statistics. They are produced by star detection but are not recoverable
    /// from starsBlob (which only carries per-star geometry), so they have to be cached here or a
    /// cache hit would silently drop them from the registration metrics that weighting reads.
    FrameNoiseStatistics noise;
    QByteArray starsBlob;
    qint64 updatedAt = 0;
};

/**
 * @brief Persisted registration metric row.
 */
struct RegistrationMetricRecord
{
    FrameId frameId;
    QString groupKey;
    int settingsVersion = 0;
    FrameQualityMetrics metrics;
    FrameNoiseStatistics noise; ///< Weighting inputs; see FrameNoiseStatistics for why they live here
    QString outputPath;
    qint64 outputSizeBytes = -1;
    qint64 outputModifiedAtMs = -1;
    QString outputIdentityToken;
    QString transformJson;
    qint64 updatedAt = 0;
};

/// The lookup status a matched source is recorded with before a calibration has decided it.
inline constexpr char kMatchedSourceUntested[] = "untested";

/**
 * @brief Persisted matched Gaia source reference for SPCC handoff reuse.
 *
 * The reference is the engine's record of the match; the lookup status is the text the database's column holds, which a
 * registration writes as "untested" and a spectrophotometric calibration writes its verdict into.
 */
struct RegistrationMatchedSourceRecord
{
    FrameId frameId;
    QString groupKey;
    int settingsVersion = 0;
    an::MatchedSourceReference reference;
    QString lookupStatus = QString::fromLatin1(kMatchedSourceUntested);
    qint64 updatedAt = 0;
};

/**
 * @brief Complete matched-source replacement for one frame in a group batch.
 */
struct RegistrationMatchedSourceReplacement
{
    FrameId frameId;
    std::vector<an::MatchedSourceReference> references;
    qint64 updatedAt = 0;
};

/**
 * @brief Aligned output path update for one persisted registration metric row.
 */
struct RegistrationAlignedOutputPathUpdate
{
    FrameId frameId;
    QString outputPath;
    qint64 outputSizeBytes = -1;
    qint64 outputModifiedAtMs = -1;
    QString outputIdentityToken;
};

/**
 * @brief Narrow diagnostics for aligned-output canonical index tests.
 */
struct RegistrationAlignedOutputPathBatchDiagnostics
{
    size_t groupRowScans = 0;
    size_t storedRowsIndexed = 0;
    size_t canonicalLookups = 0;
    size_t resolvedUpdates = 0;
    size_t canonicalCollisions = 0;
};

/**
 * @brief Persisted comet tracking run keyed by exact comet registration context.
 */
struct CometTrackingRunRecord
{
    QString cometRunId;
    QString groupKey;
    int settingsVersion = 0;
    QString cometContextId;
    QString detectionHash;
    QString registrationHash;
    QString lightFilesHash;
    QString calibrationHash;
    QString resolvedReferenceFrameId;
    double referenceEpoch = 0.0;
    QString mode;
    QString status;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct CometTrackingRunSummaryRecord
{
    QString cometRunId;
    QString fitModel;
    double referenceEpoch = 0.0;
    double fitTimeOrigin = 0.0;
    double rmsPixels = 0.0;
    double maxResidualPixels = 0.0;
    int acceptedInputCount = 0;
    int rejectedInputCount = 0;
    int predictedOnlyFrameCount = 0;
    QString warningFlags;
    qint64 updatedAt = 0;
};

struct CometTrackingPointRecord
{
    QString pointId;
    QString cometRunId;
    FrameId originalFrameId;
    QString refinementSourcePath;
    double epoch = 0.0;
    QPointF sourcePosition;
    QPointF transformDomainPosition;
    QPointF predictedSourcePosition;
    QPointF referencePlanePosition;
    QPointF predictedReferencePlanePosition;
    double confidence = 0.0;
    double residualPixels = 0.0;
    double measuredFwhmPixels = 0.0;
    double measuredDiameterPixels = 0.0;
    double measuredEllipticity = 0.0;
    double apertureRadiusPixels = 0.0;
    double localBackground = 0.0;
    double snr = 0.0;
    double roiX = 0.0;
    double roiY = 0.0;
    double roiWidth = 0.0;
    double roiHeight = 0.0;
    QString measurementMethod;
    QString origin;
    QString fitState;
    QString rejectionReason;
    qint64 updatedAt = 0;
};

struct CometTransformRoleRecord
{
    QString cometRunId;
    FrameId frameId;
    QString role;
    QString cometOutputPlaneId;
    QString transformJson;
    QString sourceDistortionJson;
    bool succeeded = false;
    QString failureReason;
    qint64 updatedAt = 0;
};

struct CometOutputPlaneRecord
{
    QString cometOutputPlaneId;
    QString cometRunId;
    QString role;
    QString alignmentMode;
    QString coordinateFrameKind;
    int canvasWidth = 0;
    int canvasHeight = 0;
    double canvasOffsetX = 0.0;
    double canvasOffsetY = 0.0;
    bool skyWcsValid = false;
    QString skyWcsInvalidReason;
    QString descriptorJson;
    QString sourceFingerprint;
    qint64 updatedAt = 0;
};

struct CometRegisteredOutputRecord
{
    QString cometRunId;
    FrameId frameId;
    QString role;
    QString cometOutputPlaneId;
    QString outputPath;
    QString artifactHash;
    qint64 outputSizeBytes = -1;
    qint64 outputModifiedAtMs = -1;
    QString outputIdentityToken;
    bool succeeded = false;
    QString failureReason;
    qint64 updatedAt = 0;
};

struct CometRoleReadinessRecord
{
    QString cometRunId;
    QString role;
    QString readinessStatus;
    int selectedFrameCount = 0;
    int successfulTransformCount = 0;
    int failedTransformCount = 0;
    int missingTransformCount = 0;
    QString outputPlaneStatus;
    bool requiresMaterializedOutputs = false;
    int materializedOutputCount = 0;
    int missingOutputCount = 0;
    int failedOutputCount = 0;
    QString failureReason;
    qint64 updatedAt = 0;
};

struct CometTransformRoleProduct
{
    CometRoleReadinessRecord readiness;
    CometOutputPlaneRecord outputPlane;
    std::vector<CometTransformRoleRecord> transforms;
};

struct StackingCometArtifactRecord
{
    QString cometRunId;
    QString integrationGroupKey;
    QString stackingHash;
    QString starBaseOutputDescriptorId;
    QString cometOutputPlaneId;
    QString artifactKind;
    QString outputPath;
    QString artifactHash;
    QString maskSettingsFingerprint;
    bool succeeded = false;
    QString failureReason;
    qint64 updatedAt = 0;
};

/**
 * @brief Enumerated group/version pair available in a metrics database.
 */
struct RegistrationMetricsGroupVersion
{
    QString groupKey;
    int settingsVersion = 0;
    qint64 updatedAt = 0;
};

/**
 * @brief One detected star a registration keeps for its frame, at its engine pixel coordinate.
 */
struct RegisteredStar
{
    double x = 0.0;
    double y = 0.0;

    /// The star's full width at half maximum, in pixels.
    double fwhm = 0.0;

    /// The ratio of the star's minor width to its major one, from zero to one.
    double roundness = 0.0;

    friend bool operator==(const RegisteredStar& left, const RegisteredStar& right) = default;
};

/**
 * @brief What a registration recorded about one frame: its stars, its figures and its transform onto the reference.
 */
struct RegisteredFrame
{
    /// The file the frame was registered from.
    QString sourcePath;

    /// The stars detected on it.
    std::vector<RegisteredStar> stars;

    /// Its figures: star count, width, signal to noise, eccentricity and the catalog's.
    FrameQualityMetrics metrics;

    /// Its noise figures, which weighting reads.
    FrameNoiseStatistics noise;

    /// The quality score the reference is chosen by.
    double quality = 0.0;

    /// The transform from the frame onto the reference, absent when the frame was not registered.
    std::optional<an::RegistrationTransform> transform;

    /// The source distortion the transform was solved over.
    stage_utils::SourceDistortionProvenance distortion;

    /// Whether the registration of the frame failed.
    bool failed = false;

    /// What registering the frame did differently from its settings, worded to follow the frame's name - "matched 6
    /// star pairs, fewer than the 15 a polynomial of order 4 needs, ..." - for whoever reports the run to say; empty
    /// when it did what they state. Not persisted.
    QString registrationNote;
};

/**
 * @brief The frames of one registration, keyed by the file each was registered from, and the reference.
 */
struct RegisteredFrameSet
{
    std::map<QString, RegisteredFrame> frames;

    /// The reference frame's file.
    QString referencePath;
};

/**
 * @brief Reconstructed persisted registration state for one group/version.
 */
struct RegistrationMetricsLoadResult
{
    std::optional<RegistrationGroupState> groupState;
    std::vector<RegistrationMetricRecord> frameMetrics;
    std::vector<RegistrationMatchedSourceRecord> matchedSources;
    std::unordered_map<QString, RegistrationStarCacheEntry> starCacheByFrame;
    RegisteredFrameSet registeredFrames;
};

[[nodiscard]] inline bool isCometOutputPlaneSiderealWcsUsable(const CometOutputPlaneRecord& plane) noexcept
{
    return plane.skyWcsValid && plane.role != QStringLiteral("comet_aligned");
}

/**
 * @brief Compute eccentricity from a roundness value in [0, 1].
 *
 * Uses the convention: eccentricity = 1 - roundness, clamped to [0, 1].
 * Shared between the standalone bridge and IIP registration stages so both
 * write identical eccentricity values to registration_metrics.db.
 */
[[nodiscard]] inline double registrationEccentricityFromRoundness(double roundness) noexcept
{
    return 1.0 - std::clamp(roundness, 0.0, 1.0);
}

inline constexpr quint32 kRegistrationStarCacheBlobMagic = 0x414E5352; // 'ANSR'

/// The star cache blob's version: three holds engine pixel coordinates. A blob of another version is not read - a
/// star cache is found by its detection hash, which carries the star detections' family identity, so a blob of
/// another version is never asked for.
inline constexpr quint16 kRegistrationStarCacheBlobVersion = 3;

/**
 * @brief Serialize detected stars into the SQLite cache blob format.
 */
[[nodiscard]] inline QByteArray serializeRegistrationStarCacheBlob(const std::vector<RegisteredStar>& stars)
{
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_5);
    stream << kRegistrationStarCacheBlobMagic;
    stream << kRegistrationStarCacheBlobVersion;
    stream << static_cast<quint32>(stars.size());
    for (const RegisteredStar& star : stars)
    {
        stream << star.x << star.y << star.fwhm << star.roundness;
    }
    return out;
}

/**
 * @brief Deserialize detected stars from the SQLite cache blob format; nothing, with the reason, for a blob of another
 * version, a truncated one or one that is not a star cache.
 */
[[nodiscard]] inline std::optional<std::vector<RegisteredStar>>
deserializeRegistrationStarCacheBlob(const QByteArray& blob, QString& error)
{
    error.clear();

    QDataStream stream(blob);
    stream.setVersion(QDataStream::Qt_6_5);

    quint32 magic = 0;
    quint16 version = 0;
    stream >> magic;
    stream >> version;
    if (stream.status() != QDataStream::Ok || magic != kRegistrationStarCacheBlobMagic)
    {
        error = QStringLiteral("Invalid star cache blob.");
        return std::nullopt;
    }
    if (version != kRegistrationStarCacheBlobVersion)
    {
        error = QStringLiteral("Star cache blob version %1 is not read; this build reads version %2.")
                    .arg(version)
                    .arg(kRegistrationStarCacheBlobVersion);
        return std::nullopt;
    }

    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok)
    {
        error = QStringLiteral("Invalid star cache blob.");
        return std::nullopt;
    }

    std::vector<RegisteredStar> out;
    out.reserve((std::min)(count, quint32{1u << 20}));
    for (quint32 index = 0; index < count; ++index)
    {
        RegisteredStar star;
        stream >> star.x >> star.y >> star.fwhm >> star.roundness;
        if (stream.status() != QDataStream::Ok)
        {
            error = QStringLiteral("Star cache blob is truncated.");
            return std::nullopt;
        }
        out.push_back(star);
    }
    return out;
}

/**
 * @brief Returns the canonical frame key for a file path using FrameId.
 *
 * This is the single shared implementation used by both
 * StandaloneRegistrationUtils and ImageRegistrationProcess. The function
 * returns an empty QString if @p path is empty or FrameId throws.
 */
[[nodiscard]] inline QString registrationCanonicalFrameKey(const QString& path) noexcept
{
    try
    {
        return FrameId(path).canonicalPath();
    }
    catch (...)
    {
        return {};
    }
}
} // namespace an::ui

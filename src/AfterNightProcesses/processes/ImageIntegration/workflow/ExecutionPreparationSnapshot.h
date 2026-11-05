// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ExecutionPreparationSnapshot.h
 * @brief Immutable facts captured once for Image Integration execution preparation.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

struct ExecutionPreparationDiagnostics
{
    std::uint64_t fileStatOperations = 0;
    std::uint64_t databaseOpenOperations = 0;
    std::uint64_t parsedRecords = 0;
    /// Headers of recorded masters read because a master's platform file identity was no longer the recorded one.
    std::uint64_t strictHeaderReads = 0;
    std::uint64_t calibrationAssignmentsIndexed = 0;
    std::uint64_t calibrationAssignmentVisits = 0;
    std::uint64_t captureAttempts = 0;
    std::uint64_t consistencyRetries = 0;
    /// Bytes read to digest a file: the normalization coefficient documents the snapshot pins. No image is hashed.
    std::uint64_t bytesHashed = 0;
};

/// How the snapshot decides whether a recorded artifact can be reused. Neither mode reads an image payload.
enum class PreparationArtifactValidation
{
    /// The record's fingerprint, size and modification time match the file, and the record carries some provenance.
    MetadataOnly,
    /// The masters' check: as `MetadataOnly`, then the recorded platform file identity, or else the identity the file
    /// carries in its own header, which must be the recorded stamp and the manifest entry's identity.
    StrictContent
};

/**
 * @brief Read-only preparation state shared by planning and workflow stages.
 *
 * Construction snapshots filesystem, manifest, artifact-registry, registration,
 * normalization, drizzle, and calibration-assignment facts. Consumers receive
 * shared_ptr<const ExecutionPreparationSnapshot>; no mutating API is exposed.
 */
class AFTERNIGHTPROCESSES_API ExecutionPreparationSnapshot final
{
public:
    struct FileStamp
    {
        QString canonicalPath;
        bool exists = false;
        bool isFile = false;
        qint64 sizeBytes = 0;
        qint64 modifiedAtMs = 0;
        QString identityToken;
    };

    struct RegistrationGroupState
    {
        int settingsVersion = 0;
        QString detectionHash;
        QString registrationHash;
        QString referenceFrameId;
        QString lightFilesHash;
        QString calibrationHash;
        QString outputWcsJson;
        qint64 updatedAt = 0;
    };

    struct RegistrationMetric
    {
        bool succeeded = false;
        QString outputPath;
        qint64 outputSizeBytes = -1;
        qint64 outputModifiedAtMs = -1;
        QString outputIdentityToken;
        qint64 updatedAt = 0;
        /// The transform the record holds, in engine pixel coordinates.
        std::optional<an::RegistrationTransform> transform;
        stage_utils::SourceDistortionProvenance sourceDistortion;
        bool transformParsed = true;
    };

    struct StarCacheInputStamp
    {
        QString inputPath;
        qint64 inputMtime = 0;
        qint64 sizeBytes = -1;
        qint64 modifiedAtMs = -1;
        QString identityToken;
    };

    struct ParsedMetadata
    {
        QString path;
        bool parsed = false;
        QJsonObject object;
    };

    struct DrizzleSourceGeometryFacts
    {
        QString canonicalPath;
        bool metadataProbeSucceeded = false;
        std::uint64_t sourceWidth = 0;
        std::uint64_t sourceHeight = 0;
        std::optional<an::SipDistortion> inheritedSourceDistortion;
        QString inheritedSourceDistortionFingerprint;
    };

    struct CalibrationSubsetFacts
    {
        QString subsetHash;
        std::uint64_t matchingAssignmentCount = 0;
        /// The assignments with a master or a pedestal to calibrate their light with; the others' lights are used as
        /// they are, and no calibrated copy of them is made.
        std::uint64_t calibratingAssignmentCount = 0;
        QSet<QString> dependencyPaths;
        bool allCalibratedArtifactsReusable = false;
    };

    using RegistrationMetrics = QHash<FrameId, RegistrationMetric>;

    /// Captures the facts for a manifest, reading a drizzle source's header, and the header of a recorded master whose
    /// platform file identity moved, through `headerReader`; without one, no source geometry is captured, no drizzle
    /// artifact is reused and a moved master is recomputed. No master's payload is read.
    [[nodiscard]] static std::shared_ptr<const ExecutionPreparationSnapshot>
    build(const WorkflowManifest& manifest, const std::function<bool()>& shouldCancel = {},
          ImageIntegration::FrameHeaderReader headerReader = {});
    [[nodiscard]] static std::shared_ptr<const ExecutionPreparationSnapshot>
    build(const WorkflowManifest& manifest, std::optional<WorkflowManifest> previousManifest,
          const std::function<bool()>& shouldCancel = {}, ImageIntegration::FrameHeaderReader headerReader = {});

    ~ExecutionPreparationSnapshot();
    ExecutionPreparationSnapshot(const ExecutionPreparationSnapshot&) = delete;
    ExecutionPreparationSnapshot& operator=(const ExecutionPreparationSnapshot&) = delete;

    [[nodiscard]] const std::optional<WorkflowManifest>& previousManifest() const noexcept;
    [[nodiscard]] const ExecutionPreparationDiagnostics& diagnostics() const noexcept;
    /// Whether every fact the snapshot captured still holds: each file's size, modification time and platform identity
    /// stated again by a stat, and each drizzle source's header read again. It reads no artifact's payload.
    [[nodiscard]] bool matchesCurrentFacts(const std::function<bool()>& shouldCancel = std::function<bool()>(),
                                           const std::function<QString(const QString&)>& fileIdentityProvider =
                                               std::function<QString(const QString&)>()) const;
    [[nodiscard]] const FileStamp* fileStamp(const QString& path) const;
    [[nodiscard]] QJsonObject fileStampJson(const QString& path) const;

    [[nodiscard]] const WorkflowManifest::LightsCalibrationParams* calibrationAssignment(const FrameId& light) const;
    [[nodiscard]] const CalibrationSubsetFacts* calibrationSubsetFacts(const std::vector<FrameId>& lightFiles) const;

    [[nodiscard]] bool hasArtifactRecord(const QString& path) const;
    [[nodiscard]] bool isArtifactValid(const QString& path, const QString& expectedManifestHash,
                                       PreparationArtifactValidation validation) const;

    [[nodiscard]] bool registrationDatabaseUsable() const noexcept;
    [[nodiscard]] const RegistrationGroupState* registrationGroupState(const QString& groupKey,
                                                                       int settingsVersion) const;
    [[nodiscard]] const RegistrationGroupState* latestRegistrationGroupState(const QString& groupKey) const;
    [[nodiscard]] const RegistrationMetrics* registrationMetrics(const QString& groupKey, int settingsVersion) const;
    [[nodiscard]] std::optional<int> resolveRegistrationSettingsVersion(const QString& groupKey,
                                                                        int preferredVersion) const;
    [[nodiscard]] std::optional<qint64> starCacheMtime(const QString& groupKey, const QString& detectionHash,
                                                       const FrameId& frame) const;
    [[nodiscard]] std::optional<QString> starCacheInputPath(const QString& groupKey, const QString& detectionHash,
                                                            const FrameId& frame) const;
    [[nodiscard]] const StarCacheInputStamp* starCacheInputStamp(const QString& groupKey, const QString& detectionHash,
                                                                 const FrameId& frame) const;

    [[nodiscard]] const ParsedMetadata* normalizationMetadata(const IntegrationGroupKey& groupKey) const;
    [[nodiscard]] std::optional<QString> normalizationCoefficientDigest(const IntegrationGroupKey& groupKey) const;
    [[nodiscard]] const ParsedMetadata* drizzleMetadata(const IntegrationGroupKey& groupKey) const;
    [[nodiscard]] const DrizzleSourceGeometryFacts* drizzleSourceGeometry(const FrameId& frame) const;

private:
    friend class ExecutionPreparationSnapshotBuilder;
    struct Data;
    ExecutionPreparationSnapshot(std::unique_ptr<Data> data, const WorkflowManifest& manifest,
                                 const std::function<bool()>& shouldCancel);
    void captureStrictArtifactIdentities(const std::function<bool()>& shouldCancel);
    void buildCalibrationSubsetFacts(const WorkflowManifest& manifest);

    std::unique_ptr<Data> m_data;
};

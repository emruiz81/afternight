// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationMetricsStateLoader.h"

#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/SpectrophotometricColorCalibration/SpccHandoffSubsetSpectrophotometricCatalog.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <AfterNightPlatform/Logging.h>

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace
{
void assignError(QString* error, const QString& value)
{
    if (error != nullptr)
    {
        *error = value;
    }
}

QString canonicalFrameKey(const QString& frameId)
{
    try
    {
        return FrameId(frameId).canonicalPath();
    }
    catch (...)
    {
        LOG_WARNING("RegistrationMetricsStateLoader: failed to canonicalize frame ID '{}'; using trimmed fallback",
                    frameId.trimmed().toStdString());
        return frameId.trimmed();
    }
}

[[nodiscard]] std::optional<FrameId> persistedFrameId(const QString& frameId)
{
    if (frameId.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    try
    {
        return FrameId(frameId);
    }
    catch (...)
    {
        LOG_WARNING("RegistrationMetricsStateLoader: ignored invalid persisted frame ID '{}'",
                    frameId.trimmed().toStdString());
        return std::nullopt;
    }
}

[[nodiscard]] bool preferIdentityRow(const qint64 candidateUpdatedAt, const QString& candidateStoredFrameId,
                                     const qint64 selectedUpdatedAt, const QString& selectedStoredFrameId)
{
    if (candidateUpdatedAt != selectedUpdatedAt)
    {
        return candidateUpdatedAt > selectedUpdatedAt;
    }

    const auto candidate = persistedFrameId(candidateStoredFrameId);
    const auto selected = persistedFrameId(selectedStoredFrameId);
    if (!candidate.has_value())
    {
        return false;
    }
    if (!selected.has_value())
    {
        return true;
    }

    const bool candidateUsesCanonicalSpelling = candidateStoredFrameId == candidate->canonicalPath();
    const bool selectedUsesCanonicalSpelling = selectedStoredFrameId == selected->canonicalPath();
    if (candidateUsesCanonicalSpelling != selectedUsesCanonicalSpelling)
    {
        return candidateUsesCanonicalSpelling;
    }
    const int canonicalComparison =
        QString::compare(candidate->canonicalPath(), selected->canonicalPath(), Qt::CaseSensitive);
    return canonicalComparison != 0
               ? canonicalComparison < 0
               : QString::compare(candidateStoredFrameId, selectedStoredFrameId, Qt::CaseSensitive) < 0;
}

bool isIdentityTransform(const an::RegistrationTransform& transform)
{
    if (!transform.isLinear())
    {
        return false;
    }

    static constexpr double kTolerance = 1e-6;
    const auto& matrix = transform.matrix();
    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const double expected = (row == col) ? 1.0 : 0.0;
            if (std::abs(matrix[static_cast<std::size_t>(row * 3 + col)] - expected) > kTolerance)
            {
                return false;
            }
        }
    }

    return true;
}

QString referenceRegistrationPath(
    const QString& referenceFrameId,
    const std::unordered_map<QString, const an::ui::RegistrationStarCacheEntry*>& starCacheByIdentity)
{
    if (referenceFrameId.trimmed().isEmpty())
    {
        return {};
    }

    const auto frame = persistedFrameId(referenceFrameId);
    if (!frame.has_value())
    {
        return {};
    }
    const auto it = starCacheByIdentity.find(frame->identityKey());
    if (it != starCacheByIdentity.end() && it->second != nullptr && !it->second->inputPath.trimmed().isEmpty())
    {
        return it->second->inputPath;
    }

    return frame->canonicalPath();
}

/// A Gaia source identifier as the database holds it, or nothing for text that names none.
std::optional<std::uint64_t> sourceIdentifierOf(const QString& text)
{
    const QByteArray digits = text.trimmed().toLatin1();
    std::uint64_t value = 0u;
    const auto [end, failure] = std::from_chars(digits.constData(), digits.constData() + digits.size(), value);
    if (failure != std::errc{} || end != digits.constData() + digits.size() || value == 0u)
    {
        return std::nullopt;
    }
    return value;
}

std::string toStdString(const QVariant& value)
{
    return value.toString().toStdString();
}

std::optional<double> optionalDouble(const QVariant& value)
{
    if (value.isNull())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
}

an::ui::RegistrationMatchedSourceRecord matchedSourceRecordFromQuery(const QString& groupKey, const int settingsVersion,
                                                                     const QString& rawFrameId, const QSqlQuery& query)
{
    an::ui::RegistrationMatchedSourceRecord record;
    record.frameId = FrameId(rawFrameId);
    record.groupKey = groupKey;
    record.settingsVersion = settingsVersion;
    record.reference.sourceIdentifier = sourceIdentifierOf(query.value(1).toString()).value_or(0u);
    record.reference.astrometricCatalogFamilyId = toStdString(query.value(2));
    record.reference.astrometricCatalogManifestId = toStdString(query.value(3));
    record.reference.spectrumCatalogFamilyId = toStdString(query.value(4));
    record.reference.spectrumCatalogManifestId = toStdString(query.value(5));
    record.reference.wcsSolutionId = toStdString(query.value(6));
    record.reference.observationEpoch = toStdString(query.value(7));
    record.reference.properMotionModelVersion = toStdString(query.value(8));
    record.reference.separationArcseconds = optionalDouble(query.value(9));
    record.reference.residualPixels = optionalDouble(query.value(10));
    const QString status = query.value(11).toString().trimmed();
    record.lookupStatus = status.isEmpty() ? QString::fromLatin1(an::ui::kMatchedSourceUntested) : status;
    record.updatedAt = query.value(12).toLongLong();
    return record;
}

an::ui::CometTrackingRunRecord cometRunRecordFromQuery(const QSqlQuery& query)
{
    an::ui::CometTrackingRunRecord record;
    record.cometRunId = query.value(0).toString();
    record.groupKey = query.value(1).toString();
    record.settingsVersion = query.value(2).toInt();
    record.cometContextId = query.value(3).toString();
    record.detectionHash = query.value(4).toString();
    record.registrationHash = query.value(5).toString();
    record.lightFilesHash = query.value(6).toString();
    record.calibrationHash = query.value(7).toString();
    record.resolvedReferenceFrameId = query.value(8).toString();
    record.referenceEpoch = query.value(9).toDouble();
    record.mode = query.value(10).toString();
    record.status = query.value(11).toString();
    record.createdAt = query.value(12).toLongLong();
    record.updatedAt = query.value(13).toLongLong();
    return record;
}

an::ui::CometRoleReadinessRecord cometRoleReadinessRecordFromQuery(const QSqlQuery& query)
{
    an::ui::CometRoleReadinessRecord record;
    record.cometRunId = query.value(0).toString();
    record.role = query.value(1).toString();
    record.readinessStatus = query.value(2).toString();
    record.selectedFrameCount = query.value(3).toInt();
    record.successfulTransformCount = query.value(4).toInt();
    record.failedTransformCount = query.value(5).toInt();
    record.missingTransformCount = query.value(6).toInt();
    record.outputPlaneStatus = query.value(7).toString();
    record.requiresMaterializedOutputs = query.value(8).toInt() != 0;
    record.materializedOutputCount = query.value(9).toInt();
    record.missingOutputCount = query.value(10).toInt();
    record.failedOutputCount = query.value(11).toInt();
    record.failureReason = query.value(12).toString();
    record.updatedAt = query.value(13).toLongLong();
    return record;
}

an::ui::CometTrackingRunSummaryRecord cometRunSummaryRecordFromQuery(const QSqlQuery& query)
{
    an::ui::CometTrackingRunSummaryRecord record;
    record.cometRunId = query.value(0).toString();
    record.fitModel = query.value(1).toString();
    record.referenceEpoch = query.value(2).toDouble();
    record.fitTimeOrigin = query.value(3).toDouble();
    record.rmsPixels = query.value(4).toDouble();
    record.maxResidualPixels = query.value(5).toDouble();
    record.acceptedInputCount = query.value(6).toInt();
    record.rejectedInputCount = query.value(7).toInt();
    record.predictedOnlyFrameCount = query.value(8).toInt();
    record.warningFlags = query.value(9).toString();
    record.updatedAt = query.value(10).toLongLong();
    return record;
}

an::ui::CometTrackingPointRecord cometTrackingPointRecordFromQuery(const QSqlQuery& query)
{
    an::ui::CometTrackingPointRecord record;
    record.pointId = query.value(0).toString();
    record.cometRunId = query.value(1).toString();
    record.originalFrameId = FrameId(query.value(2).toString());
    record.refinementSourcePath = query.value(3).toString();
    record.epoch = query.value(4).toDouble();
    record.sourcePosition = QPointF(query.value(5).toDouble(), query.value(6).toDouble());
    record.transformDomainPosition = QPointF(query.value(7).toDouble(), query.value(8).toDouble());
    record.predictedSourcePosition = QPointF(query.value(9).toDouble(), query.value(10).toDouble());
    record.referencePlanePosition = QPointF(query.value(11).toDouble(), query.value(12).toDouble());
    record.predictedReferencePlanePosition = QPointF(query.value(13).toDouble(), query.value(14).toDouble());
    record.confidence = query.value(15).toDouble();
    record.residualPixels = query.value(16).toDouble();
    record.measuredFwhmPixels = query.value(17).toDouble();
    record.measuredDiameterPixels = query.value(18).toDouble();
    record.measuredEllipticity = query.value(19).toDouble();
    record.apertureRadiusPixels = query.value(20).toDouble();
    record.localBackground = query.value(21).toDouble();
    record.snr = query.value(22).toDouble();
    record.roiX = query.value(23).toDouble();
    record.roiY = query.value(24).toDouble();
    record.roiWidth = query.value(25).toDouble();
    record.roiHeight = query.value(26).toDouble();
    record.measurementMethod = query.value(27).toString();
    record.origin = query.value(28).toString();
    record.fitState = query.value(29).toString();
    record.rejectionReason = query.value(30).toString();
    record.updatedAt = query.value(31).toLongLong();
    return record;
}

an::ui::CometOutputPlaneRecord cometOutputPlaneRecordFromQuery(const QSqlQuery& query)
{
    an::ui::CometOutputPlaneRecord record;
    record.cometOutputPlaneId = query.value(0).toString();
    record.cometRunId = query.value(1).toString();
    record.role = query.value(2).toString();
    record.alignmentMode = query.value(3).toString();
    record.coordinateFrameKind = query.value(4).toString();
    record.canvasWidth = query.value(5).toInt();
    record.canvasHeight = query.value(6).toInt();
    record.canvasOffsetX = query.value(7).toDouble();
    record.canvasOffsetY = query.value(8).toDouble();
    record.skyWcsValid = query.value(9).toInt() != 0;
    record.skyWcsInvalidReason = query.value(10).toString();
    record.descriptorJson = query.value(11).toString();
    record.sourceFingerprint = query.value(12).toString();
    record.updatedAt = query.value(13).toLongLong();
    return record;
}

an::ui::CometTransformRoleRecord cometTransformRoleRecordFromQuery(const QSqlQuery& query)
{
    an::ui::CometTransformRoleRecord record;
    record.cometRunId = query.value(0).toString();
    record.frameId = FrameId(query.value(1).toString());
    record.role = query.value(2).toString();
    record.cometOutputPlaneId = query.value(3).toString();
    record.transformJson = query.value(4).toString();
    record.sourceDistortionJson = query.value(5).toString();
    record.succeeded = query.value(6).toInt() != 0;
    record.failureReason = query.value(7).toString();
    record.updatedAt = query.value(8).toLongLong();
    return record;
}

an::ui::CometRegisteredOutputRecord cometRegisteredOutputRecordFromQuery(const QSqlQuery& query)
{
    an::ui::CometRegisteredOutputRecord record;
    record.cometRunId = query.value(0).toString();
    record.frameId = FrameId(query.value(1).toString());
    record.role = query.value(2).toString();
    record.cometOutputPlaneId = query.value(3).toString();
    record.outputPath = query.value(4).toString();
    record.artifactHash = query.value(5).toString();
    record.outputSizeBytes = query.value(6).toLongLong();
    record.outputModifiedAtMs = query.value(7).toLongLong();
    record.outputIdentityToken = query.value(8).toString();
    record.succeeded = query.value(9).toInt() != 0;
    record.failureReason = query.value(10).toString();
    record.updatedAt = query.value(11).toLongLong();
    return record;
}

an::ui::StackingCometArtifactRecord stackingCometArtifactRecordFromQuery(const QSqlQuery& query)
{
    an::ui::StackingCometArtifactRecord record;
    record.cometRunId = query.value(0).toString();
    record.integrationGroupKey = query.value(1).toString();
    record.stackingHash = query.value(2).toString();
    record.starBaseOutputDescriptorId = query.value(3).toString();
    record.cometOutputPlaneId = query.value(4).toString();
    record.artifactKind = query.value(5).toString();
    record.outputPath = query.value(6).toString();
    record.artifactHash = query.value(7).toString();
    record.maskSettingsFingerprint = query.value(8).toString();
    record.succeeded = query.value(9).toInt() != 0;
    record.failureReason = query.value(10).toString();
    record.updatedAt = query.value(11).toLongLong();
    return record;
}
} // namespace

namespace an::ui
{
RegistrationMetricsStateLoader::RegistrationMetricsStateLoader(QString databasePath, QString connectionPrefix,
                                                               WorkflowExecutionCounterAccumulator* counters)
    : m_connection(std::move(databasePath), std::move(connectionPrefix), counters)
{
}

bool RegistrationMetricsStateLoader::open(QString* error)
{
    assignError(error, QString());
    return m_connection.open(error);
}

bool RegistrationMetricsStateLoader::isOpen() const
{
    return m_connection.isOpen();
}

bool RegistrationMetricsStateLoader::loadAvailableStates(std::vector<RegistrationMetricsGroupVersion>& out,
                                                         QString* error)
{
    assignError(error, QString());
    out.clear();

    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT group_key, settings_version, MAX(updated_at) "
                                 "FROM ("
                                 "    SELECT group_key, settings_version, updated_at FROM registration_group_state "
                                 "    UNION ALL "
                                 "    SELECT group_key, settings_version, updated_at FROM registration_metrics "
                                 "    UNION ALL "
                                 "    SELECT group_key, settings_version, updated_at FROM registration_matched_sources"
                                 ") "
                                 "GROUP BY group_key, settings_version "
                                 "ORDER BY MAX(updated_at) DESC, group_key ASC, settings_version DESC"));

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    while (query.next())
    {
        RegistrationMetricsGroupVersion state;
        state.groupKey = query.value(0).toString();
        state.settingsVersion = query.value(1).toInt();
        state.updatedAt = query.value(2).toLongLong();
        if (!state.groupKey.trimmed().isEmpty())
        {
            out.push_back(std::move(state));
        }
    }

    return true;
}

std::optional<RegistrationGroupState>
RegistrationMetricsStateLoader::loadGroupState(const QString& groupKey, int settingsVersion, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT detection_hash, registration_hash, reference_frame_id, light_files_hash, "
                                 "calibration_hash, output_wcs_json, updated_at "
                                 "FROM registration_group_state "
                                 "WHERE group_key = :group_key AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        return std::nullopt;
    }

    RegistrationGroupState state;
    state.groupKey = groupKey;
    state.settingsVersion = settingsVersion;
    state.detectionHash = query.value(0).toString();
    state.registrationHash = query.value(1).toString();
    state.referenceFrameId = query.value(2).toString();
    state.lightFilesHash = query.value(3).toString();
    state.calibrationHash = query.value(4).toString();
    state.outputWcsJson = query.value(5).toString();
    state.updatedAt = query.value(6).toLongLong();
    return state;
}

std::optional<RegistrationGroupState> RegistrationMetricsStateLoader::loadLatestGroupState(const QString& groupKey,
                                                                                           QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT settings_version, detection_hash, registration_hash, reference_frame_id, "
                                 "light_files_hash, calibration_hash, output_wcs_json, updated_at "
                                 "FROM registration_group_state "
                                 "WHERE group_key = :group_key "
                                 "ORDER BY settings_version DESC LIMIT 1"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        return std::nullopt;
    }

    RegistrationGroupState state;
    state.groupKey = groupKey;
    state.settingsVersion = query.value(0).toInt();
    state.detectionHash = query.value(1).toString();
    state.registrationHash = query.value(2).toString();
    state.referenceFrameId = query.value(3).toString();
    state.lightFilesHash = query.value(4).toString();
    state.calibrationHash = query.value(5).toString();
    state.outputWcsJson = query.value(6).toString();
    state.updatedAt = query.value(7).toLongLong();
    return state;
}

bool RegistrationMetricsStateLoader::loadMetricStatuses(const QString& groupKey, int settingsVersion,
                                                        std::unordered_map<QString, RegistrationMetricStatus>& out,
                                                        QString* error)
{
    assignError(error, QString());
    out.clear();

    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT frame_id, succeeded, output_path, output_size_bytes, "
                                 "output_modified_at_ms, output_identity_token, transform_json, updated_at "
                                 "FROM registration_metrics "
                                 "WHERE group_key = :group_key AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    struct SelectedStatus
    {
        QString storedFrameId;
        FrameId frameId;
        RegistrationMetricStatus status;
    };
    std::unordered_map<QString, SelectedStatus> selectedByIdentity;
    while (query.next())
    {
        const QString storedFrameId = query.value(0).toString();
        const auto frameId = persistedFrameId(storedFrameId);
        if (!frameId.has_value())
        {
            continue;
        }

        RegistrationMetricStatus status;
        status.succeeded = (query.value(1).toInt() != 0);
        status.outputPath = query.value(2).toString();
        status.outputSizeBytes = query.value(3).toLongLong();
        status.outputModifiedAtMs = query.value(4).toLongLong();
        status.outputIdentityToken = query.value(5).toString();
        status.transformJson = query.value(6).toString();
        status.updatedAt = query.value(7).toLongLong();
        const auto selected = selectedByIdentity.find(frameId->identityKey());
        if (selected == selectedByIdentity.end() ||
            preferIdentityRow(status.updatedAt, storedFrameId, selected->second.status.updatedAt,
                              selected->second.storedFrameId))
        {
            selectedByIdentity.insert_or_assign(frameId->identityKey(),
                                                SelectedStatus{storedFrameId, *frameId, std::move(status)});
        }
    }

    out.reserve(selectedByIdentity.size());
    for (auto& [_, selected] : selectedByIdentity)
    {
        out.emplace(selected.frameId.canonicalPath(), std::move(selected.status));
    }

    return true;
}

bool RegistrationMetricsStateLoader::loadStarCacheForGroup(const QString& groupKey, const QString& detectionHash,
                                                           std::unordered_map<QString, RegistrationStarCacheEntry>& out,
                                                           QString* error)
{
    assignError(error, QString());
    out.clear();

    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral(
        "SELECT frame_id, input_path, input_mtime, input_size_bytes, input_modified_at_ms, input_identity_token, "
        "stars_count, fwhm, snr, roundness, stars_blob, updated_at, "
        "background_noise, global_mad, global_scale "
        "FROM star_detection_cache "
        "WHERE group_key = :group_key AND detection_hash = :detection_hash"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":detection_hash"), detectionHash);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    std::unordered_map<QString, QString> selectedStoredFrameIdByIdentity;
    while (query.next())
    {
        const QString rawFrameId = query.value(0).toString();
        const auto frameId = persistedFrameId(rawFrameId);
        if (!frameId.has_value())
        {
            continue;
        }

        RegistrationStarCacheEntry entry;
        entry.frameId = *frameId;
        entry.groupKey = groupKey;
        entry.detectionHash = detectionHash;
        entry.inputPath = query.value(1).toString();
        entry.inputMtime = query.value(2).toLongLong();
        entry.inputSizeBytes = query.value(3).toLongLong();
        entry.inputModifiedAtMs = query.value(4).toLongLong();
        entry.inputIdentityToken = query.value(5).toString();
        entry.starsCount = query.value(6).toInt();
        entry.fwhm = query.value(7).toDouble();
        entry.snr = query.value(8).toDouble();
        entry.roundness = query.value(9).toDouble();
        entry.starsBlob = query.value(10).toByteArray();
        entry.updatedAt = query.value(11).toLongLong();
        entry.noise.background_noise = query.value(12).toDouble();
        entry.noise.global_mad = query.value(13).toDouble();
        entry.noise.global_scale = query.value(14).toDouble();
        const QString identityKey = frameId->identityKey();
        const auto selected = out.find(identityKey);
        if (selected == out.end() || preferIdentityRow(entry.updatedAt, rawFrameId, selected->second.updatedAt,
                                                       selectedStoredFrameIdByIdentity.at(identityKey)))
        {
            selectedStoredFrameIdByIdentity.insert_or_assign(identityKey, rawFrameId);
            out.insert_or_assign(identityKey, std::move(entry));
        }
    }

    std::unordered_map<QString, RegistrationStarCacheEntry> byCanonicalSpelling;
    byCanonicalSpelling.reserve(out.size());
    for (auto& [_, entry] : out)
    {
        byCanonicalSpelling.emplace(entry.frameId.canonicalPath(), std::move(entry));
    }
    out = std::move(byCanonicalSpelling);

    return true;
}

bool RegistrationMetricsStateLoader::loadMetricRecords(const QString& groupKey, int settingsVersion,
                                                       std::vector<RegistrationMetricRecord>& out, QString* error)
{
    assignError(error, QString());
    out.clear();

    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT frame_id, stars, fwhm, snr, eccentricity, "
                                 "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, "
                                 "astrometric_rms_arcsec, limiting_magnitude, catalog_matched_stars, "
                                 "succeeded, output_path, output_size_bytes, output_modified_at_ms, "
                                 "output_identity_token, transform_json, updated_at, "
                                 "background_noise, global_mad, global_scale "
                                 "FROM registration_metrics "
                                 "WHERE group_key = :group_key AND settings_version = :settings_version "
                                 "ORDER BY frame_id ASC"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    struct SelectedMetricRecord
    {
        QString storedFrameId;
        RegistrationMetricRecord record;
    };
    std::unordered_map<QString, SelectedMetricRecord> selectedByIdentity;
    while (query.next())
    {
        const QString storedFrameId = query.value(0).toString();
        const auto frameId = persistedFrameId(storedFrameId);
        if (!frameId.has_value())
        {
            continue;
        }

        RegistrationMetricRecord record;
        record.frameId = *frameId;
        record.groupKey = groupKey;
        record.settingsVersion = settingsVersion;
        record.metrics.stars = query.value(1).toInt();
        record.metrics.fwhm = query.value(2).toDouble();
        record.metrics.snr = query.value(3).toDouble();
        record.metrics.eccentricity = query.value(4).toDouble();
        record.metrics.photometric_zero_point = query.value(5).toDouble();
        record.metrics.zero_point_scatter = query.value(6).toDouble();
        record.metrics.astrometric_rms_pixels = query.value(7).toDouble();
        record.metrics.astrometric_rms_arcsec = query.value(8).toDouble();
        record.metrics.limiting_magnitude = query.value(9).toDouble();
        record.metrics.catalog_matched_stars = query.value(10).toInt();
        record.metrics.succeeded = (query.value(11).toInt() != 0);
        record.outputPath = query.value(12).toString();
        record.outputSizeBytes = query.value(13).toLongLong();
        record.outputModifiedAtMs = query.value(14).toLongLong();
        record.outputIdentityToken = query.value(15).toString();
        record.transformJson = query.value(16).toString();
        record.updatedAt = query.value(17).toLongLong();
        record.noise.background_noise = query.value(18).toDouble();
        record.noise.global_mad = query.value(19).toDouble();
        record.noise.global_scale = query.value(20).toDouble();
        const auto selected = selectedByIdentity.find(frameId->identityKey());
        if (selected == selectedByIdentity.end() ||
            preferIdentityRow(record.updatedAt, storedFrameId, selected->second.record.updatedAt,
                              selected->second.storedFrameId))
        {
            selectedByIdentity.insert_or_assign(frameId->identityKey(),
                                                SelectedMetricRecord{storedFrameId, std::move(record)});
        }
    }

    out.reserve(selectedByIdentity.size());
    for (auto& [_, selected] : selectedByIdentity)
    {
        out.push_back(std::move(selected.record));
    }
    std::sort(out.begin(), out.end(), [](const RegistrationMetricRecord& lhs, const RegistrationMetricRecord& rhs) {
        return lhs.frameId.canonicalPath() < rhs.frameId.canonicalPath();
    });

    return true;
}

bool RegistrationMetricsStateLoader::loadMatchedSources(const QString& groupKey, int settingsVersion,
                                                        std::vector<RegistrationMatchedSourceRecord>& out,
                                                        QString* error)
{
    assignError(error, QString());
    out.clear();

    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT frame_id, gaia_source_id, astrometric_catalog_family_id, "
                                 "astrometric_catalog_manifest_id, spcc_catalog_family_id, "
                                 "spcc_catalog_manifest_id, wcs_solution_id, observation_epoch, "
                                 "proper_motion_model_version, match_separation_arcsec, residual_px, lookup_status, "
                                 "updated_at "
                                 "FROM registration_matched_sources "
                                 "WHERE group_key = :group_key AND settings_version = :settings_version "
                                 "ORDER BY frame_id ASC, gaia_source_id ASC"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    struct MatchedSourcePayload
    {
        QString storedFrameId;
        FrameId frameId;
        qint64 newestUpdatedAt = (std::numeric_limits<qint64>::min)();
        std::vector<RegistrationMatchedSourceRecord> records;
    };
    std::unordered_map<QString, std::unordered_map<QString, MatchedSourcePayload>> payloadsByIdentity;
    while (query.next())
    {
        const QString storedFrameId = query.value(0).toString();
        const auto frameId = persistedFrameId(storedFrameId);
        const QString sourceId = query.value(1).toString().trimmed();
        if (!frameId.has_value() || !sourceIdentifierOf(sourceId).has_value())
        {
            continue;
        }

        auto& payload = payloadsByIdentity[frameId->identityKey()][storedFrameId];
        if (payload.storedFrameId.isEmpty())
        {
            payload.storedFrameId = storedFrameId;
            payload.frameId = *frameId;
        }
        payload.newestUpdatedAt = (std::max)(payload.newestUpdatedAt, query.value(12).toLongLong());
        payload.records.push_back(matchedSourceRecordFromQuery(groupKey, settingsVersion, storedFrameId, query));
    }

    for (auto& [_, payloadsBySpelling] : payloadsByIdentity)
    {
        MatchedSourcePayload* selected = nullptr;
        for (auto& payloadEntry : payloadsBySpelling)
        {
            auto& payload = payloadEntry.second;
            if (selected == nullptr || preferIdentityRow(payload.newestUpdatedAt, payload.storedFrameId,
                                                         selected->newestUpdatedAt, selected->storedFrameId))
            {
                selected = &payload;
            }
        }
        if (selected != nullptr)
        {
            out.insert(out.end(), std::make_move_iterator(selected->records.begin()),
                       std::make_move_iterator(selected->records.end()));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const RegistrationMatchedSourceRecord& lhs, const RegistrationMatchedSourceRecord& rhs) {
                  if (lhs.frameId.canonicalPath() != rhs.frameId.canonicalPath())
                  {
                      return lhs.frameId.canonicalPath() < rhs.frameId.canonicalPath();
                  }
                  return lhs.reference.sourceIdentifier < rhs.reference.sourceIdentifier;
              });

    return true;
}

bool RegistrationMetricsStateLoader::loadMatchedSourcesForFrame(const QString& groupKey, int settingsVersion,
                                                                const FrameId& frameId,
                                                                std::vector<RegistrationMatchedSourceRecord>& out,
                                                                QString* error)
{
    assignError(error, QString());
    out.clear();

    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    if (!frameId.isValid())
    {
        return true;
    }

    std::vector<RegistrationMatchedSourceRecord> groupRecords;
    if (!loadMatchedSources(groupKey, settingsVersion, groupRecords, error))
    {
        return false;
    }
    for (auto& record : groupRecords)
    {
        if (record.frameId == frameId)
        {
            out.push_back(std::move(record));
        }
    }

    return true;
}

std::optional<CometTrackingRunRecord> RegistrationMetricsStateLoader::loadActiveCometRunForContext(
    const QString& groupKey, const QString& registrationHash, const QString& lightFilesHash,
    const QString& calibrationHash, const QString& resolvedReferenceFrameId, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_run_id, group_key, settings_version, comet_context_id, "
                                 "detection_hash, registration_hash, light_files_hash, calibration_hash, "
                                 "resolved_reference_frame_id, reference_epoch, mode, status, created_at, updated_at "
                                 "FROM comet_tracking_runs "
                                 "WHERE group_key = :group_key AND registration_hash = :registration_hash "
                                 "AND light_files_hash = :light_files_hash AND calibration_hash = :calibration_hash "
                                 "AND resolved_reference_frame_id = :resolved_reference_frame_id "
                                 "AND status = 'Active'"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":registration_hash"), registrationHash);
    query.bindValue(QStringLiteral(":light_files_hash"), lightFilesHash);
    query.bindValue(QStringLiteral(":calibration_hash"), calibrationHash);
    query.bindValue(QStringLiteral(":resolved_reference_frame_id"), resolvedReferenceFrameId);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        return std::nullopt;
    }

    return cometRunRecordFromQuery(query);
}

std::optional<CometTrackingRunSummaryRecord>
RegistrationMetricsStateLoader::loadCometTrackingRunSummary(const QString& cometRunId, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_run_id, fit_model, reference_epoch, fit_time_origin, rms_pixels, "
                                 "max_residual_pixels, accepted_input_count, rejected_input_count, "
                                 "predicted_only_frame_count, warning_flags, updated_at "
                                 "FROM comet_tracking_run_summary "
                                 "WHERE comet_run_id = :comet_run_id"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        assignError(error, QStringLiteral("Comet tracking run summary was not found for run '%1'.").arg(cometRunId));
        return std::nullopt;
    }

    return cometRunSummaryRecordFromQuery(query);
}

bool RegistrationMetricsStateLoader::loadCometTrackingPoints(const QString& cometRunId,
                                                             std::vector<CometTrackingPointRecord>& out, QString* error)
{
    assignError(error, QString());
    out.clear();
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT point_id, comet_run_id, original_frame_id, refinement_source_path, epoch, "
                                 "source_x, source_y, transform_domain_x, transform_domain_y, predicted_source_x, "
                                 "predicted_source_y, reference_plane_x, reference_plane_y, "
                                 "predicted_reference_plane_x, predicted_reference_plane_y, confidence, "
                                 "residual_pixels, measured_fwhm_pixels, measured_diameter_pixels, "
                                 "measured_ellipticity, aperture_radius_pixels, local_background, snr, roi_x, "
                                 "roi_y, roi_width, roi_height, measurement_method, origin, fit_state, "
                                 "rejection_reason, updated_at "
                                 "FROM comet_tracking_points "
                                 "WHERE comet_run_id = :comet_run_id "
                                 "ORDER BY original_frame_id ASC, point_id ASC"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    while (query.next())
    {
        out.push_back(cometTrackingPointRecordFromQuery(query));
    }

    return true;
}

std::optional<CometRoleReadinessRecord>
RegistrationMetricsStateLoader::loadCometRoleReadiness(const QString& cometRunId, const QString& role, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_run_id, role, readiness_status, selected_frame_count, "
                                 "successful_transform_count, failed_transform_count, missing_transform_count, "
                                 "output_plane_status, requires_materialized_outputs, materialized_output_count, "
                                 "missing_output_count, failed_output_count, failure_reason, updated_at "
                                 "FROM comet_role_readiness "
                                 "WHERE comet_run_id = :comet_run_id AND role = :role"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    query.bindValue(QStringLiteral(":role"), role);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        assignError(
            error,
            QStringLiteral("Comet role readiness was not found for run '%1' and role '%2'.").arg(cometRunId, role));
        return std::nullopt;
    }

    return cometRoleReadinessRecordFromQuery(query);
}

std::optional<CometOutputPlaneRecord>
RegistrationMetricsStateLoader::loadCometOutputPlane(const QString& cometOutputPlaneId, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_output_plane_id, comet_run_id, role, alignment_mode, "
                                 "coordinate_frame_kind, canvas_width, canvas_height, canvas_offset_x, "
                                 "canvas_offset_y, sky_wcs_valid, sky_wcs_invalid_reason, descriptor_json, "
                                 "source_fingerprint, updated_at "
                                 "FROM comet_output_planes WHERE comet_output_plane_id = :comet_output_plane_id"));
    query.bindValue(QStringLiteral(":comet_output_plane_id"), cometOutputPlaneId);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        assignError(error, QStringLiteral("Comet output plane '%1' was not found.").arg(cometOutputPlaneId));
        return std::nullopt;
    }

    return cometOutputPlaneRecordFromQuery(query);
}

std::optional<CometOutputPlaneRecord>
RegistrationMetricsStateLoader::loadCometOutputPlaneForRole(const QString& cometRunId, const QString& role,
                                                            QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_output_plane_id, comet_run_id, role, alignment_mode, "
                                 "coordinate_frame_kind, canvas_width, canvas_height, canvas_offset_x, "
                                 "canvas_offset_y, sky_wcs_valid, sky_wcs_invalid_reason, descriptor_json, "
                                 "source_fingerprint, updated_at "
                                 "FROM comet_output_planes "
                                 "WHERE comet_run_id = :comet_run_id AND role = :role"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    query.bindValue(QStringLiteral(":role"), role);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        assignError(
            error,
            QStringLiteral("Comet output plane was not found for run '%1' and role '%2'.").arg(cometRunId, role));
        return std::nullopt;
    }

    return cometOutputPlaneRecordFromQuery(query);
}

std::optional<CometTransformRoleRecord>
RegistrationMetricsStateLoader::loadCometTransformRole(const QString& cometRunId, const FrameId& frameId,
                                                       const QString& role, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_run_id, frame_id, role, comet_output_plane_id, transform_json, "
                                 "source_distortion_json, succeeded, failure_reason, updated_at "
                                 "FROM comet_transform_roles "
                                 "WHERE comet_run_id = :comet_run_id AND frame_id = :frame_id AND role = :role"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    query.bindValue(QStringLiteral(":frame_id"), frameId.canonicalPath());
    query.bindValue(QStringLiteral(":role"), role);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        assignError(error, QStringLiteral("Comet transform role '%1' was not found for run '%2' and frame '%3'.")
                               .arg(role, cometRunId, frameId.canonicalPath()));
        return std::nullopt;
    }

    return cometTransformRoleRecordFromQuery(query);
}

bool RegistrationMetricsStateLoader::loadCometTransformRoles(const QString& cometRunId, const QString& role,
                                                             std::vector<CometTransformRoleRecord>& out, QString* error)
{
    assignError(error, QString());
    out.clear();
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_run_id, frame_id, role, comet_output_plane_id, transform_json, "
                                 "source_distortion_json, succeeded, failure_reason, updated_at "
                                 "FROM comet_transform_roles "
                                 "WHERE comet_run_id = :comet_run_id AND role = :role "
                                 "ORDER BY frame_id ASC"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    query.bindValue(QStringLiteral(":role"), role);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    while (query.next())
    {
        out.push_back(cometTransformRoleRecordFromQuery(query));
    }

    return true;
}

std::optional<CometTransformRoleProduct>
RegistrationMetricsStateLoader::loadCometTransformRoleProduct(const QString& cometRunId, const QString& role,
                                                              QString* error)
{
    assignError(error, QString());

    const auto readiness = loadCometRoleReadiness(cometRunId, role, error);
    if (!readiness.has_value())
    {
        return std::nullopt;
    }

    const auto outputPlane = loadCometOutputPlaneForRole(cometRunId, role, error);
    if (!outputPlane.has_value())
    {
        return std::nullopt;
    }

    std::vector<CometTransformRoleRecord> transforms;
    if (!loadCometTransformRoles(cometRunId, role, transforms, error))
    {
        return std::nullopt;
    }
    if (transforms.empty())
    {
        assignError(
            error,
            QStringLiteral("Comet transform roles were not found for run '%1' and role '%2'.").arg(cometRunId, role));
        return std::nullopt;
    }

    CometTransformRoleProduct product;
    product.readiness = readiness.value();
    product.outputPlane = outputPlane.value();
    product.transforms = std::move(transforms);
    return product;
}

std::optional<CometRegisteredOutputRecord>
RegistrationMetricsStateLoader::loadCometRegisteredOutput(const QString& cometRunId, const FrameId& frameId,
                                                          const QString& role, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_run_id, frame_id, role, comet_output_plane_id, output_path, "
                                 "artifact_hash, output_size_bytes, output_modified_at_ms, output_identity_token, "
                                 "succeeded, failure_reason, updated_at "
                                 "FROM comet_registered_outputs "
                                 "WHERE comet_run_id = :comet_run_id AND frame_id = :frame_id AND role = :role"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);
    query.bindValue(QStringLiteral(":frame_id"), frameId.canonicalPath());
    query.bindValue(QStringLiteral(":role"), role);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next())
    {
        assignError(error,
                    QStringLiteral("Comet registered output role '%1' was not found for run '%2' and frame '%3'.")
                        .arg(role, cometRunId, frameId.canonicalPath()));
        return std::nullopt;
    }

    return cometRegisteredOutputRecordFromQuery(query);
}

bool RegistrationMetricsStateLoader::loadStackingCometArtifacts(const QString& cometRunId,
                                                                std::vector<StackingCometArtifactRecord>& out,
                                                                QString* error)
{
    assignError(error, QString());
    out.clear();

    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return false;
    }

    QSqlQuery query(m_connection.db());
    query.prepare(QStringLiteral("SELECT comet_run_id, integration_group_key, stacking_hash, "
                                 "star_base_output_descriptor_id, comet_output_plane_id, artifact_kind, output_path, "
                                 "artifact_hash, mask_settings_fingerprint, succeeded, failure_reason, updated_at "
                                 "FROM stacking_comet_artifacts "
                                 "WHERE comet_run_id = :comet_run_id "
                                 "ORDER BY integration_group_key ASC, stacking_hash ASC, artifact_kind ASC"));
    query.bindValue(QStringLiteral(":comet_run_id"), cometRunId);

    if (!m_connection.execute(query))
    {
        assignError(error, query.lastError().text());
        return false;
    }

    while (query.next())
    {
        out.push_back(stackingCometArtifactRecordFromQuery(query));
    }

    return true;
}

std::optional<RegistrationMetricsLoadResult>
RegistrationMetricsStateLoader::loadState(const QString& groupKey, int settingsVersion,
                                          const std::vector<FrameId>& currentFrames, QString* error)
{
    assignError(error, QString());
    if (!m_connection.isOpen())
    {
        assignError(error, QStringLiteral("Registration metrics database is not open."));
        return std::nullopt;
    }

    RegistrationMetricsLoadResult result;

    QString groupStateError;
    result.groupState = loadGroupState(groupKey, settingsVersion, &groupStateError);
    if (!groupStateError.isEmpty())
    {
        assignError(error, groupStateError);
        return std::nullopt;
    }

    if (!loadMetricRecords(groupKey, settingsVersion, result.frameMetrics, error))
    {
        return std::nullopt;
    }

    if (!loadMatchedSources(groupKey, settingsVersion, result.matchedSources, error))
    {
        return std::nullopt;
    }

    if (result.groupState.has_value() && !result.groupState->detectionHash.trimmed().isEmpty())
    {
        if (!loadStarCacheForGroup(groupKey, result.groupState->detectionHash, result.starCacheByFrame, error))
        {
            return std::nullopt;
        }
    }

    std::unordered_map<QString, RegistrationMetricRecord> metricByFrame;
    metricByFrame.reserve(result.frameMetrics.size());
    std::unordered_map<QString, stage_utils::ParsedTransformRecord> transformsByFrame;
    transformsByFrame.reserve(result.frameMetrics.size());
    std::unordered_map<QString, const RegistrationStarCacheEntry*> starCacheByIdentity;
    starCacheByIdentity.reserve(result.starCacheByFrame.size());
    for (const auto& [_, entry] : result.starCacheByFrame)
    {
        starCacheByIdentity.insert_or_assign(entry.frameId.identityKey(), &entry);
    }

    for (const auto& record : result.frameMetrics)
    {
        const QString frameKey = record.frameId.identityKey();
        metricByFrame[frameKey] = record;

        if (record.transformJson.trimmed().isEmpty())
        {
            continue;
        }

        // Each record is parsed once here, as it is read; one of another format is refused.
        QString parseError;
        std::optional<stage_utils::ParsedTransformRecord> parsed =
            stage_utils::parseTransformRecord(record.transformJson, parseError);
        if (!parsed.has_value())
        {
            assignError(error, QStringLiteral("Failed to parse persisted registration transform for '%1': %2")
                                   .arg(record.frameId.canonicalPath(), parseError));
            return std::nullopt;
        }

        transformsByFrame.emplace(frameKey, std::move(*parsed));
    }

    std::vector<FrameId> targetFrames;
    targetFrames.reserve(currentFrames.size());
    if (!currentFrames.empty())
    {
        targetFrames = currentFrames;
    }
    else
    {
        for (const auto& record : result.frameMetrics)
        {
            targetFrames.push_back(record.frameId);
        }

        std::vector<QString> starOnlyFrames;
        starOnlyFrames.reserve(result.starCacheByFrame.size());
        for (const auto& [_, entry] : result.starCacheByFrame)
        {
            if (metricByFrame.find(entry.frameId.identityKey()) == metricByFrame.end())
            {
                starOnlyFrames.push_back(entry.frameId.canonicalPath());
            }
        }
        std::sort(starOnlyFrames.begin(), starOnlyFrames.end());
        for (const auto& frameId : starOnlyFrames)
        {
            targetFrames.emplace_back(frameId);
        }
    }

    RegisteredFrameSet registered;

    QString referenceFrameId;
    QString referenceFrameIdentity;
    if (result.groupState.has_value())
    {
        referenceFrameId = canonicalFrameKey(result.groupState->referenceFrameId);
        const auto referenceFrame = persistedFrameId(referenceFrameId);
        referenceFrameIdentity = referenceFrame.has_value() ? referenceFrame->identityKey() : QString();
    }

    for (const auto& frame : targetFrames)
    {
        const QString frameId = frame.canonicalPath();
        const QString frameIdentity = frame.identityKey();
        const auto metricIt = metricByFrame.find(frameIdentity);
        const auto starCacheIt = starCacheByIdentity.find(frameIdentity);

        // Skip frames that have no persisted data at all — they are new/unregistered
        // and should not appear in the registered data map.
        if (metricIt == metricByFrame.end() && starCacheIt == starCacheByIdentity.end())
        {
            continue;
        }

        RegisteredFrame image;
        image.sourcePath = (starCacheIt != starCacheByIdentity.end() && starCacheIt->second != nullptr &&
                            !starCacheIt->second->inputPath.trimmed().isEmpty())
                               ? starCacheIt->second->inputPath
                               : frameId;

        if (starCacheIt != starCacheByIdentity.end() && starCacheIt->second != nullptr)
        {
            QString parseError;
            std::optional<std::vector<RegisteredStar>> stars =
                deserializeRegistrationStarCacheBlob(starCacheIt->second->starsBlob, parseError);
            if (!stars.has_value())
            {
                LOG_WARNING("RegistrationMetricsStateLoader: failed to parse star cache for frame '{}': {}",
                            frameId.toStdString(), parseError.toStdString());
            }
            else
            {
                image.stars = std::move(*stars);
            }

            image.metrics.stars = starCacheIt->second->starsCount;
            image.metrics.fwhm = starCacheIt->second->fwhm;
            image.metrics.snr = starCacheIt->second->snr;
            image.metrics.eccentricity = registrationEccentricityFromRoundness(starCacheIt->second->roundness);
            image.noise = starCacheIt->second->noise;
        }

        if (metricIt != metricByFrame.end())
        {
            const auto& metric = metricIt->second;
            const int cachedStars = image.metrics.stars;
            image.metrics = metric.metrics;
            image.metrics.stars = (std::max)(cachedStars, metric.metrics.stars);
            image.quality = frameQualityScore(metric.metrics);
            // Metric rows win over the star cache: they are written per settings version and are
            // what the stacking stage reads, so keeping the two in sync avoids a frame appearing
            // weightable in one path and not the other.
            if (metric.noise.background_noise > 0.0)
            {
                image.noise.background_noise = metric.noise.background_noise;
            }
            if (metric.noise.global_mad > 0.0)
            {
                image.noise.global_mad = metric.noise.global_mad;
            }
            if (metric.noise.global_scale > 0.0)
            {
                image.noise.global_scale = metric.noise.global_scale;
            }
            image.failed = !metric.metrics.succeeded;

            if (const auto transformIt = transformsByFrame.find(frameIdentity); transformIt != transformsByFrame.end())
            {
                image.transform = transformIt->second.transform;
                image.distortion = transformIt->second.provenance;
            }
            else if (!metric.metrics.succeeded || frameIdentity == referenceFrameIdentity)
            {
                image.transform = an::RegistrationTransform{};
            }
        }
        else
        {
            image.failed = true;
            if (frameIdentity == referenceFrameIdentity)
            {
                image.transform = an::RegistrationTransform{};
                image.failed = false;
            }
        }

        QString key = image.sourcePath;
        registered.frames.insert_or_assign(std::move(key), std::move(image));
    }

    QString resolvedReferencePath = referenceRegistrationPath(referenceFrameId, starCacheByIdentity);
    if (resolvedReferencePath.trimmed().isEmpty())
    {
        for (const auto& frame : targetFrames)
        {
            const QString frameId = frame.canonicalPath();
            const QString frameIdentity = frame.identityKey();
            const auto transformIt = transformsByFrame.find(frameIdentity);
            const auto metricIt = metricByFrame.find(frameIdentity);
            if (transformIt == transformsByFrame.end() || metricIt == metricByFrame.end() ||
                !metricIt->second.metrics.succeeded)
            {
                continue;
            }

            if (isIdentityTransform(transformIt->second.transform))
            {
                resolvedReferencePath = referenceRegistrationPath(frameId, starCacheByIdentity);
                referenceFrameId = frameId;
                referenceFrameIdentity = frameIdentity;
                break;
            }
        }
    }

    if (!resolvedReferencePath.trimmed().isEmpty() &&
        registered.frames.find(resolvedReferencePath) == registered.frames.end())
    {
        resolvedReferencePath.clear();
    }

    if (resolvedReferencePath.trimmed().isEmpty() && !targetFrames.empty())
    {
        referenceFrameId = targetFrames.front().canonicalPath();
        referenceFrameIdentity = targetFrames.front().identityKey();
        resolvedReferencePath = referenceRegistrationPath(referenceFrameId, starCacheByIdentity);
    }

    registered.referencePath = resolvedReferencePath;
    result.registeredFrames = std::move(registered);
    return result;
}
} // namespace an::ui

namespace an::ui::processes
{

an::Result<IntegrationMatchedSources> readIntegrationMatchedSources(const QString& metricsDatabasePath,
                                                                    const QString& registrationGroupKey)
{
    const auto failure = [&](const QString& reason) {
        return an::Status::failure(an::ErrorCode::ExternalFailure,
                                   an::StatusDetail{.message = QStringLiteral("the registration database '%1' %2")
                                                                   .arg(metricsDatabasePath, reason)
                                                                   .toStdString()});
    };
    RegistrationMetricsStateLoader loader(metricsDatabasePath, QStringLiteral("SpccIntegrationHandoff"));
    QString error;
    if (!loader.open(&error))
    {
        return failure(QStringLiteral("cannot be opened: %1").arg(error));
    }
    std::vector<RegistrationMetricsGroupVersion> states;
    if (!loader.loadAvailableStates(states, &error))
    {
        return failure(QStringLiteral("cannot be read: %1").arg(error));
    }
    // The states come newest first; the group's latest settings version is the first of its rows.
    std::optional<int> latest;
    for (const RegistrationMetricsGroupVersion& state : states)
    {
        if (state.groupKey == registrationGroupKey && (!latest.has_value() || state.settingsVersion > *latest))
        {
            latest = state.settingsVersion;
        }
    }
    if (!latest.has_value())
    {
        return an::Status::failure(
            an::ErrorCode::EmptySelection,
            an::StatusDetail{.message = QStringLiteral("the registration database '%1' holds no settings version of "
                                                       "group '%2'")
                                            .arg(metricsDatabasePath, registrationGroupKey)
                                            .toStdString()});
    }
    std::vector<RegistrationMatchedSourceRecord> records;
    if (!loader.loadMatchedSources(registrationGroupKey, *latest, records, &error))
    {
        return failure(QStringLiteral("cannot be read: %1").arg(error));
    }
    IntegrationMatchedSources sources{.settingsVersion = *latest};
    sources.references.reserve(records.size());
    for (RegistrationMatchedSourceRecord& record : records)
    {
        sources.references.push_back(std::move(record.reference));
    }
    return sources;
}

} // namespace an::ui::processes

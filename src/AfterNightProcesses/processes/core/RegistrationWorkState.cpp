// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whether a registration's recorded state still answers the current frames and settings: the analysis over the
/// registration database the standalone registration and the integration workflow both read.

#include "stdafx.hpp"
#include "processes/core/RegistrationWorkState.h"

#include "processes/ImageIntegration/workflow/ArtifactTracker.h"

#include <QFileInfo>
#include <QHash>
#include <QSqlQuery>

#include <algorithm>
#include <optional>

namespace registration_work_detail
{
void appendReason(std::vector<RegistrationWorkReason>& reasons, RegistrationWorkReasonCode code, int count)
{
    reasons.push_back(RegistrationWorkReason{code, count});
}

[[nodiscard]] std::unordered_set<QString> ignoredFrameIdentities(const RegistrationWorkCurrentState& currentState)
{
    std::unordered_set<QString> identities;
    identities.reserve(currentState.ignoredFrameIds.size());
    for (const auto& storedFrameId : currentState.ignoredFrameIds)
    {
        if (storedFrameId.trimmed().isEmpty())
        {
            continue;
        }
        try
        {
            identities.insert(FrameId(storedFrameId).identityKey());
        }
        catch (...)
        {
        }
    }
    return identities;
}

[[nodiscard]] bool isIgnoredFrame(const std::unordered_set<QString>& ignoredIdentities, const FrameId& frame)
{
    return ignoredIdentities.contains(frame.identityKey());
}

[[nodiscard]] bool registrationHashMatches(const QString& previous_hash,
                                           const RegistrationWorkCurrentState& current_state)
{
    return previous_hash == current_state.registrationHash ||
           (current_state.allowPlainRegistrationHash && !current_state.plainRegistrationHash.isEmpty() &&
            previous_hash == current_state.plainRegistrationHash);
}

} // namespace registration_work_detail

namespace
{

using namespace registration_work_detail;

[[nodiscard]] bool cachedInputMatchesCurrentFile(const QString& path, const qint64 cached_size_bytes,
                                                 const qint64 cached_modified_at_ms,
                                                 const QString& cached_identity_token)
{
    if (cached_size_bytes < 0 || cached_modified_at_ms < 0)
    {
        return false;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() != cached_size_bytes ||
        info.lastModified().toMSecsSinceEpoch() != cached_modified_at_ms)
    {
        return false;
    }
    if (cached_identity_token.isEmpty())
    {
        return false;
    }
    const QString current_identity_token = ArtifactTracker::fileIdentityForPath(info.absoluteFilePath());
    return !current_identity_token.isEmpty() && current_identity_token == cached_identity_token;
}

[[nodiscard]] std::optional<FrameId> persistedFrameId(const QString& storedFrameId)
{
    if (storedFrameId.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    try
    {
        return FrameId(storedFrameId);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

[[nodiscard]] bool preferPersistedRow(const qint64 candidateUpdatedAt, const QString& candidateStoredFrameId,
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
    const bool candidateCanonical = candidateStoredFrameId == candidate->canonicalPath();
    const bool selectedCanonical = selectedStoredFrameId == selected->canonicalPath();
    if (candidateCanonical != selectedCanonical)
    {
        return candidateCanonical;
    }
    const int canonicalComparison =
        QString::compare(candidate->canonicalPath(), selected->canonicalPath(), Qt::CaseSensitive);
    return canonicalComparison != 0
               ? canonicalComparison < 0
               : QString::compare(candidateStoredFrameId, selectedStoredFrameId, Qt::CaseSensitive) < 0;
}

} // namespace

using namespace registration_work_detail;

RegistrationWorkAnalysis analyzeRegistrationWork(QSqlDatabase& db, const RegistrationWorkCurrentState& currentState)
{
    RegistrationWorkAnalysis analysis;
    const QString groupKey = currentState.groupKey;

    QSqlQuery stateQuery(db);
    stateQuery.prepare(QStringLiteral(
        "SELECT settings_version, detection_hash, registration_hash, reference_frame_id, light_files_hash, "
        "calibration_hash "
        "FROM registration_group_state WHERE group_key = :group_key ORDER BY settings_version DESC LIMIT 1"));
    stateQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!stateQuery.exec() || !stateQuery.next())
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::NoPreviousState);
        return analysis;
    }

    const int lastVersion = stateQuery.value(0).toInt();
    const QString previousDetectionHash = stateQuery.value(1).toString();
    const QString previousRegistrationHash = stateQuery.value(2).toString();
    const QString previousReference = stateQuery.value(3).toString();
    const QString previousFilesHash = stateQuery.value(4).toString();
    const QString previousCalibrationHash = stateQuery.value(5).toString();

    bool stateChangeRequiresFullRefresh = false;
    if (previousDetectionHash != currentState.detectionHash)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::DetectionSettingsChanged);
        stateChangeRequiresFullRefresh = true;
    }
    if (!registrationHashMatches(previousRegistrationHash, currentState))
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::RegistrationSettingsChanged);
        stateChangeRequiresFullRefresh = true;
    }
    if (!currentState.autoReferenceRuntime && previousReference != currentState.referenceFrameId)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::ReferenceChanged);
        stateChangeRequiresFullRefresh = true;
    }
    if (previousFilesHash != currentState.lightFilesHash)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::FrameListChanged);
    }
    if (currentState.compareCalibrationHash && previousCalibrationHash != currentState.calibrationHash)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::CalibrationChanged);
    }
    if (lastVersion != currentState.settingsVersion)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::SettingsVersionChanged);
    }
    if (stateChangeRequiresFullRefresh)
    {
        return analysis;
    }
    const auto ignoredFrameIdentityKeys = ignoredFrameIdentities(currentState);

    struct CachedStarRow
    {
        QString storedFrameId;
        QString inputPath;
        qint64 inputMtime = 0;
        qint64 inputSizeBytes = -1;
        qint64 inputModifiedAtMs = -1;
        QString inputIdentityToken;
        qint64 updatedAt = 0;
    };
    QHash<QString, CachedStarRow> cachedStarByIdentity;
    QSqlQuery starsQuery(db);
    starsQuery.prepare(QStringLiteral(
        "SELECT frame_id, input_path, input_mtime, input_size_bytes, input_modified_at_ms, input_identity_token, "
        "updated_at FROM star_detection_cache WHERE group_key = :group_key AND detection_hash = :det"));
    starsQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    starsQuery.bindValue(QStringLiteral(":det"), currentState.detectionHash);
    if (!starsQuery.exec())
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::StarCacheUnavailable);
        return analysis;
    }

    while (starsQuery.next())
    {
        const QString storedFrameId = starsQuery.value(0).toString();
        const auto frameId = persistedFrameId(storedFrameId);
        if (!frameId.has_value())
        {
            continue;
        }
        CachedStarRow candidate{storedFrameId,
                                starsQuery.value(1).toString(),
                                starsQuery.value(2).toLongLong(),
                                starsQuery.value(3).toLongLong(),
                                starsQuery.value(4).toLongLong(),
                                starsQuery.value(5).toString(),
                                starsQuery.value(6).toLongLong()};
        const auto selected = cachedStarByIdentity.constFind(frameId->identityKey());
        if (selected == cachedStarByIdentity.cend() || preferPersistedRow(candidate.updatedAt, candidate.storedFrameId,
                                                                          selected->updatedAt, selected->storedFrameId))
        {
            cachedStarByIdentity.insert(frameId->identityKey(), std::move(candidate));
        }
    }

    int missingStars = 0;
    int changedInputs = 0;
    for (const auto& frame : currentState.lightFiles)
    {
        if (isIgnoredFrame(ignoredFrameIdentityKeys, frame))
        {
            continue;
        }

        const auto cached = cachedStarByIdentity.constFind(frame.identityKey());
        if (cached == cachedStarByIdentity.cend())
        {
            ++missingStars;
            continue;
        }

        const QString input_path = cached->inputPath.trimmed().isEmpty() ? frame.canonicalPath() : cached->inputPath;
        if (!cachedInputMatchesCurrentFile(input_path, cached->inputSizeBytes, cached->inputModifiedAtMs,
                                           cached->inputIdentityToken))
            ++changedInputs;
    }

    if (missingStars > 0)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::MissingCachedStars, missingStars);
    }
    if (changedInputs > 0)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::ChangedInputs, changedInputs);
    }

    struct CachedMetricRow
    {
        QString storedFrameId;
        bool succeeded = false;
        qint64 updatedAt = 0;
    };
    QHash<QString, CachedMetricRow> metricByIdentity;
    QSqlQuery metricsQuery(db);
    metricsQuery.prepare(QStringLiteral("SELECT frame_id, succeeded, updated_at FROM registration_metrics "
                                        "WHERE group_key = :group_key AND settings_version = :v"));
    metricsQuery.bindValue(QStringLiteral(":group_key"), groupKey);
    metricsQuery.bindValue(QStringLiteral(":v"), currentState.settingsVersion);
    if (metricsQuery.exec())
    {
        while (metricsQuery.next())
        {
            const QString storedFrameId = metricsQuery.value(0).toString();
            const auto frameId = persistedFrameId(storedFrameId);
            if (!frameId.has_value())
            {
                continue;
            }
            CachedMetricRow candidate{storedFrameId, metricsQuery.value(1).toInt() != 0,
                                      metricsQuery.value(2).toLongLong()};
            const auto selected = metricByIdentity.constFind(frameId->identityKey());
            if (selected == metricByIdentity.cend() || preferPersistedRow(candidate.updatedAt, candidate.storedFrameId,
                                                                          selected->updatedAt, selected->storedFrameId))
            {
                metricByIdentity.insert(frameId->identityKey(), std::move(candidate));
            }
        }
    }

    int missingMetrics = 0;
    int failedMetrics = 0;
    for (const auto& frame : currentState.lightFiles)
    {
        if (isIgnoredFrame(ignoredFrameIdentityKeys, frame))
        {
            continue;
        }

        const auto metric = metricByIdentity.constFind(frame.identityKey());
        if (metric == metricByIdentity.cend())
        {
            ++missingMetrics;
        }
        else if (!metric->succeeded)
        {
            ++failedMetrics;
        }
    }

    if (missingMetrics > 0)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::MissingMetrics, missingMetrics);
    }
    if (failedMetrics > 0)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::FailedMetrics, failedMetrics);
    }

    analysis.needsWork = !analysis.reasons.empty();
    return analysis;
}

QString describeRegistrationWorkReason(const RegistrationWorkReason& reason)
{
    switch (reason.code)
    {
    case RegistrationWorkReasonCode::NoPreviousState:
        return QStringLiteral("No previous state");
    case RegistrationWorkReasonCode::DetectionSettingsChanged:
        return QStringLiteral("Detection settings changed");
    case RegistrationWorkReasonCode::RegistrationSettingsChanged:
        return QStringLiteral("Registration settings changed");
    case RegistrationWorkReasonCode::CalibrationChanged:
        return QStringLiteral("Calibration assignments changed");
    case RegistrationWorkReasonCode::ReferenceChanged:
        return QStringLiteral("Reference changed");
    case RegistrationWorkReasonCode::FrameListChanged:
        return QStringLiteral("Frame list changed");
    case RegistrationWorkReasonCode::SettingsVersionChanged:
        return QStringLiteral("Settings version changed");
    case RegistrationWorkReasonCode::StarCacheUnavailable:
        return QStringLiteral("Star cache missing");
    case RegistrationWorkReasonCode::MissingCachedStars:
        return QStringLiteral("%1 frame(s) missing cached stars").arg(reason.count);
    case RegistrationWorkReasonCode::ChangedInputs:
        return QStringLiteral("%1 frame(s) changed on disk").arg(reason.count);
    case RegistrationWorkReasonCode::MissingMetrics:
        return QStringLiteral("%1 frame(s) missing metrics").arg(reason.count);
    case RegistrationWorkReasonCode::FailedMetrics:
        return QStringLiteral("%1 frame(s) failed registration").arg(reason.count);
    }

    return QStringLiteral("Unknown");
}

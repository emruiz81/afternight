// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whether a registration's recorded state still answers the current frames and settings: the reasons it does not, the
/// state the caller states, and the analysis over the registration database the standalone registration and the
/// integration workflow both read. Nothing here reaches the integration's manifest or model.
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/FrameId.h"

#include <QSqlDatabase>
#include <QString>

#include <unordered_set>
#include <vector>

enum class RegistrationWorkReasonCode
{
    NoPreviousState,
    DetectionSettingsChanged,
    RegistrationSettingsChanged,
    CalibrationChanged,
    ReferenceChanged,
    FrameListChanged,
    SettingsVersionChanged,
    StarCacheUnavailable,
    MissingCachedStars,
    ChangedInputs,
    MissingMetrics,
    FailedMetrics
};

struct RegistrationWorkReason
{
    RegistrationWorkReasonCode code = RegistrationWorkReasonCode::NoPreviousState;
    int count = 0;
};

struct RegistrationWorkAnalysis
{
    bool needsWork = true;
    std::vector<RegistrationWorkReason> reasons;
};

struct RegistrationWorkCurrentState
{
    QString groupKey;
    int settingsVersion = 0;
    QString detectionHash;
    QString registrationHash;
    /// The registration hash without the plate-solve settings - the one a standalone registration records - which a
    /// group's recorded state also matches while `allowPlainRegistrationHash` holds.
    QString plainRegistrationHash;
    QString calibrationHash;
    QString referenceFrameId;
    QString lightFilesHash;
    /// Whether the plate-solve settings are the defaults, which leave the two hashes nothing to tell apart.
    bool allowPlainRegistrationHash = false;
    bool autoReferenceRuntime = false;
    bool compareCalibrationHash = false;
    std::vector<FrameId> lightFiles;
    std::unordered_set<QString> ignoredFrameIds;
};

/// Whether the recorded state of a registration group answers the state the caller states: its settings, its
/// reference, its frames, each frame's cached stars against the file as it is now, and each frame's metrics.
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationWorkAnalysis
analyzeRegistrationWork(QSqlDatabase& db, const RegistrationWorkCurrentState& currentState);

/// A person's words for a reason.
[[nodiscard]] QString describeRegistrationWorkReason(const RegistrationWorkReason& reason);

/// The steps the analyses over the database and over a preparation snapshot share.
namespace registration_work_detail
{

/// Appends a reason.
void appendReason(std::vector<RegistrationWorkReason>& reasons, RegistrationWorkReasonCode code, int count = 0);

/// The identities of the frames the state ignores.
[[nodiscard]] std::unordered_set<QString> ignoredFrameIdentities(const RegistrationWorkCurrentState& currentState);

/// Whether a frame is ignored.
[[nodiscard]] bool isIgnoredFrame(const std::unordered_set<QString>& ignoredIdentities, const FrameId& frame);

/// Whether a recorded registration hash answers the state's.
[[nodiscard]] bool registrationHashMatches(const QString& previous_hash,
                                           const RegistrationWorkCurrentState& current_state);

} // namespace registration_work_detail

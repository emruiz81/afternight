// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file StandaloneRegistrationPersistence.h
 * @brief Shared persistence bridge for standalone image registration sessions.
 *
 * A standalone registration records its frames in the integration workflow's registration database, under the group
 * key the workflow would give its frames when they form one group, so a later integration of the same lights reuses
 * the stars and transforms it found. Each frame is the `FrameInfo` the integration's import reads from its header.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/core/RegistrationMetricsPersistenceTypes.h"
#include "processes/core/WorkflowSettingsRecords.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace an::ui
{
/**
 * @brief Derived persistence contract for one standalone registration session.
 */
struct StandaloneRegistrationPersistenceContext
{
    QString outputRoot;
    QString databasePath;
    QString groupKey;
    int settingsVersion = 0;
    QString detectionHash;
    QString registrationHash;
    QString referenceFrameId;
    QString lightFilesHash;
    QString calibrationHash;
    QString outputWcsJson;
    bool interoperableGroup = false;
};

/**
 * @brief Reconstructed standalone registration state loaded from `registration_metrics.db`.
 */
struct StandaloneRegistrationPersistenceState
{
    StandaloneRegistrationPersistenceContext context;
    std::optional<RegistrationGroupState> groupState;
    std::vector<RegistrationMetricRecord> frameMetrics;
    std::unordered_map<QString, RegistrationMetricRecord> metricsByFrameId;
    std::unordered_map<QString, RegisteredFrame> registeredImagesByFrameId;
    RegisteredFrameSet registeredData;
};

/**
 * @brief Full-session reuse decision for cached standalone registration state.
 */
struct StandaloneRegistrationReuseCheck
{
    bool reusable = false;
    bool starDataReusable = false;
    QString reason;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<StandaloneRegistrationPersistenceContext>
buildStandaloneRegistrationPersistenceContext(const QString& outputRoot, const std::vector<FrameInfo>& images,
                                              const RegistrationSettings& settings, QString* error = nullptr);

[[nodiscard]] AFTERNIGHTPROCESSES_API bool
standaloneRegistrationStateMatchesContext(const StandaloneRegistrationPersistenceContext& context,
                                          const RegistrationGroupState& state);

[[nodiscard]] AFTERNIGHTPROCESSES_API bool
persistStandaloneRegistrationState(const StandaloneRegistrationPersistenceContext& context,
                                   const std::vector<FrameInfo>& images, const RegisteredFrameSet& registeredData,
                                   QString* error = nullptr);

[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<StandaloneRegistrationPersistenceState>
loadStandaloneRegistrationState(const StandaloneRegistrationPersistenceContext& context,
                                const std::vector<FrameInfo>& currentImages, QString* error = nullptr);

[[nodiscard]] AFTERNIGHTPROCESSES_API StandaloneRegistrationReuseCheck checkReusableStandaloneRegistrationState(
    const StandaloneRegistrationPersistenceContext& context, const std::vector<FrameInfo>& currentImages,
    const std::unordered_set<QString>& ignoredFrameIds = {}, QString* error = nullptr);

[[nodiscard]] AFTERNIGHTPROCESSES_API bool
clearStandaloneRegistrationState(const StandaloneRegistrationPersistenceContext& context, QString* error = nullptr);
} // namespace an::ui

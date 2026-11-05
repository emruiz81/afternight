// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationWorkAnalyzer.h"

#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/core/RegistrationWorkState.h"

#include <QFileInfo>
#include <QHash>
#include <QSqlQuery>

#include <algorithm>
#include <optional>

namespace
{

using namespace registration_work_detail;

[[nodiscard]] bool
registrationGroupHasStarsPlusCometExperimentalConsumer(const WorkflowManifest& manifest,
                                                       const WorkflowManifest::RegistrationGroup& registration_group)
{
    if (ImageIntegration::registrationRequestsStarsPlusCometExperimentalOutput(registration_group.settings))
        return true;

    const QString registration_key = registration_group.groupKey.toString();
    return std::any_of(manifest.integrationGroups.cbegin(), manifest.integrationGroups.cend(),
                       [&](const WorkflowManifest::IntegrationGroup& group) {
                           return group.registrationGroupKey.toString() == registration_key &&
                                  ImageIntegration::isStarsPlusCometExperimentalStackingMode(
                                      group.stacking.cometStackingMode);
                       });
}

[[nodiscard]] RegistrationWorkCurrentState currentStateForGroup(const WorkflowManifest::RegistrationGroup& group)
{
    using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
    using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
    using ImageIntegration::RegistrationHashUtils::registrationTransformHash;

    RegistrationWorkCurrentState current_state;
    current_state.groupKey = group.groupKey.toString();
    current_state.settingsVersion = group.settings.version;
    current_state.detectionHash = registrationDetectionHash(group.settings);
    current_state.registrationHash = registrationTransformHash(group.settings);
    current_state.referenceFrameId =
        group.referenceFrame.has_value() ? group.referenceFrame->canonicalPath() : QString();
    current_state.lightFilesHash = hashFrameIdList(group.lightFiles);
    current_state.lightFiles = group.lightFiles;
    return current_state;
}

[[nodiscard]] RegistrationWorkCurrentState currentStateForManifest(const WorkflowManifest& manifest,
                                                                   const WorkflowManifest::RegistrationGroup& group,
                                                                   const ExecutionPreparationSnapshot* snapshot)
{
    using ImageIntegration::RegistrationHashUtils::effectiveRegistrationHash;
    using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
    using ImageIntegration::RegistrationHashUtils::plateSolveSettingsAreDefault;
    using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
    using ImageIntegration::RegistrationHashUtils::registrationTransformHash;

    const QString api_key_hash = manifest.astrometryApiKeyHash.trimmed().isEmpty()
                                     ? ImageIntegration::astrometryApiKeyHash(manifest.plateSolve.astrometryApiKey)
                                     : manifest.astrometryApiKeyHash.trimmed();
    const QString configured_method =
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(group.settings.registrationMethod);
    const bool stars_plus_comet_star_base_required =
        configured_method == QStringLiteral("comet_tracking") &&
        registrationGroupHasStarsPlusCometExperimentalConsumer(manifest, group);
    const QString runtime_method =
        stars_plus_comet_star_base_required ? QStringLiteral("star_matching") : configured_method;

    RegistrationWorkCurrentState current_state;
    current_state.groupKey = group.groupKey.toString();
    current_state.settingsVersion = group.settings.version;
    current_state.detectionHash = registrationDetectionHash(group.settings);
    current_state.registrationHash = effectiveRegistrationHash(group.settings, manifest.plateSolve, api_key_hash);
    current_state.plainRegistrationHash = registrationTransformHash(group.settings);
    current_state.allowPlainRegistrationHash = plateSolveSettingsAreDefault(manifest.plateSolve, api_key_hash);
    current_state.autoReferenceRuntime =
        group.settings.autoReferenceDuringRegistration &&
        ImageIntegration::RegistrationSettingsUtils::usesStarDetection(runtime_method) &&
        !stars_plus_comet_star_base_required;
    current_state.referenceFrameId = !current_state.autoReferenceRuntime && group.referenceFrame.has_value()
                                         ? group.referenceFrame->canonicalPath()
                                         : QString();
    current_state.lightFilesHash = hashFrameIdList(group.lightFiles);
    current_state.calibrationHash =
        computeLightsCalibrationHashForSubset(manifest.lightsCalibration, group.lightFiles, snapshot);
    current_state.compareCalibrationHash = true;
    current_state.lightFiles = group.lightFiles;
    return current_state;
}
} // namespace

using namespace registration_work_detail;

RegistrationWorkAnalysis analyzeRegistrationWork(QSqlDatabase& db, const WorkflowManifest::RegistrationGroup& group)
{
    return analyzeRegistrationWork(db, currentStateForGroup(group));
}

RegistrationWorkAnalysis analyzeRegistrationWork(QSqlDatabase& db, const WorkflowManifest& manifest,
                                                 const WorkflowManifest::RegistrationGroup& group)
{
    return analyzeRegistrationWork(db, currentStateForManifest(manifest, group, nullptr));
}

RegistrationWorkAnalysis analyzeRegistrationWork(const ExecutionPreparationSnapshot& snapshot,
                                                 const RegistrationWorkCurrentState& currentState)
{
    RegistrationWorkAnalysis analysis;
    const auto* previous = snapshot.latestRegistrationGroupState(currentState.groupKey);
    if (previous == nullptr)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::NoPreviousState);
        return analysis;
    }

    bool state_change_requires_full_refresh = false;
    if (previous->detectionHash != currentState.detectionHash)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::DetectionSettingsChanged);
        state_change_requires_full_refresh = true;
    }
    if (!registrationHashMatches(previous->registrationHash, currentState))
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::RegistrationSettingsChanged);
        state_change_requires_full_refresh = true;
    }
    if (!currentState.autoReferenceRuntime && previous->referenceFrameId != currentState.referenceFrameId)
    {
        appendReason(analysis.reasons, RegistrationWorkReasonCode::ReferenceChanged);
        state_change_requires_full_refresh = true;
    }
    if (previous->lightFilesHash != currentState.lightFilesHash)
        appendReason(analysis.reasons, RegistrationWorkReasonCode::FrameListChanged);
    if (currentState.compareCalibrationHash && previous->calibrationHash != currentState.calibrationHash)
        appendReason(analysis.reasons, RegistrationWorkReasonCode::CalibrationChanged);
    if (previous->settingsVersion != currentState.settingsVersion)
        appendReason(analysis.reasons, RegistrationWorkReasonCode::SettingsVersionChanged);
    if (state_change_requires_full_refresh)
        return analysis;
    const auto ignored_frame_identity_keys = ignoredFrameIdentities(currentState);

    int missing_stars = 0;
    int changed_inputs = 0;
    for (const auto& frame : currentState.lightFiles)
    {
        if (isIgnoredFrame(ignored_frame_identity_keys, frame))
            continue;
        const auto* cached_stamp =
            snapshot.starCacheInputStamp(currentState.groupKey, currentState.detectionHash, frame);
        if (cached_stamp == nullptr)
        {
            ++missing_stars;
            continue;
        }
        const QString input_path =
            cached_stamp->inputPath.trimmed().isEmpty() ? frame.canonicalPath() : cached_stamp->inputPath;
        const auto* stamp = snapshot.fileStamp(input_path);
        if (cached_stamp->sizeBytes < 0 || cached_stamp->modifiedAtMs < 0 || stamp == nullptr || !stamp->isFile ||
            stamp->sizeBytes != cached_stamp->sizeBytes || stamp->modifiedAtMs != cached_stamp->modifiedAtMs ||
            cached_stamp->identityToken.isEmpty() || stamp->identityToken.isEmpty() ||
            stamp->identityToken != cached_stamp->identityToken)
            ++changed_inputs;
    }
    if (missing_stars > 0)
        appendReason(analysis.reasons, RegistrationWorkReasonCode::MissingCachedStars, missing_stars);
    if (changed_inputs > 0)
        appendReason(analysis.reasons, RegistrationWorkReasonCode::ChangedInputs, changed_inputs);

    const auto* metrics = snapshot.registrationMetrics(currentState.groupKey, currentState.settingsVersion);
    int missing_metrics = 0;
    int failed_metrics = 0;
    for (const auto& frame : currentState.lightFiles)
    {
        if (isIgnoredFrame(ignored_frame_identity_keys, frame))
            continue;
        if (metrics == nullptr)
        {
            ++missing_metrics;
            continue;
        }
        const auto metric = metrics->constFind(frame);
        if (metric == metrics->cend())
            ++missing_metrics;
        else if (!metric->succeeded)
            ++failed_metrics;
    }
    if (missing_metrics > 0)
        appendReason(analysis.reasons, RegistrationWorkReasonCode::MissingMetrics, missing_metrics);
    if (failed_metrics > 0)
        appendReason(analysis.reasons, RegistrationWorkReasonCode::FailedMetrics, failed_metrics);

    analysis.needsWork = !analysis.reasons.empty();
    return analysis;
}

RegistrationWorkAnalysis analyzeRegistrationWork(const ExecutionPreparationSnapshot& snapshot,
                                                 const WorkflowManifest::RegistrationGroup& group)
{
    return analyzeRegistrationWork(snapshot, currentStateForGroup(group));
}

RegistrationWorkAnalysis analyzeRegistrationWork(const ExecutionPreparationSnapshot& snapshot,
                                                 const WorkflowManifest& manifest,
                                                 const WorkflowManifest::RegistrationGroup& group)
{
    return analyzeRegistrationWork(snapshot, currentStateForManifest(manifest, group, &snapshot));
}

std::vector<RegistrationGroupWorkAnalysis>
analyzeRegistrationGroupsWork(QSqlDatabase& db, const std::vector<WorkflowManifest::RegistrationGroup>& groups)
{
    std::vector<RegistrationGroupWorkAnalysis> analyses;
    analyses.reserve(groups.size());

    for (const auto& group : groups)
    {
        analyses.push_back(RegistrationGroupWorkAnalysis{group, analyzeRegistrationWork(db, group)});
    }

    return analyses;
}

std::vector<RegistrationGroupWorkAnalysis>
analyzeRegistrationGroupsWork(const ExecutionPreparationSnapshot& snapshot,
                              const std::vector<WorkflowManifest::RegistrationGroup>& groups)
{
    std::vector<RegistrationGroupWorkAnalysis> analyses;
    analyses.reserve(groups.size());
    for (const auto& group : groups)
        analyses.push_back(RegistrationGroupWorkAnalysis{group, analyzeRegistrationWork(snapshot, group)});
    return analyses;
}

std::vector<RegistrationGroupWorkAnalysis> analyzeRegistrationGroupsWork(const ExecutionPreparationSnapshot& snapshot,
                                                                         const WorkflowManifest& manifest)
{
    std::vector<RegistrationGroupWorkAnalysis> analyses;
    analyses.reserve(manifest.registrationGroups.size());
    for (const auto& group : manifest.registrationGroups)
        analyses.push_back(RegistrationGroupWorkAnalysis{group, analyzeRegistrationWork(snapshot, manifest, group)});
    return analyses;
}

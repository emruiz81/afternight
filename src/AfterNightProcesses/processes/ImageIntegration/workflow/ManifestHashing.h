// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The fingerprints the workflow decides reuse by: each derived family's settings, inputs and dependencies, and its
/// semantic identity - the engine generation and the family's revision - so an artifact another engine generation
/// computed, or one a stage computed by an older rule, never satisfies a fingerprint taken now.

#include "ExecutionPreparationSnapshot.h"
#include "WorkflowManifest.h"

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>

#include <QJsonObject>
#include <QString>

namespace ImageIntegration
{
/// The overlap graph the normalization stage solves: every pair touching the reference and a maximum spanning tree over
/// overlap area. Part of the overlap settings' fingerprint.
inline constexpr an::OverlapGraphPolicy kWorkflowOverlapGraphPolicy = an::OverlapGraphPolicy::ReferenceStarSpanningTree;
} // namespace ImageIntegration

// Serialize sub-settings into a QJsonObject for fingerprinting.
// Shared by ManifestHashing and individual workflow stages.
void addLocalNormalizationSettings(QJsonObject& obj, const LocalNormalizationSettings& s);
AFTERNIGHTPROCESSES_API void addOverlapNormalizationSettings(QJsonObject& obj, const OverlapNormalizationSettings& s);

// Compute the exact fingerprints persisted by the normalization stages.
// Frame order is significant because coefficient arrays are indexed by the effective input order.
AFTERNIGHTPROCESSES_API QString computeFrameFileStampFingerprint(
    const std::vector<FrameId>& frames, const ExecutionPreparationSnapshot* snapshot = nullptr);
std::vector<ExecutionPreparationSnapshot::FileStamp>
captureFrameFileStamps(const std::vector<FrameId>& frames, const ExecutionPreparationSnapshot* snapshot = nullptr);
AFTERNIGHTPROCESSES_API QString
computeFrameFileStampFingerprint(const std::vector<ExecutionPreparationSnapshot::FileStamp>& stamps);
bool frameFileStampsMatchCurrent(const std::vector<ExecutionPreparationSnapshot::FileStamp>& expected);
AFTERNIGHTPROCESSES_API QString computeNormalizationSettingsFingerprint(const WorkflowManifest::IntegrationGroup& group,
                                                                        const std::vector<FrameId>& referenceFrames,
                                                                        const QString& cometProductsFingerprint = {});

// Compute stable hashes for settings to detect changes
AFTERNIGHTPROCESSES_API QString computeStackingSettingsHash(const StackingSettings& settings);
QString computeRegistrationSettingsHash(const RegistrationSettings& settings);
QString computeCalibrationSettingsHash(const WorkflowManifest::CalibrationSettings& settings);
QString computeLightsCalibrationHash(const std::vector<WorkflowManifest::LightsCalibrationParams>& params,
                                     const ExecutionPreparationSnapshot* snapshot = nullptr);
AFTERNIGHTPROCESSES_API QString computeCalibratedLightArtifactHash(
    const WorkflowManifest::LightsCalibrationParams& params, const ExecutionPreparationSnapshot* snapshot = nullptr);
AFTERNIGHTPROCESSES_API QString
computeCalibratedLightArtifactHash(const WorkflowManifest::LightsCalibrationParams& params,
                                   const std::vector<ExecutionPreparationSnapshot::FileStamp>& orderedInputStamps);
AFTERNIGHTPROCESSES_API QString calibratedLightOutputPathFor(const QString& outputDirectory, const FrameId& lightFrame);
AFTERNIGHTPROCESSES_API QString chromaticAberrationLightOutputPathFor(const QString& outputDirectory,
                                                                      const FrameId& lightFrame);
/// The file a light's registration reads: its chromatic aberration output, its calibrated light, or the light itself,
/// the first that exists. With a snapshot, whether one exists is what the execution plan found before the run; a stage
/// that reads the file passes none, since the run's own calibration and registration write those outputs later.
QString registrationInputPathForFrame(const QString& outputDirectory, bool calibrateLightsInMemory,
                                      const WorkflowManifest::RegistrationGroup* registrationGroup,
                                      const FrameId& lightFrame,
                                      const ExecutionPreparationSnapshot* snapshot = nullptr);
[[nodiscard]] AFTERNIGHTPROCESSES_API bool
registrationGroupMaterializationUsesFits(const WorkflowManifest& manifest,
                                         const WorkflowManifest::RegistrationGroup& registrationGroup,
                                         const ExecutionPreparationSnapshot* snapshot = nullptr);
[[nodiscard]] AFTERNIGHTPROCESSES_API QString registrationGroupMaterializationContractFingerprint(
    const WorkflowManifest& manifest, const WorkflowManifest::RegistrationGroup& registrationGroup,
    const ExecutionPreparationSnapshot* snapshot = nullptr);

// Compute calibration hash for a subset of lights (e.g., those in a registration group).
// Returns empty string if no matching calibration params are found.
AFTERNIGHTPROCESSES_API QString computeLightsCalibrationHashForSubset(
    const std::vector<WorkflowManifest::LightsCalibrationParams>& allParams, const std::vector<FrameId>& lightFiles,
    const ExecutionPreparationSnapshot* snapshot = nullptr);
AFTERNIGHTPROCESSES_API QString computeLightsCalibrationHashForAssignments(
    const std::vector<const WorkflowManifest::LightsCalibrationParams*>& assignments,
    const ExecutionPreparationSnapshot* snapshot = nullptr);

// Compute stable hashes for stage artifacts (inputs + settings).
AFTERNIGHTPROCESSES_API QString computeCalibrationGroupHash(const WorkflowManifest& manifest,
                                                            const WorkflowManifest::CalibrationStage::Group& group,
                                                            const ExecutionPreparationSnapshot* snapshot = nullptr);
AFTERNIGHTPROCESSES_API QString computeIntegrationGroupHash(const WorkflowManifest& manifest,
                                                            const WorkflowManifest::IntegrationGroup& group,
                                                            const ExecutionPreparationSnapshot* snapshot = nullptr);
AFTERNIGHTPROCESSES_API QString computeStackedLightArtifactHash(
    const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group, const QString& inputsFingerprint,
    const QString& transformsFingerprint = {}, const QString& sourceDistortionFingerprint = {},
    const QString& cometProductsFingerprint = {}, const ExecutionPreparationSnapshot* snapshot = nullptr);

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// The engine-free helpers the workflow's stages share: progress, the calibration stacks' timing profile, the
/// one-line summaries of a group's settings, and the paths and orders the normalization and stacking stages agree on.

#include "processes/ImageIntegration/workflow/CancellationToken.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @namespace stage_utils
 * @brief Shared utilities for workflow stage implementations.
 *
 * Functions:
 * - reportStageProgress(): standardized progress updates
 */
namespace stage_utils
{
struct StackInputPreloadDiagnostics
{
    size_t metadataLoadCount = 0;
    size_t preflightDecodeCount = 0;
    size_t noPixelProbeCount = 0;
    size_t inconclusiveProbeCount = 0;
    size_t probeFailureCount = 0;
    size_t fallbackDecodeCount = 0;
    size_t fallbackDecodeFailureCount = 0;
    size_t decodedMetadataMismatchCount = 0;
    size_t peakFallbackImagesResident = 0;
    std::uint64_t fallbackSpillBytes = 0;
};

/// Durations from one preflight of a calibration stack's inputs, kept separate from the counters above on
/// purpose: every field of StackInputPreloadDiagnostics is reproducible run to run and a test may
/// assert it, while nothing here is. No test may assert a magnitude in this struct.
///
/// This exists because the calibration stacking stages - bias, dark and flat - do a per-frame
/// preflight that lights stacking does not do at all, and until it was timed nobody could say what it
/// cost. It is strictly serial by construction, so its share of a group is the first thing to look at
/// when a stack is slower per frame than it has any business being.
struct StackInputPreflightProfile
{
    /// Metadata read plus payload validation, once per input frame.
    std::uint64_t probe_nanoseconds = 0;
    /// The bounded fallback decode for frames whose probe could not validate the payload without one.
    /// Zero for every FITS input, which is every input this workflow produces.
    std::uint64_t fallback_nanoseconds = 0;
    /// Wall time of the whole call: the denominator the shares above are measured against.
    std::uint64_t total_nanoseconds = 0;
    /// Decoded payload bytes the preflight moved. A duration alone does not transfer between
    /// machines; paired with the bytes that moved through it, it does.
    std::uint64_t decoded_sample_bytes = 0;
};

/// One calibration stacking stage's timing, accumulated across its groups.
///
/// Bias, dark and flat stacking share this because they share the shape: a serial per-frame preflight
/// that lights stacking does not run at all, then the engine call, then the master write. Reporting
/// the same decomposition for all three is what makes it possible to say whether a difference between
/// them is the preflight or the stack.
struct CalibrationStackProfile
{
    StackInputPreflightProfile preflight;
    /// The engine's calibrate_and_stack()/stack() call.
    std::uint64_t stack_nanoseconds = 0;
    /// Encoding and writing the master.
    std::uint64_t write_nanoseconds = 0;
    /// Wall time of every group the stage ran: the denominator the shares above are measured against.
    std::uint64_t loop_nanoseconds = 0;
    /// Input frames that reached the stacker. Groups that reused an existing master are excluded, so
    /// a per-frame cost derived from this describes work actually done.
    std::uint64_t frames_stacked = 0;
};

/// Adds one group's preflight timings into a stage total.
void accumulateStackInputPreflightProfile(CalibrationStackProfile& total, const StackInputPreflightProfile& group);

/// "412 ms/frame | probe 1.2%, preload 47.3%, stack 38.1%, write 9.9%, other 3.5%", or an empty
/// string when nothing was stacked. Shares are of loop_nanoseconds, and whatever the named phases do
/// not add up to is reported as `other` rather than absorbed into an adjacent one.
[[nodiscard]] QString formatCalibrationStackProfile(const CalibrationStackProfile& profile);

/// The scale those shares are relative to. A percentage split is only actionable next to the payload
/// that produced it and the core count that was available to work on it.
[[nodiscard]] QString formatCalibrationStackThroughput(const CalibrationStackProfile& profile);

AFTERNIGHTPROCESSES_API void reportStageProgress(ProgressReporter& reporter, WorkflowStage stage, int group_index,
                                                 int total_groups, float stage_progress, const QString& operation,
                                                 const QString& current_file);

/**
 * @brief Reports progress with sub-step awareness for multi-phase operations.
 *
 * When a group has multiple internal phases (e.g., load → register → stack),
 * use this overload so the overall progress accounts for sub-step position
 * and does not reset between phases.
 *
 * @param current_sub_step 1-based index of the current sub-step.
 * @param total_sub_steps  Total number of sub-steps in the current group.
 * @param sub_step_name    Short label for the current sub-step (e.g., "Normalizing").
 */
AFTERNIGHTPROCESSES_API void reportStageProgress(ProgressReporter& reporter, WorkflowStage stage, int group_index,
                                                 int total_groups, float stage_progress, const QString& operation,
                                                 const QString& current_file, int current_sub_step, int total_sub_steps,
                                                 const QString& sub_step_name);

/**
 * @brief Logs progress for repetitive items without flooding the log.
 *
 * Logs at TRACE level. Only logs the first, last, and every Nth item.
 */
AFTERNIGHTPROCESSES_API void logBatchProgress(const QString& operation, int current, int total, int batchSize = 10);

/// Says warnings in a stage's log and keeps each of them in the stage's warnings once.
///
/// @param reporter the stage's reporter.
/// @param said the warnings.
/// @param warnings the stage's warnings, which receive each one they do not hold yet.
AFTERNIGHTPROCESSES_API void sayStageWarnings(ProgressReporter& reporter, const QStringList& said,
                                              QStringList& warnings);

/**
 * @brief Builds a compact summary for stacking settings.
 *
 * Example:
 * "Stack: Average + Winsorized Sigma Clipping | Weighting: None | Normalization: Global (Additive With Scaling, IKSS)"
 */
[[nodiscard]] QString describeStackingSettings(const StackingSettings& settings);

/**
 * @brief Builds a compact summary for normalization settings only.
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API QString describeNormalizationSettings(const StackingSettings& settings);

/**
 * @brief Builds a verbose reference-selection summary for stacking/normalization logs.
 */
[[nodiscard]] AFTERNIGHTPROCESSES_API QString describeReferenceSelection(const StackingSettings& settings,
                                                                         const std::vector<FrameId>& referenceFrames);

/**
 * @brief Builds a compact summary for drizzle settings.
 */
[[nodiscard]] QString describeDrizzleSettings(const WorkflowManifest::IntegrationGroup::DrizzleSettings& drizzle);

/// Canonical normalization-coefficient artifact path used by both producer and consumers.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString
normalizationArtifactPath(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group);

[[nodiscard]] bool isWorkflowOwnedOutputPath(const WorkflowManifest& manifest, const FrameId& frame);

struct RegistrationOutputPlaneExpectations
{
    int width = 0;
    int height = 0;
    bool sipCorrectionApplied = false;
};

[[nodiscard]] QString fingerprintRegistrationOutputWcs(const QString& output_wcs_json);

/// Translates integration-input reference indices into the deterministic
/// path-sorted order used by drizzle's registration data.
[[nodiscard]] bool buildDrizzleLocalNormalizationReferenceIndices(const std::vector<size_t>& input_reference_indices,
                                                                  const std::vector<FrameId>& registration_inputs,
                                                                  std::vector<size_t>& out_indices, QString& reason);

} // namespace stage_utils

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What the bias, dark and flat stages share: the preflight that keeps the frames a group can stack, the stack of
/// those frames - calibrated by the group's masters as they are read - into a master published whole and stamped,
/// and the run of a stage's groups around them.
///
/// **The preflight reads headers, not pixels.** Each frame is probed through the run's codec registry, its payload
/// validated where the codec can do so without decoding; a frame that cannot be probed, changes while it is probed, or
/// does not have the group's first readable frame's size, channels, representation and filter array is skipped and
/// reported against its own file, and the group goes on with the rest. The stack decodes every frame once, as it reads
/// it, so nothing is spilled to temporary files, and a frame that changed after it was probed is refused when it is
/// read.
///
/// **The stack is the engine's.** `executeStack` combines the frames, read by a source that decodes each whole frame
/// through the run's registry when the stack asks for it, every frame a fraction of its representation's full scale.
/// A dark or flat group's masters are applied on the way in by the stack's own calibration stage, so no calibrated
/// frame is ever held. A rejection needs three frames; a group of fewer is combined without it and says so.
///
/// **The master is published whole and stamped.** It is written as Float32 FITS through the output transaction with
/// the identity of the manifest entry it satisfies, so a later run decides its reuse from a stat or a header read, and
/// its record is made only after the write committed.

#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class WorkflowArtifactPublicationLedger;

namespace ImageIntegration
{
class WorkflowStageServices;
} // namespace ImageIntegration

namespace core
{
class CalibrationMasterSet;
} // namespace core

namespace stage_utils
{

/// The frames of a group the stack can read.
struct ReadableStackInputs
{
    /// The readable frames, in the group's order.
    std::vector<FrameId> inputFiles;
    /// Each readable frame's revision when it was probed, which its read is held to.
    std::vector<FileRevisionStamp> revisions;
    /// A failure that stops the group rather than skipping one frame.
    std::optional<IntegrationError> fatalError;
    /// Whether the run was stopped.
    bool cancelled = false;
};

/// Probes every frame of a group and keeps the ones the stack can read.
///
/// @param services the run's services.
/// @param inputFiles the group's frames.
/// @param frameType "bias", "dark" or "flat", for the messages.
/// @param errorReport receives one error per skipped frame, against its file.
/// @param stageWarnings receives one warning per skipped frame.
/// @param reporter the stage's reporter.
/// @param diagnostics receives the probe counts; may be null.
/// @param token the run's cancellation; may be null.
/// @param profile receives the probe time; may be null.
/// @return the readable frames.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReadableStackInputs
loadReadableStackInputs(const ImageIntegration::WorkflowStageServices& services, const std::vector<FrameId>& inputFiles,
                        const QString& frameType, IntegrationErrorReport& errorReport, QStringList& stageWarnings,
                        ProgressReporter& reporter, StackInputPreloadDiagnostics* diagnostics = nullptr,
                        CancellationToken* token = nullptr, StackInputPreflightProfile* profile = nullptr);

/// One calibration group's stack.
struct CalibrationStackRequest
{
    /// The stage, for its progress.
    WorkflowStage stage = WorkflowStage::None;
    /// The group's position among the stage's groups, one-based, and their count.
    int groupIndex = 0;
    int totalGroups = 0;
    /// What the stage is doing, for its progress.
    QString operation;
    /// The group, for the messages.
    QString groupLabel;
    /// The readable frames and their probed revisions.
    const ReadableStackInputs* inputs = nullptr;
    /// Where the master is published.
    QString outputPath;
    /// The group's stacking settings.
    StackingSettings stacking;
    /// Whether the normalization the group states is applied: a flat master's is; a bias or dark master is combined
    /// without one, and a normalization its group still states is left out and said.
    bool normalizes = true;
    /// The normalization a bias or dark group still states beside its settings, which its master leaves out.
    NormalizationMode statedNormalization = NormalizationMode::None;
    /// The bias subtracted from every frame on the way in, or none.
    std::optional<FrameId> biasMaster;
    /// The dark subtracted from every frame on the way in, or none.
    std::optional<FrameId> darkMaster;
    /// The fingerprint of the manifest entry the master satisfies, and the run's identity.
    QString artifactHash;
    QString runId;
};

/// Stacks a group's readable frames into its master and publishes it stamped.
///
/// @param services the run's services.
/// @param request the group's stack.
/// @param masters the stage's masters, read once for every group that names them.
/// @param stageWarnings receives what the stack decided that the person should know - a rejection a small group
///        cannot apply, a weighting a master does not use.
/// @param token the run's cancellation.
/// @param reporter the stage's reporter.
/// @param profile receives the stack and write time; may be null.
/// @return success; cancelled when the run was stopped; or the group's failure, the engine's as its cause and the file
///         it concerns named.
[[nodiscard]] AFTERNIGHTPROCESSES_API StageResult
stackCalibrationGroup(const ImageIntegration::WorkflowStageServices& services, const CalibrationStackRequest& request,
                      core::CalibrationMasterSet& masters, QStringList& stageWarnings, CancellationToken& token,
                      ProgressReporter& reporter, CalibrationStackProfile* profile = nullptr);

/// One calibration stage's run.
struct CalibrationStageRun
{
    /// The stage.
    WorkflowStage stage = WorkflowStage::None;
    /// The stage's name, as its log names it.
    QString stageName;
    /// Its frames' word: "bias", "dark" or "flat".
    QString frameType;
    /// The groups the run stacks.
    std::vector<const WorkflowManifest::CalibrationStage::Group*> groups;
    /// Whether the group's bias and dark masters are applied to its frames: the flat stage's, as the manifest names
    /// them; the bias and dark stages apply none.
    bool appliesBias = false;
    bool appliesDark = false;
    /// Whether a group's stated normalization is applied: the flat stage's; the bias and dark stages combine their
    /// masters without one.
    bool normalizes = true;
};

/// Runs a calibration stage's groups: an existing master used as it is, a current master reused, and every other group
/// probed, stacked and its master published and recorded.
///
/// @param run the stage and its groups.
/// @param manifest the run's manifest.
/// @param services the run's services; a stage run outside a workflow run has none and is refused.
/// @param counters the run's counters; may be null.
/// @param ledger the run's publication ledger; may be null.
/// @param token the run's cancellation.
/// @param reporter the stage's reporter.
/// @return the stage's result: success, a partial success naming the frames skipped, cancelled, or the first group's
///         failure.
[[nodiscard]] StageResult runCalibrationStage(const CalibrationStageRun& run, const WorkflowManifest& manifest,
                                              const ImageIntegration::WorkflowStageServices* services,
                                              WorkflowExecutionCounterAccumulator* counters,
                                              WorkflowArtifactPublicationLedger* ledger, CancellationToken& token,
                                              ProgressReporter& reporter);

} // namespace stage_utils

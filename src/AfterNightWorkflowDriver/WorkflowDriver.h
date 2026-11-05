// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file WorkflowDriver.h
 * @brief Runs one ImageIntegration workflow the way the product runs it, and measures it.
 *
 * A measurement compares two builds of the product's workflow on the same dataset, in separate processes, from two
 * deployments. This is the process each side of a pair launches. It builds an integration model from a pinned frame set
 * through the product's own import, then performs an accepted execution request through the product's own preparation
 * and controller - the full process's planned preparation or the Lite process's direct one, then
 * `WorkflowProgressController`, whose executor runs every stage, publishes the outputs, commits the run state and
 * writes the reports. Nothing is reimplemented here; the dialogs the product would show are the one thing left out.
 *
 * The measured region runs from the accepted request to the controller's `finished` signal. Across it the driver
 * records the wall time of preparation and execution, the executor's own phase and stage timings, the resident peak
 * the operating system reports with a sampler placing it in time, the workflow's cache and I/O counters, the engine's
 * own accounting - what its context's memory resource held at most and refused, and what the session's consumer
 * allowances held - and, when asked to cancel, the latency from the request to the finished signal. The outputs are
 * listed with their digests after the region, for the comparison to hold them to a contract before it accepts a
 * timing.
 *
 * Every engine call the run makes - the import's header probes, the preparation's header reads and every stage - is
 * made in the one `an::consumer::EngineSession` the caller opens, as the application's session is the one every window
 * of the product works in.
 */
#pragma once

#include "processes/ImageIntegration/model/FrameType.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class IntegrationDataModel;

namespace an::consumer
{
class EngineSession;
}

namespace ImageIntegrationLite
{
struct ProcessingOptions;
}

namespace an::measurement
{

/// Which product front end's workflow runs.
enum class DriverWorkflow
{
    Full,
    Lite
};

/// One run's request.
struct DriverOptions
{
    DriverWorkflow workflow = DriverWorkflow::Full;
    /// The corpus manifest that names the frame set: `corpus.json` of the host's corpus.
    QString corpusManifest;
    /// The frame set's id in it.
    QString frameSet;
    /// The directory the frame set's relative paths resolve against.
    QString corpusRoot;
    /// The workflow's output directory.
    QString outputDirectory;
    /// Where the result is written.
    QString resultPath;
    /// Zero for the product's settings; one raises the light stack's high rejection sigma by 0.1, which is the change
    /// an incremental case alternates so that each run re-stacks and reuses everything before the stack.
    int variant = 0;
    /// Zero for the workflow's own default width.
    int maxParallelCores = 0;
    /// Requests cancellation this long after execution starts, for the cancellation-latency case.
    std::optional<int> cancelAfterMs;
    /// Whether every frame's SHA-256 is checked against the manifest before the run.
    bool verifyDigests = false;
    /// The resident-memory sampler's interval.
    int samplingIntervalMs = 10;
    /// What the engine this driver was built against says of itself, written into the result as it is.
    QJsonObject engine;
};

struct DriverOptionsParse
{
    std::optional<DriverOptions> options;
    QString error;
};

/// Reads the command line, without the program name.
[[nodiscard]] DriverOptionsParse parseDriverOptions(const QStringList& arguments);

/// The command line's usage text.
[[nodiscard]] QString driverUsage();

/// One frame of a frame set, resolved against the corpus root.
struct FrameSetMember
{
    FrameType type = FrameType::Light;
    QString role;
    QString path;
    qint64 sizeBytes = 0;
    QString sha256;
};

/// A resolved frame set.
struct FrameSet
{
    QString id;
    /// The benchmark layer's identity: the SHA-256 over each role's name and its members' recorded digests, in order.
    QString identityDigest;
    /// The SHA-256 of the corpus manifest the set was read from.
    QString manifestSha256;
    std::vector<FrameSetMember> members;
    qint64 totalBytes = 0;
};

struct FrameSetRead
{
    std::optional<FrameSet> frameSet;
    QString error;
};

/// The identity of a frame-set entry of a corpus manifest, computed as the benchmark layer computes it.
[[nodiscard]] QString frameSetIdentityDigest(const QJsonObject& frameSetEntry);

/// Resolves a frame set: every member present at its recorded size - and, when asked, its recorded digest - or the
/// set refused whole.
[[nodiscard]] FrameSetRead readFrameSet(const QString& corpusManifest, const QString& frameSetId,
                                        const QString& corpusRoot, bool verifyDigests);

struct FrameImport
{
    int imported = 0;
    QStringList issues;
};

/// Adds a frame set to a model the way the product's per-type add buttons do: each role's files probed in `session`
/// from the import's own pool, typed as the role and applied as one batch.
[[nodiscard]] FrameImport importFrameSet(IntegrationDataModel& model, const FrameSet& frameSet,
                                         const an::consumer::EngineSession& session);

/// Applies a full-workflow variant to a model: variant one sets the light stack's high rejection sigma of every
/// integration group 0.1 above the default; variant zero leaves the product's settings.
void applyFullVariant(IntegrationDataModel& model, int variant);

/// Applies a Lite-workflow variant to the Lite processing options: variant one switches the light stack's weighting
/// between none and noise; variant zero leaves the options as they are.
void applyLiteVariant(ImageIntegrationLite::ProcessingOptions& options, int variant);

/// Runs one measured workflow in `session` and returns its result, which is also written to `options.resultPath`.
[[nodiscard]] QJsonObject runWorkflowMeasurement(const DriverOptions& options,
                                                 const an::consumer::EngineSession& session);

/// The process exit code a result stands for: 0 when the workflow completed, 3 when it was cancelled as asked, 2
/// otherwise.
[[nodiscard]] int exitCodeFor(const QJsonObject& result);

} // namespace an::measurement

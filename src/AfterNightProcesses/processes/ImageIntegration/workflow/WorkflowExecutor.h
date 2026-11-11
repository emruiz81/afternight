// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file WorkflowExecutor.h
 * @brief Orchestrates sequential execution of workflow stages.
 *
 * WorkflowExecutor manages the execution of the integration pipeline,
 * running stages in sequence, handling cancellation, aggregating results,
 * and providing progress callbacks.
 *
 * @par Execution Model
 * Stages are executed synchronously in the order they were added. Each stage
 * can be skipped based on the manifest configuration. The executor stops
 * on first fatal error or cancellation request.
 *
 * @par Thread Safety
 * The executor is designed to run in a worker thread. Use requestCancel()
 * from the UI thread for cooperative cancellation.
 *
 * @par Engine work
 * A run lends its stages the session its options name, the parallel-cores setting as the width of each stage
 * operation, and a snapshot of the RAW treatment with a codec registry built from it. Nothing it does changes a
 * process-wide setting: the viewer and every other reader keep their threads and their RAW treatment while it runs.
 *
 * @see IWorkflowStage, WorkflowManifest, StageResult
 */

#include "processes/ImageIntegration/workflow/CancellationToken.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowImageCache.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRawLoadPolicy.h"
#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include <QObject>
#include <QStringList>
#include <verdigris/wobjectdefs.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class IWorkflowRunArchive;

namespace an
{
class CodecRegistry;
} // namespace an

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

/**
 * @brief Log level for workflow messages.
 */
enum class WorkflowLogLevel
{
    Info,
    Warning,
    Error,
    Header,
    Progress,
    Detail,
    Success
};

// Register workflow types for Verdigris signal metadata.
W_REGISTER_ARGTYPE(WorkflowLogLevel)
W_REGISTER_ARGTYPE(WorkflowStage)
W_REGISTER_ARGTYPE(WorkflowProgress)

using WorkflowMonotonicClock = std::function<std::chrono::steady_clock::time_point()>;

/**
 * @brief Exclusive workflow preparation durations in milliseconds.
 *
 * The process/controller preparation path owns manifestBuildMs and
 * planAndValidationMs. WorkflowExecutor owns stage timing and the later
 * executor-tail phases. The two preparation fields are non-overlapping sibling
 * durations and may be summed through totalMs(); that derived parent total must
 * not be added to either child. These fields do not represent complete
 * user-start-to-ready time on their own because executor execution and
 * completion timing are stored separately.
 *
 * A default-constructed value means that a direct executor caller did not
 * supply preparation timing. captured distinguishes that case from a
 * legitimately measured preparation whose millisecond fields both round to
 * zero.
 */
struct WorkflowPreparationTiming
{
    bool captured = false; ///< True when the preparation path supplied measurements, including zero-millisecond ones
    qint64 manifestBuildMs = 0;
    qint64 planAndValidationMs = 0;

    [[nodiscard]] qint64 totalMs() const noexcept
    {
        return manifestBuildMs + planAndValidationMs;
    }

    [[nodiscard]] bool hasMeasurements() const noexcept
    {
        return captured;
    }
};

/**
 * @brief Exclusive executor-owned durations in milliseconds.
 *
 * stageExecutionMs, stateCommitMs, archiveMs, and reportMs are non-overlapping
 * sibling phases. totalDurationMs on WorkflowExecutor::Result is their parent
 * executor interval and must not be added to these children. A phase that was
 * skipped is represented by zero, as is an executed interval that rounds down
 * to zero milliseconds.
 */
struct WorkflowExecutorTiming
{
    bool captured = false;
    qint64 stageExecutionMs = 0;
    qint64 stateCommitMs = 0;
    qint64 archiveMs = 0;
    qint64 reportMs = 0;

    [[nodiscard]] qint64 exclusivePhasesTotalMs() const noexcept
    {
        return stageExecutionMs + stateCommitMs + archiveMs + reportMs;
    }
};

struct WorkflowExecutionOptions
{
    bool collectCounters = false;
    std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot;
    std::optional<std::uint64_t> workflowCacheCapacityBytes;

    /// The session the run's stages do their engine work in, and the frames' headers are read through; it outlives
    /// the run. Without one the stages are lent no services and the manifest's frame sizes are not checked.
    const an::consumer::EngineSession* session = nullptr;
};

/// Builds the archive one run commits through, for the run's output directory, its diagnostics and the session its
/// report's previews and figures are read in, which outlives the run and may be null.
using WorkflowRunArchiveFactory = std::function<std::unique_ptr<IWorkflowRunArchive>(
    const QString& outputDirectory, WorkflowExecutionCounterAccumulator* counters,
    const an::consumer::EngineSession* session)>;

/// Builds the codec registry one run reads through, from its RAW treatment.
using WorkflowCodecRegistryFactory =
    std::function<std::shared_ptr<const an::CodecRegistry>(const ImageIntegration::WorkflowRawPolicy& policy)>;

/**
 * @brief Orchestrates workflow stage execution.
 *
 * WorkflowExecutor runs a series of IWorkflowStage implementations in order,
 * aggregating results, handling errors, and providing progress feedback.
 *
 * @par Usage
 * @code
 * WorkflowExecutor executor;
 * executor.addStage(std::make_unique<BiasStackingStage>());
 * executor.addStage(std::make_unique<DarkStackingStage>());
 * // ... add more stages
 *
 * executor.setProgressCallback([](const Progress& p) {
 *     updateProgressBar(p.overallProgress);
 * });
 *
 * auto result = executor.execute(manifest);
 * if (!result.success) {
 *     handleError(result.errorMessage);
 * }
 * @endcode
 */
class AFTERNIGHTPROCESSES_API WorkflowExecutor : public QObject
{
    W_OBJECT(WorkflowExecutor)

public:
    using Progress = WorkflowProgress;
    using Stage = WorkflowStage;
    using ProgressCallback = std::function<void(const Progress&)>;
    using LogCallback = std::function<void(WorkflowLogLevel, const QString&)>;

    /**
     * @brief Aggregated result of workflow execution.
     */
    struct Result
    {
        struct StageTiming
        {
            QString stageName;
            QString status;
            int durationMs = 0;
            int groupsProcessed = 0;
        };
        struct CalibrationGroupResult
        {
            QString type;
            QString groupKey;
            QString masterPath;
            int inputCount = 0;
            bool success = false;
            /// Whether the stage found the master still valid and left it as it was. A group has a result whenever
            /// its stage ran, so this is what tells a master the run stacked from one it did not.
            bool reused = false;
        };
        /// What the session's engine counted over the run: its memory's high-water mark when the run ended and
        /// when it started - it never falls, so the first is the run's own whenever it rose inside the run - the
        /// bytes still allocated when it ended, and the allocations made and refused between the two.
        struct EngineMemory
        {
            std::uint64_t peakBytes = 0;
            std::uint64_t peakBytesBeforeRun = 0;
            std::uint64_t liveBytesAtEnd = 0;
            std::uint64_t allocations = 0;
            std::uint64_t bytesAllocated = 0;
            std::uint64_t deniedAllocations = 0;
        };

        bool success = false; ///< true if all stages completed (possibly with partial failures)
        bool cancelled = false; ///< true if cancelled by user
        bool partialSuccess = false; ///< true if completed but some frames failed
        QString errorMessage; ///< Error description if failed
        QStringList warnings; ///< All warnings from all stages
        QStringList errors; ///< Per-frame/per-group error messages collected during execution

        QStringList generatedBiasMasters; ///< Paths to generated bias masters
        QStringList generatedDarkMasters; ///< Paths to generated dark masters
        QStringList generatedFlatMasters; ///< Paths to generated flat masters
        QStringList generatedLightMasters; ///< Paths to final stacked images
        QStringList generatedRejectionMaps; ///< Paths to rejection map images

        std::vector<StageTiming> stageResults; ///< Per-stage execution stats
        std::vector<IntegrationGroupResult> integrationGroupResults; ///< Per-group execution results
        /// The integration groups the run left as they are because their masters were still valid: the ones an
        /// incremental run's plan left out, then the ones the stacking stage found up to date. None has a group
        /// result, and no count of the run's successful groups reads the list.
        std::vector<ReusedIntegrationGroup> reusedIntegrationGroups;
        std::vector<CalibrationGroupResult> calibrationGroupResults; ///< Per-calibration-group execution results
        std::vector<AlignmentGroupResult> alignmentGroupResults; ///< Per-group alignment results (SIP info)
        QString capturedLog; ///< Full log of the execution

        WorkflowPreparationTiming preparationTiming; ///< Process/controller-owned exclusive preparation timing
        WorkflowExecutorTiming executorTiming; ///< Executor-owned exclusive stage and completion phases
        std::optional<WorkflowExecutionCounters> executionCounters; ///< Present only when diagnostics were requested
        std::optional<WorkflowImageCacheDiagnostics> workflowCacheDiagnostics; ///< Optional P2-13 cache diagnostics
        std::optional<EngineMemory> engineMemory; ///< Present when the run had a session whose engine counts memory
        int totalDurationMs = 0; ///< Parent executor interval ending after all applicable completion work
        int warningCount = 0; ///< Total number of warnings

        QString htmlReportPath; ///< Path to the generated HTML report (empty if archiving failed)
    };

    explicit WorkflowExecutor(QObject* parent = nullptr);
    explicit WorkflowExecutor(WorkflowMonotonicClock monotonicClock, QObject* parent = nullptr);
    ~WorkflowExecutor() override;

    /** @brief Set callback for progress updates. */
    void setProgressCallback(ProgressCallback callback);

    /** @brief Set callback for log messages. */
    void setLogCallback(LogCallback callback);

    /** @brief Add a stage to the execution pipeline. */
    void addStage(std::unique_ptr<IWorkflowStage> stage);

    /** @brief Remove all stages. */
    void clearStages();

    /** @brief Set what builds each run's archive; without one a run commits nothing beyond its run state. */
    void setRunArchiveFactory(WorkflowRunArchiveFactory factory);

    /** @brief Set what builds each run's codec registry; without one the product registry is built. */
    void setCodecRegistryFactory(WorkflowCodecRegistryFactory factory);

    /**
     * @brief Execute all stages sequentially.
     * @param manifest Workflow configuration.
     * @param preparationTiming Exclusive preparation timing supplied by the process/controller.
     * @return Aggregated execution result.
     */
    [[nodiscard]] Result execute(const WorkflowManifest& manifest, WorkflowPreparationTiming preparationTiming = {},
                                 WorkflowExecutionOptions options = {});

    /** @brief Request cooperative cancellation. Thread-safe. */
    void requestCancel();

    /** @brief Check if cancellation was requested. */
    [[nodiscard]] bool isCancelRequested() const;

signals:
    void progressUpdated(const Progress& progress) W_SIGNAL(progressUpdated, progress);
    void logMessage(WorkflowLogLevel level, const QString& message) W_SIGNAL(logMessage, level, message);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

W_REGISTER_ARGTYPE(WorkflowExecutor::Result)

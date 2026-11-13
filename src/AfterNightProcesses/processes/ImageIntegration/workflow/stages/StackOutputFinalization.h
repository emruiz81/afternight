// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the lights stacking stage does around a master once it is stacked: the previous outputs moved aside for the
/// run's archive, the finalization phases timed, the maps beside the master made into images, and the preview the run's
/// report and the integration window show.
///
/// **The previous outputs are kept for the archive.** A master, its preview and its maps already on disk are moved to
/// a staged copy before the new ones are published, so the run's archive keeps the run they came from; a group that
/// fails or is stopped puts them back.
///
/// **A map is an image the person can open.** The stack counts, per output element, how many samples its rejection took
/// and how many it combined; a rejection map is written as that count over the group's frames, the coverage as the
/// count itself, and the variance, the signal-to-noise gain and the weight sum as the stack computed them.
///
/// **The preview is the display's presentation.** It is the master presented as the viewer presents an image - the
/// automatic stretch from the display's own constants, each colour channel on its own - reduced so its longer side is
/// at most 2048 pixels, kept as JPEG bytes for the report, and a 256-pixel thumbnail beside the master. Beside it the
/// report keeps a detail: the master's centre at full scale, presented through the analysis the whole frame's pass
/// measured, so both pictures of a master are stretched alike.

#include "AfterNightProcessesExport.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/stages/LightsStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/Image.h>

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace an
{
class EngineContext;
} // namespace an

namespace an::consumer
{
class OperationScope;
} // namespace an::consumer

namespace ImageIntegration::detail
{

struct LocalStagedFile
{
    QString originalPath;
    QString stagedPath;
    WorkflowExecutionCounterAccumulator* counters = nullptr;
};

struct LocalOutputTransaction
{
    QString runId;
    QStringList trackedPaths;
    std::vector<LocalStagedFile> staged;
    WorkflowExecutionCounterAccumulator* counters = nullptr;
    bool restored = false;
};

/// Moves one existing file aside into the transaction; a file that does not exist is tracked so a failure removes
/// what the run wrote in its place.
[[nodiscard]] bool stageLocalFileForOverwrite(const QString& source_path, LocalOutputTransaction& transaction);

/// Moves a master's previous outputs aside: the master, its previews and every map its name may have.
[[nodiscard]] an::Result<LocalOutputTransaction>
stageOutputsForOverwrite(const QString& output_path, WorkflowExecutionCounterAccumulator* counters);

/// Removes what the run wrote in the transaction's place and puts the staged files back.
void restoreLocalStagedFiles(LocalOutputTransaction& transaction);

/// The master's preview as the report and the window show it.
[[nodiscard]] AFTERNIGHTPROCESSES_API StackPreviewAssets generateResidentStackPreviewAssets(
    const an::consumer::OperationScope& scope, const an::Image<float>& master, const QString& thumbnail_path);

/// A master's two report pictures, and what they were presented through.
struct ReportMasterPictures
{
    /// The whole frame, reduced as the report's preview of a master is.
    QImage whole;

    /// The master's centre at full scale; null when none could be made.
    QImage detail;

    /// The balance and statistics the whole frame was presented through: its own, or the ones it was given.
    an::DisplayPresentationAnalysis analysis{};
};

/// Presents a master as the stacking stage presents the one it holds for its report - the whole frame reduced by
/// taking every n-th sample, and the detail at full scale - through the master's own display stretch or through
/// another master's. A run's own pictures are made this way of the master in memory, so a master pictured to be set
/// beside them is made the same way and through the analysis this returns of the run's master: the two then differ
/// as their samples do, in the stretch and in how they were reduced alike.
///
/// @param scope the operation the pictures are worked out in.
/// @param master the master.
/// @param through the analysis to present through, in place of the master's own; null for its own.
/// @return the pictures; nothing when the master cannot be presented, or cannot be presented through the analysis
///         given - a master of another number of channels - since its own stretch would not be the one asked for.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<ReportMasterPictures>
presentMasterForReport(const an::consumer::OperationScope& scope, const an::Image<float>& master,
                       const an::DisplayPresentationAnalysis* through = nullptr);

void clearPreparedStackPreviewAssets(IntegrationGroupResult& group_result);

/// Makes a group's preview with the stage's generator; a preview that cannot be made is let go of and logged.
void prepareResidentStackPreviewAssets(const StackPreviewGenerator& preview_generator,
                                       const an::consumer::OperationScope& scope, const an::Image<float>& master,
                                       const QString& output_path, IntegrationGroupResult& group_result);

/// One map beside a master, as an image.
struct StackMapImage
{
    StackingAuxiliaryOutputKind kind = StackingAuxiliaryOutputKind::RejectionMerged;
    an::Image<float> image;
};

/// The maps a stack wrote, as images: each rejection map as its count over the group's frames, the coverage as its
/// count, and the variance, the signal-to-noise gain and the weight sum as computed. A map the stack did not write is
/// left out.
///
/// @param engine the context the images are made in.
/// @param result the stack's result; its maps are taken.
/// @param frameCount how many frames the stack combined.
/// @param metadata what every map states of itself.
/// @return the maps; or what describing or making one refused.
[[nodiscard]] an::Result<std::vector<StackMapImage>> stackMapImages(const an::EngineContext& engine,
                                                                    an::StackResult& result, std::uint32_t frameCount,
                                                                    const an::ImageMetadata& metadata);

enum class StackFinalizationPhase : size_t
{
    FinalMasterSave,
    RequestedMaps,
    FinalMasterWcs,
    PreviewGeneration,
    DigestArtifactRecording,
    CompletionPublication,
};

constexpr size_t kStackFinalizationPhaseCount = 6;

[[nodiscard]] QString stackFinalizationPhaseName(const StackFinalizationPhase phase);

[[nodiscard]] inline int monotonicDurationMs(const std::chrono::steady_clock::time_point start,
                                             const std::chrono::steady_clock::time_point end)
{
    const qint64 duration_ms =
        (std::max)(static_cast<qint64>(0),
                   static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()));
    return static_cast<int>((std::min)(duration_ms, static_cast<qint64>((std::numeric_limits<int>::max)())));
}

class StackGroupTimingRecorder
{
public:
    explicit StackGroupTimingRecorder(const StackGroupMonotonicClock& clock) : m_clock(clock), m_groupStart(m_clock())
    {
    }

    void beginFinalization()
    {
        if (m_timing.captured)
        {
            return;
        }

        m_timing.captured = true;
        m_timing.phases.reserve(kStackFinalizationPhaseCount);
        for (size_t index = 0; index < kStackFinalizationPhaseCount; ++index)
        {
            const auto phase = static_cast<StackFinalizationPhase>(index);
            m_timing.phases.push_back({.stageName = stackFinalizationPhaseName(phase),
                                       .status = QStringLiteral("skipped"),
                                       .durationMs = 0,
                                       .groupsProcessed = 0});
        }
        m_lastBoundary = m_clock();
    }

    void capturePhase(const StackFinalizationPhase phase, const QString& status)
    {
        const auto current = m_clock();
        auto& timing = m_timing.phases.at(static_cast<size_t>(phase));
        timing.status = status;
        timing.durationMs = monotonicDurationMs(m_lastBoundary, current);
        timing.groupsProcessed = 1;
        m_lastBoundary = current;
    }

    [[nodiscard]] int capturedGroupDurationMs() const
    {
        return monotonicDurationMs(m_groupStart, m_lastBoundary);
    }

    [[nodiscard]] int elapsedNowMs() const
    {
        return monotonicDurationMs(m_groupStart, m_clock());
    }

    [[nodiscard]] const StackGroupFinalizationTiming& finalizationTiming() const noexcept
    {
        return m_timing;
    }

private:
    const StackGroupMonotonicClock& m_clock;
    std::chrono::steady_clock::time_point m_groupStart;
    std::chrono::steady_clock::time_point m_lastBoundary;
    StackGroupFinalizationTiming m_timing;
};

} // namespace ImageIntegration::detail

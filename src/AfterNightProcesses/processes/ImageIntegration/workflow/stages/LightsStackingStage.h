// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file LightsStackingStage.h
 * @brief Workflow stage for final light frame stacking.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

#include <AfterNightEngine/Image/Image.h>

#include <QByteArray>
#include <QSize>

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace an::consumer
{
class OperationScope;
} // namespace an::consumer

namespace an::ui
{
class PlateSolveService;
}

using StackGroupMonotonicClock = std::function<std::chrono::steady_clock::time_point()>;

struct StackPreviewAssets
{
    QByteArray reportPreviewJpeg; ///< Bounded JPEG bytes; no resident master is retained.
    QSize reportPreviewSize; ///< Pixel dimensions of reportPreviewJpeg.
    QByteArray reportDetailJpeg; ///< JPEG bytes of the master's centre at full scale, stretched as the whole frame is.
    QSize reportDetailSize; ///< Pixel dimensions of reportDetailJpeg; empty when there is no detail.
};

/// Makes a master's preview: its report JPEG, and the thumbnail written at the path given.
using StackPreviewGenerator =
    std::function<StackPreviewAssets(const an::consumer::OperationScope&, const an::Image<float>&, const QString&)>;

/// Called with the files a group's stack reads just before it reads them.
using StackPreStackGenerationHook = std::function<void(const std::vector<FrameId>&)>;

/// Stacks each integration group's lights into its master.
///
/// A group's lights are read where its registration put them - its registered lights, or its registration inputs
/// warped onto the registration's plane as they are read - or as they are when no registration placed them, or
/// drizzled onto their canvas, or as the comet's registration aligned them; brought to the reference by the
/// normalization stage's coefficients; weighted by what the registration measured; and combined by the engine's stack
/// in one admitted operation. The master's solution is chosen, and solved if the group asks, before it is written, and
/// the master and its maps are published once, whole and stamped with the manifest entry they satisfy. Afterwards the
/// red, green and blue masters of a registration are combined into one colour master when the run asks.
class AFTERNIGHTPROCESSES_API LightsStackingStage : public IWorkflowStage
{
public:
    explicit LightsStackingStage(std::shared_ptr<an::ui::PlateSolveService> plateSolveService = {},
                                 StackGroupMonotonicClock monotonicClock = {},
                                 StackPreviewGenerator previewGenerator = {},
                                 StackPreStackGenerationHook preStackGenerationHook = {});

    [[nodiscard]] QString name() const override;
    [[nodiscard]] WorkflowStage stageId() const override;
    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override;
    [[nodiscard]] QString skipReason(const WorkflowManifest& manifest) const override;

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token,
                        ProgressReporter& reporter) override;

private:
    std::shared_ptr<an::ui::PlateSolveService> m_plateSolveService;
    StackGroupMonotonicClock m_monotonicClock;
    StackPreviewGenerator m_previewGenerator;
    StackPreStackGenerationHook m_preStackGenerationHook;
};

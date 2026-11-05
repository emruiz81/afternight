// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A calibration stage run outside the executor, as the executor runs it: an engine session holding the FITS and TIFF
/// codecs, the services a run lends its stages bound for the call, and calibration frames written by the neutral layer
/// as capture programs write them.

#include "processes/ImageIntegration/workflow/CancellationToken.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <QString>

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace an::tests::integration
{

/// One stage's run: the session, the services and the reporter, as an executor lends them.
class CalibrationRun
{
public:
    /// A run of `width` threads; `valid` says whether the session opened.
    explicit CalibrationRun(std::uint32_t width = 2u);

    [[nodiscard]] bool valid() const noexcept;

    /// The session the run's stages work in.
    [[nodiscard]] const an::consumer::EngineSession& session() const;

    /// Runs a stage with the run's services bound, as the executor does.
    StageResult run(IWorkflowStage& stage, const WorkflowManifest& manifest);

    /// Runs a stage with the run's services bound and the caller's token.
    StageResult run(IWorkflowStage& stage, const WorkflowManifest& manifest, CancellationToken& token);

    /// Stops the run's engine work, as the executor's cancellation does.
    void requestStop();

    /// Every warning the stage logged.
    [[nodiscard]] const QStringList& loggedWarnings() const noexcept;

    /// Every operation detail the stages logged, as the run log writes it, in the order they logged them.
    [[nodiscard]] const QStringList& loggedDetails() const noexcept;

    /// Every progress report the stage made, unthrottled, in the order it made them.
    [[nodiscard]] const std::vector<WorkflowProgress>& progressReports() const noexcept;

private:
    std::optional<an::consumer::EngineSession> m_session;
    std::stop_source m_stop;
    QStringList m_warnings;
    QStringList m_details;
    std::vector<WorkflowProgress> m_progress;
};

/// Writes a small grey calibration frame of a constant sixteen-bit level, stating its kind, at a path.
///
/// @param path the file.
/// @param kind its kind as a capture program states it ("Bias Frame", "Dark Frame", "Flat Field", "Light Frame").
/// @param level the code every sample holds.
/// @param overrides samples set to other codes.
/// @return false, with the reason in `error`, when the file cannot be written.
[[nodiscard]] bool writeCalibrationFrame(const QString& path, const std::string& kind, double level,
                                         const std::vector<an::tests::fixtures::FixtureSampleOverride>& overrides,
                                         std::string& error);

/// Reads a file the stage wrote with the neutral reader, independently of the engine that wrote it.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureFitsFile> readWrittenImage(const QString& path);

/// The artifact and run identities a written file's header carries.
struct WrittenStamp
{
    std::string artifactId;
    std::string runId;
};

/// The stamp a written file's header carries, read as the artifact tracker reads it; nothing when it carries none.
[[nodiscard]] std::optional<WrittenStamp> writtenIdentity(const an::consumer::EngineSession& session,
                                                          const QString& path);

/// The width and height every written calibration frame has.
inline constexpr std::uint32_t kCalibrationFrameWidth = 16u;
inline constexpr std::uint32_t kCalibrationFrameHeight = 12u;

/// A sixteen-bit code as the fraction of full scale the calibration works in.
[[nodiscard]] constexpr double fractionOf(double code) noexcept
{
    return code / 65535.0;
}

} // namespace an::tests::integration

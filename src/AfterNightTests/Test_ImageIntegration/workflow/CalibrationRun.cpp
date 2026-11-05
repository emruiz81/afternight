// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A calibration stage run outside the executor, and the frames it reads.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"

#include "Test_ImageIntegration/ImportSession.h"

#include "fixtures/common/IntegrationFrameFiles.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <chrono>
#include <filesystem>
#include <utility>

namespace an::tests::integration
{

CalibrationRun::CalibrationRun(const std::uint32_t width)
{
    an::Result<an::consumer::EngineSession> session = openImportSession(width);
    if (session.hasValue())
    {
        m_session.emplace(std::move(session).value());
    }
}

bool CalibrationRun::valid() const noexcept
{
    return m_session.has_value();
}

const an::consumer::EngineSession& CalibrationRun::session() const
{
    return *m_session;
}

StageResult CalibrationRun::run(IWorkflowStage& stage, const WorkflowManifest& manifest)
{
    CancellationToken token;
    return run(stage, manifest, token);
}

StageResult CalibrationRun::run(IWorkflowStage& stage, const WorkflowManifest& manifest, CancellationToken& token)
{
    const ImageIntegration::WorkflowStageServices services(*m_session, 0u, ImageIntegration::workflowRawPolicy(),
                                                           nullptr, m_stop.get_token());
    ProgressReporter reporter;
    reporter.setLogCallback([this](const LogCategory category, const QString& message) {
        if (category == LogCategory::Warning)
        {
            m_warnings.append(message);
        }
        else if (category == LogCategory::Detail)
        {
            m_details.append(message);
        }
    });
    reporter.setMinimumInterval(std::chrono::milliseconds(0));
    reporter.setCallback([this](const WorkflowProgress& progress) { m_progress.push_back(progress); });
    stage.setStageServices(&services);
    StageResult result = stage.execute(manifest, token, reporter);
    stage.setStageServices(nullptr);
    return result;
}

void CalibrationRun::requestStop()
{
    m_stop.request_stop();
}

const QStringList& CalibrationRun::loggedWarnings() const noexcept
{
    return m_warnings;
}

const QStringList& CalibrationRun::loggedDetails() const noexcept
{
    return m_details;
}

const std::vector<WorkflowProgress>& CalibrationRun::progressReports() const noexcept
{
    return m_progress;
}

bool writeCalibrationFrame(const QString& path, const std::string& kind, const double level,
                           const std::vector<an::tests::fixtures::FixtureSampleOverride>& overrides, std::string& error)
{
    an::tests::fixtures::AcquisitionFacts facts;
    facts.kind = kind;
    an::tests::fixtures::FixtureImage frame =
        an::tests::fixtures::acquisitionFrame(std::filesystem::path(path.toStdWString()).filename().string(), facts,
                                              kCalibrationFrameWidth, kCalibrationFrameHeight);
    frame.levels = {an::tests::fixtures::FixtureChannelLevels{.base = level}};
    frame.overrides = overrides;
    return an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(path.toStdWString()), error);
}

std::optional<an::tests::fixtures::FixtureFitsFile> readWrittenImage(const QString& path)
{
    std::string error;
    return an::tests::fixtures::readFixtureFits(std::filesystem::path(path.toStdWString()), error);
}

std::optional<WrittenStamp> writtenIdentity(const an::consumer::EngineSession& session, const QString& path)
{
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    const std::string nativePath = path.toUtf8().toStdString();
    const an::Result<an::CodecImageInfo> probed = an::probeImage(
        scope->engine(), scope->execution(), an::CodecReadRequest{.path = nativePath, .validatePayload = false});
    if (!probed.hasValue())
    {
        return std::nullopt;
    }
    const auto identity = an::artifactIdentityOf(probed->metadata.codec(), an::CodecNamespace::Fits);
    if (!identity.hasValue() || !identity->has_value())
    {
        return std::nullopt;
    }
    return WrittenStamp{.artifactId = (*identity)->artifactId(), .runId = (*identity)->runId()};
}

} // namespace an::tests::integration

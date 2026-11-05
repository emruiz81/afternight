// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights calibration stage: each light read through the run's registry, calibrated by its
/// masters with the engine's calibration, corrected where the dark marks defective pixels, and published whole and
/// stamped to `calibrated_lights` - its record made only after its write committed.
///
/// A master that cannot be read, or a dark the bias cannot be subtracted from, fails the lights that name it - each
/// reported against its own file - and the stage goes on with the rest; such a failure never escapes the stage, even
/// when the master's codec throws. A light with no master and no pedestal is left as it is, and what a calibration fell
/// back to is said once per calibration.

#include "stdafx.hpp"
#include "LightsCalibrationStage.h"

#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <variant>
#include <vector>

namespace
{

/// Where one stage run's time went, across every light it calibrated.
struct CalibrationStageProfile
{
    std::uint64_t framesCompleted = 0u;
    std::uint64_t loadNanoseconds = 0u;
    std::uint64_t convertNanoseconds = 0u;
    std::uint64_t mastersNanoseconds = 0u;
    std::uint64_t calibrateNanoseconds = 0u;
    std::uint64_t writeNanoseconds = 0u;
    std::uint64_t loopNanoseconds = 0u;
    std::uint64_t inputSampleBytes = 0u;
    std::uint64_t outputSampleBytes = 0u;
};

/// Adds its own lifetime to an accumulator.
class PhaseTimer
{
public:
    explicit PhaseTimer(std::uint64_t& accumulator)
        : m_accumulator(accumulator), m_start(std::chrono::steady_clock::now())
    {
    }

    PhaseTimer(const PhaseTimer&) = delete;
    PhaseTimer& operator=(const PhaseTimer&) = delete;

    ~PhaseTimer()
    {
        m_accumulator += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_start).count());
    }

private:
    std::uint64_t& m_accumulator;
    std::chrono::steady_clock::time_point m_start;
};

/// The stage's time as shares of its loop, so a slow run can be read as bound by its storage or by its cores wherever
/// it ran. What the named phases do not add up to is printed as `other`.
[[nodiscard]] QString formatCalibrationProfile(const CalibrationStageProfile& profile)
{
    if (profile.framesCompleted == 0u || profile.loopNanoseconds == 0u)
    {
        return {};
    }
    const auto share = [&](const std::uint64_t nanoseconds) {
        return 100.0 * static_cast<double>(nanoseconds) / static_cast<double>(profile.loopNanoseconds);
    };
    QStringList parts;
    const auto append = [&](const char* name, const std::uint64_t nanoseconds) {
        if (nanoseconds > 0u)
        {
            parts << QStringLiteral("%1 %2%").arg(QLatin1String(name)).arg(share(nanoseconds), 0, 'f', 1);
        }
    };
    append("load", profile.loadNanoseconds);
    append("convert", profile.convertNanoseconds);
    append("masters", profile.mastersNanoseconds);
    append("calibrate", profile.calibrateNanoseconds);
    append("write", profile.writeNanoseconds);
    const std::uint64_t accounted = profile.loadNanoseconds + profile.convertNanoseconds + profile.mastersNanoseconds +
                                    profile.calibrateNanoseconds + profile.writeNanoseconds;
    parts << QStringLiteral("other %1%")
                 .arg(profile.loopNanoseconds > accounted ? share(profile.loopNanoseconds - accounted) : 0.0, 0, 'f',
                      1);
    const double perFrameMs =
        static_cast<double>(profile.loopNanoseconds) / 1.0e6 / static_cast<double>(profile.framesCompleted);
    return QStringLiteral("%1 ms/frame | %2").arg(perFrameMs, 0, 'f', 0).arg(parts.join(QStringLiteral(", ")));
}

/// The scale the shares are relative to: the bytes read and written per frame and the threads available.
[[nodiscard]] QString formatCalibrationThroughput(const CalibrationStageProfile& profile)
{
    if (profile.framesCompleted == 0u || profile.loopNanoseconds == 0u)
    {
        return {};
    }
    constexpr double kMegabyte = 1024.0 * 1024.0;
    const double seconds = static_cast<double>(profile.loopNanoseconds) / 1.0e9;
    const double frames = static_cast<double>(profile.framesCompleted);
    const double movedMb = static_cast<double>(profile.inputSampleBytes + profile.outputSampleBytes) / kMegabyte;
    return QStringLiteral("%1 frames, %2 MB read / %3 MB written per frame, %4 MB/s decoded+encoded, %5 hardware "
                          "threads")
        .arg(profile.framesCompleted)
        .arg(static_cast<double>(profile.inputSampleBytes) / (frames * kMegabyte), 0, 'f', 1)
        .arg(static_cast<double>(profile.outputSampleBytes) / (frames * kMegabyte), 0, 'f', 1)
        .arg(movedMb / (std::max)(seconds, 1.0e-9), 0, 'f', 0)
        .arg(QThread::idealThreadCount());
}

/// The bytes an image's samples occupy.
[[nodiscard]] std::uint64_t sampleBytesOf(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& typed) -> std::uint64_t {
            const auto view = typed.view();
            return view.has_value() ? view->layout().totalBytes() : 0u;
        },
        image);
}

/**
 * @brief Key for grouping lights by their calibration configuration.
 *
 * Lights that share the same bias, dark, flat masters and calibration settings
 * can be processed in a single batch call to the engine, improving efficiency.
 */
struct CalibrationConfigKey
{
    QString biasPath;
    QString darkPath;
    QString flatPath;
    bool subtractBiasFromDark = false;
    bool cosmeticCorrection = false;
    double sigmaHot = 3.0;
    double sigmaCold = 3.0;
    bool darkOptimization = false;
    double darkMultiplicationFactor = 1.0;
    bool flatAutoLevel = true;
    double pedestal = 0.0;

    bool operator<(const CalibrationConfigKey& other) const
    {
        // Compare all fields in order for strict weak ordering
        if (biasPath != other.biasPath)
            return biasPath < other.biasPath;
        if (darkPath != other.darkPath)
            return darkPath < other.darkPath;
        if (flatPath != other.flatPath)
            return flatPath < other.flatPath;
        if (subtractBiasFromDark != other.subtractBiasFromDark)
            return subtractBiasFromDark < other.subtractBiasFromDark;
        if (cosmeticCorrection != other.cosmeticCorrection)
            return cosmeticCorrection < other.cosmeticCorrection;
        if (sigmaHot != other.sigmaHot)
            return sigmaHot < other.sigmaHot;
        if (sigmaCold != other.sigmaCold)
            return sigmaCold < other.sigmaCold;
        if (darkOptimization != other.darkOptimization)
            return darkOptimization < other.darkOptimization;
        if (darkMultiplicationFactor != other.darkMultiplicationFactor)
            return darkMultiplicationFactor < other.darkMultiplicationFactor;
        if (flatAutoLevel != other.flatAutoLevel)
            return flatAutoLevel < other.flatAutoLevel;
        return pedestal < other.pedestal;
    }
};

CalibrationConfigKey makeConfigKey(const WorkflowManifest::LightsCalibrationParams& params)
{
    CalibrationConfigKey key;
    key.biasPath = params.biasPath.has_value() ? params.biasPath->canonicalPath() : QString();
    key.darkPath = params.darkPath.has_value() ? params.darkPath->canonicalPath() : QString();
    key.flatPath = params.flatPath.has_value() ? params.flatPath->canonicalPath() : QString();
    key.subtractBiasFromDark = params.settings.subtractBiasFromDark;
    key.cosmeticCorrection = params.settings.cosmeticCorrection;
    key.sigmaHot = params.settings.sigmaHot;
    key.sigmaCold = params.settings.sigmaCold;
    key.darkOptimization = params.settings.darkOptimization;
    key.darkMultiplicationFactor = params.settings.darkMultiplicationFactor;
    key.flatAutoLevel = params.settings.flatAutoLevel;
    key.pedestal = params.settings.pedestal;
    return key;
}

std::vector<const WorkflowManifest::LightsCalibrationParams*>
calibrationParamsToProcess(const WorkflowManifest& manifest)
{
    std::vector<const WorkflowManifest::LightsCalibrationParams*> params;
    params.reserve(manifest.lightsCalibration.size());

    if (manifest.executionMode != ExecutionMode::Incremental)
    {
        for (const auto& p : manifest.lightsCalibration)
        {
            params.push_back(&p);
        }
        return params;
    }
    if (manifest.incrementalRegistrationGroups.empty())
    {
        return params;
    }

    std::set<QString> selected_lights;
    for (const auto& registration_group : manifest.registrationGroups)
    {
        const auto it = std::find(manifest.incrementalRegistrationGroups.begin(),
                                  manifest.incrementalRegistrationGroups.end(), registration_group.groupKey);
        if (it == manifest.incrementalRegistrationGroups.end())
        {
            continue;
        }

        for (const auto& frame : registration_group.lightFiles)
        {
            selected_lights.insert(frame.canonicalPath());
        }
    }

    if (selected_lights.empty())
    {
        return params;
    }

    for (const auto& p : manifest.lightsCalibration)
    {
        if (selected_lights.find(p.lightPath.canonicalPath()) != selected_lights.end())
        {
            params.push_back(&p);
        }
    }

    return params;
}
} // namespace

QString LightsCalibrationStage::name() const
{
    return QStringLiteral("LightsCalibration");
}

WorkflowStage LightsCalibrationStage::stageId() const
{
    return WorkflowStage::LightsCalibration;
}

bool LightsCalibrationStage::shouldSkip(const WorkflowManifest& manifest) const
{
    if (manifest.calibrateLightsInMemory)
    {
        LOG_INFO("LightsCalibrationStage: skipping because in-memory calibration is enabled.");
        return true;
    }

    if (manifest.executionMode == ExecutionMode::Incremental && manifest.incrementalRegistrationGroups.empty())
    {
        LOG_INFO("LightsCalibrationStage: skipping because incremental plan has no dirty registration groups.");
        return true;
    }

    return calibrationParamsToProcess(manifest).empty();
}

QString LightsCalibrationStage::skipReason(const WorkflowManifest& manifest) const
{
    if (manifest.calibrateLightsInMemory)
    {
        return QStringLiteral("In-memory calibration enabled (no calibrated files written).");
    }
    if (manifest.executionMode == ExecutionMode::Incremental && manifest.incrementalRegistrationGroups.empty())
    {
        return QStringLiteral("No dirty registration groups in incremental plan.");
    }
    if (calibrationParamsToProcess(manifest).empty())
    {
        return QStringLiteral("No lights selected for calibration.");
    }
    return QStringLiteral("No work required.");
}

StageResult LightsCalibrationStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                            ProgressReporter& reporter)
{
    const auto params_to_process = calibrationParamsToProcess(manifest);
    if (params_to_process.empty())
    {
        return StageResult::skipped();
    }
    const ImageIntegration::WorkflowStageServices* services = stageServices();
    if (services == nullptr)
    {
        return StageResult::failure(IntegrationError::internalError(QStringLiteral(
            "The lights calibration stage runs only inside a workflow run, which lends it the session")));
    }

    const QDir output_root(manifest.outputDirectory);
    const QString output_dir = output_root.filePath(QStringLiteral("calibrated_lights"));
    if (!output_root.exists() && !output_root.mkpath(QStringLiteral(".")))
    {
        return StageResult::failure(IntegrationError::directoryNotWritable(manifest.outputDirectory));
    }
    QDir calibrated_dir(output_dir);
    if (!calibrated_dir.exists() && !calibrated_dir.mkpath(QStringLiteral(".")))
    {
        return StageResult::failure(IntegrationError::directoryNotWritable(output_dir));
    }

    // Lights that share their masters and settings are reported as one batch, and share one prepared calibration.
    std::map<CalibrationConfigKey, std::vector<const WorkflowManifest::LightsCalibrationParams*>> groups;
    for (const auto* params : params_to_process)
    {
        if (!params || !params->lightPath.isValid())
        {
            return StageResult::failure(IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                                         QStringLiteral("Invalid light frame path"),
                                                         params ? params->lightPath.toString() : QString()));
        }
        groups[makeConfigKey(*params)].push_back(params);
    }
    LOG_INFO("LightsCalibrationStage: {} frames grouped into {} batch(es)", params_to_process.size(), groups.size());

    const int total_batches = static_cast<int>(groups.size());
    reporter.logger().stageStarted(name(), total_batches);
    QElapsedTimer stage_timer;
    stage_timer.start();

    ArtifactTracker tracker(manifest.outputDirectory, executionCounterAccumulator(),
                            sessionArtifactStampReader(services->session()));
    core::CalibrationMasterSet masters(services->codecRegistry());
    const an::CodecRegistry* registry = services->codecRegistry().get();
    IntegrationErrorReport error_report;
    QStringList stage_warnings;
    CalibrationStageProfile stage_profile;

    const int total_frames = static_cast<int>(params_to_process.size());
    int processed_frames = 0;
    int batch_index = 0;

    const auto recordFailure = [&](const QString& lightPath, const IntegrationError& error) {
        error_report.addError(error);
        reporter.logger().warning(
            QStringLiteral("Frame calibration failed: %1 - %2").arg(lightPath, error.toUserString()));
    };

    for (const auto& [config, params_list] : groups)
    {
        if (token.isCancellationRequested())
        {
            return StageResult::cancelled();
        }
        ++batch_index;
        reporter.logger().groupStarted(
            batch_index, total_batches,
            QStringLiteral("Batch %1 (%2 frames)").arg(batch_index).arg(static_cast<int>(params_list.size())));
        QElapsedTimer batch_timer;
        batch_timer.start();

        // A batch with no master and no pedestal has nothing to calibrate its lights with: they are left as they are,
        // and the registration reads each light itself, as it does in memory. It is said in the log only, as
        // lights-only processing warns of nothing.
        if (!params_list.front()->calibrates())
        {
            reporter.logger().operationDetail(
                QStringLiteral("Nothing to calibrate with"),
                QStringLiteral("No master or pedestal is assigned; %1 light(s) are used as they are")
                    .arg(static_cast<int>(params_list.size())));
            processed_frames += static_cast<int>(params_list.size());
            stage_utils::reportStageProgress(reporter, stageId(), processed_frames, total_frames,
                                             static_cast<float>(processed_frames) / static_cast<float>(total_frames),
                                             QStringLiteral("Nothing to calibrate with"),
                                             params_list.back()->lightPath.toString());
            reporter.logger().groupCompleted(batch_index, total_batches, QStringLiteral("Batch %1").arg(batch_index),
                                             batch_timer.elapsed());
            continue;
        }

        struct PendingLight
        {
            const WorkflowManifest::LightsCalibrationParams* params;
            QString outputPath;
            QString expectedHash;
        };
        std::vector<PendingLight> to_process;
        to_process.reserve(params_list.size());
        int reuse_count = 0;
        int missing_output_count = 0;
        int invalid_output_count = 0;
        for (const auto* params : params_list)
        {
            const QString outputPath = calibrated_dir.filePath(ImageIntegration::makeCalibratedOutputFilename(
                QFileInfo(params->lightPath.canonicalPath()).filePath()));
            const QString expected_hash = computeCalibratedLightArtifactHash(*params);
            if (manifest.reprocessExisting)
            {
                to_process.push_back({params, outputPath, expected_hash});
                continue;
            }
            if (!QFileInfo::exists(outputPath))
            {
                ++missing_output_count;
                to_process.push_back({params, outputPath, expected_hash});
                continue;
            }
            if (tracker.isArtifactValid(outputPath, expected_hash))
            {
                ++reuse_count;
            }
            else
            {
                ++invalid_output_count;
                to_process.push_back({params, outputPath, expected_hash});
            }
        }
        reporter.logger().operationDetail(
            QStringLiteral("Incremental plan"),
            QStringLiteral("Reuse: %1 | Calibrate: %2").arg(reuse_count).arg(to_process.size()) +
                (manifest.reprocessExisting ? QStringLiteral(" (reprocess enabled)")
                                            : QStringLiteral(" (missing: %1 | changed: %2)")
                                                  .arg(missing_output_count)
                                                  .arg(invalid_output_count)));
        if (reuse_count > 0)
        {
            processed_frames += reuse_count;
            stage_utils::reportStageProgress(reporter, stageId(), processed_frames, total_frames,
                                             static_cast<float>(processed_frames) / static_cast<float>(total_frames),
                                             QStringLiteral("Reusing calibrated files"),
                                             params_list.back()->lightPath.toString());
        }
        if (to_process.empty())
        {
            reporter.logger().groupCompleted(batch_index, total_batches, QStringLiteral("Batch %1").arg(batch_index),
                                             batch_timer.elapsed());
            continue;
        }

        // The batch's masters, its CCD dark and its cosmetic maps, each made once for the run.
        std::shared_ptr<const core::LightCalibration> calibration;
        {
            an::Result<std::shared_ptr<const core::LightCalibration>> prepared =
                [&]() -> an::Result<std::shared_ptr<const core::LightCalibration>> {
                an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
                if (!scope.hasValue())
                {
                    return scope.status();
                }
                const PhaseTimer timer(stage_profile.mastersNanoseconds);
                return masters.prepare(*scope, *params_list.front());
            }();
            if (!prepared.hasValue())
            {
                if (prepared.status().code() == an::ErrorCode::Cancelled)
                {
                    return StageResult::cancelled();
                }
                const QString cause = ImageIntegration::engineFailureText(prepared.status());
                for (const PendingLight& light : to_process)
                {
                    error_report.addError(ImageIntegration::integrationErrorFromEngine(
                        prepared.status(), IntegrationErrorCode::CalibrationFailed,
                        QStringLiteral("Master frame load failed"), light.params->lightPath.canonicalPath()));
                }
                reporter.logger().warning(
                    QStringLiteral("Batch %1: master frame load failed: %2 - skipping %3 frame(s)")
                        .arg(batch_index)
                        .arg(cause)
                        .arg(to_process.size()));
                processed_frames += static_cast<int>(to_process.size());
                reporter.logger().groupCompleted(batch_index, total_batches,
                                                 QStringLiteral("Batch %1 (failed)").arg(batch_index),
                                                 batch_timer.elapsed());
                continue;
            }
            calibration = std::move(*prepared);
        }
        if (calibration->coldMap != nullptr || calibration->hotMap != nullptr)
        {
            reporter.logger().operationDetail(QStringLiteral("Cosmetic correction"),
                                              QStringLiteral("Detected %1 cold pixel(s), %2 hot pixel(s)")
                                                  .arg(calibration->coldPixels)
                                                  .arg(calibration->hotPixels));
        }

        for (const PendingLight& light : to_process)
        {
            if (token.isCancellationRequested())
            {
                return StageResult::cancelled();
            }
            const QString lightPath = light.params->lightPath.canonicalPath();
            const auto loopStarted = std::chrono::steady_clock::now();
            const an::Result<an::ArtifactIdentity> identity =
                ImageIntegration::workflowArtifactIdentity(light.expectedHash, manifest.executionId);
            an::Status outcome = [&]() -> an::Status {
                if (!identity.hasValue())
                {
                    return identity.status();
                }
                an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
                if (!scope.hasValue())
                {
                    return scope.status();
                }
                an::Result<an::consumer::AnyImage> read = [&]() {
                    const PhaseTimer timer(stage_profile.loadNanoseconds);
                    return core::readWorkflowImage(*scope, registry, lightPath);
                }();
                if (!read.hasValue())
                {
                    return read.status();
                }
                stage_profile.inputSampleBytes += sampleBytesOf(*read);
                an::Result<an::Image<float>> fractions = [&]() {
                    const PhaseTimer timer(stage_profile.convertNanoseconds);
                    return core::workingFractions(*scope, *read);
                }();
                if (!fractions.hasValue())
                {
                    return fractions.status();
                }
                an::Result<core::CalibratedLight> calibrated = [&]() {
                    const PhaseTimer timer(stage_profile.calibrateNanoseconds);
                    return core::calibrateLight(*scope, *calibration, *fractions);
                }();
                if (!calibrated.hasValue())
                {
                    return calibrated.status();
                }
                if (calibrated->darkScaleFixedInstead)
                {
                    LOG_WARNING("LightsCalibrationStage: the dark scale of '{}' could not be optimized; the fixed "
                                "scale {} was used",
                                lightPath.toStdString(), light.params->settings.darkMultiplicationFactor);
                }
                if (const auto view = calibrated->image.view(); view.has_value())
                {
                    stage_profile.outputSampleBytes += view->layout().totalBytes();
                }
                const PhaseTimer timer(stage_profile.writeNanoseconds);
                return ImageIntegration::publishWorkflowImage(*scope, calibrated->image, light.outputPath, *identity);
            }();
            stage_profile.loopNanoseconds += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - loopStarted)
                    .count());

            if (outcome.failed())
            {
                if (outcome.code() == an::ErrorCode::Cancelled)
                {
                    return StageResult::cancelled();
                }
                recordFailure(lightPath, ImageIntegration::integrationErrorFromEngine(
                                             outcome, IntegrationErrorCode::CalibrationFailed,
                                             QStringLiteral("Calibration failed"), lightPath));
            }
            // The record follows the committed write: a stop between them leaves a whole file without a record,
            // which the next run calibrates again, never a record of a file not written.
            else if (!tracker.recordArtifact(light.outputPath, light.expectedHash, *identity,
                                             [&token]() { return token.isCancellationRequested(); }))
            {
                if (token.isCancellationRequested())
                {
                    return StageResult::cancelled();
                }
                recordFailure(lightPath,
                              IntegrationError::fileNotReadable(
                                  light.outputPath, QStringLiteral("Failed to record the calibrated light")));
            }
            else
            {
                ++stage_profile.framesCompleted;
            }
            ++processed_frames;
            stage_utils::reportStageProgress(
                reporter, stageId(), processed_frames, total_frames,
                static_cast<float>(processed_frames) / static_cast<float>(total_frames),
                QStringLiteral("Calibrating image %1 of %2").arg(processed_frames).arg(total_frames), lightPath);
        }

        reporter.logger().groupCompleted(batch_index, total_batches, QStringLiteral("Batch %1").arg(batch_index),
                                         batch_timer.elapsed());
    }

    LOG_INFO("LightsCalibrationStage: completed {} frames", processed_frames);
    if (const QString profile_line = formatCalibrationProfile(stage_profile); !profile_line.isEmpty())
    {
        reporter.logger().operationDetail(QStringLiteral("Calibration profile"), profile_line);
        reporter.logger().operationDetail(QStringLiteral("Calibration throughput"),
                                          formatCalibrationThroughput(stage_profile));
        LOG_INFO("LightsCalibrationStage: {} | {}", profile_line.toStdString(),
                 formatCalibrationThroughput(stage_profile).toStdString());
    }
    // What a calibration fell back to - a cosmetic correction with no dark, a dark scale the optimization could not
    // fit - once per calibration, as every stage that calibrates the lights in memory says it.
    stage_utils::sayStageWarnings(reporter, masters.fallbackWarnings(), stage_warnings);
    reporter.logger().stageCompleted(name(), total_batches, stage_timer.elapsed());

    if (!error_report.errors.empty())
    {
        LOG_WARNING("LightsCalibrationStage: {} frame(s) had calibration errors", error_report.errors.size());
        error_report.warnings = stage_warnings;
        return StageResult::partialSuccess(error_report);
    }
    StageResult success = StageResult::success();
    success.warnings = stage_warnings;
    return success;
}

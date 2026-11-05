// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The bias, dark and flat stages' preflight, their stack into a master, and the run of their groups.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/CalibrationStackInputs.h"

#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactPublicationLedger.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSourceRules.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <utility>

namespace stage_utils
{

namespace
{

using ImageIntegration::PersistedChoice;

constexpr std::uint64_t kBytesPerMebibyte = 1024u * 1024u;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

[[nodiscard]] std::string utf8(const QString& text)
{
    return text.toUtf8().toStdString();
}

[[nodiscard]] bool isText(const QString& text, const char* expected)
{
    return text.trimmed().compare(QLatin1String(expected), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] bool isCancellation(const an::Status& status)
{
    return status.code() == an::ErrorCode::Cancelled;
}

/// Adds its own lifetime to an accumulator, or to nothing when handed a null one.
class ScopedPhaseTimer
{
public:
    explicit ScopedPhaseTimer(std::uint64_t* accumulator)
        : m_accumulator(accumulator), m_start(std::chrono::steady_clock::now())
    {
    }

    ScopedPhaseTimer(const ScopedPhaseTimer&) = delete;
    ScopedPhaseTimer& operator=(const ScopedPhaseTimer&) = delete;

    ~ScopedPhaseTimer()
    {
        if (m_accumulator != nullptr)
        {
            *m_accumulator += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_start)
                    .count());
        }
    }

private:
    std::uint64_t* m_accumulator;
    std::chrono::steady_clock::time_point m_start;
};

/// The stack's own progress, in the stage's reporter as the group's fraction.
class GroupProgress final : public an::ProgressSink
{
public:
    GroupProgress(ProgressReporter& reporter, const CalibrationStackRequest& request)
        : m_reporter(reporter), m_request(request)
    {
    }

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        if (report.total == 0u)
        {
            return;
        }
        try
        {
            reportStageProgress(
                m_reporter, m_request.stage, m_request.groupIndex, m_request.totalGroups,
                static_cast<float>(static_cast<double>(report.completed) / static_cast<double>(report.total)),
                m_request.operation, m_request.outputPath);
        }
        catch (...)
        {
            // A reporter that fails loses a progress line; the stack is not stopped for it.
        }
    }

private:
    ProgressReporter& m_reporter;
    const CalibrationStackRequest& m_request;
};

void recordUnreadableFrame(const FrameId& frame, const QString& frameType, const QString& reason,
                           IntegrationErrorReport& errorReport, QStringList& stageWarnings, ProgressReporter& reporter)
{
    const QString path = frame.isValid() ? frame.canonicalPath() : frame.toString();
    const QString displayName = path.isEmpty() ? QStringLiteral("<invalid frame>") : QFileInfo(path).fileName();
    const QString warning = QStringLiteral("Skipping unreadable %1 frame: %2 (%3)").arg(frameType, displayName, reason);
    errorReport.addError(IntegrationError::invalidFileFormat(path, reason));
    stageWarnings.append(warning);
    reporter.logger().warning(warning);
    LOG_WARNING("{}", warning.toStdString());
}

/// Whether two frames hold the same samples in the same place: size, channels, representation and filter array.
[[nodiscard]] bool sameFrameShape(const an::ImageDescriptor& left, const an::ImageDescriptor& right)
{
    return left.extent() == right.extent() && left.sampleType() == right.sampleType() && left.cfa() == right.cfa();
}

[[nodiscard]] QString shapeText(const an::ImageDescriptor& descriptor)
{
    return QStringLiteral("%1x%2, %3 channel(s), %4%5")
        .arg(descriptor.extent().width())
        .arg(descriptor.extent().height())
        .arg(descriptor.extent().channelCount())
        .arg(QString::fromLatin1(an::sampleTypeName(descriptor.sampleType())))
        .arg(descriptor.isMosaiced() ? QStringLiteral(", mosaic") : QString());
}

/// A group's frames, each decoded whole through the run's registry when the stack reads it and packed planar into the
/// stack's read buffer, each held to the revision it was probed at.
class CalibrationFrameSource final : public an::StackFrameSource
{
public:
    CalibrationFrameSource(const an::EngineContext& engine, const an::CodecRegistry* registry,
                           const ReadableStackInputs& inputs, const an::ImageDescriptor& descriptor)
        : m_engine(engine), m_registry(registry), m_inputs(inputs), m_descriptor(descriptor),
          m_layouts(inputs.inputFiles.size())
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return an::StackSourceAccess::WholeFrame;
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution,
                                                       const std::uint32_t frameIndex, const std::uint32_t firstRow,
                                                       const std::uint32_t rows,
                                                       const std::span<std::byte> buffer) override
    {
        if (frameIndex >= m_inputs.inputFiles.size() || firstRow != 0u || rows != m_descriptor.extent().height())
        {
            return refusal(an::ErrorCode::OutOfRange, "the stack asked for a frame the group does not hold");
        }
        const QString& canonical = m_inputs.inputFiles[frameIndex].canonicalPath();
        const std::string path = utf8(canonical);
        if (!unchangedSinceProbed(frameIndex))
        {
            return refusal(an::ErrorCode::InvalidState, "'" + path + "' changed after it was checked");
        }
        const an::CodecReadRequest request{.path = path};
        an::Result<an::CodecDecodeResult> decoded = m_registry != nullptr
                                                        ? an::decodeImage(m_engine, execution, *m_registry, request)
                                                        : an::decodeImage(m_engine, execution, request);
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        if (!unchangedSinceProbed(frameIndex))
        {
            return refusal(an::ErrorCode::InvalidState, "'" + path + "' changed while it was read");
        }
        const std::optional<an::RawFrameDescriptor> frame = decoded->frame.descriptor();
        const std::optional<an::ImageLayout> layout = decoded->frame.layout();
        const std::optional<an::SampleType> stored = frame.has_value() ? frame->residentSampleType() : std::nullopt;
        const std::optional<an::ImageDescriptor> described =
            frame.has_value() ? frame->toImageDescriptor() : std::nullopt;
        if (!layout.has_value() || !stored.has_value() || !described.has_value() ||
            !sameFrameShape(*described, m_descriptor))
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           "'" + path +
                               "' no longer has the size, channels, representation and filter array it was "
                               "checked with");
        }
        const std::optional<an::ImageLayout> packed =
            an::ImageLayout::create(an::ImageLayoutRequest{.extent = m_descriptor.extent(),
                                                           .elementSizeBytes = static_cast<std::uint32_t>(
                                                               an::sampleTypeSizeBytes(m_descriptor.sampleType())),
                                                           .samplesPerPixel = 1u,
                                                           .planeCount = m_descriptor.extent().channelCount(),
                                                           .rowAlignmentBytes = 1u},
                                    m_engine.limits());
        if (!packed.has_value() || packed->totalBytes() > buffer.size())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "a decoded frame does not fit the stack's read slot");
        }
        if (an::Status copied = an::repackImageSamples(execution, *stored, decoded->frame.bytes().data(), *layout,
                                                       buffer.data(), *packed);
            copied.failed())
        {
            return copied;
        }
        const bool foreignOrder = frame->byteOrder() != an::ByteOrder::Native &&
                                  frame->byteOrder() != an::nativeByteOrder() && an::isByteOrderMeaningful(*stored);
        if (foreignOrder)
        {
            if (an::Status swapped = an::swapImageByteOrder(execution, *stored, buffer.data(), *packed);
                swapped.failed())
            {
                return swapped;
            }
        }
        m_layouts[frameIndex] = *packed;
        return an::StackSourceBand{.sampleType = *stored,
                                   .samples = buffer.data(),
                                   .layout = &*m_layouts[frameIndex],
                                   .frameIndex = frameIndex,
                                   .firstRow = 0u,
                                   .validity =
                                       an::StackBandValidity{.descriptor = an::ValidityDescriptor{}, .mask = nullptr}};
    }

private:
    [[nodiscard]] bool unchangedSinceProbed(const std::uint32_t frameIndex) const
    {
        if (frameIndex >= m_inputs.revisions.size())
        {
            return true;
        }
        const std::optional<FileRevisionStamp> now =
            ArtifactTracker::captureFileRevision(m_inputs.inputFiles[frameIndex].canonicalPath());
        return now.has_value() && m_inputs.revisions[frameIndex].sameRevision(*now);
    }

    const an::EngineContext& m_engine;
    const an::CodecRegistry* m_registry;
    const ReadableStackInputs& m_inputs;
    an::ImageDescriptor m_descriptor;
    std::vector<std::optional<an::ImageLayout>> m_layouts;
};

/// The combination and rejection a group's method and rejection texts name.
struct CombinationChoice
{
    an::StackCombination combination = an::StackCombination::Average;
    an::StackRejection rejection = an::StackRejection::None;
};

[[nodiscard]] an::Result<CombinationChoice> combinationOf(const StackingSettings& settings)
{
    const ImageIntegration::PersistedChoiceValue method =
        ImageIntegration::persistedChoiceFromText(PersistedChoice::StackMethod, settings.stackMethod);
    if (!method.known)
    {
        return refusal(an::ErrorCode::InvalidArgument, "Unknown stacking method '" + utf8(settings.stackMethod) + "'");
    }
    CombinationChoice choice;
    if (const std::optional<an::StackCombination> combination = ImageIntegration::stackCombinationForKey(method.key))
    {
        choice.combination = *combination;
        if (choice.combination == an::StackCombination::Average)
        {
            const ImageIntegration::PersistedChoiceValue rejection =
                ImageIntegration::persistedChoiceFromText(PersistedChoice::RejectionMethod, settings.rejectionMethod);
            if (!rejection.known)
            {
                return refusal(an::ErrorCode::InvalidArgument,
                               "Unknown rejection method '" + utf8(settings.rejectionMethod) + "'");
            }
            choice.rejection = ImageIntegration::stackRejectionForKey(rejection.key).value_or(an::StackRejection::None);
        }
        return choice;
    }
    if (const std::optional<an::StackRejection> rejection = ImageIntegration::stackRejectionForKey(method.key))
    {
        choice.rejection = *rejection;
        return choice;
    }
    return refusal(an::ErrorCode::InvalidArgument, "Unknown stacking method '" + utf8(settings.stackMethod) + "'");
}

/// The engine's stack request for a calibration group: its combination and rejection - a rejection only over enough
/// frames - uniform weights, the global normalization a flat group states, Float32 output and no maps. A bias or dark
/// master is combined without normalization, and a normalization its group still states is said in `notes`.
[[nodiscard]] an::Result<an::StackRequest>
calibrationStackRequest(const StackingSettings& settings, const bool normalizes,
                        const NormalizationMode statedNormalization, const an::ImageDescriptor& input,
                        const std::uint32_t frameCount, const QString& groupLabel, QStringList& notes)
{
    const an::Result<CombinationChoice> choice = combinationOf(settings);
    if (!choice.hasValue())
    {
        return choice.status();
    }
    an::StackRequest request{.input = an::StackInputDescription{.frameCount = frameCount, .descriptor = input},
                             .combination = choice->combination};
    if (choice->rejection != an::StackRejection::None && frameCount < an::kMinimumRejectionFrames)
    {
        notes.append(QStringLiteral("Group '%1' has %2 frame(s): %3 needs at least %4, so the master is combined "
                                    "without it.")
                         .arg(groupLabel)
                         .arg(frameCount)
                         .arg(settings.rejectionMethod)
                         .arg(an::kMinimumRejectionFrames));
    }
    else
    {
        request.rejection.method = choice->rejection;
    }
    if (an::stackRejectionUsesSigmaThresholds(request.rejection.method))
    {
        request.rejection.sigma = an::SigmaClipThresholds{.low = settings.sigmaLow, .high = settings.sigmaHigh};
    }
    else if (an::stackRejectionUsesMedianFractions(request.rejection.method))
    {
        request.rejection.medianFractions =
            an::MedianClipFractions{.low = settings.sigmaLow, .high = settings.sigmaHigh};
    }
    else if (an::stackRejectionUsesGesdtParameters(request.rejection.method))
    {
        request.rejection.gesdt =
            an::GesdtParameters{.maximumOutlierFraction = settings.sigmaLow, .significance = settings.sigmaHigh};
    }
    if (!settings.weighting.trimmed().isEmpty() && !isText(settings.weighting, "None"))
    {
        notes.append(QStringLiteral("Group '%1': a calibration master is combined with equal weights; the %2 "
                                    "weighting is not applied.")
                         .arg(groupLabel, settings.weighting));
    }
    request.weighting.method = an::StackWeighting::Uniform;

    const NormalizationMode leftOut =
        statedNormalization != NormalizationMode::None ? statedNormalization : settings.normalizationMode;
    if (!normalizes && leftOut != NormalizationMode::None)
    {
        StackingSettings stated = settings;
        stated.normalizationMode = leftOut;
        const QString described =
            leftOut == NormalizationMode::Local ? QStringLiteral("Local") : describeNormalizationSettings(stated);
        notes.append(QStringLiteral("Group '%1': a bias or dark master is combined without normalization; the "
                                    "group's %2 normalization is not applied.")
                         .arg(groupLabel, described));
    }
    else if (settings.normalizationMode != NormalizationMode::None)
    {
        if (settings.normalizationMode == NormalizationMode::Local)
        {
            notes.append(QStringLiteral("Group '%1': a calibration master is normalized globally; the local "
                                        "normalization is not applied.")
                             .arg(groupLabel));
        }
        request.normalization.scope = an::StackNormalizationScope::Global;
        const ImageIntegration::PersistedChoiceValue method = ImageIntegration::persistedChoiceFromInteger(
            PersistedChoice::GlobalNormalizationMethod, static_cast<int>(settings.globalNormalizationMethod));
        request.normalization.globalMethod = ImageIntegration::normalizationMethodForKey(method.key);
        if (!request.normalization.globalMethod.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "Unknown global normalization method");
        }
        const ImageIntegration::PersistedChoiceValue estimator = ImageIntegration::persistedChoiceFromInteger(
            PersistedChoice::ScaleEstimator, static_cast<int>(settings.scaleEstimator));
        const std::optional<an::ScaleEstimator> scale = ImageIntegration::scaleEstimatorForKey(estimator.key);
        if (!scale.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "Unknown scale estimator");
        }
        request.normalization.scaleEstimator = *scale;
    }
    request.output.sampleType = an::SampleType::Float32;

    const an::StackModelError refused = an::StackModel::check(request);
    if (refused != an::StackModelError::None)
    {
        return refusal(an::stackModelErrorCode(refused),
                       std::string("The master cannot be stacked: ") + an::stackModelErrorName(refused));
    }
    return request;
}

/// The stacked image of a result, with the reference frame's metadata moved by the stack's transition.
[[nodiscard]] an::Result<an::Image<float>> stackedImage(an::StackResult& result, const an::StackModel& model,
                                                        const an::ImageMetadata& reference)
{
    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::fromBytes(std::move(result.image));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::fromStorage(std::move(*storage), result.descriptor, reference);
    if (!image.hasValue())
    {
        return image.status();
    }
    const an::Result<an::MetadataTransition> change = an::stackTransition(model, result.report, reference.semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = image->transition(*change); stated.failed())
    {
        return stated;
    }
    return std::move(*image);
}

[[nodiscard]] StageResult engineFailure(const an::Status& status, IntegrationErrorCode stageCode,
                                        const QString& message, const QString& path)
{
    if (isCancellation(status))
    {
        return StageResult::cancelled();
    }
    return StageResult::failure(ImageIntegration::integrationErrorFromEngine(status, stageCode, message, path));
}

} // namespace

ReadableStackInputs loadReadableStackInputs(const ImageIntegration::WorkflowStageServices& services,
                                            const std::vector<FrameId>& inputFiles, const QString& frameType,
                                            IntegrationErrorReport& errorReport, QStringList& stageWarnings,
                                            ProgressReporter& reporter, StackInputPreloadDiagnostics* diagnostics,
                                            CancellationToken* token, StackInputPreflightProfile* profile)
{
    if (profile != nullptr)
    {
        *profile = {};
    }
    const ScopedPhaseTimer totalTimer(profile != nullptr ? &profile->total_nanoseconds : nullptr);
    StackInputPreloadDiagnostics local;
    StackInputPreloadDiagnostics& preflight = diagnostics != nullptr ? *diagnostics : local;
    preflight = {};

    ReadableStackInputs result;
    const auto stopped = [&]() {
        return (token != nullptr && token->isCancellationRequested()) || services.stopToken().stop_requested();
    };
    an::Result<an::consumer::OperationScope> scope = services.admitStageOperation();
    if (!scope.hasValue())
    {
        if (isCancellation(scope.status()))
        {
            result.cancelled = true;
            return result;
        }
        result.fatalError = ImageIntegration::integrationErrorFromEngine(
            scope.status(), IntegrationErrorCode::InternalError,
            QStringLiteral("The %1 frames could not be checked").arg(frameType));
        return result;
    }
    const an::CodecRegistry* registry = services.codecRegistry().get();

    std::optional<an::ImageDescriptor> first;
    for (const FrameId& frame : inputFiles)
    {
        if (stopped())
        {
            result.cancelled = true;
            return result;
        }
        if (!frame.isValid())
        {
            ++preflight.probeFailureCount;
            recordUnreadableFrame(frame, frameType, QStringLiteral("Invalid input frame ID."), errorReport,
                                  stageWarnings, reporter);
            continue;
        }
        const QString& canonical = frame.canonicalPath();
        const std::optional<FileRevisionStamp> before = ArtifactTracker::captureFileRevision(canonical);
        if (!before.has_value())
        {
            ++preflight.probeFailureCount;
            recordUnreadableFrame(frame, frameType, QStringLiteral("Could not capture a stable file revision."),
                                  errorReport, stageWarnings, reporter);
            continue;
        }
        const std::string path = utf8(canonical);
        const an::CodecReadRequest request{.path = path};
        an::Result<an::CodecImageInfo> probed = [&]() {
            ++preflight.metadataLoadCount;
            const ScopedPhaseTimer probeTimer(profile != nullptr ? &profile->probe_nanoseconds : nullptr);
            return registry != nullptr ? an::probeImage(scope->engine(), scope->execution(), *registry, request)
                                       : an::probeImage(scope->engine(), scope->execution(), request);
        }();
        if (!probed.hasValue())
        {
            if (isCancellation(probed.status()))
            {
                result.cancelled = true;
                return result;
            }
            ++preflight.probeFailureCount;
            recordUnreadableFrame(frame, frameType,
                                  QStringLiteral("Image readability probe failed: %1")
                                      .arg(ImageIntegration::engineFailureText(probed.status())),
                                  errorReport, stageWarnings, reporter);
            continue;
        }
        const std::optional<FileRevisionStamp> after = ArtifactTracker::captureFileRevision(canonical);
        if (!after.has_value() || !before->sameRevision(*after))
        {
            ++preflight.probeFailureCount;
            recordUnreadableFrame(frame, frameType, QStringLiteral("Image file changed during readability probing."),
                                  errorReport, stageWarnings, reporter);
            continue;
        }
        const std::optional<an::ImageDescriptor> described = probed->descriptor.toImageDescriptor();
        if (!described.has_value())
        {
            ++preflight.probeFailureCount;
            recordUnreadableFrame(frame, frameType,
                                  QStringLiteral("The image holds samples this engine does not stack."), errorReport,
                                  stageWarnings, reporter);
            continue;
        }
        if (first.has_value() && !sameFrameShape(*first, *described))
        {
            ++preflight.probeFailureCount;
            recordUnreadableFrame(frame, frameType,
                                  QStringLiteral("The image is %1 where the group's first readable frame is %2.")
                                      .arg(shapeText(*described), shapeText(*first)),
                                  errorReport, stageWarnings, reporter);
            continue;
        }
        if (probed->disposition == an::CodecProbeDisposition::PayloadValidatedWithoutDecode)
        {
            ++preflight.noPixelProbeCount;
        }
        else
        {
            ++preflight.inconclusiveProbeCount;
        }
        if (!first.has_value())
        {
            first = described;
        }
        result.inputFiles.push_back(frame);
        result.revisions.push_back(*before);
    }
    return result;
}

StageResult stackCalibrationGroup(const ImageIntegration::WorkflowStageServices& services,
                                  const CalibrationStackRequest& request, core::CalibrationMasterSet& masters,
                                  QStringList& stageWarnings, CancellationToken& token, ProgressReporter& reporter,
                                  CalibrationStackProfile* profile)
{
    if (request.inputs == nullptr || request.inputs->inputFiles.empty())
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::EmptyFrameList,
                                                     QStringLiteral("No input files provided for stacking")));
    }
    const ReadableStackInputs& inputs = *request.inputs;
    const auto frameCount = static_cast<std::uint32_t>(inputs.inputFiles.size());
    const QString stackFailure =
        QStringLiteral("The master of group '%1' could not be stacked").arg(request.groupLabel);

    GroupProgress progress(reporter, request);
    std::optional<an::consumer::OperationScope> scope;
    {
        an::Result<an::consumer::OperationScope> admitted = services.admitStageOperation(&progress);
        if (!admitted.hasValue())
        {
            return engineFailure(admitted.status(), IntegrationErrorCode::StackingFailed, stackFailure,
                                 request.outputPath);
        }
        scope.emplace(std::move(*admitted));
    }
    const an::CodecRegistry* registry = services.codecRegistry().get();

    // The frames' description and the reference frame's metadata, read from the first frame's header.
    const std::string firstPath = utf8(inputs.inputFiles.front().canonicalPath());
    const an::CodecReadRequest firstRequest{.path = firstPath, .validatePayload = false};
    const an::Result<an::CodecImageInfo> first =
        registry != nullptr ? an::probeImage(scope->engine(), scope->execution(), *registry, firstRequest)
                            : an::probeImage(scope->engine(), scope->execution(), firstRequest);
    if (!first.hasValue())
    {
        return engineFailure(first.status(), IntegrationErrorCode::FileNotReadable, stackFailure,
                             inputs.inputFiles.front().canonicalPath());
    }
    const std::optional<an::ImageDescriptor> stored = first->descriptor.toImageDescriptor();
    if (!stored.has_value())
    {
        return StageResult::failure(
            IntegrationError(IntegrationErrorCode::InvalidFileFormat,
                             QStringLiteral("The first frame holds samples this engine does not stack"),
                             inputs.inputFiles.front().canonicalPath()));
    }
    // Every frame is read as fractions of its representation's full scale, whatever domain its header states.
    an::ImageDescriptorRequest described = stored->toRequest();
    described.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> input = an::ImageDescriptor::create(described);
    if (!input.has_value())
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::InvalidFileFormat,
                                                     QStringLiteral("The frames have no linear description"),
                                                     inputs.inputFiles.front().canonicalPath()));
    }

    QStringList notes;
    const an::Result<an::StackRequest> stackRequest =
        calibrationStackRequest(request.stacking, request.normalizes, request.statedNormalization, *input, frameCount,
                                request.groupLabel, notes);
    if (!stackRequest.hasValue())
    {
        return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
            stackRequest.status(), IntegrationErrorCode::InvalidConfiguration, stackFailure, request.outputPath));
    }
    for (const QString& note : notes)
    {
        stageWarnings.append(note);
        reporter.logger().warning(note);
        LOG_WARNING("{}", note.toStdString());
    }
    const std::optional<an::StackModel> model = an::StackModel::create(*stackRequest);
    if (!model.has_value())
    {
        return StageResult::failure(IntegrationError::stackingFailed(
            QStringLiteral("The stack model refused group '%1'").arg(request.groupLabel)));
    }

    // The masters applied on the way in, read once for every group of the stage that names them.
    an::CalibrationMasters applied;
    for (const auto& [master, view, role] : {std::tuple{&request.biasMaster, &applied.bias, "bias"},
                                             std::tuple{&request.darkMaster, &applied.dark, "dark"}})
    {
        if (!master->has_value())
        {
            continue;
        }
        an::Result<const an::Image<float>*> read = masters.master(*scope, **master);
        if (!read.hasValue())
        {
            return engineFailure(read.status(), IntegrationErrorCode::FileNotReadable,
                                 QStringLiteral("The %1 master could not be read").arg(QLatin1String(role)),
                                 (*master)->canonicalPath());
        }
        const auto masterView = (*read)->view();
        const std::optional<an::ImageDescriptor> masterDescriptor = (*read)->descriptor();
        if (!masterView.has_value() || !masterDescriptor.has_value())
        {
            return StageResult::failure(IntegrationError::fileNotReadable((*master)->canonicalPath(),
                                                                          QStringLiteral("The master holds nothing")));
        }
        an::Result<an::CalibrationFrameView> frameView = an::CalibrationFrameView::create(
            masterView->data(), masterView->layout().totalBytes(), masterView->layout(), *masterDescriptor);
        if (!frameView.hasValue())
        {
            return engineFailure(frameView.status(), IntegrationErrorCode::CalibrationFailed,
                                 QStringLiteral("The %1 master cannot calibrate the group").arg(QLatin1String(role)),
                                 (*master)->canonicalPath());
        }
        *view = *frameView;
    }
    std::array<an::CalibrationPlan, 1u> plans{};
    an::StackSourceContract contract{.state = an::StackFrameState::Calibrated};
    if (applied.bias.has_value() || applied.dark.has_value())
    {
        an::CalibrationRequest calibration;
        calibration.flatNormalization = an::FlatNormalizationMode::None;
        an::Result<an::CalibrationPlan> plan = an::prepareImageCalibration(
            scope->execution(), an::stackSourceCalibrationDescriptor(*model), applied, calibration);
        if (!plan.hasValue())
        {
            const FrameId& named = request.darkMaster.has_value() ? *request.darkMaster : *request.biasMaster;
            return engineFailure(
                plan.status(), IntegrationErrorCode::CalibrationFailed,
                QStringLiteral("The masters cannot calibrate the frames of group '%1'").arg(request.groupLabel),
                named.canonicalPath());
        }
        plans[0] = std::move(*plan);
        contract.state = an::StackFrameState::Raw;
        contract.calibration = std::span<const an::CalibrationPlan>(plans);
        contract.masters = &applied;
    }

    an::StackExecutionRequest execution;
    if (request.stacking.limitMemory && request.stacking.maxMemoryMb > 0)
    {
        execution.workingSetLimitBytes = static_cast<std::uint64_t>(request.stacking.maxMemoryMb) * kBytesPerMebibyte;
    }
    CalibrationFrameSource source(scope->engine(), registry, inputs, *stored);
    an::Result<an::StackResult> stacked = [&]() {
        const ScopedPhaseTimer stackTimer(profile != nullptr ? &profile->stack_nanoseconds : nullptr);
        return an::executeStack(scope->engine(), scope->execution(), *model, source, contract, execution,
                                an::StackExecutionOutputs{});
    }();
    if (!stacked.hasValue())
    {
        return engineFailure(stacked.status(), IntegrationErrorCode::StackingFailed, stackFailure, request.outputPath);
    }
    an::Result<an::Image<float>> image = stackedImage(*stacked, *model, first->metadata);
    if (!image.hasValue())
    {
        return engineFailure(image.status(), IntegrationErrorCode::StackingFailed, stackFailure, request.outputPath);
    }
    if (token.isCancellationRequested())
    {
        return StageResult::cancelled();
    }

    const an::Result<an::ArtifactIdentity> identity =
        ImageIntegration::workflowArtifactIdentity(request.artifactHash, request.runId);
    if (!identity.hasValue())
    {
        return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
            identity.status(), IntegrationErrorCode::InternalError,
            QStringLiteral("The master's artifact identity could not be stated"), request.outputPath));
    }
    {
        const ScopedPhaseTimer writeTimer(profile != nullptr ? &profile->write_nanoseconds : nullptr);
        const an::Status published =
            ImageIntegration::publishWorkflowImage(*scope, *image, request.outputPath, *identity);
        if (published.failed())
        {
            return engineFailure(published, IntegrationErrorCode::FileNotWritable,
                                 QStringLiteral("Failed to save the master"), request.outputPath);
        }
    }
    reportStageProgress(reporter, request.stage, request.groupIndex, request.totalGroups, 1.0f, request.operation,
                        request.outputPath);
    return StageResult::success();
}

StageResult runCalibrationStage(const CalibrationStageRun& run, const WorkflowManifest& manifest,
                                const ImageIntegration::WorkflowStageServices* services,
                                WorkflowExecutionCounterAccumulator* counters,
                                WorkflowArtifactPublicationLedger* ledger, CancellationToken& token,
                                ProgressReporter& reporter)
{
    if (run.groups.empty())
    {
        return StageResult::skipped();
    }
    if (services == nullptr)
    {
        return StageResult::failure(IntegrationError::internalError(
            QStringLiteral("The %1 stage runs only inside a workflow run, which lends it the session")
                .arg(run.frameType)));
    }
    ArtifactTracker tracker(manifest.outputDirectory, counters, sessionArtifactStampReader(services->session()));
    const int totalGroups = static_cast<int>(run.groups.size());
    reporter.logger().stageStarted(run.stageName, totalGroups);
    QElapsedTimer stageTimer;
    stageTimer.start();

    CalibrationStackProfile stageProfile;
    QStringList stageWarnings;
    IntegrationErrorReport errorReport;
    core::CalibrationMasterSet masters(services->codecRegistry());
    const QString capitalized = run.frameType.left(1).toUpper() + run.frameType.mid(1);
    const auto cancelRequested = [&token]() { return token.isCancellationRequested(); };

    int groupIndex = 0;
    for (const auto* groupPtr : run.groups)
    {
        const auto& group = *groupPtr;
        ++groupIndex;
        if (token.isCancellationRequested())
        {
            return StageResult::cancelled();
        }

        const QString groupLabel = group.groupKey.toCompactString();
        reporter.logger().groupStarted(groupIndex, totalGroups, groupLabel);
        QElapsedTimer groupTimer;
        groupTimer.start();

        const QString outputPath = QDir(manifest.outputDirectory).filePath(group.outputFilename);
        const QString hash = computeCalibrationGroupHash(manifest, group);

        if (group.existingMaster.has_value())
        {
            reporter.logger().operationDetail(QStringLiteral("Using existing master"),
                                              QFileInfo(group.existingMaster->canonicalPath()).fileName());
            reportStageProgress(reporter, run.stage, groupIndex, totalGroups, 1.0f,
                                QStringLiteral("%1 stacking (existing master)").arg(capitalized),
                                group.existingMaster->toString());
            reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTimer.elapsed());
            continue;
        }
        if (group.skipIfExists && tracker.isArtifactValid(outputPath, hash))
        {
            reporter.logger().operationDetail(QStringLiteral("Skipping"), QStringLiteral("Output up-to-date"));
            reportStageProgress(reporter, run.stage, groupIndex, totalGroups, 1.0f,
                                QStringLiteral("%1 stacking (skipped)").arg(capitalized), outputPath);
            reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTimer.elapsed());
            continue;
        }
        if (group.inputFiles.empty())
        {
            return StageResult::failure(IntegrationError(IntegrationErrorCode::EmptyFrameList,
                                                         QStringLiteral("No input files provided for stacking")));
        }

        const std::optional<FrameId> bias = run.appliesBias ? group.biasMaster : std::nullopt;
        const std::optional<FrameId> dark = run.appliesDark ? group.darkMaster : std::nullopt;
        reporter.logger().operationDetail(QStringLiteral("Inputs"),
                                          QStringLiteral("%1 frame(s)").arg(static_cast<int>(group.inputFiles.size())));
        if (run.appliesBias || run.appliesDark)
        {
            const QString calibration =
                bias.has_value() && dark.has_value() ? QStringLiteral("Bias + dark masters")
                : bias.has_value() ? QStringLiteral("Bias master: %1").arg(QFileInfo(bias->canonicalPath()).fileName())
                : dark.has_value() ? QStringLiteral("Dark master only")
                                   : QStringLiteral("None");
            reporter.logger().operationDetail(QStringLiteral("Calibration"), calibration);
        }
        reporter.logger().operationDetail(QStringLiteral("Settings"), describeStackingSettings(group.stacking));
        if (run.appliesDark && !bias.has_value() && !dark.has_value())
        {
            const QString warning =
                QStringLiteral("Flat group '%1' has no bias or dark master assigned; master flat will be uncalibrated.")
                    .arg(groupLabel);
            LOG_WARNING("  {}", warning.toStdString());
            reporter.logger().warning(warning);
            stageWarnings.append(warning);
        }
        QString operation = QStringLiteral("%1 stacking").arg(capitalized);
        if (bias.has_value() && dark.has_value())
        {
            operation = QStringLiteral("Calibrating + stacking %1s (bias + dark)").arg(run.frameType);
        }
        else if (bias.has_value())
        {
            operation = QStringLiteral("Calibrating + stacking %1s (bias)").arg(run.frameType);
        }
        else if (dark.has_value())
        {
            operation = QStringLiteral("Calibrating + stacking %1s (dark)").arg(run.frameType);
        }

        const qint64 groupStartedMs = groupTimer.elapsed();
        StackInputPreflightProfile preflightProfile;
        const ReadableStackInputs readable =
            loadReadableStackInputs(*services, group.inputFiles, run.frameType, errorReport, stageWarnings, reporter,
                                    nullptr, &token, &preflightProfile);
        if (readable.cancelled)
        {
            return StageResult::cancelled();
        }
        if (readable.fatalError.has_value())
        {
            return StageResult::failure(*readable.fatalError);
        }
        if ((group.inputFiles.size() > 1u && readable.inputFiles.size() < 2u) || readable.inputFiles.empty())
        {
            const QString detail = QStringLiteral("Only %1 of %2 %3 frame(s) were readable.")
                                       .arg(static_cast<int>(readable.inputFiles.size()))
                                       .arg(static_cast<int>(group.inputFiles.size()))
                                       .arg(run.frameType);
            LOG_ERROR("Insufficient readable {} frames for group '{}': {}", run.frameType.toStdString(),
                      groupLabel.toStdString(), detail.toStdString());
            StageResult failure = StageResult::failure(IntegrationError(
                IntegrationErrorCode::InsufficientFrames,
                QStringLiteral("Too few readable %1 frames for stacking").arg(run.frameType), {}, detail));
            failure.warnings = stageWarnings;
            failure.errorReport = errorReport;
            return failure;
        }
        if (readable.inputFiles.size() != group.inputFiles.size())
        {
            reporter.logger().operationDetail(QStringLiteral("Readable inputs"),
                                              QStringLiteral("%1 of %2 frame(s)")
                                                  .arg(static_cast<int>(readable.inputFiles.size()))
                                                  .arg(static_cast<int>(group.inputFiles.size())));
        }

        CalibrationStackRequest request;
        request.stage = run.stage;
        request.groupIndex = groupIndex;
        request.totalGroups = totalGroups;
        request.operation = operation;
        request.groupLabel = groupLabel;
        request.inputs = &readable;
        request.outputPath = outputPath;
        request.stacking = group.stacking;
        request.normalizes = run.normalizes;
        request.statedNormalization = group.statedNormalization;
        request.biasMaster = bias;
        request.darkMaster = dark;
        request.artifactHash = hash;
        request.runId = manifest.executionId;
        StageResult stacked =
            stackCalibrationGroup(*services, request, masters, stageWarnings, token, reporter, &stageProfile);
        if (!stacked.isSuccess())
        {
            if (!stacked.isCancelled())
            {
                LOG_ERROR("{} stacking group '{}' failed: {}", capitalized.toStdString(), groupLabel.toStdString(),
                          stacked.error.toLogString().toStdString());
            }
            stacked.warnings = stageWarnings;
            stacked.errorReport = errorReport;
            return stacked;
        }

        accumulateStackInputPreflightProfile(stageProfile, preflightProfile);
        stageProfile.frames_stacked += static_cast<std::uint64_t>(readable.inputFiles.size());
        stageProfile.loop_nanoseconds += static_cast<std::uint64_t>(groupTimer.elapsed() - groupStartedMs) * 1000000ull;

        // The record is made only after the master's write committed, so a run that stops between them leaves a
        // whole master with no record, which the next run recomputes, and never a record of a master not written.
        const auto committed = FinalCommittedArtifact::fromFile(outputPath, counters, cancelRequested);
        if (!committed.has_value() || !tracker.recordCurrentRunCommittedArtifact(*committed, hash, cancelRequested))
        {
            if (token.isCancellationRequested())
            {
                return StageResult::cancelled();
            }
            return StageResult::failure(IntegrationError::fileNotReadable(
                outputPath, QStringLiteral("Failed to persist the published %1 master provenance").arg(run.frameType)));
        }
        if (ledger != nullptr && !ledger->recordPublishedArtifact(*committed))
        {
            return StageResult::failure(IntegrationError::fileNotReadable(
                outputPath, QStringLiteral("Failed to record the published %1 master revision").arg(run.frameType)));
        }
        LOG_INFO("{} stacking group '{}' complete: {}", capitalized.toStdString(), groupLabel.toStdString(),
                 outputPath.toStdString());
        reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTimer.elapsed());
    }

    if (const QString profile = formatCalibrationStackProfile(stageProfile); !profile.isEmpty())
    {
        reporter.logger().operationDetail(QStringLiteral("%1 stacking profile").arg(capitalized), profile);
        reporter.logger().operationDetail(QStringLiteral("%1 stacking throughput").arg(capitalized),
                                          formatCalibrationStackThroughput(stageProfile));
    }
    reporter.logger().stageCompleted(run.stageName, totalGroups, stageTimer.elapsed());
    if (!errorReport.errors.empty())
    {
        errorReport.warnings = stageWarnings;
        return StageResult::partialSuccess(errorReport);
    }
    StageResult success = StageResult::success();
    success.warnings = stageWarnings;
    return success;
}

} // namespace stage_utils

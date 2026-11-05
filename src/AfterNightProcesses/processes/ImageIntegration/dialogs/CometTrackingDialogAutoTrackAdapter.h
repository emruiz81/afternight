// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the comet tracking dialog's Auto-Track and Find Moving Object buttons run on a worker: the integration's guided
/// tracker over the observations the dialog holds, and the engine's blind moving-object search over the group's
/// frames, each answering in the dialog's diagnostics.
///
/// Neither reads a file itself: the caller lends a reader that reads a frame's refinement file, demosaiced, as an image
/// of the operation's engine, and the execution the work runs under. Every position is an engine pixel coordinate of
/// the frame's own file, as the dialog's marks are.

#include "processes/ImageIntegration/core/CometGuidedAutoTracker.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialog.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

#include <AfterNightEngine/Algorithms/Registration/CometDetection.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace an::ui
{
/// Reads a frame's refinement file as the tracker measures it: demosaiced, one sample type, an image of the engine the
/// caller's execution belongs to. A frame that cannot be read answers why.
using CometTrackingFrameReader = std::function<an::Result<an::Image<float>>(const CometTrackingFrameContext&)>;

/// The smallest residual above the temporal median the blind search admits, as a fraction of full scale: one code of
/// a sixteen-bit sensor, a one-unit threshold stated for frames read as fractions.
inline constexpr double kCometDialogMinimumPositiveResidual = 1.0 / 65535.0;

/// The blind search's options as the dialog asks for them: the engine's defaults, with the residual threshold stated
/// for frames read as fractions of full scale.
[[nodiscard]] inline an::CometMovingObjectDetectionOptions cometDialogDetectionOptions()
{
    an::CometMovingObjectDetectionOptions options;
    options.minimumPositiveResidual = kCometDialogMinimumPositiveResidual;
    return options;
}

struct CometTrackingDialogAutoTrackRequest
{
    std::vector<FrameInfo> frames;
    FrameId referenceFrame;
    RegistrationSettings::CometTrackingSettings settings;

    /// Reads the frame each refinement measures; a frame is read once while the tracker asks about it.
    CometTrackingFrameReader frameReader;
    CometGuidedCancellation isCancellationRequested;
};

struct CometTrackingDialogBlindDetectRequest
{
    std::vector<FrameInfo> frames;
    FrameId referenceFrame;
    RegistrationSettings::CometTrackingSettings settings;

    /// The search's thresholds, in the numeric domain the reader answers in.
    an::CometMovingObjectDetectionOptions detectionOptions = cometDialogDetectionOptions();

    /// Reads every frame the search compares; the frames must share one size and description.
    CometTrackingFrameReader frameReader;
    CometGuidedCancellation isCancellationRequested;
};

struct CometTrackingDialogAutoTrackResult
{
    bool succeeded = false;
    bool productionFitAvailable = false;
    bool bootstrapOnly = false;
    bool cancelled = false;
    std::vector<CometTrackingDialogDiagnostic> diagnostics;
    QString message;
};

[[nodiscard]] inline QString cometDialogAutoTrackStatusText(const CometGuidedAutoTrackingStatus status)
{
    switch (status)
    {
    case CometGuidedAutoTrackingStatus::AcceptedObservation:
        return QStringLiteral("Accepted");
    case CometGuidedAutoTrackingStatus::Candidate:
        return QStringLiteral("Candidate");
    case CometGuidedAutoTrackingStatus::PredictedOnly:
        return QStringLiteral("Predicted");
    case CometGuidedAutoTrackingStatus::Cancelled:
        return QStringLiteral("Cancelled");
    case CometGuidedAutoTrackingStatus::Failed:
    default:
        return QStringLiteral("Failed");
    }
}

[[nodiscard]] inline QPointF cometDialogAutoTrackDisplayPosition(const CometGuidedAutoTrackingDiagnostic& diagnostic)
{
    switch (diagnostic.status)
    {
    case CometGuidedAutoTrackingStatus::Candidate:
    case CometGuidedAutoTrackingStatus::AcceptedObservation:
        return QPointF(diagnostic.refinedSourcePosition.x, diagnostic.refinedSourcePosition.y);
    case CometGuidedAutoTrackingStatus::PredictedOnly:
    case CometGuidedAutoTrackingStatus::Failed:
    case CometGuidedAutoTrackingStatus::Cancelled:
    default:
        return QPointF(diagnostic.predictedSourcePosition.x, diagnostic.predictedSourcePosition.y);
    }
}

[[nodiscard]] inline std::vector<CometTrackingFrameContextInput>
cometDialogAutoTrackFrameInputs(const std::vector<FrameInfo>& frames)
{
    std::vector<CometTrackingFrameContextInput> inputs;
    inputs.reserve(frames.size());

    for (const FrameInfo& frame : frames)
    {
        const QDateTime captured = frame.metadata.capturedAtUtc();
        CometTrackingFrameContextInput input;
        input.originalFrameId = frame.id;
        input.refinementSourcePath = frame.id.canonicalPath();
        input.dateObs = captured.isValid() ? captured.toUTC().toString(Qt::ISODateWithMs) : QString();
        input.exposureSeconds = frame.metadata.exposureSeconds();
        inputs.push_back(std::move(input));
    }

    return inputs;
}

[[nodiscard]] inline std::vector<CometGuidedAutoTrackingFrame>
cometDialogAutoTrackGuidedFrames(const std::vector<CometTrackingFrameContext>& contexts)
{
    std::vector<CometGuidedAutoTrackingFrame> frames;
    frames.reserve(contexts.size());
    for (const CometTrackingFrameContext& context : contexts)
    {
        frames.push_back(CometGuidedAutoTrackingFrame{.context = context});
    }
    return frames;
}

[[nodiscard]] inline std::vector<CometTrackingDialogDiagnostic>
cometDialogDiagnosticsFromGuidedResult(const std::vector<CometGuidedAutoTrackingDiagnostic>& diagnostics)
{
    std::vector<CometTrackingDialogDiagnostic> dialog_diagnostics;
    dialog_diagnostics.reserve(diagnostics.size());

    for (const CometGuidedAutoTrackingDiagnostic& diagnostic : diagnostics)
    {
        CometTrackingDialogDiagnostic dialog_diagnostic;
        dialog_diagnostic.frameId = diagnostic.originalFrameId.canonicalPath();
        dialog_diagnostic.sourcePosition = cometDialogAutoTrackDisplayPosition(diagnostic);
        dialog_diagnostic.confidence = diagnostic.confidence;
        dialog_diagnostic.state = cometDialogAutoTrackStatusText(diagnostic.status);
        dialog_diagnostic.message = diagnostic.message;
        dialog_diagnostics.push_back(std::move(dialog_diagnostic));
    }

    return dialog_diagnostics;
}

/// How much a blind candidate is to be trusted: its persistence, its mean integrated signal-to-noise ratio against
/// fifty and its residual from a straight track against five pixels, each weighted as stated here.
[[nodiscard]] inline double cometDialogBlindDetectConfidence(const an::CometMovingObjectCandidate& candidate)
{
    const double snr_score = (std::min)(candidate.meanIntegratedSignalToNoise / 50.0, 1.0);
    const double residual_score = candidate.rootMeanSquareResidualPixels <= 0.0
                                      ? 1.0
                                      : (std::max)(0.0, 1.0 - (candidate.rootMeanSquareResidualPixels / 5.0));
    return (std::clamp)((candidate.persistence * 0.55) + (snr_score * 0.30) + (residual_score * 0.15), 0.0, 1.0);
}

/// The line a blind candidate's diagnostics carry: its detections and integrated signal, its motion, its residual and
/// its persistence.
[[nodiscard]] inline QString cometDialogBlindDetectCandidateMessage(const an::CometMovingObjectCandidate& candidate)
{
    double signal = 0.0;
    for (const an::CometMovingObjectPoint& point : candidate.points)
    {
        signal += point.integratedSignal;
    }
    const QString summary = QStringLiteral("Moving-object candidate track with %1 detections and integrated signal %2.")
                                .arg(static_cast<qulonglong>(candidate.points.size()))
                                .arg(signal, 0, 'f', 6);
    return QStringLiteral("%1 Motion: %2 px. RMS: %3 px. Persistence: %4%.")
        .arg(summary)
        .arg(candidate.motionPixels, 0, 'f', 2)
        .arg(candidate.rootMeanSquareResidualPixels, 0, 'f', 2)
        .arg(candidate.persistence * 100.0, 0, 'f', 0);
}

/// One diagnostic per point of every candidate, each point named by the frame it was found in.
///
/// @param candidates the search's candidates, in its order.
/// @param frameIds the frame each detection identity names: the identity is the index into this list.
/// @return the diagnostics, every one a candidate the person may accept.
[[nodiscard]] inline std::vector<CometTrackingDialogDiagnostic>
cometDialogDiagnosticsFromBlindCandidates(const std::vector<an::CometMovingObjectCandidate>& candidates,
                                          const std::vector<QString>& frameIds)
{
    std::vector<CometTrackingDialogDiagnostic> diagnostics;
    for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index)
    {
        const an::CometMovingObjectCandidate& candidate = candidates[candidate_index];
        const QString candidate_id = QStringLiteral("blind-%1").arg(candidate_index);
        const QString message = cometDialogBlindDetectCandidateMessage(candidate);
        const double confidence = cometDialogBlindDetectConfidence(candidate);
        for (const an::CometMovingObjectPoint& point : candidate.points)
        {
            CometTrackingDialogDiagnostic diagnostic;
            diagnostic.candidateId = candidate_id;
            diagnostic.frameId =
                point.frameId < frameIds.size() ? frameIds[static_cast<std::size_t>(point.frameId)] : QString();
            diagnostic.sourcePosition = QPointF(point.position.x, point.position.y);
            diagnostic.confidence = confidence;
            diagnostic.state = QStringLiteral("Candidate");
            diagnostic.message = message;
            diagnostics.push_back(std::move(diagnostic));
        }
    }
    return diagnostics;
}

/// A frame the reader read, held with the layout its view is checked against for as long as a search borrows it.
struct CometDialogReadFrame
{
    /// The samples.
    an::Image<float> image;

    /// Where they sit.
    an::ImageLayout layout;

    /// What they mean.
    an::ImageDescriptor descriptor;
};

/// A frame the reader read, checked as a surface a comet is measured on.
///
/// @param image what the reader answered.
/// @return the frame, or why it holds nothing.
[[nodiscard]] inline an::Result<CometDialogReadFrame> cometDialogReadFrameOf(an::Image<float>&& image)
{
    const auto view = std::as_const(image).view();
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    if (!view.has_value() || !descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the comet frame holds nothing");
    }
    an::ImageLayout layout = view->layout();
    an::ImageDescriptor described = *descriptor;
    return CometDialogReadFrame{std::move(image), std::move(layout), std::move(described)};
}

/// The checked view a measurement reads of a frame read.
///
/// @param frame the frame; it must outlive the view.
/// @return the view, or why the frame is not one a comet is measured on.
[[nodiscard]] inline an::Result<an::StarFrameView> cometDialogFrameView(const CometDialogReadFrame& frame)
{
    const auto view = frame.image.view();
    if (!view.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the comet frame holds nothing");
    }
    return an::StarFrameView::create(view->data(), frame.layout.totalBytes(), frame.layout, frame.descriptor);
}

/// Runs the guided tracker over the dialog's observations.
///
/// @param execution the caller's admitted execution; the reader's images belong to its engine.
/// @param request the frames, the observations and the reader.
/// @return the tracker's diagnostics, worded for the dialog.
[[nodiscard]] inline CometTrackingDialogAutoTrackResult
runCometTrackingDialogAutoTrack(const an::ExecutionContext& execution,
                                const CometTrackingDialogAutoTrackRequest& request)
{
    CometTrackingDialogAutoTrackResult result;
    if (!request.referenceFrame.isValid())
    {
        result.message = QStringLiteral("Select a comet tracking reference frame before auto-tracking.");
        return result;
    }
    if (request.frames.empty())
    {
        result.message = QStringLiteral("No light frames are available for comet auto-tracking.");
        return result;
    }

    CometTrackingFrameContextRequest context_request;
    context_request.cometSettings = request.settings;
    context_request.resolvedReferenceFrame = request.referenceFrame;
    context_request.frames = cometDialogAutoTrackFrameInputs(request.frames);
    const CometTrackingFrameContextResult contexts = buildCometTrackingFrameContexts(context_request);
    if (!contexts.succeeded)
    {
        result.message = contexts.message;
        return result;
    }

    // The tracker asks about one frame at a time, so the frame last read is kept until it asks about another.
    QString cached_path;
    std::optional<CometDialogReadFrame> cached_frame;

    CometGuidedAutoTrackingRequest guided_request;
    guided_request.frames = cometDialogAutoTrackGuidedFrames(contexts.contexts);
    guided_request.observations = request.settings.observations;
    guided_request.trackingOptions = request.settings.trackingOptions;
    guided_request.fitOptions = request.settings.fitOptions;
    guided_request.isCancellationRequested = request.isCancellationRequested;
    guided_request.refiner =
        [&](const CometTrackingFrameContext& context, const an::PixelCoordinate& seed,
            const an::CometCenterRefinementOptions& options) -> an::Result<CometRefinementMeasurement> {
        if (!request.frameReader)
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "no frame reader is available");
        }
        if (!cached_frame.has_value() || cached_path != context.refinementSourcePath)
        {
            cached_frame.reset();
            cached_path.clear();
            an::Result<an::Image<float>> image = request.frameReader(context);
            if (!image.hasValue())
            {
                return image.status();
            }
            an::Result<CometDialogReadFrame> read = cometDialogReadFrameOf(std::move(*image));
            if (!read.hasValue())
            {
                return read.status();
            }
            cached_frame.emplace(std::move(*read));
            cached_path = context.refinementSourcePath;
        }
        const an::Result<an::StarFrameView> view = cometDialogFrameView(*cached_frame);
        if (!view.hasValue())
        {
            return view.status();
        }
        // A colour frame is measured in green, where a comet's nucleus is brightest against the sky.
        an::CometCenterRefinementOptions measured = options;
        measured.channelIndex = cached_frame->descriptor.extent().channelCount() >= 3u ? 1u : 0u;
        const an::Result<an::CometCenterRefinementResult> refined =
            an::refineCometCenter(execution, *view, seed, measured);
        if (!refined.hasValue())
        {
            return refined.status();
        }
        return CometRefinementMeasurement{.refined = *refined,
                                          .width = cached_frame->descriptor.extent().width(),
                                          .height = cached_frame->descriptor.extent().height()};
    };

    const CometGuidedAutoTrackingResult guided_result = runCometGuidedAutoTracking(execution, guided_request);
    result.succeeded = guided_result.succeeded;
    result.productionFitAvailable = guided_result.productionFitAvailable;
    result.bootstrapOnly = guided_result.bootstrapOnly;
    result.cancelled = guided_result.cancelled;
    result.message = guided_result.message;
    result.diagnostics = cometDialogDiagnosticsFromGuidedResult(guided_result.diagnostics);
    return result;
}

/// Searches the group's frames for a source that moves through them, with no observation to start from.
///
/// Frames the context builder could not place in time, and frames the reader cannot read, are left out; the engine's
/// search compares the rest pixel by pixel, so they must share one size and description.
///
/// @param execution the caller's admitted execution; the reader's images belong to its engine.
/// @param request the frames, the thresholds and the reader.
/// @return every candidate's points as diagnostics the person may accept, never a fitted track.
[[nodiscard]] inline CometTrackingDialogAutoTrackResult
runCometTrackingDialogBlindDetect(const an::ExecutionContext& execution,
                                  const CometTrackingDialogBlindDetectRequest& request)
{
    CometTrackingDialogAutoTrackResult result;
    if (!request.referenceFrame.isValid())
    {
        result.message = QStringLiteral("Select a comet tracking reference frame before moving-object search.");
        return result;
    }
    if (request.frames.empty())
    {
        result.message = QStringLiteral("No light frames are available for moving-object search.");
        return result;
    }

    CometTrackingFrameContextRequest context_request;
    context_request.cometSettings = request.settings;
    context_request.resolvedReferenceFrame = request.referenceFrame;
    context_request.frames = cometDialogAutoTrackFrameInputs(request.frames);
    const CometTrackingFrameContextResult contexts = buildCometTrackingFrameContexts(context_request);
    if (!contexts.succeeded)
    {
        result.message = contexts.message;
        return result;
    }

    // Every frame is read before any view is made, so no view borrows a frame the list later moves.
    std::vector<CometDialogReadFrame> read_frames;
    std::vector<an::UtcInstant> midpoints;
    std::vector<QString> frame_ids;
    read_frames.reserve(contexts.contexts.size());
    for (const CometTrackingFrameContext& context : contexts.contexts)
    {
        if (request.isCancellationRequested && request.isCancellationRequested())
        {
            result.cancelled = true;
            result.message = QStringLiteral("Moving-object search was cancelled.");
            return result;
        }
        if (context.status != CometTrackingFrameContextStatus::Ready || !context.midpoint.has_value() ||
            !request.frameReader)
        {
            continue;
        }

        an::Result<an::Image<float>> image = request.frameReader(context);
        if (!image.hasValue())
        {
            if (image.status().code() == an::ErrorCode::Cancelled)
            {
                result.cancelled = true;
                result.message = QStringLiteral("Moving-object search was cancelled.");
                return result;
            }
            continue;
        }
        an::Result<CometDialogReadFrame> read = cometDialogReadFrameOf(std::move(*image));
        if (!read.hasValue())
        {
            continue;
        }
        read_frames.push_back(std::move(*read));
        midpoints.push_back(*context.midpoint);
        frame_ids.push_back(context.originalFrameId.canonicalPath());
    }

    // A search over fewer frames than a tracklet needs is refused before any pixel is looked at.
    const std::size_t minimum_detections =
        (std::max)(std::size_t{2u}, static_cast<std::size_t>(request.detectionOptions.minimumDetections));
    if (read_frames.size() < minimum_detections)
    {
        result.message = QStringLiteral("Moving-object detection requires enough star-aligned frames for a tracklet.");
        return result;
    }

    std::vector<an::CometDetectionFrame> detection_frames;
    detection_frames.reserve(read_frames.size());
    for (std::size_t index = 0; index < read_frames.size(); ++index)
    {
        an::Result<an::StarFrameView> view = cometDialogFrameView(read_frames[index]);
        if (!view.hasValue())
        {
            result.message = QStringLiteral("Moving-object detection requires valid star-aligned image frames: %1")
                                 .arg(QString::fromUtf8(view.status().message()));
            return result;
        }
        detection_frames.push_back(an::CometDetectionFrame{.frameId = static_cast<std::uint64_t>(index),
                                                           .midpoint = midpoints[index],
                                                           .frame = *view,
                                                           .required = true});
    }

    const an::Result<an::CometMovingObjectDetectionResult> detection = an::detectMovingCometCandidates(
        execution, std::span<const an::CometDetectionFrame>(detection_frames), request.detectionOptions);
    if (!detection.hasValue())
    {
        if (detection.status().code() == an::ErrorCode::Cancelled)
        {
            result.cancelled = true;
            result.message = QStringLiteral("Moving-object search was cancelled.");
            return result;
        }
        result.message = QString::fromUtf8(detection.status().message());
        return result;
    }

    result.succeeded = true;
    result.bootstrapOnly = true;
    result.productionFitAvailable = false;
    result.message = detection->candidates.empty()
                         ? QStringLiteral("Moving-object search completed; no candidate tracks found.")
                         : QStringLiteral("Moving-object search found %1 candidate track(s).")
                               .arg(static_cast<qulonglong>(detection->candidates.size()));
    result.diagnostics = cometDialogDiagnosticsFromBlindCandidates(detection->candidates, frame_ids);
    return result;
}
} // namespace an::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - deriving a stack's frame weights from its measurements.
//
// Every source's rule, its dispositions and its refusals are stated in the header. The table is
// frame-major and channel-minor. A frame-level figure writes channel zero's column, which is then
// copied to every channel; under the noise source, and under the width source with per-channel
// widths, each of the first `kMaxStackWeightChannels` channels that every frame measures is then
// rewritten from its own figures. Every column is divided by its mean, summed in frame order, and
// the whole derivation runs on the calling thread, so the table does not depend on the worker
// count. Every record is checked before the first entry is written.

#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/stacking/StackHistory.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace an
{
namespace
{

using stacking::failure;
using stacking::refuse;

constexpr const char* kOperation = "an::computeStackFrameWeights";
constexpr const char* kStage = "deriving frame weights";

/// The width penalty: twice the width per unit of relative star deficit.
constexpr double kWidthPenaltyScale = 2.0;

/// The floor on a penalized width: a tenth of the measured one.
constexpr double kWidthPenaltyFloor = 0.1;

/// What `std::snprintf` wants a sixty-four-bit count as.
using Count = unsigned long long;

/// Which figure a source reads on every frame.
enum class SourceFigure : std::uint8_t
{
    None,
    BackgroundNoise,
    StarCount,
    FullWidth,
    IntegratedFrameCount,
    ExposureSeconds,
    SignalToNoise
};

/// The spelling of a figure, as a refusal names it.
///
/// @param figure the figure.
/// @return its member name.
[[nodiscard]] const char* figureName(SourceFigure figure) noexcept
{
    switch (figure)
    {
    case SourceFigure::BackgroundNoise:
        return "backgroundNoise";
    case SourceFigure::StarCount:
        return "starCount";
    case SourceFigure::FullWidth:
        return "fullWidth";
    case SourceFigure::IntegratedFrameCount:
        return "integratedFrameCount";
    case SourceFigure::ExposureSeconds:
        return "exposureSeconds";
    case SourceFigure::SignalToNoise:
        return "signalToNoise";
    case SourceFigure::None:
        break;
    }
    return "";
}

/// Whether a frame carries a figure.
///
/// @param measurement the record.
/// @param figure the figure.
/// @return true when the figure is present.
[[nodiscard]] bool carries(const StackFrameMeasurement& measurement, SourceFigure figure) noexcept
{
    switch (figure)
    {
    case SourceFigure::BackgroundNoise:
        return measurement.backgroundNoise.has_value();
    case SourceFigure::StarCount:
        return measurement.starCount.has_value();
    case SourceFigure::FullWidth:
        return measurement.fullWidth.has_value();
    case SourceFigure::IntegratedFrameCount:
        return measurement.integratedFrameCount.has_value();
    case SourceFigure::ExposureSeconds:
        return measurement.exposureSeconds.has_value();
    case SourceFigure::SignalToNoise:
        return measurement.signalToNoise.has_value();
    case SourceFigure::None:
        break;
    }
    return false;
}

/// Which frame-level figure a source reads.
///
/// @param source the source.
/// @return the figure, or none for the sources with their own records.
[[nodiscard]] SourceFigure figureOf(StackWeighting source) noexcept
{
    switch (source)
    {
    case StackWeighting::Noise:
        return SourceFigure::BackgroundNoise;
    case StackWeighting::StarCount:
        return SourceFigure::StarCount;
    case StackWeighting::Fwhm:
        return SourceFigure::FullWidth;
    case StackWeighting::ImageCount:
        return SourceFigure::IntegratedFrameCount;
    case StackWeighting::Exposure:
        return SourceFigure::ExposureSeconds;
    case StackWeighting::SignalToNoise:
        return SourceFigure::SignalToNoise;
    case StackWeighting::Uniform:
    case StackWeighting::Explicit:
    case StackWeighting::Photometric:
        break;
    }
    return SourceFigure::None;
}

/// A refusal about one frame's record.
///
/// @param code why.
/// @param frame which frame.
/// @param frames how many there are.
/// @param message what a person reads.
/// @return the failure, naming the frame as the requested count.
[[nodiscard]] Status frameFailure(ErrorCode code, std::uint32_t frame, std::uint32_t frames, std::string message)
{
    return failure(code, kOperation, std::move(message),
                   {StatusContextField::ofText(ContextKey::Parameter, "measurements"),
                    StatusContextField::ofCount(ContextKey::Requested, frame),
                    StatusContextField::ofCount(ContextKey::Limit, frames)});
}

/// A refusal of a malformed figure.
///
/// @param frame which frame.
/// @param frames how many there are.
/// @param field the figure's spelling.
/// @param requirement what it had to be.
/// @return the failure.
[[nodiscard]] Status malformed(std::uint32_t frame, std::uint32_t frames, const char* field, const char* requirement)
{
    std::string message = "measurements[";
    message += std::to_string(frame);
    message += "].";
    message += field;
    message += " must be ";
    message += requirement;
    return frameFailure(ErrorCode::InvalidArgument, frame, frames, std::move(message));
}

/// Whether a present figure is finite and strictly positive.
///
/// @param value the figure.
/// @return true when absent or well formed.
[[nodiscard]] bool positiveOrAbsent(const std::optional<double>& value) noexcept
{
    return !value.has_value() || (std::isfinite(*value) && *value > 0.0);
}

/// Checks one record whole.
///
/// @param measurement the record.
/// @param frame which frame.
/// @param frames how many there are.
/// @return success, or the first malformed figure.
[[nodiscard]] Status checkMeasurement(const StackFrameMeasurement& measurement, std::uint32_t frame,
                                      std::uint32_t frames)
{
    if (measurement.medianRoundness.has_value() &&
        !(std::isfinite(*measurement.medianRoundness) && *measurement.medianRoundness > 0.0 &&
          *measurement.medianRoundness <= 1.0))
    {
        return malformed(frame, frames, "medianRoundness", "finite, positive and at most one");
    }
    if (!positiveOrAbsent(measurement.fullWidth))
    {
        return malformed(frame, frames, "fullWidth", "finite and positive");
    }
    if (!positiveOrAbsent(measurement.backgroundNoise))
    {
        return malformed(frame, frames, "backgroundNoise", "finite and positive");
    }
    if (!positiveOrAbsent(measurement.dispersion))
    {
        return malformed(frame, frames, "dispersion", "finite and positive");
    }
    if (measurement.integratedFrameCount.has_value() && *measurement.integratedFrameCount == 0u)
    {
        return malformed(frame, frames, "integratedFrameCount", "at least one");
    }
    if (!positiveOrAbsent(measurement.exposureSeconds))
    {
        return malformed(frame, frames, "exposureSeconds", "finite and positive");
    }
    if (!positiveOrAbsent(measurement.signalToNoise))
    {
        return malformed(frame, frames, "signalToNoise", "finite and positive");
    }
    for (const StackChannelMeasurement& channel : measurement.channels)
    {
        if (!positiveOrAbsent(channel.backgroundNoise))
        {
            return malformed(frame, frames, "channels[].backgroundNoise", "finite and positive");
        }
        if (!positiveOrAbsent(channel.dispersion))
        {
            return malformed(frame, frames, "channels[].dispersion", "finite and positive");
        }
        if (!positiveOrAbsent(channel.fullWidth))
        {
            return malformed(frame, frames, "channels[].fullWidth", "finite and positive");
        }
    }
    return Status::success();
}

/// How a figure is carried across the frames.
enum class Coverage : std::uint8_t
{
    None,
    Every
};

/// Whether every frame, no frame, or only some frames carry a figure.
///
/// @param measurements the records.
/// @param present whether one record carries the figure.
/// @param firstMissing receives the first frame without it when only some carry it.
/// @return the coverage, or nothing for a partial set.
template <typename PresentT>
[[nodiscard]] std::optional<Coverage> coverageOf(std::span<const StackFrameMeasurement> measurements,
                                                 PresentT&& present, std::uint32_t& firstMissing) noexcept
{
    std::uint32_t carried = 0u;
    bool missingSeen = false;
    for (std::uint32_t frame = 0u; frame < measurements.size(); ++frame)
    {
        if (present(measurements[frame]))
        {
            ++carried;
        }
        else if (!missingSeen)
        {
            missingSeen = true;
            firstMissing = frame;
        }
    }
    if (carried == 0u)
    {
        return Coverage::None;
    }
    if (carried == measurements.size())
    {
        return Coverage::Every;
    }
    return std::nullopt;
}

/// Decides, for each channel a record describes on its own, whether every frame carries its figure.
///
/// @param measurements the records.
/// @param ownChannels how many leading channels to decide.
/// @param present whether one record carries the figure for one channel.
/// @param figure the figure's spelling, for a refusal.
/// @param usesOwn receives one decision per channel.
/// @return success, or the refusal of a channel some frames carry and some do not.
template <typename PresentT>
[[nodiscard]] Status ownChannelCoverage(std::span<const StackFrameMeasurement> measurements, std::uint32_t ownChannels,
                                        PresentT&& present, const char* figure, std::span<bool> usesOwn)
{
    for (std::uint32_t channel = 0u; channel < ownChannels; ++channel)
    {
        std::uint32_t firstMissing = 0u;
        const std::optional<Coverage> coverage = coverageOf(
            measurements,
            [&present, channel](const StackFrameMeasurement& measurement) { return present(measurement, channel); },
            firstMissing);
        if (!coverage.has_value())
        {
            std::string message = "measurements carry channel ";
            message += std::to_string(channel);
            message += "'s ";
            message += figure;
            message += " on some frames and not on frame ";
            message += std::to_string(firstMissing);
            return frameFailure(ErrorCode::IncompatibleArguments, firstMissing,
                                static_cast<std::uint32_t>(measurements.size()), std::move(message));
        }
        usesOwn[channel] = *coverage == Coverage::Every;
    }
    return Status::success();
}

/// Writes a channel's raw weights over its mean.
///
/// @param frameWeights the table.
/// @param frames how many frames.
/// @param channels how many channels.
/// @param channel which channel's column to normalize.
void normalizeChannel(std::span<double> frameWeights, std::uint32_t frames, std::uint32_t channels,
                      std::uint32_t channel) noexcept
{
    double sum = 0.0;
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        sum += frameWeights[static_cast<std::size_t>(frame) * channels + channel];
    }
    const double mean = sum / static_cast<double>(frames);
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        frameWeights[static_cast<std::size_t>(frame) * channels + channel] /= mean;
    }
}

/// Copies one channel's column of the table into another.
///
/// @param frameWeights the table.
/// @param frames how many frames.
/// @param channels how many channels.
/// @param from the source column.
/// @param to the destination column.
void copyChannel(std::span<double> frameWeights, std::uint32_t frames, std::uint32_t channels, std::uint32_t from,
                 std::uint32_t to) noexcept
{
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        frameWeights[static_cast<std::size_t>(frame) * channels + to] =
            frameWeights[static_cast<std::size_t>(frame) * channels + from];
    }
}

/// Fills every entry of the table with one.
///
/// @param frameWeights the table.
void fillOnes(std::span<double> frameWeights) noexcept
{
    for (double& weight : frameWeights)
    {
        weight = 1.0;
    }
}

/// One channel's noise weights from a noise and an optional dispersion per frame.
///
/// @param measurements the records.
/// @param reference the reference frame.
/// @param noiseOf the frame's noise.
/// @param dispersionOf the frame's dispersion, or nothing.
/// @param frameWeights the table.
/// @param channels how many channels the table has.
/// @param channel which column to write.
template <typename NoiseT, typename DispersionT>
void noiseWeights(std::span<const StackFrameMeasurement> measurements, std::uint32_t reference, NoiseT&& noiseOf,
                  DispersionT&& dispersionOf, std::span<double> frameWeights, std::uint32_t channels,
                  std::uint32_t channel) noexcept
{
    const std::optional<double> referenceDispersion = dispersionOf(measurements[reference]);
    for (std::uint32_t frame = 0u; frame < measurements.size(); ++frame)
    {
        const double noise = noiseOf(measurements[frame]);
        const std::optional<double> dispersion = dispersionOf(measurements[frame]);
        const double scale =
            referenceDispersion.has_value() && dispersion.has_value() ? *referenceDispersion / *dispersion : 1.0;
        frameWeights[static_cast<std::size_t>(frame) * channels + channel] = 1.0 / (scale * scale * noise * noise);
    }
}

/// Writes one channel's width weights into its column.
///
/// Each width is first penalized for a star deficit against the reference frame when both have
/// stars, and floored at a tenth of itself; the weight is then the inverse square rescaled so the
/// widest frame is zero and the narrowest one, raised to the floor. When every effective width is
/// the same the column is left holding the widths, for the caller to replace.
///
/// @param measurements the records.
/// @param reference the reference frame.
/// @param widthOf the frame's width.
/// @param floor the model's floor.
/// @param frameWeights the table.
/// @param channels how many channels the table has.
/// @param channel which column to write.
/// @param floored receives how many entries were raised to the floor.
/// @return true when the widths ranked the frames.
template <typename WidthT>
[[nodiscard]] bool widthWeights(std::span<const StackFrameMeasurement> measurements, std::uint32_t reference,
                                WidthT&& widthOf, double floor, std::span<double> frameWeights, std::uint32_t channels,
                                std::uint32_t channel, std::uint32_t& floored) noexcept
{
    const std::uint32_t referenceStars = measurements[reference].starCount.value_or(0u);
    double lowest = (std::numeric_limits<double>::max)();
    double highest = 0.0;
    for (std::uint32_t frame = 0u; frame < measurements.size(); ++frame)
    {
        double width = widthOf(measurements[frame]);
        const std::uint32_t stars = measurements[frame].starCount.value_or(0u);
        if (referenceStars > 0u && stars > 0u)
        {
            const double penalty = kWidthPenaltyScale * width *
                                   (static_cast<double>(referenceStars) - static_cast<double>(stars)) /
                                   static_cast<double>(stars);
            width = (std::max)(width + penalty, widthOf(measurements[frame]) * kWidthPenaltyFloor);
        }
        frameWeights[static_cast<std::size_t>(frame) * channels + channel] = width;
        lowest = (std::min)(lowest, width);
        highest = (std::max)(highest, width);
    }
    if (!(highest > 0.0) || lowest == highest)
    {
        return false;
    }
    const double inverseDenominator = 1.0 / ((1.0 / (lowest * lowest)) - (1.0 / (highest * highest)));
    const double inverseHighest = 1.0 / (highest * highest);
    for (std::uint32_t frame = 0u; frame < measurements.size(); ++frame)
    {
        double& entry = frameWeights[static_cast<std::size_t>(frame) * channels + channel];
        const double raw = (std::max)(0.0, (1.0 / (entry * entry) - inverseHighest) * inverseDenominator);
        if (raw < floor)
        {
            ++floored;
            entry = floor;
        }
        else
        {
            entry = raw;
        }
    }
    return true;
}

/// Says what was written, for a person.
///
/// @param execution the operation.
/// @param result the completed result.
void reportWeights(const ExecutionContext& execution, const StackFrameWeightingResult& result) noexcept
{
    char message[256];
    std::snprintf(message, sizeof(message),
                  "Frame weights from %s over %llu frames and %llu channels: %s, range [%.4f, %.4f], efficiency %.4f.",
                  stackWeightingName(result.source), static_cast<Count>(result.frameCount),
                  static_cast<Count>(result.channelCount), stackFrameWeightingDispositionName(result.disposition),
                  result.summary.minimum, result.summary.maximum, result.summary.efficiency);
    execution.reportDiagnostic(DiagnosticSeverity::Info, ErrorDomain::Algorithms, kOperation, message);
}

/// Completes a result over the written table and reports it.
///
/// @param execution the operation.
/// @param frameWeights the table.
/// @param result the result to complete.
/// @return the result.
[[nodiscard]] StackFrameWeightingResult finish(const ExecutionContext& execution, std::span<const double> frameWeights,
                                               StackFrameWeightingResult result) noexcept
{
    result.summary = summarizeStackFrameWeights(frameWeights);
    result.effectiveFrameCount = result.summary.efficiency * static_cast<double>(result.frameCount);
    if (result.disposition == StackFrameWeightingDisposition::UniformNoMeasurements)
    {
        execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Algorithms, kOperation,
                                   "No frame carried the figure the weighting source reads; every frame received "
                                   "weight one.");
    }
    reportWeights(execution, result);
    execution.reportProgress(result.frameCount, result.frameCount, kStage);
    return result;
}

} // namespace

const char* stackFrameWeightingDispositionName(StackFrameWeightingDisposition disposition) noexcept
{
    switch (disposition)
    {
    case StackFrameWeightingDisposition::Uniform:
        return "Uniform";
    case StackFrameWeightingDisposition::Computed:
        return "Computed";
    case StackFrameWeightingDisposition::UniformNoMeasurements:
        return "UniformNoMeasurements";
    case StackFrameWeightingDisposition::UniformEqualMeasurements:
        return "UniformEqualMeasurements";
    case StackFrameWeightingDisposition::UniformDegenerate:
        return "UniformDegenerate";
    case StackFrameWeightingDisposition::Supplied:
        return "Supplied";
    }
    return "UnknownStackFrameWeightingDisposition";
}

Result<StackFrameWeightingResult> computeStackFrameWeights(const ExecutionContext& execution, const StackModel& model,
                                                           std::span<const StackFrameMeasurement> measurements,
                                                           std::span<double> frameWeights)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kOperation,
                       "deriving frame weights requires an execution with an engine-context lease");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const StackRequest& request = model.request();
    const StackWeighting source = request.weighting.method;
    if (source == StackWeighting::Explicit)
    {
        return refuse(ErrorCode::IncompatibleArguments, kOperation, "model",
                      "model weighs its frames by an Explicit table, which checkStackFrameWeights checks and "
                      "nothing derives");
    }
    const std::uint32_t frames = request.input.frameCount;
    const std::uint32_t channels = request.input.descriptor.extent().channelCount();
    if (measurements.size() != frames)
    {
        return failure(ErrorCode::IncompatibleArguments, kOperation,
                       "measurements does not hold one record per frame of the stack",
                       {StatusContextField::ofText(ContextKey::Parameter, "measurements"),
                        StatusContextField::ofCount(ContextKey::Requested, measurements.size()),
                        StatusContextField::ofCount(ContextKey::Limit, frames)});
    }
    const std::uint64_t entries = static_cast<std::uint64_t>(frames) * channels;
    if (frameWeights.size() != entries)
    {
        return failure(ErrorCode::IncompatibleArguments, kOperation,
                       "frameWeights does not hold one weight per frame and channel of the stack",
                       {StatusContextField::ofText(ContextKey::Parameter, "frameWeights"),
                        StatusContextField::ofCount(ContextKey::Requested, frameWeights.size()),
                        StatusContextField::ofCount(ContextKey::Limit, entries)});
    }
    for (std::uint32_t frame = 0u; frame < frames; ++frame)
    {
        status = checkMeasurement(measurements[frame], frame, frames);
        if (status.failed())
        {
            return status;
        }
    }

    StackFrameWeightingResult result;
    result.source = source;
    result.frameCount = frames;
    result.channelCount = channels;
    const std::uint32_t reference = request.normalization.referenceFrame;
    const std::uint32_t ownChannels = (std::min)(channels, kMaxStackWeightChannels);

    if (source == StackWeighting::Uniform)
    {
        fillOnes(frameWeights);
        result.disposition = StackFrameWeightingDisposition::Uniform;
        return finish(execution, frameWeights, result);
    }

    if (source == StackWeighting::Photometric)
    {
        // The model's own records are contiguous copies of the frames' photometric figures, and
        // its outputs one record per frame, both held for the call.
        const std::uint64_t recordBytes = static_cast<std::uint64_t>(frames) * sizeof(PhotometricFrameMeasurement);
        const std::uint64_t weightBytes = static_cast<std::uint64_t>(frames) * sizeof(PhotometricStackWeight);
        static_assert(alignof(PhotometricFrameMeasurement) >= alignof(PhotometricStackWeight));
        const std::uint64_t alignedRecordBytes = (recordBytes + alignof(PhotometricFrameMeasurement) - 1u) /
                                                 alignof(PhotometricFrameMeasurement) *
                                                 alignof(PhotometricFrameMeasurement);
        Result<plane_detail::ScopedContextMemory> workspace = plane_detail::ScopedContextMemory::take(
            execution, alignedRecordBytes + weightBytes, alignof(PhotometricFrameMeasurement), kOperation);
        if (!workspace.hasValue())
        {
            return workspace.status();
        }
        auto* const records = static_cast<PhotometricFrameMeasurement*>(workspace->data());
        auto* const weights =
            reinterpret_cast<PhotometricStackWeight*>(static_cast<std::byte*>(workspace->data()) + alignedRecordBytes);
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            new (records + frame) PhotometricFrameMeasurement(measurements[frame].photometric);
            new (weights + frame) PhotometricStackWeight();
        }
        Result<PhotometricWeightingResult> photometric = computePhotometricStackWeights(
            execution, std::span<const PhotometricFrameMeasurement>(records, frames), request.weighting.photometric,
            std::span<PhotometricStackWeight>(weights, frames));
        if (!photometric.hasValue())
        {
            return photometric.status();
        }
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                frameWeights[static_cast<std::size_t>(frame) * channels + channel] = weights[frame].value;
            }
        }
        switch (photometric->disposition)
        {
        case PhotometricWeightingDisposition::Computed:
        case PhotometricWeightingDisposition::SingleFrame:
            result.disposition = StackFrameWeightingDisposition::Computed;
            break;
        case PhotometricWeightingDisposition::UniformNoZeroPoints:
            result.disposition = StackFrameWeightingDisposition::UniformNoMeasurements;
            break;
        case PhotometricWeightingDisposition::UniformDegenerate:
            result.disposition = StackFrameWeightingDisposition::UniformDegenerate;
            break;
        }
        result.measuredFrameCount = photometric->measuredFrameCount;
        result.flooredCount = photometric->minimumAppliedCount * channels;
        result.workingBytes = workspace->bytes() + photometric->workingBytes;
        result.photometric = *photometric;
        return finish(execution, frameWeights, result);
    }

    // The frame-level figure the source reads: every frame carries it, none does, or the set is
    // refused naming the first frame without it.
    const SourceFigure figure = figureOf(source);
    std::uint32_t firstMissing = 0u;
    const std::optional<Coverage> coverage = coverageOf(
        measurements, [figure](const StackFrameMeasurement& measurement) { return carries(measurement, figure); },
        firstMissing);
    if (source != StackWeighting::ImageCount)
    {
        if (!coverage.has_value())
        {
            std::string message = "measurements carry ";
            message += figureName(figure);
            message += " on some frames and not on frame ";
            message += std::to_string(firstMissing);
            return frameFailure(ErrorCode::IncompatibleArguments, firstMissing, frames, std::move(message));
        }
        if (*coverage == Coverage::None)
        {
            fillOnes(frameWeights);
            result.disposition = StackFrameWeightingDisposition::UniformNoMeasurements;
            return finish(execution, frameWeights, result);
        }
        result.measuredFrameCount = frames;
    }

    switch (source)
    {
    case StackWeighting::Noise: {
        bool usesOwn[kMaxStackWeightChannels] = {false, false, false, false};
        status = ownChannelCoverage(
            measurements, ownChannels,
            [](const StackFrameMeasurement& measurement, std::uint32_t channel) {
                return measurement.channels[channel].backgroundNoise.has_value();
            },
            "backgroundNoise", usesOwn);
        if (status.failed())
        {
            return status;
        }
        noiseWeights(
            measurements, reference,
            [](const StackFrameMeasurement& measurement) { return *measurement.backgroundNoise; },
            [](const StackFrameMeasurement& measurement) { return measurement.dispersion; }, frameWeights, channels,
            0u);
        normalizeChannel(frameWeights, frames, channels, 0u);
        for (std::uint32_t channel = 1u; channel < channels; ++channel)
        {
            copyChannel(frameWeights, frames, channels, 0u, channel);
        }
        for (std::uint32_t channel = 0u; channel < ownChannels; ++channel)
        {
            if (!usesOwn[channel])
            {
                continue;
            }
            noiseWeights(
                measurements, reference,
                [channel](const StackFrameMeasurement& measurement) {
                    return *measurement.channels[channel].backgroundNoise;
                },
                [channel](const StackFrameMeasurement& measurement) {
                    return measurement.channels[channel].dispersion;
                },
                frameWeights, channels, channel);
            normalizeChannel(frameWeights, frames, channels, channel);
            result.perChannel = true;
        }
        result.disposition = StackFrameWeightingDisposition::Computed;
        break;
    }
    case StackWeighting::StarCount: {
        const StarCountWeightingParameters& parameters = request.weighting.starCount;
        const bool roundness = parameters.scaling == StarCountScaling::RoundnessWeighted;
        double lowest = (std::numeric_limits<double>::max)();
        double highest = 0.0;
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            const StackFrameMeasurement& measurement = measurements[frame];
            double effective = static_cast<double>(*measurement.starCount);
            if (roundness && measurement.medianRoundness.has_value() && *measurement.starCount > 0u)
            {
                effective *= *measurement.medianRoundness;
            }
            frameWeights[static_cast<std::size_t>(frame) * channels] = effective;
            lowest = (std::min)(lowest, effective);
            highest = (std::max)(highest, effective);
        }
        if (highest == lowest || highest == 0.0)
        {
            fillOnes(frameWeights);
            result.disposition = StackFrameWeightingDisposition::UniformEqualMeasurements;
            return finish(execution, frameWeights, result);
        }
        const double denominator = highest - lowest;
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            double& entry = frameWeights[static_cast<std::size_t>(frame) * channels];
            const double normal = (entry - lowest) / denominator;
            double raw = 0.0;
            switch (parameters.scaling)
            {
            case StarCountScaling::Linear:
                raw = normal;
                break;
            case StarCountScaling::Quadratic:
            case StarCountScaling::RoundnessWeighted:
                raw = normal * normal;
                break;
            case StarCountScaling::Exponential:
                raw = std::exp(-parameters.exponentialAlpha * (1.0 - normal));
                break;
            }
            if (raw < request.weighting.minimumWeight)
            {
                raw = request.weighting.minimumWeight;
                ++result.flooredCount;
            }
            entry = raw;
        }
        normalizeChannel(frameWeights, frames, channels, 0u);
        for (std::uint32_t channel = 1u; channel < channels; ++channel)
        {
            copyChannel(frameWeights, frames, channels, 0u, channel);
        }
        result.flooredCount *= channels;
        result.disposition = StackFrameWeightingDisposition::Computed;
        break;
    }
    case StackWeighting::Fwhm: {
        const double floor = request.weighting.minimumWeight;
        bool usesOwn[kMaxStackWeightChannels] = {false, false, false, false};
        if (request.weighting.perChannelFwhm)
        {
            status = ownChannelCoverage(
                measurements, ownChannels,
                [](const StackFrameMeasurement& measurement, std::uint32_t channel) {
                    return measurement.channels[channel].fullWidth.has_value();
                },
                "fullWidth", usesOwn);
            if (status.failed())
            {
                return status;
            }
        }
        std::uint32_t floored = 0u;
        const bool ranked = widthWeights(
            measurements, reference, [](const StackFrameMeasurement& measurement) { return *measurement.fullWidth; },
            floor, frameWeights, channels, 0u, floored);
        if (ranked)
        {
            normalizeChannel(frameWeights, frames, channels, 0u);
        }
        else
        {
            for (std::uint32_t frame = 0u; frame < frames; ++frame)
            {
                frameWeights[static_cast<std::size_t>(frame) * channels] = 1.0;
            }
        }
        result.flooredCount = floored * channels;
        for (std::uint32_t channel = 1u; channel < channels; ++channel)
        {
            copyChannel(frameWeights, frames, channels, 0u, channel);
        }
        bool anyRanked = ranked;
        if (request.weighting.perChannelFwhm)
        {
            for (std::uint32_t channel = 0u; channel < ownChannels; ++channel)
            {
                if (!usesOwn[channel])
                {
                    continue;
                }
                std::uint32_t channelFloored = 0u;
                const bool channelRanked = widthWeights(
                    measurements, reference,
                    [channel](const StackFrameMeasurement& measurement) {
                        return *measurement.channels[channel].fullWidth;
                    },
                    floor, frameWeights, channels, channel, channelFloored);
                if (!channelRanked)
                {
                    copyChannel(frameWeights, frames, channels, 0u, channel);
                    continue;
                }
                normalizeChannel(frameWeights, frames, channels, channel);
                result.flooredCount += channelFloored;
                result.flooredCount -= floored;
                result.perChannel = true;
                anyRanked = true;
            }
        }
        result.disposition = anyRanked ? StackFrameWeightingDisposition::Computed
                                       : StackFrameWeightingDisposition::UniformEqualMeasurements;
        break;
    }
    case StackWeighting::ImageCount: {
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            const double count = static_cast<double>(measurements[frame].integratedFrameCount.value_or(1u));
            frameWeights[static_cast<std::size_t>(frame) * channels] = count;
            result.measuredFrameCount += measurements[frame].integratedFrameCount.has_value() ? 1u : 0u;
        }
        normalizeChannel(frameWeights, frames, channels, 0u);
        for (std::uint32_t channel = 1u; channel < channels; ++channel)
        {
            copyChannel(frameWeights, frames, channels, 0u, channel);
        }
        result.disposition = StackFrameWeightingDisposition::Computed;
        break;
    }
    case StackWeighting::Exposure:
    case StackWeighting::SignalToNoise: {
        // The product's standalone stacking: the figure itself, over the mean.
        const bool exposure = source == StackWeighting::Exposure;
        for (std::uint32_t frame = 0u; frame < frames; ++frame)
        {
            frameWeights[static_cast<std::size_t>(frame) * channels] =
                exposure ? *measurements[frame].exposureSeconds : *measurements[frame].signalToNoise;
        }
        normalizeChannel(frameWeights, frames, channels, 0u);
        for (std::uint32_t channel = 1u; channel < channels; ++channel)
        {
            copyChannel(frameWeights, frames, channels, 0u, channel);
        }
        result.disposition = StackFrameWeightingDisposition::Computed;
        break;
    }
    case StackWeighting::Uniform:
    case StackWeighting::Explicit:
    case StackWeighting::Photometric:
        return failure(ErrorCode::Internal, kOperation, "a weighting source was not dispatched");
    }
    return finish(execution, frameWeights, result);
}

std::uint64_t stackFrameWeightingWorkingBytes(const StackModel& model) noexcept
{
    if (model.request().weighting.method != StackWeighting::Photometric)
    {
        return 0u;
    }
    const std::uint32_t frames = model.request().input.frameCount;
    return static_cast<std::uint64_t>(frames) * (sizeof(PhotometricFrameMeasurement) + sizeof(PhotometricStackWeight)) +
           photometricWeightingWorkingBytes(frames, true);
}

} // namespace an

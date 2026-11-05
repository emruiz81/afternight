// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Turning fitted channel gains into an image, and saying what that did.
///
/// **The apply is one operation, shared by every colour calibration.** Photometric and
/// spectrophotometric calibration both end with three gains, three offsets and a clipping decision,
/// and this is the one place that turns them into samples. It computes in double and narrows once,
/// walks the frame on the operation's threads, and copies rather than transforms a sample the frame's
/// validity rule says means nothing.
///
/// **The background estimate and the offsets it becomes are separate questions.**
/// `estimateColorBackground` measures each channel's sky - a median, a robust scale, and a second
/// median over the samples inside a stated asymmetric window - and
/// `colorCalibrationOffsets` states the two ways an offset follows from a level and a gain. A caller
/// that already knows its sky levels skips the estimate; one that wants no neutralization passes
/// zero offsets.
///
/// **What the result means is declared, not narrated.** `kColorCalibrationApplyEffect` says the
/// operation writes samples and restates their meaning, and `colorCalibratedSemantics` is the one
/// place the `colorCalibrated` flag is set, so a consumer asks the image rather than parsing a
/// provenance string.
///
/// Two answers are deliberate. The sky window is not clamped to the unit interval, because a linear
/// frame whose sky sits near zero has negative samples that belong in the estimate and a physically
/// scaled frame has no unit interval to clamp to. An integer frame is refused rather than applied,
/// because it carries a quantization of its own and the engine converts representations through its
/// explicit conversion surface, never inside a calibration.

#pragma once

#include <AfterNightEngine/Algorithms/Color/ColorChannels.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <array>
#include <cstdint>
#include <optional>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// One frame, checked once, that an operation reads. Defined in `Algorithms/Stars/StarDetection.h`.
///
/// Named rather than included so that a caller planning offsets or a transition does not inherit the
/// star detector's include closure; the two operations that read a frame take it by reference.
class StarFrameView;

/// How many robust scales below the median the background window reaches.
///
/// The window is asymmetric: wider below, because a sky sample far below the median is a cold pixel
/// rather than a star, and narrower above, because a sample above it usually is one.
inline constexpr double kColorBackgroundLowerSigmas = 2.8;

/// How many robust scales above the median the background window reaches.
inline constexpr double kColorBackgroundUpperSigmas = 2.0;

/// The stable name of the photometric colour-calibration recipe this build implements.
///
/// Recorded by `photometricCalibrationTransition` alongside the fitted gains and offsets. The
/// raw-buffer apply does not own metadata; the caller applies that transition to its image.
inline constexpr const char* kPhotometricColorCalibrationAlgorithmVersion = "pcc-gaia-bp-rp-v1";

/// What the offsets do to the background after the gains have moved it.
enum class ColorCalibrationBackgroundMode : std::uint8_t
{
    /// Every channel's background is brought to one shared level, the mean of the three measured
    /// levels. The sky stays where it was on average and becomes neutral.
    SharedPedestal = 0u,

    /// Every channel's background is brought to zero.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SkyToZero = 1u
};

/// Whether a value names a background mode this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param mode the value to inspect.
/// @return true for one of the two declared values.
[[nodiscard]] constexpr bool isColorCalibrationBackgroundMode(ColorCalibrationBackgroundMode mode) noexcept
{
    return mode == ColorCalibrationBackgroundMode::SharedPedestal || mode == ColorCalibrationBackgroundMode::SkyToZero;
}

/// The mode's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param mode the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownColorCalibrationBackgroundMode"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
colorCalibrationBackgroundModeName(ColorCalibrationBackgroundMode mode) noexcept;

/// What happens to a calibrated sample that leaves the frame's representable interval.
enum class ColorCalibrationClipping : std::uint8_t
{
    /// The sample is held at the interval's nearest end, which holds a negative result at zero whatever
    /// the frame's domain.
    ClampToPixelRange = 0u,

    /// The sample is written as computed, negatives and values above full scale included. Only
    /// legitimate on a floating representation, which is the only kind this operation writes.
    PreserveSignedFloat = 1u,

    /// The whole operation is refused before any sample is written when one would leave the
    /// interval, so a calibration that clips is a calibration the caller hears about.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RejectOnClip = 2u
};

/// Whether a value names a clipping policy this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param clipping the value to inspect.
/// @return true for one of the three declared values.
[[nodiscard]] constexpr bool isColorCalibrationClipping(ColorCalibrationClipping clipping) noexcept
{
    return static_cast<std::uint8_t>(clipping) <= static_cast<std::uint8_t>(ColorCalibrationClipping::RejectOnClip);
}

/// The policy's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param clipping the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownColorCalibrationClipping"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* colorCalibrationClippingName(ColorCalibrationClipping clipping) noexcept;

/// The offsets that follow from measured background levels under a stated mode.
///
/// Under `SharedPedestal` each offset is `pedestal - level * gain`; under `SkyToZero` it is
/// `-level * gain`. The pedestal is the caller's, so a caller that wants the mean of the three
/// levels passes it and one that wants another level passes that.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param gains the fitted per-channel multipliers.
/// @param backgroundLevels the measured per-channel sky levels, in the frame's numeric domain.
/// @param mode where the background goes.
/// @param sharedPedestal the level every channel's background is brought to under `SharedPedestal`;
///        ignored under `SkyToZero`.
/// @return the three offsets. An out-of-range mode yields the `SkyToZero` answer.
[[nodiscard]] constexpr ColorChannelOffsets colorCalibrationOffsets(const ColorChannelGains& gains,
                                                                    const ColorChannelOffsets& backgroundLevels,
                                                                    ColorCalibrationBackgroundMode mode,
                                                                    double sharedPedestal) noexcept
{
    ColorChannelOffsets offsets{};
    for (std::size_t channel = 0u; channel < offsets.size(); ++channel)
    {
        const double moved = backgroundLevels[channel] * gains[channel];
        offsets[channel] = mode == ColorCalibrationBackgroundMode::SharedPedestal ? sharedPedestal - moved : -moved;
    }
    return offsets;
}

/// The arithmetic mean of three background levels: the shared pedestal.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param backgroundLevels the three measured levels.
/// @return their mean.
[[nodiscard]] constexpr double meanBackgroundPedestal(const ColorChannelOffsets& backgroundLevels) noexcept
{
    return (backgroundLevels[0] + backgroundLevels[1] + backgroundLevels[2]) / 3.0;
}

/// What each channel's sky measured as.
struct ColorBackgroundEstimate
{
    /// The sky level per channel: the median of the samples inside the clipping window.
    ColorChannelOffsets levels{};

    /// The unclipped median per channel, which centred the window.
    ColorChannelOffsets medians{};

    /// The robust scale per channel - the median absolute deviation scaled to a sigma - which sized
    /// the window.
    ColorChannelOffsets robustScales{};

    /// How many samples each channel's window retained.
    std::array<std::uint64_t, 3u> retainedSamples{};
};

/// Measures each channel's sky level the way the colour calibrations neutralize it.
///
/// Per channel: the median and median absolute deviation under the frame's own validity rule; then
/// the median of the samples within `kColorBackgroundLowerSigmas` robust scales below that median
/// and `kColorBackgroundUpperSigmas` above it. The window is decided once, not iterated, and it is
/// not clamped to the unit interval: a linear frame whose sky sits near zero keeps its negative
/// samples in the estimate rather than having the window truncated at zero.
///
/// Every rank is exact, so the result is independent of the admitted thread count. Thread-safe with
/// respect to `execution` and `frame`, which are only read.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked three-channel frame.
/// @param region the rectangle to measure, or absent for the whole frame.
/// @return the three levels and what centred and sized them; `ErrorCode::NotInitialized` when
///         `execution` holds nothing; `ErrorCode::InvalidArgument` for an invalid frame;
///         `ErrorCode::Unsupported` for a frame that is not three-channel RGB;
///         `ErrorCode::OutOfRange` for an empty region or one outside the frame;
///         `ErrorCode::EmptySelection` when a channel's rule admits no sample; or a cancellation,
///         data or memory-budget failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorBackgroundEstimate>
estimateColorBackground(const ExecutionContext& execution, const StarFrameView& frame,
                        const std::optional<StatisticsRegion>& region = std::nullopt);

/// What one apply writes and how it treats a sample that leaves the interval.
struct ColorCalibrationApplyRequest
{
    /// The per-channel multipliers. Finite and positive.
    ColorChannelGains gains{1.0, 1.0, 1.0};

    /// The per-channel addends, in the frame's numeric domain. Finite.
    ColorChannelOffsets offsets{0.0, 0.0, 0.0};

    /// What happens to a sample outside the representable interval.
    ColorCalibrationClipping clipping = ColorCalibrationClipping::ClampToPixelRange;
};

/// What one apply did.
struct ColorCalibrationApplyResult
{
    /// Samples written, over all three channels.
    std::uint64_t samplesWritten = 0u;

    /// Samples the frame's validity rule rejected, which were copied unchanged.
    std::uint64_t samplesCopiedUnchanged = 0u;

    /// Samples whose computed value left the interval. Under `ClampToPixelRange` these were held at
    /// an end; under `PreserveSignedFloat` they were written as computed; under `RejectOnClip` the
    /// call failed instead and this is zero.
    std::uint64_t samplesOutsideInterval = 0u;

    /// The interval's inclusive lower endpoint, zero for every domain this operation writes.
    double intervalLow = 0.0;

    /// The interval's inclusive upper endpoint: one for a `NumericDomain::NormalizedLinear` frame
    /// and unbounded for every other domain.
    double intervalHigh = 1.0;
};

/// Writes `sample * gain + offset` for every valid sample of a three-channel floating frame.
///
/// The destination shares the source's layout exactly and may be the source itself; any other
/// overlap is refused. The frame's representation must be Float32 or Float64: the operation is
/// linear, but an integer frame has a quantization of its own that this engine converts through
/// its explicit conversion surface rather than inside a calibration. Under `RejectOnClip` the frame
/// is read once to count the samples that would leave the interval and nothing is written when any
/// would; under the other two policies the count is reported.
///
/// Computes in double, narrows once, and walks channel rows on the operation's threads; the result
/// does not depend on how many. A sample the frame's validity rule rejects is copied to the
/// destination unchanged. Thread-safe with respect to `execution` and `frame`, which are only read
/// unless the destination is the frame's own storage. Under `ZeroIsNoData`, a valid sample that
/// rounds or clamps to zero is written as the smallest normal magnitude of its representation,
/// with the computed zero's sign, so it stays valid. `DomainRange` is refused; materialize its
/// validity as an explicit mask before applying gains. On a failure after the checks, a stop
/// included, the destination may be partly written, except under `RejectOnClip`'s refusal.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked three-channel RGB source.
/// @param destination where the calibrated samples go, laid out exactly as the source and aligned
///        for its representation, checked before any write. May equal `frame.samples()`.
/// @param destinationBytes bytes available from `destination`, at least the layout's total.
/// @param request the gains, offsets and clipping policy.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for an invalid frame, a null or undersized destination, or a
///         gain, offset or policy outside its stated range; `ErrorCode::Unsupported` for a frame
///         that is not three-channel RGB, not a floating representation, or uses domain-range
///         validity; `ErrorCode::IncompatibleArguments` for a misaligned destination;
///         `ErrorCode::Aliasing` for an overlap other than the source itself;
///         `ErrorCode::InvalidData` under `RejectOnClip` when a sample would leave the interval,
///         carrying the count in `ContextKey::Completed`; a memory-budget refusal;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ColorCalibrationApplyResult>
applyColorCalibration(const ExecutionContext& execution, const StarFrameView& frame, void* destination,
                      std::uint64_t destinationBytes, const ColorCalibrationApplyRequest& request);

/// The meaning a frame has after a colour calibration wrote it.
///
/// The input's meaning with `ProcessingState::colorCalibrated` set. The one place that flag is set,
/// so an orchestrator hands this to `operationTransition` rather than editing the state itself.
///
/// Thread-safe and allocation-free.
///
/// @param input what the source frame meant.
/// @return the restated meaning, or whatever `ImageSemantics::create` refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageSemantics> colorCalibratedSemantics(const ImageSemantics& input);

/// What `applyColorCalibration` does to the frame it writes.
///
/// Samples are written and their meaning is restated - the frame becomes colour calibrated - while
/// the description, solution, cards and output decision all still describe it, and each sample's
/// validity is what it was.
inline constexpr OperationEffect kColorCalibrationApplyEffect{.samples = SampleEffect::Writes,
                                                              .semantics = SemanticEffect::Restates};

/// What `estimateColorBackground` does to the frame it reads.
inline constexpr OperationEffect kColorBackgroundEstimateEffect{.samples = SampleEffect::Reads};

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The frame weights a stack derives from what was measured about its frames.
///
/// One operation over a value: a `StackFrameMeasurement` per frame, stating each figure as present
/// or absent rather than as a zero that might be data, and `computeStackFrameWeights` writing the
/// working set's frame-major weight table for the source the checked model names. Each source's
/// rule is written out beside the request it reads. A set where no frame carries the measurement is
/// uniform and the result and a warning say so; a set where some frames carry it and some do not is
/// refused naming the first frame that does not, because dropping that frame is not a weighting
/// anybody asked for. The photometric source is the model of
/// `Algorithms/Stacking/PhotometricWeighting.h`, called rather than repeated.
///
/// The exposure and signal-to-noise sources weight each frame by its figure over the mean, under the
/// same refusal of a partly measured set.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackQuality.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

class StackModel;

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The weights a caller supplies under `StackWeighting::Explicit`.
///
/// Stated once, per frame or per frame and channel, never both. The spans are borrowed for the
/// call that receives them and must outlive it. Values are dimensionless, finite and
/// non-negative; a zero excludes a frame from a pixel's combination, and at least one frame of
/// every channel must weigh more than nothing. No scale is assumed: a weighted average divides by
/// the weight sum, so weights normalized to mean one - which is what the photometric model
/// produces - and weights on any other scale combine identically.
struct StackFrameWeights
{
    /// One weight per frame, in frame order, or empty.
    std::span<const double> perFrame{};

    /// One weight per frame and channel, frame-major - all of frame zero's channels, then frame
    /// one's - or empty.
    std::span<const double> perFrameChannel{};
};

/// Checks a caller's explicit weights against the stack they are for.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param model the checked stack the weights are for.
/// @param weights the weights.
/// @return success when the weights are well formed for this stack;
///         `ErrorCode::IncompatibleArguments` when the stack does not weigh explicitly and a span
///         is not empty, when it does and both spans are empty or both are filled, or when a
///         span's length is not the frame count or the frame count times the channel count;
///         `ErrorCode::InvalidArgument` for a weight that is not finite or is negative, naming its
///         index; and `ErrorCode::EmptySelection` when no frame of some channel weighs more than
///         zero, naming the channel.
[[nodiscard]] AFTERNIGHTENGINE_API Status checkStackFrameWeights(const StackModel& model,
                                                                 const StackFrameWeights& weights);

/// How many channels a measurement record describes on their own.
///
/// The widest schema this engine stacks with per-channel statistics has four channels; a stack
/// with more takes the frame-level figures for the channels beyond these.
inline constexpr std::uint32_t kMaxStackWeightChannels = 4u;

/// The figures measured on one channel of one frame.
///
/// Every value is present or absent; a present value is finite and strictly positive. A channel
/// figure is read only when every frame of the stack carries it for that channel, so a set that
/// measures a channel on some frames and not others is refused rather than mixed.
struct StackChannelMeasurement
{
    /// The channel's background noise, in the frame's numeric domain.
    std::optional<double> backgroundNoise{};

    /// The channel's robust dispersion, in the same units: the iterative k-sigma scale where it
    /// was measured, the scaled median absolute deviation otherwise.
    std::optional<double> dispersion{};

    /// The median full width at half maximum of the channel's stars, in pixels.
    std::optional<double> fullWidth{};
};

/// The figures measured on one frame, as the weighting sources read them.
///
/// Every value is present or absent. Each source reads the figures its rule names and no other;
/// a record is checked whole, so a malformed figure is refused whether or not the source reads it.
struct StackFrameMeasurement
{
    /// How many stars were detected. Zero is a measured count of none.
    std::optional<std::uint32_t> starCount{};

    /// The median roundness of the detected stars, in `(0, 1]`.
    std::optional<double> medianRoundness{};

    /// The median full width at half maximum of the detected stars, in pixels.
    std::optional<double> fullWidth{};

    /// The frame's background noise, in its numeric domain.
    std::optional<double> backgroundNoise{};

    /// The frame's robust dispersion, in the same units.
    std::optional<double> dispersion{};

    /// How many frames were already integrated into this one; absent for a single exposure.
    /// A present value is at least one.
    std::optional<std::uint32_t> integratedFrameCount{};

    /// The photometric zero point, its uncertainty and the alignment residual, read by
    /// `StackWeighting::Photometric` and checked by its own model.
    PhotometricFrameMeasurement photometric{};

    /// The first channels' own figures, read by `StackWeighting::Noise` and, under
    /// `StackWeightingRequest::perChannelFwhm`, by `StackWeighting::Fwhm`.
    std::array<StackChannelMeasurement, kMaxStackWeightChannels> channels{};

    /// How long the frame was exposed, in seconds; read by `StackWeighting::Exposure`.
    std::optional<double> exposureSeconds{};

    /// The frame's measured signal-to-noise ratio, dimensionless; read by
    /// `StackWeighting::SignalToNoise`. What it is the ratio of is the measuring operation's
    /// business: the source reads only how the frames' ratios compare.
    std::optional<double> signalToNoise{};
};

/// Why a completed weighting produced the table it did.
enum class StackFrameWeightingDisposition : std::uint8_t
{
    /// The source is `StackWeighting::Uniform`, and every entry is one.
    Uniform,

    /// The source's rule was evaluated over the measurements.
    Computed,

    /// No frame carried the figure the source reads, so every entry is one and a warning was
    /// reported.
    UniformNoMeasurements,

    /// Every frame carried the same figure, so the source's rule cannot rank them and every
    /// entry is one; an information diagnostic was reported.
    UniformEqualMeasurements,

    /// The photometric model produced no usable raw weight, so every entry is one.
    UniformDegenerate,

    /// The source is `StackWeighting::Explicit`: the caller's weights were checked and copied
    /// into the table, and nothing was derived.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Supplied
};

/// Whether a value names a disposition this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param disposition the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackFrameWeightingDisposition(StackFrameWeightingDisposition disposition) noexcept
{
    return static_cast<std::uint8_t>(disposition) <=
           static_cast<std::uint8_t>(StackFrameWeightingDisposition::Supplied);
}

/// The disposition's own name.
///
/// Thread-safe and allocation-free.
///
/// @param disposition the disposition to name. A value outside the enumeration is named
///        `"UnknownStackFrameWeightingDisposition"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
stackFrameWeightingDispositionName(StackFrameWeightingDisposition disposition) noexcept;

/// What a completed weighting wrote, and what it read to write it.
struct StackFrameWeightingResult
{
    /// Why the table holds what it holds.
    StackFrameWeightingDisposition disposition = StackFrameWeightingDisposition::Uniform;

    /// The source the model named.
    StackWeighting source = StackWeighting::Uniform;

    /// How many frames the table has.
    std::uint32_t frameCount = 0u;

    /// How many channels each frame has.
    std::uint32_t channelCount = 0u;

    /// How many frames carried the figure the source reads.
    std::uint32_t measuredFrameCount = 0u;

    /// How many entries were raised to the model's floor, over every channel.
    std::uint32_t flooredCount = 0u;

    /// Whether at least one channel's entries were derived from that channel's own figures.
    bool perChannel = false;

    /// The written table as a whole.
    StackWeightSummary summary{};

    /// `summary.efficiency` times the frame count: how many uniform frames the table is worth.
    double effectiveFrameCount = 0.0;

    /// The photometric model's own result, present under `StackWeighting::Photometric`.
    std::optional<PhotometricWeightingResult> photometric{};

    /// The engine-context bytes held during the call, for the photometric model's records.
    std::uint64_t workingBytes = 0u;
};

/// Derives a stack's frame weights from its measurements.
///
/// The rules, per source. `Noise`: one over the square of the noise, the noise first multiplied
/// by the reference frame's dispersion over the frame's own where both are present. `StarCount`:
/// the counts - or the counts times the roundness under the roundness-weighted rule - normalized
/// between the fewest and the most, put through the scaling, and raised to the floor; equal
/// counts are uniform. `Fwhm`: each width penalized by
/// `2 * width * (referenceStars - stars) / stars` where the reference frame has stars and the
/// frame has some, floored at a tenth of itself, then the inverse square rescaled between the
/// narrowest and the widest and raised to the floor; equal widths are uniform. `ImageCount`: the
/// integrated frame count, one when absent. `Photometric`: `computePhotometricStackWeights` over
/// the records, whose result is carried back. `Exposure`: the exposure in seconds.
/// `SignalToNoise`: the measured ratio. Under `Noise`, and under `Fwhm` when the request asks for
/// per-channel widths, a channel every frame measures is weighted from its own figures and any
/// other channel from the frame-level ones. Every channel's entries are then divided by their
/// mean, so the table sums to the frame count per channel.
///
/// The reference frame is the model's `StackNormalizationRequest::referenceFrame`. Every record is
/// checked before anything is written, and the table is written only on success. Admitted
/// concurrency changes no output bit: the sums run in frame order. A successful call reports an
/// informational diagnostic describing the table, a warning when no frame carried the figure,
/// and progress once, complete.
///
/// Thread-safe with respect to `execution` and the measurements, which are only read; the table is
/// written and must not be shared. Allocates the photometric model's records from the engine
/// context under that source and a failure it has to describe.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack, whose weighting names the source. Not `StackWeighting::Explicit`,
///        whose weights a caller checks with `checkStackFrameWeights`.
/// @param measurements one record per frame of the model.
/// @param frameWeights receives one weight per frame and channel, frame-major - the working set's
///        `frameWeights` table - and must hold exactly that many.
/// @return the result; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint;
///         `ErrorCode::IncompatibleArguments` when the model weighs explicitly, when the records
///         or the table are not the model's counts, or when a figure the source reads is carried
///         by some frames and not by others - naming the figure and, as the requested count, the
///         first frame without it; `ErrorCode::InvalidArgument` for a present figure that is not
///         finite, not positive, a roundness above one or an integrated frame count of zero,
///         naming the figure and the frame; and the photometric model's own refusals under that
///         source, which include the engine context's refusal of its workspace.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackFrameWeightingResult>
computeStackFrameWeights(const ExecutionContext& execution, const StackModel& model,
                         std::span<const StackFrameMeasurement> measurements, std::span<double> frameWeights);

/// Upper bound for scratch held while deriving a stack's frame weights.
///
/// Includes the photometric records and their uncertainty propagation, even when the inputs omit
/// uncertainties, because the plan is made before the measurements are read. Thread-safe and
/// allocation-free.
///
/// @param model the checked stack whose weighting is being planned.
/// @return the bytes; zero for sources that derive weights without temporary allocations.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t stackFrameWeightingWorkingBytes(const StackModel& model) noexcept;

} // namespace an

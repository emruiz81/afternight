// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Deterministic frame weights from photometric and registration measurements.
///
/// `computePhotometricStackWeights` turns one `PhotometricFrameMeasurement` per frame - an
/// optional zero point and its uncertainty in magnitudes, and an optional registration residual in
/// pixels - into one `PhotometricStackWeight` per frame whose arithmetic mean is one, so a stack
/// weighted by them does not change scale with the frame count. The model is a transparency,
/// confidence and alignment product; this header names its units, its defaults and its tunable terms
/// in `PhotometricWeightingRequest`. The measurements are consumed, never inferred from pixels:
/// catalog matching and star fitting produce them elsewhere.
///
/// Missing is not malformed. A set with no zero point, a single frame and a model that degenerates
/// are successful, named `PhotometricWeightingDisposition` outcomes; a present value that is not
/// finite or out of range is a typed failure, never read as absent.
/// When every measured zero point carries an uncertainty, every weight carries a first-order
/// propagated uncertainty; when one does not, none does, because every weight depends on the
/// common mean.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most frames one photometric-weighting call accepts.
///
/// The bound keeps the deterministic ranking and uncertainty workspace predictable even when a
/// caller has not imposed a tighter document-specific limit.
inline constexpr std::uint32_t kMaxPhotometricWeightFrames = kMaxStackFrames;

/// The default confidence penalty, in inverse magnitudes.
///
/// Multiplying this by a zero-point standard uncertainty in magnitudes gives the dimensionless
/// term added to one in the confidence denominator.
inline constexpr double kDefaultZeroPointUncertaintyPenaltyPerMagnitude = 5.0;

/// The default alignment penalty, applied to squared RMS excess ratio.
inline constexpr double kDefaultAstrometricAlignmentPenalty = 4.0;

/// The default raw-weight floor, before mean-one normalization.
inline constexpr double kDefaultPhotometricMinimumRawWeight = 0.01;

/// Why a completed photometric-weighting call produced the weights it did.
enum class PhotometricWeightingDisposition : std::uint8_t
{
    /// At least one zero point was available and the photometric model was evaluated.
    Computed,

    /// The one supplied frame necessarily has normalized weight one.
    SingleFrame,

    /// No frame supplied a zero point, so every frame received weight one.
    UniformNoZeroPoints,

    /// The model produced no positive representable raw weight, so every frame received weight one.
    UniformDegenerate
};

/// Whether a value belongs to `PhotometricWeightingDisposition`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param disposition the value to test.
/// @return true for one of the four declared values; false for an out-of-range cast.
[[nodiscard]] constexpr bool isPhotometricWeightingDisposition(PhotometricWeightingDisposition disposition) noexcept
{
    switch (disposition)
    {
    case PhotometricWeightingDisposition::Computed:
    case PhotometricWeightingDisposition::SingleFrame:
    case PhotometricWeightingDisposition::UniformNoZeroPoints:
    case PhotometricWeightingDisposition::UniformDegenerate:
        return true;
    }
    return false;
}

/// The stable name of a photometric-weighting disposition.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param disposition the value to name.
/// @return a never-null string literal; `"UnknownPhotometricWeightingDisposition"` for an
///         out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
photometricWeightingDispositionName(PhotometricWeightingDisposition disposition) noexcept;

/// The measurements available for one candidate stack frame.
///
/// All present values are measured inputs rather than sentinels: zero is a legitimate zero point
/// and a legitimate zero uncertainty. The zero points in one call must use the same catalog band,
/// magnitude convention, exposure normalization and aperture convention. Astrometric RMS values
/// must describe the same coordinate scale, so their ratios are meaningful.
struct PhotometricFrameMeasurement
{
    /// The fitted photometric zero point, in magnitudes, or absent when the frame was not measured.
    std::optional<double> zeroPointMagnitudes{};

    /// The zero point's one-standard-deviation uncertainty, in magnitudes.
    ///
    /// May be present only when `zeroPointMagnitudes` is present. Absence leaves the frame's
    /// confidence multiplier at one and makes result uncertainty incomplete; zero means a measured
    /// exact input for propagation, not missing data.
    std::optional<double> zeroPointStandardUncertaintyMagnitudes{};

    /// Registration residual root mean square, in pixels, or absent when it was not measured.
    ///
    /// A present value is strictly positive. It affects a frame only when that frame also has a
    /// zero point, because an unmeasured frame receives the measured set's median raw weight.
    std::optional<double> astrometricRootMeanSquarePixels{};
};

/// The tunable terms of the frame-weight model.
struct PhotometricWeightingRequest
{
    /// Confidence penalty per magnitude of zero-point standard uncertainty.
    ///
    /// Finite and non-negative. A frame with uncertainty `sigma` is multiplied by
    /// `1 / (1 + uncertaintyPenaltyPerMagnitude * sigma)`.
    double uncertaintyPenaltyPerMagnitude = kDefaultZeroPointUncertaintyPenaltyPerMagnitude;

    /// Penalty applied to the squared excess over the median astrometric RMS.
    ///
    /// Finite and non-negative. RMS at or below the median is neutral; a larger value with ratio
    /// excess `r` is multiplied by `1 / (1 + alignmentPenalty * r * r)`.
    double alignmentPenalty = kDefaultAstrometricAlignmentPenalty;

    /// Smallest raw weight before the output is normalized to arithmetic mean one.
    ///
    /// Finite and in the closed interval `[0, 1]`. The floor belongs to the model's raw scale,
    /// whose minimum-zero-point transparency is one, and is applied before missing-frame median
    /// substitution and normalization.
    double minimumRawWeight = kDefaultPhotometricMinimumRawWeight;

    /// Whether two requests state the same model terms.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every term is equal.
    [[nodiscard]] friend constexpr bool operator==(const PhotometricWeightingRequest& left,
                                                   const PhotometricWeightingRequest& right) noexcept = default;
};

/// The stack-ready result for one input frame.
struct PhotometricStackWeight
{
    /// Dimensionless weight normalized so the arithmetic mean over the call is one.
    double value = 1.0;

    /// Linearized one-standard-deviation uncertainty of `value`, or absent when any measured zero
    /// point omitted its uncertainty.
    ///
    /// Propagation treats the supplied zero-point errors as independent and holds the selected
    /// floor, median rank, confidence multiplier and astrometric multiplier fixed. It is therefore
    /// a local first-order uncertainty, not a confidence interval.
    std::optional<double> standardUncertainty{};

    /// Whether this frame had no zero point and inherited the measured raw-weight median.
    bool usedMedianSubstitution = false;

    /// Whether this frame's model value was raised to `minimumRawWeight`.
    bool minimumApplied = false;
};

/// Summary and diagnostics for one completed weighting model.
struct PhotometricWeightingResult
{
    /// Why this call produced computed or uniform weights.
    PhotometricWeightingDisposition disposition = PhotometricWeightingDisposition::UniformNoZeroPoints;

    /// Number of frame records and output weights.
    std::uint32_t frameCount = 0u;

    /// Frames that supplied a photometric zero point.
    std::uint32_t measuredFrameCount = 0u;

    /// Measured frames that also supplied a zero-point standard uncertainty.
    std::uint32_t uncertaintyMeasurementCount = 0u;

    /// Measured frames that supplied an astrometric RMS used to form the median.
    std::uint32_t alignmentMeasurementCount = 0u;

    /// Frames whose raw weight was substituted from the measured-set median.
    std::uint32_t medianSubstitutionCount = 0u;

    /// Measured frames whose raw model value was raised to the requested floor.
    std::uint32_t minimumAppliedCount = 0u;

    /// Smallest measured zero point, in magnitudes, or absent when no zero point was supplied.
    std::optional<double> baselineZeroPointMagnitudes{};

    /// Median usable astrometric RMS among measured frames, in pixels, or absent when none exists.
    std::optional<double> medianAstrometricRootMeanSquarePixels{};

    /// Whether every measured zero point supplied the uncertainty needed for every output's
    /// `standardUncertainty`.
    bool uncertaintyComplete = false;

    /// Sum of the stack-ready weights, equal to `frameCount` within Float64 rounding.
    double weightSum = 0.0;

    /// Kish effective frame count, `(sum(weight)^2) / sum(weight^2)`.
    ///
    /// It is one for a one-frame set, `frameCount` for uniform weights, and lower when the stack is
    /// dominated by fewer frames.
    double effectiveFrameCount = 0.0;

    /// Peak engine-context bytes held for deterministic ranking and uncertainty propagation.
    std::uint64_t workingBytes = 0u;
};

/// Computes deterministic photometric weights ready for a later stack executor.
///
/// For a measured frame the raw model is the product of three dimensionless terms: transparency
/// `10^(0.4 * (zeroPoint - minimumZeroPoint))`, zero-point confidence, and the optional
/// above-median astrometric quality. The raw floor is then applied. A frame without a zero point
/// receives the measured raw-weight median. Finally every weight is divided by the input-order
/// arithmetic mean, so the weights average one and do not depend on the frame count.
///
/// Validation, minimum selection, ranking, sums and uncertainty propagation all follow input
/// order or a value-and-input-index ordering fixed by this operation; admitted concurrency does not
/// change any output bit. The call uses engine-context memory for its bounded workspace, checks
/// cancellation before committing output, and writes no output element unless it can return a
/// complete result. Calls on distinct executions and output spans are reentrant. A uniform
/// fallback - no zero point, or a degenerate model - is reported as a warning through the
/// execution's diagnostic sink, and every successful return reports progress once, complete.
///
/// @param execution the operation policy and engine-context lease.
/// @param measurements one record per stack frame, between one and
///        `kMaxPhotometricWeightFrames` records.
/// @param request the finite non-negative penalty terms and raw-weight floor.
/// @param output receives one stack-ready weight per measurement and must have exactly the same
///        length. Existing elements are preserved on failure.
/// @return the completed report; `ErrorCode::NotInitialized` without an engine-context lease;
///         `ErrorCode::EmptySelection` for no frames; `ErrorCode::LimitExceeded` above the frame
///         ceiling; `ErrorCode::IncompatibleArguments` for an output-length mismatch or an
///         uncertainty without its zero point; `ErrorCode::InvalidArgument` for an out-of-range
///         request or a non-finite measurement, a negative uncertainty, or a non-positive RMS;
///         `ErrorCode::InvalidData` when a finite input set produces an unrepresentable model or
///         propagated uncertainty; `ErrorCode::MemoryBudgetExceeded` or
///         `ErrorCode::AllocationFailed` when the engine context refuses the workspace; and
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from a checkpoint.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PhotometricWeightingResult>
computePhotometricStackWeights(const ExecutionContext& execution,
                               std::span<const PhotometricFrameMeasurement> measurements,
                               const PhotometricWeightingRequest& request, std::span<PhotometricStackWeight> output);

/// Scratch residency of the photometric model, excluding the caller's input and output records.
///
/// What `computePhotometricStackWeights` takes from the engine context and reports as
/// `PhotometricWeightingResult::workingBytes`. Thread-safe and allocation-free; zero or one frame
/// needs no scratch.
///
/// @param frames how many frames are measured.
/// @param uncertaintyComplete whether every measured zero point supplies uncertainty, which adds
///        the derivative storage the propagation needs.
/// @return the bytes held concurrently during the weighting.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t photometricWeightingWorkingBytes(std::uint32_t frames,
                                                                                  bool uncertaintyComplete) noexcept;

} // namespace an

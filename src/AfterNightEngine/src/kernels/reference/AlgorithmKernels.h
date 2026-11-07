// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - private run shapes shared by algorithm executors and CPU dispatch tables.
//
// An algorithm executor decides everything once - validity, geometry, conversions, which frames
// and rows to read - and then hands contiguous runs to a body that does only arithmetic. This
// header is the vocabulary of that hand-off: the parameters and outcome of each kind of run, the
// function-pointer types a dispatch table holds, and the accessors that read one entry out of the
// execution's immutable CPU table. A table holds function pointers, so no shape here names a
// public algorithm type; where a run needs a public enumeration it carries a private spelling of
// it, which the algorithm maps onto the public one.
//
// Every accessor answers null for the scalar table, which is the one `ComputeBackend::Reference`
// selects, and for a vector table that compiled no body for that run. A null answer means the
// algorithm runs its own scalar authority. Several compiled runs share their text with that
// authority through a row body in a `.inl` file in this directory, so their tails are the scalar
// row by construction.
//
// A run reports trouble through a count, a flag or a failure enumerator rather than a `Status`,
// which can allocate, because it is called once per row; the operation above turns what it reports
// into one failure. Outcome counts are added up by the caller across rows and threads.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace an
{

class ExecutionContext;

/// What one compiled pointwise run wrote.
struct PointwiseRunOutcome
{
    /// Pixels written from the start of the run: a whole number of vectors, after which the caller
    /// evaluates the rest with the scalar program; or, for a run over integer samples, every pixel.
    std::uint64_t pixelsProcessed = 0u;

    /// Written samples, across every channel, whose value was not finite - before an integer run
    /// narrowed it.
    std::uint64_t nonFiniteOutputs = 0u;

    /// Written samples, across every channel, that an integer run held at its representation's
    /// range; zero for a floating run.
    std::uint64_t saturatedOutputs = 0u;
};

/// One contiguous run of a pointwise program over all-valid samples, compiled for one instruction
/// set.
///
/// `sources` holds one row pointer per source and channel, source-major -
/// `sources[source * channelCount + channel]` - and `destinations` one per channel, each already
/// offset to the run's first pixel. A floating run writes a prefix of `pixelCount` and says how long
/// it was: the vector-width prefix for a scale, bias and clamp, an add of a second source before
/// them, or one sampled curve, whose steps the evaluator answers with the same bits; every pixel,
/// its last partial vector included, for any other program, since a gamma's vector power is not the
/// evaluator's. A run over integer samples widens every source to the program's
/// arithmetic width, which holds each of their codes exactly, evaluates the steps there and narrows
/// the result as the evaluator's conversion narrows it - not a number to zero, the infinities and
/// what lies beyond the representation to its ends, everything else to the nearest code, ties to
/// even - and writes every pixel, its last partial vector included. `pointwiseRunFor` answers null
/// for a program with a step no compiled body evaluates.
using PointwiseRunFn = PointwiseRunOutcome (*)(const PointwiseProgram& program, const void* const* sources,
                                               void* const* destinations, std::uint64_t pixelCount) noexcept;

/// The compiled pointwise run over one integer representation at one arithmetic width, or null where
/// the table compiled none: eight- and sixteen-bit codes at either width, thirty-two-bit ones in
/// Float64 alone, which is the only width that holds every one of them.
using PointwiseIntegerRunLookupFn = PointwiseRunFn (*)(SampleType sampleType, ComputePrecision precision) noexcept;

/// One contiguous run of a Float32 program that is one sampled curve, read from its table narrowed to Float32.
///
/// `table` holds the step's `entries` entries, each the program's entry narrowed to nearest. The run writes the bits
/// the evaluator writes for the step - interpolated or nearest, clamped or extrapolated outside its domain - and is
/// never handed one that refuses a value outside its domain or reads the entry below. It returns the number of
/// outputs that are not finite.
using SampledCurveFloat32RunFn = std::uint64_t (*)(const float* source, float* destination, std::uint64_t count,
                                                   const PointwiseOp& step, const float* table,
                                                   std::uint32_t entries) noexcept;

/// One contiguous normalized Float32 lower-neighbour lookup run.
///
/// The function is present only in a compiled CPU table. It returns the number of non-finite inputs;
/// NaNs are preserved, while infinities and finite values outside zero to one are clamped to an
/// endpoint. With `stream` it writes its full vectors with streaming stores, which do not read a
/// destination line before writing it, and fences them before it returns; a caller asks for that
/// only where the destination is not the source.
using NormalizedLowerCurveFloat32RunFn = std::uint64_t (*)(const float* source, float* destination,
                                                           std::uint64_t sampleCount, const float* table,
                                                           std::uint32_t tableEntries, bool stream) noexcept;

/// A range selection's weighting, prepared once by the algorithm for every run.
///
/// A sample inside `[lowerLimit, upperLimit]` weighs one, or rises linearly from zero at a limit to
/// one at the nearer plateau when `fuzzy` is set; a sample outside weighs zero.
struct RangeSelectionRunParameters
{
    /// The smallest sample value selected, inclusive.
    double lowerLimit = 0.0;

    /// The largest sample value selected, inclusive.
    double upperLimit = 0.0;

    /// One over the width of each fuzzy ramp, or zero when there is no ramp.
    double inverseRamp = 0.0;

    /// Where the lower ramp reaches full weight: `lowerLimit` plus the ramp width.
    double lowerPlateau = 0.0;

    /// Where the upper ramp leaves full weight: `upperLimit` less the ramp width.
    double upperPlateau = 0.0;

    /// Whether the weight is multiplied by the sample itself.
    bool screening = false;

    /// Whether the written value is one less the weight.
    bool invert = false;

    /// Whether the ramps apply, which is when the ramp width is nonzero.
    bool fuzzy = false;
};

/// One contiguous run of normalized Float32 samples replaced by their range-selection weights.
///
/// Evaluated in Float64 lanes and narrowed once per sample. Returns how many inputs were not
/// finite; every sample of the run is written.
using RangeSelectionFloat32RunFn = std::uint64_t (*)(const float* source, float* destination, std::uint64_t sampleCount,
                                                     const RangeSelectionRunParameters& parameters) noexcept;

/// What one destination row of a coordinate warp reads and how it is placed.
///
/// The matrix is nine row-major doubles mapping a destination position to a source one, which is
/// the direction sampling asks in. Everything else describes the source plane the taps come from.
struct WarpRowParameters
{
    /// The source plane's first sample.
    const float* source = nullptr;

    /// The destination-to-source homogeneous matrix, nine row-major doubles.
    const double* destinationToSource = nullptr;

    /// Samples from one source row to the next, which is not the width when the plane is padded.
    std::uint64_t sourceRowStride = 0u;

    /// The source extent, signed because a window's first index may be negative.
    std::int64_t sourceWidth = 0;

    /// The source height, signed for the same reason.
    std::int64_t sourceHeight = 0;

    /// Whether windows wholly outside the source may produce a valid constant zero.
    /// Set only for constant-zero borders over all-valid samples without renormalization.
    bool constantZeroBorder = false;
};

/// One contiguous Float32 destination row of a cubic coordinate warp, over samples it can reach.
///
/// The function is present only in a compiled CPU table, and it writes a prefix rather than a row:
/// it stops at the first destination column whose four-by-four window needs the general border
/// reader. With `constantZeroBorder`, wholly exterior windows produce valid zeroes; partially
/// overlapping windows still stop. The caller finishes from there and may call again.
///
/// Every sample it writes is the general reader's sample to within the kernel conformance bound: it
/// resolves the same taps with the same weights and sums them as one column-weighted accumulation
/// per source row rather than as one running total over all sixteen, which is what lets a vector of
/// lanes carry the window.
using WarpCubicRowRunFn = std::uint32_t (*)(const WarpRowParameters& parameters, std::uint32_t y, std::uint32_t first,
                                            std::uint32_t last, float* destination) noexcept;

/// The cubic warp row selected by one execution's immutable CPU table, or null when this build has
/// no compiled row for the set that execution resolves to.
[[nodiscard]] WarpCubicRowRunFn warpCubicRowFor(const ExecutionContext& execution) noexcept;

/// One contiguous Float32 destination row of a Lanczos-4 coordinate warp, over samples it can reach.
///
/// The cubic row's contract with an eight-by-eight window: present only in a compiled CPU table, it
/// writes a prefix and stops at the first destination column whose window needs the general border
/// reader, and with `constantZeroBorder` a wholly exterior window is a valid zero. It also stops at a
/// column whose coordinate is a whole sample on either axis, where the reader's tap under it weighs
/// exactly one.
///
/// Every sample it writes is the general reader's sample to within the kernel conformance bound: the
/// taps are the same function of the same distances, evaluated through one sine and one cosine of a
/// quarter of each axis's fraction and of one less it rather than two sines a tap, and summed as one
/// row-weighted accumulation per source row with both axes' normalisations taken as one division.
using WarpLanczos4RowRunFn = std::uint32_t (*)(const WarpRowParameters& parameters, std::uint32_t y,
                                               std::uint32_t first, std::uint32_t last, float* destination) noexcept;

/// The Lanczos-4 warp row selected by one execution's immutable CPU table, or null when this build
/// has no compiled row for the set that execution resolves to.
[[nodiscard]] WarpLanczos4RowRunFn warpLanczos4RowFor(const ExecutionContext& execution) noexcept;

/// Scalars shared by the Float32 and Float64 fused calibration rows.
///
/// Each step rounds to the samples' own representation before the next reads it;
/// `kernels/reference/CalibrationRow.inl` is the scalar text of the row.
struct CalibrationRunParameters
{
    /// Added to every light sample before the bias is subtracted. Zero skips the step.
    double pedestal = 0.0;

    /// Multiplies each dark sample before it is subtracted. One skips the multiplication.
    double darkScale = 1.0;

    /// One divisor per flat channel, which a flat sample is divided by before it divides the light;
    /// null when the flat is used as stored.
    const double* flatNormalization = nullptr;

    /// Interleaved samples per pixel of the light, bias, dark and destination rows.
    std::uint32_t channelCount = 1u;

    /// Interleaved samples per pixel of the flat row: one for a flat every channel shares, or
    /// `channelCount`.
    std::uint32_t flatChannelCount = 1u;
};

/// Counts returned by one contiguous fused calibration row.
struct CalibrationRunOutcome
{
    /// Samples written to the destination, which is every sample of the run.
    std::uint64_t samplesWritten = 0u;

    /// Written samples that are not finite, the not-a-number a zero flat divisor produces included.
    std::uint64_t nonFiniteOutputs = 0u;
};

/// One contiguous Float32 row of fused bias, dark and flat calibration.
using CalibrationFloat32RunFn = CalibrationRunOutcome (*)(const float* input, const float* bias, const float* dark,
                                                          const float* flat, float* destination,
                                                          std::uint64_t pixelCount,
                                                          const CalibrationRunParameters& parameters) noexcept;

/// One contiguous Float64 row of fused bias, dark and flat calibration.
using CalibrationFloat64RunFn = CalibrationRunOutcome (*)(const double* input, const double* bias, const double* dark,
                                                          const double* flat, double* destination,
                                                          std::uint64_t pixelCount,
                                                          const CalibrationRunParameters& parameters) noexcept;

/// The Float32 fused calibration row selected by one execution's immutable CPU table.
[[nodiscard]] CalibrationFloat32RunFn calibrationFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The Float64 fused calibration row selected by one execution's immutable CPU table.
[[nodiscard]] CalibrationFloat64RunFn calibrationFloat64RunFor(const ExecutionContext& execution) noexcept;

/// Scalars shared by the Float32 and Float64 normalization rows.
///
/// `scale` and `offset` hold one value per interleaved channel, or one value for a planar row.
/// `rule` is one of the three value rules a compiled row serves; a packed mask never reaches one.
struct NormalizationRunParameters
{
    /// The multiplier of each channel, `channelCount` of them.
    const double* scale = nullptr;

    /// The offset of each channel, added after the multiply; not read when `multiplicative` is set.
    const double* offset = nullptr;

    /// Interleaved samples per pixel, which indexes `scale` and `offset`; one for a planar row.
    std::uint32_t channelCount = 1u;

    /// The value rule deciding which samples are transformed; the rest are copied unchanged.
    ValidityRule rule = ValidityRule::AllValid;

    /// Whether a sample is only scaled, with no offset added.
    bool multiplicative = false;
};

/// Counts returned by one contiguous normalization row.
struct NormalizationRunOutcome
{
    /// Samples the rule admitted, scaled and offset in Float64 and narrowed once.
    std::uint64_t samplesTransformed = 0u;

    /// Samples the rule rejected, copied to the destination unchanged.
    std::uint64_t samplesPreserved = 0u;

    /// Results that narrowed to exactly zero under `ValidityRule::ZeroIsNoData` and were moved to
    /// the smallest normal magnitude, signed like the scaled sample before its offset, so they stay
    /// data.
    std::uint64_t noDataCollisions = 0u;
};

/// One contiguous Float32 row of affine normalization under a value rule.
using NormalizationFloat32RunFn = NormalizationRunOutcome (*)(const float* source, float* destination,
                                                              std::uint64_t pixelCount,
                                                              const NormalizationRunParameters& parameters) noexcept;

/// One contiguous Float64 row of affine normalization under a value rule.
using NormalizationFloat64RunFn = NormalizationRunOutcome (*)(const double* source, double* destination,
                                                              std::uint64_t pixelCount,
                                                              const NormalizationRunParameters& parameters) noexcept;

/// The Float32 normalization row selected by one execution's immutable CPU table.
[[nodiscard]] NormalizationFloat32RunFn normalizationFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The Float64 normalization row selected by one execution's immutable CPU table.
[[nodiscard]] NormalizationFloat64RunFn normalizationFloat64RunFor(const ExecutionContext& execution) noexcept;

/// One grid's node values mixed along the row axis for one image row of local normalization.
///
/// `mixed` holds one value per node column - the grid's four contributing node rows blended at
/// the row's fraction, or two for a bilinear row - and `lower` and `upper` hold, per column, the
/// smallest and largest of those contributing nodes, which a bicubic evaluation is clamped to. A
/// bilinear row has no overshoot to clamp, so its bounds are neither written nor read.
struct LocalNormalizationRunGrid
{
    /// The grid's node rows blended at the row's fraction, one value per node column.
    const double* mixed = nullptr;

    /// Per node column, the smallest contributing node value; read by a bicubic row only.
    const double* lower = nullptr;

    /// Per node column, the largest contributing node value; read by a bicubic row only.
    const double* upper = nullptr;
};

/// Everything one row of local normalization needs beside its samples.
///
/// `fractions` holds each column's position within its cell, `cellStarts` holds the first column
/// of each of the `gridSize + 1` cells followed by the row width, and `sampleStride` is one for a
/// planar row and the channel count for an interleaved one. `scale.mixed` is null when one factor
/// serves the whole row, in which case `uniformScale` is that factor. `rule` is one of the three
/// value rules a compiled row serves; a packed mask never reaches one.
struct LocalNormalizationRunParameters
{
    /// Each column's position within its cell, one per column of the row.
    const double* fractions = nullptr;

    /// The first column of each of the `gridSize + 1` cells, followed by the row width.
    const std::uint32_t* cellStarts = nullptr;

    /// Cells along one axis of the grid; each axis has one node more.
    std::uint32_t gridSize = 0u;

    /// Samples from one pixel of the row to the next.
    std::uint32_t sampleStride = 1u;

    /// The reference frame's background surface, mixed for this row.
    LocalNormalizationRunGrid reference{};

    /// This frame's own background surface, mixed for this row.
    LocalNormalizationRunGrid target{};

    /// The scale surface mixed for this row, or a grid whose `mixed` is null when `uniformScale`
    /// serves the whole row.
    LocalNormalizationRunGrid scale{};

    /// The factor applied everywhere when `scale.mixed` is null.
    double uniformScale = 1.0;

    /// The value rule deciding which samples are transformed; the rest are copied unchanged.
    ValidityRule rule = ValidityRule::AllValid;

    /// Whether the surfaces are interpolated as clamped Catmull-Rom cubics rather than bilinearly.
    bool bicubic = false;
};

/// Counts returned by one row of local normalization.
struct LocalNormalizationRunOutcome
{
    /// Samples the rule admitted and the row transformed as
    /// `scale * (sample - target) + reference`, in Float64 and narrowed once.
    std::uint64_t samplesTransformed = 0u;

    /// Samples the rule rejected, copied to the destination unchanged.
    std::uint64_t samplesPreserved = 0u;

    /// Results that narrowed to exactly zero under `ValidityRule::ZeroIsNoData` and were moved to
    /// the smallest normal magnitude of the zero's own sign, so they stay data.
    std::uint64_t noDataCollisions = 0u;
};

/// One row of Float32 local normalization under a value rule.
using LocalNormalizationFloat32RunFn = LocalNormalizationRunOutcome (*)(
    const float* source, float* destination, const LocalNormalizationRunParameters& parameters) noexcept;

/// One row of Float64 local normalization under a value rule.
using LocalNormalizationFloat64RunFn = LocalNormalizationRunOutcome (*)(
    const double* source, double* destination, const LocalNormalizationRunParameters& parameters) noexcept;

/// The Float32 local-normalization row selected by one execution's immutable CPU table.
[[nodiscard]] LocalNormalizationFloat32RunFn
localNormalizationFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The Float64 local-normalization row selected by one execution's immutable CPU table.
[[nodiscard]] LocalNormalizationFloat64RunFn
localNormalizationFloat64RunFor(const ExecutionContext& execution) noexcept;

/// One chunk's running reduction terms, which the compiled runs add each contiguous run into.
///
/// A run folds in every sample it is given: the algorithm reaches for one only over a Float32
/// component with one sample per pixel whose rule admits every sample and that has no value window.
struct ReductionRunAccumulator
{
    /// Samples folded in.
    std::uint64_t count = 0u;

    /// The smallest sample, widened to Float64; positive infinity before the first.
    double minimum = std::numeric_limits<double>::infinity();

    /// The largest sample, widened to Float64; negative infinity before the first.
    double maximum = -std::numeric_limits<double>::infinity();

    /// The sum of the samples.
    double sum = 0.0;

    /// The sum of the samples' magnitudes.
    double l1 = 0.0;

    /// The sum of the squared samples.
    double squaredL2 = 0.0;

    /// The largest magnitude.
    double infinityNorm = 0.0;

    /// The sum of squared deviations from the mean a centered run was given; the first-pass run
    /// leaves it alone.
    double centeredMoment = 0.0;

    /// Cleared when a first-pass run met a sample that is not finite, which the algorithm refuses.
    bool finite = true;
};

/// The first reduction pass over one contiguous run of Float32 samples, in Float64 lanes.
///
/// Adds the run's count, extrema, sum and norms to `accumulator`. The lanes sum in their own order,
/// so a compiled pass need not reproduce the scalar pass's bits; it does reproduce its own for one
/// dispatched target, whatever the thread count.
using ReductionFirstRunFn = void (*)(const void* source, std::uint64_t sampleCount,
                                     ReductionRunAccumulator& accumulator) noexcept;

/// The second reduction pass over one contiguous run of Float32 samples: each sample's squared
/// deviation from `mean`, in Float64 lanes, added to `accumulator.centeredMoment`.
using ReductionCenteredRunFn = void (*)(const void* source, std::uint64_t sampleCount, double mean,
                                        ReductionRunAccumulator& accumulator) noexcept;

/// One row of a separable horizontal pass over already border-resolved samples.
///
/// `padded` holds `count + tapCount - 1` samples: the output at column `x` reads `padded[x + tap]`,
/// so the body has no edge case and no border rule of its own. The taps are applied in their own
/// order into a Float64 accumulator, which is what makes this comparable with the scalar body
/// rather than merely close to it.
using StencilRowRunFn = void (*)(const float* padded, std::uint64_t count, const double* taps, std::uint32_t tapCount,
                                 double* destination) noexcept;

/// One row of a separable vertical pass down an intermediate band.
///
/// `band` is the first contributing row at column zero and `rowStride` separates one band row from
/// the next, so the body reads `band[tap * rowStride + x]`. The band already holds the halo rows,
/// so this body has no border rule either. Results narrow once to Float32 on the way out.
using StencilColumnRunFn = void (*)(const double* band, std::uint64_t rowStride, std::uint64_t count,
                                    const double* taps, std::uint32_t tapCount, float* destination) noexcept;

/// One row of a symmetric Float32 horizontal Gaussian pass.
using SymmetricGaussianHorizontalRunFn = void (*)(const float* source, float* destination, std::uint32_t width,
                                                  const float* taps, std::uint32_t radius) noexcept;

/// One row of a symmetric Float32 vertical Gaussian pass over a run of a plane's columns: `count` samples from the
/// run's first column, the plane's rows `rowStride` samples apart.
using SymmetricGaussianVerticalRunFn = void (*)(const float* source, float* destination, std::uint32_t count,
                                                std::uint64_t rowStride, std::uint32_t height, std::uint32_t row,
                                                const float* taps, std::uint32_t radius) noexcept;

/// The lowest exponent the preview gamma table's vector run states a bound for.
inline constexpr double kPreviewGammaVectorLowestExponent = 0.125;

/// The highest exponent the preview gamma table's vector run states a bound for.
inline constexpr double kPreviewGammaVectorHighestExponent = 8.0;

/// How far the preview gamma table's vector curve may stand from the exact power, relative to it, for an exponent
/// between `kPreviewGammaVectorLowestExponent` and `kPreviewGammaVectorHighestExponent`: `(1 + 16 e) 2^-20`, four
/// times what evaluating the quotient, the logarithm, the product and the exponential in Float32 can lose with the
/// vector library's stated four units in the last place for the logarithm and one for the exponential.
///
/// @param exponent the power.
/// @return the relative slack.
[[nodiscard]] constexpr double previewGammaCurveSlack(double exponent) noexcept
{
    return (1.0 + 16.0 * exponent) / 1048576.0;
}

/// The real-time preview's gamma table over `count` entries, through a vector power.
///
/// For each entry `i` from one to `count - 1`, `curve[i]` is `(i / codeMaximum)^exponent` within
/// `previewGammaCurveSlack(exponent)` of the exact power, for an exponent in the stated range. `entries[i]` is
/// `floor(curve * codeMaximum) / codeMaximum` where that slack either side of `curve * codeMaximum` cannot change its
/// floor, so it is the entry the exact power gives; elsewhere it is a NaN, which the caller decides with the exact
/// power. `errors[i]` is `|entries[i - 1] - curve[i]|` from these values, a NaN below an undecided entry. Entry zero
/// of `curve` and `errors` is left alone, and the caller writes `entries[0]` before the call.
///
/// @return the largest of `errors[1]` to `errors[count - 1]` that is not a NaN.
using PreviewGammaTableRunFn = double (*)(double exponent, double codeMaximum, std::uint32_t count, double* curve,
                                          double* entries, double* errors) noexcept;

/// One interior run of the four-direction edge tightening over Float32 samples every rule admits.
///
/// `above`, `current` and `below` are the run's first column in three consecutive rows, each
/// readable one sample before it and one after its last; output `x` of the run reads the three-by-three
/// neighbourhood centred on `current[x]` and goes to `destination[x]`. `kernels` are the four
/// row-major directional kernels. The run writes the bits the scalar body writes: each response the
/// Float64 sum of all nine taps in order, a zero coefficient's term included, narrowed to Float32;
/// the magnitudes summed in Float32 in the kernels' order; then a twentieth of them scaled by
/// `amount`, subtracted from the centre, `offset` added and the result held to `[lowest, highest]`,
/// in Float64.
using EdgeTightenRowRunFn = void (*)(const float* above, const float* current, const float* below, std::uint64_t count,
                                     const std::array<double, 9>* kernels, double amount, double offset, double lowest,
                                     double highest, float* destination) noexcept;

/// The pointwise run selected by one execution's immutable CPU table.
[[nodiscard]] PointwiseRunFn pointwiseRunFor(const ExecutionContext& execution,
                                             const PointwiseProgram& program) noexcept;

/// The one-step Float32 sampled curve run selected by one execution's immutable CPU table, or null where the table
/// has none and the general executor runs the step.
[[nodiscard]] SampledCurveFloat32RunFn sampledCurveFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The normalized Float32 lower-neighbour lookup selected by one execution's immutable CPU table.
[[nodiscard]] NormalizedLowerCurveFloat32RunFn
normalizedLowerCurveFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The normalized Float32 range-selection run selected by one execution's immutable CPU table.
[[nodiscard]] RangeSelectionFloat32RunFn rangeSelectionFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The first-pass reduction run selected by one execution's immutable CPU table.
[[nodiscard]] ReductionFirstRunFn reductionFirstRunFor(const ExecutionContext& execution) noexcept;

/// The centered reduction run selected by one execution's immutable CPU table.
[[nodiscard]] ReductionCenteredRunFn reductionCenteredRunFor(const ExecutionContext& execution) noexcept;

/// The separable horizontal run selected by one execution's immutable CPU table.
[[nodiscard]] StencilRowRunFn stencilRowRunFor(const ExecutionContext& execution) noexcept;

/// The separable vertical run selected by one execution's immutable CPU table.
[[nodiscard]] StencilColumnRunFn stencilColumnRunFor(const ExecutionContext& execution) noexcept;

/// The symmetric horizontal Gaussian row selected by one execution's immutable CPU table.
[[nodiscard]] SymmetricGaussianHorizontalRunFn
symmetricGaussianHorizontalRunFor(const ExecutionContext& execution) noexcept;

/// The symmetric vertical Gaussian row selected by one execution's immutable CPU table.
[[nodiscard]] SymmetricGaussianVerticalRunFn
symmetricGaussianVerticalRunFor(const ExecutionContext& execution) noexcept;

/// The edge tightening's interior run selected by one execution's immutable CPU table, or null where
/// the table has none and the scalar body runs instead.
[[nodiscard]] EdgeTightenRowRunFn edgeTightenRowRunFor(const ExecutionContext& execution) noexcept;

/// The preview gamma table's vector run of the best table this host runs and this build holds, or null where
/// that table has none. A plan builder has no execution to ask; what the run produces is decided against the
/// exact power before it is used, so no plan depends on which instruction set built it.
[[nodiscard]] PreviewGammaTableRunFn previewGammaTableRunForHost() noexcept;

/// One run of one transcendental over Float32 samples.
///
/// The exponent is narrowed to Float32 because the lanes are: a compiled body that took a Float64
/// exponent would have to widen every lane to use it, which is a different kernel from the one the
/// accuracy tier was measured for. The narrowing is one of the three terms the composed accuracy
/// bound in `Algorithms/Kernel/MathKernel.h` is derived from.
using MathRunFn = void (*)(const float* source, float* destination, std::uint64_t count, float exponent) noexcept;

/// The compiled transcendental run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @param function which function is wanted.
/// @return the compiled run, or null when the selected table has none - which is the scalar table
///         always, and any table for an unknown function. A null answer means the scalar reference
///         evaluates, and is reported as `MathProvider::ScalarLibrary` rather than hidden.
[[nodiscard]] MathRunFn mathRunFor(const ExecutionContext& execution, MathFunction function) noexcept;

/// One run of interior bilateral outputs: every window tap of `count` adjacent pixels of one row.
///
/// `centre[x]` is a pixel and `centre[x + tapOffsets[t]]` its neighbour under tap `t`, every
/// neighbour inside the plane and meaning something. A tap's weight is
/// `tapWeights[t] * exp(d * d * rangeScale)` for the Float32 difference `d`, evaluated through
/// this table's exponential; the output is the Float64 sum of weight times neighbour over the taps
/// in order, divided by the Float64 sum of the weights and narrowed once - the arithmetic of the
/// scalar loop, so the two differ only by the exponential's tier. The sums live in registers, so
/// the run touches memory once per neighbour and once per output.
using BilateralRowRunFn = void (*)(const float* centre, std::uint64_t count, const std::int64_t* tapOffsets,
                                   const float* tapWeights, std::uint32_t tapCount, float rangeScale,
                                   float* output) noexcept;

/// The compiled bilateral row run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none, which is the scalar table
///         always. A null answer means the interior is evaluated one tap at a time through the
///         platform's exponential.
[[nodiscard]] BilateralRowRunFn bilateralRowRunFor(const ExecutionContext& execution) noexcept;

/// One row of the plain chromatic green suppression under one of the two neutral ceilings.
///
/// Green is held to half the sum of red and blue when `averageNeutral` is set and to the larger
/// of the two otherwise; every channel is then clamped to `[0, whiteLevel]` and stored. Each
/// selection keeps the ordering of the scalar row - `ceiling < green ? ceiling : green`, with the
/// same answer for a not-a-number - so the compiled and scalar rows store identical bits. The
/// return value counts the stored samples that differ from their inputs.
using ChromaticCeilingRunFn = std::uint64_t (*)(const float* red, const float* green, const float* blue, float* outRed,
                                                float* outGreen, float* outBlue, std::uint64_t count, float whiteLevel,
                                                bool averageNeutral) noexcept;

/// The compiled chromatic ceiling row selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none, which is the scalar table
///         always. A null answer means the row is evaluated one pixel at a time.
[[nodiscard]] ChromaticCeilingRunFn chromaticCeilingRunFor(const ExecutionContext& execution) noexcept;

/// The authoritative scalar evaluation of one run, one sample at a time.
///
/// @param function which function to evaluate.
/// @param source where the samples start.
/// @param destination where the results go, which may be the source.
/// @param count how many samples.
/// @param exponent the Float64 exponent, for the power and ignored otherwise.
void referenceMathRun(MathFunction function, const float* source, float* destination, std::uint64_t count,
                      double exponent) noexcept;

/// One run of two packed validity masks combined into a third, byte by byte.
///
/// Bits rather than samples, so there is one shape rather than one per representation. The three
/// pointers cover the same byte count and the destination may be either source exactly.
using MaskCombineRunFn = void (*)(const std::uint8_t* left, const std::uint8_t* right, std::uint8_t* destination,
                                  std::uint64_t byteCount) noexcept;

/// One run of a packed validity mask inverted into another.
///
/// The bits past the last pixel of a row are not this body's problem: it inverts whole bytes, and the
/// operation above it re-clears each row's final byte, which is one masked write per row rather than
/// a branch per byte.
using MaskInvertRunFn = void (*)(const std::uint8_t* source, std::uint8_t* destination,
                                 std::uint64_t byteCount) noexcept;

/// One run of a packed validity mask's set bits counted.
using MaskPopcountRunFn = std::uint64_t (*)(const std::uint8_t* bits, std::uint64_t byteCount) noexcept;

/// The mask-intersection run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table
///         always. A null answer means the operation walks the bytes itself.
[[nodiscard]] MaskCombineRunFn maskIntersectRunFor(const ExecutionContext& execution) noexcept;

/// The mask-union run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none.
[[nodiscard]] MaskCombineRunFn maskUnionRunFor(const ExecutionContext& execution) noexcept;

/// The mask-inversion run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none.
[[nodiscard]] MaskInvertRunFn maskInvertRunFor(const ExecutionContext& execution) noexcept;

/// The mask-population-count run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none.
[[nodiscard]] MaskPopcountRunFn maskPopcountRunFor(const ExecutionContext& execution) noexcept;

/// The most parameters any point-spread-function fit this engine runs has.
///
/// Eight: background, amplitude, both centres, the major scale, the roundness angle, the position
/// angle and, for a free-exponent Moffat, the exponent angle.
inline constexpr std::size_t kPsfMaxFitParameters = 8u;

/// The samples one point-spread-function fit iterates over, already compacted.
///
/// Structure of arrays rather than a box, because the saturated samples have been removed: what is
/// left is a run whose coordinates no longer follow from its index. `x` and `y` hold each sample's
/// pixel-centre coordinate inside the box.
struct PsfSampleRun
{
    /// Sample values, `count` of them.
    const double* values = nullptr;

    /// Horizontal coordinates, one per value.
    const double* x = nullptr;

    /// Vertical coordinates, one per value.
    const double* y = nullptr;

    /// How many samples the run holds.
    std::uint64_t count = 0u;
};

/// One point-spread function's parameters, already derived from the solver's parameter vector.
///
/// The derivation - the absolute value of the scale, the cosine parameterisation of the roundness
/// and of the exponent, the reciprocals - happens once per model evaluation rather than once per
/// sample, and it is arithmetic the kernel must not repeat. So the kernel takes what it needs and
/// nothing it would have to derive.
struct PsfModelTerms
{
    /// The sky level parameter.
    double background = 0.0;

    /// The peak parameter.
    double amplitude = 0.0;

    /// The horizontal centre, in box coordinates.
    double centreX = 0.0;

    /// The vertical centre, in box coordinates.
    double centreY = 0.0;

    /// One over the major-axis squared scale.
    double inverseScaleX = 0.0;

    /// One over the minor-axis squared scale.
    double inverseScaleY = 0.0;

    /// One over the axis ratio.
    double inverseRatio = 0.0;

    /// The sine of the roundness angle, which the roundness derivative needs.
    double ratioSine = 0.0;

    /// The cosine of the position angle.
    double angleCosine = 1.0;

    /// The sine of the position angle.
    double angleSine = 0.0;

    /// The Moffat exponent. Meaningless for a Gaussian.
    double beta = 0.0;

    /// Half the exponent ceiling times the sine of the exponent angle.
    ///
    /// The exponent derivative's whole constant factor, folded once by the caller rather than
    /// rebuilt from an engine constant the backend layer cannot see. Zero unless the exponent is
    /// free.
    double betaDerivativeScale = 0.0;

    /// How many parameters the fit is moving: seven, or eight for a free exponent.
    std::uint32_t parameterCount = 7u;

    /// Whether the shape is a Moffat rather than a Gaussian.
    bool moffat = false;
};

/// The residual sum, the normal-equation matrix and the gradient of one model evaluation.
///
/// Only the upper triangle of `matrix` is written, and only the first `parameterCount` entries of
/// each. The Jacobian itself is never materialised: one pass over the run accumulates all three.
struct PsfNormalEquations
{
    /// Sum over the run of the squared residual.
    double residualSumOfSquares = 0.0;

    /// The upper triangle of the transposed Jacobian times the Jacobian.
    double matrix[kPsfMaxFitParameters][kPsfMaxFitParameters] = {};

    /// The transposed Jacobian times the residual.
    double gradient[kPsfMaxFitParameters] = {};
};

/// One trial step's residual sum of squares over a compacted sample run.
///
/// The compiled run and its scalar authority in `kernels/reference/PsfFitRow.inl` differ only by the
/// vector exponential the compiled body uses.
using PsfResidualRunFn = double (*)(const PsfSampleRun& samples, const PsfModelTerms& terms) noexcept;

/// One accepted step's residual sum, normal-equation matrix and gradient over the same run.
using PsfNormalEquationsRunFn = void (*)(const PsfSampleRun& samples, const PsfModelTerms& terms,
                                         PsfNormalEquations& out) noexcept;

/// The point-spread-function residual run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table
///         always. A null answer means the fit evaluates its own scalar authority.
[[nodiscard]] PsfResidualRunFn psfResidualRunFor(const ExecutionContext& execution) noexcept;

/// The point-spread-function normal-equation run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none.
[[nodiscard]] PsfNormalEquationsRunFn psfNormalEquationsRunFor(const ExecutionContext& execution) noexcept;

/// How one admitted stack sample changes its element's running term.
///
/// The private spelling of the public update enumeration, kept here so a compiled row can be
/// written without reaching the algorithms surface; the algorithm maps one onto the other.
enum class StackAccumulateRunUpdate : std::uint8_t
{
    /// The sample times its effective weight is added to the running sum.
    WeightedSum,

    /// The running term becomes the smaller of itself and the sample; the first sample replaces.
    Minimum,

    /// The running term becomes the larger of itself and the sample; the first sample replaces.
    Maximum
};

/// Everything one run of stack accumulation needs beside its samples and its terms.
///
/// `rule` is the band's value rule; `lowestValid` and `highestValid` are read under
/// `ValidityRule::DomainRange`; `maskBits` is the band row's packed bits, read under
/// `ValidityRule::ExplicitMask`, with `maskBitOffset` the bit of the run's first sample. When
/// `weighted` is set the effective weight of a sample is `frameWeight` times the local weight at
/// its index when `localWeights` is not null, and a sample contributes only when that product is
/// positive and finite; when it is clear every admitted sample contributes with a weight of one.
struct StackAccumulateRunParameters
{
    /// How an admitted sample changes its element's running term in `sums`.
    StackAccumulateRunUpdate update = StackAccumulateRunUpdate::WeightedSum;

    /// The band's value rule, deciding which samples are admitted.
    ValidityRule rule = ValidityRule::AllValid;

    /// The smallest admitted value under `ValidityRule::DomainRange`, inclusive.
    double lowestValid = 0.0;

    /// The largest admitted value under `ValidityRule::DomainRange`, inclusive.
    double highestValid = 0.0;

    /// The band row's packed validity bits, read under `ValidityRule::ExplicitMask` only.
    const std::uint8_t* maskBits = nullptr;

    /// The bit of `maskBits` that belongs to the run's first sample.
    std::uint64_t maskBitOffset = 0u;

    /// The frame's weight, the first factor of every sample's effective weight.
    double frameWeight = 1.0;

    /// One local weight per sample of the run, multiplied into `frameWeight`, or null for none.
    const float* localWeights = nullptr;

    /// Whether samples carry an effective weight; when clear every admitted sample weighs one.
    bool weighted = false;
};

/// The running terms one run writes, each already offset to the run's first element.
///
/// A null term is one the stack does not keep; `sums` and `counts` are never null. The variance
/// moments hold two values per element, the running mean first.
struct StackAccumulatorRunTargets
{
    /// Each element's running term: the weighted sum, or the minimum or maximum so far.
    double* sums = nullptr;

    /// How many samples each element has taken in.
    std::uint32_t* counts = nullptr;

    /// Each element's sum of effective weights, or null.
    double* weightSums = nullptr;

    /// Each element's sum of squared effective weights, or null.
    double* weightSquareSums = nullptr;

    /// Each element's unweighted running mean and sum of squared deviations, updated by Welford's
    /// method, or null.
    double* varianceMoments = nullptr;
};

/// Counts returned by one run of stack accumulation.
///
/// `finite` is cleared when an admitted sample was not finite or a written term left the finite
/// range, which the operation above reports as a refusal.
struct StackAccumulateRunOutcome
{
    /// Samples admitted by the rule and by their weight, and taken into the terms.
    std::uint64_t accumulated = 0u;

    /// Samples the rule rejected.
    std::uint64_t excludedByValidity = 0u;

    /// Admitted samples whose effective weight was not positive and finite.
    std::uint64_t excludedByWeight = 0u;

    /// Whether every admitted sample and every term written was finite.
    bool finite = true;
};

/// One contiguous run of Float32 stack samples accumulated into Float64 terms.
using StackAccumulateFloat32RunFn = StackAccumulateRunOutcome (*)(const float* samples, std::uint64_t count,
                                                                  const StackAccumulateRunParameters& parameters,
                                                                  const StackAccumulatorRunTargets& targets) noexcept;

/// One contiguous run of Float64 stack samples accumulated into Float64 terms.
using StackAccumulateFloat64RunFn = StackAccumulateRunOutcome (*)(const double* samples, std::uint64_t count,
                                                                  const StackAccumulateRunParameters& parameters,
                                                                  const StackAccumulatorRunTargets& targets) noexcept;

/// The Float32 stack-accumulation run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table
///         always. A null answer means the algorithm runs its own scalar authority.
[[nodiscard]] StackAccumulateFloat32RunFn stackAccumulateFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The Float64 stack-accumulation run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none.
[[nodiscard]] StackAccumulateFloat64RunFn stackAccumulateFloat64RunFor(const ExecutionContext& execution) noexcept;

/// One row of every band of a tile accumulated into the row's terms, every band in turn at every
/// element - the order a run per band gives, so the terms are that run's bits.
///
/// `rows` holds one planar row per band, in band order, each of `count` samples. `parameters` apply
/// to every band. `admittedPerBand` receives, added to what it holds, each band's samples taken into
/// the terms. A compiled row assumes nothing beyond the scalar authority's contract: a run it cannot
/// take a vector at a time - a weighted, masked or extremum run, or one that keeps more than sums and
/// counts - goes through the scalar authority it was compiled with.
///
/// @tparam SampleT the bands' stored or tile representation.
template <typename SampleT>
using StackAccumulateBandsRunFn = StackAccumulateRunOutcome (*)(const SampleT* const* rows, std::uint32_t bands,
                                                                std::uint64_t count,
                                                                const StackAccumulateRunParameters& parameters,
                                                                const StackAccumulatorRunTargets& targets,
                                                                std::uint64_t* admittedPerBand) noexcept;

/// The multi-band rows of one compiled table, one per representation a band can be accumulated at.
struct StackAccumulateBandsRuns
{
    StackAccumulateBandsRunFn<float> float32 = nullptr; ///< Float32 tile bands.
    StackAccumulateBandsRunFn<double> float64 = nullptr; ///< Float64 tile bands.
    StackAccumulateBandsRunFn<std::uint16_t> uint16 = nullptr; ///< Stored UInt16 codes.
    StackAccumulateBandsRunFn<std::int16_t> int16 = nullptr; ///< Stored Int16 codes.
    StackAccumulateBandsRunFn<std::uint8_t> uint8 = nullptr; ///< Stored UInt8 codes.
    StackAccumulateBandsRunFn<std::int8_t> int8 = nullptr; ///< Stored Int8 codes.
};

/// The multi-band rows selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the rows, or null when the selected table has none - the scalar table always - and the
///         algorithm runs the scalar authority.
[[nodiscard]] const StackAccumulateBandsRuns* stackAccumulateBandsRunsFor(const ExecutionContext& execution) noexcept;

/// Which sample-history combination a column run computes.
///
/// The private spelling of the public combination enumeration, kept here so a compiled row can be
/// written without reaching the algorithms surface; the algorithm maps one onto the other.
enum class StackColumnRunCombination : std::uint8_t
{
    /// The exact median of the admitted samples.
    Median,

    /// The auto-adaptive weighted mean of the admitted samples, scaled and iterated as the run
    /// parameters state.
    AutoAdaptiveWeightedAverage
};

/// One frame of a sample history as a column run reads it.
///
/// Where the frame's band begins and how its planes, rows and components are strided, so a run
/// finds the row of any channel by arithmetic and not through a layout object; and the frame's own
/// validity rule with the packed mask it reads under `ValidityRule::ExplicitMask`, strided the same
/// way, with `maskPlanes` one for a plane shared by every channel, the channel count for one per
/// channel, and zero for no mask.
struct StackColumnFrame
{
    /// The first byte of the frame's band: plane zero, row zero.
    const std::byte* samples = nullptr;

    /// Bytes from one plane of the band to the next.
    std::uint64_t planeStrideBytes = 0u;

    /// Bytes from one row of the band to the next.
    std::uint64_t rowStrideBytes = 0u;

    /// Components per pixel of a plane. Channel `c` lives in plane `c / samplesPerPixel` at
    /// component `c % samplesPerPixel`, and this is also the distance, in samples, from one pixel
    /// to the next.
    std::uint32_t samplesPerPixel = 1u;

    /// Bytes of one stored sample, which places a component within its pixel.
    std::uint32_t elementBytes = 4u;

    /// The frame's value rule, deciding which of its samples are admitted.
    ValidityRule rule = ValidityRule::AllValid;

    /// The smallest admitted value under `ValidityRule::DomainRange`, inclusive.
    double lowestValid = 0.0;

    /// The largest admitted value under `ValidityRule::DomainRange`, inclusive.
    double highestValid = 0.0;

    /// The first byte of the frame's packed mask over the band, or null for no mask.
    const std::uint8_t* maskBits = nullptr;

    /// Bytes from one bit-plane of the mask to the next.
    std::uint64_t maskPlaneStrideBytes = 0u;

    /// Bytes from one mask row to the next.
    std::uint64_t maskRowStrideBytes = 0u;

    /// How many bit-planes the mask has: one shared by every channel, one per channel, or zero.
    std::uint32_t maskPlanes = 0u;
};

/// Everything one column run needs beside its frames, its scratch and its targets.
///
/// A run is one row of one channel: `channel` and `y` name it within every frame's band. The
/// auto-adaptive parameters - the scale the samples are multiplied by, the iteration count, the
/// dispersion below which the plain mean is taken and the fewest samples that are re-weighted -
/// are read by that combination alone.
struct StackColumnRunParameters
{
    /// Which combination each column becomes.
    StackColumnRunCombination combination = StackColumnRunCombination::Median;

    /// The sample history's frames, `frameCount` of them, borrowed for the call.
    const StackColumnFrame* frames = nullptr;

    /// How many frames `frames` holds, which is also the length of one scratch column.
    std::uint32_t frameCount = 0u;

    /// The channel combined, which selects each frame's plane and component.
    std::uint32_t channel = 0u;

    /// The row combined, within every frame's band.
    std::uint32_t y = 0u;

    /// What the auto-adaptive mean multiplies each sample by before it iterates, and divides its
    /// result by.
    double sampleScale = 1.0;

    /// How many re-weighting iterations the auto-adaptive mean runs.
    std::uint32_t iterations = 0u;

    /// The first iteration's dispersion at or below which the auto-adaptive mean keeps the plain
    /// mean.
    double smallSigma = 0.0;

    /// The fewest admitted samples the auto-adaptive mean re-weights; below it the plain mean is
    /// kept.
    std::uint32_t minimumSamples = 0u;

    /// Whether the output row holds Float64 samples rather than Float32.
    bool outputFloat64 = false;
};

/// The most columns a compiled column run judges at once, which is what its workspace holds.
inline constexpr std::uint32_t kStackColumnRunLanes = 8u;

/// One worker's scratch as a column run uses it.
///
/// `column` holds one column of `frameCount` doubles, which the scalar row gathers admitted samples
/// into; `workspace` holds `kStackColumnRunLanes` columns of `frameCount` doubles for the median's
/// selection and twice that for the auto-adaptive mean's samples and weights, which the compiled
/// row judges one pixel per lane and the scalar row uses for its weights.
struct StackColumnRunScratch
{
    /// One column of `frameCount` doubles, the scalar row's admitted samples.
    double* column = nullptr;

    /// The compiled row's lanes, and the scalar row's auto-adaptive weights.
    double* workspace = nullptr;
};

/// Where one column run writes: the row of one channel of each target, or null for a map the
/// stack does not write. The output row is typed by `StackColumnRunParameters::outputFloat64`; the
/// mask row is the packed bits of the row, zeroed by the caller, in which a run sets the bit of
/// every pixel that received a sample.
struct StackColumnRunTargets
{
    /// The combined row; every pixel of the run is written, zero where no sample was admitted.
    std::byte* output = nullptr;

    /// The row's packed coverage bits, or null.
    std::uint8_t* maskRow = nullptr;

    /// Each pixel's admitted sample count, or null.
    std::uint32_t* coverage = nullptr;

    /// Each pixel's unbiased sample variance of the admitted samples, zero for fewer than two, or
    /// null.
    float* variance = nullptr;

    /// Each pixel's signal-to-noise gain, written as its admitted sample count, or null.
    float* gain = nullptr;

    /// Each pixel's weight sum, written as its admitted sample count, or null.
    float* weightSum = nullptr;
};

/// Counts returned by one column run.
///
/// `finite` is cleared when an admitted sample was not finite or a combined value left the finite
/// range, which the operation above reports as a refusal.
struct StackColumnRunOutcome
{
    /// Samples the frames' rules admitted, over every pixel of the run.
    std::uint64_t admitted = 0u;

    /// Samples the frames' rules rejected.
    std::uint64_t excludedByValidity = 0u;

    /// Pixels that received at least one sample.
    std::uint64_t withSamples = 0u;

    /// Whether every admitted sample and every combined value was finite.
    bool finite = true;
};

/// One row of one channel of a sample history combined column by column, from Float32 bands.
///
/// `firstPixel` and `count` name the pixels of the row the run covers, so the scalar row can
/// finish a compiled row's tail.
using StackColumnFloat32RunFn = StackColumnRunOutcome (*)(std::uint64_t firstPixel, std::uint64_t count,
                                                          const StackColumnRunParameters& parameters,
                                                          const StackColumnRunScratch& scratch,
                                                          const StackColumnRunTargets& targets) noexcept;

/// One row of one channel of a sample history combined column by column, from Float64 bands.
using StackColumnFloat64RunFn = StackColumnRunOutcome (*)(std::uint64_t firstPixel, std::uint64_t count,
                                                          const StackColumnRunParameters& parameters,
                                                          const StackColumnRunScratch& scratch,
                                                          const StackColumnRunTargets& targets) noexcept;

/// The Float32 column run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table
///         always. A null answer means the algorithm runs its own scalar authority.
[[nodiscard]] StackColumnFloat32RunFn stackColumnFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The Float64 column run selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none.
[[nodiscard]] StackColumnFloat64RunFn stackColumnFloat64RunFor(const ExecutionContext& execution) noexcept;

/// The most frames a compiled rejection row reads.
inline constexpr std::uint32_t kStackRejectionRowFrames = 8u;

/// Which rejection estimator a compiled rejection row runs.
///
/// The private spelling of six public rejection families, kept here so a compiled row can be
/// written without reaching the algorithms surface; the algorithm maps one onto the other.
enum class StackRejectionRowFamily : std::uint8_t
{
    /// Iterated sigma clipping about the median, which removes a rejected sample.
    SigmaClipping,

    /// Iterated sigma clipping about the median, which replaces a rejected sample with the median.
    MedianSigmaReplacement,

    /// Sigma clipping with a winsorized deviation, which removes a rejected sample.
    WinsorizedSigmaClipping,

    /// Iterated clipping about a line fitted over the survivors' ranks, which removes a rejected
    /// sample.
    LinearFitClipping,

    /// One pass of clipping at stated fractions of the median, which removes a rejected sample.
    PercentileClipping,

    /// The generalized extreme Studentized deviate test, which removes a confirmed outlier.
    GeneralizedEsdTest
};

/// What a compiled rejection row needs beside its samples and targets, as the one-column estimator
/// uses it: the family; the two sigma thresholds rounded to Float32; the percentile's two median
/// fractions; for the linear fit, the rank statistics prepared over the stack's full frame count -
/// the mean rank, the reciprocal of the ranks' variance and the reciprocal of each count from one;
/// and for the Studentized deviate test, the critical value rounded to Float32 and the most
/// outliers, each for a column of every count from one, indexed by the count less one.
struct StackRejectionRowParameters
{
    /// The estimator that judges each column.
    StackRejectionRowFamily family = StackRejectionRowFamily::SigmaClipping;

    /// The low-side sigma threshold, rounded to Float32.
    float lowFactor = 0.0f;

    /// The high-side sigma threshold, rounded to Float32.
    float highFactor = 0.0f;

    /// The percentile family's low-side fraction of the median.
    double lowFraction = 0.0;

    /// The percentile family's high-side fraction of the median.
    double highFraction = 0.0;

    /// The linear fit's mean rank.
    float meanRank = 0.0f;

    /// The reciprocal of the linear fit's rank variance.
    float inverseRankVariance = 0.0f;

    /// The linear fit's reciprocal of each count.
    std::array<float, kStackRejectionRowFrames> reciprocals{};

    /// The Studentized deviate test's critical value for each count.
    std::array<float, kStackRejectionRowFrames> critical{};

    /// The most outliers the Studentized deviate test may confirm for each count.
    std::array<std::int32_t, kStackRejectionRowFrames> maximumOutliers{};
};

/// One row of one channel of a planar Float32 sample history: every frame's row, the one value
/// rule every frame states - all-valid, finite-only or exact zero as no data - and the width.
/// Only as many rows are read as the compiled row has frames.
struct StackRejectionRowSource
{
    /// Each frame's row of the channel, in frame order.
    std::array<const float*, kStackRejectionRowFrames> rows{};

    /// The value rule every frame states.
    ValidityRule rule = ValidityRule::AllValid;

    /// Pixels in each row.
    std::uint32_t width = 0u;
};

/// Where one compiled rejection row writes: the row of each target, or null for a map the stack
/// does not write. The output row is Float64 when `outputFloat64` holds and Float32 otherwise; the
/// mask row, when there is one, is zeroed by the caller; and `frameCounters`, when there is one,
/// holds the worker's admitted and rejected counts, two per frame, which the row lowers by what
/// it excluded and raises by what it rejected.
struct StackRejectionRowTargets
{
    /// The combined row: each column's survivor mean, or the median of its admitted samples when
    /// the judgement removed every one of them.
    std::byte* output = nullptr;

    /// Whether `output` holds Float64 samples rather than Float32.
    bool outputFloat64 = false;

    /// The row's packed coverage bits, or null.
    std::uint8_t* maskRow = nullptr;

    /// Each column's count of combined samples, or null.
    std::uint32_t* coverage = nullptr;

    /// Each column's signal-to-noise gain, written as its count of combined samples, or null.
    float* gain = nullptr;

    /// Each column's weight sum, written as its count of combined samples, or null.
    float* weightSum = nullptr;

    /// The low rejection map's row: each column's samples rejected on the low side, or null.
    std::uint32_t* low = nullptr;

    /// The high rejection map's row: each column's samples rejected on the high side, or null.
    std::uint32_t* high = nullptr;

    /// The merged rejection map's row: each column's low and high rejections together, or null.
    std::uint32_t* merged = nullptr;

    /// The worker's admitted and rejected sample counts, two per frame, or null.
    std::uint64_t* frameCounters = nullptr;
};

/// Why a compiled rejection row stopped.
enum class StackRejectionRowFailure : std::uint8_t
{
    /// It did not stop.
    None,

    /// An admitted sample is not finite.
    NonFiniteSample,

    /// A column's statistics left the finite range, or its winsorized deviation did not settle.
    NonFiniteStatistics,

    /// A combined value left the finite range.
    NonFiniteCombination
};

/// What one compiled rejection row counted, and whether it stopped. A row that stopped has written
/// an unspecified part of its targets and its counts are meaningless.
struct StackRejectionRowOutcome
{
    /// Samples the rule excluded before any column was judged.
    std::uint64_t excludedByValidity = 0u;

    /// Columns whose output took at least one sample.
    std::uint64_t withSamples = 0u;

    /// Samples the columns combined, which is the sum of their coverage.
    std::uint64_t samplesCombined = 0u;

    /// Low-side rejections the judgement made, counted as it made them.
    std::uint64_t rejectedLow = 0u;

    /// High-side rejections the judgement made, counted as it made them.
    std::uint64_t rejectedHigh = 0u;

    /// The row's total in the low rejection map.
    std::uint64_t mappedLow = 0u;

    /// The row's total in the high rejection map.
    std::uint64_t mappedHigh = 0u;

    /// Why the row stopped, or `StackRejectionRowFailure::None`.
    StackRejectionRowFailure failure = StackRejectionRowFailure::None;
};

/// Judges and combines one row, to the bits the one-column estimator and the single-pass
/// combination write for every column: each sample admitted by the rule, the family's judgement,
/// the unweighted Siril-compatible mean of the survivors, their count as the coverage, the gain
/// and the weight sum, and each column's first rejections per side. A column with no admitted
/// sample is written as the general pass writes it - zero, uncovered and unmasked.
using StackRejectionFloat32RowFn = StackRejectionRowOutcome (*)(const StackRejectionRowParameters& parameters,
                                                                const StackRejectionRowSource& source,
                                                                const StackRejectionRowTargets& targets) noexcept;

/// The Float32 rejection row for one frame count, selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @param frames the frames of the stack.
/// @return the compiled row, or null when the selected table has none for that many frames - which
///         is the scalar table always. A null answer means the algorithm runs its one-column
///         estimator.
[[nodiscard]] StackRejectionFloat32RowFn stackRejectionFloat32RowFor(const ExecutionContext& execution,
                                                                     std::uint32_t frames) noexcept;

/// The masters' rows one calibrating source row reads, each at the destination width and strided
/// like its own frame.
///
/// A null master is one the plan does not subtract or divide by. `flatFactors` holds the central
/// means the flat is divided by before it divides the light, one per site of this row - one entry
/// for a frame with no filter array, the block side's worth for a mosaic, indexed by the column
/// modulo `flatSites` - or null when the flat is not normalized.
struct StackSourceCalibrationRow
{
    /// The master bias row, in the tile representation, or null.
    const void* bias = nullptr;

    /// The master dark row, in the tile representation, or null.
    const void* dark = nullptr;

    /// The master flat row, in the tile representation, or null.
    const void* flat = nullptr;

    /// Samples from one pixel of the bias row to the next.
    std::uint32_t biasStride = 1u;

    /// Samples from one pixel of the dark row to the next.
    std::uint32_t darkStride = 1u;

    /// Samples from one pixel of the flat row to the next.
    std::uint32_t flatStride = 1u;

    /// Added to every sample before the bias is subtracted. Zero skips the step.
    double pedestal = 0.0;

    /// Multiplies each dark sample before it is subtracted. One skips the multiplication.
    double darkScale = 1.0;

    /// The central means the flat is divided by, `flatSites` of them, or null.
    const double* flatFactors = nullptr;

    /// How many entries `flatFactors` holds; column `x` reads entry `x % flatSites`.
    std::uint32_t flatSites = 1u;
};

/// Everything one row of one channel of the stack source path needs beside its samples.
///
/// `rule`, its bounds and its mask bits are the frame's own validity claim in the stored units;
/// `policy` is the conversion the stored samples are widened under; `calibration` is read when
/// `calibrate` is set; `local` is one prepared local-normalization row or null; `scale`, `offset`
/// and `multiplicative` are read when `normalizeGlobally` is set; and `zeroRule` says whether the
/// band the row writes states the zero rule, so a normalized sample that lands exactly on zero is
/// moved off it and counted.
struct StackSourceRunParameters
{
    /// Stored samples from one pixel of the row to the next.
    std::uint32_t sourceStride = 1u;

    /// Tile samples from one pixel of the row to the next.
    std::uint32_t destinationStride = 1u;

    /// The frame's value rule, deciding which stored samples are admitted.
    ValidityRule rule = ValidityRule::AllValid;

    /// The smallest admitted value under `ValidityRule::DomainRange`, inclusive, in stored units.
    double lowestValid = 0.0;

    /// The largest admitted value under `ValidityRule::DomainRange`, inclusive, in stored units.
    double highestValid = 0.0;

    /// The frame row's packed validity bits, read under `ValidityRule::ExplicitMask` only.
    const std::uint8_t* maskBits = nullptr;

    /// The bit of `maskBits` that belongs to the row's first sample.
    std::uint64_t maskBitOffset = 0u;

    /// The conversion a stored sample is widened to the tile representation under.
    ConversionPolicy policy{};

    /// Whether `calibration` is applied.
    bool calibrate = false;

    /// The masters' rows and the calibration scalars.
    StackSourceCalibrationRow calibration{};

    /// One prepared local-normalization row, or null for none.
    const LocalNormalizationRunParameters* local = nullptr;

    /// Whether the global match - `scale`, `offset` and `multiplicative` - is applied last.
    bool normalizeGlobally = false;

    /// The global match's multiplier.
    double scale = 1.0;

    /// The global match's offset, not read when `multiplicative` is set.
    double offset = 0.0;

    /// Whether the global match only scales.
    bool multiplicative = false;

    /// Whether the band written states `ValidityRule::ZeroIsNoData`.
    bool zeroRule = false;
};

/// Counts returned by one row of the stack source path.
///
/// `finite` is cleared when a sample the frame's rule admitted was not a finite number, which the
/// operation above reports as a refusal.
struct StackSourceRunOutcome
{
    /// Tile samples written, which is one per pixel of the row.
    std::uint64_t written = 0u;

    /// Samples the frame's rule excluded: written as a quiet NaN under calibration or a
    /// domain-range rule, and as their widened stored value otherwise.
    std::uint64_t preserved = 0u;

    /// Admitted samples carried through the stages, those a calibration left non-finite included.
    std::uint64_t transformed = 0u;

    /// Excluded samples written as a quiet NaN because the row calibrates.
    std::uint64_t invalidCalibrationInputs = 0u;

    /// Admitted samples a calibration left non-finite, which skip the later stages.
    std::uint64_t nonFiniteOutputs = 0u;

    /// Results the zero rule moved off exactly zero, in the local or the global stage.
    std::uint64_t noDataCollisions = 0u;

    /// Whether every admitted stored sample was finite.
    bool finite = true;
};

/// One row of one channel of stored samples read through the source path into the tile.
///
/// @tparam SourceT the stored representation.
/// @tparam DestT the tile representation: `double` for a Float64 source and `float` otherwise.
template <typename SourceT, typename DestT>
using StackSourceRunFn = StackSourceRunOutcome (*)(const SourceT* source, DestT* destination, std::uint64_t pixelCount,
                                                   const StackSourceRunParameters& parameters) noexcept;

/// The eight source rows compiled for one instruction set, one per stored representation.
struct StackSourceCpuRuns
{
    /// The row for UInt8 samples, read into a Float32 tile.
    StackSourceRunFn<std::uint8_t, float> uint8 = nullptr;

    /// The row for Int8 samples, read into a Float32 tile.
    StackSourceRunFn<std::int8_t, float> int8 = nullptr;

    /// The row for UInt16 samples, read into a Float32 tile.
    StackSourceRunFn<std::uint16_t, float> uint16 = nullptr;

    /// The row for Int16 samples, read into a Float32 tile.
    StackSourceRunFn<std::int16_t, float> int16 = nullptr;

    /// The row for UInt32 samples, read into a Float32 tile.
    StackSourceRunFn<std::uint32_t, float> uint32 = nullptr;

    /// The row for Int32 samples, read into a Float32 tile.
    StackSourceRunFn<std::int32_t, float> int32 = nullptr;

    /// The row for Float32 samples, read into a Float32 tile.
    StackSourceRunFn<float, float> float32 = nullptr;

    /// The row for Float64 samples, read into a Float64 tile.
    StackSourceRunFn<double, double> float64 = nullptr;
};

/// The source rows selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled rows, or null when the selected table has none - which is the scalar table
///         always. A null answer means the algorithm runs its own scalar authority.
[[nodiscard]] const StackSourceCpuRuns* stackSourceRunsFor(const ExecutionContext& execution) noexcept;

/// The curve argument above which the inverse asinh evaluates `sinh(t) / amount` as the
/// exponential it equals, `exp(|t| - log(2 amount))`.
///
/// Past this the two differ by `exp(-2 |t|)` relative, below half a Float64 rounding, and the
/// exponential form never overflows: the hyperbolic sine of an admitted amount's largest argument
/// would, although its quotient by the amount is below one.
inline constexpr double kAsinhInverseLargeArgument = 20.0;

/// The amount below which an asinh curve is the identity to within rounding.
///
/// The primitive `asinh(a t)` is `a t (1 - (a t)^2 / 6 + ...)` and a tangent slope
/// `a / sqrt(1 + (a d)^2)` is `a (1 - (a d)^2 / 2 + ...)`, with `t` and `d` at most one, so below this
/// amount the normalized curve differs from its input by less than `2^-118` relative - far below a
/// Float64 rounding - and a subnormal amount's reciprocal, which the inverse scales by, is not finite.
inline constexpr double kAsinhIdentityAmount = 0x1p-60;

/// One asinh stretch, planned once per request for either direction.
///
/// The curve is the primitive `P(x) = asinh(amount (x - symmetry))` between the protection points,
/// extended by its tangent lines outside them and normalized to `(P(x) - P(0)) / extent`. The
/// tangent slopes are formed as `amount / hypot(1, amount distance)`, which squares nothing and so
/// stays finite at every admitted amount; a protection point at the symmetry point has the amount
/// itself as its slope.
///
/// The forward is the lower tangent `x lowGradient` below the low protection point, the upper tangent
/// `1 - (1 - x) highGradient` from the high one, and between them `(P - origin) / extent`, with `P`
/// taken from the input's distance to the symmetry point and given that side's sign. The inverse is
/// one of three pieces, chosen by the normalized input `y`: the lower tangent line below `lowOutput`,
/// the upper one above `highOutput`, and between them `symmetry + sinh(P) / amount` at
/// `P = origin + y extent`, with `P` held to the curve's own range.
///
/// A plan applies only to an amount of at least `kAsinhIdentityAmount`; a smaller one is the identity
/// and never reaches a run.
struct AsinhCoefficients
{
    /// The amount, which scales the distance from the symmetry point inside the primitive.
    double amount = 1.0;

    /// The low protection point, below which the forward is the lower tangent.
    double lowProtection = 0.0;

    /// The high protection point, from which the forward is the upper tangent.
    double highProtection = 1.0;

    /// The lower tangent's slope in normalized terms: the forward below the low protection point is
    /// `x * lowGradient`.
    double lowGradient = 1.0;

    /// The upper tangent's slope in normalized terms: the forward from the high protection point is
    /// `1 - (1 - x) * highGradient`.
    double highGradient = 1.0;

    /// The primitive at zero, where the normalized curve starts.
    double origin = 0.0;

    /// The primitive's rise over the unit interval, which normalizes it.
    double extent = 1.0;

    /// The primitive at the low protection point, the curve piece's lower end.
    double lowPrimitive = 0.0;

    /// The primitive at the high protection point, the curve piece's upper end.
    double highPrimitive = 0.0;

    /// The symmetry point, where the primitive is zero.
    double symmetryPoint = 0.0;

    /// The normalized value of the low protection point, below which the lower tangent applies.
    double lowOutput = 0.0;

    /// The normalized value of the high protection point, above which the upper tangent applies.
    double highOutput = 1.0;

    /// The lower tangent's inverse slope in normalized terms: its input is `y * lowScale`.
    double lowScale = 1.0;

    /// The upper tangent's inverse slope in normalized terms: its input is `1 - (1 - y) * highScale`.
    double highScale = 1.0;

    /// The reciprocal of the amount, which scales `sinh(P)` back to an input distance.
    double inverseAmount = 1.0;

    /// `log(amount) + log(2)`, which the exponential form subtracts from `|P|`.
    double logTwiceAmount = 0.0;

    /// Whether any curve argument can exceed `kAsinhInverseLargeArgument`, so a run needs the
    /// exponential form at all.
    bool largeArguments = false;
};

/// One contiguous Float32 row through a planned asinh in one direction, evaluated in Float64.
///
/// Every sample is finite, as the operation above has already checked. Each is clamped to the unit
/// interval; zero and below write positive zero, one and above write one, and every other result is
/// clamped to the unit interval before it is narrowed. The arithmetic is the scalar authority's with
/// the transcendental functions - the forward's inverse hyperbolic sine, the inverse's hyperbolic
/// sine and exponential - taken from the vector library, within its tier of the platform's, so after
/// narrowing a sample can differ from the scalar row by one Float32 step. A partial final vector goes
/// through the same lanes, so what one sample becomes does not depend on where in the row it sits.
/// Exact aliasing of source and destination is supported.
using AsinhFloat32RunFn = void (*)(const float* source, float* destination, std::uint64_t count,
                                   const AsinhCoefficients& coefficients) noexcept;

/// The compiled forward asinh row selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled row, or null when the selected table has none - which is the scalar table
///         always. A null answer means the stretch runs its own scalar authority.
[[nodiscard]] AsinhFloat32RunFn asinhFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The compiled inverse asinh row selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled row, or null when the selected table has none - which is the scalar table
///         always. A null answer means the stretch runs its own scalar authority.
[[nodiscard]] AsinhFloat32RunFn asinhInverseFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The scaled exponent above which the generalized-hyperbolic inverse evaluates
/// `expm1(z) / rate` as the exponential it equals, `exp(z - log(rate))`.
///
/// Past this the two differ by `exp(-z)` relative, below half a Float64 rounding, and the
/// exponential form never overflows: `expm1` of the largest scaled exponent would, although its
/// quotient by the rate may be below one.
inline constexpr double kGeneralizedHyperbolicLargeExponent = 40.0;

/// The amount below which a generalized-hyperbolic curve is the identity to within rounding.
///
/// Every member's rise `G(t)` is `t - s a t^2 / 2` and smaller terms at amount `a`, with `s` at most
/// two, so below this amount the whole normalized curve differs from its input by less than
/// `2^-60` relative - below a Float64 rounding - and the curve's formulas, which divide by the
/// amount, would lose far more than that on a subnormal one.
inline constexpr double kGeneralizedHyperbolicIdentityAmount = 0x1p-60;

/// Which member of the generalized-hyperbolic family a request evaluates.
///
/// Each member is written through one exponent `E(t)` of the distance `t` from the symmetry point:
/// the curve's slope is `exp(-E)` or, above zero bias, `exp(-(1 + b) E)`, and its remaining rise is
/// `exp(-k E)` with `k` the member's saturation.
enum class GeneralizedHyperbolicRegime : std::uint8_t
{
    /// Bias zero: `E = a t` and the curve `(1 - exp(-E)) / a`. A bias whose magnitude is below
    /// `2^-80`, or whose product with the amount is below `2^-60`, is this member too: the power
    /// member it names differs from it by less than a Float64 rounding wherever either is not
    /// saturated, and its own formulas would divide by that bias.
    Exponential = 0u,

    /// Bias minus one: `E = log1p(a t)` and the curve `E / a`, which never saturates.
    Logarithmic = 1u,

    /// A bias below zero other than minus one: `E = log1p(|b| a t) / |b|` and the curve
    /// `(1 - exp(-(1 + b) E)) / ((1 + b) a)`, which saturates algebraically above minus one and,
    /// where `1 + b` is negative, grows without bound below it.
    IntegralPower = 2u,

    /// A bias above zero: `E = log1p(b a t) / b` and the curve `(1 - exp(-E)) / a`.
    HyperbolicPower = 3u
};

/// One generalized-hyperbolic stretch, planned once per request for either direction.
///
/// The curve `G(t)` rises from the symmetry point with unit slope either way, a tangent line
/// continues it past each protection point, and the whole is normalized over the unit interval.
/// Each piece is evaluated from its own end of the interval - `x * lowSlope / extent` on the lower
/// line, `(lowLine + (lowCurve - G(symmetry - x))) / extent` on the lower curve and the reflections
/// above - so the pieces meet exactly and no small rise is cancelled against the whole extent. The
/// extent is divided rather than multiplied by its reciprocal: at the largest amounts it is
/// subnormal, and its reciprocal is not finite.
///
/// The inverse finds where a curve piece has fallen by a given drop from its protection point's
/// value. Where the curve saturates, the value it reaches is its supremum less a rise too small to
/// survive a subtraction, so there the inverse solves the remaining rise `exp(-k E)` directly -
/// exactly when that is below one half, where it is the better conditioned form.
///
/// A plan applies only to an amount of at least `kGeneralizedHyperbolicIdentityAmount`; a smaller
/// one is the identity and never reaches an evaluation. Every reciprocal below is finite by that
/// bound and by the regime rules.
struct GeneralizedHyperbolicCoefficients
{
    /// Which member the bias selects.
    GeneralizedHyperbolicRegime regime = GeneralizedHyperbolicRegime::Exponential;

    /// The amount `a`.
    double amount = 1.0;

    /// `1 / a`.
    double inverseAmount = 1.0;

    /// What multiplies the distance inside the exponent's logarithm: `|b| a` for a power member,
    /// `a` otherwise.
    double rate = 1.0;

    /// `1 / rate`.
    double inverseRate = 1.0;

    /// `log(rate)`, which the inverse's exponential form subtracts from a large scaled exponent.
    double logRate = 0.0;

    /// Whether any scaled exponent of the curve can exceed `kGeneralizedHyperbolicLargeExponent`,
    /// so an inverse needs the exponential form at all. It is set from three quarters of that bound,
    /// which covers an exponent recovered from a remaining rise that underflowed to a subnormal.
    bool largeExponents = false;

    /// `|b|` for a power member and one otherwise, which divides that logarithm.
    double scale = 1.0;

    /// `1 / scale`.
    double inverseScale = 1.0;

    /// `k`: `1 + b` for the integral power - negative below a bias of minus one, where the curve does
    /// not saturate and its remaining rise `exp(-k E)` is above one - zero for the logarithm and one
    /// otherwise.
    double saturation = 1.0;

    /// `1 / k`, or zero for the logarithm.
    double inverseSaturation = 1.0;

    /// `k a`: what the remaining rise gains per unit the curve falls, zero for the logarithm.
    double saturationRate = 1.0;

    /// `1 / (k a)`, the saturating curve's divisor, or zero for the logarithm.
    double inverseSaturationRate = 1.0;

    /// The symmetry point.
    double symmetryPoint = 0.0;

    /// The low protection point.
    double lowProtection = 0.0;

    /// The high protection point.
    double highProtection = 1.0;

    /// The symmetry point less the low protection point: how far the lower curve piece reaches.
    double lowDistance = 0.0;

    /// The high protection point less the symmetry point.
    double highDistance = 1.0;

    /// `G(lowDistance)`, the lower curve piece's whole rise.
    double lowCurve = 0.0;

    /// `G(highDistance)`.
    double highCurve = 0.0;

    /// The curve's slope at the low protection point, which the lower line keeps.
    double lowSlope = 1.0;

    /// The curve's slope at the high protection point.
    double highSlope = 1.0;

    /// The remaining rise `exp(-k E)` at the low protection point; one for the logarithm.
    double lowTail = 1.0;

    /// The remaining rise at the high protection point.
    double highTail = 1.0;

    /// `lowProtection * lowSlope`, the lower line's rise.
    double lowLine = 0.0;

    /// `(1 - highProtection) * highSlope`.
    double highLine = 0.0;

    /// `lowLine + lowCurve`, the unnormalized value at the symmetry point.
    double lowRise = 0.0;

    /// Everything the curve rises over the unit interval, which normalizes it.
    double extent = 1.0;

    // The inverse measures a value's rise in units of a power of two that brings the extent into
    // [1, 2), and the values below are the rises above in those units. At the largest amounts the
    // extent is near the least normal Float64, and a small value's share of it underflows - to zero
    // for the least positive Float32 above an amount of about 1e278, which would then be answered
    // the low protection point instead of the symmetry point. The exchange multiplies or divides by
    // a power of two, which is exact, so wherever nothing underflowed the inverse computes what it
    // computed in the plain values; a plan whose exchange would not be exact keeps the plain values.

    /// `extent` in units.
    double unitExtent = 1.0;

    /// `lowLine` in units.
    double unitLowLine = 0.0;

    /// `highLine` in units.
    double unitHighLine = 0.0;

    /// `lowCurve` in units.
    double unitLowCurve = 0.0;

    /// `highCurve` in units.
    double unitHighCurve = 0.0;

    /// `lowRise` in units.
    double unitLowRise = 0.0;

    /// `saturationRate` per unit: what the remaining rise gains per unit the curve falls.
    double unitSaturationRate = 1.0;

    /// `amount` per unit: the logarithm's exponent per unit of rise.
    double unitAmount = 1.0;
};

/// One contiguous Float32 row through a planned generalized-hyperbolic curve, in one direction,
/// evaluated in Float64.
///
/// Every sample is finite, as the operation above has already checked. Each is clamped to the unit
/// interval; zero and below write positive zero, one and above write one, and every other result is
/// clamped to the unit interval before it is narrowed. The arithmetic is the scalar authority's with
/// the logarithms and exponentials taken from the vector library, within its tier of the
/// platform's, so after narrowing a sample can differ from the scalar row by one Float32 step - or,
/// for a result so near zero that a Float32 step is finer than 1e-12, by up to 1e-12: an inverse
/// distance recovered from an exponent in the hundreds carries one rounding of its logarithm. A
/// partial final vector goes through the same lanes, so what one sample becomes does not depend on
/// where in the row it sits. Exact aliasing of source and destination is supported.
using GeneralizedHyperbolicFloat32RunFn = void (*)(const float* source, float* destination, std::uint64_t count,
                                                   const GeneralizedHyperbolicCoefficients& coefficients) noexcept;

/// The compiled forward generalized-hyperbolic row selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled row, or null when the selected table has none - which is the scalar table
///         always. A null answer means the stretch runs its own scalar authority.
[[nodiscard]] GeneralizedHyperbolicFloat32RunFn
generalizedHyperbolicFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The compiled inverse generalized-hyperbolic row selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled row, or null when the selected table has none - which is the scalar table
///         always. A null answer means the stretch runs its own scalar authority.
[[nodiscard]] GeneralizedHyperbolicFloat32RunFn
generalizedHyperbolicInverseFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The power of two by which a Float32 midtones transfer row scales both distances.
///
/// Multiplying by it is exact. It is large enough that the distance of any Float32 sample above a
/// shadow point from it - at least `2^-202`, a Float64 step at the smallest shadow point beside a
/// Float32 sample - stays a normal Float32 value, and small enough that no distance of an input in
/// the unit interval overflows.
inline constexpr float kMidtonesTransferFloat32Scale = 0x1p100f;

/// The smallest balance, complement and endpoint range a Float32 midtones transfer row admits.
///
/// With all three at least this, one of the two scaled distances is at least `2^39`, so the
/// denominator is no smaller than about `2^-21` and never zero, and a rise small enough to lose its
/// precision below the normal range can only accompany a result that is itself zero in Float32.
inline constexpr double kMidtonesTransferFloat32Floor = 0x1p-60;

/// The Float32 form of one planned midtones transfer.
///
/// Both endpoints are held scaled by `kMidtonesTransferFloat32Scale` as two Float32 parts, the
/// endpoint rounded and its remainder rounded. The pair misses the endpoint by at most half a step
/// of the remainder, and the remainder is never larger than the distance of a Float32 sample on the
/// transfer's side of the endpoint, so each distance is off by at most `2^-24` of itself before the
/// row rounds it. The selection bounds are the Float32 values that select exactly the inputs the
/// Float64 bounds select.
struct MidtonesTransferFloat32Plan
{
    /// Whether the balance, its complement and the endpoint range are each at least
    /// `kMidtonesTransferFloat32Floor`; a row evaluates any other transfer in Float64.
    bool admitted = false;

    /// The midtones balance `m`, rounded to Float32.
    float midtones = 0.5f;

    /// `1 - m`, rounded to Float32.
    float complement = 0.5f;

    /// The scaled shadow point as two Float32 parts, the larger first.
    std::array<float, 2u> shadows{};

    /// The scaled highlight point as two Float32 parts, the larger first.
    std::array<float, 2u> highlights{};

    /// The largest Float32 value at or below `MidtonesTransferCoefficients::zeroThrough`, or
    /// infinity where that is infinite.
    float zeroThrough = 0.0f;

    /// The smallest Float32 value at or above the highlight point.
    float oneFrom = 1.0f;
};

/// One midtones transfer function, planned once per channel of a request.
///
/// An input at or below `shadows` maps to zero and one at or above `highlights` to one, the shadow
/// rule first, so equal endpoints are a step. Between them the curve is
/// `(1 - m)(v - s) / (m (h - v) + (1 - m)(v - s))`: the definition `(m - 1) x / ((2m - 1) x - m)` of
/// the rescaled input `x = (v - s) / (h - s)`, multiplied through by `h - s`. Both distances are
/// positive there, so nothing cancels, the numerator is part of the denominator, and the value is
/// in the unit interval - positive zero at a balance of one - without a clamp.
///
/// A compiled Float32 row evaluates the same fraction in Float32 when `float32` admits it, and in
/// Float64 otherwise; see `MidtonesTransferFloat32RunFn`.
struct MidtonesTransferCoefficients
{
    /// The midtones balance `m`.
    double midtones = 0.5;

    /// `1 - m`, which weighs the distance above the shadow point.
    double complement = 0.5;

    /// The shadow point `s`.
    double shadows = 0.0;

    /// The highlight point `h`.
    double highlights = 1.0;

    /// The largest input a row maps to zero without clamping it first: the shadow point, or
    /// infinity when the shadow point is one, since every clamped input is then at or below it.
    /// Everywhere else clamping an input to the unit interval never changes which endpoint rule
    /// applies, so a row that selects by this bound writes what the clamped evaluation writes.
    double zeroThrough = 0.0;

    /// The Float32 form of the same transfer.
    MidtonesTransferFloat32Plan float32{};
};

/// One contiguous Float32 row through a planned midtones transfer, which also checks the next row.
///
/// Every sample is finite, as the operation above has already checked. The row selects what the
/// scalar authority selects for the sample clamped to the unit interval - zero through `zeroThrough`
/// and one from the highlight point - without clamping, since a clamp changes no selection.
/// Between the endpoints a transfer `float32` admits is evaluated in Float32: the scaled distances
/// from the two-part endpoints, the rise, the denominator and one division, with contraction off.
/// Every step rounds once and both distances are positive, each found to within two roundings and
/// its endpoint's remainder, so a result `q` is within `(10 (1 - q) + 2.5)` Float32 steps of the
/// authority's narrowed Float64 result, and never more than twelve; that is under `2^-21` absolute,
/// inside the transfer's accepted tier of `1e-6`. The result lies in the unit interval, a zero is
/// positive zero, as the authority's are, and every instruction set writes the same bits. A
/// transfer `float32` does not admit is evaluated as the authority's operations in its order in
/// Float64, bit for bit.
///
/// While it writes, the row reads `upcoming` - the same plane of the next row - and answers whether
/// every sample of it is finite, so the caller can refuse that row before writing any of it without
/// reading it from memory on its own first. With `stream` the destination is written with streaming
/// stores from its first aligned vector on, which writes each line without reading it first and
/// leaves it out of the caches; the row ends with a store fence, so the samples are visible to
/// whoever synchronizes with the writing thread. Exact aliasing of source and destination is
/// supported, but not with `stream`.
using MidtonesTransferFloat32RunFn = bool (*)(const float* source, float* destination, std::uint64_t count,
                                              const MidtonesTransferCoefficients& coefficients, bool stream,
                                              const float* upcoming) noexcept;

/// The compiled midtones transfer row selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled row, or null when the selected table has none - which is the scalar table
///         always. A null answer means the stretch runs its own scalar authority.
[[nodiscard]] MidtonesTransferFloat32RunFn midtonesTransferFloat32RunFor(const ExecutionContext& execution) noexcept;

/// One row of a linear presentation of eight- or sixteen-bit codes: every `step`-th code of a run, `count` of them,
/// as the bytes `kernels/reference/PresentationRow.inl` presents them, which every instruction set writes alike.
/// `sourceSamples` is how many codes the run holds from `source` on, at least `(count - 1) * step + 1`; a compiled row
/// reads none past it.
using PresentationCodeRowRunFn = void (*)(const void* source, std::uint64_t sourceSamples, std::uint64_t step,
                                          std::uint8_t* destination, std::uint64_t count) noexcept;

/// The compiled presentation row of one code representation selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @param sampleType `SampleType::UInt8` or `SampleType::UInt16`.
/// @return the compiled row, or null when the selected table has none - which is the scalar table always - or for
///         any other representation. A null answer means the presentation runs its own scalar authority.
[[nodiscard]] PresentationCodeRowRunFn presentationCodeRowRunFor(const ExecutionContext& execution,
                                                                 SampleType sampleType) noexcept;

} // namespace an

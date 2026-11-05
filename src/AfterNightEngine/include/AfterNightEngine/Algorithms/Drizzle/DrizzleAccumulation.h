// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Checked, allocation-free drizzle accumulation: one borrowed source band scattered into one
/// caller-owned destination band of Float64 weighted sums and signed weight sums.
///
/// `DrizzleInputBand` borrows one frame's samples, its optional validity mask and its optional pixel
/// and flat weights for exactly the core rows of a `DrizzlePixmapView`. `accumulateDrizzleBand` reads
/// every source sample through the model's validity rule, forms its effective weight, and adds
/// `sample / (scale * scale)` times the kernel coefficient and that weight into the
/// `DrizzleAccumulatorBand` terms. The terms stay in the source representation's units and survive
/// successive bands and frames; dividing them by the signed total weight, mapping a normalized
/// integer full scale and regenerating validity belong to the executor's finalization, not to this
/// primitive. `DrizzleKernelTable` is the lookup the two Lanczos kernels read, and
/// `drizzleSquareOverlap` is the square kernel's overlap as a public query.
///
/// **Every destination sees one addition order.** A destination element receives frames, source
/// rows and source columns in increasing order whatever the worker count or the tiling, with no
/// floating atomics and no per-worker partial sums, so one implementation gives the same answer at
/// any width. The serial `Reference` and the optimized `OwnedRows` keep that order but can differ in
/// the last bits, because they integrate square overlaps and reuse separable coefficients differently.
///
/// **The terms are Float64 and the flux is divided by the scale squared**, as the published drizzle
/// method states: for a constant source of one at scale two the covered weighted mean is 0.25.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePixmap.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

namespace an
{

/// Immutable, caller-owned Lanczos table. The whole object - its 512 Float32 entries and its kernel
/// order - is what the plan's `kernelTableBytes` counts; no heap storage or context is acquired.
/// Copies may be read concurrently.
class DrizzleKernelTable
{
public:
    /// Builds the table: the window `sinc(x) * sinc(x / order)` evaluated in Float32 at
    /// every 0.01 of a drop width from 0 to 5.11, zero at and beyond the order, signed lobes retained.
    /// @param kernel Lanczos2 or Lanczos3.
    /// @return the table, or `ErrorCode::InvalidArgument` for any other kernel.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DrizzleKernelTable> create(DrizzleKernel kernel);
    /// Reads a signed coefficient from the nearest entry. A distance whose magnitude reaches 5.11
    /// returns zero before any index is formed; the entries past the window's order are zero already.
    /// @param distance finite signed distance, in drop widths of `pixelFraction * scale` output pixels.
    /// @return the coefficient, or `ErrorCode::InvalidArgument` for a non-finite distance.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<float> lookup(float distance) const;
    /// Which window these entries describe.
    /// @return Lanczos2 or Lanczos3.
    [[nodiscard]] DrizzleKernel kernel() const noexcept
    {
        return m_kernel;
    }
    /// Borrows the immutable coefficients, indexed by rounded absolute distance divided by 0.01.
    /// @return exactly 512 entries, valid through this object's lifetime and unchanged until assignment.
    [[nodiscard]] std::span<const float, 512u> values() const noexcept
    {
        return m_values;
    }

private:
    /// Initializes storage before the factory fills it.
    /// @param kernel the validated window.
    explicit DrizzleKernelTable(DrizzleKernel kernel) noexcept : m_kernel(kernel) {}
    DrizzleKernel m_kernel; ///< The window's order.
    std::array<float, 512u> m_values{}; ///< Coefficients at distances 0, 0.01, ..., 5.11.
};

/// Clips a finite convex drop to the unit square centred at an integer output coordinate.
/// Uses double precision relative to the pixel centre; orientation is immaterial. Allocation-free.
/// @param footprint four consecutive clockwise or counterclockwise corners, in output pixels.
/// @param x output column.
/// @param y output row.
/// @return the overlap area in [0,1] square output pixels, not divided by the drop's own area; or
///         `ErrorCode::InvalidData` for a non-finite, degenerate or non-convex drop.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> drizzleSquareOverlap(const DrizzleFootprint& footprint,
                                                                       std::uint32_t x, std::uint32_t y);

/// A full-width resident source band. Geometry may be planar, interleaved, mixed or padded.
/// The bytes remain alive and immutable during accumulation. Values are widened without rescaling.
struct DrizzleSampleBand
{
    ImageLayout layout; ///< Band geometry; its height is the number of stored source rows.
    std::span<const std::byte> samples; ///< At least layout.totalBytes() readable bytes, naturally aligned.
    SampleType sampleType = SampleType::Float32; ///< Must match the model's corresponding description.
};

/// One frame's borrowed samples and optional weights, for exactly the map's core rows.
struct DrizzleInputBand
{
    DrizzleSampleBand pixels; ///< Source samples in their stated numeric domain.
    std::uint32_t firstRow = 0u; ///< Global source row of the band's first row; CFA phase uses this offset.
    std::uint32_t frameIndex = 0u; ///< Zero-based index below the model's frame count.
    /// Positive finite per-output-channel factors; those past the output's channel count must be one.
    std::array<double, 4u> frameWeights{1.0, 1.0, 1.0, 1.0};
    /// The band's packed mask, present exactly when the model's validity rule needs one, with the
    /// sample band's extent.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;
    /// Non-negative finite factors, present exactly when the model describes pixel weights.
    const DrizzleSampleBand* pixelWeights = nullptr;
    /// Strictly positive finite factors, present exactly when the model describes flat weights.
    const DrizzleSampleBand* flatWeights = nullptr;
};

/// Caller-owned packed planar accumulator rows. Every span is disjoint from every other writable
/// span and all inputs. Initialize all terms to zero once per destination band, then retain them.
/// Calls must visit frames and source bands in increasing order; only the executor owns that sequence.
/// This primitive is reentrant on disjoint destination buffers; simultaneous writes to one band are forbidden.
struct DrizzleAccumulatorBand
{
    std::uint32_t firstRow = 0u; ///< Global destination row corresponding to element zero.
    std::uint32_t rowCount = 0u; ///< Positive number of full-width rows, inside the output canvas.
    std::span<double> sums{}; ///< Float64 weighted flux sums, one entry per destination band sample.
    std::span<double> weightSums{}; ///< Float64 signed weights, with the same element count.
    /// Distinct contributing frames, present exactly when the model requests a coverage map.
    std::span<std::uint32_t> coverage{};
    /// Last contributing frame index plus one, zero for untouched; present exactly with `coverage`.
    std::span<std::uint32_t> lastFrame{};
};

/// The implementation actually executed; neither implementation claims an explicit SIMD instruction set.
enum class DrizzleAccumulationImplementation : std::uint8_t
{
    Reference, ///< Serial source-order scatter, polygon clipping and direct coefficient evaluation.
    OwnedRows ///< Disjoint destination strips, bounded source traversal and separable coefficient reuse.
};

/// Counts from a successful source-band call; additions do not depend on destination worker count.
struct DrizzleAccumulationReport
{
    std::uint64_t selected = 0u; ///< Finite source samples accepted by validity and positive effective weight.
    std::uint64_t excluded = 0u; ///< Other source samples, counted once regardless of output tiling.
    std::uint64_t nonFinite = 0u; ///< Validity-admitted non-finite samples explicitly excluded by policy.
    std::uint64_t contributions = 0u; ///< Nonzero weighted sample-to-destination updates in this band.
    std::uint64_t covered = 0u; ///< New distinct-frame coverage entries added in this band.
    /// Planned scheduler workers, one for the serial reference, or zero when the band reaches no
    /// destination row.
    std::uint32_t workers = 0u;
    DrizzleAccumulationImplementation implementation = DrizzleAccumulationImplementation::Reference; ///< Actual code.
};

/// Accumulates one source band into one destination band without allocating image or scratch storage.
///
/// Source values, weights, spans, aliasing, halo and backend are checked before the first write, and
/// so are the destination's prior terms: a non-finite term, or coverage that runs ahead of this
/// frame, is refused before anything is added. Every destination sees frames, source rows and
/// columns in caller order, with Float64 products and additions and flux divided by scale squared.
/// Square uses normalized polygon area; Turbo uses normalized rectangular overlap; Gaussian and
/// Lanczos retain their stated finite support and signed lobes, and omit a coefficient below 1e-10
/// in magnitude. Negative total weights remain in the terms until finalization. Coverage counts a
/// frame once for any nonzero signed contribution, including valid zero-valued samples.
///
/// The scalar `Reference` and the optimized `OwnedRows` have the same addition order; their polygon
/// integration and separable coefficient reuse can differ by Float64 roundoff, and neither reorders
/// the sums. There are no floating atomics and no per-worker partial sums. A data overflow or a
/// cancellation after writing starts leaves discardable partial accumulators; no complete image,
/// mask, progress total or artifact is published by this primitive. Cancellation is checked at each
/// source row and at intervals of at most 4096 samples or destination updates.
/// @param execution initialized operation supplying cancellation, limits, backend and admitted workers.
/// @param model checked sample, weight, kernel and output contract.
/// @param input samples and auxiliary bands, borrowed unchanged throughout the call.
/// @param map checked coordinate planes with the exact source/output geometry and source core interval.
/// @param destination caller-owned terms retained across ordered calls.
/// @param table required for Lanczos and absent otherwise; must have the model's kernel.
/// @return the band's counts. An empty selection succeeds with zero contributions; the executor's
///         finalization decides validity. Otherwise `ErrorCode::NotInitialized` for an uninitialized
///         execution; `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when it stops;
///         `ErrorCode::UnsupportedBackend` for a forced GPU backend or a forced instruction set other
///         than scalar with fallback forbidden; `ErrorCode::LimitExceeded` when the geometry or a
///         source buffer exceeds the execution's limits; `ErrorCode::IncompatibleArguments` when the
///         map, samples, weights, mask, table or accumulator spans disagree with the model;
///         `ErrorCode::OutOfRange` when the band is not the map's core, the frame index or the
///         destination rows lie outside the model, or a square drop lacks its halo;
///         `ErrorCode::InvalidArgument` for a null, short or misaligned buffer or an invalid frame
///         weight; `ErrorCode::Aliasing` when an accumulator span overlaps another or an input; and
///         `ErrorCode::InvalidData` for a non-finite or unrepresentable sample, weight, product or
///         prior term, an invalid auxiliary weight, or a drop too narrow to represent.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleAccumulationReport>
accumulateDrizzleBand(const ExecutionContext& execution, const DrizzleModel& model, const DrizzleInputBand& input,
                      const DrizzlePixmapView& map, const DrizzleAccumulatorBand& destination,
                      const DrizzleKernelTable* table = nullptr);

} // namespace an

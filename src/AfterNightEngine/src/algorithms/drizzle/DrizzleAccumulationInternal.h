// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle accumulation's private seams: the executor's entry that skips
// the prior-term scan, and the value-only pieces the scatter in DrizzleAccumulation.cpp shares with
// the kernel arithmetic in DrizzleKernel.cpp and the sample gather in DrizzleSamples.cpp.
//
// Nothing here owns a buffer or holds scheduler state. A reader borrows a band, a drop is one source
// pixel's footprint and support computed on the caller's stack, and every function is either a pure
// calculation or a check that returns a typed failure.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>

#include <array>

/// The drizzle implementation's private helpers, shared by its translation units: accumulation,
/// sample reading, kernel arithmetic, geometry access, execution buffers and finalization.
namespace an::drizzle_detail
{

/// The executor's accumulation entry: `accumulateDrizzleBand` without the scan of prior terms.
///
/// The executor alone owns these terms: it zeroed them and visits frames and bands in order, and
/// every successful addition checked finiteness. Rechecking all prior destination terms on each
/// small source band would turn bounded streaming into repeated full-output scans. Argument, span,
/// aliasing and sample validation still run on every call.
/// @param execution the operation; see `accumulateDrizzleBand`.
/// @param model the checked model.
/// @param input the borrowed source band.
/// @param map the band's coordinate map.
/// @param destination the executor's zero-initialized terms.
/// @param table the Lanczos table, or null for the other kernels.
/// @return what `accumulateDrizzleBand` returns, apart from its refusal of a bad prior term.
[[nodiscard]] Result<DrizzleAccumulationReport>
accumulateOwnedDrizzleBand(const ExecutionContext& execution, const DrizzleModel& model, const DrizzleInputBand& input,
                           const DrizzlePixmapView& map, const DrizzleAccumulatorBand& destination,
                           const DrizzleKernelTable* table);

/// Reads one band's samples as doubles through a conversion `readerFor` chose once for the band's
/// representation. An absent band reads as one, the neutral weight.
struct SampleReader
{
    const DrizzleSampleBand* band = nullptr; ///< The band read, or null for an absent auxiliary plane.
    double (*read)(const std::byte*) noexcept = nullptr; ///< Widens one stored sample exactly to double.
    /// One sample of the band. A one-channel band broadcasts its channel to every channel asked for.
    /// @param x the column.
    /// @param y the row, relative to the band's first row.
    /// @param channel the source channel.
    /// @return the widened sample, or one for an absent band.
    [[nodiscard]] double at(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const noexcept;
};

/// The three readers one input band needs.
struct DrizzleReaders
{
    SampleReader pixels; ///< The source samples.
    SampleReader weights; ///< The pixel weights, or an absent reader that reads one.
    SampleReader flats; ///< The flat weights, or an absent reader that reads one.
};

/// One source pixel's admitted samples and effective weights, indexed by output channel.
struct DrizzleValues
{
    std::array<double, 4u> samples{}; ///< Admitted samples already divided by scale squared.
    std::array<double, 4u> weights{}; ///< Effective weights; zero for a sample that was not admitted.
    /// The first output channel the scatter visits: the CFA colour of this site, or zero.
    std::uint32_t firstChannel = 0u;
    /// How many output channels the scatter visits from `firstChannel`: the source's channel count.
    std::uint32_t channelCount = 0u;
};

/// One source pixel's drop: its mapped centre, the square kernel's footprint, the kernel's constants
/// and the destination cells its support can reach, clipped to the canvas.
struct DrizzleDrop
{
    DrizzleFootprint corners{}; ///< The square kernel's interpolated corners; unused by other kernels.
    double x = 0.0; ///< The mapped centre's column, in output pixels.
    double y = 0.0; ///< The mapped centre's row, in output pixels.
    double area = 1.0; ///< The square drop's area or the Turbo width squared; normalizes the overlap.
    /// The support's half-extent in output pixels: half the Turbo width, three Gaussian sigmas, or
    /// the Lanczos order in drop widths plus one half.
    double radius = 0.0;
    double inverseWidth = 1.0; ///< One over `pixelFraction * scale`; turns a distance into drop widths.
    double gaussianExponent = 0.0; ///< `-0.5 / (sigma * sigma)`, the factor of a squared distance.
    double gaussianNormalization = 0.0; ///< `1 / (2 * pi * sigma * sigma)`, the Gaussian's amplitude.
    bool rectangle = false; ///< Whether the square drop's corners prove it axis-aligned, so separable.
    /// The first destination column the support reaches; the cells are columns `[xBegin, xEnd)` and
    /// rows `[yBegin, yEnd)`.
    std::uint32_t xBegin = 0u, xEnd = 0u, yBegin = 0u, yEnd = 0u;
};

/// @var DrizzleDrop::xEnd
/// One past the last destination column the support reaches.

/// @var DrizzleDrop::yBegin
/// The first destination row the support reaches.

/// @var DrizzleDrop::yEnd
/// One past the last destination row the support reaches.

/// A failure in the algorithms domain, attributed to `an::accumulateDrizzleBand`.
/// @param code why the operation failed.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status accumulationFailure(ErrorCode code, const char* message);
/// The reader for a band's representation.
/// @param band the band, or null.
/// @return the reader, or an absent reader for a null band.
[[nodiscard]] SampleReader readerFor(const DrizzleSampleBand* band) noexcept;
/// Reads one source pixel, applies the validity rule and the non-finite policy, and forms each
/// admitted channel's scaled sample and effective weight. Auxiliary weights are checked even where
/// validity excludes the sample.
/// @param model the checked model.
/// @param input the band, for its mask, first row and frame weights.
/// @param readers the band's readers.
/// @param x the source column.
/// @param y the row, relative to the band's first row.
/// @param values receives the pixel's values.
/// @param report receives the selected, excluded and non-finite counts, or null to count nothing.
/// @return success; or `ErrorCode::InvalidData` for an invalid pixel or flat weight, a
///         validity-admitted non-finite sample under the reject policy, or an effective weight or
///         weighted flux that is not representable.
[[nodiscard]] Status gatherDrizzleValues(const DrizzleModel& model, const DrizzleInputBand& input,
                                         const DrizzleReaders& readers, std::uint32_t x, std::uint32_t y,
                                         DrizzleValues& values, DrizzleAccumulationReport* report);
/// Builds one source pixel's drop from its mapped centre, and for the square kernel its
/// interpolated corners, and clips its support to the output canvas.
/// @param model the checked model.
/// @param map the coordinate map, whose clipped halo the caller has checked.
/// @param row the source row's coordinates.
/// @param x the source column.
/// @param y the global source row.
/// @param drop receives the drop.
/// @return false when the centre is not finite, a square drop's corners cannot be interpolated or
///         enclose no area, or the support misses the canvas.
[[nodiscard]] bool prepareDrizzleDrop(const DrizzleModel& model, const DrizzlePixmapView& map,
                                      const DrizzleCoordinateRow& row, std::uint32_t x, std::uint32_t y,
                                      DrizzleDrop& drop);
/// The area of a convex quadrilateral in either orientation, computed relative to its first corner.
/// @param footprint the four corners, in order.
/// @return the area, or zero when a corner is not finite or the corners are degenerate or not convex.
[[nodiscard]] double squareArea(const DrizzleFootprint& footprint) noexcept;
/// The reference overlap: the footprint clipped against the four edges of the unit cell centred at
/// `(x, y)` in turn (Sutherland-Hodgman), its area by the shoelace formula, clamped to [0,1].
/// @param footprint the four corners.
/// @param x the destination column.
/// @param y the destination row.
/// @return the overlap area, not divided by the drop's area.
[[nodiscard]] double clippedSquareArea(const DrizzleFootprint& footprint, std::uint32_t x, std::uint32_t y) noexcept;
/// The optimized overlap: the signed areas under the footprint's four edges, each clamped to the
/// unit cell centred at `(x, y)`, summed and clamped to [0,1]. It shares no code with
/// `clippedSquareArea`, so each is a check on the other.
/// @param footprint the four corners.
/// @param x the destination column.
/// @param y the destination row.
/// @return the overlap area, not divided by the drop's area.
[[nodiscard]] double integratedSquareArea(const DrizzleFootprint& footprint, std::uint32_t x, std::uint32_t y) noexcept;
/// One destination cell's kernel coefficient for a drop, before the effective weight: the
/// normalized overlap for Square and Turbo, one for Point, the amplitude times both axis values for
/// Gaussian, and the product of both table values for Lanczos; a Gaussian or Lanczos coefficient
/// below 1e-10 in magnitude is zero.
/// @param model the checked model.
/// @param drop the drop.
/// @param table the Lanczos table, or null for the other kernels.
/// @param x the destination column.
/// @param y the destination row.
/// @param reference whether a square drop uses the clipped-polygon overlap; otherwise a rectangular
///        drop uses its closed form and any other the edge integral.
/// @return the coefficient.
[[nodiscard]] double drizzleCoefficient(const DrizzleModel& model, const DrizzleDrop& drop,
                                        const DrizzleKernelTable* table, std::uint32_t x, std::uint32_t y,
                                        bool reference) noexcept;
/// One axis's coefficient at a signed distance from the drop centre: the Turbo overlap length, the
/// Gaussian's unnormalized value, the Lanczos table entry at that distance in drop widths, or one
/// for the other kernels.
/// @param model the checked model.
/// @param drop the drop.
/// @param table the Lanczos table; required for the Lanczos kernels.
/// @param distance the signed distance, in output pixels.
/// @return the coefficient.
[[nodiscard]] double drizzleAxisCoefficient(const DrizzleModel& model, const DrizzleDrop& drop,
                                            const DrizzleKernelTable* table, double distance) noexcept;

} // namespace an::drizzle_detail

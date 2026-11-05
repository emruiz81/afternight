// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the arithmetic both background paths are built out of.
//
// A background model is a surface fitted to numbers and then evaluated over a picture, and the two
// halves of that sentence are shared by everything in this domain: the sample-driven path of
// `Algorithms/Background/BackgroundModel.h` and the automatic path of
// `Algorithms/Background/AutoBackground.h` fit the same polynomial in the same basis, solve the
// same kind of small dense system, and put the same level back. Writing that once is what stops the
// two of them disagreeing about what a degree-three polynomial is.
//
// Two things live here that are not obviously shared and are. The first is the **low-pass**: a
// moving-window average of a radius that is a fraction of the picture rather than a filter radius.
// The neighbourhood family's tap arrays are declared up to `kMaxStencilRadius`, and a smoothing
// that reaches a tenth of the frame is outside what that declaration is for; a running sum is the
// one form whose cost per output does not grow with the window, which is what makes the hundreds of
// them a robust iteration runs affordable. The second is the **dense solve**: both paths reduce
// to a small symmetric system, and rank deficiency in one has to mean the same thing as in the
// other - a basis function the sites do not determine is dropped and reported, never quietly
// stabilised into a number nobody asked for.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <cstdint>
#include <span>

/// The background domain's private arithmetic: the polynomial basis, the thin-plate radial function,
/// the two dense solves, the rank statistics, the deterministic dither, and the box low-pass,
/// reduction and enlargement of working planes.
namespace an::background_detail
{

/// One term of the two-dimensional polynomial basis.
///
/// Trivially copyable.
struct PolynomialTerm
{
    /// The power the horizontal coordinate is raised to.
    std::uint32_t xPower = 0u;

    /// The power the vertical coordinate is raised to.
    std::uint32_t yPower = 0u;
};

/// The most terms a basis of the highest accepted degree holds.
///
/// Twenty-eight, which is the triangular number of a degree-six basis. Written as a constant so a
/// term table can live on a stack frame.
inline constexpr std::uint32_t kMaxPolynomialTerms = 28u;

/// How many extra unknowns a thin-plate spline carries beside one per site.
///
/// Three: the constant and the two linear coefficients that span the radial function's null space.
/// Constraining all three is what makes the system non-singular for any site set that is not
/// collinear; constraining the constant alone does not.
inline constexpr std::uint32_t kThinPlateAffineTerms = 3u;

/// The squared distance below which the thin-plate radial function is taken to be zero.
///
/// The function is zero at the origin and its logarithm is not, so a site evaluated at its own
/// centre needs the value rather than the limit.
inline constexpr double kThinPlateOriginEpsilon = 1.0e-12;

/// A coordinate mapped onto the interval the polynomial basis is expressed over.
///
/// The whole picture spans `[-1, 1]` along each axis, which is what keeps a degree-six basis
/// conditioned. A picture one pixel wide has no interval to span and maps to its centre.
///
/// @param value the pixel coordinate.
/// @param extent how many pixels the axis holds.
/// @return the normalised coordinate.
[[nodiscard]] double normalisedCoordinate(double value, std::uint32_t extent) noexcept;

/// How many terms a polynomial basis of one degree holds.
///
/// @param degree the total degree.
/// @return `(degree + 1) * (degree + 2) / 2`.
[[nodiscard]] std::uint32_t polynomialTermCount(std::uint32_t degree) noexcept;

/// Writes the basis of one degree, lowest total degree first.
///
/// The order matters and is part of the contract: a solve that has to drop a term drops the one it
/// meets last, so the terms a caller loses first are the highest-order ones rather than an
/// arbitrary selection.
///
/// @param degree the total degree.
/// @param terms where the basis goes, of at least `polynomialTermCount(degree)` entries.
/// @return how many terms were written.
[[nodiscard]] std::uint32_t writePolynomialTerms(std::uint32_t degree, std::span<PolynomialTerm> terms) noexcept;

/// Evaluates every basis function at one normalised position.
///
/// @param terms the basis.
/// @param x the normalised horizontal coordinate.
/// @param y the normalised vertical coordinate.
/// @param values where the basis values go, of at least the basis size.
void evaluatePolynomialBasis(std::span<const PolynomialTerm> terms, double x, double y,
                             std::span<double> values) noexcept;

/// Evaluates a fitted polynomial at one normalised position.
///
/// @param terms the basis.
/// @param coefficients one per term.
/// @param x the normalised horizontal coordinate.
/// @param y the normalised vertical coordinate.
/// @return the surface's value.
[[nodiscard]] double evaluatePolynomial(std::span<const PolynomialTerm> terms, std::span<const double> coefficients,
                                        double x, double y) noexcept;

/// The thin-plate radial function of a squared distance.
///
/// `r^2 * log(r)`, written as `r^2 * log(r^2) / 2` so the square root is never taken.
///
/// @param squaredDistance the squared distance between two positions.
/// @return the radial function, which is zero at and very near the origin.
[[nodiscard]] double thinPlateRadial(double squaredDistance) noexcept;

/// What one symmetric solve did.
struct SymmetricSolveResult
{
    /// Whether the system had a solution at all: false when every unknown was dropped - including a
    /// system whose diagonal holds nothing positive - or when the spans are shorter than the order.
    bool solved = false;

    /// How many basis functions were dropped because the data does not determine them.
    std::uint32_t droppedTerms = 0u;

    /// The square root of the ratio of the largest kept pivot to the smallest.
    ///
    /// The pivots are the squares of the triangular factor's diagonal, so the square root of their
    /// ratio estimates the conditioning of the system that was fitted rather than of its normal
    /// form.
    double conditionEstimate = 0.0;
};

/// Solves a symmetric positive-semidefinite system in place, dropping what the data cannot support.
///
/// A Cholesky factorisation with one addition: when a pivot falls to or below a tolerance - eight
/// times the machine epsilon times the order times the largest original diagonal - that unknown is
/// removed from the system rather than divided by. Removing it means zeroing its row of the factor
/// and its right-hand side, which is exactly the factorisation of the system with that row and
/// column struck out, so the remaining unknowns are solved on the reduced basis and the removed one
/// is reported as zero. Only the lower triangle of `matrix` is read.
///
/// @param matrix the system, row-major and of `order * order` entries, overwritten by its factor.
/// @param rhs the right-hand side, overwritten by the solution.
/// @param order how many unknowns.
/// @return what the solve did; when it is not `solved`, `rhs` holds nothing meaningful.
[[nodiscard]] SymmetricSolveResult solveSymmetric(std::span<double> matrix, std::span<double> rhs,
                                                  std::uint32_t order) noexcept;

/// Solves a general dense system in place by elimination with partial pivoting.
///
/// The thin-plate spline's system is symmetric and indefinite - its affine block sits on a zero
/// diagonal - so it is not a candidate for the factorisation above and gets the general one.
///
/// @param execution the operation policy and its cancellation state.
/// @param matrix the system, row-major and of `order * order` entries, destroyed by the solve.
/// @param rhs the right-hand side, overwritten by the solution.
/// @param order how many unknowns.
/// @return true when every pivot was usable; false for a singular system, whose right-hand side is
///         then meaningless; or a cancellation or deadline failure.
[[nodiscard]] Result<bool> solveGeneral(const ExecutionContext& execution, std::span<double> matrix,
                                        std::span<double> rhs, std::uint32_t order);

/// A deterministic centred unit-width dither, derived from one sample position.
///
/// A dither has to be reproducible and independent of how the work was divided between threads, so
/// it is a function of the sample's own index rather than a draw from a generator. The mixing is
/// the output SplitMix64 produces from a state equal to the index, which is enough to leave no
/// visible structure at the amplitude a dither uses, and its top fifty-three bits become the
/// fraction.
///
/// @param index the sample's position within its channel.
/// @return a value in `[-0.5, 0.5)`.
[[nodiscard]] double ditherFraction(std::uint64_t index) noexcept;

/// The median of a mutable array, which it reorders.
///
/// An exact rank rather than a histogram approximation, and the mean of the two central values for
/// an even count.
///
/// @param values the array, reordered by the call.
/// @return the median, or zero for an empty array.
[[nodiscard]] double medianOf(std::span<double> values) noexcept;

/// A linearly interpolated percentile of a mutable array, which it sorts.
///
/// @param values the array, sorted by the call.
/// @param percentile where to sample, within `[0, 100]` and clamped into it.
/// @return the value, or zero for an empty array.
[[nodiscard]] double percentileOf(std::span<double> values, double percentile) noexcept;

/// Averages a moving window over a plane once along each axis, as a running sum.
///
/// The border is resolved by clamping to the edge sample, which, unlike a zero border, leaves the
/// window average of a constant plane equal to that constant at every position, corners included.
/// The running sum is kept in Float64, so its cost per output does not grow with the radius.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the plane to read.
/// @param destination the plane to write, of the same extent and sharing nothing with the source.
/// @param scratch a third plane of the same extent, holding the horizontal pass.
/// @param radius the half-window, in pixels. Zero copies the source.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status lowpassPass(const ExecutionContext& execution, PlaneView<const float> source,
                                 PlaneView<float> destination, PlaneView<float> scratch, std::uint32_t radius);

/// Applies `lowpassPass` a stated number of times.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the plane to read, which is left as it was unless it is the destination.
/// @param destination the plane the answer goes into.
/// @param first a working plane of the same extent, sharing nothing with either.
/// @param second another such plane.
/// @param radius the half-window, in pixels.
/// @param passes how many times to apply it. Zero copies the source.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status lowpassPlane(const ExecutionContext& execution, PlaneView<const float> source,
                                  PlaneView<float> destination, PlaneView<float> first, PlaneView<float> second,
                                  std::uint32_t radius, std::uint32_t passes);

/// Copies one plane onto another of the same extent.
///
/// @param execution the operation policy and engine-context lease.
/// @param source the plane to read.
/// @param destination the plane to write.
/// @return success, or an execution or cancellation failure.
[[nodiscard]] Status copyPlaneSamples(const ExecutionContext& execution, PlaneView<const float> source,
                                      PlaneView<float> destination);

/// One reduced plane read at a full-resolution position by bilinear interpolation.
///
/// The reduced sample at index `i` stands for the centre of the block it averaged, which is at
/// `i * factor + (factor - 1) / 2` in full-resolution coordinates; interpolating between block
/// centres rather than between block corners is what keeps the enlarged surface from shifting by
/// half a block. A position before the first centre or past the last takes the edge value, so the
/// enlargement never extrapolates. A partial last block is placed as if it were whole, so where the
/// factor does not divide the extent its centre is taken to lie less than half a block further out
/// than the samples it averaged.
///
/// @param plane the reduced plane.
/// @param factor the reduction factor it was produced with.
/// @param x the full-resolution column.
/// @param y the full-resolution row.
/// @return the interpolated value.
[[nodiscard]] double interpolateReduced(PlaneView<const float> plane, std::uint32_t factor, double x,
                                        double y) noexcept;

} // namespace an::background_detail

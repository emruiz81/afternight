// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - scalar Float64 polynomial distortion, its Newton inverse, and its maps.
//
// The model is the Simple Imaging Polynomial convention for FITS headers (Shupe et al., 2005): a
// forward pair of polynomials in the offset from the reference pixel that corrects a distorted
// position, and an optional inverse pair that approximately undoes it. Evaluation sums terms in a
// fixed order so two runs agree bit for bit. When no inverse pair exists, each position is solved
// from the forward pair by two-dimensional Newton iteration seeded at the position itself, never at
// a neighbour's answer. The maps tile a grid by full-width row bands, resolve the route and the
// Newton term lists once per map rather than per pixel, and publish counters once per tile.

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ParallelExecutor.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace an
{
namespace
{

constexpr const char* kCreateOperation = "an::SipDistortion::create";
constexpr const char* kForwardOperation = "an::applySipForward";
constexpr const char* kInverseOperation = "an::applySipInverse";
constexpr const char* kForwardMapOperation = "an::computeSipForwardMap";
constexpr const char* kInverseMapOperation = "an::computeSipInverseMap";

/// A squared Newton residual above this is a residual whose length is above the inverse tolerance.
///
/// The square of the tolerance widened by a millionth: far more than the few units in the last
/// place a computed squared length can be off from the exact one, and far less than any step's
/// progress, so it separates the steps that have certainly not arrived from the rest.
constexpr double kToleranceSquaredMargin = kSipInverseTolerancePixels * kSipInverseTolerancePixels * (1.0 + 1.0e-6);

/// The smallest Jacobian determinant a Newton step will divide by.
///
/// Below it the linearised correction is not a direction, and the step it would produce is
/// arbitrarily large rather than merely inaccurate.
constexpr double kSingularJacobian = 1.0e-12;

/// The tallest band of rows one tile covers.
///
/// A map tile is always the full width of the grid, because a row of a coordinate plane is
/// contiguous and nothing is reused across columns: splitting by column would break both output
/// streams into short runs for no gain. What is chosen per map is how many rows a tile holds, and
/// this is the ceiling on that.
constexpr std::uint32_t kMaximumMapTileHeight = 32u;

/// How many tiles each worker should have to draw from.
///
/// Enough that a worker which finishes early has somewhere to go, and few enough that the per-tile
/// bookkeeping stays a rounding error against the polynomial work inside one.
constexpr std::uint32_t kMapTilesPerWorker = 4u;

/// What one mapped pixel costs, so the scheduler can decide whether a tile is worth handing over.
///
/// Two floats out, and arithmetic in the low hundreds of operations for an inverse pixel - a couple
/// of Newton steps, each evaluating two polynomials and their derivatives - against a few tens for
/// a forward one. The order of magnitude is what matters: leaving the cost at its default of one
/// operation per item tells the scheduler a megapixel map is smaller than its own hand-off, and it
/// answers by merging the tiles until most of the workers have nothing to do.
constexpr std::uint64_t kMapBytesPerPixel = 8u;
constexpr std::uint64_t kForwardMapOperationsPerPixel = 32u;
constexpr std::uint64_t kInverseMapOperationsPerPixel = 128u;

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] Status distortionFailure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// The powers of one offset pair, up to the order that will be evaluated.
struct PowerTable
{
    /// `u[i]` is the horizontal offset raised to `i`.
    std::array<double, kSipCoefficientRowCount> u{};

    /// `v[j]` is the vertical offset raised to `j`.
    std::array<double, kSipCoefficientRowCount> v{};
};

/// Fills the powers of one offset pair.
///
/// @param u the horizontal offset from the reference pixel.
/// @param v the vertical offset.
/// @param order the highest power needed, which is at most `kMaximumSipOrder`.
/// @return the table, whose entry zero is one on both axes.
[[nodiscard]] PowerTable buildPowerTable(const double u, const double v, const std::uint32_t order) noexcept
{
    PowerTable powers;
    powers.u[0] = 1.0;
    powers.v[0] = 1.0;
    for (std::size_t index = 1u; index <= static_cast<std::size_t>(order); ++index)
    {
        powers.u[index] = powers.u[index - 1u] * u;
        powers.v[index] = powers.v[index - 1u] * v;
    }
    return powers;
}

/// Evaluates one polynomial component at an already-tabulated offset.
///
/// Terms are summed in ascending horizontal degree and, within that, ascending vertical degree, and
/// a coefficient that is exactly zero contributes nothing and is skipped. The order is part of the
/// contract: a floating-point sum is not associative, so the sequence is what makes two runs of this
/// engine agree with each other.
///
/// @param component the polynomial to evaluate.
/// @param powers the offset powers, built for at least `component.order`.
/// @param out the sum.
/// @return false when the sum is not finite, which is a polynomial that overflowed.
[[nodiscard]] bool evaluateComponent(const SipPolynomial& component, const PowerTable& powers, double& out) noexcept
{
    out = 0.0;
    if (component.order == 0u)
    {
        return true;
    }

    const std::size_t order = static_cast<std::size_t>(component.order);
    for (std::size_t i = 0u; i <= order; ++i)
    {
        for (std::size_t j = 0u; j <= order - i; ++j)
        {
            const double coefficient = component.coefficients[i][j];
            if (coefficient == 0.0)
            {
                continue;
            }
            out += coefficient * powers.u[i] * powers.v[j];
        }
    }
    return std::isfinite(out);
}

/// Most terms one polynomial of the highest order has.
constexpr std::size_t kMaximumSipTerms = kSipCoefficientRowCount * (kSipCoefficientRowCount + 1u) / 2u;

/// One product a polynomial or one of its derivatives adds: a factor times two tabulated powers.
struct SipTerm
{
    /// The coefficient, or for a derivative the coefficient already multiplied by its exponent.
    double factor = 0.0;

    /// Which horizontal power the factor multiplies.
    std::uint8_t horizontal = 0u;

    /// Which vertical power.
    std::uint8_t vertical = 0u;
};

/// One polynomial's non-zero terms, and each derivative's, in the order they are accumulated.
///
/// A polynomial and its two derivatives are sums over the coefficient grid in ascending horizontal
/// degree and, within that, ascending vertical degree, skipping a coefficient that is exactly zero -
/// the order `evaluateComponent` states and the contract relies on. A Newton map evaluates the same
/// polynomial millions of times, so the grid is walked once here, testing each coefficient and each
/// exponent for zero, and an evaluation adds the products it found. Each list is the sequence of
/// terms one accumulator receives, in that order, and each product is the same product: a
/// derivative's term is `coefficient * exponent * u * v`, evaluated left to right, so multiplying
/// the first two out ahead leaves every rounding where it was.
struct SipTermList
{
    /// The polynomial's own terms.
    std::array<SipTerm, kMaximumSipTerms> value{};

    /// Its derivative with respect to the horizontal offset.
    std::array<SipTerm, kMaximumSipTerms> horizontal{};

    /// Its derivative with respect to the vertical offset.
    std::array<SipTerm, kMaximumSipTerms> vertical{};

    /// How many entries of each list are in use.
    std::uint32_t valueCount = 0u;
    std::uint32_t horizontalCount = 0u;
    std::uint32_t verticalCount = 0u;
};

/// Walks one polynomial's coefficients once, in the order the sums are defined in.
///
/// @param component the polynomial.
/// @return its term lists; empty for a component of order zero, which evaluates to zero.
[[nodiscard]] SipTermList sipTermsOf(const SipPolynomial& component) noexcept
{
    SipTermList terms;
    if (component.order == 0u)
    {
        return terms;
    }
    const std::size_t order = static_cast<std::size_t>(component.order);
    for (std::size_t i = 0u; i <= order; ++i)
    {
        for (std::size_t j = 0u; j <= order - i; ++j)
        {
            const double coefficient = component.coefficients[i][j];
            if (coefficient == 0.0)
            {
                continue;
            }
            const auto horizontalPower = static_cast<std::uint8_t>(i);
            const auto verticalPower = static_cast<std::uint8_t>(j);
            terms.value[terms.valueCount++] = {coefficient, horizontalPower, verticalPower};
            if (i > 0u)
            {
                terms.horizontal[terms.horizontalCount++] = {coefficient * static_cast<double>(i),
                                                             static_cast<std::uint8_t>(i - 1u), verticalPower};
            }
            if (j > 0u)
            {
                terms.vertical[terms.verticalCount++] = {coefficient * static_cast<double>(j), horizontalPower,
                                                         static_cast<std::uint8_t>(j - 1u)};
            }
        }
    }
    return terms;
}

/// Sums one term list at an already-tabulated offset.
///
/// @param terms the products, in the order they are added.
/// @param count how many are in use.
/// @param powers the offset powers.
/// @return the sum.
[[nodiscard]] double sumTerms(const std::array<SipTerm, kMaximumSipTerms>& terms, const std::uint32_t count,
                              const PowerTable& powers) noexcept
{
    double sum = 0.0;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        sum += terms[index].factor * powers.u[terms[index].horizontal] * powers.v[terms[index].vertical];
    }
    return sum;
}

/// What a Newton inverse evaluates, resolved once from the model.
struct SipNewtonModel
{
    /// The reference pixel the offsets are measured from.
    PixelCoordinate reference{};

    /// The order one shared power table covers.
    std::uint32_t order = 0u;

    /// The horizontal forward polynomial's terms.
    SipTermList forwardX;

    /// The vertical forward polynomial's terms.
    SipTermList forwardY;
};

/// The higher of two polynomial orders, which is what one shared power table has to cover.
///
/// @param first one component.
/// @param second the other.
/// @return the higher order.
[[nodiscard]] std::uint32_t sharedOrder(const SipPolynomial& first, const SipPolynomial& second) noexcept
{
    return first.order > second.order ? first.order : second.order;
}

/// What a Newton inverse of this model evaluates.
///
/// @param distortion the model.
/// @return its reference pixel, shared order and forward term lists.
[[nodiscard]] SipNewtonModel newtonModelOf(const SipDistortion& distortion) noexcept
{
    SipNewtonModel model;
    model.reference = distortion.referencePixel();
    model.order = sharedOrder(distortion.forwardX(), distortion.forwardY());
    model.forwardX = sipTermsOf(distortion.forwardX());
    model.forwardY = sipTermsOf(distortion.forwardY());
    return model;
}

/// Applies the forward polynomials without re-checking the position.
///
/// @param distortion the model to apply.
/// @param pixel the finite distorted position.
/// @param out the corrected position.
/// @return false when a polynomial overflows to a non-finite value.
[[nodiscard]] bool forwardCorrection(const SipDistortion& distortion, const PixelCoordinate& pixel,
                                     PixelCoordinate& out) noexcept
{
    if (!distortion.hasForwardModel())
    {
        out = pixel;
        return true;
    }

    const PixelCoordinate reference = distortion.referencePixel();
    const PowerTable powers = buildPowerTable(pixel.x - reference.x, pixel.y - reference.y,
                                              sharedOrder(distortion.forwardX(), distortion.forwardY()));
    double dx = 0.0;
    double dy = 0.0;
    if (!evaluateComponent(distortion.forwardX(), powers, dx) || !evaluateComponent(distortion.forwardY(), powers, dy))
    {
        return false;
    }

    out = PixelCoordinate{.x = pixel.x + dx, .y = pixel.y + dy};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

/// Solves the forward model for the position that maps to `target`.
///
/// Newton's method on the two-dimensional correction, seeded at the target itself. The iteration
/// stops as soon as the forward model lands within `kSipInverseTolerancePixels`; if it exhausts its
/// steps, the final estimate is accepted only when its own round trip is within ten times that.
///
/// @param distortion the model, which has forward polynomials.
/// @param model what each step evaluates, resolved from `distortion` once rather than per step.
/// @param target the undistorted position to invert.
/// @param solved the corrected position, written only on success.
/// @param iterations how many steps ran, written whether or not the solve succeeded, because steps
///        a failed pixel spent are work the map performed.
/// @param residualOut the round-trip distance the accepted estimate leaves, written on success.
/// @return false when the Jacobian is singular, a step leaves the finite range, or the final
///         estimate is not close enough.
[[nodiscard]] bool solveNewton(const SipDistortion& distortion, const SipNewtonModel& model,
                               const PixelCoordinate& target, PixelCoordinate& solved, std::uint32_t& iterations,
                               double& residualOut) noexcept
{
    const PixelCoordinate reference = model.reference;
    PixelCoordinate estimate = target;
    iterations = 0u;
    residualOut = std::numeric_limits<double>::quiet_NaN();

    for (std::uint32_t iteration = 0u; iteration < kMaximumSipNewtonIterations; ++iteration)
    {
        iterations = iteration + 1u;
        const PowerTable powers = buildPowerTable(estimate.x - reference.x, estimate.y - reference.y, model.order);
        const double dxValue = sumTerms(model.forwardX.value, model.forwardX.valueCount, powers);
        const double dxDu = sumTerms(model.forwardX.horizontal, model.forwardX.horizontalCount, powers);
        const double dxDv = sumTerms(model.forwardX.vertical, model.forwardX.verticalCount, powers);
        if (!std::isfinite(dxValue) || !std::isfinite(dxDu) || !std::isfinite(dxDv))
        {
            return false;
        }
        const double dyValue = sumTerms(model.forwardY.value, model.forwardY.valueCount, powers);
        const double dyDu = sumTerms(model.forwardY.horizontal, model.forwardY.horizontalCount, powers);
        const double dyDv = sumTerms(model.forwardY.vertical, model.forwardY.verticalCount, powers);
        if (!std::isfinite(dyValue) || !std::isfinite(dyDu) || !std::isfinite(dyDv))
        {
            return false;
        }

        const double residualX = (estimate.x + dxValue) - target.x;
        const double residualY = (estimate.y + dyValue) - target.y;
        // The residual's length is `std::hypot`'s, which is what the tolerance is stated against and
        // what the map reports, but a careful library call is not needed to see that a step is still
        // far from the answer. The squared length rounds within a few units in the last place of the
        // exact one, so a squared length a millionth above the squared tolerance is a length above
        // the tolerance, and only a step that may have arrived asks `std::hypot`. A not-a-number
        // compares false and reaches `std::hypot` as well, which then decides it as it always did.
        const double squared = residualX * residualX + residualY * residualY;
        if (!(squared > kToleranceSquaredMargin))
        {
            const double residual = std::hypot(residualX, residualY);
            if (residual <= kSipInverseTolerancePixels)
            {
                solved = estimate;
                residualOut = residual;
                return true;
            }
        }

        const double j00 = 1.0 + dxDu;
        const double j01 = dxDv;
        const double j10 = dyDu;
        const double j11 = 1.0 + dyDv;
        const double determinant = j00 * j11 - j01 * j10;
        if (!std::isfinite(determinant) || std::abs(determinant) < kSingularJacobian)
        {
            return false;
        }

        estimate.x -= (j11 * residualX - j01 * residualY) / determinant;
        estimate.y -= (-j10 * residualX + j00 * residualY) / determinant;
        if (!std::isfinite(estimate.x) || !std::isfinite(estimate.y))
        {
            return false;
        }
    }

    PixelCoordinate roundTrip{};
    if (!forwardCorrection(distortion, estimate, roundTrip))
    {
        return false;
    }
    const double residual = std::hypot(roundTrip.x - target.x, roundTrip.y - target.y);
    if (!(residual <= 10.0 * kSipInverseTolerancePixels))
    {
        return false;
    }

    solved = estimate;
    residualOut = residual;
    return true;
}

/// Undoes the forward polynomials by evaluating the model's inverse polynomials once, without
/// re-checking the position.
///
/// @param distortion the model, which has inverse polynomials.
/// @param pixel the finite undistorted position.
/// @param solved the corrected position, written on success.
/// @param residualOut the round-trip distance the forward model leaves at `solved`, or not-a-number
///        when there is no forward model or it overflows there.
/// @return false when an inverse polynomial or the corrected position is not finite.
[[nodiscard]] bool inverseByCoefficients(const SipDistortion& distortion, const PixelCoordinate& pixel,
                                         PixelCoordinate& solved, double& residualOut) noexcept
{
    const PixelCoordinate reference = distortion.referencePixel();
    const PowerTable powers = buildPowerTable(pixel.x - reference.x, pixel.y - reference.y,
                                              sharedOrder(distortion.inverseX(), distortion.inverseY()));
    double dx = 0.0;
    double dy = 0.0;
    if (!evaluateComponent(distortion.inverseX(), powers, dx) || !evaluateComponent(distortion.inverseY(), powers, dy))
    {
        return false;
    }

    solved = PixelCoordinate{.x = pixel.x + dx, .y = pixel.y + dy};
    if (!std::isfinite(solved.x) || !std::isfinite(solved.y))
    {
        return false;
    }

    residualOut = std::numeric_limits<double>::quiet_NaN();
    if (distortion.hasForwardModel())
    {
        PixelCoordinate roundTrip{};
        if (forwardCorrection(distortion, solved, roundTrip))
        {
            residualOut = std::hypot(roundTrip.x - pixel.x, roundTrip.y - pixel.y);
        }
    }
    return true;
}

/// Which per-pixel work an inverse correction is, which the model decides and no pixel changes.
///
/// Resolved once per map rather than at every pixel: a map of a megapixel frame asked the same
/// three questions a million times over, and the answer cannot differ between two of them.
///
/// @param distortion the model.
/// @return the inverse polynomials when it has any, else Newton iteration when it has forward
///         polynomials, else the identity.
[[nodiscard]] SipInverseRoute inverseRouteOf(const SipDistortion& distortion) noexcept
{
    if (distortion.hasInverseModel())
    {
        return SipInverseRoute::InverseCoefficients;
    }
    return distortion.hasForwardModel() ? SipInverseRoute::NewtonIteration : SipInverseRoute::Identity;
}

/// Undoes the forward polynomials without re-checking the position.
///
/// @param distortion the model to undo.
/// @param pixel the finite undistorted position.
/// @param result the corrected position and how it was reached.
/// @return false when the model does not invert at that position.
[[nodiscard]] bool inverseCorrection(const SipDistortion& distortion, const PixelCoordinate& pixel,
                                     SipInverseResult& result) noexcept
{
    result = SipInverseResult{};
    result.route = inverseRouteOf(distortion);
    result.residualPixels = std::numeric_limits<double>::quiet_NaN();

    switch (result.route)
    {
    case SipInverseRoute::Identity:
        result.pixel = pixel;
        return true;
    case SipInverseRoute::InverseCoefficients:
        return inverseByCoefficients(distortion, pixel, result.pixel, result.residualPixels);
    case SipInverseRoute::NewtonIteration:
        return solveNewton(distortion, newtonModelOf(distortion), pixel, result.pixel, result.iterations,
                           result.residualPixels);
    }
    return false;
}

/// The per-worker counters a map accumulates before publishing them once per tile.
struct MapTally
{
    /// Pixels that received a finite position.
    std::uint64_t mapped = 0u;

    /// Pixels left holding the unmapped sentinel.
    std::uint64_t unmapped = 0u;

    /// Pixels answered by returning the position unchanged.
    std::uint64_t identityRoute = 0u;

    /// Pixels answered by the model's own inverse polynomials.
    std::uint64_t inverseCoefficientRoute = 0u;

    /// Pixels answered by a Newton solve.
    std::uint64_t newtonRoute = 0u;

    /// Newton steps run across the tile.
    std::uint64_t newtonIterations = 0u;

    /// The largest measured round-trip distance in the tile.
    double maximumResidual = 0.0;
};

/// The shared destination one map's workers publish their tallies into.
struct MapCounters
{
    /// Pixels that received a finite position.
    std::atomic<std::uint64_t> mapped{0u};

    /// Pixels left holding the unmapped sentinel.
    std::atomic<std::uint64_t> unmapped{0u};

    /// Pixels answered by returning the position unchanged.
    std::atomic<std::uint64_t> identityRoute{0u};

    /// Pixels answered by the model's own inverse polynomials.
    std::atomic<std::uint64_t> inverseCoefficientRoute{0u};

    /// Pixels answered by a Newton solve.
    std::atomic<std::uint64_t> newtonRoute{0u};

    /// Newton steps run across every tile.
    std::atomic<std::uint64_t> newtonIterations{0u};

    /// The largest measured round-trip distance across every tile.
    std::atomic<double> maximumResidual{0.0};
};

/// Publishes one tile's tally into the shared counters.
///
/// Once per tile rather than once per pixel: the arithmetic a tile does is thousands of scalar
/// operations, and paying for a contended increment on each of them would measure the counter.
///
/// @param counters the shared destination.
/// @param tally what one tile counted.
void publishTally(MapCounters& counters, const MapTally& tally) noexcept
{
    counters.mapped.fetch_add(tally.mapped, std::memory_order_relaxed);
    counters.unmapped.fetch_add(tally.unmapped, std::memory_order_relaxed);
    counters.identityRoute.fetch_add(tally.identityRoute, std::memory_order_relaxed);
    counters.inverseCoefficientRoute.fetch_add(tally.inverseCoefficientRoute, std::memory_order_relaxed);
    counters.newtonRoute.fetch_add(tally.newtonRoute, std::memory_order_relaxed);
    counters.newtonIterations.fetch_add(tally.newtonIterations, std::memory_order_relaxed);

    double observed = counters.maximumResidual.load(std::memory_order_relaxed);
    while (tally.maximumResidual > observed &&
           !counters.maximumResidual.compare_exchange_weak(observed, tally.maximumResidual, std::memory_order_relaxed,
                                                           std::memory_order_relaxed))
    {
    }
}

/// Reads the shared counters into the public report.
///
/// @param counters what the tiles published.
/// @return the report.
[[nodiscard]] SipMapReport readCounters(const MapCounters& counters) noexcept
{
    SipMapReport report;
    report.mapped = counters.mapped.load(std::memory_order_relaxed);
    report.unmapped = counters.unmapped.load(std::memory_order_relaxed);
    report.identityRoute = counters.identityRoute.load(std::memory_order_relaxed);
    report.inverseCoefficientRoute = counters.inverseCoefficientRoute.load(std::memory_order_relaxed);
    report.newtonRoute = counters.newtonRoute.load(std::memory_order_relaxed);
    report.newtonIterations = counters.newtonIterations.load(std::memory_order_relaxed);
    report.maximumResidualPixels = counters.maximumResidual.load(std::memory_order_relaxed);
    return report;
}

/// Walks one tile, asking `correct` for each pixel and writing what it answers.
///
/// The correction is a template parameter rather than a call through a pointer, so the whole
/// per-pixel path of one route inlines into one loop with no branch of its own. Which route that is
/// was decided before the tiling began.
///
/// @param request the grid and the caller-owned planes.
/// @param tile the pixels this call covers.
/// @param tally the counters to add this tile's work to.
/// @param correct what to do with one pixel, which returns false for a pixel it cannot correct.
template <typename CorrectT>
void mapTile(const SipMapRequest& request, const ParallelTile& tile, MapTally& tally, CorrectT&& correct)
{
    float* const xPlane = request.xMap.data();
    float* const yPlane = request.yMap.data();
    for (std::uint32_t y = tile.yBegin; y < tile.yEnd; ++y)
    {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(request.width);
        for (std::uint32_t x = tile.xBegin; x < tile.xEnd; ++x)
        {
            double correctedX = 0.0;
            double correctedY = 0.0;
            const bool succeeded =
                correct(static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5, correctedX, correctedY, tally);
            const std::size_t index = rowOffset + static_cast<std::size_t>(x);
            if (succeeded)
            {
                xPlane[index] = static_cast<float>(correctedX);
                yPlane[index] = static_cast<float>(correctedY);
                ++tally.mapped;
            }
            else
            {
                xPlane[index] = unmappedCoordinate();
                yPlane[index] = unmappedCoordinate();
                ++tally.unmapped;
            }
        }
    }
}

/// Validates one map request against the frame it is asked to fill.
///
/// @param request the grid and the caller-owned planes.
/// @param operation the public operation that is validating.
/// @return success, or the first rule the request breaks.
[[nodiscard]] Status checkMapRequest(const SipMapRequest& request, const char* const operation)
{
    if (request.width == 0u || request.height == 0u)
    {
        return distortionFailure(ErrorCode::InvalidArgument, "a coordinate map needs a nonzero width and height",
                                 operation);
    }

    const std::uint64_t pixels = static_cast<std::uint64_t>(request.width) * static_cast<std::uint64_t>(request.height);
    if (pixels > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        return distortionFailure(ErrorCode::LimitExceeded, "the mapped grid holds more pixels than this host can index",
                                 operation);
    }

    const std::size_t required = static_cast<std::size_t>(pixels);
    if (request.xMap.size() != required || request.yMap.size() != required)
    {
        return distortionFailure(ErrorCode::IncompatibleArguments,
                                 "each coordinate plane must hold exactly one entry per mapped pixel", operation);
    }

    const float* const xBegin = request.xMap.data();
    const float* const yBegin = request.yMap.data();
    if (xBegin < yBegin + required && yBegin < xBegin + required)
    {
        return distortionFailure(ErrorCode::Aliasing, "the two coordinate planes must not overlap", operation);
    }

    return Status::success();
}

/// Fills both planes with one distortion direction.
///
/// @param execution the operation this map runs in.
/// @param distortion the model to apply.
/// @param request the grid and the caller-owned planes.
/// @param inverse whether the inverse direction is being mapped.
/// @param operation the public operation, for the failures this reports.
/// @return what the map did, or the first failure.
[[nodiscard]] Result<SipMapReport> computeMap(const ExecutionContext& execution, const SipDistortion& distortion,
                                              const SipMapRequest& request, const bool inverse,
                                              const char* const operation)
{
    if (!execution.valid())
    {
        return distortionFailure(ErrorCode::NotInitialized,
                                 "a coordinate map needs an execution context that holds a "
                                 "lease on an engine context",
                                 operation);
    }
    if (const Status checked = checkMapRequest(request, operation); !checked.ok())
    {
        return checked;
    }

    const std::uint64_t wanted =
        (std::max<std::uint64_t>)(1u, static_cast<std::uint64_t>((std::max)(1u, execution.admittedConcurrency())) *
                                          kMapTilesPerWorker);
    const std::uint64_t rowsPerTile = (static_cast<std::uint64_t>(request.height) + wanted - 1u) / wanted;
    ParallelTileRequest tiles;
    tiles.width = request.width;
    tiles.height = request.height;
    tiles.tileWidth = request.width;
    tiles.tileHeight = static_cast<std::uint32_t>(
        (std::clamp<std::uint64_t>)(rowsPerTile, 1u, static_cast<std::uint64_t>(kMaximumMapTileHeight)));
    tiles.pixelCost =
        TaskCost{.bytesPerItem = kMapBytesPerPixel,
                 .operationsPerItem = inverse ? kInverseMapOperationsPerPixel : kForwardMapOperationsPerPixel};
    const Result<ParallelTilePlan> plan = planParallelTiles(execution, tiles);
    if (!plan.hasValue())
    {
        return plan.status();
    }

    // The route is a property of the model, so it is resolved here rather than at every pixel, and so
    // is what a Newton step evaluates.
    const SipInverseRoute route = inverseRouteOf(distortion);
    const SipNewtonModel newtonModel = newtonModelOf(distortion);
    MapCounters counters;
    const auto body = [&](const ParallelTile& tile) -> Status {
        MapTally tally;
        if (!inverse)
        {
            mapTile(request, tile, tally, [&](const double x, const double y, double& outX, double& outY, MapTally&) {
                PixelCoordinate corrected{};
                if (!forwardCorrection(distortion, PixelCoordinate{.x = x, .y = y}, corrected))
                {
                    return false;
                }
                outX = corrected.x;
                outY = corrected.y;
                return true;
            });
        }
        else
        {
            switch (route)
            {
            case SipInverseRoute::Identity:
                mapTile(request, tile, tally,
                        [](const double x, const double y, double& outX, double& outY, MapTally& counted) {
                            outX = x;
                            outY = y;
                            ++counted.identityRoute;
                            return true;
                        });
                break;
            case SipInverseRoute::InverseCoefficients:
                mapTile(
                    request, tile, tally,
                    [&](const double x, const double y, double& outX, double& outY, MapTally& counted) {
                        PixelCoordinate corrected{};
                        double residual = 0.0;
                        if (!inverseByCoefficients(distortion, PixelCoordinate{.x = x, .y = y}, corrected, residual))
                        {
                            return false;
                        }
                        outX = corrected.x;
                        outY = corrected.y;
                        ++counted.inverseCoefficientRoute;
                        if (residual > counted.maximumResidual)
                        {
                            counted.maximumResidual = residual;
                        }
                        return true;
                    });
                break;
            case SipInverseRoute::NewtonIteration:
                mapTile(request, tile, tally,
                        [&](const double x, const double y, double& outX, double& outY, MapTally& counted) {
                            PixelCoordinate corrected{};
                            std::uint32_t iterations = 0u;
                            double residual = 0.0;
                            const bool solved = solveNewton(distortion, newtonModel, PixelCoordinate{.x = x, .y = y},
                                                            corrected, iterations, residual);
                            counted.newtonIterations += iterations;
                            if (!solved)
                            {
                                return false;
                            }
                            outX = corrected.x;
                            outY = corrected.y;
                            ++counted.newtonRoute;
                            if (residual > counted.maximumResidual)
                            {
                                counted.maximumResidual = residual;
                            }
                            return true;
                        });
                break;
            }
        }
        publishTally(counters, tally);
        return Status::success();
    };

    if (const Status executed = executeParallelTiles(execution, *plan, std::span<std::byte>{}, body); !executed.ok())
    {
        return executed;
    }
    return readCounters(counters);
}

} // namespace

float unmappedCoordinate() noexcept
{
    return std::numeric_limits<float>::quiet_NaN();
}

bool isUnmappedCoordinate(const float value) noexcept
{
    return std::isnan(value);
}

const char* sipInverseRouteName(const SipInverseRoute route) noexcept
{
    switch (route)
    {
    case SipInverseRoute::Identity:
        return "Identity";
    case SipInverseRoute::InverseCoefficients:
        return "InverseCoefficients";
    case SipInverseRoute::NewtonIteration:
        return "NewtonIteration";
    }
    return "UnknownSipInverseRoute";
}

Result<SipDistortion> SipDistortion::create(const SipDistortionRequest& request)
{
    if (!std::isfinite(request.referencePixel.x) || !std::isfinite(request.referencePixel.y))
    {
        return distortionFailure(ErrorCode::InvalidArgument,
                                 "a distortion model's reference pixel must be two finite numbers", kCreateOperation);
    }

    const SipPolynomial* const components[] = {&request.forwardX, &request.forwardY, &request.inverseX,
                                               &request.inverseY};
    const char* const names[] = {"forwardX", "forwardY", "inverseX", "inverseY"};
    for (std::size_t component = 0u; component < 4u; ++component)
    {
        const SipPolynomial& polynomial = *components[component];
        if (polynomial.order != 0u && (polynomial.order < kMinimumSipOrder || polynomial.order > kMaximumSipOrder))
        {
            return distortionFailure(ErrorCode::InvalidArgument,
                                     std::string("the ") + names[component] +
                                         " polynomial's order must be zero or between two and six",
                                     kCreateOperation);
        }

        for (std::size_t i = 0u; i < kSipCoefficientRowCount; ++i)
        {
            for (std::size_t j = 0u; j < kSipCoefficientRowCount; ++j)
            {
                const double coefficient = polynomial.coefficients[i][j];
                if (!std::isfinite(coefficient))
                {
                    return distortionFailure(ErrorCode::InvalidArgument,
                                             std::string("the ") + names[component] +
                                                 " polynomial carries a coefficient that is not finite",
                                             kCreateOperation);
                }
                if (coefficient != 0.0 && (i + j) > static_cast<std::size_t>(polynomial.order))
                {
                    return distortionFailure(ErrorCode::InvalidArgument,
                                             std::string("the ") + names[component] +
                                                 " polynomial carries a nonzero coefficient of a degree its order "
                                                 "excludes, which evaluating it would silently discard",
                                             kCreateOperation);
                }
            }
        }
    }

    return SipDistortion(request);
}

Result<PixelCoordinate> applySipForward(const SipDistortion& distortion, const PixelCoordinate& pixel)
{
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y))
    {
        return distortionFailure(ErrorCode::InvalidArgument, "a corrected position must be two finite numbers",
                                 kForwardOperation);
    }

    PixelCoordinate corrected{};
    if (!forwardCorrection(distortion, pixel, corrected))
    {
        return distortionFailure(ErrorCode::OutOfRange,
                                 "the forward distortion polynomial has no finite value at that position",
                                 kForwardOperation);
    }
    return corrected;
}

Result<SipInverseResult> applySipInverse(const SipDistortion& distortion, const PixelCoordinate& pixel)
{
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y))
    {
        return distortionFailure(ErrorCode::InvalidArgument, "a corrected position must be two finite numbers",
                                 kInverseOperation);
    }

    SipInverseResult result{};
    if (!inverseCorrection(distortion, pixel, result))
    {
        if (result.route == SipInverseRoute::NewtonIteration)
        {
            return distortionFailure(ErrorCode::InvalidData,
                                     "the forward distortion polynomial does not invert at that position",
                                     kInverseOperation);
        }
        return distortionFailure(ErrorCode::OutOfRange,
                                 "the inverse distortion polynomial has no finite value at that position",
                                 kInverseOperation);
    }
    return result;
}

Result<SipMapReport> computeSipForwardMap(const ExecutionContext& execution, const SipDistortion& distortion,
                                          const SipMapRequest& request)
{
    return computeMap(execution, distortion, request, false, kForwardMapOperation);
}

Result<SipMapReport> computeSipInverseMap(const ExecutionContext& execution, const SipDistortion& distortion,
                                          const SipMapRequest& request)
{
    return computeMap(execution, distortion, request, true, kInverseMapOperation);
}

} // namespace an

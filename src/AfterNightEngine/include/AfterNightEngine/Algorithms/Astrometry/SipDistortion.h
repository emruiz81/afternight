// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The polynomial plate distortion, its two directions, and its maps.
///
/// A plate solve of a real optical system does not end at a linear matrix: the field is bent, and
/// the Simple Imaging Polynomial convention describes that bend as two polynomials in the offset
/// from the reference pixel. This header owns that model as a value with a checked order and finite
/// coefficients, rather than as a bag of header cards a reader re-derives.
///
/// **One pixel convention, stated here.** Every position in this header is an engine pixel
/// coordinate: the centre of the first pixel is (0.5, 0.5), the same convention `DetectedStar` and
/// `StarAlignment` use, so a detected star needs no conversion before it is corrected. The FITS
/// spelling, whose `CRPIX` counts from one at pixel centres, is translated once by the card bridge
/// in `Algorithms/Astrometry/WcsCards.h` and never again.
///
/// **The two directions are not each other's inverse by assumption.** The forward polynomials are
/// what a solver fits; the inverse polynomials are an approximation a writer may or may not have
/// included. When they are absent this header solves the forward model by Newton iteration rather
/// than pretending the correction is small enough to ignore, and it reports which of the three
/// routes an answer came from.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <array>
#include <cstdint>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in Runtime/ExecutionContext.h.
class ExecutionContext;

/// A position in an image's pixel grid, in pixels from the image origin.
///
/// The centre of the first pixel is (0.5, 0.5), so pixel column `i` covers `[i, i + 1)`. Fractional
/// and outside the image are both ordinary: a solution of a cropped frame routinely has its
/// reference pixel off the canvas.
struct PixelCoordinate
{
    /// Horizontal position, in pixels from the left edge.
    double x = 0.0;

    /// Vertical position, in pixels from the origin row's edge.
    double y = 0.0;

    /// Whether two positions are the same two numbers.
    ///
    /// @param left one position.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const PixelCoordinate& left,
                                                   const PixelCoordinate& right) noexcept = default;
};

/// The highest polynomial order this engine reads or writes.
///
/// Six is above every order a plate solver in this domain produces; a twenty-eight-term polynomial
/// already describes more field curvature than an amateur optical train has. It is a ceiling rather
/// than a limit that grows, because the coefficient table is a fixed-size member of a public value
/// record.
inline constexpr std::uint32_t kMaximumSipOrder = 6u;

/// The lowest polynomial order that describes anything.
///
/// Orders zero and one are absorbed by the linear world-coordinate matrix, so a distortion
/// polynomial of order below two is a distortion model that is not one. The convention writes such
/// a model by omitting it.
inline constexpr std::uint32_t kMinimumSipOrder = 2u;

/// How many coefficients one polynomial's table holds along each axis.
inline constexpr std::size_t kSipCoefficientRowCount = static_cast<std::size_t>(kMaximumSipOrder) + 1u;

/// One component of a polynomial distortion model.
///
/// A plain aggregate: an order, and the coefficient table it selects from. Entry `[i][j]` multiplies
/// `u^i * v^j`, where `u` and `v` are the offsets from the model's reference pixel. Only entries
/// with `i + j <= order` participate; `SipDistortion::create` refuses a nonzero coefficient outside
/// that triangle rather than dropping it silently.
struct SipPolynomial
{
    /// The polynomial's order: zero when the component is absent, otherwise in
    /// `[kMinimumSipOrder, kMaximumSipOrder]`.
    std::uint32_t order = 0u;

    /// The coefficient table, indexed `[i][j]` for the term in `u^i * v^j`.
    std::array<std::array<double, kSipCoefficientRowCount>, kSipCoefficientRowCount> coefficients{};

    /// Whether two components are the same order and the same numbers.
    ///
    /// Exact equality of every table entry, including the ones the order excludes, which is what a
    /// round-trip comparison of a parsed model wants.
    ///
    /// @param left one component.
    /// @param right the other.
    /// @return true when the orders and every coefficient are equal.
    [[nodiscard]] friend constexpr bool operator==(const SipPolynomial& left, const SipPolynomial& right) = default;
};

/// What a solver or a header reports about one frame's distortion, before any of it is checked.
///
/// A plain aggregate so a call site names each component rather than passing four coefficient
/// tables positionally and getting the two directions the wrong way round.
struct SipDistortionRequest
{
    /// The pixel the polynomials measure their offsets from, in engine pixel coordinates.
    PixelCoordinate referencePixel{};

    /// The horizontal forward component - the convention's `A`. Maps a distorted position to the
    /// correction that removes the distortion.
    SipPolynomial forwardX{};

    /// The vertical forward component - the convention's `B`.
    SipPolynomial forwardY{};

    /// The horizontal inverse component - the convention's `AP`. Optional: when it is absent the
    /// inverse is solved from the forward model instead.
    SipPolynomial inverseX{};

    /// The vertical inverse component - the convention's `BP`.
    SipPolynomial inverseY{};
};

/// The polynomial distortion of one solved frame.
///
/// Immutable and trivially copyable. There is no way to build one whose order is out of range,
/// whose coefficients are not finite, or which carries a coefficient the order excludes: the
/// constructor is private and `create` is the only way in.
///
/// A default-constructed one has neither direction and corrects nothing. That is the honest state
/// for a frame whose solution is purely linear, and it is why the world-coordinate model holds one
/// by value rather than as an optional.
class SipDistortion
{
public:
    /// A distortion that corrects nothing.
    ///
    /// The state of a frame whose solution has no polynomial term. Both directions are absent and
    /// every correction is the identity.
    constexpr SipDistortion() noexcept = default;

    /// A distortion with these polynomials, when they describe a model.
    ///
    /// @param request what the solver or the header reported.
    /// @return the model; `ErrorCode::InvalidArgument` when the reference pixel is not finite, an
    ///         order is neither zero nor within `[kMinimumSipOrder, kMaximumSipOrder]`, a
    ///         coefficient is not finite, or a nonzero coefficient sits outside its polynomial's
    ///         order triangle.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<SipDistortion> create(const SipDistortionRequest& request);

    /// Whether this model has forward polynomials.
    ///
    /// @return true when either forward component has a nonzero order.
    [[nodiscard]] constexpr bool hasForwardModel() const noexcept
    {
        return m_forwardX.order != 0u || m_forwardY.order != 0u;
    }

    /// Whether this model has inverse polynomials.
    ///
    /// @return true when either inverse component has a nonzero order.
    [[nodiscard]] constexpr bool hasInverseModel() const noexcept
    {
        return m_inverseX.order != 0u || m_inverseY.order != 0u;
    }

    /// The pixel the polynomials measure their offsets from.
    ///
    /// @return the reference pixel, in engine pixel coordinates.
    [[nodiscard]] constexpr PixelCoordinate referencePixel() const noexcept
    {
        return m_referencePixel;
    }

    /// The horizontal forward component.
    ///
    /// @return the component, whose order is zero when it is absent.
    [[nodiscard]] constexpr const SipPolynomial& forwardX() const noexcept
    {
        return m_forwardX;
    }

    /// The vertical forward component.
    ///
    /// @return the component, whose order is zero when it is absent.
    [[nodiscard]] constexpr const SipPolynomial& forwardY() const noexcept
    {
        return m_forwardY;
    }

    /// The horizontal inverse component.
    ///
    /// @return the component, whose order is zero when it is absent.
    [[nodiscard]] constexpr const SipPolynomial& inverseX() const noexcept
    {
        return m_inverseX;
    }

    /// The vertical inverse component.
    ///
    /// @return the component, whose order is zero when it is absent.
    [[nodiscard]] constexpr const SipPolynomial& inverseY() const noexcept
    {
        return m_inverseY;
    }

    /// Every field, in a form `create` accepts.
    ///
    /// @return the request this model was built from.
    [[nodiscard]] constexpr SipDistortionRequest toRequest() const
    {
        return SipDistortionRequest{.referencePixel = m_referencePixel,
                                    .forwardX = m_forwardX,
                                    .forwardY = m_forwardY,
                                    .inverseX = m_inverseX,
                                    .inverseY = m_inverseY};
    }

    /// Whether two models say the same thing.
    ///
    /// @param left one model.
    /// @param right the other.
    /// @return true when the reference pixels and all four components are equal.
    [[nodiscard]] friend constexpr bool operator==(const SipDistortion& left, const SipDistortion& right) = default;

private:
    /// Builds from a request whose numbers have already been checked.
    ///
    /// @param request the validated model.
    constexpr explicit SipDistortion(const SipDistortionRequest& request)
        : m_referencePixel(request.referencePixel), m_forwardX(request.forwardX), m_forwardY(request.forwardY),
          m_inverseX(request.inverseX), m_inverseY(request.inverseY)
    {
    }

    /// The pixel the polynomials measure their offsets from.
    PixelCoordinate m_referencePixel{};

    /// The horizontal forward component.
    SipPolynomial m_forwardX{};

    /// The vertical forward component.
    SipPolynomial m_forwardY{};

    /// The horizontal inverse component.
    SipPolynomial m_inverseX{};

    /// The vertical inverse component.
    SipPolynomial m_inverseY{};
};

/// Which of the three ways an inverse correction was reached.
enum class SipInverseRoute : std::uint8_t
{
    /// The model has no polynomial at all, so the position was returned unchanged.
    Identity,

    /// The model's own inverse polynomials were evaluated once.
    InverseCoefficients,

    /// The model has forward polynomials and no inverse ones, so the forward model was solved by
    /// Newton iteration.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NewtonIteration
};

/// Whether a value names one of the three routes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param route the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSipInverseRoute(SipInverseRoute route) noexcept
{
    return static_cast<std::uint8_t>(route) <= static_cast<std::uint8_t>(SipInverseRoute::NewtonIteration);
}

/// The route's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param route the route to name. A value outside the enumeration is named
///        `"UnknownSipInverseRoute"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* sipInverseRouteName(SipInverseRoute route) noexcept;

/// The round-trip distance, in pixels, at which a Newton solution is accepted and the iteration
/// stops.
///
/// The iteration stops as soon as the forward model maps its current estimate to within this
/// distance of the requested position: a hundredth of a hundredth of a pixel, far below any
/// resampling this feeds and far above the point where a double-precision polynomial stops
/// improving. A solve that runs out of steps first is still accepted when its final round trip is
/// within ten times this distance.
inline constexpr double kSipInverseTolerancePixels = 1.0e-4;

/// The most Newton steps one inverse correction takes before it is judged on its residual.
inline constexpr std::uint32_t kMaximumSipNewtonIterations = 16u;

/// One inverse correction and how it was reached.
struct SipInverseResult
{
    /// The corrected position, in engine pixel coordinates.
    PixelCoordinate pixel{};

    /// Which route produced it.
    SipInverseRoute route = SipInverseRoute::Identity;

    /// How many Newton steps ran. Zero for the other two routes.
    std::uint32_t iterations = 0u;

    /// The round-trip distance the forward model leaves at the corrected position, in pixels.
    ///
    /// Measured only when a forward model exists; not-a-number otherwise, because a model with no
    /// forward polynomials has nothing to check its own inverse against.
    double residualPixels = 0.0;
};

/// Applies the forward polynomials to one position.
///
/// The forward direction of the convention: a distorted position in, the position the linear
/// world-coordinate matrix should be given out. A model with no forward polynomials returns its
/// argument, which is what makes an inverse-only model usable without a branch at every call site.
///
/// The calculation is scalar Float64. Plate distortion is one of the precision policy's named
/// Float64 paths: a sixth-order polynomial in offsets of a thousand pixels reaches magnitudes a
/// Float32 accumulation cannot hold apart, and the result conditions sub-pixel resampling.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param distortion the model to apply.
/// @param pixel the distorted position, in engine pixel coordinates.
/// @return the corrected position; `ErrorCode::InvalidArgument` when `pixel` is not finite; or
///         `ErrorCode::OutOfRange` when the polynomial overflows to a non-finite correction.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate> applySipForward(const SipDistortion& distortion,
                                                                           const PixelCoordinate& pixel);

/// Undoes the forward polynomials at one position.
///
/// Takes the model's own inverse polynomials when it has them, and solves the forward model by
/// Newton iteration when it does not. A model with neither direction returns its argument. An
/// answer from the inverse polynomials is returned as they give it: its round trip through the
/// forward model is measured and reported, never used to refuse it.
///
/// The Newton solve always starts from the requested position. It never seeds from a neighbouring
/// answer: a seed that depends on which pixel was solved first would make one pixel's result depend
/// on the traversal order, and the traversal order of a map is a scheduling decision.
///
/// The calculation is scalar Float64, for the reason `applySipForward` states.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param distortion the model to undo.
/// @param pixel the undistorted position, in engine pixel coordinates.
/// @return the corrected position and its route; `ErrorCode::InvalidArgument` when `pixel` is not
///         finite; `ErrorCode::OutOfRange` when the inverse polynomials overflow to a non-finite
///         value; or `ErrorCode::InvalidData` when the Newton solve fails - a forward polynomial or
///         a step overflows, the Jacobian determinant falls below 1e-12 in magnitude, or
///         `kMaximumSipNewtonIterations` steps end more than ten times
///         `kSipInverseTolerancePixels` from the target - which is a model that does not invert at
///         that position rather than a malformed call.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SipInverseResult> applySipInverse(const SipDistortion& distortion,
                                                                            const PixelCoordinate& pixel);

/// The value a map writes for a pixel it could not correct.
///
/// A quiet not-a-number: the only value in the range of a coordinate map that no resampler will read
/// as a position.
///
/// A function rather than a constant, because no correct caller compares against it: every
/// comparison with a not-a-number is false, so this value exists to be written and
/// `isUnmappedCoordinate` exists to be asked.
///
/// Thread-safe and allocation-free.
///
/// @return the sentinel.
[[nodiscard]] AFTERNIGHTENGINE_API float unmappedCoordinate() noexcept;

/// Whether one entry of a coordinate map is a pixel the map could not correct.
///
/// Thread-safe and allocation-free.
///
/// @param value one entry of either plane.
/// @return true for any not-a-number, which includes the sentinel `unmappedCoordinate` writes.
[[nodiscard]] AFTERNIGHTENGINE_API bool isUnmappedCoordinate(float value) noexcept;

/// What one map computation is asked to fill.
///
/// The caller owns both planes. They are separate spans rather than one interleaved buffer because
/// every consumer of a coordinate map in this engine reads the two axes independently.
struct SipMapRequest
{
    /// Width of the mapped grid, in pixels. Must not be zero.
    std::uint32_t width = 0u;

    /// Height of the mapped grid, in pixels. Must not be zero.
    std::uint32_t height = 0u;

    /// Horizontal destinations, row-major and exactly `width * height` long.
    std::span<float> xMap{};

    /// Vertical destinations, row-major and exactly `width * height` long.
    std::span<float> yMap{};
};

/// What a map computation did.
///
/// The three route counters and the Newton step count describe the inverse direction, which is the
/// one with three routes; a forward map leaves them at zero because it has one.
struct SipMapReport
{
    /// Pixels that received a finite position.
    std::uint64_t mapped = 0u;

    /// Pixels left holding the unmapped sentinel.
    std::uint64_t unmapped = 0u;

    /// Pixels answered by returning the position unchanged.
    std::uint64_t identityRoute = 0u;

    /// Pixels answered by the model's own inverse polynomials.
    std::uint64_t inverseCoefficientRoute = 0u;

    /// Pixels answered by a Newton solve of the forward model.
    std::uint64_t newtonRoute = 0u;

    /// Newton steps run across every pixel that took that route.
    std::uint64_t newtonIterations = 0u;

    /// The largest round-trip distance any measured pixel left, in pixels. Zero when nothing was
    /// measured, which is what a map with no forward model reports.
    double maximumResidualPixels = 0.0;
};

/// Fills a grid with the forward-corrected position of every pixel.
///
/// One entry per pixel of the requested grid, in row-major order, holding what `applySipForward`
/// returns for that pixel's centre-based coordinate, rounded to Float32 - column `x` and row `y` map
/// the position `(x + 0.5, y + 0.5)`. A pixel the model cannot correct receives
/// `unmappedCoordinate` in both planes and is counted rather than failing the whole map, because
/// one overflowing corner does not invalidate the rest of a frame.
///
/// Runs on the operation's scheduler grant and checks cancellation per tile. Allocation-free: the
/// counters are shared atomics a tile publishes into once, not per pixel and not per worker.
///
/// @param execution the operation this map runs in.
/// @param distortion the model to apply.
/// @param request the grid and the caller-owned planes to fill.
/// @return what the map did; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a zero dimension; `ErrorCode::IncompatibleArguments`
///         when either plane is not exactly `width * height` long; `ErrorCode::Aliasing` when the
///         two planes overlap; `ErrorCode::LimitExceeded` when the pixel count does not fit; or the
///         scheduler and cancellation failures.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SipMapReport>
computeSipForwardMap(const ExecutionContext& execution, const SipDistortion& distortion, const SipMapRequest& request);

/// Fills a grid with the inverse-corrected position of every pixel.
///
/// The counterpart of `computeSipForwardMap`, holding what `applySipInverse` returns for each
/// pixel's centre-based coordinate. This is the expensive direction when the model carries no
/// inverse polynomials: every pixel is then its own Newton solve, and the report's route counters
/// are what say so rather than a log line nobody reads.
///
/// Runs on the operation's scheduler grant and checks cancellation per tile. Allocation-free, as
/// the forward map is.
///
/// @param execution the operation this map runs in.
/// @param distortion the model to undo.
/// @param request the grid and the caller-owned planes to fill.
/// @return what the map did, with the same failures `computeSipForwardMap` returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SipMapReport>
computeSipInverseMap(const ExecutionContext& execution, const SipDistortion& distortion, const SipMapRequest& request);

} // namespace an

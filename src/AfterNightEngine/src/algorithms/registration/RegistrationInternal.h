// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - shared implementation details of the registration domain.
//
// The engine owns no globals and no operation allocates behind the caller's back, so a container
// this domain needs for the length of one call takes its bytes from the execution's engine context.
// The standard containers ask for memory through an allocator that cannot return a failure, so the
// adapter below turns the context's refusal into an exception and each public entry point turns it
// back into the failure it already was. That translation happens here rather than escaping, which
// is what `Core/ExceptionBoundary.h` asks of every adapter boundary.

#pragma once

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <memory_resource>
#include <utility>

/// The registration domain's private machinery: the engine-context allocator adapter and the
/// transform arithmetic a coordinate map runs at every pixel.
namespace an::registration_internal
{

/// The engine context's refusal, carried out of an allocator that cannot return one.
class ContextAllocationFailure
{
public:
    /// Wraps a refusal.
    ///
    /// @param status the failure the memory resource returned.
    explicit ContextAllocationFailure(Status status) noexcept : m_status(std::move(status)) {}

    /// The refusal this failure carries.
    ///
    /// @return the original failure, with its budget context intact.
    [[nodiscard]] const Status& status() const noexcept
    {
        return m_status;
    }

private:
    /// The refusal.
    Status m_status;
};

/// A standard allocator over one execution's engine-context memory.
///
/// Counts what it holds so an operation can report the temporary bytes it needed, which is the
/// number a caller sizing a batch actually wants.
class ExecutionMemory final : public std::pmr::memory_resource
{
public:
    /// Binds to a context's memory resource.
    ///
    /// @param memory the resource, which must outlive this adapter.
    explicit ExecutionMemory(MemoryResource& memory) noexcept : m_memory(memory) {}

    /// The most bytes this adapter held at once.
    ///
    /// @return the peak, in bytes.
    [[nodiscard]] std::uint64_t peakBytes() const noexcept
    {
        return m_peakBytes;
    }

private:
    /// Takes bytes from the engine context.
    ///
    /// @param bytes how many bytes are wanted.
    /// @param alignment the required alignment.
    /// @return the memory; throws `ContextAllocationFailure` when the context refuses.
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        Result<void*> taken = m_memory.allocate(bytes, alignment);
        if (!taken.hasValue())
        {
            throw ContextAllocationFailure(taken.status());
        }
        m_liveBytes += bytes;
        m_peakBytes = (std::max)(m_peakBytes, m_liveBytes);
        return *taken;
    }

    /// Returns bytes to the engine context.
    ///
    /// @param pointer the memory to release.
    /// @param bytes how many bytes it holds.
    /// @param alignment the alignment it was taken with.
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        m_memory.deallocate(pointer, bytes, alignment);
        m_liveBytes -= bytes;
    }

    /// Whether another resource is this one.
    ///
    /// @param other the resource to compare against.
    /// @return true only for this object.
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    /// The engine context's resource.
    MemoryResource& m_memory;

    /// Bytes currently held.
    std::uint64_t m_liveBytes = 0u;

    /// The most bytes held at once.
    std::uint64_t m_peakBytes = 0u;
};

// The arithmetic below is defined in this header rather than beside the public entry points that
// wrap it. A coordinate map calls it once for every destination pixel, from a different translation
// unit, and a call the compiler cannot see through costs more than the arithmetic inside it. The
// public entry points still hold the argument checking and the returned failures, so what moved
// here is the arithmetic alone and there is one text of it.

/// The smallest projective denominator a mapping will divide by.
///
/// The same number the linear determinant is judged against: below it the homogeneous coordinate has
/// lost every significant digit it had.
inline constexpr double kMinimumDenominator = 1.0e-12;

/// The smallest Jacobian determinant a Newton step will divide by.
inline constexpr double kSingularJacobian = 1.0e-12;

/// Whether a position holds two finite numbers.
///
/// @param pixel the position to inspect.
/// @return true when both components are finite.
[[nodiscard]] inline bool isFinitePixel(const PixelCoordinate& pixel) noexcept
{
    return std::isfinite(pixel.x) && std::isfinite(pixel.y);
}

/// One polynomial evaluation's powers of the normalized offsets.
///
/// The entries carry no initializer, and this is the one place in the domain where that is true.
/// `fillPowerTable` writes every entry from zero through the order it is given, `evaluatePolynomial`
/// reads no entry above that order, and the two are the only code that touches this table - so
/// zeroing all ten doubles would be ten stores discarded for every evaluation, and a coordinate map
/// evaluates this twice for every pixel of a megapixel grid.
struct PowerTable
{
    /// `x[i]` is the normalized horizontal offset raised to `i`, for `i` up to the order filled.
    std::array<double, kMaximumRegistrationPolynomialOrder + 1u> x;

    /// `y[j]` is the normalized vertical offset raised to `j`, for `j` up to the order filled.
    std::array<double, kMaximumRegistrationPolynomialOrder + 1u> y;
};

/// Fills the powers of one normalized position into a caller-owned table.
///
/// Filled in place rather than returned, because a coordinate map evaluates this twice for every
/// pixel of a megapixel grid and a returned table is a table zeroed and then copied.
///
/// @param nx the normalized horizontal offset.
/// @param ny the normalized vertical offset.
/// @param order the highest power needed.
/// @param powers the table to fill; entries above `order` are left as they were.
inline void fillPowerTable(const double nx, const double ny, const std::uint32_t order, PowerTable& powers) noexcept
{
    powers.x[0] = 1.0;
    powers.y[0] = 1.0;
    for (std::size_t index = 1u; index <= static_cast<std::size_t>(order); ++index)
    {
        powers.x[index] = powers.x[index - 1u] * nx;
        powers.y[index] = powers.y[index - 1u] * ny;
    }
}

/// One polynomial evaluation, with the derivatives a Newton step needs.
struct PolynomialValue
{
    /// The corrected position, in the linear map's own output coordinates.
    PixelCoordinate pixel{};

    /// The partial derivative of the horizontal output with respect to the horizontal input.
    double dxdx = 1.0;

    /// The partial derivative of the horizontal output with respect to the vertical input.
    double dxdy = 0.0;

    /// The partial derivative of the vertical output with respect to the horizontal input.
    double dydx = 0.0;

    /// The partial derivative of the vertical output with respect to the vertical input.
    double dydy = 1.0;
};

/// The template argument that asks for the order a polynomial carries rather than a fixed one.
inline constexpr std::uint32_t kDynamicPolynomialOrder = 0u;

/// Evaluates a polynomial correction and, when asked, its Jacobian.
///
/// Terms are summed in ascending total degree and, within one degree, in ascending vertical degree.
/// The order is part of the contract: a floating-point sum is not associative, so the sequence is
/// what makes two runs of this engine agree with each other. `CompiledOrder` changes nothing about
/// that sequence; it only decides whether the trip count is a constant this text is compiled for or
/// a number it reads.
///
/// @tparam CompiledOrder the order to compile the term loop for, or `kDynamicPolynomialOrder` to
///         take it from the polynomial at run time.
/// @param polynomial the correction, whose order is nonzero.
/// @param inverseScale `inverseNormalizationScale` of the polynomial.
/// @param pixel the linear map's output.
/// @param derivatives whether the Jacobian is wanted.
/// @param value the corrected position and, when asked, its derivatives.
/// @return false when the sum or a derivative is not finite.
template <std::uint32_t CompiledOrder>
[[nodiscard]] inline bool evaluatePolynomialTerms(const RegistrationPolynomial& polynomial,
                                                  const PixelCoordinate& inverseScale, const PixelCoordinate& pixel,
                                                  const bool derivatives, PolynomialValue& value) noexcept
{
    // Constant whenever the caller named an order, so the loops below are compiled with their trip
    // count known and the per-term degree tests decided.
    const std::uint32_t order = CompiledOrder == kDynamicPolynomialOrder ? polynomial.order : CompiledOrder;
    const double scaleX = polynomial.normalizationScale.x;
    const double scaleY = polynomial.normalizationScale.y;
    const double inverseScaleX = inverseScale.x;
    const double inverseScaleY = inverseScale.y;
    const double nx = (pixel.x - polynomial.normalizationOrigin.x) * inverseScaleX;
    const double ny = (pixel.y - polynomial.normalizationOrigin.y) * inverseScaleY;
    PowerTable powers;
    fillPowerTable(nx, ny, order, powers);

    double sumX = 0.0;
    double sumY = 0.0;
    double dxdnx = 0.0;
    double dxdny = 0.0;
    double dydnx = 0.0;
    double dydny = 0.0;
    std::size_t index = 0u;
    // The two loops are one text with the derivative accumulation lifted out of the term loop
    // rather than tested inside it, because the term loop runs twice per pixel of a coordinate map.
    if (!derivatives)
    {
        for (std::uint32_t degree = 0u; degree <= order; ++degree)
        {
            for (std::uint32_t verticalDegree = 0u; verticalDegree <= degree; ++verticalDegree)
            {
                const double term = powers.x[degree - verticalDegree] * powers.y[verticalDegree];
                sumX += polynomial.x[index] * term;
                sumY += polynomial.y[index] * term;
                ++index;
            }
        }
    }
    else
    {
        for (std::uint32_t degree = 0u; degree <= order; ++degree)
        {
            for (std::uint32_t verticalDegree = 0u; verticalDegree <= degree; ++verticalDegree)
            {
                const std::uint32_t horizontalDegree = degree - verticalDegree;
                const double term = powers.x[horizontalDegree] * powers.y[verticalDegree];
                const double coefficientX = polynomial.x[index];
                const double coefficientY = polynomial.y[index];
                sumX += coefficientX * term;
                sumY += coefficientY * term;
                if (horizontalDegree > 0u)
                {
                    const double horizontalTerm = static_cast<double>(horizontalDegree) *
                                                  powers.x[horizontalDegree - 1u] * powers.y[verticalDegree];
                    dxdnx += coefficientX * horizontalTerm;
                    dydnx += coefficientY * horizontalTerm;
                }
                if (verticalDegree > 0u)
                {
                    const double verticalTerm = static_cast<double>(verticalDegree) * powers.x[horizontalDegree] *
                                                powers.y[verticalDegree - 1u];
                    dxdny += coefficientX * verticalTerm;
                    dydny += coefficientY * verticalTerm;
                }
                ++index;
            }
        }
    }

    value.pixel = PixelCoordinate{.x = polynomial.normalizationOrigin.x + sumX * scaleX,
                                  .y = polynomial.normalizationOrigin.y + sumY * scaleY};
    if (!isFinitePixel(value.pixel))
    {
        return false;
    }
    if (!derivatives)
    {
        return true;
    }

    // The scale factors cancel on the diagonal and cross over off it, because a normalized offset
    // on one axis is divided by that axis's own scale and multiplied back by the output's.
    value.dxdx = dxdnx;
    value.dxdy = scaleX * inverseScaleY * dxdny;
    value.dydx = scaleY * inverseScaleX * dydnx;
    value.dydy = dydny;
    return std::isfinite(value.dxdx) && std::isfinite(value.dxdy) && std::isfinite(value.dydx) &&
           std::isfinite(value.dydy);
}

/// The reciprocals of a polynomial's normalization scale.
///
/// Normalizing and, when they are wanted, the Jacobian's cross terms all divide by the same two
/// numbers, so an evaluation multiplies by these two reciprocals rather than dividing four times. A
/// caller evaluating one polynomial at every pixel of a map takes them once: they are the same two
/// numbers each time, and a division sits in the dependency chain of every evaluation.
///
/// @param polynomial the correction.
/// @return one over each axis's normalization scale.
[[nodiscard]] inline PixelCoordinate inverseNormalizationScale(const RegistrationPolynomial& polynomial) noexcept
{
    return PixelCoordinate{.x = 1.0 / polynomial.normalizationScale.x, .y = 1.0 / polynomial.normalizationScale.y};
}

/// Evaluates a polynomial correction and, when asked, its Jacobian, with the reciprocals of its
/// normalization scale already taken.
///
/// The term loop's trip count is the polynomial's order, which is two, three or four. Compiled for
/// each of those the loop is unrolled and every per-term degree test is decided; compiled for a
/// number it reads it can be neither, and a coordinate map runs this twice for every pixel of a
/// megapixel grid. Each case below is the same text with its trip count named, and the last answers
/// any other order without one.
///
/// @param polynomial the correction, whose order is nonzero.
/// @param inverseScale `inverseNormalizationScale` of the polynomial.
/// @param pixel the linear map's output.
/// @param derivatives whether the Jacobian is wanted.
/// @param value the corrected position and, when asked, its derivatives.
/// @return false when the sum or a derivative is not finite.
[[nodiscard]] inline bool evaluatePolynomial(const RegistrationPolynomial& polynomial,
                                             const PixelCoordinate& inverseScale, const PixelCoordinate& pixel,
                                             const bool derivatives, PolynomialValue& value) noexcept
{
    switch (polynomial.order)
    {
    case 2u:
        return evaluatePolynomialTerms<2u>(polynomial, inverseScale, pixel, derivatives, value);
    case 3u:
        return evaluatePolynomialTerms<3u>(polynomial, inverseScale, pixel, derivatives, value);
    case 4u:
        return evaluatePolynomialTerms<4u>(polynomial, inverseScale, pixel, derivatives, value);
    default:
        return evaluatePolynomialTerms<kDynamicPolynomialOrder>(polynomial, inverseScale, pixel, derivatives, value);
    }
}

/// Evaluates a polynomial correction and, when asked, its Jacobian.
///
/// @param polynomial the correction, whose order is nonzero.
/// @param pixel the linear map's output.
/// @param derivatives whether the Jacobian is wanted.
/// @param value the corrected position and, when asked, its derivatives.
/// @return false when the sum or a derivative is not finite.
[[nodiscard]] inline bool evaluatePolynomial(const RegistrationPolynomial& polynomial, const PixelCoordinate& pixel,
                                             const bool derivatives, PolynomialValue& value) noexcept
{
    return evaluatePolynomial(polynomial, inverseNormalizationScale(polynomial), pixel, derivatives, value);
}

/// Applies the linear part of a transform to one position.
///
/// @param matrix the row-major homogeneous coefficients.
/// @param projective whether the last row participates.
/// @param pixel the position to map.
/// @param out the mapped position.
/// @return false when the denominator vanishes or the result is not finite.
[[nodiscard]] inline bool applyLinear(const std::array<double, 9>& matrix, const bool projective,
                                      const PixelCoordinate& pixel, PixelCoordinate& out) noexcept
{
    const double numeratorX = matrix[0] * pixel.x + matrix[1] * pixel.y + matrix[2];
    const double numeratorY = matrix[3] * pixel.x + matrix[4] * pixel.y + matrix[5];
    if (!projective)
    {
        // A division by one is exact, so an affine map skips both. They are not free: an inverse
        // through a polynomial applies this three times on each pixel's dependency chain.
        out = PixelCoordinate{.x = numeratorX, .y = numeratorY};
        return isFinitePixel(out);
    }
    const double denominator = matrix[6] * pixel.x + matrix[7] * pixel.y + matrix[8];
    if (!std::isfinite(numeratorX) || !std::isfinite(numeratorY) || !std::isfinite(denominator) ||
        std::abs(denominator) < kMinimumDenominator)
    {
        return false;
    }
    out = PixelCoordinate{.x = numeratorX / denominator, .y = numeratorY / denominator};
    return isFinitePixel(out);
}

/// The Jacobian of the linear part at one position.
///
/// @param matrix the row-major homogeneous coefficients.
/// @param projective whether the last row participates.
/// @param pixel where the derivative is taken.
/// @param value the four partial derivatives, in the order used by `PolynomialValue`.
/// @return false when the denominator vanishes.
[[nodiscard]] inline bool linearJacobian(const std::array<double, 9>& matrix, const bool projective,
                                         const PixelCoordinate& pixel, PolynomialValue& value) noexcept
{
    if (!projective)
    {
        value.dxdx = matrix[0];
        value.dxdy = matrix[1];
        value.dydx = matrix[3];
        value.dydy = matrix[4];
        return true;
    }
    const double denominator = matrix[6] * pixel.x + matrix[7] * pixel.y + matrix[8];
    if (!std::isfinite(denominator) || std::abs(denominator) < kMinimumDenominator)
    {
        return false;
    }
    const double inverse = 1.0 / denominator;
    const double inverseSquared = inverse * inverse;
    const double numeratorX = matrix[0] * pixel.x + matrix[1] * pixel.y + matrix[2];
    const double numeratorY = matrix[3] * pixel.x + matrix[4] * pixel.y + matrix[5];
    value.dxdx = (matrix[0] * denominator - numeratorX * matrix[6]) * inverseSquared;
    value.dxdy = (matrix[1] * denominator - numeratorX * matrix[7]) * inverseSquared;
    value.dydx = (matrix[3] * denominator - numeratorY * matrix[6]) * inverseSquared;
    value.dydy = (matrix[4] * denominator - numeratorY * matrix[7]) * inverseSquared;
    return std::isfinite(value.dxdx) && std::isfinite(value.dxdy) && std::isfinite(value.dydx) &&
           std::isfinite(value.dydy);
}

/// Inverts the linear part of a transform.
///
/// @param transform the transform whose matrix is inverted.
/// @param out the row-major inverse.
/// @return false when the matrix is singular to within `kRegistrationSingularDeterminant`.
[[nodiscard]] inline bool invertLinear(const RegistrationTransform& transform, std::array<double, 9>& out) noexcept
{
    const std::array<double, 9>& matrix = transform.matrix();
    if (transform.linearKind() != RegistrationTransformKind::Projective)
    {
        const double determinant = transform.linearDeterminant();
        if (!std::isfinite(determinant) || std::abs(determinant) < kRegistrationSingularDeterminant)
        {
            return false;
        }
        out[0] = matrix[4] / determinant;
        out[1] = -matrix[1] / determinant;
        out[3] = -matrix[3] / determinant;
        out[4] = matrix[0] / determinant;
        out[2] = -(out[0] * matrix[2] + out[1] * matrix[5]);
        out[5] = -(out[3] * matrix[2] + out[4] * matrix[5]);
        out[6] = 0.0;
        out[7] = 0.0;
        out[8] = 1.0;
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]) && std::isfinite(out[3]) &&
               std::isfinite(out[4]) && std::isfinite(out[5]);
    }

    const double cofactor0 = matrix[4] * matrix[8] - matrix[5] * matrix[7];
    const double cofactor1 = matrix[5] * matrix[6] - matrix[3] * matrix[8];
    const double cofactor2 = matrix[3] * matrix[7] - matrix[4] * matrix[6];
    const double determinant = matrix[0] * cofactor0 + matrix[1] * cofactor1 + matrix[2] * cofactor2;
    if (!std::isfinite(determinant) || std::abs(determinant) < kRegistrationSingularDeterminant)
    {
        return false;
    }
    out[0] = cofactor0 / determinant;
    out[1] = (matrix[2] * matrix[7] - matrix[1] * matrix[8]) / determinant;
    out[2] = (matrix[1] * matrix[5] - matrix[2] * matrix[4]) / determinant;
    out[3] = cofactor1 / determinant;
    out[4] = (matrix[0] * matrix[8] - matrix[2] * matrix[6]) / determinant;
    out[5] = (matrix[2] * matrix[3] - matrix[0] * matrix[5]) / determinant;
    out[6] = cofactor2 / determinant;
    out[7] = (matrix[1] * matrix[6] - matrix[0] * matrix[7]) / determinant;
    out[8] = (matrix[0] * matrix[4] - matrix[1] * matrix[3]) / determinant;
    for (const double value : out)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

/// Solves the composed model for the source position of one reference position.
///
/// @param transform the model, which carries a polynomial.
/// @param target the reference position with the caller's translation already removed.
/// @param start where the iteration begins.
/// @param requireOneRefinement whether an answer inside the tolerance takes one further step first.
/// @param result the source position, the step count and the residual it left.
/// @return false when a Jacobian is singular, an evaluation fails, or the iteration ends outside
///         `kRegistrationInverseFinalTolerancePixels`.
[[nodiscard]] inline bool solveNewton(const RegistrationTransform& transform, const PixelCoordinate& target,
                                      const PixelCoordinate& start, const bool requireOneRefinement,
                                      RegistrationInverseResult& result) noexcept
{
    const bool projective = transform.linearKind() == RegistrationTransformKind::Projective;
    PixelCoordinate current = start;
    bool refined = !requireOneRefinement;
    result.iterations = 0u;
    for (std::uint32_t step = 0u; step < kMaximumRegistrationNewtonIterations; ++step)
    {
        ++result.iterations;
        PixelCoordinate linearOut{};
        if (!applyLinear(transform.matrix(), projective, current, linearOut))
        {
            return false;
        }
        PolynomialValue polynomial;
        if (!evaluatePolynomial(transform.polynomial(), linearOut, true, polynomial))
        {
            return false;
        }
        const double residualX = polynomial.pixel.x - target.x;
        const double residualY = polynomial.pixel.y - target.y;
        // The squared form rather than `std::hypot`, which is a library call with overflow scaling
        // this residual - a distance of a few pixels at most - cannot need, inside a loop that runs
        // twice for every pixel of a coordinate map. The final residual below keeps `std::hypot`,
        // where it is evaluated once.
        const double residual = std::sqrt(residualX * residualX + residualY * residualY);
        result.residualPixels = residual;
        if (residual <= kRegistrationInverseTolerancePixels)
        {
            if (refined)
            {
                result.pixel = current;
                return true;
            }
            // A warm route takes one exact step past the residual rule, so that where it stops does
            // not depend on how good its seed happened to be.
            refined = true;
        }

        PolynomialValue linear;
        if (!linearJacobian(transform.matrix(), projective, current, linear))
        {
            return false;
        }
        const double j00 = polynomial.dxdx * linear.dxdx + polynomial.dxdy * linear.dydx;
        const double j01 = polynomial.dxdx * linear.dxdy + polynomial.dxdy * linear.dydy;
        const double j10 = polynomial.dydx * linear.dxdx + polynomial.dydy * linear.dydx;
        const double j11 = polynomial.dydx * linear.dxdy + polynomial.dydy * linear.dydy;
        const double determinant = j00 * j11 - j01 * j10;
        if (!std::isfinite(determinant) || std::abs(determinant) < kSingularJacobian)
        {
            return false;
        }
        current.x -= (j11 * residualX - j01 * residualY) / determinant;
        current.y -= (j00 * residualY - j10 * residualX) / determinant;
        if (!isFinitePixel(current))
        {
            return false;
        }
    }

    PixelCoordinate linearOut{};
    PolynomialValue polynomial;
    if (!applyLinear(transform.matrix(), projective, current, linearOut) ||
        !evaluatePolynomial(transform.polynomial(), linearOut, false, polynomial))
    {
        return false;
    }
    result.pixel = current;
    result.residualPixels = std::hypot(polynomial.pixel.x - target.x, polynomial.pixel.y - target.y);
    return result.residualPixels <= kRegistrationInverseFinalTolerancePixels;
}

/// The part of an inverse that depends on the transform and not on the position.
///
/// A coordinate map inverts a million positions through one transform, and the linear inverse is
/// the same matrix for every one of them, so the inversion is hoisted out of the map loop, as a
/// value the caller holds rather than as a variable inside one loop.
struct PreparedRegistrationInverse
{
    /// The row-major inverse of the transform's linear part.
    std::array<double, 9> matrix{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// Whether the last matrix row participates when the inverse is applied.
    bool projective = false;

    /// Whether the transform has a linear inverse at all.
    bool usable = false;
};

/// Inverts a transform's linear part once, for every position that will be inverted through it.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @return the prepared inverse, whose `usable` is false when the matrix is singular.
[[nodiscard]] inline PreparedRegistrationInverse
prepareRegistrationInverse(const RegistrationTransform& transform) noexcept
{
    PreparedRegistrationInverse prepared;
    prepared.projective = transform.linearKind() == RegistrationTransformKind::Projective;
    prepared.usable = invertLinear(transform, prepared.matrix);
    return prepared;
}

/// Inverts one position through an already-inverted linear part.
///
/// The arithmetic `mapRegistrationPointInverse` performs, without the per-position argument
/// checking and without building a returned failure - which is what a per-pixel caller needs and
/// what a caller inverting one position wants checked. The public entry point is this function
/// plus those checks, so there is one text and not two.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @param prepared the transform's prepared linear inverse.
/// @param pixel the reference position, already known to be finite.
/// @param postTranslation the offset that was added to the forward result.
/// @param warmStart whether a warm seed may be used at all.
/// @param warmSeed the previous answer, or null.
/// @param result the source position and what the solve cost.
/// @return false when the position does not invert through this transform.
[[nodiscard]] inline bool
invertWithPreparedInverse(const RegistrationTransform& transform, const PreparedRegistrationInverse& prepared,
                          const PixelCoordinate& pixel, const PixelCoordinate& postTranslation, const bool warmStart,
                          const PixelCoordinate* warmSeed, RegistrationInverseResult& result) noexcept
{
    if (!prepared.usable)
    {
        return false;
    }
    const PixelCoordinate target{.x = pixel.x - postTranslation.x, .y = pixel.y - postTranslation.y};
    PixelCoordinate authoritative{};
    if (!applyLinear(prepared.matrix, prepared.projective, target, authoritative))
    {
        return false;
    }

    result = RegistrationInverseResult{};
    if (transform.isLinear())
    {
        result.pixel = authoritative;
        return true;
    }
    if (warmStart && warmSeed != nullptr && isFinitePixel(*warmSeed))
    {
        RegistrationInverseResult warm;
        if (solveNewton(transform, target, *warmSeed, true, warm))
        {
            warm.warmSeedAccepted = true;
            result = warm;
            return true;
        }
    }
    return solveNewton(transform, target, authoritative, false, result);
}

/// Maps one position forward without the per-position argument checking.
///
/// The counterpart of `invertWithPreparedInverse` for the direction that needs no preparation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @param pixel the source position, already known to be finite.
/// @param postTranslation the offset added to the result.
/// @param out the reference position.
/// @return false when the transform does not map the position.
[[nodiscard]] inline bool mapWithoutChecking(const RegistrationTransform& transform, const PixelCoordinate& pixel,
                                             const PixelCoordinate& postTranslation, PixelCoordinate& out) noexcept
{
    if (!applyLinear(transform.matrix(), transform.linearKind() == RegistrationTransformKind::Projective, pixel, out))
    {
        return false;
    }
    if (!transform.isLinear())
    {
        PolynomialValue value;
        if (!evaluatePolynomial(transform.polynomial(), out, false, value))
        {
            return false;
        }
        out = value.pixel;
    }
    out.x += postTranslation.x;
    out.y += postTranslation.y;
    return isFinitePixel(out);
}

} // namespace an::registration_internal

// AfterNightEngine - fitting one point-spread function to one box of samples.
//
// A Levenberg-Marquardt trust-region iteration on the scaled normal equations, with Nielsen damping
// and the "more" diagonal scaling: a fixed initial damping, a step accepted or rejected on the ratio
// of actual to predicted reduction, a stop after fifteen consecutive rejections, and two convergence
// tests. Unlike a general-purpose driver, it never materialises the Jacobian: the linear system is
// solved by Cholesky on the scaled normal equations rather than by a factorisation of the Jacobian
// at every trial. That is what removes the per-star workspace allocation and the per-iteration
// factorisation, and it is why the evaluation itself is a dispatched kernel rather than a callback.
//
// The initial guess is the other half of the algorithm and is not a detail: a star is fitted from
// the trace of its own pixels above half its peak, measured outwards from the box centre, so a
// brighter neighbour sitting inside the same box cannot capture the fit.
//
// **Derived from Siril's PSF fit.** The starting point from the half-maximum trace and its moments, the
// Gaussian and Moffat models and their bounded parameterisation of the axis ratio and the Moffat
// exponent are adapted from Siril (https://siril.org, `siril/src/algos/PSF.c`), whose file states
// `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)` and
// `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)` and is distributed under the GNU
// General Public License, version 3 or any later version; this file is part of that derived work and is
// distributed under the same licence. The trust-region iteration follows the method of the GNU
// Scientific Library's nonlinear least-squares driver that Siril calls, written here without its
// workspace; the normal equations accumulated without a Jacobian, the dispatched evaluation and the
// typed failures are this engine's. The provenance note is `docs/dev/STAR_DETECTION_PROVENANCE.md`.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Stars/PsfFit.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/stars/StarsInternal.h"
#include "kernels/reference/AlgorithmKernels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace an
{

namespace
{

using stars_detail::failure;
using stars_detail::isFinitePositive;
using stars_detail::parameterFailure;
using stars_detail::rangeFailure;

constexpr const char* kFitOperation = "an::fitStarPsf";
constexpr const char* kScaleOperation = "an::psfScaleFromFullWidth";
constexpr const char* kWidthOperation = "an::psfFullWidthFromScale";

/// The scalar authority for both evaluations, shared verbatim with the compiled kernels' tails.
#include "kernels/reference/PsfFitRow.inl"

/// The most consecutive rejected trial steps a trust region takes before it gives up.
constexpr int kMaxRejectedSteps = 15;

/// The initial damping, as a multiple of the largest scaled column norm squared.
constexpr double kInitialDamping = 1.0e-3;

/// The floor the damping's multiplicative decrease is held at after an accepted step.
constexpr double kDampingDecreaseFloor = 0.333333333333333;

/// The smallest radius, in pixels, the half-maximum trace is measured from.
constexpr int kMinimumHalfRadius = 1;

/// The smallest full width at which the moment analysis is used instead of the plain trace.
constexpr int kMinimumMomentWidth = 3;

/// The largest convergence multiplier a caller may ask for.
constexpr std::uint32_t kMaxConvergence = 16u;

/// The angle past which a fitted position angle is a solver artefact rather than a star.
constexpr double kAbsurdAngleDegrees = 10000.0;

/// One over four times the natural logarithm of two, to fifteen significant digits.
constexpr double kInverseFourLogTwo = 0.360673760222241;

/// Degrees in one radian.
[[nodiscard]] constexpr double degreesPerRadian() noexcept
{
    return 180.0 / std::numbers::pi;
}

/// The model, derived once from the solver's parameter vector.
///
/// The absolute value of the major scale, the cosine parameterisation of the roundness and of the
/// exponent and the reciprocals all happen here, so the per-sample evaluation has none of them.
[[nodiscard]] PsfModelTerms termsOf(const double* parameters, std::size_t count, PsfModel model,
                                    double fixedBeta) noexcept
{
    PsfModelTerms terms{};
    terms.background = parameters[0];
    terms.amplitude = parameters[1];
    terms.centreX = parameters[2];
    terms.centreY = parameters[3];
    const double majorScale = std::fabs(parameters[4]);
    const double ratio = 0.5 * (std::cos(parameters[5]) + 1.0);
    const double minorScale = ratio * ratio * majorScale;
    terms.inverseScaleX = 1.0 / majorScale;
    terms.inverseScaleY = 1.0 / minorScale;
    terms.inverseRatio = 1.0 / ratio;
    terms.ratioSine = std::sin(parameters[5]);
    terms.angleCosine = std::cos(parameters[6]);
    terms.angleSine = std::sin(parameters[6]);
    terms.parameterCount = static_cast<std::uint32_t>(count);
    terms.moffat = model != PsfModel::Gaussian;
    if (count > 7u)
    {
        terms.beta = kMoffatBetaCeiling * 0.5 * (std::cos(parameters[7]) + 1.0);
        terms.betaDerivativeScale = 0.5 * kMoffatBetaCeiling * std::sin(parameters[7]);
    }
    else if (terms.moffat)
    {
        terms.beta = fixedBeta;
    }
    return terms;
}

/// Solves the symmetric positive definite system whose upper triangle is `matrix`.
///
/// @param count how many parameters.
/// @param matrix the system, of which only the upper triangle is read.
/// @param rhs the right-hand side.
/// @param solution receives the solution.
/// @return false when the system is not numerically positive definite, leaving `solution` partly
///         written.
[[nodiscard]] bool choleskySolve(std::size_t count, const double (&matrix)[kPsfMaxFitParameters][kPsfMaxFitParameters],
                                 const double* rhs, double* solution) noexcept
{
    double lower[kPsfMaxFitParameters][kPsfMaxFitParameters] = {};
    for (std::size_t column = 0u; column < count; ++column)
    {
        double pivot = matrix[column][column];
        for (std::size_t k = 0u; k < column; ++k)
        {
            pivot -= lower[column][k] * lower[column][k];
        }
        if (!(pivot > 0.0) || !std::isfinite(pivot))
        {
            return false;
        }
        const double diagonal = std::sqrt(pivot);
        lower[column][column] = diagonal;
        const double inverse = 1.0 / diagonal;
        for (std::size_t row = column + 1u; row < count; ++row)
        {
            double sum = matrix[column][row];
            for (std::size_t k = 0u; k < column; ++k)
            {
                sum -= lower[row][k] * lower[column][k];
            }
            lower[row][column] = sum * inverse;
        }
    }
    double intermediate[kPsfMaxFitParameters] = {};
    for (std::size_t row = 0u; row < count; ++row)
    {
        double sum = rhs[row];
        for (std::size_t k = 0u; k < row; ++k)
        {
            sum -= lower[row][k] * intermediate[k];
        }
        intermediate[row] = sum / lower[row][row];
    }
    for (std::size_t row = count; row-- > 0u;)
    {
        double sum = intermediate[row];
        for (std::size_t k = row + 1u; k < count; ++k)
        {
            sum -= lower[k][row] * solution[k];
        }
        solution[row] = sum / lower[row][row];
    }
    return true;
}

/// The diagonal of the inverse normal-equation matrix, which the parameter uncertainties are.
///
/// @param count how many parameters.
/// @param matrix the normal equations, upper triangle.
/// @param diagonal receives the diagonal.
/// @return false when the matrix is singular.
[[nodiscard]] bool covarianceDiagonal(std::size_t count,
                                      const double (&matrix)[kPsfMaxFitParameters][kPsfMaxFitParameters],
                                      double* diagonal) noexcept
{
    for (std::size_t column = 0u; column < count; ++column)
    {
        double unit[kPsfMaxFitParameters] = {};
        double solution[kPsfMaxFitParameters] = {};
        unit[column] = 1.0;
        if (!choleskySolve(count, matrix, unit, solution))
        {
            return false;
        }
        diagonal[column] = solution[column];
    }
    return true;
}

/// What one solver run produced.
struct SolverResult
{
    /// Why it stopped.
    PsfFitOutcome outcome = PsfFitOutcome::NoProgress;

    /// The residual sum of squares at the returned parameters.
    double residualSumOfSquares = 0.0;

    /// How many accepted iterations it took.
    std::uint32_t iterations = 0u;

    /// The normal equations at the returned parameters, which the uncertainties come from.
    PsfNormalEquations system{};

    /// Whether the first evaluation was usable at all.
    bool started = false;
};

/// The two evaluations one fit runs, already selected from the operation's dispatch table.
struct PsfEvaluators
{
    /// The compiled residual run, or null for the scalar authority.
    PsfResidualRunFn residual = nullptr;

    /// The compiled normal-equation run, or null for the scalar authority.
    PsfNormalEquationsRunFn normalEquations = nullptr;

    /// The residual sum of squares of one model over the run.
    [[nodiscard]] double residualOf(const PsfSampleRun& samples, const PsfModelTerms& terms) const noexcept
    {
        return residual != nullptr ? residual(samples, terms) : psfResidualScalarRun(samples, 0u, terms);
    }

    /// The residual sum, matrix and gradient of one model over the run.
    void systemOf(const PsfSampleRun& samples, const PsfModelTerms& terms, PsfNormalEquations& out) const noexcept
    {
        if (normalEquations != nullptr)
        {
            normalEquations(samples, terms, out);
            return;
        }
        out = PsfNormalEquations{};
        psfNormalEquationsScalarAccumulate(samples, 0u, terms, out);
    }
};

/// Runs the trust-region iteration to convergence, an iteration ceiling or a collapsed region.
///
/// @param count how many parameters are moving.
/// @param parameters the vector, updated in place.
/// @param samples the compacted sample run.
/// @param maxIterations the ceiling.
/// @param parameterTolerance the step-size convergence test.
/// @param gradientTolerance the scaled-gradient convergence test.
/// @param model which shape.
/// @param fixedBeta the exponent a fixed-beta Moffat holds.
/// @param evaluators the dispatched evaluations.
/// @return what the run produced.
[[nodiscard]] SolverResult solve(std::size_t count, double* parameters, const PsfSampleRun& samples,
                                 std::uint32_t maxIterations, double parameterTolerance, double gradientTolerance,
                                 PsfModel model, double fixedBeta, const PsfEvaluators& evaluators) noexcept
{
    SolverResult result{};
    PsfNormalEquations current{};
    evaluators.systemOf(samples, termsOf(parameters, count, model, fixedBeta), current);
    if (!std::isfinite(current.residualSumOfSquares))
    {
        return result;
    }
    result.started = true;
    double residualNorm = current.residualSumOfSquares;

    double scaling[kPsfMaxFitParameters] = {};
    double damping = 0.0;
    {
        double largest = -1.0;
        for (std::size_t index = 0u; index < count; ++index)
        {
            double norm = std::sqrt(current.matrix[index][index]);
            if (norm == 0.0)
            {
                norm = 1.0;
            }
            scaling[index] = (std::max)(0.0, norm);
            largest = (std::max)(largest, std::sqrt(current.matrix[index][index]) / scaling[index]);
        }
        damping = kInitialDamping * largest * largest;
    }
    long dampingGrowth = 2;

    double step[kPsfMaxFitParameters] = {};
    double trial[kPsfMaxFitParameters] = {};
    std::uint32_t iteration = 0u;
    bool converged = false;
    while (iteration < maxIterations)
    {
        bool accepted = false;
        bool stalled = false;
        int rejected = 0;
        while (!accepted)
        {
            double system[kPsfMaxFitParameters][kPsfMaxFitParameters] = {};
            double rhs[kPsfMaxFitParameters] = {};
            double solution[kPsfMaxFitParameters] = {};
            for (std::size_t row = 0u; row < count; ++row)
            {
                const double inverseScale = 1.0 / scaling[row];
                for (std::size_t column = row; column < count; ++column)
                {
                    system[row][column] = current.matrix[row][column] * inverseScale / scaling[column];
                }
                system[row][row] += damping;
                rhs[row] = -current.gradient[row] * inverseScale;
            }
            double ratio = -1.0;
            double trialResidual = 0.0;
            if (choleskySolve(count, system, rhs, solution))
            {
                for (std::size_t index = 0u; index < count; ++index)
                {
                    step[index] = solution[index] / scaling[index];
                    trial[index] = parameters[index] + step[index];
                }
                trialResidual = evaluators.residualOf(samples, termsOf(trial, count, model, fixedBeta));
                if (std::isfinite(trialResidual) && trialResidual < residualNorm)
                {
                    const double actual = 1.0 - trialResidual / residualNorm;
                    double stepInMetric = 0.0;
                    double stepInScaling = 0.0;
                    for (std::size_t row = 0u; row < count; ++row)
                    {
                        double product = current.matrix[row][row] * step[row];
                        for (std::size_t column = 0u; column < row; ++column)
                        {
                            product += current.matrix[column][row] * step[column];
                        }
                        for (std::size_t column = row + 1u; column < count; ++column)
                        {
                            product += current.matrix[row][column] * step[column];
                        }
                        stepInMetric += step[row] * product;
                        stepInScaling += (scaling[row] * step[row]) * (scaling[row] * step[row]);
                    }
                    const double predicted = stepInMetric / residualNorm + 2.0 * damping * stepInScaling / residualNorm;
                    ratio = predicted > 0.0 ? actual / predicted : -1.0;
                }
                accepted = ratio > 0.0;
            }

            if (accepted)
            {
                for (std::size_t index = 0u; index < count; ++index)
                {
                    parameters[index] = trial[index];
                }
                evaluators.systemOf(samples, termsOf(parameters, count, model, fixedBeta), current);
                residualNorm = current.residualSumOfSquares;
                for (std::size_t index = 0u; index < count; ++index)
                {
                    double norm = std::sqrt(current.matrix[index][index]);
                    if (norm == 0.0)
                    {
                        norm = 1.0;
                    }
                    scaling[index] = (std::max)(scaling[index], norm);
                }
                dampingGrowth = 2;
                double decrease = 2.0 * ratio - 1.0;
                decrease = 1.0 - decrease * decrease * decrease;
                damping *= (std::max)(kDampingDecreaseFloor, decrease);
                rejected = 0;
            }
            else
            {
                damping *= static_cast<double>(dampingGrowth);
                dampingGrowth <<= 1;
                if (++rejected > kMaxRejectedSteps)
                {
                    stalled = true;
                    break;
                }
            }
        }

        if (stalled && iteration == 0u)
        {
            // No step was ever accepted, so the parameters are still the initial guess and the
            // normal equations still describe it. That is reported rather than treated as a fit.
            result.outcome = PsfFitOutcome::NoProgress;
            result.residualSumOfSquares = residualNorm;
            result.iterations = iteration;
            result.system = current;
            return result;
        }
        ++iteration;

        // A stall after at least one accepted step is not final: the damping the rejections grew
        // makes the next region small enough that a step is usually accepted, and the outer ceiling
        // is what bounds the attempt. The convergence tests below therefore run on the last step
        // that was accepted, which is what the solver being reproduced here does.
        bool smallStep = true;
        for (std::size_t index = 0u; index < count; ++index)
        {
            const double tolerance =
                parameterTolerance * parameterTolerance + parameterTolerance * std::fabs(parameters[index]);
            if (!(std::fabs(step[index]) < tolerance))
            {
                smallStep = false;
                break;
            }
        }
        if (smallStep)
        {
            converged = true;
            break;
        }
        double scaledGradient = 0.0;
        for (std::size_t index = 0u; index < count; ++index)
        {
            const double magnitude = (std::max)(parameters[index], 1.0);
            scaledGradient = (std::max)(scaledGradient, std::fabs(magnitude * current.gradient[index]));
        }
        if (scaledGradient <= gradientTolerance * (std::max)(0.5 * residualNorm, 1.0))
        {
            converged = true;
            break;
        }
    }

    result.outcome = converged ? PsfFitOutcome::Converged : PsfFitOutcome::IterationLimit;
    result.residualSumOfSquares = residualNorm;
    result.iterations = iteration;
    result.system = current;
    return result;
}

/// What the initial guess measured.
struct InitialGuess
{
    /// The peak above the stated sky, at the box centre.
    double amplitude = 0.0;

    /// The horizontal centre, in box coordinates.
    double centreX = 0.0;

    /// The vertical centre, in box coordinates.
    double centreY = 0.0;

    /// The larger full width, in pixels.
    double majorFullWidth = 0.0;

    /// The smaller full width, in pixels.
    double minorFullWidth = 0.0;

    /// The major axis's position angle, in degrees.
    double angleDegrees = 0.0;
};

/// Measures a starting point from the trace of the box above half its central peak.
///
/// The central sample is taken to be the peak - the detector guarantees it - so nothing searches
/// for a maximum and a brighter neighbour inside the box cannot capture the guess. A star that is
/// narrower than three pixels across is described by the trace's extent alone; a wider one has its
/// centroid, both principal moments and its rotation taken from the trace's zeroth, first and
/// second moments, which is resilient to a single hot sample in a way the samples themselves are
/// not.
///
/// @param box the square box, row-major.
/// @param side the box's width and height.
/// @param background the sky the trace is measured above.
/// @return the starting point.
[[nodiscard]] InitialGuess initialGuessOf(const double* box, std::size_t side, double background) noexcept
{
    const auto at = [box, side, background](int row, int column) {
        return box[static_cast<std::size_t>(row) * side + static_cast<std::size_t>(column)] - background;
    };
    const int extent = static_cast<int>(side);
    const int centre = extent / 2;
    InitialGuess guess{};
    const double peak = at(centre, centre);
    guess.amplitude = peak;
    const double half = peak * 0.5;

    int down = centre + kMinimumHalfRadius;
    int up = centre - kMinimumHalfRadius;
    int right = centre + kMinimumHalfRadius;
    int left = centre - kMinimumHalfRadius;
    while (down < extent - 1 && at(down + 1, centre) > half)
    {
        ++down;
    }
    while (up > 0 && at(up - 1, centre) > half)
    {
        --up;
    }
    while (right < extent - 1 && at(centre, right + 1) > half)
    {
        ++right;
    }
    while (left > 0 && at(centre, left - 1) > half)
    {
        --left;
    }
    const double widthX = static_cast<double>(right - left);
    const double widthY = static_cast<double>(down - up);
    if ((std::min)(widthX, widthY) < static_cast<double>(kMinimumMomentWidth))
    {
        guess.centreX = static_cast<double>(right + left + 1) / 2.0;
        guess.centreY = static_cast<double>(down + up + 1) / 2.0;
        guess.majorFullWidth = (std::max)(widthX, widthY);
        guess.minorFullWidth = (std::min)(widthX, widthY);
        guess.angleDegrees = 0.0;
        return guess;
    }

    // The moments of the trace above half maximum, walked outwards from the central row so that
    // only pixels connected to this star's own trace contribute.
    double area = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double momentXX = 0.0;
    double momentYY = 0.0;
    double momentXY = 0.0;
    const auto walk = [&](int startRow, int direction) {
        int row = startRow;
        int cursorRight = right;
        int cursorLeft = left;
        while (row >= 0 && row < extent)
        {
            const double rowSquared = static_cast<double>(row) * static_cast<double>(row);
            if (cursorRight >= 0 && cursorRight < extent - 1 && at(row, cursorRight) > half)
            {
                while (cursorRight < extent - 1 && at(row, cursorRight + 1) > half)
                {
                    ++cursorRight;
                }
            }
            else
            {
                --cursorRight;
                while (cursorRight > 0 && cursorRight > cursorLeft && at(row, cursorRight - 1) <= half)
                {
                    --cursorRight;
                }
            }
            if (cursorLeft >= 0 && at(row, cursorLeft) > half)
            {
                while (cursorLeft > 1 && at(row, cursorLeft - 1) > half)
                {
                    --cursorLeft;
                }
            }
            else if (cursorLeft < cursorRight)
            {
                ++cursorLeft;
                while (cursorLeft < cursorRight && at(row, cursorLeft + 1) <= half)
                {
                    ++cursorLeft;
                }
            }
            if (cursorRight < cursorLeft)
            {
                return;
            }
            if (cursorRight == cursorLeft && at(row, cursorRight) <= half)
            {
                return;
            }
            for (int column = cursorLeft; column <= cursorRight; ++column)
            {
                const double columnSquared = static_cast<double>(column) * static_cast<double>(column);
                area += 1.0;
                sumX += static_cast<double>(column);
                sumY += static_cast<double>(row);
                momentXX += rowSquared;
                momentYY += columnSquared;
                momentXY += static_cast<double>(column) * static_cast<double>(row);
            }
            row += direction;
        }
    };
    walk(centre, 1);
    walk(centre - 1, -1);

    if (!(area > 0.0))
    {
        guess.centreX = static_cast<double>(right + left + 1) / 2.0;
        guess.centreY = static_cast<double>(down + up + 1) / 2.0;
        guess.majorFullWidth = (std::max)(widthX, widthY);
        guess.minorFullWidth = (std::min)(widthX, widthY);
        return guess;
    }

    const double centroidX = sumX / area;
    const double centroidY = sumY / area;
    momentXX -= area * centroidY * centroidY;
    momentYY -= area * centroidX * centroidX;
    momentXY -= area * centroidX * centroidY;

    // The two-by-two singular value decomposition of the inertia matrix, which gives the principal
    // moments and the rotation between them.
    const double productXX = momentXX * momentXX + momentXY * momentXY;
    const double productXY = (momentXX + momentYY) * momentXY;
    const double productYY = momentYY * momentYY + momentXY * momentXY;
    const double angle = 90.0 + 0.5 * std::atan2(2.0 * productXY, productXX - productYY) * degreesPerRadian();
    const double trace = productXX + productYY;
    const double spread = std::sqrt((productXX - productYY) * (productXX - productYY) + 4.0 * productXY * productXY);
    const double majorMoment = std::sqrt((trace + spread) * 0.5);
    const double minorMoment = std::sqrt((trace - spread) * 0.5);
    const double ratio = std::sqrt(minorMoment / majorMoment);
    const double fullWidth = 2.0 * std::sqrt(area / std::numbers::pi / ratio);

    guess.centreX = centroidX + 0.5;
    guess.centreY = centroidY + 0.5;
    guess.majorFullWidth = fullWidth;
    guess.minorFullWidth = fullWidth * ratio;
    guess.angleDegrees = angle;
    return guess;
}

/// The instrumental magnitude of a whole box above one background.
///
/// Every sample of the box takes part, saturated ones included: what this measures is the flux the
/// box holds, and withholding the brightest samples of a star would understate exactly the stars a
/// magnitude is most wanted for.
[[nodiscard]] std::optional<double> boxMagnitude(const double* box, std::size_t count, double background) noexcept
{
    double flux = 0.0;
    for (std::size_t index = 0u; index < count; ++index)
    {
        flux += box[index] - background;
    }
    if (!(flux > 0.0) || !std::isfinite(flux))
    {
        return std::nullopt;
    }
    return -2.5 * std::log10(flux);
}

/// One relative uncertainty, or nothing when it cannot be formed.
[[nodiscard]] std::optional<double> relativeUncertainty(bool available, double variance, double value) noexcept
{
    if (!available || !(variance >= 0.0) || !std::isfinite(variance) || value == 0.0 || !std::isfinite(value))
    {
        return std::nullopt;
    }
    const double uncertainty = std::sqrt(variance) / value;
    return std::isfinite(uncertainty) ? std::optional<double>{uncertainty} : std::nullopt;
}

} // namespace

const char* psfFitOutcomeName(PsfFitOutcome outcome) noexcept
{
    switch (outcome)
    {
    case PsfFitOutcome::Converged:
        return "Converged";
    case PsfFitOutcome::IterationLimit:
        return "IterationLimit";
    case PsfFitOutcome::NoProgress:
        return "NoProgress";
    }
    return "UnknownPsfFitOutcome";
}

Result<double> psfScaleFromFullWidth(double fullWidth, double beta, PsfModel model)
{
    if (!isPsfModel(model))
    {
        return parameterFailure(kScaleOperation, ErrorCode::InvalidArgument,
                                "the model is not one this build implements", "model");
    }
    if (!isFinitePositive(fullWidth))
    {
        return parameterFailure(kScaleOperation, ErrorCode::InvalidArgument, "the full width is finite and positive",
                                "fullWidth");
    }
    if (model == PsfModel::Gaussian)
    {
        return fullWidth * fullWidth * kInverseFourLogTwo;
    }
    if (!isFinitePositive(beta))
    {
        return parameterFailure(kScaleOperation, ErrorCode::InvalidArgument, "a Moffat exponent is finite and positive",
                                "beta");
    }
    const double denominator = std::pow(2.0, 1.0 / beta) - 1.0;
    if (!isFinitePositive(denominator))
    {
        return parameterFailure(kScaleOperation, ErrorCode::InvalidArgument,
                                "the exponent leaves no usable core radius", "beta");
    }
    return fullWidth * fullWidth * 0.25 / denominator;
}

Result<double> psfFullWidthFromScale(double scale, double beta, PsfModel model)
{
    if (!isPsfModel(model))
    {
        return parameterFailure(kWidthOperation, ErrorCode::InvalidArgument,
                                "the model is not one this build implements", "model");
    }
    if (!std::isfinite(scale) || scale < 0.0)
    {
        return parameterFailure(kWidthOperation, ErrorCode::InvalidArgument, "the scale is finite and not negative",
                                "scale");
    }
    if (model == PsfModel::Gaussian)
    {
        return scale * kGaussianFullWidthPerSigma;
    }
    if (!isFinitePositive(beta))
    {
        return parameterFailure(kWidthOperation, ErrorCode::InvalidArgument, "a Moffat exponent is finite and positive",
                                "beta");
    }
    const double factor = std::pow(2.0, 1.0 / beta) - 1.0;
    if (!(factor >= 0.0) || !std::isfinite(factor))
    {
        return parameterFailure(kWidthOperation, ErrorCode::InvalidArgument,
                                "the exponent leaves no usable core radius", "beta");
    }
    return 2.0 * scale * std::sqrt(factor);
}

Result<PsfFitResult> fitStarPsf(const ExecutionContext& execution, std::span<const double> box, std::uint32_t radius,
                                const PsfFitRequest& request, std::span<double> scratch)
{
    if (!execution.valid())
    {
        return failure(kFitOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!isPsfModel(request.model))
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument, "the model is not one this build implements",
                                "model");
    }
    if (radius == 0u || radius > kMaxPsfBoxRadius)
    {
        return rangeFailure(kFitOperation, "the box radius lies between one and the maximum", "radius", radius,
                            kMaxPsfBoxRadius);
    }
    if (request.convergence == 0u || request.convergence > kMaxConvergence)
    {
        return rangeFailure(kFitOperation, "the convergence multiplier lies between one and sixteen", "convergence",
                            request.convergence, kMaxConvergence);
    }
    if (!std::isfinite(request.background))
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument, "the background is finite", "background");
    }
    if (!std::isfinite(request.saturationLevel) || !(request.saturationLevel > request.background))
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument,
                                "the saturation level is finite and above the background", "saturationLevel");
    }
    if (!isFinitePositive(request.parameterTolerance))
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument,
                                "the parameter tolerance is finite and positive", "parameterTolerance");
    }
    if (!isFinitePositive(request.gradientTolerance))
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument,
                                "the gradient tolerance is finite and positive", "gradientTolerance");
    }
    if (request.model == PsfModel::MoffatFixedBeta &&
        (!isFinitePositive(request.fixedBeta) || request.fixedBeta > kMoffatBetaCeiling))
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument,
                                "a held exponent is finite, positive and at or below the ceiling", "fixedBeta");
    }
    if (request.arcsecondsPerPixel.has_value() && !isFinitePositive(*request.arcsecondsPerPixel))
    {
        return parameterFailure(kFitOperation, ErrorCode::InvalidArgument,
                                "a stated plate scale is finite and positive", "arcsecondsPerPixel");
    }
    const std::size_t side = 2u * static_cast<std::size_t>(radius) + 1u;
    const std::size_t boxSamples = side * side;
    if (box.size() != boxSamples)
    {
        return failure(kFitOperation, ErrorCode::IncompatibleArguments,
                       "the box is not the square the radius describes");
    }
    if (scratch.size() < psfFitScratchLength(radius))
    {
        return failure(kFitOperation, ErrorCode::IncompatibleArguments,
                       "the scratch is shorter than the box's compacted run needs");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }

    const std::size_t parameters = request.model == PsfModel::MoffatFreeBeta ? 8u : 7u;
    double* const values = scratch.data();
    double* const coordinateX = scratch.data() + boxSamples;
    double* const coordinateY = scratch.data() + 2u * boxSamples;
    std::uint64_t kept = 0u;
    for (std::size_t row = 0u; row < side; ++row)
    {
        for (std::size_t column = 0u; column < side; ++column)
        {
            const double value = box[row * side + column];
            if (!std::isfinite(value))
            {
                return failure(kFitOperation, ErrorCode::InvalidData, "the box holds a sample that is not a number");
            }
            if (value < request.saturationLevel)
            {
                values[kept] = value;
                coordinateX[kept] = static_cast<double>(column) + 0.5;
                coordinateY[kept] = static_cast<double>(row) + 0.5;
                ++kept;
            }
        }
    }
    if (kept <= parameters)
    {
        return failure(kFitOperation, ErrorCode::InvalidData,
                       "the box holds no more unsaturated samples than the fit has parameters");
    }

    const InitialGuess guess = initialGuessOf(box.data(), side, request.background);
    const bool betaFree = request.model == PsfModel::MoffatFreeBeta;
    const double startingBeta = request.model == PsfModel::Gaussian ? 0.0 : (betaFree ? 2.0 : request.fixedBeta);
    const Result<double> majorScale = psfScaleFromFullWidth(
        isFinitePositive(guess.majorFullWidth) ? guess.majorFullWidth : 1.0, startingBeta, request.model);
    if (!majorScale.hasValue())
    {
        return majorScale.status();
    }

    double ratio = isFinitePositive(guess.majorFullWidth) ? guess.minorFullWidth / guess.majorFullWidth : 1.0;
    double angleRadians = guess.angleDegrees / degreesPerRadian();
    if (!(ratio > 0.0) || !(ratio <= 1.0))
    {
        ratio = 1.0;
    }
    if (ratio == 1.0)
    {
        // A perfectly round starting point sits on the boundary of the roundness parameterisation,
        // where the gradient of the roundness angle is zero and the solver cannot move it. Backing
        // off a tenth and starting from no rotation moves it off that boundary.
        ratio = 0.9;
        angleRadians = 0.0;
    }

    double vector[kPsfMaxFitParameters] = {};
    vector[0] = request.background;
    vector[1] = guess.amplitude;
    vector[2] = guess.centreX;
    vector[3] = guess.centreY;
    vector[4] = *majorScale;
    vector[5] = std::acos(2.0 * ratio - 1.0);
    vector[6] = angleRadians;
    vector[7] = betaFree ? std::acos(2.0 * startingBeta / kMoffatBetaCeiling - 1.0) : 0.0;

    const std::uint32_t withheld = static_cast<std::uint32_t>(boxSamples - kept);
    const std::uint32_t iterationCeiling = kPsfIterationsPerConvergenceUnit * (withheld > 0u ? 3u : 1u) *
                                           request.convergence * (request.model == PsfModel::Gaussian ? 1u : 2u);

    const PsfSampleRun samples{.values = values, .x = coordinateX, .y = coordinateY, .count = kept};
    const PsfEvaluators evaluators{.residual = psfResidualRunFor(execution),
                                   .normalEquations = psfNormalEquationsRunFor(execution)};
    const SolverResult solved = solve(parameters, vector, samples, iterationCeiling, request.parameterTolerance,
                                      request.gradientTolerance, request.model, request.fixedBeta, evaluators);
    if (!solved.started)
    {
        return failure(kFitOperation, ErrorCode::InvalidData, "the model could not be evaluated at the initial guess");
    }

    double variance[kPsfMaxFitParameters] = {};
    const bool uncertaintiesAvailable = covarianceDiagonal(parameters, solved.system.matrix, variance);

    PsfFitResult result{};
    result.psf.model = request.model;
    result.psf.background = vector[0];
    result.psf.amplitude = vector[1];
    result.psf.centreX = vector[2];
    result.psf.centreY = vector[3];
    result.psf.boxRadius = radius;
    result.psf.saturationLevel = request.saturationLevel;

    double beta = 0.0;
    if (request.model == PsfModel::MoffatFixedBeta)
    {
        beta = request.fixedBeta;
        result.psf.beta = beta;
    }
    else if (betaFree)
    {
        beta = 0.5 * kMoffatBetaCeiling * (std::cos(vector[7]) + 1.0);
        result.psf.beta = beta;
    }

    result.psf.scaleX =
        request.model == PsfModel::Gaussian ? std::sqrt(std::fabs(vector[4]) * 0.5) : std::sqrt(std::fabs(vector[4]));
    const double fittedRatio = 0.5 * (std::cos(vector[5]) + 1.0);
    result.psf.scaleY = result.psf.scaleX * fittedRatio;
    const Result<double> widthX = psfFullWidthFromScale(result.psf.scaleX, beta, request.model);
    const Result<double> widthY = psfFullWidthFromScale(result.psf.scaleY, beta, request.model);
    if (!widthX.hasValue() || !widthY.hasValue())
    {
        return failure(kFitOperation, ErrorCode::InvalidData, "the fit produced a width no model can describe");
    }
    result.psf.fullWidthX = *widthX;
    result.psf.fullWidthY = *widthY;
    if (!std::isfinite(result.psf.fullWidthX) || !std::isfinite(result.psf.fullWidthY) ||
        !(result.psf.fullWidthX > 0.0) || !(result.psf.fullWidthY > 0.0))
    {
        return failure(kFitOperation, ErrorCode::InvalidData, "the fit produced a width that is not positive");
    }
    if (request.arcsecondsPerPixel.has_value())
    {
        result.psf.fullWidthXArcseconds = result.psf.fullWidthX * *request.arcsecondsPerPixel;
        result.psf.fullWidthYArcseconds = result.psf.fullWidthY * *request.arcsecondsPerPixel;
    }

    double angleDegrees = -vector[6] * degreesPerRadian();
    if (std::fabs(angleDegrees) > kAbsurdAngleDegrees)
    {
        return failure(kFitOperation, ErrorCode::InvalidData, "the fit produced a position angle no star has");
    }
    while (std::fabs(angleDegrees) > 90.0)
    {
        angleDegrees += angleDegrees > 0.0 ? -180.0 : 180.0;
    }
    result.psf.angleDegrees = -angleDegrees;
    result.psf.residualRootMeanSquare = std::sqrt(solved.residualSumOfSquares / static_cast<double>(kept));

    result.uncertainty.background = relativeUncertainty(uncertaintiesAvailable, variance[0], vector[0]);
    result.uncertainty.amplitude = relativeUncertainty(uncertaintiesAvailable, variance[1], vector[1]);
    result.uncertainty.centreX = relativeUncertainty(uncertaintiesAvailable, variance[2], vector[2]);
    result.uncertainty.centreY = relativeUncertainty(uncertaintiesAvailable, variance[3], vector[3]);
    result.uncertainty.scaleX = relativeUncertainty(uncertaintiesAvailable, variance[4], vector[4]);
    result.uncertainty.scaleY = relativeUncertainty(uncertaintiesAvailable, variance[5], vector[5]);
    result.uncertainty.angle = relativeUncertainty(uncertaintiesAvailable, variance[6], vector[6]);
    if (betaFree)
    {
        result.uncertainty.beta = relativeUncertainty(uncertaintiesAvailable, variance[7], vector[7]);
    }

    result.metrics.roundness = result.psf.fullWidthX != 0.0 ? result.psf.fullWidthY / result.psf.fullWidthX : 0.0;
    result.metrics.circularity =
        result.psf.amplitude / (0.1 + std::fabs(result.psf.fullWidthX - result.psf.fullWidthY));
    result.metrics.meanFullWidth = 0.5 * (result.psf.fullWidthX + result.psf.fullWidthY);

    result.instrumentalMagnitude = boxMagnitude(box.data(), boxSamples, result.psf.background);
    result.report.outcome = solved.outcome;
    result.report.iterations = solved.iterations;
    result.report.fittedSamples = kept;
    result.report.withheldSamples = boxSamples - kept;
    result.report.uncertaintiesAvailable = uncertaintiesAvailable;
    return result;
}

} // namespace an

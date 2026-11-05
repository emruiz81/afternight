// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Fitting one point-spread function to one box of samples.
///
/// `fitStarPsf` is the measurement step of star detection, published so a caller holding its own
/// candidate box can fit it without running a detection. The box is a square of samples centred on
/// the star's peak, already widened to double in the frame's numeric domain; the fit returns a
/// `StarPsf` - the model's parameters and the full widths derived from them - with relative
/// uncertainties, an instrumental magnitude and a report of why the solver stopped.
///
/// A fitted centre is a box coordinate under the engine's half-pixel convention: the first box
/// sample's centre is (0.5, 0.5). `detectStars` adds the box's origin in the frame to turn it into
/// a `DetectedStar` position. The two scale helpers convert between a full width at half maximum
/// and the scale term the fit moves, which is `2 sigma^2` for a Gaussian and the squared core
/// radius for a Moffat.
///
/// The fit allocates nothing: the caller supplies `psfFitScratchLength(radius)` doubles of scratch,
/// which is what lets the detector run one fit per worker from one block it took up front.

#pragma once

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The largest square box a fit reads, as a radius in pixels.
///
/// A box of this radius is 401 by 401 samples, which is far larger than any real star and is the
/// point past which a "star" is something else - a satellite trail, a galaxy core, a reflection.
inline constexpr std::uint32_t kMaxPsfBoxRadius = 200u;

/// The largest exponent the free-beta Moffat parameterisation can reach.
///
/// Beta is fitted as `kMoffatBetaCeiling * (cos(f) + 1) / 2`, so the solver moves a bounded angle
/// rather than an unbounded exponent and cannot walk beta to zero or to infinity.
inline constexpr double kMoffatBetaCeiling = 10.0;

/// The default step-size and gradient tolerance of the fit.
///
/// One part in a thousand for both tests. A star is measured to a hundredth of a pixel at best, so a
/// tighter tolerance buys iterations rather than accuracy.
inline constexpr double kDefaultPsfFitTolerance = 1.0e-3;

/// How many iterations one unit of convergence buys a Gaussian fit.
///
/// The iteration ceiling is this number times the convergence request, doubled for a Moffat, and
/// tripled again when the box holds withheld saturated samples, which spends the iterations where
/// the residual surface is hardest.
inline constexpr std::uint32_t kPsfIterationsPerConvergenceUnit = 20u;

/// Why a fit stopped.
enum class PsfFitOutcome : std::uint8_t
{
    /// A convergence test passed: either every parameter step fell below its tolerance, or the
    /// scaled gradient did.
    Converged = 0u,

    /// The iteration ceiling was reached with both tests still failing. The parameters are the
    /// best the solver reached and are reported, because a star that is nearly fitted is still
    /// measurable; whether to keep it is the detector's decision, not the solver's.
    IterationLimit = 1u,

    /// The solver rejected more than fifteen consecutive trial steps before accepting its first,
    /// so the trust region collapsed without progress and the parameters are the initial guess.
    /// A run of rejections after an accepted step does not end the fit; the iteration ceiling
    /// bounds it instead.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NoProgress = 2u
};

/// Whether a value names an outcome this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param outcome the value to inspect.
/// @return true for one of the three declared outcomes.
[[nodiscard]] constexpr bool isPsfFitOutcome(PsfFitOutcome outcome) noexcept
{
    return outcome == PsfFitOutcome::Converged || outcome == PsfFitOutcome::IterationLimit ||
           outcome == PsfFitOutcome::NoProgress;
}

/// The outcome's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param outcome the outcome to name.
/// @return a null-terminated string literal compiled into this library; `"UnknownPsfFitOutcome"`
///         for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* psfFitOutcomeName(PsfFitOutcome outcome) noexcept;

/// What a caller asks one point-spread-function fit for.
///
/// The box itself is passed separately. Everything here is policy: which shape, what the sky under
/// it is, which samples are too bright to believe, and how hard to work.
struct PsfFitRequest
{
    /// Which shape to fit.
    PsfModel model = PsfModel::Gaussian;

    /// The exponent to hold, for `PsfModel::MoffatFixedBeta`.
    ///
    /// Must be finite and above zero, and at or below `kMoffatBetaCeiling`. Read by that model
    /// alone.
    double fixedBeta = 1.5;

    /// The sky level the initial guess subtracts, in the box's numeric domain.
    ///
    /// This seeds the background parameter and centres the moment analysis; the fit is free to move
    /// it. Must be finite.
    double background = 0.0;

    /// The level at or above which a sample is withheld from the fit.
    ///
    /// Samples at or above it are not residuals the solver sees, so a clipped core does not drag
    /// the amplitude down onto the plateau. Must be finite and above `background`. A caller that
    /// wants every sample fitted states a level above the box's maximum.
    double saturationLevel = 1.0;

    /// How many units of iteration ceiling the fit is allowed. Must be between one and sixteen.
    std::uint32_t convergence = 1u;

    /// The relative step size below which the fit calls itself converged. Finite and positive.
    double parameterTolerance = kDefaultPsfFitTolerance;

    /// The scaled gradient below which the fit calls itself converged. Finite and positive.
    double gradientTolerance = kDefaultPsfFitTolerance;

    /// Arcseconds one pixel subtends, when the caller knows it.
    ///
    /// Present turns the fitted widths into the arcsecond fields of `StarPsf`; absent leaves those
    /// absent. Must be finite and positive when present. One plate scale states what a focal length
    /// and a pixel pitch would, with the arithmetic done once by the caller, who knows which axis the
    /// pitch describes.
    std::optional<double> arcsecondsPerPixel{};
};

/// What one fit produced, beside the star it fitted.
struct PsfFitReport
{
    /// Why the solver stopped.
    PsfFitOutcome outcome = PsfFitOutcome::Converged;

    /// How many accepted iterations it took.
    std::uint32_t iterations = 0u;

    /// How many samples of the box were below the saturation level and therefore fitted.
    std::uint64_t fittedSamples = 0u;

    /// How many samples were withheld as saturated.
    std::uint64_t withheldSamples = 0u;

    /// Whether the inverse normal-equation matrix existed, so the uncertainties are present.
    bool uncertaintiesAvailable = false;
};

/// One fitted star and the record of how it was fitted.
struct PsfFitResult
{
    /// The fitted light distribution.
    StarPsf psf{};

    /// How well each parameter is determined.
    StarPsfUncertainty uncertainty{};

    /// Shape figures derived from the fitted widths.
    StarMetrics metrics{};

    /// Instrumental magnitude of the whole box above the fitted background, or absent when that
    /// sum is not positive.
    std::optional<double> instrumentalMagnitude{};

    /// How the fit went.
    PsfFitReport report{};
};

/// The squared scale term a full width at half maximum implies, for one model.
///
/// The term the fit parameterises, which is `SX` in the Gaussian `B + A exp(-(tx^2 / SX + ...))`:
/// twice the variance, `2 sigma^2`, for a Gaussian, and the squared core radius for a Moffat. It
/// is therefore not the inverse of `psfFullWidthFromScale`, which takes the standard deviation or
/// the core radius itself: `psfFullWidthFromScale(sqrt(scale / 2), ...)` recovers a Gaussian's width
/// and `psfFullWidthFromScale(sqrt(scale), ...)` a Moffat's.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param fullWidth the full width at half maximum, in pixels. Must be finite and positive.
/// @param beta the Moffat exponent; ignored for a Gaussian. Must be finite and above zero for a
///        Moffat.
/// @param model which shape the width describes.
/// @return the squared scale term, in square pixels; `ErrorCode::InvalidArgument` for a non-finite
///         or non-positive width, an unusable exponent, or an out-of-range model.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> psfScaleFromFullWidth(double fullWidth, double beta, PsfModel model);

/// The full width at half maximum a scale parameter implies, for one model.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param scale the Gaussian standard deviation or Moffat core radius, in pixels. Must be finite
///        and not negative.
/// @param beta the Moffat exponent; ignored for a Gaussian. Must be finite and above zero for a
///        Moffat.
/// @param model which shape the scale describes.
/// @return the full width at half maximum; `ErrorCode::InvalidArgument` for a non-finite or
///         negative scale, an unusable exponent, or an out-of-range model.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double> psfFullWidthFromScale(double scale, double beta, PsfModel model);

/// Fits one point-spread function to one square box of samples.
///
/// The box is row-major, `2 * radius + 1` samples on each side, and its centre sample is taken to
/// be the star's peak: the initial guess measures the trace above half that peak outwards from the
/// centre rather than searching for a maximum, so a brighter neighbour inside the box cannot
/// capture the fit. Samples at or above the request's saturation level are withheld from the
/// residual, and the fit needs strictly more surviving samples than it has free parameters.
///
/// The solver is a Levenberg-Marquardt trust-region iteration on the scaled normal equations, with
/// Nielsen damping and the "more" diagonal scaling. The residual sum, the normal-equation matrix
/// and the gradient are accumulated in one pass over the surviving samples through the operation's
/// dispatched kernel, so the Jacobian is never materialised and the instruction set the arithmetic
/// runs on is the one the execution selected. A compiled body evaluates the exponential with a
/// vector polynomial rather than the platform's scalar one, so fits on two instruction sets agree
/// to a stated distance - about 1e-14 pixels of centre and width - rather than bit for bit;
/// `ComputeBackend::Reference` runs the scalar text.
///
/// Thread-safe with respect to `execution` and `box`, which are only read; several fits may run at
/// once on separate executions. It allocates nothing: the working set is bounded by the parameter
/// count and lives on the stack, and the compacted sample list is the caller's `scratch` span.
/// Cancellation is checked once, before the iteration starts.
///
/// @param execution the operation policy and engine-context lease, which also selects the kernel.
/// @param box the square box, row-major, exactly `(2 * radius + 1) ^ 2` samples long.
/// @param radius half the box width, between one and `kMaxPsfBoxRadius`.
/// @param request which shape, what sky, which samples and how hard.
/// @param scratch borrowed working storage for the compacted sample list, at least three doubles
///        per box sample. It is written and its contents mean nothing afterwards.
/// @return the fitted star, whose centre is in box coordinates; `ErrorCode::NotInitialized` when
///         `execution` holds nothing; `ErrorCode::InvalidArgument` for an out-of-range model,
///         tolerance, plate scale or exponent, a non-finite sky, or a saturation level that is not
///         finite and above the sky; `ErrorCode::OutOfRange` for a radius or convergence outside
///         its range, carrying the value and the limit; `ErrorCode::IncompatibleArguments` when the
///         box is not the length the radius implies or the scratch is shorter;
///         `ErrorCode::InvalidData` for a non-finite sample, a box whose surviving samples do not
///         outnumber the free parameters, a model that cannot be evaluated at the initial guess, or
///         a fit that produced a non-finite or non-positive width or a position angle beyond ten
///         thousand degrees; or `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`. A solver
///         that stopped without converging is a success whose report says so.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PsfFitResult> fitStarPsf(const ExecutionContext& execution,
                                                                   std::span<const double> box, std::uint32_t radius,
                                                                   const PsfFitRequest& request,
                                                                   std::span<double> scratch);

/// How many doubles of scratch `fitStarPsf` needs for a box of one radius.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param radius half the box width, in pixels.
/// @return the scratch length, three per box sample: the value and both coordinates of every
///         sample the fit may keep.
[[nodiscard]] constexpr std::uint64_t psfFitScratchLength(std::uint32_t radius) noexcept
{
    const std::uint64_t side = 2ull * radius + 1ull;
    return 3ull * side * side;
}

} // namespace an

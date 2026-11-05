// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - comet exposure timing and deterministic trajectory fitting.
//
// A track is a pair of polynomials, x(t) and y(t), of degree one or two in seconds from an origin
// instant, fitted by least squares to the accepted observations. Each fit maps its observation
// times onto [-1, 1] before forming the normal equations, solves them in extended precision, and
// translates the coefficients back to seconds, so the conditioning does not depend on how far the
// observations lie from any epoch. Outliers are removed one at a time by leave-one-out refits
// before the degree is chosen, and the coefficients are moved to the requested origin last.

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/registration/CometInternal.h"
#include "algorithms/registration/RegistrationInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <new>
#include <span>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kMidpointOperation = "an::computeCometObservationMidpoint";
constexpr const char* kFitOperation = "an::fitCometTrack";
constexpr const char* kEvaluateOperation = "an::evaluateCometTrack";

/// One accepted observation, as the fit uses it.
struct TrackPoint
{
    /// The caller's frame identity.
    std::uint64_t frameId = 0u;

    /// The exposure midpoint.
    UtcInstant midpoint{0};

    /// The position in the common reference-frame pixel plane.
    PixelCoordinate position{};
};

/// One least-squares fit of both axes.
struct FittedModel
{
    /// Horizontal coefficients of `1`, `t` and `t * t`, with `t` in seconds from the fit's origin;
    /// the last is zero for a linear fit.
    std::array<double, 3u> x{};

    /// Vertical coefficients, in the same basis.
    std::array<double, 3u> y{};

    /// The root-mean-square residual over the fitted points, in pixels.
    double rootMeanSquare = 0.0;

    /// The largest residual over the fitted points, in pixels.
    double maximumResidual = 0.0;

    /// Whether the fit exists: false for too few points, a zero time span, a singular system or a
    /// residual that is not finite.
    bool usable = false;
};

/// The signed time between two instants.
///
/// @param value the later instant, or the earlier for a negative answer.
/// @param origin the instant measured from.
/// @return seconds, in extended precision so whole nanoseconds survive.
[[nodiscard]] long double secondsFrom(const UtcInstant value, const UtcInstant origin) noexcept
{
    const long double difference =
        static_cast<long double>(value.nanosecondsSinceEpoch) - static_cast<long double>(origin.nanosecondsSinceEpoch);
    return difference / 1000000000.0L;
}

/// Solves one augmented normal-equation system of two or three unknowns by Gauss-Jordan
/// elimination with partial pivoting.
///
/// @param system the rows, each its coefficients followed by its right-hand side; taken by value.
/// @param terms how many unknowns participate.
/// @param coefficients the answer, with unused entries zero.
/// @return false when the system is empty, a pivot is at most 1e-18 of its largest entry, or the
///         answer is not finite.
[[nodiscard]] bool solveSystem(std::array<std::array<long double, 4u>, 3u> system, const std::size_t terms,
                               std::array<double, 3u>& coefficients) noexcept
{
    long double scale = 0.0L;
    for (std::size_t row = 0u; row < terms; ++row)
    {
        for (std::size_t column = 0u; column < terms; ++column)
        {
            scale = (std::max)(scale, std::abs(system[row][column]));
        }
    }
    if (!(scale > 0.0L))
    {
        return false;
    }
    for (std::size_t pivot = 0u; pivot < terms; ++pivot)
    {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1u; row < terms; ++row)
        {
            if (std::abs(system[row][pivot]) > std::abs(system[best][pivot]))
            {
                best = row;
            }
        }
        if (std::abs(system[best][pivot]) <= scale * 1.0e-18L)
        {
            return false;
        }
        if (best != pivot)
        {
            std::swap(system[best], system[pivot]);
        }
        const long double divisor = system[pivot][pivot];
        for (std::size_t column = pivot; column <= terms; ++column)
        {
            system[pivot][column] /= divisor;
        }
        for (std::size_t row = 0u; row < terms; ++row)
        {
            if (row == pivot)
            {
                continue;
            }
            const long double factor = system[row][pivot];
            for (std::size_t column = pivot; column <= terms; ++column)
            {
                system[row][column] -= factor * system[pivot][column];
            }
        }
    }
    coefficients = {};
    for (std::size_t index = 0u; index < terms; ++index)
    {
        coefficients[index] = static_cast<double>(system[index][terms]);
        if (!std::isfinite(coefficients[index]))
        {
            return false;
        }
    }
    return true;
}

/// Fits both axes by least squares over the active points, optionally leaving one out.
///
/// Times are mapped onto [-1, 1] over the fitted points' span for the solve, and the coefficients
/// are then re-expressed in seconds from `origin`; residuals are measured in that final basis.
///
/// @param points every accepted observation.
/// @param active indices into `points` of the observations still in the fit.
/// @param origin the instant the returned coefficients are measured from.
/// @param degree one for a linear track, two for a quadratic one.
/// @param excluded a position in `active` to leave out, or the largest `std::size_t` for none.
/// @return the fit, whose `usable` is false when it does not exist.
[[nodiscard]] FittedModel fitModel(const std::span<const TrackPoint> points, const std::span<const std::size_t> active,
                                   const UtcInstant origin, const std::size_t degree,
                                   const std::size_t excluded = (std::numeric_limits<std::size_t>::max)()) noexcept
{
    const std::size_t terms = degree + 1u;
    long double first = (std::numeric_limits<long double>::max)();
    long double last = (std::numeric_limits<long double>::lowest)();
    for (std::size_t position = 0u; position < active.size(); ++position)
    {
        if (position != excluded)
        {
            const auto time = secondsFrom(points[active[position]].midpoint, origin);
            first = (std::min)(first, time);
            last = (std::max)(last, time);
        }
    }
    const long double centre = first * 0.5L + last * 0.5L;
    const long double scale = (last - first) * 0.5L;
    if (!(scale > 0.0L))
    {
        return {};
    }
    std::array<std::array<long double, 4u>, 3u> xSystem{};
    std::array<std::array<long double, 4u>, 3u> ySystem{};
    std::size_t count = 0u;
    for (std::size_t position = 0u; position < active.size(); ++position)
    {
        if (position == excluded)
        {
            continue;
        }
        const TrackPoint& point = points[active[position]];
        const long double t = (secondsFrom(point.midpoint, origin) - centre) / scale;
        const std::array<long double, 3u> basis{1.0L, t, t * t};
        for (std::size_t row = 0u; row < terms; ++row)
        {
            for (std::size_t column = 0u; column < terms; ++column)
            {
                xSystem[row][column] += basis[row] * basis[column];
                ySystem[row][column] += basis[row] * basis[column];
            }
            xSystem[row][terms] += basis[row] * static_cast<long double>(point.position.x);
            ySystem[row][terms] += basis[row] * static_cast<long double>(point.position.y);
        }
        ++count;
    }
    FittedModel result;
    if (count < terms || !solveSystem(xSystem, terms, result.x) || !solveSystem(ySystem, terms, result.y))
    {
        return result;
    }

    for (auto* coefficients : {&result.x, &result.y})
    {
        const long double quadratic = (*coefficients)[2] / (scale * scale);
        const long double linear = (*coefficients)[1] / scale;
        (*coefficients)[0] = static_cast<double>((*coefficients)[0] - centre * linear + centre * centre * quadratic);
        (*coefficients)[1] = static_cast<double>(linear - 2.0L * centre * quadratic);
        (*coefficients)[2] = static_cast<double>(quadratic);
    }

    long double squared = 0.0L;
    for (std::size_t position = 0u; position < active.size(); ++position)
    {
        if (position == excluded)
        {
            continue;
        }
        const TrackPoint& point = points[active[position]];
        const double t = static_cast<double>(secondsFrom(point.midpoint, origin));
        const double predictedX = result.x[0] + result.x[1] * t + result.x[2] * t * t;
        const double predictedY = result.y[0] + result.y[1] * t + result.y[2] * t * t;
        const double residual = std::hypot(point.position.x - predictedX, point.position.y - predictedY);
        if (!std::isfinite(residual))
        {
            return FittedModel{};
        }
        squared += static_cast<long double>(residual) * residual;
        result.maximumResidual = (std::max)(result.maximumResidual, residual);
    }
    result.rootMeanSquare = std::sqrt(static_cast<double>(squared / static_cast<long double>(count)));
    result.usable = std::isfinite(result.rootMeanSquare);
    return result;
}

/// The distance between one observation and a fit's prediction at its midpoint.
///
/// @param point the observation.
/// @param model the fit.
/// @param origin the instant the fit's coefficients are measured from.
/// @return the residual, in pixels.
[[nodiscard]] double residualOf(const TrackPoint& point, const FittedModel& model, const UtcInstant origin) noexcept
{
    const double t = static_cast<double>(secondsFrom(point.midpoint, origin));
    const double x = model.x[0] + model.x[1] * t + model.x[2] * t * t;
    const double y = model.y[0] + model.y[1] * t + model.y[2] * t * t;
    return std::hypot(point.position.x - x, point.position.y - y);
}

/// Fits a comet track, letting an engine-context refusal escape as an exception.
///
/// @param execution operation policy and engine-context lease.
/// @param observations the caller's observations.
/// @param request the model policy, outlier rule, origin and warning thresholds.
/// @return the track, or a returned failure.
[[nodiscard]] Result<CometTrackResult> fitCometTrackUnguarded(const ExecutionContext& execution,
                                                              const std::span<const CometTrackObservation> observations,
                                                              const CometTrackRequest& request)
{
    if (!execution.valid())
    {
        return comet_internal::failure(ErrorCode::NotInitialized, "a track fit needs an execution context",
                                       kFitOperation);
    }
    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        return stopped;
    }
    const Status requestReady = comet_internal::validateTrackRequest(request, kFitOperation);
    if (requestReady.failed())
    {
        return requestReady;
    }
    if (observations.size() > kMaximumCometTrackObservations)
    {
        return comet_internal::failure(ErrorCode::LimitExceeded, "the observation set exceeds the comet-track ceiling",
                                       kFitOperation);
    }

    registration_internal::ExecutionMemory memory(*execution.memoryResource());
    std::pmr::vector<TrackPoint> points(&memory);
    points.reserve(observations.size());
    for (std::size_t index = 0u; index < observations.size(); ++index)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        const CometTrackObservation& observation = observations[index];
        for (std::size_t earlier = 0u; earlier < index; ++earlier)
        {
            if (observations[earlier].frameId == observation.frameId)
            {
                return comet_internal::failure(ErrorCode::InvalidArgument, "track observation identities are unique",
                                               kFitOperation);
            }
        }
        if (!observation.accepted)
        {
            if (observation.required)
            {
                return comet_internal::failure(ErrorCode::InvalidData, "a required track observation was not accepted",
                                               kFitOperation);
            }
            continue;
        }
        if (!comet_internal::finitePixel(observation.position))
        {
            return comet_internal::failure(ErrorCode::InvalidArgument, "an accepted track position is finite",
                                           kFitOperation);
        }
        for (const TrackPoint& point : points)
        {
            if (point.midpoint == observation.midpoint)
            {
                return comet_internal::failure(ErrorCode::InvalidArgument,
                                               "accepted observations have distinct exposure midpoints", kFitOperation);
            }
        }
        points.push_back(
            {.frameId = observation.frameId, .midpoint = observation.midpoint, .position = observation.position});
    }
    if (points.size() < 2u)
    {
        return comet_internal::failure(ErrorCode::EmptySelection,
                                       "a comet track needs at least two accepted observations", kFitOperation);
    }

    const auto earliest =
        std::min_element(points.begin(), points.end(), [](const TrackPoint& left, const TrackPoint& right) {
            if (left.midpoint.nanosecondsSinceEpoch != right.midpoint.nanosecondsSinceEpoch)
            {
                return left.midpoint.nanosecondsSinceEpoch < right.midpoint.nanosecondsSinceEpoch;
            }
            return left.frameId < right.frameId;
        });
    // Model selection and residuals must not depend on the requested coefficient basis.
    // Fit near the observations and translate the coefficients only after selecting a model.
    const UtcInstant origin = earliest->midpoint;
    std::pmr::vector<std::size_t> active(&memory);
    active.resize(points.size());
    for (std::size_t index = 0u; index < active.size(); ++index)
    {
        active[index] = index;
    }

    FittedModel linear = fitModel({points.data(), points.size()}, {active.data(), active.size()}, origin, 1u);
    if (!linear.usable)
    {
        return comet_internal::failure(ErrorCode::InvalidData,
                                       "the accepted observation times do not determine a linear track", kFitOperation);
    }
    std::uint32_t rejected = 0u;
    // Test outliers against the highest eligible model, so acceleration is not mistaken for a
    // bad observation before model selection has had a chance to consider it.
    std::size_t rejectionDegree = request.modelPolicy != CometTrackModelPolicy::ForceLinear &&
                                          active.size() >= request.minimumQuadraticObservations
                                      ? 2u
                                      : 1u;
    FittedModel rejectionModel =
        rejectionDegree == 2u ? fitModel({points.data(), points.size()}, {active.data(), active.size()}, origin, 2u)
                              : linear;
    if (!rejectionModel.usable)
    {
        rejectionDegree = 1u;
        rejectionModel = linear;
    }
    // Each pass refits once without each remaining observation. An observation is a rejection
    // candidate when its residual under the fit that left it out exceeds both the residual floor
    // and `outlierSigma` times that fit's RMS, and leaving it out at least halves the current RMS;
    // of the candidates, the one whose removal leaves the lowest RMS goes. Passes stop when none
    // qualifies or only enough observations to determine the degree remain.
    const std::size_t minimumRetained = rejectionDegree + 1u;
    while (active.size() > minimumRetained)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        std::size_t best = (std::numeric_limits<std::size_t>::max)();
        double bestRootMeanSquare = (std::numeric_limits<double>::infinity)();
        for (std::size_t position = 0u; position < active.size(); ++position)
        {
            const Status trialCheckpoint = execution.checkCancellation();
            if (trialCheckpoint.failed())
            {
                return trialCheckpoint;
            }
            const FittedModel trial = fitModel({points.data(), points.size()}, {active.data(), active.size()}, origin,
                                               rejectionDegree, position);
            const double excludedResidual = trial.usable ? residualOf(points[active[position]], trial, origin) : 0.0;
            const double threshold =
                (std::max)(request.minimumOutlierResidualPixels, request.outlierSigma * trial.rootMeanSquare + 1.0e-9);
            if (trial.usable && excludedResidual > threshold &&
                trial.rootMeanSquare <= rejectionModel.rootMeanSquare * 0.5 &&
                trial.rootMeanSquare < bestRootMeanSquare)
            {
                best = position;
                bestRootMeanSquare = trial.rootMeanSquare;
            }
        }
        if (best == (std::numeric_limits<std::size_t>::max)())
        {
            break;
        }
        active.erase(active.begin() + static_cast<std::ptrdiff_t>(best));
        ++rejected;
        rejectionModel =
            fitModel({points.data(), points.size()}, {active.data(), active.size()}, origin, rejectionDegree);
    }

    // The quadratic is chosen when the policy allows it, enough observations remain, and it either
    // is preferred or lowers a non-zero linear RMS by at least `minimumQuadraticImprovement`.
    linear = fitModel({points.data(), points.size()}, {active.data(), active.size()}, origin, 1u);
    FittedModel selected = linear;
    CometTrackModel model = CometTrackModel::Linear;
    if (request.modelPolicy != CometTrackModelPolicy::ForceLinear &&
        active.size() >= request.minimumQuadraticObservations)
    {
        const FittedModel quadratic =
            fitModel({points.data(), points.size()}, {active.data(), active.size()}, origin, 2u);
        const bool preferred = request.modelPolicy == CometTrackModelPolicy::PreferQuadratic;
        const bool automatic =
            linear.rootMeanSquare > 1.0e-9 &&
            quadratic.rootMeanSquare <= linear.rootMeanSquare * (1.0 - request.minimumQuadraticImprovement);
        if (quadratic.usable && (preferred || automatic))
        {
            selected = quadratic;
            model = CometTrackModel::Quadratic;
        }
    }

    double motion = 0.0;
    for (std::size_t first = 0u; first < active.size(); ++first)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        for (std::size_t second = first + 1u; second < active.size(); ++second)
        {
            const PixelCoordinate& a = points[active[first]].position;
            const PixelCoordinate& b = points[active[second]].position;
            motion = (std::max)(motion, std::hypot(a.x - b.x, a.y - b.y));
        }
    }

    CometTrackResult result;
    result.model = model;
    result.origin = request.coefficientOrigin.value_or(origin);
    result.x = selected.x;
    result.y = selected.y;
    const long double shift = secondsFrom(result.origin, origin);
    for (auto* coefficients : {&result.x, &result.y})
    {
        const long double quadratic = (*coefficients)[2];
        const long double linearCoefficient = (*coefficients)[1];
        (*coefficients)[0] = static_cast<double>((*coefficients)[0] + shift * (linearCoefficient + shift * quadratic));
        (*coefficients)[1] = static_cast<double>(linearCoefficient + 2.0L * shift * quadratic);
    }
    result.observationsUsed = static_cast<std::uint32_t>(active.size());
    result.outliersRejected = rejected;
    result.rootMeanSquareResidualPixels = selected.rootMeanSquare;
    result.maximumResidualPixels = selected.maximumResidual;
    result.motionPixels = motion;
    result.peakWorkingBytes = memory.peakBytes();
    result.diagnostics.noRedundancy = active.size() == (model == CometTrackModel::Quadratic ? 3u : 2u);
    result.diagnostics.quadraticSelected = model == CometTrackModel::Quadratic;
    result.diagnostics.highResidual =
        request.highResidualWarningPixels > 0.0 && selected.rootMeanSquare > request.highResidualWarningPixels;
    result.diagnostics.motionBelowThreshold =
        request.minimumMotionWarningPixels > 0.0 && result.motionPixels < request.minimumMotionWarningPixels;
    return result;
}

} // namespace

Result<UtcInstant> computeCometObservationMidpoint(const UtcInstant exposureStart, const double exposureSeconds)
{
    if (!std::isfinite(exposureSeconds) || exposureSeconds < 0.0)
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "an exposure duration is finite and not negative",
                                       kMidpointOperation);
    }
    const long double halfNanoseconds = static_cast<long double>(exposureSeconds) * 0.5L * 1000000000.0L;
    if (halfNanoseconds > static_cast<long double>((std::numeric_limits<std::int64_t>::max)()))
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "half the exposure does not fit signed nanoseconds",
                                       kMidpointOperation);
    }
    const std::int64_t offset = static_cast<std::int64_t>(std::round(halfNanoseconds));
    if (exposureStart.nanosecondsSinceEpoch > (std::numeric_limits<std::int64_t>::max)() - offset)
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "the exposure midpoint does not fit a UTC instant",
                                       kMidpointOperation);
    }
    return UtcInstant{.nanosecondsSinceEpoch = exposureStart.nanosecondsSinceEpoch + offset};
}

const char* cometTrackModelName(const CometTrackModel model) noexcept
{
    switch (model)
    {
    case CometTrackModel::None:
        return "None";
    case CometTrackModel::Linear:
        return "Linear";
    case CometTrackModel::Quadratic:
        return "Quadratic";
    }
    return "UnknownCometTrackModel";
}

const char* cometTrackModelPolicyName(const CometTrackModelPolicy policy) noexcept
{
    switch (policy)
    {
    case CometTrackModelPolicy::Automatic:
        return "Automatic";
    case CometTrackModelPolicy::ForceLinear:
        return "ForceLinear";
    case CometTrackModelPolicy::PreferQuadratic:
        return "PreferQuadratic";
    }
    return "UnknownCometTrackModelPolicy";
}

Result<CometTrackResult> fitCometTrack(const ExecutionContext& execution,
                                       const std::span<const CometTrackObservation> observations,
                                       const CometTrackRequest& request)
{
    try
    {
        return fitCometTrackUnguarded(execution, observations, request);
    }
    catch (const registration_internal::ContextAllocationFailure& refused)
    {
        return refused.status();
    }
    catch (const std::bad_alloc&)
    {
        return comet_internal::failure(ErrorCode::AllocationFailed, "the track result could not allocate its storage",
                                       kFitOperation);
    }
}

Result<PixelCoordinate> evaluateCometTrack(const CometTrackResult& track, const UtcInstant midpoint)
{
    if (track.model != CometTrackModel::Linear && track.model != CometTrackModel::Quadratic)
    {
        return comet_internal::failure(ErrorCode::InvalidArgument,
                                       "only a fitted linear or quadratic track can be evaluated", kEvaluateOperation);
    }
    const long double preciseSeconds = secondsFrom(midpoint, track.origin);
    const double seconds = static_cast<double>(preciseSeconds);
    if (!std::isfinite(seconds))
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "the evaluation time difference is finite",
                                       kEvaluateOperation);
    }
    const double squared = seconds * seconds;
    const PixelCoordinate position{.x = track.x[0] + track.x[1] * seconds + track.x[2] * squared,
                                   .y = track.y[0] + track.y[1] * seconds + track.y[2] * squared};
    if (!comet_internal::finitePixel(position))
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "the evaluated track position is finite",
                                       kEvaluateOperation);
    }
    return position;
}

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - shared validation and failure construction for comet registration.
//
// Comet detection, trajectory fitting and comet registration planning live in three translation
// units and refuse the same malformed inputs the same way; the track request is checked by one
// function here so a plan and a direct fit cannot disagree about what a valid request is.

#pragma once

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cmath>
#include <string>
#include <utility>

/// The comet operations' shared failure builder, position check and track-request validation.
namespace an::comet_internal
{

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] inline Status failure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a position holds two finite numbers.
///
/// @param pixel the position to inspect.
/// @return true when both components are finite.
[[nodiscard]] inline bool finitePixel(const PixelCoordinate& pixel) noexcept
{
    return std::isfinite(pixel.x) && std::isfinite(pixel.y);
}

/// Checks every field of a track request against the ranges `CometTracking.h` states.
///
/// @param request the request to check.
/// @param operation the public operation being validated.
/// @return success, or `ErrorCode::InvalidArgument` for the first field out of range.
[[nodiscard]] inline Status validateTrackRequest(const CometTrackRequest& request, const char* const operation)
{
    if (!isCometTrackModelPolicy(request.modelPolicy))
    {
        return failure(ErrorCode::InvalidArgument, "the track policy is not one this build implements", operation);
    }
    if (!std::isfinite(request.outlierSigma) || request.outlierSigma <= 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the outlier sigma is finite and positive", operation);
    }
    if (!std::isfinite(request.minimumOutlierResidualPixels) || request.minimumOutlierResidualPixels < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the outlier residual floor is finite and not negative", operation);
    }
    if (request.minimumQuadraticObservations < 3u ||
        request.minimumQuadraticObservations > kMaximumCometTrackObservations)
    {
        return failure(ErrorCode::InvalidArgument,
                       "the quadratic observation threshold lies between three and the observation ceiling", operation);
    }
    if (!std::isfinite(request.minimumQuadraticImprovement) || request.minimumQuadraticImprovement < 0.0 ||
        request.minimumQuadraticImprovement > 1.0)
    {
        return failure(ErrorCode::InvalidArgument, "the quadratic improvement lies in the unit interval", operation);
    }
    if (!std::isfinite(request.highResidualWarningPixels) || request.highResidualWarningPixels < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the high-residual warning is finite and not negative", operation);
    }
    if (!std::isfinite(request.minimumMotionWarningPixels) || request.minimumMotionWarningPixels < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the motion warning is finite and not negative", operation);
    }
    return Status::success();
}

} // namespace an::comet_internal

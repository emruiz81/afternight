// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/AstrometricSolution.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <limits>

// The ranges here are the ones a wrong answer hides in, which is why they are checked rather than
// normalised.
//
// A right ascension of 400 degrees is arithmetically the same direction as 40, and accepting it
// would make two solutions of one frame compare unequal while pointing at the same place. A
// declination of 100 is not a direction at all - it is a sign error or a swapped pair - and
// wrapping it would turn a solver's mistake into a plausible field somewhere else in the sky. So
// both are refused, and a caller that has an unnormalised angle normalises it where it knows what
// produced it.
//
// The reference pixel is deliberately not bounded by the image. A crop moves the reference point
// outside the frame it describes, and a solution that stayed valid under a crop is exactly what the
// typed model is for.

namespace an
{
namespace
{

/// Whether a value is a real number this engine will carry.
///
/// @param value the number to test.
/// @return true when it is finite.
[[nodiscard]] bool isFinite(double value) noexcept
{
    return value > -std::numeric_limits<double>::infinity() && value < std::numeric_limits<double>::infinity();
}

/// The refusal for a number that is not a position, a scale or an angle.
///
/// @param message what is wrong, for a person.
/// @param parameter which field, spelled as the caller wrote it.
/// @return an `ErrorCode::InvalidArgument` failure naming the field.
[[nodiscard]] Status refuseField(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = "AstrometricSolution::create",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

} // namespace

Result<AstrometricSolution> AstrometricSolution::create(const AstrometricSolutionRequest& request)
{
    if (!isFinite(request.referencePoint.rightAscensionDegrees) || request.referencePoint.rightAscensionDegrees < 0.0 ||
        request.referencePoint.rightAscensionDegrees >= 360.0)
    {
        return refuseField("a right ascension is a finite number of degrees in [0, 360)", "referencePoint");
    }
    if (!isFinite(request.referencePoint.declinationDegrees) || request.referencePoint.declinationDegrees < -90.0 ||
        request.referencePoint.declinationDegrees > 90.0)
    {
        return refuseField("a declination is a finite number of degrees in [-90, 90]", "referencePoint");
    }
    if (!isFinite(request.referencePixelX))
    {
        return refuseField("a reference pixel column is a finite number", "referencePixelX");
    }
    if (!isFinite(request.referencePixelY))
    {
        return refuseField("a reference pixel row is a finite number", "referencePixelY");
    }
    if (!isFinite(request.pixelScaleArcsecPerPixel) || request.pixelScaleArcsecPerPixel <= 0.0)
    {
        return refuseField("a pixel scale is a positive finite number of arcseconds", "pixelScaleArcsecPerPixel");
    }
    if (!isFinite(request.orientationDegrees) || request.orientationDegrees < 0.0 ||
        request.orientationDegrees >= 360.0)
    {
        return refuseField("an orientation is a finite number of degrees in [0, 360)", "orientationDegrees");
    }
    return AstrometricSolution{request};
}

} // namespace an

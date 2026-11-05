// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/RawCameraMetadata.h>

#include <cmath>

// The one check a raw decoder's camera calibration facts pass before they travel with an image.
//
// The values are copied out of a raw file by a decoder, so the check is about shape and finiteness
// rather than plausibility. A repeating black-offset tile is absent in both directions or present in
// both, holds exactly rows times columns entries and at most 4096 of them; the ISO speed, focal
// length and aperture are finite and not negative, zero meaning absent; and every multiplier and
// matrix entry is finite. A multiplier of zero or below is accepted, because that is how a decoder
// says a gain is unavailable, and deciding what to do without one belongs to whatever applies white
// balance rather than to this check.
//
namespace an
{
Status validateRawCameraMetadata(const RawCameraMetadata& metadata)
{
    const auto refuse = [](const char* parameter) {
        return Status::failure(ErrorCode::InvalidArgument,
                               {.message = "the camera metadata field has an invalid shape or numeric value",
                                .operation = "an::validateRawCameraMetadata",
                                .domain = ErrorDomain::Image,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
    };
    if ((metadata.blackPatternRows == 0u) != (metadata.blackPatternColumns == 0u) ||
        static_cast<std::uint64_t>(metadata.blackPatternRows) * metadata.blackPatternColumns !=
            metadata.blackPattern.size() ||
        metadata.blackPattern.size() > 4096u)
        return refuse("blackPattern");
    if (!std::isfinite(metadata.isoSpeed) || metadata.isoSpeed < 0.0)
        return refuse("isoSpeed");
    if (!std::isfinite(metadata.focalLengthMillimeters) || metadata.focalLengthMillimeters < 0.0)
        return refuse("focalLengthMillimeters");
    if (!std::isfinite(metadata.aperture) || metadata.aperture < 0.0)
        return refuse("aperture");
    for (unsigned c = 0u; c < 4u; ++c)
    {
        if (!std::isfinite(metadata.cameraMultipliers[c]))
            return refuse("cameraMultipliers");
        if (!std::isfinite(metadata.daylightMultipliers[c]))
            return refuse("daylightMultipliers");
        for (unsigned i = 0u; i < 3u; ++i)
        {
            if (!std::isfinite(metadata.cameraToRgb[i][c]))
                return refuse("cameraToRgb");
            if (!std::isfinite(metadata.cameraToXyz[c][i]))
                return refuse("cameraToXyz");
        }
    }
    return {};
}
} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The conversion of a persisted star profile's stars into the engine's detected-star records, and the read of a
/// profile file through the engine's reader.

#include "processes/StarProfileStars.h"

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/IO/StarProfileFile.h>

#include <algorithm>
#include <cmath>
#include <variant>

namespace process_utils
{

namespace
{

[[nodiscard]] an::PsfModel modelOf(an::PersistedPsfModel persisted) noexcept
{
    switch (persisted)
    {
    case an::PersistedPsfModel::MoffatFreeBeta:
        return an::PsfModel::MoffatFreeBeta;
    case an::PersistedPsfModel::MoffatFixedBeta:
        return an::PsfModel::MoffatFixedBeta;
    case an::PersistedPsfModel::Gaussian:
    default:
        return an::PsfModel::Gaussian;
    }
}

[[nodiscard]] an::DetectedStar starOf(const an::StarProfileV2Star& persisted)
{
    an::DetectedStar star;
    star.psf.background = persisted.psf.background;
    star.psf.amplitude = persisted.psf.amplitude;
    star.psf.centreX = persisted.psf.centreX;
    star.psf.centreY = persisted.psf.centreY;
    star.psf.scaleX = persisted.psf.scaleX;
    star.psf.scaleY = persisted.psf.scaleY;
    star.psf.fullWidthX = persisted.psf.fullWidthX;
    star.psf.fullWidthY = persisted.psf.fullWidthY;
    if (persisted.psf.fullWidthXArcseconds > 0.0)
    {
        star.psf.fullWidthXArcseconds = persisted.psf.fullWidthXArcseconds;
    }
    if (persisted.psf.fullWidthYArcseconds > 0.0)
    {
        star.psf.fullWidthYArcseconds = persisted.psf.fullWidthYArcseconds;
    }
    star.psf.angleDegrees = persisted.psf.angle;
    if (persisted.psf.profile != an::PersistedPsfModel::Gaussian)
    {
        star.psf.beta = persisted.psf.beta;
    }
    star.psf.residualRootMeanSquare = persisted.psf.residualRootMeanSquare;
    star.psf.saturationLevel = persisted.psf.saturation;
    star.psf.boxRadius = persisted.psf.boxRadius > 0 ? static_cast<std::uint32_t>(persisted.psf.boxRadius) : 0u;
    star.psf.saturated = persisted.psf.saturated;
    star.psf.model = modelOf(persisted.psf.profile);
    star.uncertainty.background = persisted.uncertainty.background;
    star.uncertainty.amplitude = persisted.uncertainty.amplitude;
    star.uncertainty.centreX = persisted.uncertainty.centreX;
    star.uncertainty.centreY = persisted.uncertainty.centreY;
    star.uncertainty.scaleX = persisted.uncertainty.scaleX;
    star.uncertainty.scaleY = persisted.uncertainty.scaleY;
    star.uncertainty.angle = persisted.uncertainty.angle;
    if (persisted.psf.profile != an::PersistedPsfModel::Gaussian)
    {
        star.uncertainty.beta = persisted.uncertainty.beta;
    }
    star.metrics.roundness = persisted.metrics.roundness;
    star.metrics.circularity = persisted.metrics.circularity;
    star.metrics.meanFullWidth = persisted.metrics.fullWidth;
    star.positionX = persisted.position.x;
    star.positionY = persisted.position.y;
    if (persisted.photometry.valid)
    {
        star.instrumentalMagnitude = persisted.photometry.magnitude;
        star.signalToNoise = persisted.photometry.signalToNoise;
    }
    star.channelIndex =
        persisted.astrometry.channel > 0 ? static_cast<std::uint32_t>(persisted.astrometry.channel) : 0u;
    return star;
}

[[nodiscard]] an::DetectedStar starOf(const an::StarProfileV1Star& persisted)
{
    // A first profile states each star as a circle of its radius; the engine's ellipse is four full widths across.
    an::DetectedStar star;
    const double fullWidth = (std::max)(persisted.radius, 0.0) / 2.0;
    star.psf.amplitude = persisted.intensity;
    star.psf.centreX = persisted.x;
    star.psf.centreY = persisted.y;
    star.psf.fullWidthX = fullWidth;
    star.psf.fullWidthY = fullWidth;
    star.psf.scaleX = fullWidth / an::kGaussianFullWidthPerSigma;
    star.psf.scaleY = star.psf.scaleX;
    star.psf.angleDegrees = 0.0;
    star.psf.model = an::PsfModel::Gaussian;
    star.metrics.roundness = 1.0;
    star.metrics.circularity = 1.0;
    star.metrics.meanFullWidth = fullWidth;
    star.positionX = persisted.x;
    star.positionY = persisted.y;
    return star;
}

} // namespace

an::ui::StarList starsOfProfile(const an::StarProfileFile& profile)
{
    an::ui::StarList stars;
    std::visit(
        [&stars](const auto& payload) {
            stars.reserve(payload.stars.size());
            for (const auto& persisted : payload.stars)
            {
                stars.push_back(starOf(persisted));
            }
        },
        profile.payload);
    return stars;
}

an::Result<an::ui::StarList> readProfileStars(const an::ExecutionContext& execution, std::string_view path)
{
    an::Result<an::StarProfileFile> profile = an::readStarProfileFile(execution, path);
    if (!profile.hasValue())
    {
        return profile.status();
    }
    return starsOfProfile(*profile);
}

} // namespace process_utils

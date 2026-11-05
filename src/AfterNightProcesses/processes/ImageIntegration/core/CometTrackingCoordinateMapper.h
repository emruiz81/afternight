// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Where a comet position lies in each plane of a comet registration: the distorted source frame a person or the
/// tracker measured it in, the frame the refinement reads - another file of the same frame, through the transform
/// between them - and the comet's reference plane, which is the source frame with its distortion undone.
///
/// Every position is an engine pixel coordinate, the first pixel's centre at (0.5, 0.5).

#include "processes/ImageIntegration/core/CometTrackingFrameContextBuilder.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QString>

#include <cmath>
#include <optional>

namespace an::ui
{
struct CometTrackingCoordinateMapping
{
    /// The map from the source frame to the file the refinement reads, when that file is another of the frame.
    std::optional<an::RegistrationTransform> sourceToRefinementTransform;

    /// The source frame's distortion, when its solution states one.
    std::optional<an::SipDistortion> sourceDistortion;
};

struct CometTrackingCoordinateMappingResult
{
    bool succeeded = false;
    an::PixelCoordinate point{};
    QString message;
};

[[nodiscard]] inline CometTrackingCoordinateMappingResult cometMappingFailure(const QString& message)
{
    return {.succeeded = false, .message = message};
}

[[nodiscard]] inline bool cometMappingPointIsFinite(const an::PixelCoordinate& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] inline CometTrackingCoordinateMappingResult cometMappingSuccess(const an::PixelCoordinate& point)
{
    if (!cometMappingPointIsFinite(point))
    {
        return cometMappingFailure(QStringLiteral("Mapped comet coordinate is not finite."));
    }
    return {.succeeded = true, .point = point};
}

[[nodiscard]] inline CometTrackingCoordinateMappingResult
mapCometSourceToReferencePlane(const CometTrackingCoordinateMapping& mapping,
                               const an::PixelCoordinate& source_position)
{
    if (!cometMappingPointIsFinite(source_position))
    {
        return cometMappingFailure(QStringLiteral("Source comet coordinate is not finite."));
    }
    if (!mapping.sourceDistortion.has_value())
    {
        return cometMappingSuccess(source_position);
    }
    const an::Result<an::PixelCoordinate> corrected = an::applySipForward(*mapping.sourceDistortion, source_position);
    if (!corrected.hasValue())
    {
        return cometMappingFailure(QStringLiteral("SIP forward correction failed for source comet coordinate."));
    }
    return cometMappingSuccess(*corrected);
}

[[nodiscard]] inline CometTrackingCoordinateMappingResult
mapCometReferencePlaneToSource(const CometTrackingCoordinateMapping& mapping,
                               const an::PixelCoordinate& reference_position)
{
    if (!cometMappingPointIsFinite(reference_position))
    {
        return cometMappingFailure(QStringLiteral("Reference-plane comet coordinate is not finite."));
    }
    if (!mapping.sourceDistortion.has_value())
    {
        return cometMappingSuccess(reference_position);
    }
    const an::Result<an::SipInverseResult> source = an::applySipInverse(*mapping.sourceDistortion, reference_position);
    if (!source.hasValue())
    {
        return cometMappingFailure(QStringLiteral("SIP inverse correction failed for source comet coordinate."));
    }
    return cometMappingSuccess(source->pixel);
}

[[nodiscard]] inline CometTrackingCoordinateMappingResult
mapCometSourceToRefinement(const CometTrackingCoordinateMapping& mapping, const an::PixelCoordinate& source_position)
{
    if (!cometMappingPointIsFinite(source_position))
    {
        return cometMappingFailure(QStringLiteral("Source comet coordinate is not finite."));
    }
    if (!mapping.sourceToRefinementTransform.has_value())
    {
        return cometMappingSuccess(source_position);
    }
    const an::Result<an::PixelCoordinate> refinement =
        an::mapRegistrationPoint(*mapping.sourceToRefinementTransform, source_position);
    if (!refinement.hasValue())
    {
        return cometMappingFailure(QStringLiteral("Failed to map source comet coordinate to refinement source."));
    }
    return cometMappingSuccess(*refinement);
}

[[nodiscard]] inline CometTrackingCoordinateMappingResult
mapCometRefinementToSource(const CometTrackingCoordinateMapping& mapping,
                           const an::PixelCoordinate& refinement_position)
{
    if (!cometMappingPointIsFinite(refinement_position))
    {
        return cometMappingFailure(QStringLiteral("Refinement comet coordinate is not finite."));
    }
    if (!mapping.sourceToRefinementTransform.has_value())
    {
        return cometMappingSuccess(refinement_position);
    }
    const an::Result<an::RegistrationInverseResult> source =
        an::mapRegistrationPointInverse(*mapping.sourceToRefinementTransform, refinement_position);
    if (!source.hasValue())
    {
        return cometMappingFailure(
            QStringLiteral("Unable to invert source-to-refinement transform for comet coordinate."));
    }
    return cometMappingSuccess(source->pixel);
}

[[nodiscard]] inline CometTrackingCoordinateMappingResult
mapCometReferencePlaneToRefinement(const CometTrackingCoordinateMapping& mapping,
                                   const an::PixelCoordinate& reference_position)
{
    const CometTrackingCoordinateMappingResult source = mapCometReferencePlaneToSource(mapping, reference_position);
    if (!source.succeeded)
    {
        return source;
    }
    return mapCometSourceToRefinement(mapping, source.point);
}

[[nodiscard]] inline CometTrackingCoordinateMappingResult
mapCometRefinementToReferencePlane(const CometTrackingCoordinateMapping& mapping,
                                   const an::PixelCoordinate& refinement_position)
{
    const CometTrackingCoordinateMappingResult source = mapCometRefinementToSource(mapping, refinement_position);
    if (!source.succeeded)
    {
        return source;
    }
    return mapCometSourceToReferencePlane(mapping, source.point);
}
} // namespace an::ui

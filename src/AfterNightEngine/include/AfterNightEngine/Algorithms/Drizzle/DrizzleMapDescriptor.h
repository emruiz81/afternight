// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One frame's source-to-destination geometry as an immutable, owned description; the coordinate
/// arrays of a map belong to whoever generates them.
///
/// `DrizzleMappingRequest` states the three facts a frame's mapping needs - the registration
/// transform, a post-translation onto the output canvas and an optional source SIP - and
/// `DrizzleMapDescriptor::create` binds them to the source extent, output extent and scale of a
/// checked `DrizzleModel`. The descriptor is the geometry's identity: `DrizzleGeometry.h` prepares
/// and caches it, `DrizzlePixmap.h` generates coordinates from it, and `DrizzleMapIdentity.h`
/// serializes it. Two conventions are fixed rather than chosen, and the two enumerations here name
/// them: a stored map value puts a pixel's centre at its integer index, and a position that cannot
/// be mapped is written as a pair of quiet NaNs.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

namespace an
{

/// Mapping values use the sampling convention, including the half-pixel adjustment on enlargement.
enum class DrizzleCoordinateConvention : std::uint8_t
{
    IntegerPixelCenters ///< Pixel (0,0) has its centre at (0,0); its boundary is [-0.5,0.5] on each axis.
};

/// What an unrepresentable or failed mapping writes to both coordinate planes.
enum class DrizzleInvalidCoordinate : std::uint8_t
{
    PairedNonFinite ///< Both components are quiet NaN; a finite coordinate outside the canvas is still a valid map.
};

/// Geometry for one source frame, in the registration API's centre-at-one-half convention.
struct DrizzleMappingRequest
{
    RegistrationTransform transform{}; ///< Source-to-reference model; default identity, including polynomial terms.
    PixelCoordinate postTranslation{}; ///< Finite reference-pixel offset, applied after the transform, before scaling.
    std::optional<SipDistortion>
        sourceDistortion{}; ///< Owned source SIP; its forward correction precedes registration.
};

/// Immutable descriptor from which bounded or dense coordinate maps can be reproduced.
///
/// For a sampling position p, compute s = p + (0.5,0.5), apply the source's forward SIP to s,
/// apply the registration transform, then produce (s + postTranslation) * scale - (0.5,0.5).
/// Thus identity maps an integer centre x to x*scale + 0.5*(scale-1). The registration transform
/// and SIP reference pixel already use engine coordinates; a transform written for integer pixel
/// centres must be conjugated at its boundary, never copied numerically into this descriptor.
/// Square-drop corners are not composed one by one: they are interpolated from the mapped centres,
/// with a one-row interpolation halo and extrapolation at image edges. Failed mappings propagate
/// as paired NaNs; nothing clamps them to the canvas or falls back to a different map.
///
/// Copies own all transform and SIP coefficients; concurrent immutable reads are safe. This is
/// geometry identity only: a result cache must also key source revisions, masks and kernel choices.
class DrizzleMapDescriptor
{
public:
    /// Binds a mapping to the source, output and scale of a checked sequence.
    /// Thread-safe, no sample access or map allocation.
    /// @param model the checked sequence.
    /// @param request the owned mapping facts.
    /// @return the descriptor; `ErrorCode::InvalidArgument` for a non-finite post-translation; or
    ///         `ErrorCode::IncompatibleArguments` for an inverse-only source SIP, since a
    ///         source-to-destination map needs its forward model.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DrizzleMapDescriptor> create(const DrizzleModel& model,
                                                                                  const DrizzleMappingRequest& request);

    /// The mapping facts, including every coefficient.
    /// @return a borrow valid until this descriptor is assigned or destroyed.
    [[nodiscard]] const DrizzleMappingRequest& mapping() const noexcept
    {
        return m_mapping;
    }
    /// The source pixel and plane counts.
    /// @return the checked source extent.
    [[nodiscard]] const ImageExtent& sourceExtent() const noexcept
    {
        return m_source;
    }
    /// The output pixel and plane counts.
    /// @return the checked destination extent.
    [[nodiscard]] const ImageExtent& outputExtent() const noexcept
    {
        return m_output;
    }
    /// Equal-axis enlargement.
    /// @return the finite scale in [1,4].
    [[nodiscard]] double scale() const noexcept
    {
        return m_scale;
    }
    /// The convention a produced coordinate map uses.
    /// @return integer pixel centres.
    [[nodiscard]] constexpr DrizzleCoordinateConvention coordinateConvention() const noexcept
    {
        return DrizzleCoordinateConvention::IntegerPixelCenters;
    }
    /// The representation of an unmapped sample.
    /// @return paired non-finite components.
    [[nodiscard]] constexpr DrizzleInvalidCoordinate invalidCoordinate() const noexcept
    {
        return DrizzleInvalidCoordinate::PairedNonFinite;
    }

private:
    /// Copies checked geometry.
    /// @param model the sequence geometry.
    /// @param mapping the checked frame geometry.
    DrizzleMapDescriptor(const DrizzleModel& model, const DrizzleMappingRequest& mapping)
        : m_mapping(mapping), m_source(model.request().input.extent()), m_output(model.outputDescriptor().extent()),
          m_scale(model.request().parameters.scale)
    {
    }
    DrizzleMappingRequest m_mapping; ///< Owned transform and source SIP.
    ImageExtent m_source; ///< Checked source geometry.
    ImageExtent m_output; ///< Checked output geometry.
    double m_scale; ///< Checked enlargement.
};

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The plane a registered group is aligned onto: its canvas, decided from the registration's transforms and the
/// group's canvas mode, and the world coordinates it carries - the reference frame's solution moved onto it. The
/// registration stage records the plane's solution, the alignment stage writes onto it, and the stages after them
/// check a record against it.
///
/// The registration stage registers its frames without a separate distortion model - a polynomial transform carries
/// whatever curvature the stars or the solutions showed - but a comet-aligned frame, or a record that inherits its
/// frame's distortion, can state the distortion its transform was solved over. The plane is bounded with each frame's
/// distortion corrected first, and states that a source distortion was applied when any frame carried one.
///
/// **One plane for every route.** An intersection no area is common to is replaced by the standard canvas wherever a
/// plane is planned - the alignment, the stack that warps the lights as it reads them, the drizzle and every stage that
/// checks a record against them - and the planned plane states the mode it was planned by, so a record of it names
/// that mode. A route that places the lights says so in `intersectionReplacedWarning`'s words.

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Result.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace stage_utils
{

/// One registered frame as the plane is planned from it: its transform onto the reference, its extent and the source
/// distortion its transform was solved over.
struct RegistrationOutputPlaneFrame
{
    an::RegistrationTransform transform;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::optional<an::SipDistortion> distortion;
};

/// The engine's canvas a group's canvas mode names: the reference frame's for the standard canvas, the union for a
/// mosaic, the intersection for an intersection.
[[nodiscard]] an::RegistrationCanvasMode engineCanvasMode(ImageIntegration::RegistrationCanvasMode mode) noexcept;

/// A group's plane as it is planned: the canvas and the canvas mode it was planned by.
struct PlannedRegistrationOutputPlane
{
    an::RegistrationCanvas canvas;

    /// The group's canvas mode, or the standard canvas's when it stands in for an intersection.
    ImageIntegration::RegistrationCanvasMode usedMode = ImageIntegration::RegistrationCanvasMode::Standard;

    /// Whether the standard canvas stands in for an intersection no area is common to.
    bool intersectionReplaced = false;
};

/// A drizzled group's canvas as it is planned, and the canvas mode it was planned by.
struct PlannedDrizzleCanvas
{
    an::DrizzleCanvas canvas;

    /// The group's canvas mode, or the standard canvas's when it stands in for an intersection.
    ImageIntegration::RegistrationCanvasMode usedMode = ImageIntegration::RegistrationCanvasMode::Standard;

    /// Whether the standard canvas stands in for an intersection no whole output pixel is common to.
    bool intersectionReplaced = false;
};

/// The canvas a group's registered frames are aligned onto: the group's canvas mode's, or the standard canvas when the
/// group asks for an intersection no area is common to.
///
/// @param frames each registered frame.
/// @param referenceIndex the reference's position among them.
/// @param mode the group's canvas mode.
/// @return the plane; or what bounding a frame or planning the canvas refused.
[[nodiscard]] an::Result<PlannedRegistrationOutputPlane>
planRegistrationOutputPlane(std::span<const RegistrationOutputPlaneFrame> frames, std::size_t referenceIndex,
                            ImageIntegration::RegistrationCanvasMode mode);

/// The canvas a drizzled group's frames are drizzled onto: the group's canvas mode's at the scale and drop fraction,
/// or the standard canvas when the group asks for an intersection no whole output pixel is common to.
///
/// @param frames each registered frame.
/// @param referenceIndex the reference's position among them.
/// @param mode the group's canvas mode.
/// @param scale the enlargement.
/// @param pixelFraction the drop's width as a fraction of an input pixel.
/// @return the canvas; or what bounding a frame or planning the canvas refused.
[[nodiscard]] an::Result<PlannedDrizzleCanvas>
planDrizzleOutputPlane(std::span<const RegistrationOutputPlaneFrame> frames, std::size_t referenceIndex,
                       ImageIntegration::RegistrationCanvasMode mode, double scale, double pixelFraction);

/// What a route that places a group's lights says when the standard canvas stands in for its intersection.
///
/// @param groupLabel the group, as the route's log names it.
/// @return the sentence.
[[nodiscard]] QString intersectionReplacedWarning(const QString& groupLabel);

/// The solution a plane carries: the reference frame's, moved by the canvas origin, over the canvas's extent. When the
/// alignment undoes the reference frame's own distortion its pixels are no longer distorted, so the plane's solution
/// carries none; otherwise it keeps the reference's.
///
/// @param reference the reference frame's solution.
/// @param canvas the plane.
/// @param referenceDistortionUndone whether the alignment undoes the reference frame's distortion.
/// @return the plane's solution; or what moving or describing it refused.
[[nodiscard]] an::Result<an::WcsSolution> registrationOutputPlaneSolution(const an::WcsSolution& reference,
                                                                          const an::RegistrationCanvas& canvas,
                                                                          bool referenceDistortionUndone);

/// The plane's extent, for checking an output-plane record against the registration it describes.
///
/// @param frames each registered frame.
/// @param referenceIndex the reference's position among them.
/// @param mode the group's canvas mode.
/// @param error the reason, when nothing is answered.
/// @return the extent and whether any frame's source distortion was applied; nothing when the plane cannot be
///         planned.
[[nodiscard]] std::optional<RegistrationOutputPlaneExpectations>
computeRegistrationOutputPlaneExpectations(std::span<const RegistrationOutputPlaneFrame> frames,
                                           std::size_t referenceIndex, ImageIntegration::RegistrationCanvasMode mode,
                                           QString& error);

} // namespace stage_utils

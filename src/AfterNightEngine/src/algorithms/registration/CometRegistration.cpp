// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - comet coordinate conversion, aperture projection and registration planning.
//
// Comet registration composes each frame's star registration with a translation that holds the
// comet still. Planning carries every accepted comet measurement into the common reference plane -
// through the frame's plate distortion and its source-to-reference transform, as its coordinate
// space requires - and execution fits one track through them and asks it where the comet was at
// every frame's midpoint. The per-frame translation is the reference frame's track position less
// that frame's, so the warp that follows moves the comet onto its reference position. Positions are
// engine pixel coordinates, whose first pixel centre is (0.5, 0.5).

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/registration/CometInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <numbers>
#include <span>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kSipOperation = "an::diagnoseCometSipInverse";
constexpr const char* kApertureOperation = "an::projectCometAperture";
constexpr const char* kPlanOperation = "an::planCometRegistration";
constexpr const char* kExecuteOperation = "an::executeCometRegistration";
constexpr const char* kWarpOperation = "an::planCometRegistrationWarp";

/// Whether a position lies within the span of a frame's pixel centres.
///
/// @param point the position, in engine pixel coordinates.
/// @param width the frame's width.
/// @param height the frame's height.
/// @return true when x is in `[0.5, width - 0.5]` and y in `[0.5, height - 0.5]`.
[[nodiscard]] bool pointInFrame(const PixelCoordinate& point, const std::uint32_t width,
                                const std::uint32_t height) noexcept
{
    return point.x >= 0.5 && point.x <= static_cast<double>(width) - 0.5 && point.y >= 0.5 &&
           point.y <= static_cast<double>(height) - 0.5;
}

/// Carries one comet measurement into the common reference plane.
///
/// A source-pixel measurement is corrected by the frame's plate distortion and then mapped by the
/// transform; a distortion-corrected one is only mapped; a reference-plane one is returned as it
/// is. Both source spaces must lie within the frame's span of pixel centres.
///
/// @param frame the frame the measurement belongs to.
/// @param observation the measurement and its coordinate space.
/// @param transform the source-to-reference model in use, which is the identity when the plan does
///        not retain the base registration.
/// @return the reference-plane position; `ErrorCode::InvalidArgument` for an unknown space or a
///         position that is not finite; `ErrorCode::OutOfRange` for a source position outside the
///         frame; or the distortion's or the transform's failure.
[[nodiscard]] Result<PixelCoordinate> observationInReference(const CometRegistrationFrame& frame,
                                                             const CometRegistrationObservation& observation,
                                                             const RegistrationTransform& transform)
{
    if (!isCometCoordinateSpace(observation.space) || !comet_internal::finitePixel(observation.position))
    {
        return comet_internal::failure(ErrorCode::InvalidArgument,
                                       "a comet observation has a known coordinate space and finite position",
                                       kPlanOperation);
    }
    if (observation.space != CometCoordinateSpace::ReferencePixels &&
        !pointInFrame(observation.position, frame.width, frame.height))
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "a source-plane comet observation lies inside its frame",
                                       kPlanOperation);
    }
    PixelCoordinate corrected = observation.position;
    if (observation.space == CometCoordinateSpace::SourcePixels)
    {
        const Result<PixelCoordinate> sip = applySipForward(frame.sourceDistortion, observation.position);
        if (!sip.hasValue())
        {
            return sip.status();
        }
        corrected = *sip;
    }
    if (observation.space == CometCoordinateSpace::ReferencePixels)
    {
        return corrected;
    }
    return mapRegistrationPoint(transform, corrected);
}

/// Builds a comet registration plan, letting an allocation failure escape as an exception.
///
/// A frame without a midpoint is left out, and fails the plan when it is required. A frame without
/// an accepted measurement is kept for prediction when prediction is allowed; otherwise it is left
/// out, and fails the plan when it is required. When the request names no coefficient origin, the
/// reference frame's midpoint becomes it.
///
/// @param frames the frames and their measurements.
/// @param referenceFrameId the frame the comet is held at.
/// @param request the track request and the planning policy.
/// @return the plan, or a returned failure.
[[nodiscard]] Result<CometRegistrationPlan>
planCometRegistrationUnguarded(const std::span<const CometRegistrationFrame> frames,
                               const std::uint64_t referenceFrameId, const CometRegistrationRequest& request)
{
    const Status trackReady = comet_internal::validateTrackRequest(request.track, kPlanOperation);
    if (trackReady.failed())
    {
        return trackReady;
    }
    if (frames.empty())
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "a comet registration set is not empty",
                                       kPlanOperation);
    }
    if (frames.size() > kMaximumCometTrackObservations)
    {
        return comet_internal::failure(ErrorCode::LimitExceeded, "the frame set exceeds the comet-track ceiling",
                                       kPlanOperation);
    }

    CometRegistrationPlan plan;
    plan.referenceFrameId = referenceFrameId;
    plan.request = request;
    plan.frames.reserve(frames.size());
    plan.trackObservations.reserve(frames.size());
    bool referenceFound = false;
    for (std::size_t index = 0u; index < frames.size(); ++index)
    {
        const CometRegistrationFrame& frame = frames[index];
        for (std::size_t earlier = 0u; earlier < index; ++earlier)
        {
            if (frames[earlier].frameId == frame.frameId)
            {
                return comet_internal::failure(ErrorCode::InvalidArgument,
                                               "comet registration frame identities are unique", kPlanOperation);
            }
        }
        if (!frame.midpoint.has_value())
        {
            if (frame.required)
            {
                return comet_internal::failure(ErrorCode::InvalidData,
                                               "a required comet registration frame has an exposure midpoint",
                                               kPlanOperation);
            }
            continue;
        }
        if (frame.width == 0u || frame.height == 0u)
        {
            return comet_internal::failure(ErrorCode::InvalidArgument,
                                           "a retained comet registration frame has nonzero dimensions",
                                           kPlanOperation);
        }

        const RegistrationTransform transform =
            request.retainBaseRegistration ? frame.sourceToReference : RegistrationTransform{};
        std::optional<PixelCoordinate> referenceObservation;
        if (frame.observation.has_value() && frame.observation->accepted)
        {
            const Result<PixelCoordinate> mapped = observationInReference(frame, *frame.observation, transform);
            if (!mapped.hasValue())
            {
                return mapped.status();
            }
            referenceObservation = *mapped;
            plan.trackObservations.push_back({.frameId = frame.frameId,
                                              .midpoint = *frame.midpoint,
                                              .position = *mapped,
                                              .accepted = true,
                                              .required = frame.required});
        }
        else if (!request.allowPredictedPositions && frame.required)
        {
            return comet_internal::failure(ErrorCode::InvalidData,
                                           "a required frame without an observation needs prediction enabled",
                                           kPlanOperation);
        }
        else if (!request.allowPredictedPositions)
        {
            continue;
        }

        plan.frames.push_back({.frameId = frame.frameId,
                               .midpoint = *frame.midpoint,
                               .width = frame.width,
                               .height = frame.height,
                               .sourceToReference = transform,
                               .sourceDistortion = frame.sourceDistortion,
                               .referenceObservation = referenceObservation,
                               .required = frame.required});
        referenceFound = referenceFound || frame.frameId == referenceFrameId;
    }
    if (!referenceFound)
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "the reference identity names a retained frame",
                                       kPlanOperation);
    }
    if (!plan.request.track.coefficientOrigin.has_value())
    {
        const auto reference = std::find_if(plan.frames.begin(), plan.frames.end(),
                                            [&](const auto& frame) { return frame.frameId == referenceFrameId; });
        plan.request.track.coefficientOrigin = reference->midpoint;
    }
    if (plan.trackObservations.size() < 2u)
    {
        return comet_internal::failure(ErrorCode::EmptySelection,
                                       "a comet registration needs at least two accepted positions", kPlanOperation);
    }
    plan.valid = true;
    return plan;
}

/// Re-checks a plan before execution, because a plan is a public aggregate a caller may edit.
///
/// Every frame must keep valid geometry, a unique identity and a finite measurement or a
/// prediction allowance; the reference frame must still be present; and each track observation
/// must still describe exactly one planned frame's measurement.
///
/// @param execution the operation, for cancellation.
/// @param plan the plan to check.
/// @return success; `ErrorCode::InvalidArgument` for the first broken rule;
///         `ErrorCode::LimitExceeded` above the track ceiling; or the cancellation.
[[nodiscard]] Status validateExecutablePlan(const ExecutionContext& execution, const CometRegistrationPlan& plan)
{
    if (!plan.valid || plan.frames.empty() || plan.trackObservations.size() < 2u)
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "execution needs a complete comet registration plan",
                                       kExecuteOperation);
    }
    if (plan.frames.size() > kMaximumCometTrackObservations ||
        plan.trackObservations.size() > kMaximumCometTrackObservations)
    {
        return comet_internal::failure(ErrorCode::LimitExceeded, "the modified comet plan exceeds the track ceiling",
                                       kExecuteOperation);
    }
    const Status requestReady = comet_internal::validateTrackRequest(plan.request.track, kExecuteOperation);
    if (requestReady.failed())
    {
        return requestReady;
    }
    bool referenceFound = false;
    for (std::size_t index = 0u; index < plan.frames.size(); ++index)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        const CometRegistrationFramePlan& frame = plan.frames[index];
        if (frame.width == 0u || frame.height == 0u ||
            (frame.referenceObservation.has_value() && !comet_internal::finitePixel(*frame.referenceObservation)) ||
            (!frame.referenceObservation.has_value() && !plan.request.allowPredictedPositions))
        {
            return comet_internal::failure(ErrorCode::InvalidArgument,
                                           "every planned frame retains valid geometry and observation",
                                           kExecuteOperation);
        }
        for (std::size_t earlier = 0u; earlier < index; ++earlier)
        {
            if (plan.frames[earlier].frameId == frame.frameId)
            {
                return comet_internal::failure(ErrorCode::InvalidArgument, "planned frame identities remain unique",
                                               kExecuteOperation);
            }
        }
        referenceFound = referenceFound || frame.frameId == plan.referenceFrameId;
    }
    if (!referenceFound)
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "the planned reference frame is still present",
                                       kExecuteOperation);
    }
    if (plan.trackObservations.size() > plan.frames.size())
    {
        return comet_internal::failure(ErrorCode::InvalidArgument,
                                       "planned observations fit inside the retained frame set", kExecuteOperation);
    }
    for (const CometTrackObservation& observation : plan.trackObservations)
    {
        const Status checkpoint = execution.checkCancellation();
        if (checkpoint.failed())
        {
            return checkpoint;
        }
        const auto frame = std::find_if(plan.frames.begin(), plan.frames.end(), [&](const auto& candidate) {
            return candidate.frameId == observation.frameId;
        });
        if (frame == plan.frames.end() || !frame->referenceObservation.has_value() || !observation.accepted ||
            observation.midpoint != frame->midpoint || observation.position != *frame->referenceObservation ||
            observation.required != frame->required)
        {
            return comet_internal::failure(ErrorCode::InvalidArgument,
                                           "each track observation still describes its planned frame",
                                           kExecuteOperation);
        }
    }
    for (const CometRegistrationFramePlan& frame : plan.frames)
    {
        if (!frame.referenceObservation.has_value())
        {
            continue;
        }
        const std::size_t matches = static_cast<std::size_t>(
            std::count_if(plan.trackObservations.begin(), plan.trackObservations.end(),
                          [&](const auto& observation) { return observation.frameId == frame.frameId; }));
        if (matches != 1u)
        {
            return comet_internal::failure(ErrorCode::InvalidArgument,
                                           "each planned measurement has exactly one track observation",
                                           kExecuteOperation);
        }
    }
    return Status::success();
}

} // namespace

Result<CometSipInverseDiagnostic> diagnoseCometSipInverse(const SipDistortion& distortion,
                                                          const PixelCoordinate& undistortedPosition)
{
    const Result<SipInverseResult> inverse = applySipInverse(distortion, undistortedPosition);
    if (!inverse.hasValue())
    {
        return inverse.status();
    }
    if (!isSipInverseRoute(inverse->route))
    {
        return comet_internal::failure(ErrorCode::Internal, "the SIP adapter returned an unknown inverse route",
                                       kSipOperation);
    }
    return CometSipInverseDiagnostic{.correctedPosition = inverse->pixel,
                                     .route = inverse->route,
                                     .iterations = inverse->iterations,
                                     .residualPixels = inverse->residualPixels};
}

Result<CometApertureProjection> projectCometAperture(const ExecutionContext& execution,
                                                     const CometApertureProjectionRequest& request)
{
    if (!execution.valid())
    {
        return comet_internal::failure(ErrorCode::NotInitialized, "an aperture projection needs an execution context",
                                       kApertureOperation);
    }
    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        return stopped;
    }
    if (!comet_internal::finitePixel(request.sourceCenter) || !comet_internal::finitePixel(request.postTranslation) ||
        !std::isfinite(request.sourceRadiusPixels) || request.sourceRadiusPixels <= 0.0 || request.sourceWidth == 0u ||
        request.sourceHeight == 0u || request.outputWidth == 0u || request.outputHeight == 0u ||
        request.boundarySamples < 8u || request.boundarySamples > kMaximumCometApertureBoundarySamples)
    {
        return comet_internal::failure(ErrorCode::InvalidArgument,
                                       "an aperture has finite geometry, nonzero extents and a supported sample count",
                                       kApertureOperation);
    }
    if (!pointInFrame(request.sourceCenter, request.sourceWidth, request.sourceHeight))
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "the aperture centre lies inside its source frame",
                                       kApertureOperation);
    }

    try
    {
        CometApertureProjection result;
        result.sourceCenter = request.sourceCenter;
        const Result<PixelCoordinate> correctedCenter = applySipForward(request.sourceDistortion, request.sourceCenter);
        if (!correctedCenter.hasValue())
        {
            return correctedCenter.status();
        }
        const Result<PixelCoordinate> mappedCenter =
            mapRegistrationPoint(request.sourceToReference, *correctedCenter, request.postTranslation);
        if (!mappedCenter.hasValue())
        {
            return mappedCenter.status();
        }
        result.outputCenter = *mappedCenter;
        result.outputClipped = !pointInFrame(result.outputCenter, request.outputWidth, request.outputHeight);
        result.sourcePolygon.reserve(request.boundarySamples);
        result.outputPolygon.reserve(request.boundarySamples);
        // The circle is sampled at equal angles; each vertex is clamped to the span of pixel
        // centres in the source frame before it is mapped and in the output frame after, and either
        // clamp is reported rather than hidden.
        const double sourceMaximumX = static_cast<double>(request.sourceWidth) - 0.5;
        const double sourceMaximumY = static_cast<double>(request.sourceHeight) - 0.5;
        const double outputMaximumX = static_cast<double>(request.outputWidth) - 0.5;
        const double outputMaximumY = static_cast<double>(request.outputHeight) - 0.5;
        for (std::uint32_t index = 0u; index < request.boundarySamples; ++index)
        {
            const Status checkpoint = execution.checkCancellation();
            if (checkpoint.failed())
            {
                return checkpoint;
            }
            const double angle =
                2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(request.boundarySamples);
            const PixelCoordinate raw{.x = request.sourceCenter.x + request.sourceRadiusPixels * std::cos(angle),
                                      .y = request.sourceCenter.y + request.sourceRadiusPixels * std::sin(angle)};
            const PixelCoordinate source{.x = (std::clamp)(raw.x, 0.5, sourceMaximumX),
                                         .y = (std::clamp)(raw.y, 0.5, sourceMaximumY)};
            result.sourceClipped = result.sourceClipped || source != raw;
            const Result<PixelCoordinate> corrected = applySipForward(request.sourceDistortion, source);
            if (!corrected.hasValue())
            {
                return corrected.status();
            }
            const Result<PixelCoordinate> mapped =
                mapRegistrationPoint(request.sourceToReference, *corrected, request.postTranslation);
            if (!mapped.hasValue())
            {
                return mapped.status();
            }
            const PixelCoordinate output{.x = (std::clamp)(mapped->x, 0.5, outputMaximumX),
                                         .y = (std::clamp)(mapped->y, 0.5, outputMaximumY)};
            result.outputClipped = result.outputClipped || output != *mapped;
            result.sourcePolygon.push_back(source);
            result.outputPolygon.push_back(output);
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return comet_internal::failure(ErrorCode::AllocationFailed,
                                       "the aperture polygons could not allocate their storage", kApertureOperation);
    }
}

const char* cometCoordinateSpaceName(const CometCoordinateSpace space) noexcept
{
    switch (space)
    {
    case CometCoordinateSpace::SourcePixels:
        return "SourcePixels";
    case CometCoordinateSpace::DistortionCorrectedSourcePixels:
        return "DistortionCorrectedSourcePixels";
    case CometCoordinateSpace::ReferencePixels:
        return "ReferencePixels";
    }
    return "UnknownCometCoordinateSpace";
}

Result<CometRegistrationPlan> planCometRegistration(const std::span<const CometRegistrationFrame> frames,
                                                    const std::uint64_t referenceFrameId,
                                                    const CometRegistrationRequest& request)
{
    try
    {
        return planCometRegistrationUnguarded(frames, referenceFrameId, request);
    }
    catch (const std::bad_alloc&)
    {
        return comet_internal::failure(ErrorCode::AllocationFailed,
                                       "the comet registration plan could not allocate its storage", kPlanOperation);
    }
}

Result<CometRegistrationResult> executeCometRegistration(const ExecutionContext& execution,
                                                         const CometRegistrationPlan& plan)
{
    if (!execution.valid())
    {
        return comet_internal::failure(ErrorCode::NotInitialized, "comet registration needs an execution context",
                                       kExecuteOperation);
    }
    const Status stopped = execution.checkCancellation();
    if (stopped.failed())
    {
        return stopped;
    }
    const Status planReady = validateExecutablePlan(execution, plan);
    if (planReady.failed())
    {
        return planReady;
    }
    const Result<CometTrackResult> track = fitCometTrack(execution, plan.trackObservations, plan.request.track);
    if (!track.hasValue())
    {
        return track.status();
    }
    const auto reference = std::find_if(plan.frames.begin(), plan.frames.end(),
                                        [&](const auto& frame) { return frame.frameId == plan.referenceFrameId; });
    const Result<PixelCoordinate> fixed = evaluateCometTrack(*track, reference->midpoint);
    if (!fixed.hasValue())
    {
        return fixed.status();
    }

    try
    {
        CometRegistrationResult result;
        result.referenceFrameId = plan.referenceFrameId;
        result.track = *track;
        result.referencePosition = *fixed;
        result.peakWorkingBytes = track->peakWorkingBytes;
        result.frames.reserve(plan.frames.size());
        execution.reportProgress(0u, plan.frames.size(), "comet registration: resolve frames");
        for (const CometRegistrationFramePlan& frame : plan.frames)
        {
            const Status checkpoint = execution.checkCancellation();
            if (checkpoint.failed())
            {
                return checkpoint;
            }
            const Result<PixelCoordinate> evaluated = evaluateCometTrack(*track, frame.midpoint);
            if (!evaluated.hasValue())
            {
                return evaluated.status();
            }
            const PixelCoordinate position = *evaluated;
            const bool observationConstrained = frame.referenceObservation.has_value();
            const bool predictionOnly = !observationConstrained;
            const PixelCoordinate translation{.x = fixed->x - position.x, .y = fixed->y - position.y};
            if (!comet_internal::finitePixel(translation))
            {
                return comet_internal::failure(ErrorCode::OutOfRange, "a resolved comet translation is finite",
                                               kExecuteOperation);
            }
            result.frames.push_back({.frameId = frame.frameId,
                                     .midpoint = frame.midpoint,
                                     .width = frame.width,
                                     .height = frame.height,
                                     .sourceToReference = frame.sourceToReference,
                                     .sourceDistortion = frame.sourceDistortion,
                                     .trackPositionInReference = position,
                                     .postTranslation = translation,
                                     .observationConstrained = observationConstrained,
                                     .predictionOnly = predictionOnly});
            execution.reportProgress(result.frames.size(), plan.frames.size(), "comet registration: resolve frames");
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return comet_internal::failure(ErrorCode::AllocationFailed,
                                       "the comet registration result could not allocate its storage",
                                       kExecuteOperation);
    }
}

Result<RegistrationWarpPlan> planCometRegistrationWarp(const ImageDescriptor& source,
                                                       const CometRegistrationFrameResult& frame,
                                                       const RegistrationWarpRequest& request)
{
    if (source.extent().width() != frame.width || source.extent().height() != frame.height)
    {
        return comet_internal::failure(ErrorCode::IncompatibleArguments,
                                       "the source descriptor matches the resolved comet frame", kWarpOperation);
    }
    if (!comet_internal::finitePixel(frame.postTranslation) || !comet_internal::finitePixel(request.canvasOrigin))
    {
        return comet_internal::failure(ErrorCode::InvalidArgument, "the comet and canvas translations are finite",
                                       kWarpOperation);
    }
    RegistrationWarpRequest combined = request;
    combined.canvasOrigin.x += frame.postTranslation.x;
    combined.canvasOrigin.y += frame.postTranslation.y;
    if (!comet_internal::finitePixel(combined.canvasOrigin))
    {
        return comet_internal::failure(ErrorCode::OutOfRange, "the combined comet and canvas translation is finite",
                                       kWarpOperation);
    }
    return planRegistrationWarp(source, frame.sourceToReference, frame.sourceDistortion, combined);
}

} // namespace an

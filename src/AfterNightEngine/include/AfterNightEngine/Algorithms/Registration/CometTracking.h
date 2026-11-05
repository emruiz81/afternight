// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Explicit time, coordinate and transform planning for comet registration.
///
/// A comet alignment has two independent transformations. The base registration maps the stars of
/// one frame into the reference frame; the moving-object translation then puts that frame's comet at
/// the reference exposure's comet position. The latter is carried as `postTranslation`, because
/// folding it into the matrix would change where a polynomial registration correction is evaluated.
/// The existing registration warp consumes exactly that pair and remains the sole image resampler.
///
/// Every position in this header is an engine pixel coordinate - the first pixel's centre is
/// (0.5, 0.5) - in one of three planes: the distorted source frame, the distortion-corrected source
/// frame, or the common reference frame. A frame's sample-centre rectangle is
/// `[0.5, width - 0.5] x [0.5, height - 0.5]`. Times are exposure midpoints held as integer UTC
/// nanoseconds.

#pragma once

#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

/// Computes the midpoint of one exposure without discarding sub-second time.
///
/// Half the duration is rounded to the nearest nanosecond, with halfway values away from zero as
/// `std::round` specifies. The result remains a signed Unix-epoch count, so archival dates and long
/// absolute epochs need no floating-point representation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param exposureStart when the shutter opened.
/// @param exposureSeconds non-negative finite exposure duration, in seconds.
/// @return the midpoint; `ErrorCode::InvalidArgument` for a negative or non-finite duration, or
///         `ErrorCode::OutOfRange` when the duration or sum does not fit signed nanoseconds.
[[nodiscard]] AFTERNIGHTENGINE_API Result<UtcInstant> computeCometObservationMidpoint(UtcInstant exposureStart,
                                                                                      double exposureSeconds);

/// A comet-specific view of one SIP inverse calculation.
struct CometSipInverseDiagnostic
{
    /// Corrected sensor position, in engine pixel coordinates.
    PixelCoordinate correctedPosition{};

    /// Which inverse route the distortion model supplied.
    SipInverseRoute route = SipInverseRoute::Identity;

    /// Newton steps used; zero for identity and inverse-coefficient routes.
    std::uint32_t iterations = 0u;

    /// Distance left after the corrected point is mapped forward again, in pixels.
    double residualPixels = 0.0;
};

/// Diagnoses how one comet coordinate is corrected through a SIP model.
///
/// This is the one-position counterpart of the registration map's route counters. It exists so a
/// caller refining an aperture can report whether an inverse header coefficient or a Newton solve
/// was used, without reimplementing the round trip.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param distortion the frame's checked SIP model.
/// @param undistortedPosition the position in the distortion-corrected source plane.
/// @return the corrected sensor position and route; any failure `applySipInverse` returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometSipInverseDiagnostic>
diagnoseCometSipInverse(const SipDistortion& distortion, const PixelCoordinate& undistortedPosition);

/// The most boundary samples one comet aperture projection accepts.
inline constexpr std::uint32_t kMaximumCometApertureBoundarySamples = 4096u;

/// What an aperture projection is asked to map.
struct CometApertureProjectionRequest
{
    /// Circular aperture centre in distorted source-frame engine pixel coordinates.
    PixelCoordinate sourceCenter{};

    /// Aperture radius in source pixels, finite and positive.
    double sourceRadiusPixels = 0.0;

    /// Source columns, at least one.
    std::uint32_t sourceWidth = 0u;

    /// Source rows, at least one.
    std::uint32_t sourceHeight = 0u;

    /// Output columns, at least one.
    std::uint32_t outputWidth = 0u;

    /// Output rows, at least one.
    std::uint32_t outputHeight = 0u;

    /// Equally spaced points around the circle, from eight to
    /// `kMaximumCometApertureBoundarySamples`.
    std::uint32_t boundarySamples = 32u;

    /// The frame's distortion-corrected-source to reference-frame map.
    RegistrationTransform sourceToReference{};

    /// The frame's distorted-source to distortion-corrected-source map.
    SipDistortion sourceDistortion{};

    /// Translation applied after registration, normally the comet alignment and canvas origins.
    PixelCoordinate postTranslation{};
};

/// The two polygons produced from one source aperture.
struct CometApertureProjection
{
    /// Unchanged aperture centre in the distorted source plane.
    PixelCoordinate sourceCenter{};

    /// Unclipped aperture centre after SIP, base registration and post-translation.
    PixelCoordinate outputCenter{};

    /// Boundary in the source frame after clipping to its sample-centre rectangle.
    std::vector<PixelCoordinate> sourcePolygon{};

    /// Corresponding boundary after distortion, registration and output clipping.
    std::vector<PixelCoordinate> outputPolygon{};

    /// Whether at least one source boundary position was clipped.
    bool sourceClipped = false;

    /// Whether at least one mapped output position was clipped.
    bool outputClipped = false;
};

/// Projects a circular source aperture onto a comet-aligned output plane.
///
/// The centre and each boundary point are corrected by SIP, mapped through the base registration,
/// and translated last. Boundary points are clipped to the source and output sample-centre
/// rectangles; `outputCenter` remains unclipped so the caller can see where it landed. The two
/// returned polygons retain one-to-one point order. Clipping is reported rather than repaired
/// invisibly.
///
/// The operation checks cancellation once per boundary point and allocates only the returned
/// polygons. It is reentrant.
///
/// @param execution the operation policy and engine-context lease.
/// @param request geometry, transforms and boundary resolution.
/// @return both polygons; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for a non-finite value, zero extent or boundary count
///         outside its range; `ErrorCode::OutOfRange` when the centre is outside the source; or a
///         mapping, allocation, cancellation or deadline failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometApertureProjection>
projectCometAperture(const ExecutionContext& execution, const CometApertureProjectionRequest& request);

/// Polynomial degree used to describe a comet's motion over time.
enum class CometTrackModel : std::uint8_t
{
    /// No fitted track. A successful fit never returns this value.
    None = 0u,

    /// Constant velocity on both axes.
    Linear,

    /// Constant acceleration on both axes.
    ///
    /// Deliberately the last enumerator, so a membership check needs no table.
    Quadratic
};

/// Whether a value names a comet-track model.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the value to inspect.
/// @return true for every declared enumerator.
[[nodiscard]] constexpr bool isCometTrackModel(CometTrackModel model) noexcept
{
    return static_cast<std::uint8_t>(model) <= static_cast<std::uint8_t>(CometTrackModel::Quadratic);
}

/// The track model's own name.
///
/// Thread-safe and allocation-free.
///
/// @param model the model to name.
/// @return a null-terminated literal compiled into this library; `"UnknownCometTrackModel"` for an
///         out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cometTrackModelName(CometTrackModel model) noexcept;

/// How a track fit chooses between constant velocity and acceleration.
enum class CometTrackModelPolicy : std::uint8_t
{
    /// Use a quadratic only when enough observations exist and it clears the improvement threshold.
    Automatic = 0u,

    /// Always fit constant velocity.
    ForceLinear,

    /// Select a usable quadratic whenever enough observations exist, even when a linear model fits
    /// as well.
    ///
    /// Deliberately the last enumerator, so a membership check needs no table.
    PreferQuadratic
};

/// Whether a value names a model-selection policy.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to inspect.
/// @return true for every declared enumerator.
[[nodiscard]] constexpr bool isCometTrackModelPolicy(CometTrackModelPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(CometTrackModelPolicy::PreferQuadratic);
}

/// The model policy's own name.
///
/// Thread-safe and allocation-free.
///
/// @param policy the policy to name.
/// @return a null-terminated literal compiled into this library;
///         `"UnknownCometTrackModelPolicy"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cometTrackModelPolicyName(CometTrackModelPolicy policy) noexcept;

/// One measured comet position offered to a track fit.
struct CometTrackObservation
{
    /// Stable caller identity, unique within the fit.
    std::uint64_t frameId = 0u;

    /// Exposure midpoint in UTC.
    UtcInstant midpoint{0};

    /// Position in the common reference-frame pixel plane.
    PixelCoordinate position{};

    /// Whether the caller accepts this measurement into the fit.
    bool accepted = true;

    /// Whether excluding this measurement is a malformed required input rather than an optional
    /// omission.
    bool required = true;
};

/// The largest observation set one comet track accepts.
inline constexpr std::uint32_t kMaximumCometTrackObservations = 4096u;

/// What a comet track fit is asked to do.
struct CometTrackRequest
{
    /// How the fit chooses its polynomial degree.
    CometTrackModelPolicy modelPolicy = CometTrackModelPolicy::Automatic;

    /// Time at which the returned constant coefficients are evaluated. Absence selects the
    /// midpoint of the earliest accepted observation, fixed before any outlier is rejected, so it
    /// may be an observation the fit later leaves out. Naming an origin preserves callers that need
    /// coefficients expressed at an existing reference exposure; it does not change the fitted
    /// trajectory.
    std::optional<UtcInstant> coefficientOrigin{};

    /// Multiple of a root-mean-square residual above which a point is a rejection candidate: the
    /// RMS of the fit that leaves that point out, measured against the point's residual under the
    /// same fit. A candidate is removed only when leaving it out also at least halves the current
    /// fit's RMS. Finite and above zero.
    /// Rejection uses the highest initially eligible usable degree and retains enough points to
    /// determine it. Final model selection then applies the requested observation-count threshold.
    double outlierSigma = 4.0;

    /// Pixel floor below which an observation is never rejected. Finite and not negative.
    double minimumOutlierResidualPixels = 3.0;

    /// Fewest retained observations eligible for a quadratic, from three to the observation ceiling.
    std::uint32_t minimumQuadraticObservations = 6u;

    /// Fractional root-mean-square improvement a quadratic must deliver, in [0, 1].
    double minimumQuadraticImprovement = 0.3;

    /// Residual above which diagnostics mark the fit high, in pixels. Finite and not negative;
    /// zero disables this warning.
    double highResidualWarningPixels = 5.0;

    /// Motion below which diagnostics mark the fitted time span small, in pixels. Finite and not
    /// negative; zero disables this warning.
    double minimumMotionWarningPixels = 1.0;
};

/// Structured, non-fatal observations about one fitted track.
struct CometTrackDiagnostics
{
    /// Whether only the minimum number of points survived, leaving no redundancy.
    bool noRedundancy = false;

    /// Whether the selected answer is quadratic.
    bool quadraticSelected = false;

    /// Whether its root-mean-square residual crossed the requested warning threshold.
    bool highResidual = false;

    /// Whether its end-to-end motion crossed below the requested warning threshold.
    bool motionBelowThreshold = false;
};

/// A fitted two-axis comet trajectory.
struct CometTrackResult
{
    /// Selected polynomial degree.
    CometTrackModel model = CometTrackModel::None;

    /// Time origin subtracted before coefficients are evaluated.
    UtcInstant origin{0};

    /// Horizontal coefficients in pixels, pixels per second and pixels per second squared.
    std::array<double, 3u> x{};

    /// Vertical coefficients in pixels, pixels per second and pixels per second squared.
    std::array<double, 3u> y{};

    /// Accepted observations retained by the final fit.
    std::uint32_t observationsUsed = 0u;

    /// Accepted observations removed by robust leave-one-out trimming.
    std::uint32_t outliersRejected = 0u;

    /// Root-mean-square two-axis residual, in pixels.
    double rootMeanSquareResidualPixels = 0.0;

    /// Largest two-axis residual, in pixels.
    double maximumResidualPixels = 0.0;

    /// Greatest separation between two retained observations, in pixels.
    double motionPixels = 0.0;

    /// Largest number of engine-context bytes held while fitting.
    std::uint64_t peakWorkingBytes = 0u;

    /// Non-fatal quality statements derived from the request.
    CometTrackDiagnostics diagnostics{};
};

/// Fits a linear or quadratic comet trajectory in one reference pixel plane.
///
/// Times remain integer nanoseconds until each is differenced from the requested coefficient origin
/// or, when none is supplied, the earliest retained exposure; only then do they become seconds.
/// Linear fitting needs two points, quadratic fitting needs three. Robust trimming removes one point
/// at a time only when the leave-one-out fit halves the current root-mean-square error, so a short
/// noisy sequence is not stripped merely for having no redundancy. A required observation the
/// caller did not accept fails the fit rather than disappearing; trimming itself does not consult
/// `required`, so an accepted required observation can still be removed as an outlier.
///
/// The operation is deterministic, reentrant, checks cancellation between fits, and charges its
/// working vectors to the execution's engine context.
///
/// @param execution the operation policy and engine-context lease.
/// @param observations positions in a common reference plane.
/// @param request coefficient origin, fitting, rejection, model-selection and warning policy.
/// @return the fitted track; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for invalid policy, thresholds, duplicate identities,
///         repeated times or non-finite positions; `ErrorCode::InvalidData` for a required
///         observation that is not accepted, or accepted times that do not determine a linear
///         track; `ErrorCode::EmptySelection` for fewer than two accepted observations;
///         `ErrorCode::LimitExceeded` above the observation ceiling; or a memory, cancellation or
///         deadline failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometTrackResult>
fitCometTrack(const ExecutionContext& execution, std::span<const CometTrackObservation> observations,
              const CometTrackRequest& request = {});

/// Evaluates a fitted comet trajectory at one exposure midpoint.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param track a successful linear or quadratic fit.
/// @param midpoint the time to evaluate; extrapolation is allowed.
/// @return the reference-plane position; `ErrorCode::InvalidArgument` for an unknown or `None`
///         model, or `ErrorCode::OutOfRange` when the time difference or result is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate> evaluateCometTrack(const CometTrackResult& track,
                                                                              UtcInstant midpoint);

/// Which pixel plane one supplied observation occupies.
enum class CometCoordinateSpace : std::uint8_t
{
    /// Distorted source pixels as recorded by the sensor; SIP then base registration are applied.
    SourcePixels = 0u,

    /// Source pixels after SIP correction; only base registration is applied.
    DistortionCorrectedSourcePixels,

    /// The common reference pixel plane; no transform is applied.
    ///
    /// Deliberately the last enumerator, so a membership check needs no table.
    ReferencePixels
};

/// Whether a value names an observation coordinate space.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param space the value to inspect.
/// @return true for every declared enumerator.
[[nodiscard]] constexpr bool isCometCoordinateSpace(CometCoordinateSpace space) noexcept
{
    return static_cast<std::uint8_t>(space) <= static_cast<std::uint8_t>(CometCoordinateSpace::ReferencePixels);
}

/// The coordinate space's own name.
///
/// Thread-safe and allocation-free.
///
/// @param space the space to name.
/// @return a null-terminated literal compiled into this library; `"UnknownCometCoordinateSpace"`
///         for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cometCoordinateSpaceName(CometCoordinateSpace space) noexcept;

/// One supplied position and the plane that gives it meaning.
struct CometRegistrationObservation
{
    /// Measured position, in `space`.
    PixelCoordinate position{};

    /// The coordinate system in which `position` was measured.
    CometCoordinateSpace space = CometCoordinateSpace::SourcePixels;

    /// Whether the caller accepts the measurement. A rejected optional measurement may be replaced
    /// by track prediction.
    bool accepted = true;
};

/// One sequence frame offered to comet registration planning.
struct CometRegistrationFrame
{
    /// Stable identity, unique within the plan.
    std::uint64_t frameId = 0u;

    /// Exposure midpoint. Absence is permitted only for an optional frame that planning skips.
    std::optional<UtcInstant> midpoint{};

    /// Source columns, at least one for a retained frame.
    std::uint32_t width = 0u;

    /// Source rows, at least one for a retained frame.
    std::uint32_t height = 0u;

    /// Distortion-corrected-source to reference-frame registration.
    RegistrationTransform sourceToReference{};

    /// Distorted-source to distortion-corrected-source mapping.
    SipDistortion sourceDistortion{};

    /// Measured comet position, or absence when this frame will use prediction.
    std::optional<CometRegistrationObservation> observation{};

    /// Whether the frame must appear in the executable plan.
    bool required = true;
};

/// Choices that turn frame records into one executable registration plan.
struct CometRegistrationRequest
{
    /// Fitting and model-selection policy.
    CometTrackRequest track{};

    /// Whether each frame's base star registration participates. False uses identity for every
    /// frame: the comet-only route.
    bool retainBaseRegistration = false;

    /// Whether a retained frame without an accepted observation may use the fitted track.
    bool allowPredictedPositions = true;
};

/// A validated frame as retained by the plan.
struct CometRegistrationFramePlan
{
    /// Stable caller identity.
    std::uint64_t frameId = 0u;

    /// Exposure midpoint.
    UtcInstant midpoint{0};

    /// Source columns.
    std::uint32_t width = 0u;

    /// Source rows.
    std::uint32_t height = 0u;

    /// Checked base transform selected by the request.
    RegistrationTransform sourceToReference{};

    /// Checked source distortion.
    SipDistortion sourceDistortion{};

    /// Accepted observation mapped into the reference plane, or absence for prediction.
    std::optional<PixelCoordinate> referenceObservation{};

    /// Whether absence from the execution result is a failure.
    bool required = true;
};

/// An explicit, reusable comet registration plan.
///
/// The plan owns the retained frame records and their common-plane observations. Its public members
/// make the decisions inspectable; `executeCometRegistration` rechecks their invariants so a caller
/// modifying an exported value cannot cause unchecked arithmetic.
struct CometRegistrationPlan
{
    /// Whether this value came from `planCometRegistration`.
    bool valid = false;

    /// Identity of the exposure whose comet position becomes the fixed output position.
    std::uint64_t referenceFrameId = 0u;

    /// Validated policy.
    CometRegistrationRequest request{};

    /// Retained frames, in caller order.
    std::vector<CometRegistrationFramePlan> frames{};

    /// Accepted measured positions, in caller order and already in the reference plane.
    std::vector<CometTrackObservation> trackObservations{};
};

/// Plans the coordinate conversions and track inputs of a comet-aligned sequence.
///
/// Source observations are SIP-corrected and base-registered once; corrected-source observations
/// are base-registered once; reference observations are retained. A source or corrected-source
/// observation must lie in its frame's sample-centre rectangle. When the track request omits a
/// coefficient origin, planning selects the reference exposure midpoint. Planning never fits a
/// trajectory or reads a pixel.
///
/// Thread-safe and reentrant. The returned plan owns its records.
///
/// @param frames sequence records in the order outputs should retain.
/// @param referenceFrameId identity whose time fixes the output comet position.
/// @param request base-transform and prediction policy plus track fitting parameters.
/// @return the plan; `ErrorCode::InvalidArgument` for an empty set, duplicate identity, unknown
///         coordinate space, zero extent, non-finite observation, absent reference identity, or
///         invalid track request; `ErrorCode::OutOfRange` for a source observation outside its
///         frame; `ErrorCode::InvalidData` for a required frame without time or, when prediction is
///         disabled, without an accepted observation; `ErrorCode::EmptySelection` for fewer than
///         two accepted observations; `ErrorCode::LimitExceeded` above the track ceiling; or a
///         mapping or allocation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometRegistrationPlan>
planCometRegistration(std::span<const CometRegistrationFrame> frames, std::uint64_t referenceFrameId,
                      const CometRegistrationRequest& request = {});

/// One frame's resolved comet alignment.
struct CometRegistrationFrameResult
{
    /// Stable caller identity.
    std::uint64_t frameId = 0u;

    /// Exposure midpoint used for the trajectory.
    UtcInstant midpoint{0};

    /// Source columns, retained for warp-plan shape validation.
    std::uint32_t width = 0u;

    /// Source rows, retained for warp-plan shape validation.
    std::uint32_t height = 0u;

    /// Base source-to-reference registration, never modified by comet motion.
    RegistrationTransform sourceToReference{};

    /// Source SIP model, carried to the registration warp.
    SipDistortion sourceDistortion{};

    /// Fitted trajectory position in the reference plane before comet alignment.
    PixelCoordinate trackPositionInReference{};

    /// Translation applied after `sourceToReference`, placing the comet at the reference time.
    PixelCoordinate postTranslation{};

    /// Whether this frame supplied an accepted observation that constrained the trajectory.
    bool observationConstrained = false;

    /// Whether this frame had no accepted observation and therefore relies on the fitted trajectory
    /// alone.
    bool predictionOnly = false;
};

/// The fitted trajectory and every retained frame's transform pair.
struct CometRegistrationResult
{
    /// Identity whose object position stays fixed.
    std::uint64_t referenceFrameId = 0u;

    /// Fitted common-plane trajectory.
    CometTrackResult track{};

    /// Track position at the reference exposure midpoint, in reference pixels.
    PixelCoordinate referencePosition{};

    /// Frame results in plan order.
    std::vector<CometRegistrationFrameResult> frames{};

    /// Largest number of engine-context bytes held while fitting and resolving frames.
    std::uint64_t peakWorkingBytes = 0u;
};

/// Fits and evaluates one comet registration plan.
///
/// The reference-time track position is subtracted from no model: every frame instead receives the
/// post-translation `referencePosition - framePosition`. That keeps the base polynomial's evaluation
/// space unchanged. Every frame uses the fitted trajectory, including frames that constrained it;
/// `predictionOnly` distinguishes frames for which no accepted measurement was available.
///
/// The operation is deterministic, reentrant, failure-atomic with respect to the result, and checks
/// cancellation before fitting and once per output frame. Progress is reported once per resolved
/// frame.
///
/// @param execution the operation policy and engine-context lease.
/// @param plan a value produced by `planCometRegistration`.
/// @return fitted track and frame transforms; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for a modified or default plan, including a frame left
///         without an observation while prediction is disabled; `ErrorCode::LimitExceeded` when a
///         modified plan exceeds the track ceiling; `ErrorCode::OutOfRange` when a resolved
///         translation is not finite; or any fit, evaluation, memory, cancellation or deadline
///         failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CometRegistrationResult>
executeCometRegistration(const ExecutionContext& execution, const CometRegistrationPlan& plan);

/// Builds the existing registration warp for one comet-aligned frame.
///
/// `frame.postTranslation` is added to the caller's canvas origin and then passed, with the unchanged
/// base transform and source distortion, to `planRegistrationWarp`. `warpRegisteredImage` therefore
/// remains the only executor and routes a linear frame through the geometry backend or a curved
/// frame through its coordinate map exactly as ordinary registration does.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param source the frame's image description.
/// @param frame one resolved frame from `executeCometRegistration`.
/// @param request destination extent, canvas origin and sampling policy.
/// @return the ordinary registration warp plan; `ErrorCode::IncompatibleArguments` when `source`
///         does not have the resolved frame's extent; `ErrorCode::InvalidArgument` when either
///         translation is not finite; `ErrorCode::OutOfRange` when their sum overflows; or any
///         refusal `planRegistrationWarp` returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationWarpPlan>
planCometRegistrationWarp(const ImageDescriptor& source, const CometRegistrationFrameResult& frame,
                          const RegistrationWarpRequest& request);

} // namespace an

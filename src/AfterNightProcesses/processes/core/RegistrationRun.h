// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a registration computes on the engine, shared by the standalone Image Registration and the integration's
/// registration stage: a frame read as the registration reads it, its stars detected and summarized into the records
/// the registration database keeps, its lateral chromatic aberration corrected, every frame matched to the reference by
/// its stars - or, when the reference holds too few of them, through a frame already registered near it - or by phase
/// correlation, the catalog's figures fitted over a solved frame, and each registered frame warped onto the canvas its
/// alignment mode chooses.
///
/// **A frame's own plate distortion is the record's to state.** A record whose source distortion is an explicit model
/// with forward polynomials has its stars matched where that model corrects them to, and its warp undoes the model; the
/// standalone registration states each frame's own, read from its cards, and the integration's stage states none.
///
/// **One record per frame.** A frame's figures and transform are a `RegisteredFrame`, the record the registration
/// database reads and writes (`RegistrationMetricsPersistenceTypes.h`); its stars are kept in engine pixel coordinates.
/// The detected stars' full fits travel beside it only for the catalog enrichment, which reads their fluxes.
///
/// **The settings are the integration's.** `RegistrationSettings` states the detector, the transform model, the minimum
/// and maximum star pairs, the chromatic aberration and the demosaic, as both workflows persist it; nothing here reads
/// a window.
///
/// **Each step is one admitted operation, or runs inside the caller's.** A read admits its own steps through the view's
/// file helpers; a step given an execution runs inside it, and none admits another while it holds one.
#pragma once

#include "AfterNightProcessesExport.h"

#include "app/DemosaicTextMaps.h"
#include "processes/core/RegistrationMetricsPersistenceTypes.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationFit.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui::processes
{

/// The detector's request a registration's settings state: the box radius, the threshold, the roundness and amplitude
/// windows, the profile, the smallest Moffat exponent, the convergence, the relaxed checks, and at most the maximum
/// star pairs - the registration's star finder, field for field. A value the detector refuses runs at the nearest value
/// it takes: a radius above `kLargestStarSearchRadius` at that radius, and a roundness threshold or a Moffat exponent
/// that is not positive at `kSmallestStarRoundness` or `kSmallestMoffatBeta`; `registrationStarDetectionAdjustment`
/// says which.
///
/// @param settings the settings.
/// @return the request, for samples in fractions of full scale.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarDetectionRequest
registrationStarDetection(const RegistrationSettings& settings) noexcept;

/// What `registrationStarDetection` runs differently from a registration's settings: one sentence naming each value
/// the detector refuses and the value run instead - the Moffat exponent only under the Moffat profile, which alone
/// reads it.
///
/// @param settings the settings.
/// @return the sentence; empty when the request holds every setting as it is.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString registrationStarDetectionAdjustment(const RegistrationSettings& settings);

/// How a registration demosaics a mosaic frame: the method and the filter arrangement its settings name - "Auto", the
/// default and a text the table does not hold read as the one demosaic setting's method, and an "Auto" arrangement is
/// the one the frame states.
struct RegistrationDemosaic
{
    an::DemosaicMethod method = an::DemosaicMethod::Rcd;
    an::ui::BayerPatternChoice pattern;
};

/// The demosaic a registration's settings name.
///
/// @param settings the settings.
/// @return the method and the arrangement.
[[nodiscard]] AFTERNIGHTPROCESSES_API RegistrationDemosaic registrationDemosaicOf(const RegistrationSettings& settings);

/// How frames are matched: the star model and the transform fit a registration's settings name.
struct RegistrationModel
{
    /// Whether the frames are matched by phase correlation - a translation - rather than by their stars.
    bool phaseCorrelation = false;

    /// The model the star matcher fits.
    an::StarTransformModel starModel = an::StarTransformModel::Projective;

    /// The fit over the matched stars: linear, a stated polynomial order, or the order the matches support.
    an::RegistrationFitRequest fit;

    /// The fewest matched pairs a frame needs to be registered.
    std::uint32_t minimumMatches = 10u;
};

/// The model a registration's settings name: Similarity, Affine or Homography as the linear model; "polynomial" with
/// an order from two to four as that order over a homography; "auto" as the order the matches support.
///
/// @param settings the settings.
/// @return the model; `ErrorCode::InvalidArgument` for a method or transform no registration knows.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<RegistrationModel>
registrationModelOf(const RegistrationSettings& settings);

/// The fit a manual registration's control points take: the transformation's linear model, then the polynomial the
/// mode states as `registrationModelOf` reads it, normalized about the frame's centre.
///
/// @param settings the settings.
/// @param width the frame's width in pixels.
/// @param height the frame's height in pixels.
/// @return the fit request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::RegistrationFitRequest
manualRegistrationFit(const RegistrationSettings& settings, std::uint32_t width, std::uint32_t height);

/// Reads a frame as the registration reads it: decoded, widened to Float32 fractions of full scale, and a mosaic
/// demosaiced by the stated method.
///
/// @param session the process's session.
/// @param path the frame.
/// @param demosaic how a mosaic is demosaiced.
/// @param stop stops the read.
/// @return the frame, an image of the session's engine; or what a step refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::Image<float>>
readRegistrationFrame(const an::consumer::EngineSession& session, const QString& path,
                      const RegistrationDemosaic& demosaic, std::stop_token stop);

/// A frame's detection: its record and the stars' full fits.
struct FrameDetection
{
    /// The record: stars, figures and noise; no transform yet.
    RegisteredFrame frame;

    /// The stars as the detector fitted them, in engine pixel coordinates.
    std::vector<an::DetectedStar> stars;
};

/// Detects a frame's stars: in the green channel of a colour frame and in a grey frame's only one; the frame's figures
/// are the detector's summary - star count, median full width, median signal to noise, eccentricity as one less the
/// median roundness, quality - and the channel's noise figures.
///
/// @param execution the caller's admitted execution.
/// @param frame the frame, as `readRegistrationFrame` read it.
/// @param request the detector's request.
/// @return the detection; or what planning or detecting refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<FrameDetection>
detectRegistrationStars(const an::ExecutionContext& execution, const an::Image<float>& frame,
                        const an::StarDetectionRequest& request);

/// The statistics of the channel a frame's stars are searched in, as the detection above measures them - the median,
/// the noise, the median absolute deviation and the robust scale the registration records beside the stars - taken on
/// their own, so they can be taken on another thread than the search.
///
/// @param execution the caller's admitted execution.
/// @param frame the frame, as `readRegistrationFrame` read it.
/// @param request the detector's request, which states the searched channel's region.
/// @return the statistics; or what planning or measuring refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::StarChannelStatistics>
measureRegistrationChannel(const an::ExecutionContext& execution, const an::Image<float>& frame,
                           const an::StarDetectionRequest& request);

/// Detects a frame's stars as the overload above does, with the channel's statistics `measureRegistrationChannel`
/// took of the same frame: the search reads its sky from them and the record its noise figures, so the detection is the
/// one the overload above gives.
///
/// @param execution the caller's admitted execution.
/// @param frame the frame the statistics were taken of.
/// @param request the detector's request the statistics were taken under.
/// @param measured what `measureRegistrationChannel` returned for the frame.
/// @return the detection; or what planning or detecting refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<FrameDetection>
detectRegistrationStars(const an::ExecutionContext& execution, const an::Image<float>& frame,
                        const an::StarDetectionRequest& request, const an::StarChannelStatistics& measured);

/// What correcting a frame's lateral chromatic aberration did, channel by channel.
struct ChromaticCorrection
{
    bool redApplied = false;
    bool blueApplied = false;
    std::uint32_t redMatches = 0u;
    std::uint32_t blueMatches = 0u;
    double redResidual = 0.0;
    double blueResidual = 0.0;

    /// Whether either channel was moved.
    [[nodiscard]] bool corrected() const noexcept
    {
        return redApplied || blueApplied;
    }
};

/// Corrects a colour frame's lateral chromatic aberration: stars detected in each channel, those nearer the centre than
/// the stated fraction of the half-diagonal left out, the red and blue stars matched to the green ones with the stated
/// model, and each channel whose match holds - at least the stated number of stars, under `checkRegistrationStarMatch`
/// with `kChromaticAberrationSmallMatchInlierFraction` - warped onto the green one with the stated interpolation. A
/// grey frame, or a channel whose match does not hold, is left as it was.
///
/// @param engine the engine context the execution runs in, which owns the corrected frame.
/// @param execution the caller's admitted execution.
/// @param frame the frame, replaced by the corrected one when a channel moves.
/// @param settings the correction's settings.
/// @param detection the detector's request.
/// @return what was done; or what detecting, matching or warping refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<ChromaticCorrection>
correctChromaticAberration(const an::EngineContext& engine, const an::ExecutionContext& execution,
                           an::Image<float>& frame, const RegistrationSettings::ChromaticAberrationSettings& settings,
                           const an::StarDetectionRequest& detection);

/// The fewest inlier pairs a star match is accepted on, whatever pairs its caller asks for.
inline constexpr std::uint32_t kReliableMinimumStarPairs = 5u;

/// The share of its candidate pairs a small star match between two frames must keep as inliers - a frame matched to the
/// reference or to a frame registered near it.
inline constexpr double kRegistrationSmallMatchInlierFraction = 0.8;

/// The same share for a channel's stars matched to the green ones in the chromatic-aberration correction.
inline constexpr double kChromaticAberrationSmallMatchInlierFraction = 0.6;

/// Holds a star match the engine made to the rule every one of the registration's matches is held to. The
/// engine is asked for the pairs and for no share of its candidate pairs, since it would hold a match of every size to
/// a half of them: the match keeps at least the stated pairs, and never fewer than five; and a match of fewer than ten
/// candidate pairs between fields of fewer than a thousand stars each keeps at least the stated share of them as
/// inliers, which a few spurious pairs could otherwise pass for. A larger match, or one against a field of a thousand
/// stars or more, is accepted on its pairs alone, so a frame that shares a small part of its stars with the other - a
/// mosaic panel, a light that drifted - is matched by the stars it shares.
///
/// @param quality the match's figures, as the engine reported them.
/// @param minimumPairs the fewest inlier pairs the caller asks for.
/// @param smallMatchInlierFraction the share of a small match's candidate pairs it must keep as inliers.
/// @return success; or `ErrorCode::EmptySelection`, saying what the match falls short of.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status checkRegistrationStarMatch(const an::StarAlignmentQuality& quality,
                                                                            std::uint32_t minimumPairs,
                                                                            double smallMatchInlierFraction);

/// The most frames already registered that a frame the reference does not match is tried against, nearest first.
inline constexpr std::size_t kMaximumRegistrationAnchorAttempts = 24u;

/// The most prepared anchors the chained pass keeps at once: one frame's whole window of attempts. A prepared anchor is
/// the caller's storage, outside the engine context's budget, so the pass prepares an anchor again rather than keep
/// more; a preparation is deterministic, so that changes no answer.
inline constexpr std::size_t kMaximumPreparedRegistrationAnchors = kMaximumRegistrationAnchorAttempts;

/// The plate distortion a frame's world-coordinate solution carries, as the registration reads it from the frame's own
/// cards: the solution's polynomial, when it has the forward polynomials the frame's stars are corrected by. A solution
/// with inverse polynomials alone carries none here: the stars would be matched where they were detected, and the warp
/// must then undo nothing - undoing the inverse polynomials in the warp alone would apply them under a transform that
/// had not been solved over them.
///
/// @param solution the solution the frame's cards describe.
/// @return the distortion; nothing for a solution without forward polynomials.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::SipDistortion>
registrationDistortionOf(const an::WcsSolution& solution);

/// Which samples of one channel of a Float32 frame the stars and a comet's centre are measured over: every one when the
/// channel holds only finite samples, so that a clean frame keeps the detector's and the refiner's all-valid paths, and
/// every finite one when it holds one that is not - a calibration writes a sample that is not a number where its flat
/// is zero, which an all-valid frame would have the engine refuse. One pass over the channel decides it.
///
/// @param frame the frame.
/// @param channel the channel measured.
/// @return the validity a star frame view of the frame states.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::KernelValidityView registrationChannelValidity(const an::Image<float>& frame,
                                                                                         std::uint32_t channel);

/// Matches every frame of a set to the reference by its stars: the reference's stars prepared once, each frame's
/// matched to them with the model's star model and held to `checkRegistrationStarMatch` - at least the model's minimum
/// pairs, whatever share of the frame's stars they are, and a small match `kRegistrationSmallMatchInlierFraction` of
/// its candidates - a frame the rule refuses failed, and a polynomial fitted over the matched pairs when the model asks
/// for one. A stated order the pairs cannot carry - fewer pairs than the order has terms, or a fit the order refuses -
/// leaves the frame the linear transform its stars matched, as a linear model registers it, and its record's
/// `registrationNote` says so. A frame's stars, and the reference's, are matched where the explicit source distortion
/// its record states corrects them to, a star it cannot correct left out, and as they were detected under any other
/// record.
///
/// A frame the reference does not match is then tried through the frames already registered nearest it in the set's
/// order - the reference aside, those at distance one before those at two, the earlier of two at one distance first, at
/// most `kMaximumRegistrationAnchorAttempts` of them: its stars matched to that frame's under the same rule, the
/// matched stars of that frame carried onto the reference through its transform, and the frame's transform fitted with
/// the model's fit to the pairs that gives - at least the minimum, and as many as the linear kind needs; a stated order
/// those pairs cannot carry gives way to the linear fit over them, as above. The frames nearest a registered frame are
/// tried first; a frame registered so is one the rest are tried through, and the frames still unregistered are tried
/// again while a pass registers any - each against the anchors it was not refused by already, a refused pair still
/// counting among its attempts, since a refusal is deterministic. At most `kMaximumPreparedRegistrationAnchors` anchors
/// are kept prepared at once. A frame holding fewer stars than the minimum is neither tried nor tried through.
///
/// A frame's transform maps it onto the reference; its residual in pixels is recorded as its astrometric residual.
///
/// @param execution the caller's admitted execution.
/// @param set the frames, each with its stars; their transforms and failures are written.
/// @param referencePath the reference frame, one of the set's.
/// @param model how the frames are matched.
/// @param width the frames' width, which the polynomial's normalization reads.
/// @param height the frames' height.
/// @param progress told how many frames are done of how many, on the calling thread, as each is matched to the
///        reference; may be empty.
/// @return success; `ErrorCode::InvalidArgument` when the reference is not in the set or holds too few stars; or a
///         cancellation.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
matchRegistrationStars(const an::ExecutionContext& execution, RegisteredFrameSet& set, const QString& referencePath,
                       const RegistrationModel& model, std::uint32_t width, std::uint32_t height,
                       const std::function<void(std::size_t done, std::size_t total)>& progress);

/// The translation phase correlation measures from a reference frame to another: each frame's green or only channel.
///
/// @param execution the caller's admitted execution.
/// @param reference the reference frame.
/// @param frame the frame.
/// @return the transform that maps the frame onto the reference; or what the correlation refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::RegistrationTransform>
phaseCorrelationTransform(const an::ExecutionContext& execution, const an::Image<float>& reference,
                          const an::Image<float>& frame);

/// One catalog entry a registered frame's star matched.
struct RegisteredCatalogMatch
{
    /// The entry's identifier, as the catalog names it.
    std::string identifier;

    /// How far the star lies from it on the sky, in arcseconds.
    double separationArcseconds = 0.0;
};

/// Fits the catalog's figures over a solved frame's stars: each star placed on the sky through the solution and matched
/// against the installed Gaia catalog, the matches' fluxes fitting the zero point. The figures land in the frame's
/// metrics - zero point, its scatter, the faintest matched magnitude and the matched count - and its astrometric
/// residual is restated in arcseconds through the solution's scale.
///
/// @param execution the caller's admitted execution.
/// @param frame the frame's record.
/// @param stars its stars' full fits.
/// @param solution its solution.
/// @param catalogDirectory the installed Gaia catalog.
/// @param observationEpochYear the epoch the catalog's stars are carried to, or nothing for the catalog's own.
/// @param matches receives the entries the stars matched, one per matched star in the stars' order, when asked.
/// @return whether the catalog matched any star; or a cancellation.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<bool>
enrichRegisteredFrame(const an::ExecutionContext& execution, RegisteredFrame& frame,
                      std::span<const an::DetectedStar> stars, const an::WcsSolution& solution,
                      const QString& catalogDirectory, std::optional<double> observationEpochYear,
                      std::vector<RegisteredCatalogMatch>* matches = nullptr);

/// The canvas an alignment mode names: "mosaic" the union of every frame, "intersection" what every frame covers, and
/// anything else the reference's own.
///
/// @param mode the mode's text.
/// @return the engine's canvas mode.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::RegistrationCanvasMode registrationCanvasModeOf(const QString& mode);

/// The sampling an interpolation text names: "Auto" is Lanczos4.
///
/// @param text the interpolation's text.
/// @return the interpolation; `ErrorCode::InvalidArgument` for a text no dialog offers.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::KernelInterpolation> alignmentInterpolationOf(const QString& text);

/// The file a frame is aligned into: its base name and `_reg`, as FITS, in the output directory.
///
/// @param source the frame.
/// @param directory the output directory.
/// @return the path.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString alignedOutputPath(const QString& source, const QString& directory);

/// The canvas the included frames are aligned onto, each frame's edge corrected by its plate distortion before it is
/// mapped, since its transform was solved between corrected positions.
///
/// @param frames each included frame's transform onto the reference, the reference's the identity.
/// @param referenceIndex the reference's index among them.
/// @param width the frames' width.
/// @param height the frames' height.
/// @param mode the canvas mode.
/// @param distortions each frame's plate distortion in the frames' order, a default one for a frame without; empty
///        when no frame carries one. A model with inverse polynomials alone corrects no edge.
/// @return the canvas; `ErrorCode::IncompatibleArguments` when the distortions are not one per frame; or what bounding
///         or planning refused - an intersection nothing covers among it.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::RegistrationCanvas>
planAlignmentCanvas(std::span<const an::RegistrationTransform> frames, std::uint32_t referenceIndex,
                    std::uint32_t width, std::uint32_t height, an::RegistrationCanvasMode mode,
                    std::span<const an::SipDistortion> distortions = {});

/// Warps one registered frame onto the canvas and writes it whole as Float32 FITS: read as the registration reads it,
/// sampled through its transform with its plate distortion undone, zero and marked invalid where the frame does not
/// reach, the solution and cards the warp invalidates dropped.
///
/// @param session the process's session.
/// @param path the frame.
/// @param transform its transform onto the reference.
/// @param distortion the plate distortion the transform was solved over, a default one when it was solved over none; a
///        model with inverse polynomials alone corrected no star, and is not undone.
/// @param canvas the canvas.
/// @param interpolation the sampling.
/// @param demosaic how a mosaic is demosaiced.
/// @param outputPath where it is written.
/// @param stop stops the frame.
/// @return success, or what a step refused; nothing is left at the output path on a failure.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
alignRegisteredFrame(const an::consumer::EngineSession& session, const QString& path,
                     const an::RegistrationTransform& transform, const an::SipDistortion& distortion,
                     const an::RegistrationCanvas& canvas, an::KernelInterpolation interpolation,
                     const RegistrationDemosaic& demosaic, const QString& outputPath, std::stop_token stop);

} // namespace an::ui::processes

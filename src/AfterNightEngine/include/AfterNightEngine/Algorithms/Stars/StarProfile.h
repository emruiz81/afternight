// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What one detected star is, and what a frame's stars say about the frame.
///
/// These are the records a star detection produces and every consumer of one reads. `StarPsf` is
/// the fitted light distribution, `DetectedStar` places it in the frame beside its uncertainties,
/// magnitude and shape figures, and `StarFieldSummary` reduces a frame's list to the medians a
/// frame ranking reads. None of them carries a catalog identity or a world position; the catalog
/// and astrometry surfaces attach those.
///
/// A position is an engine pixel coordinate: the first pixel's centre is (0.5, 0.5). That is the
/// convention the fit itself uses, and the one star alignment, registration and aperture
/// photometry take without conversion.
///
/// Every figure that can be missing is an absent optional rather than a sentinel, and the two
/// operations here - summarising a list and ordering it - read no pixel.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <span>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most stars one frame's detection or summary accepts.
///
/// A stated limit rather than a silent truncation: a frame that produces more than this many
/// candidates is a frame whose detection parameters are wrong, and the operation says so
/// instead of quietly answering about the first two hundred thousand of them.
inline constexpr std::uint32_t kMaxStarsPerFrame = 200000u;

/// The analytic shape a star's light distribution is fitted with.
///
/// The choice is the caller's and is recorded in every star it produced, because a full width
/// derived from a Moffat radius and one derived from a Gaussian standard deviation are different
/// numbers even when they describe the same pixels.
enum class PsfModel : std::uint8_t
{
    /// An elliptical Gaussian: `B + A exp(-(tx^2 / SX + ty^2 / SY))`.
    ///
    /// Seven free parameters - background, amplitude, both centres, the major-axis scale, the
    /// roundness angle and the position angle.
    Gaussian = 0u,

    /// An elliptical Moffat with the exponent held at the caller's value.
    ///
    /// The same seven free parameters as the Gaussian. Beta is an input rather than a result, so a
    /// frame whose stars are known to share a wing shape is fitted without spending a parameter on
    /// re-deriving it.
    MoffatFixedBeta = 1u,

    /// An elliptical Moffat whose exponent is fitted with the rest.
    ///
    /// Eight free parameters. The extra one is bounded away from the degenerate limits by the same
    /// cosine parameterisation the roundness uses, so the solver cannot walk beta to zero or to
    /// infinity while the residual keeps falling.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    MoffatFreeBeta = 2u
};

/// Whether a value names a model this build fits.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the value to inspect.
/// @return true for one of the three declared models; false for an out-of-range cast.
[[nodiscard]] constexpr bool isPsfModel(PsfModel model) noexcept
{
    return model == PsfModel::Gaussian || model == PsfModel::MoffatFixedBeta || model == PsfModel::MoffatFreeBeta;
}

/// The model's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param model the model to name.
/// @return a null-terminated string literal compiled into this library, which outlives every
///         caller; `"UnknownPsfModel"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* psfModelName(PsfModel model) noexcept;

/// The number that multiplies a Gaussian standard deviation to give its full width at half maximum.
///
/// Two times the square root of two times the natural logarithm of two, to eleven decimal places.
inline constexpr double kGaussianFullWidthPerSigma = 2.35482004503;

/// The fitted light distribution of one star, in pixels and in the frame's own numeric domain.
///
/// Everything here is a fitted parameter or a value derived from one by a closed formula. A
/// position is measured with the centre of the first pixel at one half, which is the convention the
/// fit itself uses, so a star sitting exactly on a pixel centre has a fractional coordinate.
struct StarPsf
{
    /// The local sky level the fit separated from the star, in the frame's numeric domain.
    double background = 0.0;

    /// The star's peak above `background`, in the frame's numeric domain.
    double amplitude = 0.0;

    /// Horizontal centre inside the fitted box, in pixels, with the box's first sample centred at
    /// one half. `DetectedStar::positionX` is this plus the box's first column in the frame.
    double centreX = 0.0;

    /// Vertical centre inside the fitted box, in pixels, under the same convention.
    double centreY = 0.0;

    /// Major-axis scale: a Gaussian standard deviation, or a Moffat core radius, in pixels.
    double scaleX = 0.0;

    /// Minor-axis scale, which the parameterisation holds at or below `scaleX`.
    double scaleY = 0.0;

    /// Major-axis full width at half maximum, in pixels.
    double fullWidthX = 0.0;

    /// Minor-axis full width at half maximum, in pixels.
    double fullWidthY = 0.0;

    /// Major-axis full width in arcseconds, present only when a plate scale was stated.
    std::optional<double> fullWidthXArcseconds{};

    /// Minor-axis full width in arcseconds, present only when a plate scale was stated.
    std::optional<double> fullWidthYArcseconds{};

    /// Position angle of the major axis, in degrees, folded into [-90, 90].
    double angleDegrees = 0.0;

    /// Moffat exponent. Present only for a Moffat model; absent for a Gaussian.
    ///
    /// Absent rather than a sentinel such as minus one, which a caller would have to know about.
    std::optional<double> beta{};

    /// Root mean square of the fit residual over the samples the fit actually used.
    double residualRootMeanSquare = 0.0;

    /// The level at or above which a sample was withheld from the fit, in the numeric domain.
    ///
    /// From a detection, equal to the request's full scale for a star with no saturated plateau
    /// and to the measured plateau level for one with; from a direct `fitStarPsf`, the level the
    /// request stated.
    double saturationLevel = 0.0;

    /// Half the width of the square box the fit read, in pixels.
    std::uint32_t boxRadius = 0u;

    /// Whether the fitted amplitude is above the frame's dynamic range, so the core is clipped.
    ///
    /// Set by `detectStars`, which knows the dynamic range; a direct `fitStarPsf` leaves it false.
    bool saturated = false;

    /// Which shape was fitted.
    PsfModel model = PsfModel::Gaussian;
};

/// One-sigma relative uncertainties of the fitted parameters.
///
/// Each is the square root of the corresponding diagonal entry of the inverse normal-equation
/// matrix divided by the fitted value. They are relative
/// and therefore dimensionless, and a value is absent when the normal equations were singular or
/// the parameter it would divide by is zero.
struct StarPsfUncertainty
{
    /// Relative uncertainty of `StarPsf::background`.
    std::optional<double> background{};

    /// Relative uncertainty of `StarPsf::amplitude`.
    std::optional<double> amplitude{};

    /// Relative uncertainty of `StarPsf::centreX`.
    std::optional<double> centreX{};

    /// Relative uncertainty of `StarPsf::centreY`.
    std::optional<double> centreY{};

    /// Relative uncertainty of the squared scale term the fit moves - `2 sigma^2` or the squared
    /// core radius, as `psfScaleFromFullWidth` describes - not of `StarPsf::scaleX` itself.
    std::optional<double> scaleX{};

    /// Relative uncertainty of the roundness angle, the fitted angle whose cosine gives the axis
    /// ratio.
    std::optional<double> scaleY{};

    /// Relative uncertainty of the fitted position angle, taken in radians.
    std::optional<double> angle{};

    /// Relative uncertainty of the fitted angle whose cosine gives the Moffat exponent, present only
    /// when beta was free.
    std::optional<double> beta{};
};

/// Shape figures derived from one star's fitted widths, for ranking and display.
struct StarMetrics
{
    /// `fullWidthY / fullWidthX`, in (0, 1]. One is a perfectly round star.
    double roundness = 0.0;

    /// A brightness-weighted roundness proxy: `amplitude / (0.1 + |fullWidthX - fullWidthY|)`.
    ///
    /// It is not a shape ratio and does not live in [0, 1]; a caller comparing shape alone wants
    /// `roundness`.
    double circularity = 0.0;

    /// The mean of the two full widths, in pixels. What a frame's seeing figure is the median of.
    double meanFullWidth = 0.0;
};

/// One star this engine found and fitted.
///
/// A detection-time record. It carries what the pixels said and nothing a catalog would add: an
/// identifier, a world position, a calibrated magnitude and a colour index all belong to the
/// catalog and astrometry surfaces, which attach them to a star rather than asking the detector to
/// invent them.
struct DetectedStar
{
    /// The fitted light distribution.
    StarPsf psf{};

    /// How well each fitted parameter is determined.
    StarPsfUncertainty uncertainty{};

    /// Shape figures derived from the fit.
    StarMetrics metrics{};

    /// Horizontal position in the frame, in pixels, with the first pixel's centre at one half.
    double positionX = 0.0;

    /// Vertical position in the frame, in pixels, with the first pixel's centre at one half.
    double positionY = 0.0;

    /// Instrumental magnitude: minus two and a half times the base-ten logarithm of the
    /// background-subtracted sum over the fitted box.
    ///
    /// Absent when that sum is not positive, which is the honest report for a candidate whose box
    /// contains no flux above its own background.
    std::optional<double> instrumentalMagnitude{};

    /// Signal-to-noise of the fitted amplitude against the frame's background noise.
    ///
    /// Absent when the frame's background noise is not positive.
    std::optional<double> signalToNoise{};

    /// Which channel of the source frame this star was measured in.
    std::uint32_t channelIndex = 0u;
};

/// The robust scale estimators of one channel, which frame weighting and normalization consume.
///
/// Separated from detection on purpose, so a caller that wants only a star list pays for no
/// statistics traversal of a channel it did not ask about: the detector reports the channel it
/// detected in, and a caller that wants another channel names it.
struct StarChannelStatistics
{
    /// Median of the channel, in the frame's numeric domain.
    double background = 0.0;

    /// Robust first-difference background-noise estimate.
    double backgroundNoise = 0.0;

    /// Median absolute deviation from the median.
    ///
    /// Absent when only the figures a detection reads were measured. Detection needs the sky, the
    /// noise and the largest sample and nothing else, while this and `robustScale` are most of the
    /// cost of a complete measurement, so it does not pay for them unless it is asked to.
    std::optional<double> medianAbsoluteDeviation{};

    /// Iterative k-sigma scale: the square root of the clipped biweight midvariance times 0.991.
    ///
    /// Absent under the same rule as `medianAbsoluteDeviation`.
    std::optional<double> robustScale{};

    /// Largest sample in the channel, in the frame's numeric domain.
    double maximum = 0.0;

    /// How many samples the validity rule admitted.
    std::uint64_t admittedSamples = 0u;

    /// Which channel these describe.
    std::uint32_t channelIndex = 0u;
};

/// What a frame's accepted stars say about the frame.
///
/// Every figure is a median rather than a mean, so one badly fitted star cannot move it. A field
/// is absent rather than zero when no star qualified for it, because zero is a legitimate value of
/// several of them and a caller ranking frames must be able to tell "no stars" from "bad stars".
struct StarFieldSummary
{
    /// Median of `StarMetrics::meanFullWidth` over stars with a positive width, in pixels.
    std::optional<double> medianFullWidth{};

    /// Median full width in arcseconds, present only when a plate scale was stated.
    std::optional<double> medianFullWidthArcseconds{};

    /// Median roundness over stars whose roundness lies in (0, 1]. A star whose recorded
    /// `StarMetrics::roundness` is outside that interval contributes the ratio of its fitted widths
    /// instead, when that ratio is inside it.
    std::optional<double> medianRoundness{};

    /// Median of amplitude over background noise, over unsaturated stars with positive amplitude.
    std::optional<double> medianSignalToNoise{};

    /// A frame-quality figure: `count * roundness / width` over the unsaturated
    /// stars with positive widths, where `count` is how many there are and `roundness` and
    /// `width` are their own medians - so neither is `medianRoundness` or `medianFullWidth`, which
    /// include saturated stars. A roundness median of one stands in when none of those stars has a
    /// usable roundness. Larger is better.
    ///
    /// Absent when no unsaturated star had a positive width.
    std::optional<double> quality{};

    /// The channel the detector measured, with the statistics it measured there.
    StarChannelStatistics channel{};

    /// How many accepted stars the summary was computed from.
    std::uint32_t starCount = 0u;

    /// How many of those were excluded from the quality figure because their core is clipped.
    std::uint32_t saturatedStarCount = 0u;
};

/// Summarises a list of stars into the frame figures a ranking consumer reads.
///
/// Deterministic and independent of the list's order: every figure is a median taken by value, and
/// a tie between two equal values is a tie. The list is only read.
///
/// Thread-safe with respect to `execution` and `stars`. It performs one context-accounted
/// temporary allocation proportional to the star count and checks cancellation once before it
/// starts.
///
/// @param execution the operation policy and engine-context lease.
/// @param stars the accepted stars to summarise, in any order.
/// @param channel the statistics of the channel they were detected in, which supplies the noise
///        the signal-to-noise figure is measured against.
/// @return the summary; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::LimitExceeded` when the list is longer than `kMaxStarsPerFrame`; or a
///         cancellation or memory-budget failure. An empty list is a success whose figures are all
///         absent, not `ErrorCode::EmptySelection`: a frame with no stars is a frame.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarFieldSummary> summarizeStarField(const ExecutionContext& execution,
                                                                               std::span<const DetectedStar> stars,
                                                                               const StarChannelStatistics& channel);

/// The field one star list is ordered by.
///
/// Only fields a detected star actually carries appear here. Catalog identity, world coordinates
/// and calibrated photometry are ordered by the surfaces that attach them.
enum class StarSortKey : std::uint8_t
{
    /// `DetectedStar::positionX`.
    PositionX = 0u,

    /// `DetectedStar::positionY`.
    PositionY,

    /// `StarPsf::background`.
    Background,

    /// `StarPsf::amplitude`.
    Amplitude,

    /// `StarPsf::centreX`.
    CentreX,

    /// `StarPsf::centreY`.
    CentreY,

    /// `StarPsf::scaleX`.
    ScaleX,

    /// `StarPsf::scaleY`.
    ScaleY,

    /// `StarPsf::fullWidthX`.
    FullWidthX,

    /// `StarPsf::fullWidthY`.
    FullWidthY,

    /// `StarPsf::angleDegrees`.
    Angle,

    /// `StarPsf::residualRootMeanSquare`.
    ResidualRootMeanSquare,

    /// `StarPsf::saturationLevel`.
    SaturationLevel,

    /// `StarPsf::boxRadius`.
    BoxRadius,

    /// `StarPsf::saturated`, with false before true ascending.
    Saturated,

    /// `StarPsf::beta`. A Gaussian star has none and sorts after every Moffat star ascending.
    Beta,

    /// `StarPsf::model`, in enumerator order.
    Model,

    /// `DetectedStar::instrumentalMagnitude`. An absent magnitude sorts after every present one
    /// ascending.
    InstrumentalMagnitude,

    /// `DetectedStar::signalToNoise`, with the same absent-last rule.
    SignalToNoise,

    /// `DetectedStar::channelIndex`.
    Channel,

    /// `StarMetrics::roundness`.
    Roundness,

    /// `StarMetrics::circularity`.
    Circularity,

    /// `StarMetrics::meanFullWidth`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    MeanFullWidth
};

/// Whether a value names a sort key this build orders by.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param key the value to inspect.
/// @return true for a declared key; false for an out-of-range cast.
[[nodiscard]] constexpr bool isStarSortKey(StarSortKey key) noexcept
{
    return static_cast<std::uint8_t>(key) <= static_cast<std::uint8_t>(StarSortKey::MeanFullWidth);
}

/// The key's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param key the key to name.
/// @return a null-terminated string literal compiled into this library; `"UnknownStarSortKey"` for
///         an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starSortKeyName(StarSortKey key) noexcept;

/// Which way a star list is ordered.
enum class StarSortDirection : std::uint8_t
{
    /// Smallest first.
    Ascending = 0u,

    /// Largest first.
    ///
    /// Deliberately the last enumerator.
    Descending = 1u
};

/// Orders a star list by one field, in place and deterministically.
///
/// The order is total: stars that compare equal on the key keep the relative order they had, so
/// two calls on the same list produce the same list and a sort by one key followed by a sort by
/// another is a composed order rather than an arbitrary one. An absent optional is ordered after
/// every present value ascending, and before every present value descending, which is the same
/// rule in both directions rather than two.
///
/// Runs on the calling thread. It is not parallel: the cost of the ordering is not where a star
/// pipeline spends itself.
///
/// @param stars the list to order, which is rearranged in place.
/// @param key which field to order by.
/// @param direction which way.
/// @return success; `ErrorCode::InvalidArgument` for an out-of-range key or direction, in which
///         case the list is untouched.
[[nodiscard]] AFTERNIGHTENGINE_API Status sortStars(std::span<DetectedStar> stars, StarSortKey key,
                                                    StarSortDirection direction = StarSortDirection::Ascending);

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Photometric colour calibration: three channel gains from catalog colours.
///
/// **The colour model is a stated function of one catalog colour.** `predictPhotometricColorRatios`
/// is an empirical relation between a Gaia BP minus RP colour index and the red and blue fluxes
/// relative to green, clamped to the colour range it was fitted over. It is named here
/// rather than buried in the fit so a test can hold it to its own definition; the spectrophotometric
/// route in `Algorithms/Spectrophotometry/SpectrophotometricColorCalibration.h` replaces the model
/// with a measured response and ends in the same apply.
///
/// **The fit is a robust ratio, not a regression.** Each sample's expected-over-observed ratio is a
/// candidate gain; the gain is the median of the candidates inside a robust clipping window around
/// their median. Nothing here depends on the order the samples arrive in.
///
/// **Orchestration composes published operations.** `calibratePhotometricColor` detects with the
/// star domain, matches with the catalog domain, fits here and applies through
/// `Algorithms/Photometry/ColorCalibrationApply.h`. Each stage is separately reachable, which is
/// what lets a host run the fit alone on samples it already holds.
///
/// The sky window the apply measures is not clamped to the unit interval, and the whole calibration
/// inherits that from `Algorithms/Photometry/ColorCalibrationApply.h`.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>
#include <AfterNightEngine/Algorithms/Photometry/PhotometricCalibration.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// One frame, checked once, that the calibration reads. Defined in `Algorithms/Stars/StarDetection.h`.
class StarFrameView;

/// How stars are found in each channel. Defined in `Algorithms/Stars/StarDetection.h`.
///
/// Named rather than included, and passed by reference rather than held by value, so a caller that
/// only fits gains from samples it already has does not inherit the detector's include closure.
struct StarDetectionRequest;

/// The most colour samples one fit accepts.
inline constexpr std::uint32_t kMaxPhotometricColorSamples = 1000000u;

/// The exponent slope of the red-to-green ratio against BP minus RP, per magnitude of colour.
inline constexpr double kPhotometricRedColorSlope = 0.32;

/// The exponent slope of the blue-to-green ratio against BP minus RP, per magnitude of colour.
inline constexpr double kPhotometricBlueColorSlope = -0.40;

/// The bluest colour index the model was fitted over; a bluer star is evaluated here.
inline constexpr double kPhotometricColorIndexFloor = -0.5;

/// The reddest colour index the model was fitted over; a redder star is evaluated here.
inline constexpr double kPhotometricColorIndexCeiling = 3.5;

/// The colour index assumed for a star whose catalog carries none: a solar-like star.
inline constexpr double kPhotometricDefaultColorIndex = 0.0;

/// The smallest flux a colour sample's channel may carry and still form a ratio.
inline constexpr double kMinimumPhotometricSampleFlux = 1.0e-9;

/// The floor on the robust scale of the gain candidates, as a dimensionless ratio.
inline constexpr double kDefaultMinimumColorGainRobustScale = 0.05;

/// Whether a filter name says the frame is narrowband and therefore not photometrically calibrable.
///
/// A bounded ASCII-folded scan, after trimming surrounding white space, for the spellings this
/// engine has met: `Ha` or `SII` as the whole name or its first word, and `Halpha`, `H-alpha`,
/// `OIII`, `S-II`, `narrow`, `dualband` or `triad` anywhere in it. A name that contains none of them
/// is not evidence of a broadband frame - only of a name this heuristic does not recognise - which
/// is why the answer is a boolean a caller reads rather than a refusal the engine imposes.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param filterName the filter name, in any case.
/// @return true when the name names a narrowband filter this heuristic recognises.
[[nodiscard]] AFTERNIGHTENGINE_API bool isNarrowbandFilterName(std::string_view filterName) noexcept;

/// What a star of one colour is expected to measure as, relative to its green flux.
struct PhotometricColorRatios
{
    /// Red flux over green flux.
    double redToGreen = 1.0;

    /// Green flux over green flux, which is one and is here so a channel index reads the same way
    /// for all three.
    double greenToGreen = 1.0;

    /// Blue flux over green flux.
    double blueToGreen = 1.0;

    /// The three ratios indexed by channel, red, green and blue.
    ///
    /// @return the array.
    [[nodiscard]] constexpr std::array<double, 3u> asArray() const noexcept
    {
        return {redToGreen, greenToGreen, blueToGreen};
    }

    /// Whether two predictions are the same three numbers.
    ///
    /// @param left one prediction.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const PhotometricColorRatios& left,
                                                   const PhotometricColorRatios& right) noexcept = default;
};

/// The expected red and blue fluxes relative to green for a star of one Gaia colour.
///
/// `exp(kPhotometricRedColorSlope * colour)` and `exp(kPhotometricBlueColorSlope * colour)` over a
/// colour clamped into `[kPhotometricColorIndexFloor, kPhotometricColorIndexCeiling]`. An absent
/// colour is evaluated at `kPhotometricDefaultColorIndex`; a non-finite one is treated as absent.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param colorIndexBpMinusRp the star's BP minus RP colour index, or absent.
/// @return the predicted ratios.
[[nodiscard]] AFTERNIGHTENGINE_API PhotometricColorRatios
predictPhotometricColorRatios(std::optional<double> colorIndexBpMinusRp) noexcept;

/// One matched star measured in all three channels.
struct PhotometricColorSample
{
    /// Where the star was measured, in engine pixel coordinates.
    PixelCoordinate position{};

    /// The catalog magnitude of the matched entry, in the catalog's band.
    double catalogMagnitude = 0.0;

    /// The matched entry's BP minus RP colour index, or absent when the catalog carries none.
    std::optional<double> colorIndexBpMinusRp{};

    /// Instrumental red flux, in the frame's numeric domain.
    double redFlux = 0.0;

    /// Instrumental green flux.
    double greenFlux = 0.0;

    /// Instrumental blue flux.
    double blueFlux = 0.0;

    /// How far the star was from its matched entry, in arcseconds.
    double separationArcseconds = 0.0;

    /// The matched entry's position in the catalog.
    std::uint32_t catalogEntryIndex = 0u;
};

/// The clipping rule the gain fit re-decides its candidates under, and the smallest sample it accepts.
struct PhotometricColorFitRequest
{
    /// The fewest usable samples a fit needs. At least one.
    std::uint32_t minimumSamples = 10u;

    /// How many robust scales from the median a gain candidate may sit and still be retained.
    /// Finite and positive.
    double clipSigmas = 3.0;

    /// Floor on the robust scale of the candidates, as a dimensionless ratio. Finite and not
    /// negative. The scale is held at or above it before the window is sized.
    double minimumRobustScale = kDefaultMinimumColorGainRobustScale;
};

/// One fitted set of channel gains and how it was reached.
struct PhotometricColorFitResult
{
    /// The per-channel multipliers that bring the observed ratios to the predicted ones. Green is
    /// the reference and its gain is one.
    ColorChannelGains gains{1.0, 1.0, 1.0};

    /// How many samples were supplied.
    std::uint32_t suppliedCount = 0u;

    /// How many supplied samples carried three finite fluxes above the floor and were fitted.
    std::uint32_t usableCount = 0u;

    /// How many usable samples carried a catalog colour index.
    std::uint32_t coloredCount = 0u;

    /// Whether more than half the usable samples carried a colour index, so the fit used the catalog
    /// colour model rather than the neutral default for most of them.
    bool usedCatalogColorModel = false;

    /// Root mean square of the natural-log residual between the corrected observed ratios and the
    /// predicted ones, over both colour ratios of every usable sample.
    double rmsLogResidual = 0.0;

    /// Positions into the supplied span of the samples that were fitted, in input order.
    std::vector<std::uint32_t> usedSampleIndices{};
};

/// Fits red and blue gains relative to green from colour samples.
///
/// A sample is usable when its three fluxes are finite and above `kMinimumPhotometricSampleFlux`.
/// Each usable sample contributes one red candidate, `predictedRedToGreen / observedRedToGreen`,
/// and one blue candidate. A candidate set is reduced to its median and its median absolute
/// deviation; the deviation is scaled to a sigma and held at or above `minimumRobustScale`;
/// candidates within `clipSigmas` sigmas of the median are retained; and the gain is the median of
/// what remains, or the first median when nothing remains. The window is decided once. Fewer usable
/// samples than `minimumSamples` is a returned `ErrorCode::EmptySelection`, carrying both counts.
///
/// Every rank is exact and every sum follows input order, so the answer does not depend on the
/// admitted thread count. The candidates are held in engine-context memory for the call; the
/// returned index list is the only heap allocation.
///
/// @param execution the operation policy and engine-context lease.
/// @param samples the colour samples, at most `kMaxPhotometricColorSamples` of them.
/// @param request the sample floor and the clipping rule.
/// @return the gains; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a request field outside its stated range;
///         `ErrorCode::LimitExceeded` above the sample ceiling; `ErrorCode::EmptySelection` for too
///         few usable samples, carrying the usable count in `ContextKey::Available` and the floor
///         in `ContextKey::Requested`; a memory-budget refusal; `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PhotometricColorFitResult>
fitPhotometricColorGains(const ExecutionContext& execution, std::span<const PhotometricColorSample> samples,
                         const PhotometricColorFitRequest& request = {});

/// A catalog of magnitudes beside the colour index of each of its entries.
///
/// The catalog domain's entry carries one magnitude in one band and no colour, on purpose; the
/// colour index is a provider fact. This pairs the two without adding a Gaia member to every entry:
/// the colours are index-aligned with the catalog's entries and may be empty for a catalog that
/// carries none, in which case every star is evaluated at the default colour.
struct PhotometricReferenceCatalog
{
    /// The catalog to match against. Must not be null and must be a built catalog.
    const SkyCatalog* catalog = nullptr;

    /// One colour index per entry, in entry order, or empty. When present, exactly as long as the
    /// catalog.
    std::span<const std::optional<double>> colorIndexBpMinusRp{};
};

/// The BP minus RP colour index of each Gaia record, in record order.
///
/// The colours that pair with the entries `buildGaiaCatalogEntries` produced from the same records,
/// so a `PhotometricReferenceCatalog` can be built from one Gaia decode. Allocates only the returned
/// list.
///
/// @param records the records the catalog's entries were built from.
/// @return one optional per record, absent where either band is unmeasured.
[[nodiscard]] AFTERNIGHTENGINE_API std::vector<std::optional<double>>
gaiaColorIndices(std::span<const GaiaStarRecord> records);

/// Whether and where the sky is measured for neutralization.
struct PhotometricBackgroundRequest
{
    /// Whether the sky is measured and the offsets derived from it. False writes zero offsets.
    bool neutralize = true;

    /// The rectangle to measure the sky in, or absent for the whole frame.
    std::optional<StatisticsRegion> region{};

    /// Where the neutralized background goes.
    ColorCalibrationBackgroundMode mode = ColorCalibrationBackgroundMode::SharedPedestal;
};

/// Everything one photometric colour calibration is asked to do.
struct PhotometricColorCalibrationRequest
{
    /// The most stars kept per channel, brightest first. One to `kMaxStarsPerFrame`.
    std::uint32_t maximumStarsPerChannel = 2000u;

    /// The cross-match against the reference catalog: a five arcsecond radius and a
    /// fourteenth-magnitude limit by default.
    CatalogMatchRequest match{.matchRadiusArcseconds = 5.0, .magnitudeLimit = 14.0};

    /// How far a red or blue detection may sit from the matched green star and still be the same
    /// star, in pixels. Finite and positive.
    double channelMatchRadiusPixels = 3.0;

    /// Which measurement of each star stands for its flux.
    StarFluxEstimator fluxEstimator = StarFluxEstimator::PsfVolume;

    /// The gain fit's sample floor and clipping rule.
    PhotometricColorFitRequest fit{};

    /// Whether and where the sky is neutralized.
    PhotometricBackgroundRequest background{};

    /// What happens to a calibrated sample that leaves the representable interval.
    ColorCalibrationClipping clipping = ColorCalibrationClipping::ClampToPixelRange;

    /// The frame's filter name, when the caller knows it. A narrowband name refuses the calibration
    /// by name rather than fitting a colour model to a frame it does not describe. Empty says the
    /// filter is unknown, which is not a refusal.
    std::string_view filterName{};
};

/// Everything one photometric colour calibration produced.
struct PhotometricColorCalibrationResult
{
    /// The gain fit.
    PhotometricColorFitResult fit{};

    /// The offsets that were written, zero when nothing was neutralized.
    ColorChannelOffsets offsets{0.0, 0.0, 0.0};

    /// The sky estimate the offsets came from, present when the background was neutralized.
    std::optional<ColorBackgroundEstimate> background{};

    /// What the apply wrote.
    ColorCalibrationApplyResult apply{};

    /// The colour samples the fit was offered: one per green star that matched an entry carrying a
    /// magnitude, yielded a flux, and claimed a red and a blue detection within the channel radius
    /// that yielded fluxes too.
    std::vector<PhotometricColorSample> samples{};

    /// How many stars each channel's detection accepted, red, green and blue.
    std::array<std::uint32_t, 3u> detectedStars{};

    /// How many green stars matched a catalog entry.
    std::uint32_t crossMatchedStars = 0u;

    /// How many matched green stars with a flux had no unclaimed red or blue detection within the
    /// channel radius, or claimed one that yielded no flux.
    std::uint32_t unpairedStars = 0u;
};

/// Builds the metadata transition for a successful photometric calibration.
///
/// Records the recipe version, channel gains and offsets, and sets the colour-calibrated flag.
/// Thread-safe; allocates the lineage detail. The caller applies the transition to its image.
///
/// @param input the source image semantics.
/// @param result the successful calibration whose coefficients are recorded.
/// @return the transition, a semantics/transition validation failure, or
///         `ErrorCode::AllocationFailed` when its lineage detail cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
photometricCalibrationTransition(const ImageSemantics& input, const PhotometricColorCalibrationResult& result);

/// Detects, matches, fits and applies a photometric colour calibration to one RGB frame.
///
/// In order: the request and frame are checked; stars are detected in the red, green and blue
/// channels through `planStarDetection` and `detectStars`; the green stars are placed on the sky and
/// matched against the catalog by `crossMatchDetectedStars`; each matched green star claims the
/// nearest unclaimed red and blue detection within the channel radius, in match order; the samples
/// are fitted by `fitPhotometricColorGains`; the sky is measured by `estimateColorBackground` when
/// asked; and the gains and offsets are written by `applyColorCalibration`. Progress is reported
/// once per stage and cancellation is checked between them and inside each.
///
/// The frame must be three-channel RGB in the `ColorSpace::LinearRgb` basis and a floating
/// representation in a linear numeric domain; `EncodedNonlinear` is refused independently of the
/// colour basis. The destination shares the frame's layout and may be the frame
/// itself.
/// Nothing is written unless every stage before the apply succeeds, and the apply's own clipping
/// policy decides whether it writes. The frame's metadata is not read here: the caller derives the
/// result's transition through `photometricCalibrationTransition`, which records the recipe and
/// coefficients alongside the colour-calibrated flag.
///
/// @param execution the operation policy and engine-context lease.
/// @param frame the checked three-channel linear RGB frame.
/// @param destination where the calibrated samples go, laid out exactly as the frame. May equal
///        `frame.samples()`.
/// @param destinationBytes bytes available from `destination`, at least the layout's total.
/// @param solution the frame's world-coordinate solution.
/// @param reference the catalog and its colour indices.
/// @param detection how stars are found in each channel; its `maximumStars` is replaced by the
///        request's `maximumStarsPerChannel`.
/// @param request every other stage's parameters.
/// @return the fit, offsets, apply counts and samples; `ErrorCode::NotInitialized` when
///         `execution` holds nothing or the reference catalog is null or not built;
///         `ErrorCode::InvalidArgument` for an invalid frame, colour indices not aligned with the
///         catalog's entries, a null or undersized destination or a request field outside its
///         range; `ErrorCode::Unsupported` for a frame that is not three-channel linear floating
///         RGB or a filter name this build recognises as narrowband; `ErrorCode::EmptySelection`
///         when a channel yields no star (carrying the channel in `ContextKey::Completed`), no green
///         star with a flux matches an entry with a magnitude, or fewer samples than the fit floor
///         pair; and every failure the composed operations report.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PhotometricColorCalibrationResult>
calibratePhotometricColor(const ExecutionContext& execution, const StarFrameView& frame, void* destination,
                          std::uint64_t destinationBytes, const WcsSolution& solution,
                          const PhotometricReferenceCatalog& reference, const StarDetectionRequest& detection,
                          const PhotometricColorCalibrationRequest& request = {});

} // namespace an

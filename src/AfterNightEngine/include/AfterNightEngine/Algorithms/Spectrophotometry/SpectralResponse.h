// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The spectral response model: what a camera and its filters make of each
/// wavelength, and what a star's sampled spectrum therefore measures as in each channel.
///
/// **The model is stated as data; nothing is read out of a name.** A `ResponseProfile` carries its
/// three channel curves and a `ResponseProfileKind` that says whether the profile describes a colour
/// sensor or a monochrome one, and that kind - not a prefix of the identifier - is what decides
/// whether atmospheric extinction applies and whether a narrowband integration is admissible. Who
/// loaded the curves, from which file, under which review status, is the host's business.
///
/// **Evaluation is compiled once and applied many times.** `prepareResponse` resamples the three
/// curves onto the catalog's grid, folds the observation model - the normalized atmospheric
/// transmittance and the per-channel passband - into one weight per grid sample per channel, and
/// hands back a `PreparedResponse`. Integrating a star against it is then one fused pass over the
/// spectrum: flux times photon weight times channel weight, accumulated into the interpolant, with
/// no intermediate vector per star and one counted scratch block per worker in a batch.
///
/// **The interpolant is computed here.** The integral is Akima's, exactly as the GNU Scientific
/// Library computes it - the same slope rule, the same boundary extrapolation, the same piecewise
/// cubic quadrature - so the numbers match, but the coefficients are computed into scratch this
/// engine counts, with no allocation per call and no process-wide error handler.
///
/// **The arithmetic is Float64 for a stated reason.** Three hundred and forty-three products
/// accumulate into one count and the ratios of three such counts are what the fit reads, so the
/// width is `Float64Justification::ReductionAccumulator`, which the implementation holds to
/// `Image/ComputePrecision.h` by a static assertion; the spectra themselves stay the catalog's
/// Float32 and are widened on the way in.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The stable name of the synthetic-photometry recipe this build implements.
///
/// Photon-rate weighting by wavelength, Akima interpolation of the sampled response, and a
/// piecewise cubic integral over the stated passband.
/// For a caller to record beside a prediction it caches, so a cached colour can be told from one
/// made under another recipe; `SyntheticPhotometry` does not carry it.
inline constexpr const char* kSyntheticPhotometryAlgorithmVersion = "spcc-synthetic-photometry-v2";

/// The fewest samples a spectral curve may carry.
inline constexpr std::uint32_t kMinimumSpectralCurveSamples = 2u;

/// The fewest samples at which a curve is resampled through the Akima interpolant rather than
/// linearly. Below it the interpolant has no interior to fit.
inline constexpr std::uint32_t kMinimumAkimaSamples = 5u;

/// The most samples a spectral curve may carry.
inline constexpr std::uint32_t kMaxSpectralCurveSamples = 1u << 20u;

/// The most spectra one batch integrates.
inline constexpr std::uint32_t kMaxSyntheticPhotometryTargets = kMaxSpectrumCatalogRecords;

/// Standard sea-level pressure, in hectopascals.
inline constexpr double kStandardPressureHectopascals = 1013.25;

/// The zenith angle assumed when an observation states neither its airmass nor its altitude.
inline constexpr double kDefaultZenithAngleDegrees = 41.9;

/// One point of a spectral curve.
struct SpectralSample
{
    /// The wavelength, in nanometres.
    double wavelengthNm = 0.0;

    /// The curve's value there: a dimensionless throughput for a response, a relative spectral
    /// power for a white reference.
    double value = 0.0;

    /// Whether two samples are the same two numbers.
    ///
    /// @param left one sample.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const SpectralSample& left,
                                                   const SpectralSample& right) noexcept = default;
};

/// A curve over wavelength, sampled where its author sampled it.
struct SpectralCurve
{
    /// The curve's identity, as its author spelled it. Carried; never parsed.
    std::string identifier{};

    /// The curve's version, as text. Carried; never parsed.
    std::string version{};

    /// The samples, in strictly increasing wavelength. At least `kMinimumSpectralCurveSamples`.
    std::vector<SpectralSample> samples{};
};

/// Why a spectral curve is not one this build reads.
enum class SpectralCurveError : std::uint8_t
{
    /// The curve is well formed.
    None = 0u,

    /// Fewer than `kMinimumSpectralCurveSamples` samples.
    TooFewSamples = 1u,

    /// More than `kMaxSpectralCurveSamples` samples.
    TooManySamples = 2u,

    /// A wavelength or value is not a finite number.
    NonFiniteSample = 3u,

    /// A wavelength does not exceed the one before it.
    NonIncreasingWavelength = 4u,

    /// A value is negative.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NegativeValue = 5u
};

/// Whether a value names a curve refusal this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the value to inspect.
/// @return true for every reason, `None` included.
[[nodiscard]] constexpr bool isSpectralCurveError(SpectralCurveError error) noexcept
{
    return static_cast<std::uint8_t>(error) <= static_cast<std::uint8_t>(SpectralCurveError::NegativeValue);
}

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param error the reason to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSpectralCurveError"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* spectralCurveErrorName(SpectralCurveError error) noexcept;

/// Whether a curve is well formed, and if not, the first rule it breaks.
///
/// The order is a contract: count, finiteness, monotonicity, sign, sample by sample.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param curve the curve to check.
/// @return `SpectralCurveError::None`, or the first rule that refused.
[[nodiscard]] AFTERNIGHTENGINE_API SpectralCurveError checkSpectralCurve(const SpectralCurve& curve) noexcept;

/// The curve's value at a wavelength, by linear interpolation between its samples.
///
/// Zero outside the sampled interval and for a wavelength that is not finite; the last sample's own
/// value at the last wavelength, because a response is zero where its author did not measure it.
///
/// Thread-safe, reentrant, and allocation-free. A malformed curve yields zero.
///
/// @param curve the curve, which is only read.
/// @param wavelengthNm where to evaluate it.
/// @return the interpolated value.
[[nodiscard]] AFTERNIGHTENGINE_API double evaluateSpectralCurve(const SpectralCurve& curve,
                                                                double wavelengthNm) noexcept;

/// What kind of instrument a response profile describes.
///
/// Stated, never read out of an identifier. It decides two things: whether atmospheric extinction
/// is folded into the response - it is for a colour sensor and not for a monochrome one, whose
/// per-filter observation is corrected elsewhere - and whether a narrowband integration is
/// admissible - it is not for a monochrome sensor already described through its broadband RGB
/// filters.
enum class ResponseProfileKind : std::uint8_t
{
    /// A colour sensor: an RGB camera, with or without a filter in front of it.
    ColorSensor = 0u,

    /// A monochrome sensor alone, whose channels are the sensor through three filters the
    /// observation names.
    MonoSensor = 1u,

    /// A monochrome sensor through a broadband RGB filter set already folded into the curves.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    MonoSensorRgbFilters = 2u
};

/// Whether a value names a profile kind this build reads.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the value to inspect.
/// @return true for one of the three declared values.
[[nodiscard]] constexpr bool isResponseProfileKind(ResponseProfileKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(ResponseProfileKind::MonoSensorRgbFilters);
}

/// The kind's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param kind the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownResponseProfileKind"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* responseProfileKindName(ResponseProfileKind kind) noexcept;

/// The three channel responses of one camera-and-filter combination.
struct ResponseProfile
{
    /// The profile's identity, as its author spelled it. Carried; never parsed.
    std::string identifier{};

    /// The profile's version, as text. Carried; never parsed.
    std::string version{};

    /// What kind of instrument the curves describe.
    ResponseProfileKind kind = ResponseProfileKind::ColorSensor;

    /// The red, green and blue throughput curves.
    std::array<SpectralCurve, 3u> channels{};
};

/// The spectrum a neutral colour is defined against.
struct WhiteReferenceSpectrum
{
    /// The spectrum's identity, as its author spelled it. Carried; never parsed.
    std::string identifier{};

    /// The spectrum's version, as text. Carried; never parsed.
    std::string version{};

    /// Whether the samples are already photon rates. False means energy per wavelength, which the
    /// integration weights by wavelength as it does a star's flux; true means the weighting has
    /// been applied by the author and is not applied again.
    bool photonWeighted = false;

    /// The samples, in strictly increasing wavelength. At least `kMinimumSpectralCurveSamples`.
    std::vector<SpectralSample> samples{};
};

/// The atmospheric extinction an observation was made through.
struct AtmosphericExtinction
{
    /// Whether extinction is folded into the response at all.
    bool enabled = false;

    /// The observer's height above sea level, in metres. Finite and at or above minus five hundred.
    double observerHeightMeters = 10.0;

    /// The pressure, in hectopascals. Finite and positive.
    double pressureHectopascals = kStandardPressureHectopascals;

    /// Whether `pressureHectopascals` is a sea-level figure to be reduced to the observer's height,
    /// or the local pressure as measured.
    bool pressureIsSeaLevel = true;

    /// The airmass, when the caller states it. Finite and positive when present.
    std::optional<double> airmass{};

    /// The altitude of the field centre, in degrees, when the caller states it instead of the
    /// airmass. Above zero and at or below ninety when present.
    std::optional<double> centerAltitudeDegrees{};
};

/// One channel's passband under a narrowband integration.
struct NarrowbandPassband
{
    /// The passband centre, in nanometres. Finite.
    double centerNm = 0.0;

    /// The full width, in nanometres. Finite and positive.
    double bandwidthNm = 0.0;

    /// Whether two passbands are the same two numbers.
    ///
    /// @param left one passband.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const NarrowbandPassband& left,
                                                   const NarrowbandPassband& right) noexcept = default;
};

/// Whether and where each channel's integral is confined to a narrow passband.
struct NarrowbandIntegration
{
    /// Whether the passbands apply. False integrates each channel over the whole grid.
    bool enabled = false;

    /// The red, green and blue passbands. The defaults are hydrogen alpha, doubly ionized oxygen
    /// and hydrogen beta, seven nanometres wide.
    std::array<NarrowbandPassband, 3u> passbands{NarrowbandPassband{.centerNm = 656.3, .bandwidthNm = 7.0},
                                                 NarrowbandPassband{.centerNm = 500.7, .bandwidthNm = 7.0},
                                                 NarrowbandPassband{.centerNm = 486.1, .bandwidthNm = 7.0}};
};

/// What the observation adds to the instrument: the sky between it and the star, and the passbands
/// it was made through.
struct ObservationModel
{
    /// The extinction.
    AtmosphericExtinction atmosphere{};

    /// The narrowband passbands.
    NarrowbandIntegration narrowband{};
};

/// Why an observation model is not one this build applies.
enum class ObservationModelError : std::uint8_t
{
    /// The model is well formed.
    None = 0u,

    /// The observer height is not finite or is below minus five hundred metres.
    InvalidObserverHeight = 1u,

    /// The pressure is not finite and positive.
    InvalidPressure = 2u,

    /// A stated airmass is not finite and positive.
    InvalidAirmass = 3u,

    /// A stated centre altitude is not finite, not above zero or above ninety degrees.
    InvalidCenterAltitude = 4u,

    /// A passband centre is not finite, or a width is not finite and positive.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    InvalidPassband = 5u
};

/// Whether a value names an observation refusal this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the value to inspect.
/// @return true for every reason, `None` included.
[[nodiscard]] constexpr bool isObservationModelError(ObservationModelError error) noexcept
{
    return static_cast<std::uint8_t>(error) <= static_cast<std::uint8_t>(ObservationModelError::InvalidPassband);
}

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param error the reason to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownObservationModelError"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* observationModelErrorName(ObservationModelError error) noexcept;

/// Whether an observation model is well formed, and if not, the first rule it breaks.
///
/// The atmosphere's fields are read only when it is enabled, and the passbands only when the
/// narrowband integration is; the order is a contract: height, pressure, airmass, altitude,
/// passbands.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param model the model to check.
/// @return `ObservationModelError::None`, or the first rule that refused.
[[nodiscard]] AFTERNIGHTENGINE_API ObservationModelError checkObservationModel(const ObservationModel& model) noexcept;

/// The relative airmass at a zenith angle.
///
/// Young's (1994) rational approximation in the true zenith angle, which stays finite at the
/// horizon - about 31.7 - where a secant does not.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param zenithAngleDegrees the angle from the zenith, in degrees, from zero to ninety for a
///        meaningful answer.
/// @return the airmass, one at the zenith to within a millionth.
[[nodiscard]] AFTERNIGHTENGINE_API double airmassFromZenithAngle(double zenithAngleDegrees) noexcept;

/// The airmass an extinction model resolves to.
///
/// The stated airmass when present; else the airmass at the stated centre altitude; else the
/// airmass at `kDefaultZenithAngleDegrees`.
///
/// Thread-safe, reentrant, and allocation-free. A stated airmass or altitude outside its range is
/// passed over as though it were absent, so a malformed model falls through to the next source.
///
/// @param atmosphere the extinction model.
/// @return the resolved airmass.
[[nodiscard]] AFTERNIGHTENGINE_API double resolvedAirmass(const AtmosphericExtinction& atmosphere) noexcept;

/// The unnormalized Rayleigh transmittance at one wavelength under an extinction model.
///
/// The optical depth - a pressure ratio times a height term times a power of the wavelength in
/// micrometres - raised to the resolved airmass. `prepareResponse` divides this by its maximum
/// over the grid, so the response keeps its scale and only its colour changes.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param wavelengthNm the wavelength, in nanometres; positive for a meaningful answer.
/// @param atmosphere the extinction model, read whether or not it is enabled.
/// @return the transmittance, in (0, 1] for a positive wavelength.
[[nodiscard]] AFTERNIGHTENGINE_API double atmosphericTransmittance(double wavelengthNm,
                                                                   const AtmosphericExtinction& atmosphere) noexcept;

/// A closed interval of wavelength.
struct WavelengthInterval
{
    /// The lower end, in nanometres.
    double startNm = 0.0;

    /// The upper end, in nanometres.
    double endNm = 0.0;

    /// Whether two intervals are the same two numbers.
    ///
    /// @param left one interval.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const WavelengthInterval& left,
                                                   const WavelengthInterval& right) noexcept = default;
};

/// A response profile and an observation, compiled onto one grid.
///
/// Everything an integration reads per star, computed once: the grid's wavelengths, one weight per
/// grid sample per channel - the resampled response times the normalized transmittance - and the
/// interval each channel is integrated over. Copies are independent; a prepared response is read by
/// any number of integrations at once.
struct PreparedResponse
{
    /// The grid every weight is on.
    SpectralGrid grid{};

    /// The profile's identity, carried for the record.
    std::string profileIdentifier{};

    /// The profile's version, carried for the record.
    std::string profileVersion{};

    /// What kind of instrument the profile describes.
    ResponseProfileKind kind = ResponseProfileKind::ColorSensor;

    /// Whether extinction was folded into the weights: the model enabled it and the kind admits it.
    bool atmosphereApplied = false;

    /// The airmass the extinction was evaluated at, meaningful when applied.
    double airmass = 1.0;

    /// The transmittance the extinction was normalized by, meaningful when applied.
    double atmosphereNormalization = 1.0;

    /// `grid.sampleCount` wavelengths, in nanometres.
    std::vector<double> wavelengthsNm{};

    /// `grid.sampleCount` weights per channel: the resampled response times the normalized
    /// transmittance, red, green and blue.
    std::array<std::vector<double>, 3u> channelWeights{};

    /// The interval each channel's count is integrated over, red, green and blue: the whole grid,
    /// or the passband cut to it. A passband that misses the grid leaves an empty interval, whose
    /// count is zero.
    std::array<WavelengthInterval, 3u> integrationIntervals{};
};

/// Compiles a profile and an observation onto a grid.
///
/// The kind, the grid and the observation are checked, a narrowband integration is refused for a
/// `MonoSensorRgbFilters` profile, and every curve is checked; then each curve is resampled by the
/// rule `resampleSpectralCurve` states, the transmittance is evaluated over the grid and normalized
/// by its maximum when the model enables it and the kind is `ColorSensor`, and each channel's
/// interval is the grid, from its `startNm` to its stated `endNm`, or its passband cut to that.
///
/// Runs on the calling thread and checks cancellation once, before it begins. It allocates the
/// returned tables and, for each curve long enough for the interpolant, transient interpolant
/// buffers.
///
/// @param execution the operation policy and engine-context lease.
/// @param profile the three curves and the kind.
/// @param observation the sky and the passbands.
/// @param grid the grid to compile onto; the sampled XP grid by default.
/// @return the compiled response; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a grid with fewer than `kMinimumAkimaSamples` samples
///         or a non-positive step, a curve or observation that fails its check - the message names
///         the reason - or a kind outside its range; `ErrorCode::Unsupported` for a narrowband
///         integration over a monochrome sensor with RGB filters; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PreparedResponse> prepareResponse(const ExecutionContext& execution,
                                                                            const ResponseProfile& profile,
                                                                            const ObservationModel& observation = {},
                                                                            const SpectralGrid& grid = kXpSampledGrid);

/// Resamples a curve onto a grid.
///
/// Through the Akima interpolant when the curve has at least `kMinimumAkimaSamples` samples and
/// linearly otherwise; zero outside the curve's own interval, and never negative.
///
/// Runs on the calling thread and checks cancellation once, before it begins. It allocates the
/// answer and, for the interpolant, working copies of the curve and its coefficients.
///
/// @param execution the operation policy and engine-context lease.
/// @param curve the curve, which is only read.
/// @param grid the grid to sample at.
/// @return `grid.sampleCount` values; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a malformed curve or a grid with no samples or a
///         non-positive step; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<double>>
resampleSpectralCurve(const ExecutionContext& execution, const SpectralCurve& curve, const SpectralGrid& grid);

/// The product of several curves, sampled on a grid.
///
/// Each component is resampled through `resampleSpectralCurve` and the samples are multiplied,
/// which is how a sensor, a filter and a window become one channel response. The result is a curve
/// whose samples are the grid's wavelengths.
///
/// Runs on the calling thread and allocates the answer.
///
/// @param execution the operation policy and engine-context lease.
/// @param components the curves to multiply, at least one.
/// @param identifier the identity the result carries.
/// @param version the version the result carries.
/// @param grid the grid to sample on; the sampled XP grid by default.
/// @return the product curve; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::EmptySelection` for no components; `ErrorCode::InvalidArgument` for a
///         malformed component or grid; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectralCurve>
composeSpectralCurves(const ExecutionContext& execution, std::span<const SpectralCurve> components,
                      std::string identifier, std::string version, const SpectralGrid& grid = kXpSampledGrid);

/// The response of a sensor behind both a filter and a window, from the three curves an author
/// measured: the sensor alone, the sensor with the filter, and the sensor with the window.
///
/// `sensorWithFilter * sensorWithWindow / sensor` on the union of the three curves' wavelengths
/// inside their common interval, with a sensor value at or below a small floor giving zero: the rule
/// for a one-shot-colour camera whose filter and low-pass window were each measured against the
/// bare sensor.
///
/// Runs on the calling thread and allocates the answer.
///
/// @param sensor the bare sensor's response.
/// @param sensorWithFilter the sensor measured through the filter.
/// @param sensorWithWindow the sensor measured through the window.
/// @param identifier the identity the result carries.
/// @param version the version the result carries.
/// @return the combined curve; `ErrorCode::InvalidArgument` for a malformed input; or
///         `ErrorCode::InvalidData` when the three share no wavelength interval or a sample of the
///         result is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectralCurve>
combineFilterResponses(const SpectralCurve& sensor, const SpectralCurve& sensorWithFilter,
                       const SpectralCurve& sensorWithWindow, std::string identifier, std::string version);

/// What became of one spectrum's integration.
enum class SyntheticPhotometryStatus : std::uint8_t
{
    /// Three counts were integrated and the green one is positive, so the ratios exist.
    Integrated = 0u,

    /// The catalog holds no record with the requested identifier.
    MissingSource = 1u,

    /// The spectrum holds a sample that is not finite, or an uncertainty that is not finite and
    /// non-negative.
    InvalidSpectrum = 2u,

    /// The green count is not positive, so no ratio exists.
    NonPositiveGreen = 3u,

    /// A count is not a finite number.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    NonFinite = 4u
};

/// How many statuses this build reports, `Integrated` included.
inline constexpr std::size_t kSyntheticPhotometryStatusCount =
    static_cast<std::size_t>(SyntheticPhotometryStatus::NonFinite) + 1u;

/// Whether a value names a status this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param status the value to inspect.
/// @return true for every status, `Integrated` included.
[[nodiscard]] constexpr bool isSyntheticPhotometryStatus(SyntheticPhotometryStatus status) noexcept
{
    return static_cast<std::size_t>(status) < kSyntheticPhotometryStatusCount;
}

/// The status's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param status the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSyntheticPhotometryStatus"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* syntheticPhotometryStatusName(SyntheticPhotometryStatus status) noexcept;

/// What one spectrum measures as through a prepared response.
struct SyntheticPhotometry
{
    /// What became of the integration.
    SyntheticPhotometryStatus status = SyntheticPhotometryStatus::NonFinite;

    /// The three channel counts, red, green and blue, in the catalog's flux unit times nanometres
    /// squared.
    std::array<double, 3u> counts{};

    /// The standard uncertainty of each count, propagated from the spectrum's own uncertainties.
    std::array<double, 3u> uncertainties{};

    /// Whether `uncertainties` was computed, which it is when uncertainties were supplied.
    bool uncertaintyAvailable = false;

    /// Red, green and blue relative to green. Ones unless `status` is `Integrated`.
    std::array<double, 3u> relativeToGreen{1.0, 1.0, 1.0};
};

/// Integrates one sampled spectrum against a prepared response.
///
/// Per channel: every grid sample's flux, widened to double, times its wavelength - the photon
/// weighting - times the channel's weight is accumulated into the interpolant, and the interpolant
/// is integrated over the channel's interval. The uncertainty, when the spectrum carries one, is the
/// quadrature sum over the samples inside that interval of each sample's uncertainty times its
/// wavelength, its channel weight and its weight under the grid's trapezoidal rule - the step, and
/// half of it at the grid's two ends. The counts integrate the spline and the uncertainty the
/// trapezoid.
///
/// A spectrum that cannot yield ratios is a record whose `status` says why, not a failure: the
/// operation fails only for a malformed request.
///
/// Thread-safe with respect to `execution` and `response`, which are only read. Runs on the calling
/// thread and checks cancellation once, before it begins. Scratch comes from the engine context's
/// memory resource and is released before this returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param response the compiled response.
/// @param flux `response.grid.sampleCount` flux samples.
/// @param fluxError the same number of uncertainties, or empty for none.
/// @return the prediction; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a response whose tables are not the grid's size or a
///         flux or uncertainty span of the wrong length; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SyntheticPhotometry>
integrateSyntheticPhotometry(const ExecutionContext& execution, const PreparedResponse& response,
                             std::span<const float> flux, std::span<const float> fluxError = {});

/// What one batch did.
struct SyntheticPhotometryBatchResult
{
    /// How many spectra were integrated to usable ratios.
    std::uint32_t integratedCount = 0u;

    /// How many predictions each status claimed, indexed by `SyntheticPhotometryStatus`; the
    /// `Integrated` entry equals `integratedCount`.
    std::array<std::uint32_t, kSyntheticPhotometryStatusCount> statuses{};

    /// Peak engine-context bytes held for worker scratch.
    std::uint64_t workingBytes = 0u;
};

/// Integrates a contiguous table of spectra on the operation's threads.
///
/// Each spectrum is integrated exactly as `integrateSyntheticPhotometry` would integrate it and its
/// prediction is written at its own position in `out`. Scratch is one counted block per worker
/// slot, taken once for the batch. The result does not depend on how many threads ran.
///
/// @param execution the operation policy and engine-context lease.
/// @param response the compiled response.
/// @param flux `out.size() * response.grid.sampleCount` flux samples, spectrum-major.
/// @param fluxError the same number of uncertainties, spectrum-major, or empty for none.
/// @param out receives one prediction per spectrum; at most `kMaxSyntheticPhotometryTargets` long.
///        A cancellation during the batch leaves the predictions already made written.
/// @return the counts; `ErrorCode::EmptySelection` for an empty output; `ErrorCode::LimitExceeded`
///         above the ceiling; `ErrorCode::InvalidArgument` for a flux or uncertainty table that is
///         not the output's length times the grid's; and every failure the single integration
///         reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SyntheticPhotometryBatchResult>
integrateSyntheticPhotometryTable(const ExecutionContext& execution, const PreparedResponse& response,
                                  std::span<const float> flux, std::span<const float> fluxError,
                                  std::span<SyntheticPhotometry> out);

/// Integrates the spectra of catalog records, looked up by identifier, on the operation's threads.
///
/// Each identifier is resolved through `SpectrumCatalog::findSourceIdentifier`; a record the
/// catalog does not hold is a prediction whose status is `MissingSource`. Every held record is
/// integrated exactly as `integrateSyntheticPhotometry` would integrate it, with the record's own
/// flux uncertainties. The result does not depend on how many threads ran.
///
/// @param execution the operation policy and engine-context lease.
/// @param response the compiled response, on the catalog's grid.
/// @param catalog the catalog holding the spectra.
/// @param sourceIdentifiers the records to integrate, at most `kMaxSyntheticPhotometryTargets`.
/// @param out receives one prediction per identifier and must have exactly the same length.
/// @return the counts; `ErrorCode::NotInitialized` when `execution` holds nothing or the catalog
///         handle is empty; `ErrorCode::InvalidArgument` for a response whose tables are not its
///         grid's size or that is not on the catalog's grid; `ErrorCode::EmptySelection` for no
///         identifiers; `ErrorCode::LimitExceeded` above `kMaxSyntheticPhotometryTargets`;
///         `ErrorCode::IncompatibleArguments` for an output-length mismatch; a memory-budget
///         refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SyntheticPhotometryBatchResult>
integrateSyntheticPhotometryBatch(const ExecutionContext& execution, const PreparedResponse& response,
                                  const SpectrumCatalog& catalog, std::span<const std::uint64_t> sourceIdentifiers,
                                  std::span<SyntheticPhotometry> out);

/// Integrates a white reference against a prepared response.
///
/// The spectrum is resampled onto the response's grid by the rule `resampleSpectralCurve` states -
/// zero outside its own samples - weighted by wavelength unless it is already photon weighted, and
/// integrated per channel exactly as a star would be.
///
/// Runs on the calling thread and checks cancellation once, before it begins. The integration
/// scratch comes from the engine context's memory resource and the resampled spectrum is an
/// ordinary heap allocation; both are released before this returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param response the compiled response.
/// @param whiteReference the spectrum.
/// @return the three counts, red, green and blue; `ErrorCode::NotInitialized` when `execution`
///         holds nothing; `ErrorCode::InvalidArgument` for a malformed spectrum or a response whose
///         tables are not the grid's size; a memory-budget refusal; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::array<double, 3u>>
integrateWhiteReference(const ExecutionContext& execution, const PreparedResponse& response,
                        const WhiteReferenceSpectrum& whiteReference);

/// The per-channel gains that make three white-reference counts neutral, relative to green.
///
/// `green / count` per channel, one for green itself; a channel whose count is not finite and
/// positive, or a green that is not, keeps a gain of one.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param counts the three counts, red, green and blue.
/// @return the three gains.
[[nodiscard]] AFTERNIGHTENGINE_API std::array<double, 3u>
whiteReferenceGains(const std::array<double, 3u>& counts) noexcept;

/// The white-reference response gains a spectrophotometric fit takes.
///
/// `integrateWhiteReference` followed by `whiteReferenceGains`, with a count that is not finite and
/// positive refused rather than defaulted, because a fit anchored on a gain of one that was not
/// measured is anchored on nothing.
///
/// @param execution the operation policy and engine-context lease.
/// @param response the compiled response.
/// @param whiteReference the spectrum.
/// @return the three gains; `ErrorCode::InvalidData` when a count is not finite and positive; and
///         every failure the integration reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::array<double, 3u>>
whiteReferenceResponseGains(const ExecutionContext& execution, const PreparedResponse& response,
                            const WhiteReferenceSpectrum& whiteReference);

/// What a response profile's author says it models, beyond its curves.
///
/// The facts a profile's record carries, checked against the image's processing state before a
/// calibration. They are stated by whoever loaded the profile.
struct ResponseProfileCapabilities
{
    /// The colour bases the profile's curves describe. A frame in another basis is not one the
    /// profile can predict.
    std::vector<ColorSpace> compatibleColorSpaces{};

    /// Whether the curves include the camera's raw white balance, so a frame with that balance
    /// applied is described.
    bool modelsRawWhiteBalance = false;

    /// Whether the curves include the camera's colour matrix, so a frame with that matrix applied
    /// is described.
    bool modelsCameraMatrix = false;

    /// Whether the asset passed the review its distribution requires. False - an unreviewed or
    /// private asset - makes `checkResponseProfileCompatibility` refuse the profile.
    bool reviewed = false;
};

/// What the host states about the frame a profile is to be used on.
struct FrameColorState
{
    /// The frame's colour basis.
    ColorSpace colorSpace = ColorSpace::Unspecified;

    /// Whether the camera's raw white balance has been applied.
    bool rawWhiteBalanceApplied = false;

    /// Whether the camera's colour matrix has been applied.
    bool cameraMatrixApplied = false;
};

/// Why a profile may not be used on a frame.
enum class ResponseCompatibilityVerdict : std::uint8_t
{
    /// The profile describes the frame.
    Compatible = 0u,

    /// The profile carries no identifier or no version.
    MissingIdentity = 1u,

    /// The asset is private or unreviewed.
    UnreviewedAsset = 2u,

    /// The frame's colour basis is not one the profile describes.
    IncompatibleColorSpace = 3u,

    /// The frame has its raw white balance applied and the profile does not model it.
    RawWhiteBalanceNotModelled = 4u,

    /// The frame has its colour matrix applied and the profile does not model it.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CameraMatrixNotModelled = 5u
};

/// Whether a value names a verdict this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param verdict the value to inspect.
/// @return true for every verdict, `Compatible` included.
[[nodiscard]] constexpr bool isResponseCompatibilityVerdict(ResponseCompatibilityVerdict verdict) noexcept
{
    return static_cast<std::uint8_t>(verdict) <=
           static_cast<std::uint8_t>(ResponseCompatibilityVerdict::CameraMatrixNotModelled);
}

/// The verdict's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param verdict the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownResponseCompatibilityVerdict"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
responseCompatibilityVerdictName(ResponseCompatibilityVerdict verdict) noexcept;

/// Whether a profile may be used on a frame, and if not, the first reason why not.
///
/// The order is a contract: identity, review, colour basis, white balance, colour matrix.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param profile the profile.
/// @param capabilities what its author says it models.
/// @param frame what the host says about the frame.
/// @return `ResponseCompatibilityVerdict::Compatible`, or the first rule that refused.
[[nodiscard]] AFTERNIGHTENGINE_API ResponseCompatibilityVerdict
checkResponseProfileCompatibility(const ResponseProfile& profile, const ResponseProfileCapabilities& capabilities,
                                  const FrameColorState& frame) noexcept;

} // namespace an

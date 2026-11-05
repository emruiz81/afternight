// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the spectral response model: curve checks, the compiled response, synthetic
// photometry singly and in batches, the white reference and profile compatibility.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectralResponse.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/spectrophotometry/AkimaSpline.h"
#include "algorithms/spectrophotometry/SpectrophotometryInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using spectrophotometry_detail::akimaEvaluate;
using spectrophotometry_detail::akimaInitialize;
using spectrophotometry_detail::akimaIntegrate;
using spectrophotometry_detail::AkimaScratch;
using spectrophotometry_detail::akimaScratchDoubles;
using spectrophotometry_detail::akimaScratchOver;
using spectrophotometry_detail::failure;
using spectrophotometry_detail::limitFailure;

constexpr const char* kPrepareOperation = "an::prepareResponse";
constexpr const char* kResampleOperation = "an::resampleSpectralCurve";
constexpr const char* kComposeOperation = "an::composeSpectralCurves";
constexpr const char* kCombineOperation = "an::combineFilterResponses";
constexpr const char* kIntegrateOperation = "an::integrateSyntheticPhotometry";
constexpr const char* kTableOperation = "an::integrateSyntheticPhotometryTable";
constexpr const char* kBatchOperation = "an::integrateSyntheticPhotometryBatch";
constexpr const char* kWhiteOperation = "an::integrateWhiteReference";
constexpr const char* kWhiteGainsOperation = "an::whiteReferenceResponseGains";
/// Wavelengths closer than this are one point when three curves are merged onto a shared grid, and
/// an overlap no wider than it is empty.
constexpr double kWavelengthToleranceNm = 1.0e-9;
/// A sensor response at or below this is taken as zero when a filter response is recovered by
/// dividing by it, rather than divided by.
constexpr double kSensorFloor = 1.0e-12;
constexpr std::uint32_t kColorChannelCount = 3u;

/// The width the integration accumulates in, and why it may: a count is a long summation of
/// products, and its error is what the width buys.
constexpr ComputePrecision kSyntheticPhotometryPrecision = ComputePrecision::Float64;
constexpr Float64Justification kSyntheticPhotometryPrecisionJustification = Float64Justification::ReductionAccumulator;
static_assert(checkComputePrecision(SampleType::Float32, OutputPrecisionContract::NormalizedOrLossy,
                                    kSyntheticPhotometryPrecision,
                                    kSyntheticPhotometryPrecisionJustification) == PrecisionError::None,
              "the synthetic photometry's stated width must be one the precision policy admits");

/// Whether a value is a finite positive number.
///
/// @param value the value.
/// @return true when finite and above zero.
[[nodiscard]] bool positiveFinite(const double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// The first rule a sample list breaks, or `None`.
///
/// @param samples the samples.
/// @return the verdict.
[[nodiscard]] SpectralCurveError checkSamples(const std::span<const SpectralSample> samples) noexcept
{
    if (samples.size() < kMinimumSpectralCurveSamples)
    {
        return SpectralCurveError::TooFewSamples;
    }
    if (samples.size() > kMaxSpectralCurveSamples)
    {
        return SpectralCurveError::TooManySamples;
    }
    double previous = -std::numeric_limits<double>::infinity();
    for (const SpectralSample& sample : samples)
    {
        if (!std::isfinite(sample.wavelengthNm) || !std::isfinite(sample.value))
        {
            return SpectralCurveError::NonFiniteSample;
        }
        if (sample.wavelengthNm <= previous)
        {
            return SpectralCurveError::NonIncreasingWavelength;
        }
        if (sample.value < 0.0)
        {
            return SpectralCurveError::NegativeValue;
        }
        previous = sample.wavelengthNm;
    }
    return SpectralCurveError::None;
}

/// The linear interpolation of well-formed samples at a wavelength; zero outside them.
///
/// @param samples the samples, strictly increasing.
/// @param wavelengthNm where to evaluate.
/// @return the value.
[[nodiscard]] double evaluateSamples(const std::span<const SpectralSample> samples, const double wavelengthNm) noexcept
{
    if (samples.empty() || !std::isfinite(wavelengthNm))
    {
        return 0.0;
    }
    const SpectralSample& first = samples.front();
    const SpectralSample& last = samples.back();
    if (wavelengthNm < first.wavelengthNm || wavelengthNm > last.wavelengthNm)
    {
        return 0.0;
    }
    if (wavelengthNm == last.wavelengthNm)
    {
        return last.value;
    }
    const auto upper =
        std::upper_bound(samples.begin(), samples.end(), wavelengthNm,
                         [](const double at, const SpectralSample& sample) { return at < sample.wavelengthNm; });
    if (upper == samples.begin())
    {
        return upper->value;
    }
    if (upper == samples.end())
    {
        return last.value;
    }
    const auto lower = upper - 1;
    const double span = upper->wavelengthNm - lower->wavelengthNm;
    if (span <= 0.0)
    {
        return 0.0;
    }
    const double t = (wavelengthNm - lower->wavelengthNm) / span;
    return lower->value + (upper->value - lower->value) * t;
}

/// Whether a grid can be sampled at all.
///
/// @param grid the grid.
/// @return true for a positive count, a finite start and a finite positive step.
[[nodiscard]] bool usableGrid(const SpectralGrid& grid) noexcept
{
    return grid.sampleCount > 0u && std::isfinite(grid.startNm) && std::isfinite(grid.stepNm) && grid.stepNm > 0.0;
}

/// Resamples well-formed samples onto a grid: through the interpolant above the point floor,
/// linearly below it; zero outside the samples and never negative.
///
/// @param samples the samples, already checked.
/// @param grid the grid, already checked.
/// @param out receives `grid.sampleCount` values.
void resampleSamples(const std::span<const SpectralSample> samples, const SpectralGrid& grid, std::vector<double>& out)
{
    out.assign(grid.sampleCount, 0.0);
    if (samples.size() < kMinimumAkimaSamples)
    {
        for (std::uint32_t index = 0u; index < grid.sampleCount; ++index)
        {
            out[index] = evaluateSamples(samples, spectralGridWavelengthNm(grid, index));
        }
        return;
    }
    std::vector<double> x(samples.size());
    std::vector<double> y(samples.size());
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        x[index] = samples[index].wavelengthNm;
        y[index] = samples[index].value;
    }
    std::vector<double> block(akimaScratchDoubles(samples.size()), 0.0);
    const AkimaScratch scratch = akimaScratchOver(block.data(), samples.size());
    akimaInitialize(x, y, scratch);
    for (std::uint32_t index = 0u; index < grid.sampleCount; ++index)
    {
        const double wavelength = spectralGridWavelengthNm(grid, index);
        if (wavelength < x.front() || wavelength > x.back())
        {
            continue;
        }
        out[index] = (std::max)(0.0, akimaEvaluate(x, y, scratch, wavelength));
    }
}

/// The observer's local pressure, reduced from sea level when the model says so.
///
/// The reduction is the barometric formula of a standard troposphere with a constant temperature
/// lapse rate: `P = P0 * (1 - L * h / T0) ^ (g * M / (R * L))`, with the constants below.
///
/// @param atmosphere the extinction model.
/// @return the pressure, in hectopascals.
[[nodiscard]] double localPressureHectopascals(const AtmosphericExtinction& atmosphere) noexcept
{
    if (!atmosphere.pressureIsSeaLevel)
    {
        return atmosphere.pressureHectopascals;
    }
    constexpr double kLapseRateKelvinPerMeter = 0.0065;
    constexpr double kSeaLevelTemperatureKelvin = 288.15;
    constexpr double kGravity = 9.80665;
    constexpr double kMolarMassAir = 0.0289644;
    constexpr double kGasConstant = 8.3144598;
    const double exponent = (kGravity * kMolarMassAir) / (kGasConstant * kLapseRateKelvinPerMeter);
    return atmosphere.pressureHectopascals *
           std::pow(1.0 - ((kLapseRateKelvinPerMeter * atmosphere.observerHeightMeters) / kSeaLevelTemperatureKelvin),
                    exponent);
}

/// Whether the tables of a prepared response are the size its grid says.
///
/// @param response the response.
/// @return true when every table is `grid.sampleCount` long and the count admits the interpolant.
[[nodiscard]] bool consistentResponse(const PreparedResponse& response) noexcept
{
    const std::size_t count = response.grid.sampleCount;
    if (count < kMinimumAkimaSamples || !usableGrid(response.grid) || response.wavelengthsNm.size() != count)
    {
        return false;
    }
    for (const std::vector<double>& weights : response.channelWeights)
    {
        if (weights.size() != count)
        {
            return false;
        }
    }
    return true;
}

/// One integration's scratch: three product rows and the interpolant's coefficients.
struct IntegrationScratch
{
    /// `3 * n` products, channel-major.
    double* products = nullptr;

    /// The interpolant's coefficients over `n` points.
    AkimaScratch akima{};
};

/// How many doubles one integration needs.
///
/// @param sampleCount the grid's count.
/// @return the length.
[[nodiscard]] constexpr std::size_t integrationScratchDoubles(const std::size_t sampleCount) noexcept
{
    return 3u * sampleCount + akimaScratchDoubles(sampleCount);
}

/// Carves one integration's scratch out of a block.
///
/// @param block the block.
/// @param sampleCount the grid's count.
/// @return the regions.
[[nodiscard]] IntegrationScratch integrationScratchOver(double* const block, const std::size_t sampleCount) noexcept
{
    IntegrationScratch scratch;
    scratch.products = block;
    scratch.akima = akimaScratchOver(block + 3u * sampleCount, sampleCount);
    return scratch;
}

/// The integral of a product row over one channel's interval, or zero for an empty interval.
///
/// @param response the response.
/// @param products the row.
/// @param channel which channel's interval.
/// @param scratch the interpolant's coefficients.
/// @return the integral.
[[nodiscard]] double integrateRow(const PreparedResponse& response, const std::span<const double> products,
                                  const std::size_t channel, const AkimaScratch& scratch) noexcept
{
    const WavelengthInterval& interval = response.integrationIntervals[channel];
    if (!std::isfinite(interval.startNm) || !std::isfinite(interval.endNm) || interval.endNm <= interval.startNm)
    {
        return 0.0;
    }
    akimaInitialize(response.wavelengthsNm, products, scratch);
    return akimaIntegrate(response.wavelengthsNm, products, scratch, interval.startNm, interval.endNm);
}

/// Integrates one spectrum: the fused product pass, the three integrals and the verdict.
///
/// @param response the response, already checked.
/// @param flux the spectrum, `n` long.
/// @param fluxError `n` uncertainties, or empty.
/// @param scratch the scratch.
/// @param out receives the prediction.
void integrateOne(const PreparedResponse& response, const std::span<const float> flux,
                  const std::span<const float> fluxError, const IntegrationScratch& scratch,
                  SyntheticPhotometry& out) noexcept
{
    const std::size_t count = response.grid.sampleCount;
    const bool propagate = !fluxError.empty();
    out = SyntheticPhotometry{};
    std::array<double, kColorChannelCount> variance{};
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double sample = static_cast<double>(flux[index]);
        if (!std::isfinite(sample))
        {
            out.status = SyntheticPhotometryStatus::InvalidSpectrum;
            return;
        }
        double error = 0.0;
        if (propagate)
        {
            error = static_cast<double>(fluxError[index]);
            if (!std::isfinite(error) || error < 0.0)
            {
                out.status = SyntheticPhotometryStatus::InvalidSpectrum;
                return;
            }
        }
        const double wavelength = response.wavelengthsNm[index];
        const double photonRate = sample * wavelength;
        // The counts integrate the spline; the uncertainty is propagated through the trapezoidal
        // rule's weights instead, half at the grid's two ends and zero outside the channel's interval.
        const double endpointScale = (index == 0u || index + 1u == count) ? 0.5 : 1.0;
        for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
        {
            const double weight = response.channelWeights[channel][index];
            scratch.products[channel * count + index] = photonRate * weight;
            if (propagate)
            {
                const WavelengthInterval& interval = response.integrationIntervals[channel];
                const bool inRange = wavelength >= interval.startNm && wavelength <= interval.endNm;
                const double integrationWeight =
                    inRange ? endpointScale * response.grid.stepNm * wavelength * weight : 0.0;
                variance[channel] += (error * integrationWeight) * (error * integrationWeight);
            }
        }
    }
    for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        const std::span<const double> row{scratch.products + channel * count, count};
        out.counts[channel] = integrateRow(response, row, channel, scratch.akima);
        if (propagate)
        {
            out.uncertainties[channel] = std::sqrt(variance[channel]);
        }
    }
    out.uncertaintyAvailable = propagate;
    for (const double value : out.counts)
    {
        if (!std::isfinite(value))
        {
            out.status = SyntheticPhotometryStatus::NonFinite;
            return;
        }
    }
    const double green = out.counts[1];
    if (green <= 0.0)
    {
        out.status = SyntheticPhotometryStatus::NonPositiveGreen;
        return;
    }
    for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        out.relativeToGreen[channel] = out.counts[channel] / green;
    }
    out.status = SyntheticPhotometryStatus::Integrated;
}

/// The checks every integration entry point makes before it takes scratch.
///
/// @param execution the operation.
/// @param response the response.
/// @param operation the entry point speaking.
/// @return success, or the first refusal.
[[nodiscard]] Status checkIntegrationInputs(const ExecutionContext& execution, const PreparedResponse& response,
                                            const char* const operation)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "synthetic photometry needs an execution context", operation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!consistentResponse(response))
    {
        return failure(ErrorCode::InvalidArgument, "the prepared response's tables are not its grid's size", operation);
    }
    return Status::success();
}

/// Runs one integration batch on the operation's threads.
///
/// @tparam SpectrumOf a callable `(index, flux&, error&) -> bool` that names one spectrum, or
///         returns false for a missing one.
/// @param execution the operation.
/// @param response the response, already checked.
/// @param out the predictions.
/// @param spectrumOf how each spectrum is reached.
/// @param operation the entry point speaking.
/// @return the counts, or the first failure.
template <typename SpectrumOf>
[[nodiscard]] Result<SyntheticPhotometryBatchResult>
runBatch(const ExecutionContext& execution, const PreparedResponse& response, const std::span<SyntheticPhotometry> out,
         const SpectrumOf& spectrumOf, const char* const operation)
{
    const std::size_t count = response.grid.sampleCount;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(count) * sizeof(double) * 4u,
                        .operationsPerItem = static_cast<std::uint64_t>(count) * 48u};
    const std::uint32_t slots = (std::max)(1u, plannedWorkerCount(execution, out.size(), cost));
    const std::uint64_t scratchBytes =
        static_cast<std::uint64_t>(slots) * integrationScratchDoubles(count) * sizeof(double);
    const std::uint64_t counterBytes =
        static_cast<std::uint64_t>(slots) * kSyntheticPhotometryStatusCount * sizeof(std::uint32_t);
    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, scratchBytes + counterBytes, alignof(double), operation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    double* const block = static_cast<double*>(memory->data());
    auto* const counters = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(memory->data()) + scratchBytes);
    std::fill(counters, counters + static_cast<std::size_t>(slots) * kSyntheticPhotometryStatusCount, 0u);

    const auto body = [&](const WorkerTask task) -> Status {
        const IntegrationScratch scratch = integrationScratchOver(
            block + static_cast<std::size_t>(task.workerSlot) * integrationScratchDoubles(count), count);
        std::uint32_t* const slotCounters =
            counters + static_cast<std::size_t>(task.workerSlot) * kSyntheticPhotometryStatusCount;
        for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
        {
            std::span<const float> flux;
            std::span<const float> error;
            if (!spectrumOf(static_cast<std::size_t>(index), flux, error))
            {
                out[index] = SyntheticPhotometry{};
                out[index].status = SyntheticPhotometryStatus::MissingSource;
            }
            else
            {
                integrateOne(response, flux, error, scratch, out[index]);
            }
            ++slotCounters[static_cast<std::size_t>(out[index].status)];
        }
        return Status::success();
    };
    if (const Status ran = parallelForWorkers(execution, out.size(), cost, body); ran.failed())
    {
        return ran;
    }

    SyntheticPhotometryBatchResult result{};
    result.workingBytes = memory->bytes();
    for (std::uint32_t slot = 0u; slot < slots; ++slot)
    {
        const std::uint32_t* const slotCounters =
            counters + static_cast<std::size_t>(slot) * kSyntheticPhotometryStatusCount;
        for (std::size_t status = 0u; status < kSyntheticPhotometryStatusCount; ++status)
        {
            result.statuses[status] += slotCounters[status];
        }
    }
    result.integratedCount = result.statuses[static_cast<std::size_t>(SyntheticPhotometryStatus::Integrated)];
    return result;
}

} // namespace

const char* spectralCurveErrorName(const SpectralCurveError error) noexcept
{
    switch (error)
    {
    case SpectralCurveError::None:
        return "None";
    case SpectralCurveError::TooFewSamples:
        return "TooFewSamples";
    case SpectralCurveError::TooManySamples:
        return "TooManySamples";
    case SpectralCurveError::NonFiniteSample:
        return "NonFiniteSample";
    case SpectralCurveError::NonIncreasingWavelength:
        return "NonIncreasingWavelength";
    case SpectralCurveError::NegativeValue:
        return "NegativeValue";
    }
    return "UnknownSpectralCurveError";
}

SpectralCurveError checkSpectralCurve(const SpectralCurve& curve) noexcept
{
    return checkSamples(curve.samples);
}

double evaluateSpectralCurve(const SpectralCurve& curve, const double wavelengthNm) noexcept
{
    if (checkSamples(curve.samples) != SpectralCurveError::None)
    {
        return 0.0;
    }
    return evaluateSamples(curve.samples, wavelengthNm);
}

const char* responseProfileKindName(const ResponseProfileKind kind) noexcept
{
    switch (kind)
    {
    case ResponseProfileKind::ColorSensor:
        return "ColorSensor";
    case ResponseProfileKind::MonoSensor:
        return "MonoSensor";
    case ResponseProfileKind::MonoSensorRgbFilters:
        return "MonoSensorRgbFilters";
    }
    return "UnknownResponseProfileKind";
}

const char* observationModelErrorName(const ObservationModelError error) noexcept
{
    switch (error)
    {
    case ObservationModelError::None:
        return "None";
    case ObservationModelError::InvalidObserverHeight:
        return "InvalidObserverHeight";
    case ObservationModelError::InvalidPressure:
        return "InvalidPressure";
    case ObservationModelError::InvalidAirmass:
        return "InvalidAirmass";
    case ObservationModelError::InvalidCenterAltitude:
        return "InvalidCenterAltitude";
    case ObservationModelError::InvalidPassband:
        return "InvalidPassband";
    }
    return "UnknownObservationModelError";
}

ObservationModelError checkObservationModel(const ObservationModel& model) noexcept
{
    if (model.atmosphere.enabled)
    {
        const AtmosphericExtinction& atmosphere = model.atmosphere;
        if (!std::isfinite(atmosphere.observerHeightMeters) || atmosphere.observerHeightMeters < -500.0)
        {
            return ObservationModelError::InvalidObserverHeight;
        }
        if (!positiveFinite(atmosphere.pressureHectopascals))
        {
            return ObservationModelError::InvalidPressure;
        }
        if (atmosphere.airmass.has_value() && !positiveFinite(*atmosphere.airmass))
        {
            return ObservationModelError::InvalidAirmass;
        }
        if (atmosphere.centerAltitudeDegrees.has_value() &&
            (!std::isfinite(*atmosphere.centerAltitudeDegrees) || *atmosphere.centerAltitudeDegrees <= 0.0 ||
             *atmosphere.centerAltitudeDegrees > 90.0))
        {
            return ObservationModelError::InvalidCenterAltitude;
        }
    }
    if (model.narrowband.enabled)
    {
        for (const NarrowbandPassband& passband : model.narrowband.passbands)
        {
            if (!std::isfinite(passband.centerNm) || !positiveFinite(passband.bandwidthNm))
            {
                return ObservationModelError::InvalidPassband;
            }
        }
    }
    return ObservationModelError::None;
}

double airmassFromZenithAngle(const double zenithAngleDegrees) noexcept
{
    const double radians = zenithAngleDegrees * (std::numbers::pi / 180.0);
    const double cosine = std::cos(radians);
    const double cosine2 = cosine * cosine;
    const double cosine3 = cosine2 * cosine;
    const double numerator = (1.002432 * cosine2) + (0.148386 * cosine) + 0.0096467;
    const double denominator = cosine3 + (0.149864 * cosine2) + (0.0102963 * cosine) + 0.000303978;
    return numerator / denominator;
}

double resolvedAirmass(const AtmosphericExtinction& atmosphere) noexcept
{
    if (atmosphere.airmass.has_value() && positiveFinite(*atmosphere.airmass))
    {
        return *atmosphere.airmass;
    }
    if (atmosphere.centerAltitudeDegrees.has_value() && std::isfinite(*atmosphere.centerAltitudeDegrees) &&
        *atmosphere.centerAltitudeDegrees > 0.0 && *atmosphere.centerAltitudeDegrees <= 90.0)
    {
        return airmassFromZenithAngle(90.0 - *atmosphere.centerAltitudeDegrees);
    }
    return airmassFromZenithAngle(kDefaultZenithAngleDegrees);
}

double atmosphericTransmittance(const double wavelengthNm, const AtmosphericExtinction& atmosphere) noexcept
{
    const double micrometres = wavelengthNm / 1000.0;
    const double heightKilometres = atmosphere.observerHeightMeters / 1000.0;
    const double pressure = localPressureHectopascals(atmosphere);
    const double pressureTerm = pressure / kStandardPressureHectopascals;
    const double heightTerm = 0.00864 + (6.5e-6 * heightKilometres);
    const double exponent = -(3.916 + (0.074 * micrometres) + (0.050 / micrometres));
    const double opticalDepth = pressureTerm * heightTerm * std::pow(micrometres, exponent);
    return std::exp(-opticalDepth * resolvedAirmass(atmosphere));
}

Result<PreparedResponse> prepareResponse(const ExecutionContext& execution, const ResponseProfile& profile,
                                         const ObservationModel& observation, const SpectralGrid& grid)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "preparing a response needs an execution context", kPrepareOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!isResponseProfileKind(profile.kind))
    {
        return failure(ErrorCode::InvalidArgument, "the profile kind is not one this build reads", kPrepareOperation);
    }
    if (!usableGrid(grid) || grid.sampleCount < kMinimumAkimaSamples)
    {
        return failure(ErrorCode::InvalidArgument,
                       "the grid needs a finite positive step and at least five samples for the interpolant",
                       kPrepareOperation);
    }
    if (const ObservationModelError refused = checkObservationModel(observation);
        refused != ObservationModelError::None)
    {
        return failure(ErrorCode::InvalidArgument,
                       std::string("the observation model was refused: ") + observationModelErrorName(refused),
                       kPrepareOperation);
    }
    if (observation.narrowband.enabled && profile.kind == ResponseProfileKind::MonoSensorRgbFilters)
    {
        return failure(ErrorCode::Unsupported,
                       "a narrowband integration needs a sensor-only monochrome profile; an RGB filter set is "
                       "described for broadband use",
                       kPrepareOperation);
    }
    for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        if (const SpectralCurveError refused = checkSamples(profile.channels[channel].samples);
            refused != SpectralCurveError::None)
        {
            return failure(ErrorCode::InvalidArgument,
                           std::string("a channel curve was refused: ") + spectralCurveErrorName(refused),
                           kPrepareOperation);
        }
    }

    PreparedResponse response;
    response.grid = grid;
    response.profileIdentifier = profile.identifier;
    response.profileVersion = profile.version;
    response.kind = profile.kind;
    response.wavelengthsNm.resize(grid.sampleCount);
    for (std::uint32_t index = 0u; index < grid.sampleCount; ++index)
    {
        response.wavelengthsNm[index] = spectralGridWavelengthNm(grid, index);
    }
    for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        resampleSamples(profile.channels[channel].samples, grid, response.channelWeights[channel]);
    }
    response.atmosphereApplied = observation.atmosphere.enabled && profile.kind == ResponseProfileKind::ColorSensor;
    if (response.atmosphereApplied)
    {
        response.airmass = resolvedAirmass(observation.atmosphere);
        double maximum = 0.0;
        for (const double wavelength : response.wavelengthsNm)
        {
            maximum = (std::max)(maximum, atmosphericTransmittance(wavelength, observation.atmosphere));
        }
        response.atmosphereNormalization = maximum > 0.0 ? maximum : 1.0;
        for (std::uint32_t index = 0u; index < grid.sampleCount; ++index)
        {
            const double multiplier = atmosphericTransmittance(response.wavelengthsNm[index], observation.atmosphere) /
                                      response.atmosphereNormalization;
            for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
            {
                response.channelWeights[channel][index] *= multiplier;
            }
        }
    }
    for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        WavelengthInterval& interval = response.integrationIntervals[channel];
        interval.startNm = grid.startNm;
        interval.endNm = grid.endNm;
        if (observation.narrowband.enabled)
        {
            const NarrowbandPassband& passband = observation.narrowband.passbands[channel];
            const double halfWidth = passband.bandwidthNm * 0.5;
            interval.startNm = (std::max)(passband.centerNm - halfWidth, grid.startNm);
            interval.endNm = (std::min)(passband.centerNm + halfWidth, grid.endNm);
        }
    }
    return response;
}

Result<std::vector<double>> resampleSpectralCurve(const ExecutionContext& execution, const SpectralCurve& curve,
                                                  const SpectralGrid& grid)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "resampling a curve needs an execution context", kResampleOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const SpectralCurveError refused = checkSamples(curve.samples); refused != SpectralCurveError::None)
    {
        return failure(ErrorCode::InvalidArgument,
                       std::string("the curve was refused: ") + spectralCurveErrorName(refused), kResampleOperation);
    }
    if (!usableGrid(grid))
    {
        return failure(ErrorCode::InvalidArgument, "the grid needs samples and a finite positive step",
                       kResampleOperation);
    }
    std::vector<double> values;
    resampleSamples(curve.samples, grid, values);
    return values;
}

Result<SpectralCurve> composeSpectralCurves(const ExecutionContext& execution,
                                            const std::span<const SpectralCurve> components, std::string identifier,
                                            std::string version, const SpectralGrid& grid)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "composing curves needs an execution context", kComposeOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (components.empty())
    {
        return failure(ErrorCode::EmptySelection, "a composition needs at least one curve", kComposeOperation);
    }
    if (!usableGrid(grid))
    {
        return failure(ErrorCode::InvalidArgument, "the grid needs samples and a finite positive step",
                       kComposeOperation);
    }
    std::vector<double> product(grid.sampleCount, 1.0);
    std::vector<double> values;
    for (const SpectralCurve& component : components)
    {
        if (const SpectralCurveError refused = checkSamples(component.samples); refused != SpectralCurveError::None)
        {
            return failure(ErrorCode::InvalidArgument,
                           std::string("a component was refused: ") + spectralCurveErrorName(refused),
                           kComposeOperation);
        }
        resampleSamples(component.samples, grid, values);
        for (std::uint32_t index = 0u; index < grid.sampleCount; ++index)
        {
            product[index] *= values[index];
        }
    }
    SpectralCurve curve;
    curve.identifier = std::move(identifier);
    curve.version = std::move(version);
    curve.samples.resize(grid.sampleCount);
    for (std::uint32_t index = 0u; index < grid.sampleCount; ++index)
    {
        curve.samples[index] =
            SpectralSample{.wavelengthNm = spectralGridWavelengthNm(grid, index), .value = product[index]};
    }
    return curve;
}

Result<SpectralCurve> combineFilterResponses(const SpectralCurve& sensor, const SpectralCurve& sensorWithFilter,
                                             const SpectralCurve& sensorWithWindow, std::string identifier,
                                             std::string version)
{
    const std::array<const SpectralCurve*, 3u> curves{&sensor, &sensorWithFilter, &sensorWithWindow};
    double overlapStart = -std::numeric_limits<double>::infinity();
    double overlapEnd = std::numeric_limits<double>::infinity();
    std::size_t total = 2u;
    for (const SpectralCurve* const curve : curves)
    {
        if (const SpectralCurveError refused = checkSamples(curve->samples); refused != SpectralCurveError::None)
        {
            return failure(ErrorCode::InvalidArgument,
                           std::string("a curve was refused: ") + spectralCurveErrorName(refused), kCombineOperation);
        }
        overlapStart = (std::max)(overlapStart, curve->samples.front().wavelengthNm);
        overlapEnd = (std::min)(overlapEnd, curve->samples.back().wavelengthNm);
        total += curve->samples.size();
    }
    if (!std::isfinite(overlapStart) || !std::isfinite(overlapEnd) ||
        overlapStart + kWavelengthToleranceNm >= overlapEnd)
    {
        return failure(ErrorCode::InvalidData, "the three curves share no wavelength interval", kCombineOperation);
    }
    std::vector<double> wavelengths;
    wavelengths.reserve(total);
    wavelengths.push_back(overlapStart);
    wavelengths.push_back(overlapEnd);
    for (const SpectralCurve* const curve : curves)
    {
        for (const SpectralSample& sample : curve->samples)
        {
            double wavelength = sample.wavelengthNm;
            if (std::abs(wavelength - overlapStart) <= kWavelengthToleranceNm)
            {
                wavelength = overlapStart;
            }
            else if (std::abs(wavelength - overlapEnd) <= kWavelengthToleranceNm)
            {
                wavelength = overlapEnd;
            }
            if (wavelength >= overlapStart && wavelength <= overlapEnd)
            {
                wavelengths.push_back(wavelength);
            }
        }
    }
    std::sort(wavelengths.begin(), wavelengths.end());
    const auto uniqueEnd =
        std::unique(wavelengths.begin(), wavelengths.end(), [](const double left, const double right) {
            return std::abs(left - right) <= kWavelengthToleranceNm;
        });
    wavelengths.erase(uniqueEnd, wavelengths.end());
    if (wavelengths.size() < kMinimumSpectralCurveSamples)
    {
        return failure(ErrorCode::InvalidData, "the shared wavelength grid holds fewer than two points",
                       kCombineOperation);
    }
    SpectralCurve curve;
    curve.identifier = std::move(identifier);
    curve.version = std::move(version);
    curve.samples.reserve(wavelengths.size());
    for (const double wavelength : wavelengths)
    {
        const double base = evaluateSamples(sensor.samples, wavelength);
        const double value = base <= kSensorFloor ? 0.0
                                                  : (evaluateSamples(sensorWithFilter.samples, wavelength) *
                                                     evaluateSamples(sensorWithWindow.samples, wavelength)) /
                                                        base;
        if (!std::isfinite(value) || value < 0.0)
        {
            return failure(ErrorCode::InvalidData,
                           "the combined response has a sample that is not finite and "
                           "non-negative",
                           kCombineOperation);
        }
        curve.samples.push_back(SpectralSample{.wavelengthNm = wavelength, .value = value});
    }
    if (const SpectralCurveError refused = checkSamples(curve.samples); refused != SpectralCurveError::None)
    {
        return failure(ErrorCode::InvalidData,
                       std::string("the combined response was refused: ") + spectralCurveErrorName(refused),
                       kCombineOperation);
    }
    return curve;
}

const char* syntheticPhotometryStatusName(const SyntheticPhotometryStatus status) noexcept
{
    switch (status)
    {
    case SyntheticPhotometryStatus::Integrated:
        return "Integrated";
    case SyntheticPhotometryStatus::MissingSource:
        return "MissingSource";
    case SyntheticPhotometryStatus::InvalidSpectrum:
        return "InvalidSpectrum";
    case SyntheticPhotometryStatus::NonPositiveGreen:
        return "NonPositiveGreen";
    case SyntheticPhotometryStatus::NonFinite:
        return "NonFinite";
    }
    return "UnknownSyntheticPhotometryStatus";
}

Result<SyntheticPhotometry> integrateSyntheticPhotometry(const ExecutionContext& execution,
                                                         const PreparedResponse& response,
                                                         const std::span<const float> flux,
                                                         const std::span<const float> fluxError)
{
    if (const Status checked = checkIntegrationInputs(execution, response, kIntegrateOperation); checked.failed())
    {
        return checked;
    }
    const std::size_t count = response.grid.sampleCount;
    if (flux.size() != count)
    {
        return failure(ErrorCode::InvalidArgument, "the spectrum holds one flux per grid sample", kIntegrateOperation);
    }
    if (!fluxError.empty() && fluxError.size() != count)
    {
        return failure(ErrorCode::InvalidArgument, "the uncertainties are empty or one per grid sample",
                       kIntegrateOperation);
    }
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, integrationScratchDoubles(count) * sizeof(double), alignof(double), kIntegrateOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    SyntheticPhotometry out;
    integrateOne(response, flux, fluxError, integrationScratchOver(static_cast<double*>(memory->data()), count), out);
    return out;
}

Result<SyntheticPhotometryBatchResult> integrateSyntheticPhotometryTable(const ExecutionContext& execution,
                                                                         const PreparedResponse& response,
                                                                         const std::span<const float> flux,
                                                                         const std::span<const float> fluxError,
                                                                         const std::span<SyntheticPhotometry> out)
{
    if (const Status checked = checkIntegrationInputs(execution, response, kTableOperation); checked.failed())
    {
        return checked;
    }
    if (out.empty())
    {
        return failure(ErrorCode::EmptySelection, "a batch needs at least one spectrum", kTableOperation);
    }
    if (out.size() > kMaxSyntheticPhotometryTargets)
    {
        return limitFailure("the spectrum count exceeds the stated ceiling", kTableOperation, out.size(),
                            kMaxSyntheticPhotometryTargets);
    }
    const std::size_t count = response.grid.sampleCount;
    if (flux.size() != out.size() * count)
    {
        return failure(ErrorCode::InvalidArgument, "the table holds one spectrum of the grid's length per prediction",
                       kTableOperation);
    }
    if (!fluxError.empty() && fluxError.size() != flux.size())
    {
        return failure(ErrorCode::InvalidArgument, "the uncertainty table is empty or the flux table's size",
                       kTableOperation);
    }
    const bool propagate = !fluxError.empty();
    const auto spectrumOf = [&](const std::size_t index, std::span<const float>& spectrum,
                                std::span<const float>& error) {
        spectrum = flux.subspan(index * count, count);
        error = propagate ? fluxError.subspan(index * count, count) : std::span<const float>{};
        return true;
    };
    return runBatch(execution, response, out, spectrumOf, kTableOperation);
}

Result<SyntheticPhotometryBatchResult> integrateSyntheticPhotometryBatch(
    const ExecutionContext& execution, const PreparedResponse& response, const SpectrumCatalog& catalog,
    const std::span<const std::uint64_t> sourceIdentifiers, const std::span<SyntheticPhotometry> out)
{
    if (const Status checked = checkIntegrationInputs(execution, response, kBatchOperation); checked.failed())
    {
        return checked;
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, "a catalog batch needs a built spectrum catalog", kBatchOperation);
    }
    if (!(response.grid == catalog.grid()))
    {
        return failure(ErrorCode::InvalidArgument, "the prepared response is not on the catalog's grid",
                       kBatchOperation);
    }
    if (sourceIdentifiers.empty())
    {
        return failure(ErrorCode::EmptySelection, "a batch needs at least one identifier", kBatchOperation);
    }
    if (sourceIdentifiers.size() > kMaxSyntheticPhotometryTargets)
    {
        return limitFailure("the identifier count exceeds the stated ceiling", kBatchOperation,
                            sourceIdentifiers.size(), kMaxSyntheticPhotometryTargets);
    }
    if (out.size() != sourceIdentifiers.size())
    {
        return failure(ErrorCode::IncompatibleArguments, "the output holds exactly one prediction per identifier",
                       kBatchOperation);
    }
    const auto spectrumOf = [&](const std::size_t index, std::span<const float>& spectrum,
                                std::span<const float>& error) {
        const std::optional<std::uint32_t> record = catalog.findSourceIdentifier(sourceIdentifiers[index]);
        if (!record.has_value())
        {
            return false;
        }
        spectrum = catalog.flux(*record);
        error = catalog.fluxError(*record);
        return true;
    };
    return runBatch(execution, response, out, spectrumOf, kBatchOperation);
}

Result<std::array<double, 3u>> integrateWhiteReference(const ExecutionContext& execution,
                                                       const PreparedResponse& response,
                                                       const WhiteReferenceSpectrum& whiteReference)
{
    if (const Status checked = checkIntegrationInputs(execution, response, kWhiteOperation); checked.failed())
    {
        return checked;
    }
    if (const SpectralCurveError refused = checkSamples(whiteReference.samples); refused != SpectralCurveError::None)
    {
        return failure(ErrorCode::InvalidArgument,
                       std::string("the white reference was refused: ") + spectralCurveErrorName(refused),
                       kWhiteOperation);
    }
    const std::size_t count = response.grid.sampleCount;
    std::vector<double> source;
    resampleSamples(whiteReference.samples, response.grid, source);
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, integrationScratchDoubles(count) * sizeof(double), alignof(double), kWhiteOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    const IntegrationScratch scratch = integrationScratchOver(static_cast<double*>(memory->data()), count);
    std::array<double, 3u> counts{};
    for (std::size_t channel = 0u; channel < kColorChannelCount; ++channel)
    {
        double* const row = scratch.products + channel * count;
        for (std::size_t index = 0u; index < count; ++index)
        {
            const double photonWeight = whiteReference.photonWeighted ? 1.0 : response.wavelengthsNm[index];
            row[index] = source[index] * photonWeight * response.channelWeights[channel][index];
        }
        counts[channel] = integrateRow(response, std::span<const double>{row, count}, channel, scratch.akima);
    }
    return counts;
}

std::array<double, 3u> whiteReferenceGains(const std::array<double, 3u>& counts) noexcept
{
    std::array<double, 3u> gains{1.0, 1.0, 1.0};
    const double green = counts[1];
    for (std::size_t channel = 0u; channel < gains.size(); ++channel)
    {
        const double count = counts[channel];
        if (positiveFinite(green) && positiveFinite(count))
        {
            gains[channel] = green / count;
        }
    }
    return gains;
}

Result<std::array<double, 3u>> whiteReferenceResponseGains(const ExecutionContext& execution,
                                                           const PreparedResponse& response,
                                                           const WhiteReferenceSpectrum& whiteReference)
{
    Result<std::array<double, 3u>> counts = integrateWhiteReference(execution, response, whiteReference);
    if (!counts.hasValue())
    {
        return counts.status();
    }
    for (const double count : *counts)
    {
        if (!positiveFinite(count))
        {
            return failure(ErrorCode::InvalidData, "the white reference measures as a non-positive count in a channel",
                           kWhiteGainsOperation);
        }
    }
    return whiteReferenceGains(*counts);
}

const char* responseCompatibilityVerdictName(const ResponseCompatibilityVerdict verdict) noexcept
{
    switch (verdict)
    {
    case ResponseCompatibilityVerdict::Compatible:
        return "Compatible";
    case ResponseCompatibilityVerdict::MissingIdentity:
        return "MissingIdentity";
    case ResponseCompatibilityVerdict::UnreviewedAsset:
        return "UnreviewedAsset";
    case ResponseCompatibilityVerdict::IncompatibleColorSpace:
        return "IncompatibleColorSpace";
    case ResponseCompatibilityVerdict::RawWhiteBalanceNotModelled:
        return "RawWhiteBalanceNotModelled";
    case ResponseCompatibilityVerdict::CameraMatrixNotModelled:
        return "CameraMatrixNotModelled";
    }
    return "UnknownResponseCompatibilityVerdict";
}

ResponseCompatibilityVerdict checkResponseProfileCompatibility(const ResponseProfile& profile,
                                                               const ResponseProfileCapabilities& capabilities,
                                                               const FrameColorState& frame) noexcept
{
    if (profile.identifier.empty() || profile.version.empty())
    {
        return ResponseCompatibilityVerdict::MissingIdentity;
    }
    if (!capabilities.reviewed)
    {
        return ResponseCompatibilityVerdict::UnreviewedAsset;
    }
    if (std::find(capabilities.compatibleColorSpaces.begin(), capabilities.compatibleColorSpaces.end(),
                  frame.colorSpace) == capabilities.compatibleColorSpaces.end())
    {
        return ResponseCompatibilityVerdict::IncompatibleColorSpace;
    }
    if (frame.rawWhiteBalanceApplied && !capabilities.modelsRawWhiteBalance)
    {
        return ResponseCompatibilityVerdict::RawWhiteBalanceNotModelled;
    }
    if (frame.cameraMatrixApplied && !capabilities.modelsCameraMatrix)
    {
        return ResponseCompatibilityVerdict::CameraMatrixNotModelled;
    }
    return ResponseCompatibilityVerdict::Compatible;
}

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the spectrophotometric gain fit: the repeated-median line per colour
// channel, its clipping policies, its verdicts and its diagnostics.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h>

#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/spectrophotometry/SpectrophotometryInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using spectrophotometry_detail::failure;
using spectrophotometry_detail::limitFailure;

constexpr const char* kOperation = "an::fitSpectrophotometricGains";
constexpr std::size_t kRedChannel = 0u;
constexpr std::size_t kGreenChannel = 1u;
constexpr std::size_t kBlueChannel = 2u;

/// One usable sample as one colour channel's fit sees it.
struct Observation
{
    /// The ratio the catalog predicts.
    double catalogRatio = 0.0;

    /// The ratio the image measured.
    double imageRatio = 0.0;

    /// The sample's position in the supplied span.
    std::uint32_t sampleIndex = 0u;
};

/// A line through the observations.
struct Line
{
    /// The image ratio at a catalog ratio of zero.
    double intercept = 0.0;

    /// Image ratio per unit of catalog ratio.
    double slope = 0.0;
};

/// Whether a value is a finite positive number.
///
/// @param value the value.
/// @return true when finite and above zero.
[[nodiscard]] bool positiveFinite(const double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// Whether every field of a request is inside its stated range.
///
/// @param request the request.
/// @return success, or the first field that refused.
[[nodiscard]] Status validateRequest(const SpectrophotometricFitRequest& request)
{
    if (request.minimumSamples == 0u)
    {
        return failure(ErrorCode::InvalidArgument, "the sample floor is at least one", kOperation);
    }
    if (!std::isfinite(request.minimumCatalogRatioSpan) || request.minimumCatalogRatioSpan < 0.0)
    {
        return failure(ErrorCode::InvalidArgument, "the catalog span floor is finite and not negative", kOperation);
    }
    if (!positiveFinite(request.clipSigmas))
    {
        return failure(ErrorCode::InvalidArgument, "the clipping width is finite and positive", kOperation);
    }
    if (request.maximumIterations == 0u)
    {
        return failure(ErrorCode::InvalidArgument, "the iteration ceiling is at least one", kOperation);
    }
    if (!positiveFinite(request.minimumRobustScale))
    {
        return failure(ErrorCode::InvalidArgument, "the robust-scale floor is finite and positive", kOperation);
    }
    if (!positiveFinite(request.maximumRelativeGainUncertainty))
    {
        return failure(ErrorCode::InvalidArgument, "the gain-uncertainty ceiling is finite and positive", kOperation);
    }
    if (request.lowSupportSamples < request.minimumSamples)
    {
        return failure(ErrorCode::InvalidArgument, "the low-support count is at or above the sample floor", kOperation);
    }
    for (const double gain : request.whiteReferenceGains)
    {
        if (!positiveFinite(gain))
        {
            return failure(ErrorCode::InvalidArgument, "the white-reference gains are finite and positive", kOperation);
        }
    }
    if (!isSpectrophotometricOutlierPolicy(request.outlierPolicy))
    {
        return failure(ErrorCode::InvalidArgument, "the outlier policy is not one this build implements", kOperation);
    }
    return Status::success();
}

/// Whether a sample is accepted and carries six finite positive ratios.
///
/// @param sample the sample.
/// @return true when the fit may use it.
[[nodiscard]] bool usableSample(const SpectrophotometricSample& sample) noexcept
{
    if (!sample.accepted)
    {
        return false;
    }
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        if (!positiveFinite(sample.imageRelativeToGreen[channel]) ||
            !positiveFinite(sample.catalogRelativeToGreen[channel]))
        {
            return false;
        }
    }
    return true;
}

/// The exact median of a non-empty range this call may reorder.
///
/// The upper middle by partition; for an even count the largest value below it by a scan, so the
/// answer is the value an ascending sort would place there whatever the permutation.
///
/// @param values the values, reordered by this call. Not empty.
/// @return the median.
[[nodiscard]] double selectMedian(const std::span<double> values) noexcept
{
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    double result = values[middle];
    if ((values.size() % 2u) == 0u)
    {
        const auto lower = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
        result = (*lower + result) * 0.5;
    }
    return result;
}

/// The exact median of a copy of a range.
///
/// @param values the values, which are only read.
/// @param scratch storage at least as long as `values`, reordered by this call.
/// @return the median, or a not-a-number for an empty range.
[[nodiscard]] double medianOf(const std::span<const double> values, const std::span<double> scratch) noexcept
{
    if (values.empty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::copy(values.begin(), values.end(), scratch.begin());
    return selectMedian(scratch.first(values.size()));
}

/// The span of the usable samples' catalog ratios in one channel.
///
/// @param observations the channel's observations.
/// @return the largest minus the smallest, or zero for none.
[[nodiscard]] double catalogSpan(const std::span<const Observation> observations) noexcept
{
    if (observations.empty())
    {
        return 0.0;
    }
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (const Observation& observation : observations)
    {
        low = (std::min)(low, observation.catalogRatio);
        high = (std::max)(high, observation.catalogRatio);
    }
    if (!std::isfinite(low) || !std::isfinite(high))
    {
        return 0.0;
    }
    return high - low;
}

/// The positions of the active observations.
///
/// @param active one flag per observation.
/// @return the positions, ascending.
[[nodiscard]] std::vector<std::uint32_t> activePositions(const std::vector<std::uint8_t>& active)
{
    std::vector<std::uint32_t> positions;
    positions.reserve(active.size());
    for (std::size_t index = 0u; index < active.size(); ++index)
    {
        if (active[index] != 0u)
        {
            positions.push_back(static_cast<std::uint32_t>(index));
        }
    }
    return positions;
}

/// The positions a line is fitted through: every active one, or an evenly spaced subset in
/// catalog-ratio order above the exact-fit ceiling.
///
/// @param observations the channel's observations.
/// @param active one flag per observation.
/// @return the positions.
[[nodiscard]] std::vector<std::uint32_t> fitPositions(const std::span<const Observation> observations,
                                                      const std::vector<std::uint8_t>& active)
{
    std::vector<std::uint32_t> positions = activePositions(active);
    if (positions.size() <= kSpectrophotometricExactFitPointLimit)
    {
        return positions;
    }
    std::sort(positions.begin(), positions.end(), [&observations](const std::uint32_t left, const std::uint32_t right) {
        return observations[left].catalogRatio < observations[right].catalogRatio;
    });
    std::vector<std::uint32_t> sampled;
    sampled.reserve(kSpectrophotometricExactFitPointLimit);
    const double step =
        static_cast<double>(positions.size() - 1u) / static_cast<double>(kSpectrophotometricExactFitPointLimit - 1u);
    std::size_t previousRank = std::numeric_limits<std::size_t>::max();
    for (std::size_t sample = 0u; sample < kSpectrophotometricExactFitPointLimit; ++sample)
    {
        const auto rank = static_cast<std::size_t>(std::llround(static_cast<double>(sample) * step));
        if (rank != previousRank)
        {
            sampled.push_back(positions[(std::min)(rank, positions.size() - 1u)]);
            previousRank = rank;
        }
    }
    return sampled;
}

/// Worker-private scratch for the repeated-median line, and the line's points laid out for it.
struct FitScratch
{
    /// `slots * capacity` doubles of pairwise slopes.
    double* slopes = nullptr;

    /// `capacity` doubles: the catalog ratio of each point a line goes through, in point order.
    double* abscissae = nullptr;

    /// `capacity` doubles: the image ratio of each point a line goes through, in point order.
    double* ordinates = nullptr;

    /// How many doubles each slot owns.
    std::size_t capacity = 0u;

    /// How many slots there are.
    std::uint32_t slots = 0u;
};

/// The finite slopes from one point to every point of a line's set, packed at the front of `slopes` in
/// point order.
///
/// Every quotient is formed first and the ones that are not finite dropped after, so the divisions run as
/// vectors. With finite ratios a quotient is not finite exactly when the pair has no slope: the point with
/// itself, and a point at the same catalog ratio, divide by zero, and a quotient too large to represent is
/// no slope either. Dropping them in order leaves the sequence the median is selected from as it was.
///
/// @param abscissae the set's catalog ratios.
/// @param ordinates the set's image ratios.
/// @param count how many points the set has.
/// @param anchorX the point's catalog ratio.
/// @param anchorY the point's image ratio.
/// @param slopes `count` doubles, overwritten.
/// @return how many slopes were kept.
[[nodiscard]] std::size_t pairwiseSlopes(const double* const abscissae, const double* const ordinates,
                                         const std::size_t count, const double anchorX, const double anchorY,
                                         double* const slopes) noexcept
{
    for (std::size_t other = 0u; other < count; ++other)
    {
        slopes[other] = (ordinates[other] - anchorY) / (abscissae[other] - anchorX);
    }
    std::size_t kept = 0u;
    for (std::size_t other = 0u; other < count; ++other)
    {
        const double slope = slopes[other];
        slopes[kept] = slope;
        kept += static_cast<std::size_t>(std::abs(slope) <= (std::numeric_limits<double>::max)());
    }
    return kept;
}

/// The repeated-median line through a set of points, on the operation's threads.
///
/// Each point's median pairwise slope is independent of every other point's, so the points are
/// partitioned across workers and the medians are taken in point order afterwards; the answer is
/// the same at any thread count.
///
/// @param execution the operation.
/// @param observations the channel's observations.
/// @param positions the points the line goes through.
/// @param scratch the worker scratch.
/// @return the line, absent when no slope or no intercept is finite, or a failure.
[[nodiscard]] Result<std::optional<Line>> repeatedMedianLine(const ExecutionContext& execution,
                                                             const std::span<const Observation> observations,
                                                             const std::span<const std::uint32_t> positions,
                                                             const FitScratch& scratch)
{
    if (positions.size() < 2u)
    {
        return std::optional<Line>{};
    }
    std::vector<double> pointMedians(positions.size(), std::numeric_limits<double>::quiet_NaN());
    for (std::size_t point = 0u; point < positions.size(); ++point)
    {
        scratch.abscissae[point] = observations[positions[point]].catalogRatio;
        scratch.ordinates[point] = observations[positions[point]].imageRatio;
    }
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(positions.size()) * sizeof(double),
                        .operationsPerItem = static_cast<std::uint64_t>(positions.size()) * 4u};
    const auto body = [&](const WorkerTask task) -> Status {
        double* const slopes = scratch.slopes + static_cast<std::size_t>(task.workerSlot) * scratch.capacity;
        for (std::uint64_t point = task.range.begin; point < task.range.end; ++point)
        {
            const std::size_t count = pairwiseSlopes(scratch.abscissae, scratch.ordinates, positions.size(),
                                                     scratch.abscissae[point], scratch.ordinates[point], slopes);
            if (count > 0u)
            {
                pointMedians[point] = selectMedian(std::span<double>{slopes, count});
            }
        }
        return Status::success();
    };
    if (const Status ran = parallelForWorkers(execution, positions.size(), cost, body); ran.failed())
    {
        return ran;
    }

    std::vector<double> medians;
    medians.reserve(positions.size());
    for (const double median : pointMedians)
    {
        if (std::isfinite(median))
        {
            medians.push_back(median);
        }
    }
    if (medians.empty())
    {
        return std::optional<Line>{};
    }
    Line line;
    line.slope = selectMedian(medians);
    std::vector<double> intercepts;
    intercepts.reserve(positions.size());
    for (const std::uint32_t position : positions)
    {
        const double intercept = observations[position].imageRatio - (line.slope * observations[position].catalogRatio);
        if (std::isfinite(intercept))
        {
            intercepts.push_back(intercept);
        }
    }
    if (intercepts.empty())
    {
        return std::optional<Line>{};
    }
    line.intercept = selectMedian(intercepts);
    if (!std::isfinite(line.intercept) || !std::isfinite(line.slope))
    {
        return std::optional<Line>{};
    }
    return std::optional<Line>{line};
}

/// The line's value at a catalog ratio.
///
/// @param line the line.
/// @param catalogRatio where.
/// @return the image ratio the line predicts.
[[nodiscard]] double evaluateLine(const Line& line, const double catalogRatio) noexcept
{
    return line.intercept + (line.slope * catalogRatio);
}

/// The membership after clipping the active residuals about a line.
///
/// @param observations the channel's observations.
/// @param active the current membership.
/// @param line the line.
/// @param request the clipping rule.
/// @param scratch storage at least as long as the observations.
/// @param next receives the new membership.
/// @return how many were kept.
[[nodiscard]] std::size_t clipMembership(const std::span<const Observation> observations,
                                         const std::vector<std::uint8_t>& active, const Line& line,
                                         const SpectrophotometricFitRequest& request, std::vector<double>& scratch,
                                         std::vector<std::uint8_t>& next)
{
    std::size_t residuals = 0u;
    for (std::size_t index = 0u; index < observations.size(); ++index)
    {
        if (active[index] != 0u)
        {
            scratch[residuals++] =
                std::abs(observations[index].imageRatio - evaluateLine(line, observations[index].catalogRatio));
        }
    }
    const double median = residuals == 0u ? 0.0 : selectMedian(std::span<double>{scratch.data(), residuals});
    const double sigma = (std::max)(median * kMedianAbsoluteDeviationToSigma, request.minimumRobustScale);
    const double threshold = request.clipSigmas * sigma;
    std::size_t kept = 0u;
    next.assign(observations.size(), 0u);
    for (std::size_t index = 0u; index < observations.size(); ++index)
    {
        const double residual = observations[index].imageRatio - evaluateLine(line, observations[index].catalogRatio);
        const bool keep = std::abs(residual) <= threshold;
        next[index] = keep ? 1u : 0u;
        if (keep)
        {
            ++kept;
        }
    }
    return kept;
}

/// Fits one colour channel: the line under the policy, its value at the white reference, the raw
/// gain, the uncertainties and the residual statistics.
///
/// @param execution the operation.
/// @param observations the channel's observations.
/// @param request the request.
/// @param whiteReferenceCatalogRatio the white reference's catalog ratio in this channel.
/// @param scratch the worker scratch.
/// @param out receives the channel's fit; `fitted` says whether it succeeded.
/// @return success, or a failure of the execution.
[[nodiscard]] Status fitChannel(const ExecutionContext& execution, const std::span<const Observation> observations,
                                const SpectrophotometricFitRequest& request, const double whiteReferenceCatalogRatio,
                                const FitScratch& scratch, SpectrophotometricChannelFit& out)
{
    out = SpectrophotometricChannelFit{};
    out.whiteReferenceCatalogRatio = whiteReferenceCatalogRatio;
    if (observations.size() < request.minimumSamples)
    {
        return Status::success();
    }
    std::vector<std::uint8_t> active(observations.size(), 1u);
    std::vector<std::uint8_t> next;
    std::vector<double> residualScratch(observations.size(), 0.0);
    std::optional<Line> line;

    if (request.outlierPolicy == SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask)
    {
        const std::vector<std::uint32_t> positions = fitPositions(observations, active);
        Result<std::optional<Line>> fitted = repeatedMedianLine(execution, observations, positions, scratch);
        if (!fitted.hasValue())
        {
            return fitted.status();
        }
        line = *fitted;
        if (!line.has_value())
        {
            return Status::success();
        }
        (void)clipMembership(observations, active, *line, request, residualScratch, next);
        active = next;
    }
    else
    {
        for (std::uint32_t iteration = 0u; iteration < request.maximumIterations; ++iteration)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
            const std::vector<std::uint32_t> positions = fitPositions(observations, active);
            if (positions.size() < request.minimumSamples)
            {
                break;
            }
            Result<std::optional<Line>> fitted = repeatedMedianLine(execution, observations, positions, scratch);
            if (!fitted.hasValue())
            {
                return fitted.status();
            }
            line = *fitted;
            if (!line.has_value())
            {
                break;
            }
            const std::size_t kept = clipMembership(observations, active, *line, request, residualScratch, next);
            if (kept < request.minimumSamples || next == active)
            {
                break;
            }
            active = next;
        }
    }

    const std::vector<std::uint32_t> finalPositions = fitPositions(observations, active);
    if (finalPositions.size() < request.minimumSamples)
    {
        return Status::success();
    }
    if (request.outlierPolicy != SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask)
    {
        Result<std::optional<Line>> fitted = repeatedMedianLine(execution, observations, finalPositions, scratch);
        if (!fitted.hasValue())
        {
            return fitted.status();
        }
        line = *fitted;
        if (!line.has_value())
        {
            return Status::success();
        }
    }

    const double fittedWhite = evaluateLine(*line, whiteReferenceCatalogRatio);
    const double rawGain = 1.0 / fittedWhite;
    const double observedToCatalogScale = fittedWhite / whiteReferenceCatalogRatio;
    out.intercept = line->intercept;
    out.slope = line->slope;
    if (!positiveFinite(whiteReferenceCatalogRatio) || !positiveFinite(fittedWhite) || !positiveFinite(rawGain) ||
        !positiveFinite(observedToCatalogScale))
    {
        return Status::success();
    }

    std::vector<double> logResiduals;
    std::vector<double> absoluteResiduals;
    logResiduals.reserve(finalPositions.size());
    absoluteResiduals.reserve(finalPositions.size());
    double squares = 0.0;
    for (std::size_t index = 0u; index < observations.size(); ++index)
    {
        if (active[index] == 0u)
        {
            out.rejectedSampleIndices.push_back(observations[index].sampleIndex);
            continue;
        }
        const double predicted = evaluateLine(*line, observations[index].catalogRatio);
        if (!positiveFinite(predicted))
        {
            out.rejectedSampleIndices.clear();
            return Status::success();
        }
        const double logResidual = std::log(observations[index].imageRatio / predicted);
        logResiduals.push_back(logResidual);
        absoluteResiduals.push_back(std::abs(observations[index].imageRatio - predicted));
        squares += logResidual * logResidual;
    }
    if (logResiduals.size() < request.minimumSamples)
    {
        out.rejectedSampleIndices.clear();
        return Status::success();
    }
    std::vector<double> medianScratch(logResiduals.size(), 0.0);
    const double medianResidual = medianOf(logResiduals, medianScratch);
    for (std::size_t index = 0u; index < logResiduals.size(); ++index)
    {
        medianScratch[index] = std::abs(logResiduals[index] - medianResidual);
    }
    const double madResidual = selectMedian(medianScratch);
    const double robustLogSigma = (std::max)(madResidual * kMedianAbsoluteDeviationToSigma, request.minimumRobustScale);
    const double linearSigma = (std::max)(medianOf(absoluteResiduals, medianScratch) * kMedianAbsoluteDeviationToSigma,
                                          request.minimumRobustScale);
    const double inliers = static_cast<double>(logResiduals.size());
    const double fitUncertainty = linearSigma / std::sqrt(inliers);
    const double logUncertainty = robustLogSigma / std::sqrt(inliers);

    out.fitted = true;
    out.fittedWhiteReferenceImageRatio = fittedWhite;
    out.observedToCatalogScale = observedToCatalogScale;
    out.rawGain = rawGain;
    out.gain = rawGain;
    out.scaleUncertainty = observedToCatalogScale * logUncertainty;
    out.gainUncertainty = std::abs(rawGain) * (fitUncertainty / fittedWhite);
    out.medianLogResidual = medianResidual;
    out.rmsLogResidual = std::sqrt(squares / inliers);
    out.madLogResidual = madResidual;
    out.inlierCount = static_cast<std::uint32_t>(logResiduals.size());
    out.rejectedCount = static_cast<std::uint32_t>(observations.size() - logResiduals.size());
    std::sort(out.rejectedSampleIndices.begin(), out.rejectedSampleIndices.end());
    return Status::success();
}

/// A channel's relative gain uncertainty, infinite when the gain is not usable.
///
/// @param channel the channel's fit.
/// @return the ratio.
[[nodiscard]] double relativeGainUncertainty(const SpectrophotometricChannelFit& channel) noexcept
{
    if (!positiveFinite(channel.gain) || !std::isfinite(channel.gainUncertainty))
    {
        return std::numeric_limits<double>::infinity();
    }
    return std::abs(channel.gainUncertainty / channel.gain);
}

} // namespace

const char* spectrophotometricOutlierPolicyName(const SpectrophotometricOutlierPolicy policy) noexcept
{
    switch (policy)
    {
    case SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit:
        return "IterativeSigmaClipRefit";
    case SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask:
        return "SinglePassRepeatedMedianMask";
    }
    return "UnknownSpectrophotometricOutlierPolicy";
}

const char* spectrophotometricFitVerdictName(const SpectrophotometricFitVerdict verdict) noexcept
{
    switch (verdict)
    {
    case SpectrophotometricFitVerdict::Accepted:
        return "Accepted";
    case SpectrophotometricFitVerdict::InsufficientSamples:
        return "InsufficientSamples";
    case SpectrophotometricFitVerdict::InsufficientCatalogSpan:
        return "InsufficientCatalogSpan";
    case SpectrophotometricFitVerdict::InsufficientInliers:
        return "InsufficientInliers";
    case SpectrophotometricFitVerdict::ExcessiveGainUncertainty:
        return "ExcessiveGainUncertainty";
    case SpectrophotometricFitVerdict::NonFiniteFit:
        return "NonFiniteFit";
    }
    return "UnknownSpectrophotometricFitVerdict";
}

Result<SpectrophotometricFitResult> fitSpectrophotometricGains(const ExecutionContext& execution,
                                                               const std::span<const SpectrophotometricSample> samples,
                                                               const SpectrophotometricFitRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, "a spectrophotometric fit needs an execution context", kOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status valid = validateRequest(request); valid.failed())
    {
        return valid;
    }
    if (samples.size() > kMaxSpectrophotometricFitSamples)
    {
        return limitFailure("the sample count exceeds the stated ceiling", kOperation, samples.size(),
                            kMaxSpectrophotometricFitSamples);
    }

    SpectrophotometricFitResult result{};
    result.suppliedCount = static_cast<std::uint32_t>(samples.size());
    std::vector<Observation> red;
    std::vector<Observation> blue;
    red.reserve(samples.size());
    blue.reserve(samples.size());
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        const SpectrophotometricSample& sample = samples[index];
        if (sample.accepted)
        {
            ++result.acceptedCount;
        }
        if (!usableSample(sample))
        {
            continue;
        }
        red.push_back(Observation{.catalogRatio = sample.catalogRelativeToGreen[kRedChannel],
                                  .imageRatio = sample.imageRelativeToGreen[kRedChannel],
                                  .sampleIndex = static_cast<std::uint32_t>(index)});
        blue.push_back(Observation{.catalogRatio = sample.catalogRelativeToGreen[kBlueChannel],
                                   .imageRatio = sample.imageRelativeToGreen[kBlueChannel],
                                   .sampleIndex = static_cast<std::uint32_t>(index)});
    }
    result.usableCount = static_cast<std::uint32_t>(red.size());
    result.catalogRedRatioSpan = catalogSpan(red);
    result.catalogBlueRatioSpan = catalogSpan(blue);
    result.red.whiteReferenceCatalogRatio =
        request.whiteReferenceGains[kGreenChannel] / request.whiteReferenceGains[kRedChannel];
    result.blue.whiteReferenceCatalogRatio =
        request.whiteReferenceGains[kGreenChannel] / request.whiteReferenceGains[kBlueChannel];
    if (result.usableCount < request.minimumSamples)
    {
        result.verdict = SpectrophotometricFitVerdict::InsufficientSamples;
        return result;
    }
    if (result.catalogRedRatioSpan < request.minimumCatalogRatioSpan ||
        result.catalogBlueRatioSpan < request.minimumCatalogRatioSpan)
    {
        result.verdict = SpectrophotometricFitVerdict::InsufficientCatalogSpan;
        return result;
    }

    const std::size_t points = (std::min)(static_cast<std::size_t>(result.usableCount),
                                          static_cast<std::size_t>(kSpectrophotometricExactFitPointLimit));
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(points) * sizeof(double),
                        .operationsPerItem = static_cast<std::uint64_t>(points) * 4u};
    FitScratch scratch;
    scratch.slots = (std::max)(1u, plannedWorkerCount(execution, points, cost));
    scratch.capacity = points;
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, (static_cast<std::uint64_t>(scratch.slots) + 2u) * scratch.capacity * sizeof(double),
        alignof(double), kOperation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    scratch.slopes = static_cast<double*>(memory->data());
    scratch.abscissae = scratch.slopes + static_cast<std::size_t>(scratch.slots) * scratch.capacity;
    scratch.ordinates = scratch.abscissae + scratch.capacity;

    if (const Status fitted =
            fitChannel(execution, red, request, result.red.whiteReferenceCatalogRatio, scratch, result.red);
        fitted.failed())
    {
        return fitted;
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (const Status fitted =
            fitChannel(execution, blue, request, result.blue.whiteReferenceCatalogRatio, scratch, result.blue);
        fitted.failed())
    {
        return fitted;
    }
    if (!result.red.fitted || !result.blue.fitted)
    {
        result.verdict = SpectrophotometricFitVerdict::InsufficientInliers;
        return result;
    }

    const std::array<double, 3u> rawGains{result.red.rawGain, 1.0, result.blue.rawGain};
    for (const double gain : rawGains)
    {
        if (!positiveFinite(gain))
        {
            result.verdict = SpectrophotometricFitVerdict::NonFiniteFit;
            return result;
        }
    }
    const double largest = (std::max)((std::max)(rawGains[0], rawGains[1]), rawGains[2]);
    if (!positiveFinite(largest))
    {
        result.verdict = SpectrophotometricFitVerdict::NonFiniteFit;
        return result;
    }
    if (relativeGainUncertainty(result.red) > request.maximumRelativeGainUncertainty ||
        relativeGainUncertainty(result.blue) > request.maximumRelativeGainUncertainty)
    {
        result.verdict = SpectrophotometricFitVerdict::ExcessiveGainUncertainty;
        return result;
    }

    const double normalization = 1.0 / largest;
    result.gains = {rawGains[0] * normalization, rawGains[1] * normalization, rawGains[2] * normalization};
    result.red.gain = result.gains[kRedChannel];
    result.blue.gain = result.gains[kBlueChannel];
    result.red.gainUncertainty *= normalization;
    result.blue.gainUncertainty *= normalization;
    result.rejectedSampleIndices.reserve(result.red.rejectedSampleIndices.size() +
                                         result.blue.rejectedSampleIndices.size());
    result.rejectedSampleIndices.insert(result.rejectedSampleIndices.end(), result.red.rejectedSampleIndices.begin(),
                                        result.red.rejectedSampleIndices.end());
    result.rejectedSampleIndices.insert(result.rejectedSampleIndices.end(), result.blue.rejectedSampleIndices.begin(),
                                        result.blue.rejectedSampleIndices.end());
    std::sort(result.rejectedSampleIndices.begin(), result.rejectedSampleIndices.end());
    result.rejectedSampleIndices.erase(
        std::unique(result.rejectedSampleIndices.begin(), result.rejectedSampleIndices.end()),
        result.rejectedSampleIndices.end());
    result.supportCount = (std::min)(result.red.inlierCount, result.blue.inlierCount);
    result.lowSupport = result.supportCount < request.lowSupportSamples;
    result.verdict = SpectrophotometricFitVerdict::Accepted;
    if (result.lowSupport)
    {
        execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Algorithms, kOperation,
                                   "the fit is supported by fewer inliers than the stated comfort count; confirm the "
                                   "catalog coverage, raise the magnitude limit or use a wider plate-solved field if "
                                   "the gains are unstable");
    }
    return result;
}

} // namespace an

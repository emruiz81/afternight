// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the six noise distributions, drawn from a stream keyed on the coordinate.
//
// Every draw is `positionalUniformDraw` of `core/PositionalRandom.h`, keyed on the request's seed,
// the sample's column, row and channel and the draw's own index, so a sample's noise depends on
// nothing another sample did and the rows can be split across workers in any order. The image is
// read through the spatial filters' shared channel loop: each channel is presented as a Float32
// plane - read where it lies when the storage allows, gathered otherwise - the noisy value is
// computed in double and stored as Float32, and the scatter converts it to the destination
// representation under the request's write-back policy.

#include <AfterNightEngine/Algorithms/Synthesis/NoiseGeneration.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "core/PositionalRandom.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/SpatialPlane.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::generateImageNoise";

/// How much work one row of the generator is.
///
/// @param width how many samples one row holds.
/// @param perSample how many operations each sample costs.
/// @return the cost.
[[nodiscard]] TaskCost rowCost(const std::uint32_t width, const std::uint64_t perSample) noexcept
{
    const auto samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * sizeof(float), .operationsPerItem = samples * perSample};
}

/// Whether an output policy names a rule this operation implements.
///
/// @param output the policy.
/// @return success, or why the policy is not usable.
[[nodiscard]] Status checkOutputPolicy(const SpatialOutputPolicy& output)
{
    if (!isOutputPrecisionContract(output.contract))
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "the output contract is not one this engine knows");
    }
    const Status policy = checkConversionPolicy(output.writeBack, kOperation);
    if (policy.failed())
    {
        return policy;
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return spatial_detail::spatialFailure(
            ErrorCode::InvalidArgument, kOperation,
            "a noise generator carries its samples across as the numbers they are; a full-scale domain mapping "
            "would make it a generator and a domain change at once");
    }
    return Status::success();
}

/// A draw from the standard normal distribution.
///
/// The polar-free Box-Muller transform over two uniform draws of the site's own stream. The first
/// draw is pushed off zero by the smallest step that keeps the logarithm finite, which is what the
/// half-open interval the uniform draw produces would otherwise cost at exactly one site in every
/// nine thousand million million.
///
/// @param seed the request's seed.
/// @param x the sample column.
/// @param y the sample row.
/// @param channel the sample channel.
/// @param index which pair of draws at this site.
/// @return one standard normal deviate.
[[nodiscard]] double standardNormalDraw(const std::uint64_t seed, const std::uint32_t x, const std::uint32_t y,
                                        const std::uint32_t channel, const std::uint64_t index) noexcept
{
    const double first = (std::max)(positionalUniformDraw(seed, x, y, channel, 2u * index), 0x1.0p-53);
    const double second = positionalUniformDraw(seed, x, y, channel, 2u * index + 1u);
    return std::sqrt(-2.0 * std::log(first)) * std::cos(2.0 * std::numbers::pi * second);
}

/// A draw from the Poisson distribution of a stated rate.
///
/// Knuth's multiplication below a rate of twelve, where its expected iteration count is the rate
/// itself, and Hoermann's transformed rejection above it, whose expected iteration count does not
/// grow with the rate at all. Both read the site's own stream, so the answer is a function of the
/// coordinate rather than of how many draws preceded it; the rejection loop advances the draw index
/// and is bounded by the same index, so an adversarial rate cannot make it run forever. Reaching
/// that bound, which both methods' acceptance rates make vanishingly rare, answers the count reached
/// by multiplication and the rate's floor by rejection.
///
/// @param rate the distribution's mean, finite, not negative and at most `kMaxPoissonRate`.
/// @param seed the request's seed.
/// @param x the sample column.
/// @param y the sample row.
/// @param channel the sample channel.
/// @return one Poisson deviate.
[[nodiscard]] double poissonDraw(const double rate, const std::uint64_t seed, const std::uint32_t x,
                                 const std::uint32_t y, const std::uint32_t channel) noexcept
{
    if (!(rate > 0.0))
    {
        return 0.0;
    }
    if (rate < 12.0)
    {
        const double limit = std::exp(-rate);
        double product = 1.0;
        double count = 0.0;
        for (std::uint64_t index = 0u; index < 256u; ++index)
        {
            product *= positionalUniformDraw(seed, x, y, channel, index);
            if (product <= limit)
            {
                return count;
            }
            count += 1.0;
        }
        return count;
    }

    // Hoermann's PTRD: a transformed rejection whose hat is the squared hyperbolic secant, with the
    // squeeze that accepts most candidates without the logarithm of a factorial.
    const double smu = std::sqrt(rate);
    const double b = 0.931 + 2.53 * smu;
    const double a = -0.059 + 0.02483 * b;
    const double inverseAlpha = 1.1239 + 1.1328 / (b - 3.4);
    const double vr = 0.9277 - 3.6224 / (b - 2.0);
    const double logRate = std::log(rate);
    for (std::uint64_t index = 0u; index < 256u; ++index)
    {
        const double u = positionalUniformDraw(seed, x, y, channel, 2u * index) - 0.5;
        const double v = positionalUniformDraw(seed, x, y, channel, 2u * index + 1u);
        const double us = 0.5 - std::abs(u);
        const double k = std::floor((2.0 * a / us + b) * u + rate + 0.43);
        if (us >= 0.07 && v <= vr)
        {
            return k;
        }
        if (k < 0.0 || (us < 0.013 && v > us))
        {
            continue;
        }
        const double scaled = std::log(v * inverseAlpha / (a / (us * us) + b));
        // Stirling's series for the logarithm of the factorial, to the order the method requires.
        const double kp1 = k + 1.0;
        const double logFactorial = (kp1 - 0.5) * std::log(kp1) - kp1 + 0.5 * std::log(2.0 * std::numbers::pi) +
                                    1.0 / (12.0 * kp1) - 1.0 / (360.0 * kp1 * kp1 * kp1);
        if (scaled <= k * logRate - rate - logFactorial)
        {
            return k;
        }
    }
    return std::floor(rate);
}

/// The answer for one sample of one kind.
///
/// @param request the validated request.
/// @param value the stored sample.
/// @param x the sample column.
/// @param y the sample row.
/// @param channel the sample channel.
/// @return the noisy value, before any clip.
[[nodiscard]] double noisyValue(const NoiseGenerationRequest& request, const double value, const std::uint32_t x,
                                const std::uint32_t y, const std::uint32_t channel) noexcept
{
    const double deviation = std::sqrt(request.variance);
    switch (request.kind)
    {
    case NoiseKind::Gaussian:
        return value + request.mean + deviation * standardNormalDraw(request.seed, x, y, channel, 0u);
    case NoiseKind::Speckle:
        return value + value * (request.mean + deviation * standardNormalDraw(request.seed, x, y, channel, 0u));
    case NoiseKind::Poisson:
        return poissonDraw(value * request.poissonScale, request.seed, x, y, channel) / request.poissonScale;
    case NoiseKind::Salt:
        return positionalUniformDraw(request.seed, x, y, channel, 0u) < request.amount ? request.highValue : value;
    case NoiseKind::Pepper:
        return positionalUniformDraw(request.seed, x, y, channel, 0u) < request.amount ? request.lowValue : value;
    case NoiseKind::SaltAndPepper:
        if (positionalUniformDraw(request.seed, x, y, channel, 0u) >= request.amount)
        {
            return value;
        }
        return positionalUniformDraw(request.seed, x, y, channel, 1u) < request.saltFraction ? request.highValue
                                                                                             : request.lowValue;
    }
    return value;
}

} // namespace

const char* noiseKindName(const NoiseKind kind) noexcept
{
    switch (kind)
    {
    case NoiseKind::Gaussian:
        return "gaussian";
    case NoiseKind::Poisson:
        return "poisson";
    case NoiseKind::Salt:
        return "salt";
    case NoiseKind::Pepper:
        return "pepper";
    case NoiseKind::SaltAndPepper:
        return "salt-and-pepper";
    case NoiseKind::Speckle:
        return "speckle";
    }
    return "unknown";
}

NoiseGenerationPlan::NoiseGenerationPlan(const NoiseGenerationRequest& request) noexcept : m_request(request)
{
    switch (request.kind)
    {
    case NoiseKind::Gaussian:
    case NoiseKind::Speckle:
    case NoiseKind::SaltAndPepper:
        m_drawsPerSample = 2u;
        break;
    case NoiseKind::Poisson:
    case NoiseKind::Salt:
    case NoiseKind::Pepper:
        m_drawsPerSample = 1u;
        break;
    }
}

const NoiseGenerationRequest& NoiseGenerationPlan::request() const noexcept
{
    return m_request;
}

std::uint32_t NoiseGenerationPlan::drawsPerSample() const noexcept
{
    return m_drawsPerSample;
}

std::uint64_t NoiseGenerationPlan::imagePasses() const noexcept
{
    return 1u;
}

Result<NoiseGenerationPlan> planNoiseGeneration(const NoiseGenerationRequest& request)
{
    if (!isNoiseKind(request.kind))
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "the noise kind is not one this build knows");
    }
    if (!std::isfinite(request.mean) || !std::isfinite(request.variance) || !std::isfinite(request.amount) ||
        !std::isfinite(request.saltFraction) || !std::isfinite(request.lowValue) || !std::isfinite(request.highValue) ||
        !std::isfinite(request.poissonScale))
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "every noise parameter must be finite");
    }
    if (request.variance < 0.0)
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "a noise variance must not be negative");
    }
    if (request.amount < 0.0 || request.amount > 1.0 || request.saltFraction < 0.0 || request.saltFraction > 1.0)
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "a replaced fraction and a salt fraction are fractions of the whole");
    }
    if (!(request.lowValue < request.highValue))
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "the pepper value must lie below the salt value");
    }
    if (!(request.poissonScale > 0.0))
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "a Poisson scale turns a sample into a count and must be above zero");
    }
    const double highestRate = request.highValue * request.poissonScale;
    if (request.kind == NoiseKind::Poisson && highestRate > kMaxPoissonRate)
    {
        return spatial_detail::spatialLimitFailure(
            ErrorCode::OutOfRange, kOperation, "the largest Poisson rate this request could ask for is above the limit",
            static_cast<std::uint64_t>(highestRate), static_cast<std::uint64_t>(kMaxPoissonRate));
    }
    const Status output = checkOutputPolicy(request.output);
    if (output.failed())
    {
        return output;
    }
    return NoiseGenerationPlan(request);
}

Result<NoiseGenerationResult> generateImageNoise(const ExecutionContext& execution, const SampleType sampleType,
                                                 const void* const source, const ImageLayout& sourceLayout,
                                                 void* const destination, const ImageLayout& destinationLayout,
                                                 const NoiseGenerationPlan& plan, const SpatialSelection& selection)
{
    spatial_detail::SpatialGeometry geometry{};
    const Status ready =
        spatial_detail::checkSpatialImages(execution, sampleType, source, sourceLayout, destination, destinationLayout,
                                           plan.request().output, selection, kOperation, geometry);
    if (ready.failed())
    {
        return ready;
    }
    const Result<NoiseGenerationPlan> revalidated = planNoiseGeneration(plan.request());
    if (!revalidated.hasValue())
    {
        return revalidated.status();
    }
    if (revalidated->drawsPerSample() != plan.drawsPerSample())
    {
        return spatial_detail::spatialFailure(ErrorCode::InvalidArgument, kOperation,
                                              "the plan was not produced by planNoiseGeneration");
    }

    NoiseGenerationResult result{};
    SpatialFilterResult& filter = result.filter;
    filter.horizontalRadius = 0u;
    filter.verticalRadius = 0u;
    filter.imagePasses = plan.imagePasses();
    filter.precision = ComputePrecision::Float32;
    filter.plannedWorkers = execution.admittedConcurrency();
    const NoiseGenerationRequest& request = plan.request();
    const bool keepsRejected = selection.validity.rule() != ValidityRule::AllValid;
    std::atomic<std::uint64_t> clipped{0u};
    std::uint64_t written = 0u;
    std::uint32_t channelIndex = 0u;

    const Status generated = spatial_detail::filterEveryChannel(
        execution, sampleType, source, sourceLayout, destination, destinationLayout, selection,
        request.output.writeBack, geometry, kOperation, filter,
        [&](PlaneView<const float> input, PlaneView<float> output, const KernelValidityView& validity) -> Status {
            const std::uint32_t channel = channelIndex;
            ++channelIndex;
            const std::uint32_t width = output.width();
            const Status walked =
                parallelFor(execution, output.height(), rowCost(width, 24u), [&](TaskRange range) -> Status {
                    std::uint64_t localClipped = 0u;
                    for (std::uint64_t y = range.begin; y < range.end; ++y)
                    {
                        const auto row = static_cast<std::uint32_t>(y);
                        const std::span<const float> original = *input.row(row);
                        const std::span<float> destinationRow = *output.row(row);
                        for (std::uint32_t x = 0u; x < width; ++x)
                        {
                            const double value = original[x];
                            // Noise put into a sample that means nothing is a number a later
                            // operation would read as if it meant something.
                            const bool means = !keepsRejected || (sampleIsValidByValue(validity.descriptor, value) &&
                                                                  plane_detail::maskAllows(validity, x, row));
                            if (!means)
                            {
                                destinationRow[x] = original[x];
                                continue;
                            }
                            const double noisy = noisyValue(request, value, x, row, channel);
                            double held = noisy;
                            if (request.clip)
                            {
                                held = (std::min)((std::max)(noisy, request.lowValue), request.highValue);
                                localClipped += held != noisy ? 1u : 0u;
                            }
                            destinationRow[x] = static_cast<float>(held);
                        }
                    }
                    clipped.fetch_add(localClipped, std::memory_order_relaxed);
                    return Status::success();
                });
            if (walked.failed())
            {
                return walked;
            }
            written += static_cast<std::uint64_t>(width) * output.height();
            return Status::success();
        },
        &result.samplesChanged);
    if (generated.failed())
    {
        return generated;
    }

    result.samplesClipped = clipped.load(std::memory_order_relaxed);
    const auto samples = static_cast<std::uint64_t>(geometry.width) * geometry.height * geometry.channelCount;
    filter.run.selection = ValidityCount{.valid = written, .total = samples};
    filter.run.outputsWritten = written;
    return result;
}

} // namespace an
